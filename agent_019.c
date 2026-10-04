#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9430
#define UDP_PORT 9530
#define AUTH_TOKEN "OPS-0019"
#define SID "9100"
#define BUFFER_SIZE 1024

void write_log(const char *client_ip, const char *command)
{
    FILE *log_file;
    time_t now;
    struct tm *time_info;
    char timestamp[64];

    now = time(NULL);
    time_info = localtime(&now);

    strftime(timestamp,
             sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S",
             time_info);

    log_file = fopen("remoteops_IT24300019.log", "a");

    if (log_file == NULL)
    {
        perror("log file");
        return;
    }

    fprintf(log_file,
            "[%s] CLIENT:%s COMMAND:%s SID:%s\n",
            timestamp,
            client_ip,
            command,
            SID);

    fclose(log_file);
}

struct monitor_info
{
    char client_ip[INET_ADDRSTRLEN];
    int udp_port;
    volatile int *running;
};

void *send_udp_monitor(void *arg)
{
    struct monitor_info *info =
        (struct monitor_info *)arg;

    int udp_sock;
    struct sockaddr_in udp_addr;

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_sock < 0)
    {
        perror("UDP socket");
        free(info);
        return NULL;
    }

    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(info->udp_port);

    if (inet_pton(AF_INET,
                  info->client_ip,
                  &udp_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(udp_sock);
        free(info);
        return NULL;
    }

while (*(info->running))
{
    char message[256];

    double uptime = 0.0;
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;

    long mem_total = 0;
    long mem_available = 0;

    FILE *fp;

    /*
     * Read current system uptime.
     */
    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }

    /*
     * Read current CPU load averages.
     */
    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp,
               "%lf %lf %lf",
               &load1,
               &load5,
               &load15);

        fclose(fp);
    }

    /*
     * Read current memory information.
     */
    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL)
    {
        char line[256];

        while (fgets(line,
                     sizeof(line),
                     fp) != NULL)
        {
            if (sscanf(line,
                       "MemTotal: %ld kB",
                       &mem_total) == 1)
            {
                continue;
            }

            if (sscanf(line,
                       "MemAvailable: %ld kB",
                       &mem_available) == 1)
            {
                break;
            }
        }

        fclose(fp);
    }

    long mem_used =
        mem_total - mem_available;

    snprintf(message,
             sizeof(message),
             "SYSINFO UPTIME:%.0f "
             "LOAD:%.2f,%.2f,%.2f "
             "MEM_USED:%ldkB "
             "MEM_TOTAL:%ldkB "
             "SID:%s",
             uptime,
             load1,
             load5,
             load15,
             mem_used,
             mem_total,
             SID);

    sendto(udp_sock,
           message,
           strlen(message),
           0,
           (struct sockaddr *)&udp_addr,
           sizeof(udp_addr));

    sleep(1);
}



    close(udp_sock);
    free(info);

    return NULL;
}
struct client_info
{
    int connfd;
    struct sockaddr_in client_addr;
};

