#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
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

static int read_input(const char *prompt, char *buffer, size_t size) {
    fputs(prompt, stdout);
    fflush(stdout);
    if (fgets(buffer, size, stdin) == NULL) {
        return 0;
    }

    size_t length = strcspn(buffer, "\n");
    if (buffer[length] == '\n') {
        buffer[length] = '\0';
        return 1;
    }

    if (!feof(stdin)) {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {
        }
        fprintf(stderr, "Input is too long.\n");
        return -1;
    }
    return 1;
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct pdu cpdu;
    ssize_t n;
    char choice[16];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address (assuming server is on localhost)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("UDP client started. Server port = %d\n", SERVER_PORT);

    while (1) {
        int input_status = read_input("\nEnter 'd' to download a file or 'q' to quit: ",
                                      choice, sizeof(choice));
        if (input_status == 0) {
            break;
        }
        if (input_status < 0) {
            continue;
        }

        if (strcmp(choice, "q") == 0) {
            printf("Exiting client...\n");
            break;
        } else if (strcmp(choice, "d") == 0) {
            char filename[MAX_DATA];
            input_status = read_input("Enter filename to download: ", filename, sizeof(filename));
            if (input_status == 0) {
                break;
            }
            if (input_status < 0) {
                continue;
            }
            if (!valid_filename(filename)) {
                fprintf(stderr, "Use a simple filename without directory separators.\n");
                continue;
            }

            memset(&cpdu, 0, sizeof(cpdu));
            cpdu.type = 'C';
            memcpy(cpdu.data, filename, strlen(filename) + 1);

            size_t request_size = sizeof(cpdu.type) + strlen(cpdu.data) + 1;
            if (send(sockfd, &cpdu, request_size, 0) != (ssize_t)request_size) {
                perror("send");
                continue;
            }

            FILE *fp = fopen(filename, "wb");
            if (!fp) {
                perror("fopen");
                continue;
            }

            printf("Downloading '%s'...\n", filename);
            int download_succeeded = 0;
            while (1) {
                n = recv(sockfd, &cpdu, sizeof(cpdu), 0);
                if (n < 0) {
                    perror("recv");
                    break;
                }
                if (n < (ssize_t)sizeof(cpdu.type)) {
                    fprintf(stderr, "Received an invalid response packet.\n");
                    break;
                }

                if (cpdu.type == 'E') {
                    size_t message_size = (size_t)n - sizeof(cpdu.type);
                    if (message_size >= sizeof(cpdu.data)) {
                        message_size = sizeof(cpdu.data) - 1;
                    }
                    cpdu.data[message_size] = '\0';
                    printf("Server error: %s\n", cpdu.data);
                    break;
                }

                if (cpdu.type != 'D' && cpdu.type != 'F') {
                    fprintf(stderr, "Received an unknown response packet.\n");
                    break;
                }

                size_t data_len = (size_t)n - sizeof(cpdu.type);
                if (data_len > sizeof(cpdu.data) ||
                    (data_len > 0 && fwrite(cpdu.data, 1, data_len, fp) != data_len)) {
                    fprintf(stderr, "Could not write the downloaded file.\n");
                    break;
                }
                if (cpdu.type == 'F') {
                    download_succeeded = 1;
                    break;
                }
            }

            if (fclose(fp) != 0) {
                perror("fclose");
                download_succeeded = 0;
            }
            if (download_succeeded) {
                printf("File '%s' downloaded successfully.\n", filename);
            } else {
                unlink(filename);
            }

        } else {
            printf("Invalid option. Please try again.\n");
        }
    }

    close(sockfd);
    return 0;
}
