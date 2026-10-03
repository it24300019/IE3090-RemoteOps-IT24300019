#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9430
#define AUTH_TOKEN "OPS-0019"
#define SID "9100"
#define BUFFER_SIZE 1024

void *handle_client(void *arg)
{
    int connfd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int authenticated = 0;
    ssize_t bytes_received;

    printf("Controller is being handled by a thread.\n");

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        bytes_received = recv(connfd,
                              buffer,
                              sizeof(buffer) - 1,
                              0);

        if (bytes_received <= 0)
        {
            printf("Controller disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';

        /* Remove newline from received command */
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received: %s\n", buffer);


        /* AUTH command */
        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char *token = buffer + 5;

            if (strcmp(token, AUTH_TOKEN) == 0)
            {
                authenticated = 1;

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "OK AUTHENTICATED SID:%s\n",
                         SID);

                send(connfd,
                     response,
                     strlen(response),
                     0);
            }
            else
            {
                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 001 AUTH_FAILED SID:%s\n",
                         SID);

                send(connfd,
                     response,
                     strlen(response),
                     0);
            }
        }

        /* Commands before authentication */
        else if (!authenticated)
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 001 AUTH_REQUIRED SID:%s\n",
                     SID);

            send(connfd,
                 response,
                 strlen(response),
                 0);
        }

        /* Temporary response for future commands */
        else
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 003 UNKNOWN_COMMAND SID:%s\n",
                     SID);

            send(connfd,
                 response,
                 strlen(response),
                 0);
        }
    }

    close(connfd);

    return NULL;
}





int main()
{
    int listenfd, connfd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len;

    /* Step 1: Create TCP socket */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    printf("Socket created successfully.\n");


    /* Step 2: Prepare server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);


    /* Step 3: Bind socket to port */
    if (bind(listenfd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("Bind successful.\n");


    /* Step 4: Listen for connections */
    if (listen(listenfd, 5) < 0)
    {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent listening on port %d...\n", PORT);


/* Step 5: Continuously accept Controllers */

while (1)
{
    client_len = sizeof(client_addr);

    connfd = accept(listenfd,
                    (struct sockaddr *)&client_addr,
                    &client_len);

    if (connfd < 0)
    {
        perror("accept");
        continue;
    }

    printf("Controller connected successfully!\n");

    /*
     * Allocate separate memory for this Controller's
     * socket descriptor.
     */
    int *client_socket = malloc(sizeof(int));

    if (client_socket == NULL)
    {
        perror("malloc");
        close(connfd);
        continue;
    }

    *client_socket = connfd;

    pthread_t thread_id;

    /*
     * Create a new thread for this Controller.
     */
    if (pthread_create(&thread_id,
                       NULL,
                       handle_client,
                       client_socket) != 0)
    {
        perror("pthread_create");
        close(connfd);
        free(client_socket);
        continue;
    }

    /*
     * We do not need to pthread_join() this thread.
     */
    pthread_detach(thread_id);
}

close(listenfd);

return 0;
/* Step 5: Continuously accept Controllers */

while (1)
{
    client_len = sizeof(client_addr);

    connfd = accept(listenfd,
                    (struct sockaddr *)&client_addr,
                    &client_len);

    if (connfd < 0)
    {
        perror("accept");
        continue;
    }

    printf("Controller connected successfully!\n");

    /*
     * Allocate separate memory for this Controller's
     * socket descriptor.
     */
    int *client_socket = malloc(sizeof(int));

    if (client_socket == NULL)
    {
        perror("malloc");
        close(connfd);
        continue;
    }

    *client_socket = connfd;

    pthread_t thread_id;

    /*
     * Create a new thread for this Controller.
     */
    if (pthread_create(&thread_id,
                       NULL,
                       handle_client,
                       client_socket) != 0)
    {
        perror("pthread_create");
        close(connfd);
        free(client_socket);
        continue;
    }

    /*
     * We do not need to pthread_join() this thread.
     */
    pthread_detach(thread_id);

}
close(listenfd);
return 0;
}