void *handle_client(void *arg)
{
    struct client_info *info =
    (struct client_info *)arg;

int connfd = info->connfd;

struct sockaddr_in client_addr =
    info->client_addr;

free(info);

char client_ip[INET_ADDRSTRLEN];

if (inet_ntop(AF_INET,
              &client_addr.sin_addr,
              client_ip,
              sizeof(client_ip)) == NULL)
{
    perror("inet_ntop");
    close(connfd);
    return NULL;
}

printf("Controller IP: %s\n", client_ip);


char buffer[BUFFER_SIZE];
int authenticated = 0;
ssize_t bytes_received;

/* UDP monitoring state for this Controller */
volatile int monitor_running = 0;
pthread_t monitor_thread;



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
	write_log(client_ip, buffer);


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


/*QUIT FUNCTION*/

else if (strcmp(buffer, "QUIT") == 0)
{
    char response[BUFFER_SIZE];

    snprintf(response,
             sizeof(response),
             "OK BYE SID:%s\n",
             SID);

    send(connfd,
         response,
         strlen(response),
         0);

    break;
}



/*SYSINFO BEGIN*/

	else if (strcmp(buffer, "SYSINFO") == 0)
{
    char response[BUFFER_SIZE];
    char hostname[256];
    struct utsname system_info;

    FILE *fp;
    double uptime = 0.0;
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;

    long mem_total = 0;
    long mem_available = 0;

    /* 1. Get hostname */
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "UNKNOWN");
    }

    /* 2. Get OS and kernel information */
    if (uname(&system_info) != 0)
    {
        strcpy(system_info.sysname, "UNKNOWN");
        strcpy(system_info.release, "UNKNOWN");
    }

    /* 3. Get system uptime */
    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }

    /* 4. Get CPU load averages */
    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf %lf %lf",
               &load1,
               &load5,
               &load15);

        fclose(fp);
    }

    /* 5. Get memory information */
    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL)
    {
        char line[256];

        while (fgets(line, sizeof(line), fp) != NULL)
        {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
            {
                continue;
            }

            if (sscanf(line,
                       "MemAvailable: %ld kB",
                       &mem_available) == 1)
            {
                break;
            }
        }

        fclose(fp);
    }

    snprintf(response,
             sizeof(response),
             "OK SYSINFO\n"
             "HOSTNAME: %s\n"
             "OS: %s\n"
             "KERNEL: %s\n"
             "UPTIME: %.0f seconds\n"
             "LOAD: %.2f %.2f %.2f\n"
             "MEMORY_TOTAL: %ld kB\n"
             "MEMORY_AVAILABLE: %ld kB\n"
             "SID:%s\n",
             hostname,
             system_info.sysname,
             system_info.release,
             uptime,
             load1,
             load5,
             load15,
             mem_total,
             mem_available,
             SID);

    send(connfd,
         response,
         strlen(response),
         0);
}
else if (strcmp(buffer, "LISTPROC") == 0)
{
    DIR *proc_dir;
    struct dirent *entry;

    char path[512];
    char line[256];

    char process_name[256];
    char process_state = '?';

    FILE *fp;

    proc_dir = opendir("/proc");

    if (proc_dir == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 002 INTERNAL_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);
    }
    else
    {
        char header[BUFFER_SIZE];

        snprintf(header,
                 sizeof(header),
                 "OK LISTPROC\n"
                 "PID\tNAME\tSTATE\n");

        send(connfd,
             header,
             strlen(header),
             0);

        while ((entry = readdir(proc_dir)) != NULL)
        {
            /*
             * Process directories in /proc have
             * numeric names such as:
             *
             * /proc/1
             * /proc/520
             * /proc/1342
             */

            if (!isdigit((unsigned char)entry->d_name[0]))
            {
                continue;
            }

            snprintf(path,
                     sizeof(path),
                     "/proc/%s/status",
                     entry->d_name);

            fp = fopen(path, "r");

            if (fp == NULL)
            {
                continue;
            }

            strcpy(process_name, "UNKNOWN");
            process_state = '?';

            while (fgets(line, sizeof(line), fp) != NULL)
            {
                if (strncmp(line, "Name:", 5) == 0)
                {
                    sscanf(line,
                           "Name:\t%255s",
                           process_name);
                }

                else if (strncmp(line, "State:", 6) == 0)
                {
                    sscanf(line,
                           "State:\t%c",
                           &process_state);
                }
            }

            fclose(fp);

            char process_line[1024];

            snprintf(process_line,
                     sizeof(process_line),
                     "%s\t%s\t%c\n",
                     entry->d_name,
                     process_name,
                     process_state);

            send(connfd,
                 process_line,
                 strlen(process_line),
                 0);
        }

        closedir(proc_dir);

        char end_message[64];

	snprintf(end_message,
         sizeof(end_message),
         "END SID:%s\n",
         SID);

        send(connfd,
             end_message,
             strlen(end_message),
             0);
    }
}

