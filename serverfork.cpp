#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <unistd.h>  // For close() and fork()


#define BUFFER_SIZE 1024

void print_error(const char *msg, bool exit_flag) {
    perror(msg);
    if (exit_flag)
        exit(-1);
}

void handle_client(int client_socket) {
    char request_buffer[BUFFER_SIZE];
    int received_bytes;

    time_t current_time;
    struct tm *time_info;
    char time_string[30];

    FILE *file;
    char file_name[100];
    char *response_buffer;
    int file_size;
    int request_length;

    memset(request_buffer, 0, sizeof(request_buffer));
    received_bytes = recv(client_socket, request_buffer, sizeof(request_buffer), 0);

    if (received_bytes < 0) {
        print_error("Error reading from client socket", false);
        close(client_socket);
        exit(0);
    }

    request_length = received_bytes / sizeof(char);
    request_buffer[request_length - 2] = '\0';

    if (strncmp(request_buffer, "GET ", 4) != 0) {
        char error_message[] = "HTTP/1.1 400 Bad Request\n\nInvalid Command. Use: GET <filename>\n";
        send(client_socket, error_message, strlen(error_message), 0);
    } else {
        request_length = strlen(request_buffer);
        int i, j = 0;
        for (i = 4; i < request_length; i++, j++) {
            if (request_buffer[i] == '\0' || request_buffer[i] == '\n' || request_buffer[i] == ' ') {
                break;
            } else if (request_buffer[i] == '/') {
                --j;
            } else {
                file_name[j] = request_buffer[i];
            }
        }
        file_name[j] = '\0';

        file = fopen(file_name, "r");
        if (file == NULL) {
            char not_found_message[] = "HTTP/1.1 404 Not Found\n\n";
            send(client_socket, not_found_message, strlen(not_found_message), 0);
        } else {
            time(&current_time);
            time_info = localtime(&current_time);
            strftime(time_string, 30, "%a, %d %b %Y %X %Z", time_info);

            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
            fseek(file, 0, SEEK_SET);

            char header[BUFFER_SIZE];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\nDate: %s\nContent-Length: %d\nConnection: close\nContent-Type: text/html\n\n",
                     time_string, file_size);

            response_buffer = (char *)malloc(strlen(header) + file_size + 1);
            if (response_buffer == NULL) {
                print_error("Memory allocation failed", false);
                fclose(file);
                close(client_socket);
                exit(0);
            }

            strcpy(response_buffer, header);
            fread(response_buffer + strlen(header), 1, file_size, file);

            send(client_socket, response_buffer, strlen(header) + file_size, 0);
            free(response_buffer);
            fclose(file);
        }
    }

    close(client_socket);
    exit(0);
}

int setup_server_socket(bool non_blocking, const char *host, const char *port) {
    struct addrinfo hints, *server_info = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rc = getaddrinfo(host, port, &hints, &server_info);
    if (rc != 0) {
        print_error("Host not found", true);
    }

    int server_socket = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (server_socket < 0) {
        print_error("Socket creation failed", true);
    }

    if (non_blocking) {
        fcntl(server_socket, F_SETFL, O_NONBLOCK);
    }

    if (bind(server_socket, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        print_error("Binding failed", true);
    }

    if (listen(server_socket, 100) < 0) {
        print_error("Listening failed", true);
    }

    freeaddrinfo(server_info);
    return server_socket;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <host:port>\n", argv[0]);
        exit(1);
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");

    if (!host || !port) {
        fprintf(stderr, "Invalid host:port format\n");
        exit(1);
    }

    signal(SIGCHLD, SIG_IGN);

    struct sockaddr_in client_address;
    socklen_t address_len = sizeof(struct sockaddr_in);

    int server_socket = setup_server_socket(false, host, port);

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len);
        if (client_socket < 0) {
            print_error("Accept failed", false);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            print_error("Fork failed", false);
        } else if (pid == 0) {
            close(server_socket);
            handle_client(client_socket);
        } else {
            close(client_socket);
        }
    }

    close(server_socket);
    return 0;
}
