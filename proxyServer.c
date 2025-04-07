#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ctype.h>
#include <arpa/inet.h>


#define MAX_BUFFER_SIZE 4096
#define MAX_DATE_LENGTH 50
#define INITIAL_BUFFER_SIZE 1024

void parse_command_line_arguments(int argc, char *argv[], int *port, int *pool_size, int *max_requests,
                                  char **filter_file);

void handle_conn(void *arg);

int compare_host(const char *host, const char *filter_address);

int get_mask_length(int mask);

char *ip_to_binary(const char *ip_address);

char *create_client_error(char *error_header, char *error_body);

char *get_current_date();

void close_connection_header(char *request);

char *get_ipv4_address(const char *hostname);

int is_method_exist(char *request);

int is_protocol_exist(char *request);

int get_origin_host_and_port(const char *request, char *origin_host_name, int *origin_port);

char *read_file_content(const char *file_path);

int filter_address_from_string(char *host, const char *file_content);


struct dispatch_args {
    intptr_t client_fd;
    char* filter_file_content;
};


/*  Input:
     proxyServer <port> <pool-size> <max-number-of-request> <filter>
     port - my port as a server
     pool-size - max number of threads to create
     max-number-of-request - maximum requests of clients (maximum threads to use from pool-size)
     filter - path to the addresses filter file
     */
