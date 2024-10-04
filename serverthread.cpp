#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <time.h>

#define BUFFER_SIZE 1024

void display_error(const char *msg, bool halt_flag)
{
    perror(msg);
    if (halt_flag)
        exit(-1); // exit with abnormal termination
}

void handle_client_request(int client_socket)
{
    char recv_buffer[BUFFER_SIZE];
    int recv_len;
    time_t current_time;
    struct tm *time_info;
    char time_str[30];

    FILE *html_file;
    char file_name[100];
    char *response_buffer;
    int file_size;
    
    bzero(recv_buffer, sizeof(recv_buffer));
    recv_len = recv(client_socket, recv_buffer, sizeof(recv_buffer) - 1, 0);
    if (recv_len < 0)
    {
        display_error("error reading from client socket", false);
        close(client_socket);
        pthread_exit(NULL);
    }

    recv_buffer[recv_len - 2] = '\0'; // terminate the received string

    if (strncmp(recv_buffer, "GET ", 4) != 0)
    {
        // invalid command
        const char *error_msg = "HTTP/1.1 400 Bad Request\n\nInvalid Command. Please use: GET <file_name.html>\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }
    else
    {
        // extract file name
        int i, j = 0;
        for (i = 4; i < recv_len; i++, j++)
        {
            if (recv_buffer[i] == '\0' || recv_buffer[i] == '\n' || recv_buffer[i] == ' ')
            {
                break;
            }
            else if (recv_buffer[i] == '/')
            {
                --j; // skip slashes
            }
            else
            {
                file_name[j] = recv_buffer[i];
            }
        }
        file_name[j] = '\0';

        html_file = fopen(file_name, "r");
        if (html_file == NULL)
        {
            // file not found, send 404 response
            const char *not_found_msg = "HTTP/1.1 404 Not Found\n\n";
            send(client_socket, not_found_msg, strlen(not_found_msg), 0);
        }
        else
        {
            // file found, prepare response
            time(&current_time);
            time_info = localtime(&current_time);
            strftime(time_str, sizeof(time_str), "%a, %d %b %Y %X %Z", time_info);

            fseek(html_file, 0, SEEK_END);
            file_size = ftell(html_file);
            fseek(html_file, 0, SEEK_SET);

            char header[BUFFER_SIZE];
            snprintf(header, sizeof(header), 
                "HTTP/1.1 200 OK\n"
                "Date: %s\n"
                "Content-Length: %d\n"
                "Connection: close\n"
                "Content-Type: text/html\n\n", 
                time_str, file_size);

            response_buffer = (char *)malloc(strlen(header) + file_size + 1);
            if (response_buffer == NULL)
            {
                display_error("memory allocation failed", false);
                fclose(html_file);
                close(client_socket);
                pthread_exit(NULL);
            }

            strcpy(response_buffer, header);
            char *file_data = response_buffer + strlen(header);

            size_t read_size = fread(file_data, 1, file_size, html_file);
            if (read_size < file_size)
            {
                display_error("error reading file", false);
                free(response_buffer);
                fclose(html_file);
                close(client_socket);
                pthread_exit(NULL);
            }

            send(client_socket, response_buffer, strlen(header) + file_size, 0);
            free(response_buffer);
            fclose(html_file);
        }
    }

    close(client_socket);
    pthread_exit(NULL);
}

int create_server_socket(bool non_blocking, char *host, char *port)
{
    int server_port = atoi(port);
    struct addrinfo hints, *server_info = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    int rc = getaddrinfo(host, port, &hints, &server_info);
    if (rc != 0)
    {
        printf("host not found --> %s\n", gai_strerror(rc));
        if (rc == EAI_SYSTEM)
            perror("getaddrinfo() failed");
        return -1;
    }

    int server_socket = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (server_socket < 0)
        display_error("problem creating socket", true);

    if (non_blocking)
        fcntl(server_socket, F_SETFL, O_NONBLOCK);

    if (bind(server_socket, server_info->ai_addr, server_info->ai_addrlen) < 0)
        display_error("problem binding socket", true);

    fprintf(stderr, "listening for requests on port %d...\n", server_port);
    if (listen(server_socket, 100) < 0)
        display_error("problem listening on socket", true);

    freeaddrinfo(server_info);
    return server_socket;
}

void *handle_client(void *client_ptr)
{
    pthread_detach(pthread_self());
    int client_socket = *((int *)client_ptr);
    handle_client_request(client_socket);
    close(client_socket);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <hostname:port>\n", argv[0]);
        exit(1);
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL)
    {
        fprintf(stderr, "invalid hostname:port format\n");
        exit(1);
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(struct sockaddr_in);

    int server_socket = create_server_socket(false, host, port);
    if (server_socket < 0)
    {
        exit(1);
    }

    while (true)
    {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0)
        {
            perror("problem accepting client request");
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, &client_socket) != 0)
        {
            perror("error creating thread");
            close(client_socket);
        }
    }

    close(server_socket);
    return 0;
}
