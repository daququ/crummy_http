#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void dump_bytes(char* buffer, int num_bytes)
{
  const int print_width = 16;
  int i, j;

  i = 0;

  /* Move through the buffer in chunks */
  for (i = 0; i < num_bytes; i += print_width) {

    /* Print the current chunk as hex */
    for (j = 0; j < print_width; j++) {
      if (i + j < num_bytes)
	printf("%02x ", buffer[i + j]);
      else
	printf("   ");
    }

    printf("| ");

    /* Now print it as ascii characters */
    for (j = 0; j < print_width; j++) {
      if (i + j < num_bytes) {
	if (buffer[i + j] > 31 && buffer[i + j] < 127)
	  printf("%c", buffer[i + j]);
	else
	  printf(".");
      }
    }

    printf("\n");
  }
}

int send_bytes(int socket, char* buffer)
{
  /* Ensures the entirety of the supplied buffer is sent */
  int bytes_to_send, sent_bytes;
  bytes_to_send = strlen(buffer);

  while (bytes_to_send > 0) {
    sent_bytes = send(socket, (void*)buffer, bytes_to_send, 0);
    if (sent_bytes == -1) {
      printf("Error sending bytes");
      return 0;
    }

    bytes_to_send -= sent_bytes;
    buffer += sent_bytes;
  }
  return 1;
}

int receive_bytes(int socket, char* buffer)
{
  /* Receives and loads bytes from socket into buffer until CR and NL characters
   * are received */

  int received_bytes, num_end_bytes;
  char receive_buffer;
  char* ptr = buffer;
  num_end_bytes = 0;

  received_bytes = recv(socket, (void*)&receive_buffer, 1, 0);

  while (received_bytes > 0) {
    /* Determine if the terminating characters are in the 
     * received string */
    if (receive_buffer == '\x0d' && num_end_bytes == 0)
      num_end_bytes++;
    else if (receive_buffer == '\x0a' && num_end_bytes == 1) {
      *ptr = 0;
      return strlen(buffer);
    }
    else
      num_end_bytes = 0;

    /* Copy the bytes over to the output buffer */
    *ptr = receive_buffer;

    received_bytes = recv(socket, (void*)&receive_buffer, 1, 0);

    ptr++;
  }

  return received_bytes;
}

void process_request(int socket, char* request_buffer)
{
  /* Process the request string in request buffer and
   * tries to look up the corresponding file */

  /* First determine whether the request is HTTP and whether
   * it's a get or head request*/
  char* result = strstr(request_buffer, "HTTP/");
  char* get_result = strstr(request_buffer, "GET");
  int file, num_bytes_read, total_bytes_read;
  char* ptr;
  char filename[100];
  char* file_contents;
  char file_buffer[100];

  if (result != NULL) {
    /* Set the pointer to the start of the file path specified in the
     * request */
    ptr = get_result != NULL ? request_buffer + 4 : request_buffer + 5;
    /* Put a string termination byte at the end of the file path */
    *(result - 1) = 0;
    /* Set up the file path */
    strcpy(filename, "public_html");
    strcat(filename, ptr);
    /* Add an index file to the file path if there's a trailing slash */
    if (filename[strlen(filename) - 1] == '/')
      strcat(filename, "index.html");
    /* Some output */
    printf("Request for file %s:", filename);
    /* Try to open the file */
    file = open(filename, O_RDONLY);
    /* Send the http header response specifying whether the file was found */
    if (file == -1) {
      send_bytes(socket, "HTTP/1.0 404 NOT_FOUND\r\n");
      printf(" HTTP/1.0 404 NOT_FOUND\n");
    }
    else {
      send_bytes(socket, "HTTP/1.0 200 OK\r\n");
      printf(" HTTP/1.0 200 OK\n");
    }

    send_bytes(socket, "Server: Web server\r\n\r\n");

    /* If the http request is a get request, try to return the file */
    if (get_result != NULL) {

      /* No file, so just return a simple 404 page */
      if (file == -1) {
	send_bytes(socket, "<html><head><title>404 Not Found</title></head>");
	send_bytes(socket, "<body><h1>404 Not Found</h1></body></html>\r\n");
      }
      /* File exists, so serve it up */
      else {
	/* Keep track of the number of bytes read so far */
	total_bytes_read = 0;
	/* Read the first load of bytes */
	num_bytes_read = read(file, file_buffer, 100);
	/* Set the file contents pointer */
	file_contents = NULL;
	
	while (num_bytes_read > 0) {
	  /* Re-allocate memory to accomodate the next chunk of the file */
	  file_contents = (char*)realloc((void*)file_contents,
					 total_bytes_read + num_bytes_read);

	  /* Increment the pointer to the file contents buffer ready for
	   * writing */
	  ptr = file_contents + total_bytes_read;
	  /* Copy the contents of the loaded bytes to the appropriate point
	   * in the file contents buffer */
	  strcpy(ptr, file_buffer);
	  /* Terminate the string we've just copied */
	  ptr[num_bytes_read] = 0;

	  /* Increment the number of bytes read and read a new segment from
	   * the file */
	  total_bytes_read += num_bytes_read;
	  num_bytes_read = read(file, file_buffer, 100);
	  /* Terminate the string at the place we've just copied up to 
	   * to prevent string-termination issues from arising on calling
	   * strcpy */
	  file_buffer[num_bytes_read] = 0;
	}
	
	/* Send the resulting file contents to the client, then free the
	 * file buffer */
	send_bytes(socket, file_contents);
	send_bytes(socket, "\r\n");
	free(file_contents);
      }
    }
  }
}