int main(int argc, char *argv[]) {
    // creating our infos struct
    struct dispatch_args my_dispatch_arg;


    // parsing arguments
    int port, pool_size, max_requests;
    char *filter_file_path;
    parse_command_line_arguments(argc, argv, &port, &pool_size, &max_requests, &filter_file_path);

    // reading filter file content into a string that will be sent through the struct
    my_dispatch_arg.filter_file_content = read_file_content(filter_file_path);

    // creating threadpool
    threadpool *tp = create_threadpool(pool_size);

    // creating server's welcome socket
    int welcome_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (welcome_socket == -1) {
        perror("socket\n");
        free(my_dispatch_arg.filter_file_content);
        exit(EXIT_FAILURE);
    }

    // constructing welcome server's information
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    // bind the server's welcome socket to the address and port specified in the server_address structure.
    if (bind(welcome_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror("bind\n");
        close(welcome_socket);
        free(my_dispatch_arg.filter_file_content);
        exit(EXIT_FAILURE);
    }

    // waiting for clients connections
    if (listen(welcome_socket, 5) == -1) {
        perror("listen\n");
        close(welcome_socket);
        free(my_dispatch_arg.filter_file_content);
        exit(EXIT_FAILURE);
    }

    // getting requests from clients and dispatching them
    for (int requests_sent = 0; requests_sent < max_requests; requests_sent++) {

        struct sockaddr_in client_info = {0};
        socklen_t client_len = sizeof(client_info);

        my_dispatch_arg.client_fd = accept(welcome_socket, (struct sockaddr *) &client_info, &client_len);
        if ((int) my_dispatch_arg.client_fd == -1) {
            perror("accept\n");
            close(welcome_socket);
            free(my_dispatch_arg.filter_file_content);
            exit(EXIT_FAILURE);
        }
//        printf("----------------------------- current request: %d -----------------------------\n", requests_sent+1);

        dispatch(tp, (void *) handle_conn, (void *) &my_dispatch_arg);
    }


//    printf("----------------------------- clients ended -----------------------------\n");
    destroy_threadpool(tp);
    close(welcome_socket);
    free(my_dispatch_arg.filter_file_content);
    return 0;
}

void parse_command_line_arguments(int argc, char *argv[], int *port, int *pool_size, int *max_requests,
                                  char **filter_file) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <port> <pool-size> <max-number-of-request> <filter>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Convert command line arguments to long using strtol
    *port = (int) strtol(argv[1], NULL, 10);
    *pool_size = (int) strtol(argv[2], NULL, 10);
    *max_requests = (int) strtol(argv[3], NULL, 10);
    *filter_file = argv[4];

    // Check for conversion errors
    if (*port < 0 || *port > 65535 || *pool_size <= 0 || *max_requests <= 0) {
        fprintf(stderr, "Usage: %s <port> <pool-size> <max-number-of-request> <filter>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

int is_method_exist(char *request) {
    if (strncmp("GET", request, 3) == 0) {
        return 1;
    }
    return 0;
}

int is_protocol_exist(char *request) {
    if (strstr(request, "HTTP/1.1") == NULL && strstr(request, "HTTP/1.0") == NULL) {
        return 0;
    }
    return 1;
}

char *read_file_content(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        perror("fopen\n");
        return NULL;
    }

    // Get the size of the file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        perror("ftell\n");
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    // Allocate memory for the content string
    char *content = (char *)malloc(file_size + 1); // +1 for null terminator
    if (content == NULL) {
        perror("malloc\n");
        fclose(file);
        return NULL;
    }

    // Read the file content into the buffer
    size_t bytes_read_total = 0;
    size_t bytes_read;
    while ((bytes_read = fread(content + bytes_read_total, 1, file_size - bytes_read_total, file)) > 0) {
        bytes_read_total += bytes_read;
    }

    if (ferror(file)) {
        perror("ferror\n");
        free(content);
        fclose(file);
        return NULL;
    }

    // Null-terminate the content string
    content[bytes_read_total] = '\0';

    // Close the file before returning
    if (fclose(file) != 0) {
        perror("fclose\n");
        free(content);
        return NULL;
    }

    return content;
}

void handle_conn(void *arg) {
    int client_fd = (int) ((struct dispatch_args *) arg)->client_fd;
    char* filter_content = (char*) ((struct dispatch_args *) arg)->filter_file_content;

    char response_buffer[MAX_BUFFER_SIZE];
    // Allocate memory with a larger initial size
    char *client_request = (char *)malloc(INITIAL_BUFFER_SIZE);
    if (client_request == NULL) {
        perror("malloc\n");
        // Handle error
        close(client_fd);
        return;
    }
    size_t buffer_capacity = INITIAL_BUFFER_SIZE; // Track the current buffer capacity
    size_t requestSize = 0;
    ssize_t bytes_received;

    while ((bytes_received = read(client_fd, response_buffer, sizeof(response_buffer) - 1)) > 0) {
        // Check if the current buffer size is sufficient
        if (requestSize + bytes_received >= buffer_capacity) {
            // Reallocate memory in larger chunks to reduce overhead
            buffer_capacity *= 2; // Double the buffer capacity
            char *temp = realloc(client_request, buffer_capacity);
            if (temp == NULL) {
                perror("realloc\n");
                // Handle error
                free(client_request);
                close(client_fd);
                return;
            }
            client_request = temp;
        }

        // Copy the received data to the client_request buffer
        memcpy(client_request + requestSize, response_buffer, bytes_received);
        requestSize += bytes_received;
        client_request[requestSize] = '\0';  // Ensure null-termination

        // Check if the delimiter "\r\n\r\n" is present in the client_request
        if (strstr(client_request, "\r\n\r\n") != NULL) {
            break;
        }
    }

    if (bytes_received <= 0) {
        // Free the dynamically allocated memory
        free(client_request);
        close(client_fd);
        return;
    }

    int method_flag = is_method_exist(client_request);
    int protocol_flag = is_protocol_exist(client_request);

    if (protocol_flag == 0) {
        // Send a 400 "Bad Request" response
        char *response = create_client_error("400 Bad Request", "Bad Request.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        free(client_request);
        close(client_fd);
        return;
    }

    char origin_host_name[MAX_BUFFER_SIZE]; // Assuming a maximum host name length of 255 characters
    int origin_port;

    if (get_origin_host_and_port(client_request, origin_host_name, &origin_port) == -1){
        // Send a 400 "Bad Request" response
        char *response = create_client_error("400 Bad Request", "Bad Request.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        close(client_fd);
        free(client_request);
        return;
    }

//    printf("Origin Host Name: %s\n", origin_host_name);
//    printf("Origin Port: %d\n", origin_port);

//    char *temp_host_header = strstr(origin_host_name, "Host: ");
//    if (temp_host_header == NULL) {
    if (strlen(origin_host_name) == 0) {
        // Send a 400 "Bad Request" response
        char *response = create_client_error("400 Bad Request", "Bad Request.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        close(client_fd);
        free(client_request);
        return;
    }

    // Check if the method is GET
    if (method_flag == 0) {
        // Send a 501 "Not Implemented" response
        char *response = create_client_error("501 Not Supported", "Method is not supported.");
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        free(response);
        free(client_request);
        return;
    }

    // Connect to the origin server
    struct hostent *host_entry = gethostbyname(origin_host_name);
    if (host_entry == NULL) {
        char *response = create_client_error("404 Not Found", "File not found.");
        send(client_fd, response, strlen(response), 0);
        herror("gethostbyname\n");
        free(response);
        free(client_request);
        close(client_fd);
        return;
    }


    // Check for host/ip not on the list
    int filter_status = filter_address_from_string(origin_host_name, filter_content);
    if (filter_status == 0) {
        char *response = create_client_error("403 Forbidden", "Access denied.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        close(client_fd);
        free(client_request);
        return;
    }

    else if(filter_status == -1){
        char *response = create_client_error("500 Internal Server Error", "Some server side error.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        free(client_request);
        close(client_fd);
        return;
    }

    // Create a socket for origin server
    int origin_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (origin_server_fd == -1) {
        perror("socket\n");
        char *response = create_client_error("500 Internal Server Error", "Some server side error.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        free(client_request);
        close(client_fd);
        return;
    }

    // Connect to origin server
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(origin_port);
    server_address.sin_addr = *((struct in_addr *) host_entry->h_addr);

    if (connect(origin_server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror("connect\n");
        char *response = create_client_error("500 Internal Server Error", "Some server side error.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        close(origin_server_fd);
        free(client_request);
        close(client_fd);
        return;
    }

    // close request connection
    close_connection_header(client_request);

    // Send client_request to origin server
    if (write(origin_server_fd, client_request, strlen(client_request)) == -1) {
        perror("write\n");
        char *response = create_client_error("500 Internal Server Error", "Some server side error.");
        send(client_fd, response, strlen(response), 0);
        free(response);
        close(origin_server_fd);
        free(client_request);
        close(client_fd);
        return;
    }

    // Forward response from origin server to client
    ssize_t origin_bytes_received;
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_sent;
    //read the data the client ask from the server in chunks
    while ((origin_bytes_received = read(origin_server_fd, buffer, sizeof(buffer))) > 0) {
        //sending the data from the server to the client
        bytes_sent = write(client_fd, buffer, origin_bytes_received);
        if (bytes_sent == -1) {
            perror("write\n");
            close(origin_server_fd);
            close(client_fd);
            free(client_request);
            return;
        }
    }

    // Close the sockets and free the memory
    close(client_fd);
    close(origin_server_fd);
    free(client_request);
}

int filter_address_from_string(char *host, const char *file_content) {
    if (host == NULL || file_content == NULL) {
        return -1; // Invalid input
    }

    // Make a copy of file_content
    char *content_copy = strdup(file_content);
    if (content_copy == NULL) {
        return -1; // Memory allocation failed
    }

    // Get the IP version of the client's host
    char *client_ip = get_ipv4_address(host);
    if (client_ip == NULL) {
        free(content_copy); // Free dynamically allocated memory
        return -1; // Error getting client IP
    }

    // Parse the copied file content line by line
    const char *line = strtok(content_copy, "\n");
    while (line != NULL) {
        if (strlen(line) == 0) {
            // Skip empty lines
            line = strtok(NULL, "\n");
            continue;
        }

        // Compare the client's host with the filter file entry
        if (compare_host(host, line) == 0 || compare_host(client_ip, line) == 0) {
            free(client_ip); // Free dynamically allocated memory
            free(content_copy); // Free dynamically allocated memory
            return 0; // Match found, host is'nt allowed
        }

        // Move to the next line
        line = strtok(NULL, "\n");
    }

    free(client_ip); // Free dynamically allocated memory
    free(content_copy); // Free dynamically allocated memory
    return 1; // No match found, host is allowed
}

int compare_host(const char *host, const char *filter_address) {
    // return:   1 = no match          0 = match
    char filt[256];
    memset(filt, 0, 256);
    strcpy(filt, filter_address);

    // Determine if the host is an IP or a hostname (types: IP = 0, Hostname = 1)
    int host_type = (isdigit(host[0]) == 0) ? 1 : 0;
    int filter_address_type = (isdigit(filt[0]) == 0) ? 1 : 0;

    // if different types, don't check
    if (host_type != filter_address_type) {
        return 1;
    }

    // if we need to check two IPs
    if (host_type == 0 && filter_address_type == 0) {
        int mask;

        // get the filter address subnet mask
        char *mask_ptr = strchr(filt, '/');
        if (mask_ptr == NULL) { // if there's no slash == no mask in IP
            mask = 32;
        }
        else { // Mask exist
            mask_ptr++; // remove slash
            mask = (int) strtol(mask_ptr, NULL, 10); // converting string to int
        }
        // removing slash and mask from filter address string
        int mask_length = get_mask_length(mask);
        filt[strlen(filt) - mask_length - 1] = '\0';

        // converting both ip's to binary
        char *filt_binary = ip_to_binary(filt);
        char *host_binary = ip_to_binary(host);

        // comparing both binary numbers until n = mask
        int res = strncmp(host_binary, filt_binary, mask);

        free(filt_binary);
        free(host_binary);

        if (res == 0) { // match
            return 0;
        }

        else { // no match
            return 1;
        }

    }

        // if we need to check two hostnames
    else {
        int res = strcmp(host, filter_address);

        if (res == 0) {
            return 0;
        }

        else {
            return 1;
        }
    }
}

char *get_ipv4_address(const char *hostname) {
    struct addrinfo hints, *result, *rp;
    int status;

    // Setup hints to specify IPv4 and stream socket
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;      // Use IPv4
    hints.ai_socktype = SOCK_STREAM; // Use a stream socket

    // Call getaddrinfo to get the list of addresses
    status = getaddrinfo(hostname, NULL, &hints, &result);
    if (status != 0) {
        perror("getaddrinfo\n");
        return NULL;
    }

    char *ipstr = NULL;

    // Find the first IPv4 address in the result
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *) rp->ai_addr;
            ipstr = malloc(INET_ADDRSTRLEN);
            if (ipstr == NULL) {
                perror("malloc\n");
                freeaddrinfo(result); // Free the allocated memory
                return NULL;
            }
            // Convert the IPv4 to a string
            inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, INET_ADDRSTRLEN);
            break;
        }
    }

    freeaddrinfo(result); // Free the allocated memory

    if (ipstr == NULL) {
        return NULL; // Make sure to return NULL if no IPv4 address found
    }

    return ipstr;
}

int get_mask_length(int mask) {
    int count = 0;
    // Handle the case where num is 0 separately
    if (mask == 0) {
        return 1;
    }

    while (mask > 0) {
        mask /= 10;
        count++;
    }

    return count;
}

char *ip_to_binary(const char *ip_address) {
    if (ip_address == NULL) {
        return NULL;
    }

    char binary_ip[32]; // IPv4 addresses are 32 bits
    int binary_index = 0;

    // Split the IP address into octets
    char octet[4];
    int octet_index = 0;

    for (int i = 0; i <= strlen(ip_address); i++) {
        if (ip_address[i] == '.' || ip_address[i] == '\0') {
            octet[octet_index] = '\0';
            octet_index = 0;

            // Convert octet to binary and append to the result
            int octet_value = (int) strtol(octet, NULL, 10);
            for (int j = 7; j >= 0; j--) {
                binary_ip[binary_index++] = (octet_value & (1 << j)) ? '1' : '0';
            }
        }
        else {
            octet[octet_index++] = ip_address[i];
        }
    }

    // Null-terminate the binary string
    binary_ip[binary_index] = '\0';

    // Duplicate and return the binary string
    char *result = strdup(binary_ip);
    if (result == NULL) {
        perror("strdup\n");
        return NULL;
    }

    return result;
}

void close_connection_header(char *request) {
    // Find the position of "Connection:" header
    char *connectionStart = strstr(request, "Connection:");
    if (connectionStart != NULL) {
        // Check if "Connection: close" already exists
        if (strstr(connectionStart, "close") != NULL) {
            // "Connection: close" already exists, nothing to do
            return;
        }

        // If "Connection: keep-alive" exists, replace it with "Connection: close"
        char *connectionEnd = strchr(connectionStart, '\n');
        if (connectionEnd != NULL) {
            // Calculate the length of the portion to be moved
            size_t moveLength = strlen(connectionEnd + 1);
            // Move the remaining part of the string to overwrite "Connection: keep-alive"
            memmove(connectionStart, "Connection: close\r\n", strlen("Connection: close\r\n"));
            // Copy the remaining part after the replaced string
            memmove(connectionStart + strlen("Connection: close\r\n"), connectionEnd + 1, moveLength);
            // Null-terminate the modified request string
            *(connectionStart + strlen("Connection: close\r\n") + moveLength) = '\0';
        }
    }
    else {
        // Delete the last "\r\n" of the previously last line if it exists
        size_t requestLength = strlen(request);
        if (requestLength >= 2 && request[requestLength - 2] == '\r' && request[requestLength - 1] == '\n') {
            request[requestLength - 2] = '\0';
        }
        // Add "Connection: close" at the end
        strcat(request, "Connection: close\r\n\r\n");
    }
}

char *create_client_error(char *error_header_name, char *error_body_message) {
    char error_header[MAX_BUFFER_SIZE] = {0};
    char *date_line = get_current_date();
    char error_body[MAX_BUFFER_SIZE] = {0};

    sprintf(error_body, "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n<BODY><H4>%s</H4>\r\n%s\r\n</BODY></HTML>",
            error_header_name, error_header_name, error_body_message); // need to add \r\n\r\n after body ended?
    int content_length = (int) strlen(error_body);

    sprintf(error_header,
            "HTTP/1.1 %s\r\nServer: webserver/1.0\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
            error_header_name, date_line, content_length);

    char *error_request = (char *) malloc(2 * MAX_BUFFER_SIZE);
    memset(error_request, '\0', MAX_BUFFER_SIZE);
    strcpy(error_request, error_header);
    strcat(error_request, error_body);
    free(date_line);
    return error_request;
}

char *get_current_date() {
    // Get current time in seconds since the epoch
    time_t current_time;
    time(&current_time);

    // Convert to struct tm in UTC (GMT)
    struct tm *gm_time = gmtime(&current_time);

    // Format the date string
    char formatted_date[MAX_DATE_LENGTH];
    strftime(formatted_date, sizeof(formatted_date), "%a, %d %b %Y %H:%M:%S GMT", gm_time);

    // Duplicate and return the formatted date string
    char *date = strdup(formatted_date);

    return date;
}

int get_origin_host_and_port(const char *request, char *origin_host_name, int *origin_port) {
    // Find the start of the Host header
    const char *host_start = strstr(request, "Host: ");
    if (host_start == NULL) {
        return -1;
    }

    // Move the pointer to the start of the host name
    host_start += 6; // Length of "Host: "

    // Find the end of the host name
    const char *host_end = strstr(host_start, "\r\n");
    if (host_end == NULL) {
        return -1;
    }

    // If there's nothing after "Host: ", return
    if (host_end - host_start <= 0) {
        return -1;
    }

    // Calculate the length of the host name
    size_t host_length = host_end - host_start;

    // Extract the host name
    strncpy(origin_host_name, host_start, host_length);
    origin_host_name[host_length] = '\0';

    // Find the port number in the host name
    char *port_separator = strchr(origin_host_name, ':');
    if (port_separator != NULL) {
        // Extract port number
        *origin_port = (int) strtol(port_separator + 1, NULL, 10);
        // Remove port number from host name
        *port_separator = '\0';
    }
    else {
        // No port specified, default to port 80
        *origin_port = 80;
    }

    return 1;
}