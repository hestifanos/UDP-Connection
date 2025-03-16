#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main() {
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
        // Wait for filename PDU from client
        n = recvfrom(sockfd, &spdu, sizeof(spdu), 0,
                     (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }

        // Expecting a filename request PDU: type 'C'
        if (spdu.type != 'C') {
            fprintf(stderr, "Received unexpected PDU type '%c'\n", spdu.type);
            continue;
        }

        // Extract the filename from spdu.data
        char filename[256];
        strncpy(filename, spdu.data, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';

        printf("Client requested file: %s\n", filename);

        // Open the file
        int fd = open(filename, O_RDONLY);
        if (fd < 0) {
            // File not found or can't open -> send ERROR PDU
            perror("Error opening file");
            struct pdu err_pdu;
            err_pdu.type = 'E';
            snprintf(err_pdu.data, MAX_DATA, "Cannot open '%s'", filename);
            sendto(sockfd, &err_pdu, sizeof(err_pdu.type) + strlen(err_pdu.data) + 1,
                   0, (struct sockaddr *)&client_addr, addr_len);
            continue;
        }

        // Determine file size using lseek
        off_t file_size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        // If the file is empty (0 bytes), send a FINAL PDU with no data
        if (file_size == 0) {
            struct pdu final_pdu;
            final_pdu.type = 'F';
            final_pdu.data[0] = '\0';
            sendto(sockfd, &final_pdu, sizeof(final_pdu.type) + 1, 0,
                   (struct sockaddr *)&client_addr, addr_len);
            close(fd);
            continue;
        }

        // Read file in chunks of up to MAX_DATA bytes
        while (1) {
            memset(spdu.data, 0, MAX_DATA);
            n = read(fd, spdu.data, MAX_DATA);
            if (n < 0) {
                perror("Error reading file");
                break;
            }
            if (n == 0) {
                // End of file
                break;
            }

            // Decide if this is the final chunk
            off_t current_offset = lseek(fd, 0, SEEK_CUR);
            if (current_offset >= file_size) {
                spdu.type = 'F'; // Final PDU
            } else {
                spdu.type = 'D'; // Data PDU
            }

            // Send PDU: type + n bytes of data
            if (sendto(sockfd, &spdu, sizeof(spdu.type) + n, 0,
                       (struct sockaddr *)&client_addr, addr_len) < 0) {
                perror("sendto error");
                break;
            }

            // If this was the final chunk, stop
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
