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
#include <unistd.h>  // for close() and fork()

#define BUFFER_SIZE 1024  // buffer size for reading requests

// simple function to print error messages and optionally exit the program
void print_error(const char *msg, bool exit_flag) {
    perror(msg);  // print the error message
    if (exit_flag)
        exit(-1);  // exit the program if exit_flag is true
}

// function to handle client requests
void handle_client(int client_socket) {
    char request_buffer[BUFFER_SIZE];  // buffer to store the client's request
    int received_bytes;  // to store number of bytes received

    time_t current_time;  // to store the current time
    struct tm *time_info;  // for formatted time info
    char time_string[30];  // buffer to store the time string

    FILE *file;  // file pointer to serve HTML file
    char file_name[100];  // buffer to store the requested file name
    char *response_buffer;  // buffer to store the full HTTP response
    int file_size;  // size of the requested file
    int request_length;  // length of the request message

    memset(request_buffer, 0, sizeof(request_buffer));  // clear the request buffer before use
    received_bytes = recv(client_socket, request_buffer, sizeof(request_buffer), 0);  // receive data from client

    // if recv() returns a negative value, it means there was an error reading the socket
    if (received_bytes < 0) {
        print_error("Error reading from client socket", false);  // log the error
        close(client_socket);  // close the client socket
        exit(0);  // exit the child process
    }

    request_length = received_bytes / sizeof(char);  // get the length of the request
    request_buffer[request_length - 2] = '\0';  // null-terminate the request string (removing extra characters)

    // check if the request is a valid HTTP GET request
    if (strncmp(request_buffer, "GET ", 4) != 0) {
        // tried using POST, PUT methods but GET is more straightforward for this server
        char error_message[] = "HTTP/1.1 400 Bad Request\n\nInvalid Command. Use: GET <filename>\n";
        send(client_socket, error_message, strlen(error_message), 0);  // send error message to client
    } else {
        // parse the file name from the request (skip "GET /")
        request_length = strlen(request_buffer);
        int i, j = 0;
        for (i = 4; i < request_length; i++, j++) {
            if (request_buffer[i] == '\0' || request_buffer[i] == '\n' || request_buffer[i] == ' ') {
                break;  // stop when end of the file name is found
            } else if (request_buffer[i] == '/') {
                --j;  // skip slashes (tried including slashes but didn't work well with file paths)
            } else {
                file_name[j] = request_buffer[i];  // copy character to file_name
            }
        }
        file_name[j] = '\0';  // terminate the file name string

        // try opening the requested file
        file = fopen(file_name, "r");
        if (file == NULL) {
            // if file is not found, send a 404 error response
            char not_found_message[] = "HTTP/1.1 404 Not Found\n\n";
            send(client_socket, not_found_message, strlen(not_found_message), 0);
        } else {
            // file was found, prepare the HTTP response
            time(&current_time);  // get the current time
            time_info = localtime(&current_time);  // format the time
            strftime(time_string, 30, "%a, %d %b %Y %X %Z", time_info);  // convert time to string

            // find out the size of the file
            fseek(file, 0, SEEK_END);  // move file pointer to the end
            file_size = ftell(file);  // get the size of the file
            fseek(file, 0, SEEK_SET);  // move file pointer back to the beginning

            // create the HTTP response header
            char header[BUFFER_SIZE];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\nDate: %s\nContent-Length: %d\nConnection: close\nContent-Type: text/html\n\n",
                     time_string, file_size);

            // allocate memory for the full response (header + file content)
            response_buffer = (char *)malloc(strlen(header) + file_size + 1);
            if (response_buffer == NULL) {
                print_error("Memory allocation failed", false);  // error if malloc fails
                fclose(file);  // close the file
                close(client_socket);  // close the socket
                exit(0);  // exit the child process
            }

            strcpy(response_buffer, header);  // copy the HTTP header to the response buffer
            fread(response_buffer + strlen(header), 1, file_size, file);  // read the file content into the buffer

            // send the full response to the client
            send(client_socket, response_buffer, strlen(header) + file_size, 0);

            free(response_buffer);  // free the allocated memory for the response
            fclose(file);  // close the file after sending
        }
    }

    close(client_socket);  // close the client socket after serving the request
    exit(0);  // exit the child process
}

// function to create and set up the server socket
int setup_server_socket(bool non_blocking, const char *host, const char *port) {
    struct addrinfo hints, *server_info = NULL;  // structure to hold server address info

    memset(&hints, 0, sizeof(hints));  // zero out the hints structure
    hints.ai_family = AF_UNSPEC;  // allow either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // use TCP (tried UDP but didn't fit HTTP needs)
    hints.ai_flags = AI_PASSIVE;  // for binding to any IP

    // get the server address information
    int rc = getaddrinfo(host, port, &hints, &server_info);
    if (rc != 0) {
        print_error("Host not found", true);  // error if host not found
    }

    // create a socket using the server address info
    int server_socket = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (server_socket < 0) {
        print_error("Socket creation failed", true);  // error if socket creation fails
    }

    // set socket to non-blocking if specified (helpful in high load scenarios)
    if (non_blocking) {
        fcntl(server_socket, F_SETFL, O_NONBLOCK);  // tried without non-blocking but wasn't efficient with multiple clients
    }

    // bind the socket to the address and port
    if (bind(server_socket, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        print_error("Binding failed", true);  // error if binding fails
    }

    // start listening for incoming connections
    if (listen(server_socket, 100) < 0) {
        print_error("Listening failed", true);  // error if listening fails
    }

    freeaddrinfo(server_info);  // free the memory allocated by getaddrinfo
    return server_socket;  // return the server socket
}

// main function where the server starts
int main(int argc, char *argv[]) {
    // check if the user provided the correct number of arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <host:port>\n", argv[0]);  // prompt the user to enter correct format
        exit(1);  // exit if format is wrong
    }

    // split the host and port from the input
    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");

    if (!host || !port) {
        fprintf(stderr, "Invalid host:port format\n");
        exit(1);  // exit if format is incorrect
    }

    // prevent zombie processes by ignoring child process termination signals
    signal(SIGCHLD, SIG_IGN);

    struct sockaddr_in client_address;  // structure to hold client address info
    socklen_t address_len = sizeof(struct sockaddr_in);  // length of client address structure

    int server_socket = setup_server_socket(false, host, port);  // set up the server socket

    // server loop to continuously accept and handle clients
    while (1) {
        // accept an incoming connection
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len);
        if (client_socket < 0) {
            print_error("Accept failed", false);  // log error if accept fails
            continue;  // continue accepting other connections
        }

        // create a new process to handle the client request
        pid_t pid = fork();  // fork the process
        if (pid < 0) {
            print_error("Fork failed", false);  // error if fork fails
        } else if (pid == 0) {
            // in the child process: close the server socket and handle the client
            close(server_socket);  // close the server socket in the child
            handle_client(client_socket);  // handle the client request
        } else {
            // in the parent process: close the client socket and keep accepting new clients
            close(client_socket);  // parent closes client socket after forking
        }
    }

    close(server_socket);  // close the server socket when done (though we never reach this line)
    return 0;  // exit the program
}
