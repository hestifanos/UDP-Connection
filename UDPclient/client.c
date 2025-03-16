#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

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
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    struct pdu cpdu;
    ssize_t n;
    char choice[10];

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

    printf("UDP client started. Server port = %d\n", SERVER_PORT);

    while (1) {
        printf("\nEnter 'd' to download a file or 'q' to quit: ");
        fflush(stdout);
        if (scanf("%s", choice) != 1) {
            fprintf(stderr, "Error reading choice.\n");
            break;
        }

        if (choice[0] == 'q') {
            printf("Exiting client...\n");
            break;
        } else if (choice[0] == 'd') {
            // Prompt user for filename
            char filename[256];
            printf("Enter filename to download: ");
            fflush(stdout);
            if (scanf("%s", filename) != 1) {
                fprintf(stderr, "Error reading filename.\n");
                continue;
            }

            // Prepare and send FILENAME PDU (type 'C')
            memset(&cpdu, 0, sizeof(cpdu));
            cpdu.type = 'C';
            strncpy(cpdu.data, filename, MAX_DATA - 1);

            // Send the PDU to the server
            if (sendto(sockfd, &cpdu, sizeof(cpdu.type) + strlen(cpdu.data) + 1,
                       0, (struct sockaddr *)&server_addr, addr_len) < 0) {
                perror("sendto error");
                continue;
            }

            // Open local file for writing (in current directory)
            FILE *fp = fopen(filename, "wb");
            if (!fp) {
                perror("Error opening local file for writing");
                continue;
            }

            printf("Downloading '%s'...\n", filename);

            // Receive data until FINAL PDU or ERROR PDU
            while (1) {
                n = recvfrom(sockfd, &cpdu, sizeof(cpdu), 0, NULL, NULL);
                if (n < 0) {
                    perror("recvfrom error");
                    break;
                }

                if (cpdu.type == 'E') {
                    // Server reported an error
                    printf("Server error: %s\n", cpdu.data);
                    break;
                }

                // Number of bytes of actual file data = n - 1 (for the type field)
                size_t data_len = n - sizeof(cpdu.type);
                if (data_len > 0) {
                    fwrite(cpdu.data, 1, data_len, fp);
                }

                if (cpdu.type == 'F') {
                    // Final chunk received
                    printf("File '%s' downloaded successfully.\n", filename);
                    break;
                }
            }

            fclose(fp);

        } else {
            printf("Invalid option. Please try again.\n");
        }
    }

    close(sockfd);
    return 0;
}
