# Building an HTTP Server in C

## Overview
I am beginning a personal project to create a web server in C. 

**server.c Flow So Far**: 
- Creates a new socket, binds it using the IPv4 option, and listens using the file descriptor for the new socket.   
- Enters infinite while loop for accepting connections.
- Accepts a connection and assigns a file descriptor for the new client.
- Reads from the file descriptor into a buffer.
- Parses the buffer to extract the method (GET, POST, PUT, etc.) and the path/route.
- Creates new POSIX thread and invokes a 'start routine' serve_file_aux() to attempt to serve the requested file to the client, passing the client file descriptor and filepath.
- Continue in loop, accepting up to 3 connections.

## Intended Features

- THREAD_COUNT global variable that acts as a pool of available threads such that the server could serve up to THREAD_COUNT connections at once, and listen for SOMAXCONN connections in a backlog.

- Modify serving based on method and file type.

- Already have a pool of threads created before accepting to speed up serving.

- Log requests to server periodically.

- Cache repeatedly served files into RAM or mmap into the main process address space (I need to look into this first).

- Switch to port 80.
