#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <assert.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_THREADS 3
#define MAX_QUEUE 10
#define MAX_CACHE_SIZE 65536 // 2^16, 2 bytes
#define MAX_CACHE_QUEUE 10

pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_available_cond = PTHREAD_COND_INITIALIZER;

typedef struct cache_node {
    struct cache_node *next;
    struct cache_node *prev;
    char *filepath;
    char bytes[MAX_CACHE_SIZE];
} cache_node_t;

typedef struct cache_dict_node {
    struct cache_dict_node *next;
    char *key_filepath;
    cache_node_t *value_node;
    int filesize;
} cache_dict_node_t;

typedef struct cache_dict {
    cache_dict_node_t *head;
    int count;
} cache_dict_t;

typedef struct cache_queue {
    cache_node_t *head;
    cache_node_t *tail;
} cache_queue_t;

typedef struct {
    int clientfd;
    char filepath[256];
} serve_file_args_t;

cache_queue_t *cache_queue;
cache_dict_t *cache_dict;

serve_file_args_t request_queue[MAX_QUEUE];
int queue_start = 0;
int queue_end = 0;
int queue_size = 0;

cache_dict_t *dict_new() {
    cache_dict_t *dict = malloc(sizeof(cache_dict_t));
    assert(dict != NULL);

    // Allocate a sentinel node.
    cache_dict_node_t *sentinel = malloc(sizeof(cache_dict_node_t));
    assert(sentinel != NULL);
    sentinel->next = NULL;

    dict->head = sentinel;
    
    return dict;
}

void remove_from_dict(cache_dict_node_t *node) {
    assert(cache_dict != NULL);
    assert(node != NULL);

    while (cache_dict->head != NULL) {
        cache_dict_node_t *current = cache_dict->head;
        if (current == node) {
            current = node->next;
            free(node);
            return;
        }
        current = current->next;
    }
}

cache_queue_t *queue_new() {
  cache_queue_t *q = malloc(sizeof(cache_queue_t));
  assert(q != NULL);

  // insert a dummy head node.
  cache_node_t *tmp = malloc(sizeof(cache_queue_t));
  assert(tmp != NULL);
  tmp->next = NULL;

  q->head = q->tail = tmp;

  return q;
}

void dequeue_cache() {
    if (cache_queue->tail == cache_queue->head) {
        // No elements to dequeue, queue is just the sentinel.
        return;
    }

    // Remove the least-recently-used file from the end.
    cache_node_t tail = *cache_queue->tail;
   
    cache_node_t prev = *tail.prev;
    // Point the tail of the cache queue to the second to last element.
    cache_queue->tail = &prev; 

    // Remove this pair from the cache dictionary.
    cache_dict_node_t *current = cache_dict->head;
    while (current != NULL) {
        if (strcmp(cache_dict->head->key_filepath, tail.filepath) == 0) {
            free(&tail);
            remove_from_dict(cache_dict->head);
            return;
        }
        current = current->next;
    }

    free(&tail);
}

void enqueue_cache(const char *filepath, const char *data, int size) {
    // Make space if not available.
    if (cache_dict->count >= MAX_CACHE_QUEUE) {
        dequeue_cache();
    }

    // Prepend a new node to the LRU cache, marking this as the most-recently served file.
    cache_node_t *new_node = (cache_node_t *) malloc(sizeof(cache_node_t));
    // POTENTIAL VULNERABILITY TO BUFFER OVERFLOW, FIX.
    strcpy(new_node->bytes, data);
    
    // Add pair to dictionary.
    cache_dict_node_t *new_dict_node = (cache_dict_node_t *) malloc(sizeof(cache_dict_node_t));
    new_dict_node->key_filepath = filepath;
    new_dict_node->value_node
}

void requeue_cache(const char *filepath, const char *data) {

}

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

void serve_file(int clientfd, char *filepath) {
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

    // Write headers to client.         
    if (write(clientfd, headers, strlen(headers)) < 0) {
        perror("Error writing headers");
        fclose(file);
        free(filepath);
        close(clientfd);
        return;
    }

    // Send file in BUFFER_SIZE sized chunks.
    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file)) > 0) {
        ssize_t bytes_written = write(clientfd, file_buffer, bytes_read);
        if (bytes_written < 0) { 
            // Error writing to client.
            perror("Error writing file content");
            break;
        } else if (bytes_written == 0) {
            // Client closed connection.
            printf("Client closed the connection.\n");
            break;
        }
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
