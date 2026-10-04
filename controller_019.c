#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9430
#define UDP_PORT 9530

int main()
{
    int sockfd;
    struct sockaddr_in server_addr;

    /* Step 1: Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    printf("Controller socket created successfully.\n");

    /* Step 2: Prepare Agent address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Step 3: Connect to Agent */
    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

printf("Connected to RemoteOps Agent on port %d!\n", PORT);

char buffer[1024];
char response[1024];

while (1)
{
    printf("RemoteOps> ");

    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
        break;
    }

    if (strncmp(buffer, "PUT ", 4) == 0)
{
    char filename[256];

    if (sscanf(buffer,
               "PUT %255s",
               filename) != 1)
    {
        printf("Usage: PUT <filename>\n");
        continue;
    }

    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        printf("Local file could not be opened.\n");
        continue;
    }

    /*
     * Find file size.
     */
    if (fseek(file, 0, SEEK_END) != 0)
    {
        printf("Unable to determine file size.\n");
        fclose(file);
        continue;
    }

    long file_size = ftell(file);

    if (file_size < 0)
    {
        printf("Unable to determine file size.\n");
        fclose(file);
        continue;
    }

    rewind(file);

    /*
     * Send PUT filename bytes command.
     */
    char put_command[512];

    snprintf(put_command,
             sizeof(put_command),
             "PUT %s %ld\n",
             filename,
             file_size);

    if (send(sockfd,
             put_command,
             strlen(put_command),
             0) < 0)
    {
        perror("send");
        fclose(file);
        break;
    }

    /*
     * Wait until Agent says READY.
     */
    memset(response, 0, sizeof(response));

    ssize_t bytes_received =
        recv(sockfd,
             response,
             sizeof(response) - 1,
             0);

    if (bytes_received <= 0)
    {
        printf("Agent disconnected.\n");
        fclose(file);
        break;
    }

    response[bytes_received] = '\0';

    if (strncmp(response, "READY", 5) != 0)
    {
        printf("%s", response);
        fclose(file);
        continue;
    }

    /*
     * Send the actual file bytes.
     */
    char file_buffer[4096];
    size_t bytes_read;

    while ((bytes_read =
                fread(file_buffer,
                      1,
                      sizeof(file_buffer),
                      file)) > 0)
    {
        size_t total_sent = 0;

        while (total_sent < bytes_read)
        {
            ssize_t sent =
                send(sockfd,
                     file_buffer + total_sent,
                     bytes_read - total_sent,
                     0);

            if (sent <= 0)
            {
                perror("send");
                fclose(file);
                return 1;
            }

            total_sent += (size_t)sent;
        }
    }

    fclose(file);

    /*
     * Receive final Agent response.
     */
    memset(response, 0, sizeof(response));

    bytes_received =
        recv(sockfd,
             response,
             sizeof(response) - 1,
             0);

    if (bytes_received <= 0)
    {
        printf("Agent disconnected.\n");
        break;
    }

    response[bytes_received] = '\0';

    printf("%s", response);

    continue;
}
    if (strncmp(buffer, "GET ", 4) == 0)
{
    char filename[256];

    if (sscanf(buffer,
               "GET %255s",
               filename) != 1)
    {
        printf("Usage: GET <filename>\n");
        continue;
    }





    /*
     * Send GET command to Agent.
     */
    if (send(sockfd,
             buffer,
             strlen(buffer),
             0) < 0)
    {
        perror("send");
        break;
    }

    /*
     * Receive GET header.
     */
    memset(response, 0, sizeof(response));

    ssize_t bytes_received =
        recv(sockfd,
             response,
             sizeof(response) - 1,
             0);

    if (bytes_received <= 0)
    {
        printf("Agent disconnected.\n");
        break;
    }

    response[bytes_received] = '\0';

    /*
     * Check whether Agent returned an error.
     */
    if (strncmp(response, "ERR ", 4) == 0)
    {
        printf("%s", response);
        continue;
    }

    long file_size;

    if (sscanf(response,
               "OK GET %*s %ld",
               &file_size) != 1)
    {
        printf("Invalid GET response from Agent.\n");
        continue;
    }

    printf("%s", response);
    /*
 * Tell Agent that Controller is now
 * ready to receive the file bytes.
 */
const char *ready_message = "READY\n";

if (send(sockfd,
         ready_message,
         strlen(ready_message),
         0) < 0)
{
    perror("send");
    break;
}


    /*
     * Save using a different local filename.
     */
    char local_filename[512];

    snprintf(local_filename,
             sizeof(local_filename),
             "downloaded_%s",
             filename);

    FILE *file = fopen(local_filename, "wb");

    if (file == NULL)
    {
        printf("Unable to create local file.\n");
        continue;
    }

    long total_received = 0;

    while (total_received < file_size)
    {
        char file_buffer[4096];

        long remaining =
            file_size - total_received;

        size_t amount_to_receive =
            remaining < (long)sizeof(file_buffer)
                ? (size_t)remaining
                : sizeof(file_buffer);

        ssize_t received =
            recv(sockfd,
                 file_buffer,
                 amount_to_receive,
                 0);

        if (received <= 0)
        {
            printf("File transfer failed.\n");
            break;
        }

        fwrite(file_buffer,
               1,
               (size_t)received,
               file);

        total_received += received;
    }

    fclose(file);

    if (total_received == file_size)
    {
        printf("Downloaded %s (%ld bytes)\n",
               local_filename,
               total_received);
    }
    else
    {
        printf("GET failed.\n");
        remove(local_filename);
    }

    continue;
}

   /* GET ENDS HERE */

