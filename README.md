# HTTP Proxy (server side)
Implemented a multithreading HTTP proxy server to handle client requests efficiently. Validates requests, filters based on predefined rules, and forwards legitimate requests to the appropriate web server.

proxyServer.c - Proxy server simulator. Receive GET request from client, send it to the server, and forwards server response back to the client
threadpool.c - Responsible for creating and managing the threads. Each thread handles a single client request.

This program acts as a proxy server, receiving HTTP requests from clients, filtering requests based on a provided filter
file, and forwarding valid requests to the requested origin server and back to the client. It supports multi-threading
using a thread pool for handling multiple client requests concurrently.
