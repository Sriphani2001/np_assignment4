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

// thread-safe logging mechanism to handle logs across multiple threads
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// function to log messages, added mutex to prevent race conditions with threads
void log_message(const char *message) {
    pthread_mutex_lock(&log_mutex);  // using mutex here to avoid logging issues with threads
    fprintf(stderr, "%s\n", message);  // prints message to standard error
    pthread_mutex_unlock(&log_mutex);
}

// simple error display function that halts program if 'halt_flag' is true
void display_error(const char *msg, bool halt_flag)
{
    log_message(msg);  // logs the error message
    perror(msg);  // shows the actual system error message
    if (halt_flag)  // if halt_flag is true, we exit the program
        exit(-1); // abnormal termination
}

// handles the client request
// handles the client request
void handle_client_request(int client_socket)
{
    char recv_buffer[BUFFER_SIZE];  // buffer to store client's request
    int recv_len;

    FILE *html_file;  // file pointer for the requested HTML file
    char file_name[100];  // buffer for the file name
    char *response_buffer;  // response buffer to store HTTP response
    int file_size;

    // bzero is used to zero out the buffer
    bzero(recv_buffer, sizeof(recv_buffer));

    // receive data from the client
    recv_len = recv(client_socket, recv_buffer, sizeof(recv_buffer) - 1, 0);
    if (recv_len < 0)  // if recv fails, log error and exit thread
    {
        display_error("Error reading from client socket", false);
        close(client_socket);
        pthread_exit(NULL);  // thread exits, don't want to crash the whole program
    }

    recv_buffer[recv_len - 2] = '\0';  // terminate the received string properly

    // Check if the request is a GET or HEAD request
    bool is_get_request = (strncmp(recv_buffer, "GET ", 4) == 0);
    bool is_head_request = (strncmp(recv_buffer, "HEAD ", 5) == 0);

    if (!is_get_request && !is_head_request)
    {
        // Send a 400 Bad Request if the request is neither GET nor HEAD
        const char *error_msg = "HTTP/1.1 400 Bad Request\n\nInvalid Command. Please use GET or HEAD.\n";
        send(client_socket, error_msg, strlen(error_msg), 0);  // send error message back to client
    }
    else
    {
        // Determine the request type and extract the file name
        int start_index = is_get_request ? 4 : 5;  // Skip "GET " or "HEAD " part
        int i, j = 0;
        for (i = start_index; i < recv_len; i++, j++)
        {
            if (recv_buffer[i] == '\0' || recv_buffer[i] == '\n' || recv_buffer[i] == ' ')
            {
                break;  // stop if it's end of the line or space
            }
            else if (recv_buffer[i] == '/')
            {
                --j;  // Skipping slashes in the file name
            }
            else
            {
                file_name[j] = recv_buffer[i];  // Copy file name to the buffer
            }
        }
        file_name[j] = '\0';  // properly terminate the string

        // open the requested file
        html_file = fopen(file_name, "r");
        if (html_file == NULL)
        {
            // file not found, send a 404 response
            const char *not_found_msg = "HTTP/1.1 404 Not Found\n\n";
            send(client_socket, not_found_msg, strlen(not_found_msg), 0);
        }
        else
        {
            // Get the size of the file
            fseek(html_file, 0, SEEK_END);
            file_size = ftell(html_file);
            fseek(html_file, 0, SEEK_SET);

            // Create an HTTP response header
            char header[BUFFER_SIZE];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\n"
                     "Content-Length: %d\n"
                     "Connection: close\n"
                     "Content-Type: text/html\n\n",
                     file_size);

            // Send only the headers if the request is HEAD
            send(client_socket, header, strlen(header), 0);

            if (is_get_request)
            {
                // If it's a GET request, send the file content
                response_buffer = (char *)malloc(file_size);
                if (response_buffer == NULL)
                {
                    display_error("Memory allocation failed", false);
                    fclose(html_file);
                    close(client_socket);
                    pthread_exit(NULL);
                }

                size_t read_size = fread(response_buffer, 1, file_size, html_file);
                if (read_size < file_size)
                {
                    display_error("Error reading file", false);
                    free(response_buffer);
                    fclose(html_file);
                    close(client_socket);
                    pthread_exit(NULL);
                }

                // Send the file content after headers (only for GET requests)
                send(client_socket, response_buffer, file_size, 0);
                free(response_buffer);  // free memory after sending the response
            }

            fclose(html_file);  // close the file after reading
        }
    }

    // close the socket and end the thread
    close(client_socket);
    pthread_exit(NULL);
}