int main(int argc, char** argv)
{
  short int host_port = 8000;
  int listen_socket, accept_socket;
  struct sockaddr_in host_address, client_addr;
  socklen_t client_socket_length;
  int yes = 1;
  char receive_buffer[128];

  /* If a different port is specified, set it */
  if (argc >= 2)
    host_port = atoi(argv[1]);

  /* Print the port we're listening on */
  printf("Listening on port %d...\n\n", host_port);

  /* Create the host socket to listen on */
  if ((listen_socket = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
    printf("Error: socket creation failed.");
    exit(1);
  }

  /* Make sure the address provided can be bound to the socket
   * (ignore any existing sockets when binding address) */
  if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes,
		 sizeof(int)) == -1) {
    printf("Error: failed to set socket option.\n");
    exit(1);
  }

  /* Set up the address to bind to the listening socket */

  /* Internet address family (IP address) */
  host_address.sin_family = AF_INET;
  /* Set host port (switch byte order) */
  host_address.sin_port = htons(host_port);
  /* Set the host ip address (use any) */
  host_address.sin_addr.s_addr = INADDR_ANY;
  /* Zero the remaining 8 bytes */
  memset(&(host_address.sin_zero), '\0', 8);

  /* Bind the address to the socket */

  if (bind(listen_socket, (struct sockaddr*)&host_address,
	   sizeof(host_address))) {
    printf("Error: failed on binding socket to address.\n");
    exit(1);
  }

  /* Listen to the bound address on the existing socket */

  if (listen(listen_socket, 1) == -1) {
    printf("Error: failed on listening using socket.\n");
    exit(1);
  }

  /* Keep looping and accepting connections from the socket */

  while (1) {
    client_socket_length = sizeof(struct sockaddr_in);

    /* Accept the next incoming connection in the queue */
    if ((accept_socket = accept(listen_socket, (struct sockaddr*)&client_addr,
				&client_socket_length)) == -1) {
      printf("Error: failed to accept incoming connection.\n");
      exit(1);
    }

    /* Print some info about the new connection */
    printf("Received connection from client with address %s on port %d:\n",
	   inet_ntoa(client_addr.sin_addr),
	   ntohs(client_addr.sin_port));

    /* Load up the bytes from the incoming connection into the
     * receiving buffer */
    receive_bytes(accept_socket, receive_buffer);
    process_request(accept_socket, receive_buffer);
    shutdown(accept_socket, SHUT_RDWR);
  }

  return 0;
}
