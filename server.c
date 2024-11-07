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

typedef struct cache_queue {
    cache_node_t *head;
    cache_node_t *tail;
} cache_queue_t;

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

void dict_new() {
    cache_dict_t *dict = malloc(sizeof(cache_dict_t));
    assert(dict != NULL);

    // Allocate a sentinel node.
    cache_dict_node_t *sentinel = malloc(sizeof(cache_dict_node_t));
    assert(sentinel != NULL);
    sentinel->next = NULL;
    dict->count = 0;
    dict->head = sentinel;
    
    cache_dict = dict;
}

void queue_new() {
  cache_queue_t *q = malloc(sizeof(cache_queue_t));
  assert(q != NULL);

  // insert a dummy head node.
  cache_node_t *tmp = malloc(sizeof(cache_queue_t));
  assert(tmp != NULL);
  tmp->next = NULL;

  q->head = q->tail = tmp;

  cache_queue = q;
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

    cache_dict->count = cache_dict->count - 1;
}

void dequeue_cache() {
    if (cache_queue->tail == cache_queue->head) {
        // No elements to dequeue, queue is just the sentinel.
        return;
    }

    // Remove the least-recently-used file from the end.
    cache_node_t tail = *cache_queue->tail;
    free(cache_queue->tail);
    cache_node_t prev = *tail.prev;
    // Point the tail of the cache queue to the second to last element.
    cache_queue->tail = &prev; 

    // Remove this pair from the cache dictionary.
    cache_dict_node_t *current = cache_dict->head;
    while (current != NULL) {
        if (strcmp(cache_dict->head->key_filepath, tail.filepath) == 0) {

            remove_from_dict(cache_dict->head);
            return;
        }
        current = current->next;
    }
}

void enqueue_cache(char *filepath, char *data, int size) {
    printf("in enqueue duh\n");
    printf("also enqueue\n");
    // Make space if not available.
//    if (cache_dict->count >= MAX_CACHE_QUEUE) {
  //      printf("after dereference count\n");
    //    fflush(stdout);
      //  dequeue_cache();
   // }

    printf("before malloc new node in enqueue\n");
    // Prepend a new node to the LRU cache, marking this as the most-recently served file.
    cache_node_t *new_node = (cache_node_t *) malloc(sizeof(cache_node_t));
    printf("before strcpy enqueue\n");
    // POTENTIAL VULNERABILITY TO BUFFER OVERFLOW, FIX.
    strcpy(new_node->bytes, data);
    printf("after strcpy enqueue\n");

    // D->N1->N2-/>
    // N3->N1->N2-/>
    new_node->next = cache_queue->head->next;
    // D->N3->N1->N2-/>
    cache_queue->head->next = new_node;
    
    new_node->prev = cache_queue->head;

    // Add pair to dictionary.
    cache_dict_node_t *new_dict_node = (cache_dict_node_t *) malloc(sizeof(cache_dict_node_t));
    // Initialize values in new node.
    new_dict_node->key_filepath = filepath;
    new_dict_node->value_node = new_node; 
    new_dict_node->filesize = size;
    
    // Prepend the new node to the head of the dict.
    new_dict_node->next = cache_dict->head;

    // Assign the head to the new node.
    cache_dict->head = new_dict_node;
    // Increment the size of the dict.
    cache_dict->count = cache_dict->count + 1;
    printf("done enqueueing\n");
}

void requeue_cache(cache_dict_node_t *node_to_requeue, cache_dict_node_t *prev) {
    // Remove the node.
    cache_dict_node_t *tmp = node_to_requeue->next;
    prev->next = tmp; 

    // Prepend it (after the sentinel).
    node_to_requeue->next = cache_dict->head->next;
    // Prepend the sentinel.
    cache_dict->head = node_to_requeue;
}

/*
  If the filepath is stored in the cache, it writes the data to
  write_data_here and requeues the associated cache_node to most-recent.

  Returns 0 if success, -1 if cache miss.  
*/
int retrieve_data(const char *filepath, char *write_data_here) {
    cache_dict_node_t *prev;
    cache_dict_node_t *iterator = cache_dict->head;
    while (cache_dict != NULL && iterator  != NULL) {
        printf("before strcmp in retrieve_data\n");
        if (iterator->key_filepath == NULL) {
 	    iterator  = iterator->next;
	    printf("continuing\n");
            continue;
	}
	printf("retrieve path: %s\n", iterator->key_filepath);
	fflush(stdout);
        if (strcmp(filepath, iterator->key_filepath) == 0) {
            // Write the data.
            printf("before write in retrieve_data\n");
            strcpy(write_data_here, cache_dict->head->value_node->bytes);
            requeue_cache(cache_dict->head, prev);
            return 0;
        }
        printf("before inc prev in retrieve data\n");
        prev = iterator;
        iterator = iterator->next;
    }

    // Failure, return -1.
    return -1;
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
        
        queue_end = (queue_end + 1) % MAX_QUEUE;
        queue_size++;
        
        printf("about to signal\n");
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
    printf("done dequeueing request\n");
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

    printf("before retrieving data\n");
    char *data = (char *) malloc(MAX_CACHE_SIZE);
    if (retrieve_data(filepath, data) == 0) {
        printf("cached!\n");
        return;
    }
    printf("after retrieving data\n");

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

    char *accumulated_data = (char *) malloc(MAX_CACHE_SIZE);
   size_t accumulated_size = 0;

    // Send file in BUFFER_SIZE sized chunks.
    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, file)) > 0) {
        // Add data to accumulator for caching.
	if (accumulated_size + bytes_read < MAX_CACHE_SIZE) {
           memcpy(accumulated_data + accumulated_size, file_buffer, bytes_read);
           accumulated_size += bytes_read;
        } else {
           // Handle overflow, either by truncating or reallocating.
       	   break;
        }

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

        // Since this file was not read from cache, enqueue its data into cache.
        enqueue_cache(filepath, accumulated_data, file_size);
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

    queue_new();
    dict_new();

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

	printf("about to enqueue request\n");
        // Enqueue the request.
        enqueue_request(client_fd, path);
    }

    close(server_fd);
    return 0;
}