/* ===== MONITOR START ===== */

if (strncmp(buffer, "MONITOR START ", 14) == 0)
{
    int udp_port;

    if (sscanf(buffer,
               "MONITOR START %d",
               &udp_port) != 1 ||
        udp_port < 1 ||
        udp_port > 65535)
    {
        printf("Usage: MONITOR START <udp_port>\n");
        continue;
    }

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_sock < 0)
    {
        perror("UDP socket");
        continue;
    }

    struct sockaddr_in udp_addr;

    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    udp_addr.sin_port = htons(udp_port);

    if (bind(udp_sock,
             (struct sockaddr *)&udp_addr,
             sizeof(udp_addr)) < 0)
    {
        perror("UDP bind");
        close(udp_sock);
        continue;
    }

    /*
     * Send MONITOR START to Agent using TCP.
     */
    if (send(sockfd,
             buffer,
             strlen(buffer),
             0) < 0)
    {
        perror("send");
        close(udp_sock);
        break;
    }

    memset(response, 0, sizeof(response));

    ssize_t monitor_received =
        recv(sockfd,
             response,
             sizeof(response) - 1,
             0);

    if (monitor_received <= 0)
    {
        printf("Agent disconnected.\n");
        close(udp_sock);
        break;
    }

    response[monitor_received] = '\0';

    printf("%s", response);

    /*
     * Receive one UDP monitoring packet for testing.
     */
    char udp_buffer[256];

    memset(udp_buffer, 0, sizeof(udp_buffer));

    ssize_t udp_received =
        recvfrom(udp_sock,
                 udp_buffer,
                 sizeof(udp_buffer) - 1,
                 0,
                 NULL,
                 NULL);

    if (udp_received > 0)
    {
        udp_buffer[udp_received] = '\0';

        printf("[UDP] %s\n", udp_buffer);
    }

    close(udp_sock);

    continue;
}

/* ===== MONITOR STOP ===== */

if (strcmp(buffer, "MONITOR STOP") == 0)
{
    if (send(sockfd,
             buffer,
             strlen(buffer),
             0) < 0)
    {
        perror("send");
        break;
    }

    memset(response, 0, sizeof(response));

    ssize_t monitor_received =
        recv(sockfd,
             response,
             sizeof(response) - 1,
             0);

    if (monitor_received <= 0)
    {
        printf("Agent disconnected.\n");
        break;
    }

    response[monitor_received] = '\0';

    printf("%s", response);

    continue;
}

/*  MONITOR ENDS */


/*  QUIT COMMAND */
if (strcmp(buffer, "QUIT") == 0)
{
    if (send(sockfd,
             buffer,
             strlen(buffer),
             0) < 0)
    {
        perror("send");
        break;
    }

    memset(response, 0, sizeof(response));

    ssize_t quit_received =
        recv(sockfd,
             response,
             sizeof(response) - 1,
             0);

    if (quit_received > 0)
    {
        response[quit_received] = '\0';
        printf("%s", response);
    }

    break;
}

/* Your existing normal command code */
if (send(sockfd,
         buffer,
         strlen(buffer),
         0) < 0)
{
    perror("send");
    break;
}

    memset(response, 0, sizeof(response));

ssize_t bytes_received =
    recv(sockfd,
         response,
         sizeof(response) - 1,
         0);

if (bytes_received <= 0)
{
    printf("Agent disconnected.\n");
    break;
}

response[bytes_received] = '\0';

printf("%s", response);

/*
 * LISTPROC is a multi-part response.
 * Continue receiving until END SID:9100 arrives.
 */
if (strncmp(buffer, "LISTPROC", 8) == 0 ||
    strncmp(buffer, "EXEC DISKFREE", 7) == 0)
{
    while (strstr(response, "END SID:9100") == NULL)
    {
        memset(response, 0, sizeof(response));

        bytes_received =
            recv(sockfd,
                 response,
                 sizeof(response) - 1,
                 0);

        if (bytes_received <= 0)
        {
            printf("Agent disconnected.\n");
            break;
        }

        response[bytes_received] = '\0';

        printf("%s", response);
    }
}

}
close(sockfd);

return 0;
}
