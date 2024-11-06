#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct {
    int clientfd;
    char *filepath;
} serve_file_args_t;

void serve_file(int clientfd, const char *filepath) {
    FILE *file = fopen(filepath, "r");
     if (file == NULL) {
        const char *not_found_response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "\r\n"
            "404 Not Found";
        write(clientfd, not_found_response, strlen(not_found_response));
        return;
    }
    // Determine file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    // Prepare HTTP response headers.
    char headers[BUFFER_SIZE];
    snprintf(headers, sizeof(headers),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %ld\r\n"
             "\r\n", file_size);
    write(clientfd, headers, strlen(headers));

    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    while (bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file) > 0) {
        write(clientfd, file_buffer, BUFFER_SIZE);
    }
}

void *serve_file_aux(void *args) {
    serve_file_args_t *file_args = (serve_file_args_t *) args;
    char *path = file_args->filepath;
    char filepath[256];  
    snprintf(filepath, sizeof(filepath), ".%s", path);

    serve_file(file_args->clientfd, filepath);
    return NULL;
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind the socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // Listen for connections
    listen(server_fd, 3);

    while (1) {
        // Accept a connection
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }

        read(client_fd, buffer, BUFFER_SIZE);
        printf("Received request:\n%s\n", buffer);

        // Parse request to determine file
        char method[16], path[256];
        sscanf(buffer, "%s %s", method, path);

        // Default to "index.html" if root is requested
        if (strcmp(path, "/") == 0) {
            strcpy(path, "/index.html");
        }

        pthread_t th;

        // Prepend '.' for filepath in fopen.
        char filepath[256];
        
        serve_file_args_t args = {client_fd, filepath};

        if (0 != pthread_create(&th, NULL, serve_file_aux, &args)) {
            perror("accept failed");
            close(client_fd);
            exit(EXIT_FAILURE);
        } else {
            pthread_detach(th);
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