// function to create server socket
int create_server_socket(bool non_blocking, char *host, char *port)
{
    int server_port = atoi(port);  // convert port to integer
    struct addrinfo hints, *server_info = NULL;

    // tried initializing addrinfo without memset, but caused issues
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;  // allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP connection
    hints.ai_flags = AI_PASSIVE;  // for binding
    hints.ai_protocol = 0;  // any protocol

    // getaddrinfo fills out server_info for us
   if (getaddrinfo(host, port, &hints, &server_info) != 0) {
        perror("getaddrinfo failed");
        return -1;
    }
    // create a socket (tried different protocols, but SOCK_STREAM is ideal for HTTP)
    int server_socket = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (server_socket < 0)
        display_error("Problem creating socket", true);

    // set socket to non-blocking mode if needed (tried it for performance under load)
    if (non_blocking)
        fcntl(server_socket, F_SETFL, O_NONBLOCK);

    // bind the socket to the provided address and port
    if (bind(server_socket, server_info->ai_addr, server_info->ai_addrlen) < 0)
        display_error("Problem binding socket", true);

    log_message("Listening for requests...");
    if (listen(server_socket, 100) < 0)  // listen for incoming connections
        display_error("Problem listening on socket", true);

    freeaddrinfo(server_info);  // free the memory allocated by getaddrinfo
    return server_socket;
}

// function that each thread runs to handle a client
void *handle_client(void *client_ptr)
{
    pthread_detach(pthread_self());  // detach the thread so its resources are released upon exit
    int client_socket = *((int *)client_ptr);  // cast void* back to int*
    handle_client_request(client_socket);  // handle the actual request
    close(client_socket);  // close the socket after handling
    return NULL;
}

// main function where the server starts
int main(int argc, char *argv[])
{
    // check if the correct number of arguments are provided
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <hostname:port>\n", argv[0]);
        exit(1);  // exit if hostname:port is not provided
    }

    // separate host and port from the argument
    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL)  // if port is missing, exit
    {
        fprintf(stderr, "Invalid hostname:port format\n");
        exit(1);
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(struct sockaddr_in);  // size of client address structure

    // create the server socket
    int server_socket = create_server_socket(false, host, port);
    if (server_socket < 0)
    {
        exit(1);  // exit if socket creation fails
    }

    // server loop that accepts clients
    while (true)
    {
        // accept an incoming connection
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0)
        {
            perror("Problem accepting client request");
            continue;  // continue to accept next connection
        }

        pthread_t thread_id;  // thread ID for the new client thread
        // deep copy the client_socket value for thread safety
        int *client_socket_copy = (int*)malloc(sizeof(int));  // tried without malloc, but unsafe
        if (client_socket_copy == NULL)
        {
            perror("Failed to allocate memory for client socket copy");
            close(client_socket);  // close the client socket if malloc fails
            continue;
        }
        *client_socket_copy = client_socket;  // store the socket in the new memory

        // create a new thread to handle the client request
        if (pthread_create(&thread_id, NULL, handle_client, client_socket_copy) != 0)
        {
            perror("Error creating thread");
            free(client_socket_copy);  // free the memory if thread creation fails
            close(client_socket);
        }
    }

    close(server_socket);  // close the server socket after exiting the loop
    return 0;
}
