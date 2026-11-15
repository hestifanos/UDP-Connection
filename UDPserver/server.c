#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <sys/time.h>

#define SERVER_PORT 12345
#define MAX_DATA 100

struct pdu {
    char type;        // 'C' = client filename request
                      // 'D' = data
                      // 'F' = final data (end of file)
                      // 'E' = error
    char data[MAX_DATA];
};

static int valid_filename(const char *filename) {
    if (filename[0] == '\0' || strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return 0;
    }

    for (const unsigned char *ch = (const unsigned char *)filename; *ch; ch++) {
        if (!isalnum(*ch) && *ch != '.' && *ch != '_' && *ch != '-') {
            return 0;
        }
    }
    return 1;
}

static void send_error(int sockfd, const struct sockaddr_in *client_addr,
                       socklen_t addr_len, const char *message) {
    struct pdu error_pdu = {0};
    error_pdu.type = 'E';
    snprintf(error_pdu.data, sizeof(error_pdu.data), "%s", message);
    size_t packet_size = sizeof(error_pdu.type) + strlen(error_pdu.data) + 1;
    if (sendto(sockfd, &error_pdu, packet_size, 0,
               (const struct sockaddr *)client_addr, addr_len) < 0) {
        perror("sendto error response");
    }
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct pdu spdu;
    ssize_t n;

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind to server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(SERVER_PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UDP server listening on port %d...\n", SERVER_PORT);

    while (1) {
        memset(&spdu, 0, sizeof(spdu));
        addr_len = sizeof(client_addr);
        n = recvfrom(sockfd, &spdu, sizeof(spdu), 0,
                     (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }

        if (n < 2 || spdu.type != 'C') {
            fprintf(stderr, "Received an invalid file request.\n");
            continue;
        }

        size_t request_data_size = (size_t)n - sizeof(spdu.type);
        char *terminator = memchr(spdu.data, '\0', request_data_size);
        if (terminator == NULL || !valid_filename(spdu.data)) {
            send_error(sockfd, &client_addr, addr_len, "Invalid filename");
            continue;
        }

        char filename[MAX_DATA];
        snprintf(filename, sizeof(filename), "%s", spdu.data);
        printf("Client requested file: %s\n", filename);

        int fd = open(filename, O_RDONLY | O_NOFOLLOW);
        if (fd < 0) {
            perror("Error opening file");
            send_error(sockfd, &client_addr, addr_len, "File not available");
            continue;
        }

        struct stat file_info;
        if (fstat(fd, &file_info) < 0 || !S_ISREG(file_info.st_mode)) {
            fprintf(stderr, "Requested path is not a regular file.\n");
            close(fd);
            send_error(sockfd, &client_addr, addr_len, "File not available");
            continue;
        }
        off_t remaining = file_info.st_size;

        if (remaining == 0) {
            struct pdu final_pdu = {0};
            final_pdu.type = 'F';
            if (sendto(sockfd, &final_pdu, sizeof(final_pdu.type), 0,
                       (struct sockaddr *)&client_addr, addr_len) < 0) {
                perror("sendto");
            }
            close(fd);
            continue;
        }

        while (remaining > 0) {
            size_t chunk_size = remaining < MAX_DATA ? (size_t)remaining : MAX_DATA;
            n = read(fd, spdu.data, chunk_size);
            if (n < 0) {
                perror("Error reading file");
                break;
            }
            if (n == 0) {
                fprintf(stderr, "File changed during transfer.\n");
                break;
            }
            remaining -= n;

            spdu.type = remaining == 0 ? 'F' : 'D';

            if (sendto(sockfd, &spdu, sizeof(spdu.type) + n, 0,
                       (struct sockaddr *)&client_addr, addr_len) < 0) {
                perror("sendto error");
                break;
            }
            if (spdu.type == 'F') {
                break;
            }
        }

        close(fd);
        printf("Finished sending file: %s\n", filename);
    }

    close(sockfd);
    return 0;
}
