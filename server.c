#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_THREADS 3
#define MAX_QUEUE 10

pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_available_cond = PTHREAD_COND_INITIALIZER;

typedef struct {
    int clientfd;
    char filepath[256];
} serve_file_args_t;

serve_file_args_t request_queue[MAX_QUEUE];
int queue_start = 0;
int queue_end = 0;
int queue_size = 0;

void enqueue_request(int clientfd, const char *filepath) {
    pthread_mutex_lock(&thread_count_mutex);
    
    if (queue_size == MAX_QUEUE) {
        // If queue is full, close the connection immediately (or handle as desired).
        printf("Queue is full, rejecting connection.\n");
        close(clientfd);
    } else {
        // Add the request to the queue.
        request_queue[queue_end].clientfd = clientfd; 
        // Remove the leading / from the path.
        snprintf(request_queue[queue_end].filepath, sizeof(request_queue[queue_end].filepath), ".%s", filepath);
        // 
        queue_end = (queue_end + 1) % MAX_QUEUE;
        queue_size++;
        
        // Signal that there is work to do to one waiting thread (chosen by the scheduler).
        pthread_cond_signal(&thread_available_cond);
    }
    
    pthread_mutex_unlock(&thread_count_mutex);
}

serve_file_args_t dequeue_request() {
    pthread_mutex_lock(&thread_count_mutex);
    
    // Wait until there is work to do.
    while (queue_size == 0) {
        pthread_cond_wait(&thread_available_cond, &thread_count_mutex);
    }
    
    // Dequeue the request.
    serve_file_args_t request = request_queue[queue_start];
    queue_start = (queue_start + 1) % MAX_QUEUE;
    queue_size--;
    
    pthread_mutex_unlock(&thread_count_mutex);
    return request;
}

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
        close(clientfd);
        return;
    }
    
    // Determine file size.
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

    // Send file in chunks.
    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file)) > 0) {
        write(clientfd, file_buffer, bytes_read);
    }

    fclose(file);
    close(clientfd);
}

void *worker_thread(void *arg) {
    while (1) {
        // Get the next request from the queue.
        serve_file_args_t request = dequeue_request();
        
        // Serve the file.
        serve_file(request.clientfd, request.filepath);
    }
    return NULL;
}

int main() {
    //===Boilerplate code start===
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    // Create socket.
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options.
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind the socket.
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    //===Boilerplate code end===

    // Listen for connections.
    listen(server_fd, 10);

    // Start worker threads ahead of time to speed up accept/serve process.
    pthread_t thread_pool[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&thread_pool[i], NULL, worker_thread, NULL);
    }

    // Main loop to accept connections and add them to the queue.
    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed");
            continue;
        }

        // Read the HTTP request.
        read(client_fd, buffer, BUFFER_SIZE);
        printf("Received request:\n%s\n", buffer);

        // Parse request to determine file.
        char method[16], path[256];
        sscanf(buffer, "%s %s", method, path);

        // Default to "index.html" if root is requested.
        if (strcmp(path, "/") == 0) {
            strcpy(path, "/index.html");
        }

        // Enqueue the request.
        enqueue_request(client_fd, path);
    }

    close(server_fd);
    return 0;
}