else if (strncmp(buffer, "EXEC ", 5) == 0)
{
    char *exec_command = buffer + 5;

    char response[BUFFER_SIZE];

    /*
     * EXEC DATE
     */
    if (strcmp(exec_command, "DATE") == 0)
    {
        FILE *fp;
        char output[512];

        fp = popen("date", "r");

        if (fp == NULL)
        {
            snprintf(response,
                     sizeof(response),
                     "ERR 002 INTERNAL_ERROR SID:%s\n",
                     SID);
        }
        else
        {
            memset(output, 0, sizeof(output));

            if (fgets(output, sizeof(output), fp) == NULL)
            {
                strcpy(output, "Unable to read date\n");
            }

            pclose(fp);

            snprintf(response,
                     sizeof(response),
                     "OK EXEC DATE\n"
                     "%s"
                     "SID:%s\n",
                     output,
                     SID);
        }

        send(connfd,
             response,
             strlen(response),
             0);
    }

    /*
     * EXEC UPTIME
     */
    else if (strcmp(exec_command, "UPTIME") == 0)
    {
        FILE *fp;
        char output[512];

        fp = popen("uptime", "r");

        if (fp == NULL)
        {
            snprintf(response,
                     sizeof(response),
                     "ERR 002 INTERNAL_ERROR SID:%s\n",
                     SID);
        }
        else
        {
            memset(output, 0, sizeof(output));

            if (fgets(output, sizeof(output), fp) == NULL)
            {
                strcpy(output, "Unable to read uptime\n");
            }

            pclose(fp);

            snprintf(response,
                     sizeof(response),
                     "OK EXEC UPTIME\n"
                     "%s"
                     "SID:%s\n",
                     output,
                     SID);
        }

        send(connfd,
             response,
             strlen(response),
             0);
    }

    /*
     * EXEC DF
     */
    else if (strcmp(exec_command, "DISKFREE") == 0)
    {
        FILE *fp;

        fp = popen("df -h", "r");

        if (fp == NULL)
        {
            snprintf(response,
                     sizeof(response),
                     "ERR 002 INTERNAL_ERROR SID:%s\n",
                     SID);

            send(connfd,
                 response,
                 strlen(response),
                 0);
        }
        else
        {
            snprintf(response,
                     sizeof(response),
                     "OK EXEC DISKFREE\n");

            send(connfd,
                 response,
                 strlen(response),
                 0);

            char line[512];

            while (fgets(line, sizeof(line), fp) != NULL)
            {
                send(connfd,
                     line,
                     strlen(line),
                     0);
            }

            pclose(fp);

            snprintf(response,
                     sizeof(response),
                     "END SID:%s\n",
                     SID);

            send(connfd,
                 response,
                 strlen(response),
                 0);
        }
    }



    /*
     * EXEC HOSTNAME
     */
    else if (strcmp(exec_command, "HOSTNAME") == 0)
    {
        FILE *fp;
        char output[512];

        fp = popen("hostname", "r");

        if (fp == NULL)
        {
            snprintf(response,
                     sizeof(response),
                     "ERR 002 INTERNAL_ERROR SID:%s\n",
                     SID);
        }
        else
        {
            memset(output, 0, sizeof(output));

            if (fgets(output, sizeof(output), fp) == NULL)
            {
                strcpy(output, "Unable to read hostname\n");
            }

            pclose(fp);

            snprintf(response,
                     sizeof(response),
                     "OK EXEC HOSTNAME\n"
                     "%s"
                     "SID:%s\n",
                     output,
                     SID);
        }

        send(connfd,
             response,
             strlen(response),
             0);
    }

    /*
     * EXEC WHOAMI
     */
    else if (strcmp(exec_command, "WHOAMI") == 0)
    {
        FILE *fp;
        char output[512];

        fp = popen("whoami", "r");

        if (fp == NULL)
        {
            snprintf(response,
                     sizeof(response),
                     "ERR 002 INTERNAL_ERROR SID:%s\n",
                     SID);
        }
        else
        {
            memset(output, 0, sizeof(output));

            if (fgets(output, sizeof(output), fp) == NULL)
            {
                strcpy(output, "Unable to read user\n");
            }

            pclose(fp);

            snprintf(response,
                     sizeof(response),
                     "OK EXEC WHOAMI\n"
                     "%s"
                     "SID:%s\n",
                     output,
                     SID);
        }

        send(connfd,
             response,
             strlen(response),
             0);
    }

    /*
     * Anything other than DATE, UPTIME or DF
     * HOSTNAME or WHOAMI is NOT allowed.
     */
    else
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 004 EXEC_NOT_ALLOWED SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);
    }
}

else if (strncmp(buffer, "PUT ", 4) == 0)
{
    char filename[256];
    long file_size;

    if (sscanf(buffer,
               "PUT %255s %ld",
               filename,
               &file_size) != 2 ||
        file_size < 0)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    char save_path[512];

snprintf(save_path,
         sizeof(save_path),
         "agentfiles/IT24300019/%s",
         filename);

FILE *file = fopen(save_path, "wb");
    if (file == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    /*
     * Tell Controller that Agent is ready
     * to receive file bytes.
     */
    char ready_message[64];

    snprintf(ready_message,
             sizeof(ready_message),
             "READY SID:%s\n",
             SID);

    send(connfd,
         ready_message,
         strlen(ready_message),
         0);

    long total_received = 0;
    int file_error = 0;

    while (total_received < file_size)
    {
        char file_buffer[4096];

        long remaining = file_size - total_received;

        size_t amount_to_receive =
            remaining < (long)sizeof(file_buffer)
                ? (size_t)remaining
                : sizeof(file_buffer);

        ssize_t received =
            recv(connfd,
                 file_buffer,
                 amount_to_receive,
                 0);

        if (received <= 0)
        {
            file_error = 1;
            break;
        }

        size_t written =
            fwrite(file_buffer,
                   1,
                   (size_t)received,
                   file);

        if (written != (size_t)received)
        {
            file_error = 1;
            break;
        }

        total_received += received;
    }

    fclose(file);

    char response[BUFFER_SIZE];

    if (file_error || total_received != file_size)
    {
	remove(save_path);
        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);
    }
    else
    {
        snprintf(response,
                 sizeof(response),
                 "OK PUT %s %ld SID:%s\n",
                 filename,
                 total_received,
                 SID);
    }

    send(connfd,
         response,
         strlen(response),
         0);
}

