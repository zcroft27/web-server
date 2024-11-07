# Building an HTTP Server in C

## Overview
I am beginning a personal project to create a web server in C. 

**server.c Flow So Far**: 
- Creates a new socket, binds it using the IPv4 option, and listens using the file descriptor for the new socket.   
- Enters infinite while loop for accepting connections.
- Accepts a connection and assigns a file descriptor for the new client.
- Reads from the file descriptor into a buffer.
- Parses the buffer to extract the method (GET, POST, PUT, etc.) and the path/route.
- If the file is cached, serve from memory instead of reading using a system call.
- Creates new POSIX thread (if space allows, otherwise rejects) and invokes a 'start routine' serve_file_aux() to attempt to serve the requested file to the client, passing the client file descriptor and filepath.
- If read with a system call, cache current file data and metadata in least-recently-used (LRU) cache. 
- Continue in loop, accepting up to 10 (arbitrary) connections concurrently.


## Interesting Features
- Initializes a pool of 10 worker threads before accepting connections, instead of creating
a new thread after accepting a connection.
- Uses a pthread_cond_signal to signal 1 worker thread at a time to serve a file.
- Locks shared-data access with a mutex to avoid datarace/deadlock issues.
- Utilizes a Least-recently-used (LRU) cache to store MAX_CACHE_QUEUE files in memory
to speed up serving files.
- Utilizes a dictionary alongside the LRU cache as a lookup table for cached nodes
for quicker access (I don't think I actually implemented the dictionary with O(1) access... TODO).
- Enqueues requests from clients using a cute circular buffer I found that really confused me at first:
serve_file_args_t request_queue[MAX_QUEUE];
int queue_start = 0;
int queue_end = 0;
int queue_size = 0;
**On enqueue:**

request_queue[queue_end].clientfd = clientfd;
snprintf(request_queue[queue_end].filepath, sizeof(request_queue[queue_end].filepath), ".%s", filepath);
queue_end = (queue_end + 1) % MAX_QUEUE;
queue_size++;

**On dequeue:**
serve_file_args_t request = request_queue[queue_start];
queue_start = (queue_start + 1) % MAX_QUEUE;
queue_size--;

It just rotates the start and end pointers around... cool.

## What Did I Learn
I used pthread_cond_signal--instead of using a variable representing an threads in use--to notify
threads to start work on serving a file. pthread_cond_signal unblocks one thread (by default) that
is waiting on the pthread_cond_t passed into it, as opposed to pthread_cond_broadcast which unblocks
all threads waiting for the specified condition.

The dictionary/lookup table worked fine as as a singly linked-list, with a sentinel at the head for ease of removal/insertion. The queue of cache nodes (file path w/ data)
worked to implement as a doubly linked-list, with the queue having pointers to head and tail, both initially pointing to a 'dummy' node
for ease of removal/insertion. (I just learned this queue in systems and thought it would be cool).

Attempting to do this without using pthread_cond_signal and instead using a global variable to represent a number
of threads available led to weird data race issues, and pthread_cond_signal was a huge help.

Multithreading (with a capacity) seems like an effective way to handle concurrent requests,
although I am not sure if it would be better to use fork() to better isolate requests.

A simple way to test if your multithreading is working is to create a huge file, request it, and then
make another request concurrently.

I created long.txt: 
shuf -i1-100000000 > long.txt
And requesting this file in ~10 tabs rapidly used 16gb of RAM before I killed the server.


## Intended Features/TODO
I got excited about caching and multithreading and forgot
about supporting other methods, serving more file types, etc.

- Modify serving based on method and file type.

- Log requests to server periodically.

- Refactor the LRU-cache to a dynamic pool of memory instead of allocating
2 byte chunks for each node in the cache queue.

- Experiment with using mmap instead of using a cache list on the heap.

- Prohibit files larger than 2 bytes from being cached (and then breaking everything...)

- Switch to port 80.