else if (strncmp(buffer, "GET ", 4) == 0)
{
    char filename[256];
    char filepath[512];
    char response[BUFFER_SIZE];

    if (sscanf(buffer, "GET %255s", filename) != 1)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    snprintf(filepath,
             sizeof(filepath),
             "agentfiles/IT24300019/%s",
             filename);

    FILE *file = fopen(filepath, "rb");

    if (file == NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    /* Find file size */
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);

        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    long file_size = ftell(file);

    if (file_size < 0)
    {
        fclose(file);

        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    rewind(file);

    /* Tell Controller how many bytes are coming */
    snprintf(response,
             sizeof(response),
             "OK GET %s %ld SID:%s\n",
             filename,
             file_size,
             SID);

    send(connfd,
         response,
         strlen(response),
         0);

    /*
 * Wait for Controller to confirm that
 * it is ready to receive file bytes.
 */
char ready_buffer[64];

memset(ready_buffer, 0, sizeof(ready_buffer));

ssize_t ready_received =
    recv(connfd,
         ready_buffer,
         sizeof(ready_buffer) - 1,
         0);

if (ready_received <= 0)
{
    fclose(file);
    continue;
}

ready_buffer[ready_received] = '\0';
ready_buffer[strcspn(ready_buffer, "\r\n")] = '\0';

if (strcmp(ready_buffer, "READY") != 0)
{
    fclose(file);
    continue;
}



    /* Send exact file bytes */
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
                send(connfd,
                     file_buffer + total_sent,
                     bytes_read - total_sent,
                     0);

            if (sent <= 0)
            {
                break;
            }

            total_sent += (size_t)sent;
        }

        if (total_sent < bytes_read)
        {
            break;
        }
    }

    fclose(file);
}

else if (strncmp(buffer, "MONITOR START ", 14) == 0)
{
    int udp_port;
    if (sscanf(buffer,
           "MONITOR START %d",
           &udp_port) != 1 ||
    udp_port != UDP_PORT)
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

    continue;
}

    if (monitor_running)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR MONITOR_ALREADY_RUNNING SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    struct monitor_info *monitor =
        malloc(sizeof(struct monitor_info));

    if (monitor == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 002 INTERNAL_ERROR SID:%s\n",
                 SID);

        send(connfd,
             response,
             strlen(response),
             0);

        continue;
    }

    strncpy(monitor->client_ip,
            client_ip,
            sizeof(monitor->client_ip) - 1);

    monitor->client_ip[
        sizeof(monitor->client_ip) - 1] = '\0';

    monitor->udp_port = udp_port;
    monitor->running = &monitor_running;

    monitor_running = 1;

    if (pthread_create(&monitor_thread,
                       NULL,
                       send_udp_monitor,
                       monitor) != 0)
    {
        perror("pthread_create");

        monitor_running = 0;
        free(monitor);

        continue;
    }

    char response[BUFFER_SIZE];

    snprintf(response,
             sizeof(response),
             "OK MONITOR START UDP:%d SID:%s\n",
             udp_port,
             SID);

    send(connfd,
         response,
         strlen(response),
         0);
}

else if (strcmp(buffer, "MONITOR STOP") == 0)
{
    char response[BUFFER_SIZE];

    if (!monitor_running)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR MONITOR_NOT_RUNNING SID:%s\n",
                 SID);
    }
    else
    {
        monitor_running = 0;

        pthread_join(monitor_thread, NULL);

        snprintf(response,
                 sizeof(response),
                 "OK MONITOR STOP SID:%s\n",
                 SID);
    }

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
     struct client_info *info =
    malloc(sizeof(struct client_info));

if (info == NULL)
{
    perror("malloc");
    close(connfd);
    continue;
}

/* Save BOTH socket and Controller address */
info->connfd = connfd;
info->client_addr = client_addr;

pthread_t thread_id;

if (pthread_create(&thread_id,
                   NULL,
                   handle_client,
                   info) != 0)
{
    perror("pthread_create");
    close(connfd);
    free(info);
    continue;
}

pthread_detach(thread_id);
    /*
     * We do not need to pthread_join() this thread.
     */
    pthread_detach(thread_id);

}
close(listenfd);
return 0;
}
