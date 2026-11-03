/*
 * create TCP socket - socket(AF_INET, SOCK_STREAM, 0)
 * bind to a port    - bind() attaches to 127.0.0.1:8080
 * listen for conn   - listen() waits for incoming reqs
 * accept conn       - accept() establishes a conn w browser
 * read HTTP req     - recv() reads the browser's GET HTTP 1.1 req
 * load index.html   - fopen() and fread() read html file into mem
 * send response     - send() 
 * 		       HTTP/1.1 200 OK
 * 		       Content-Type: text/html
 * 		       Content-Length <file size>
 * 		       Blank line => html content
 * close conn        - release client socket
 * repeat            - loop back to accept() for next req
 *
 * Uses:
 * 	- socket()
 * 	- bind()
 * 	- listen()
 * 	- accept()
 * 	- recv()
 * 	- fopen() / fread()
 * 	- send()
 * 	- close()
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 4096

static int write_all(int fd, const void *data, size_t size) 
{
	const char *p = data;

	while (size > 0) {
		ssize_t n = write(fd, p, size);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		p += n;
		size -= (size_t)n;
	}
}

static void serve(int client) 
{
	char request[BUFFER_SIZE];
	ssize_t n;

	for (;;) {
		n = read(client, request, sizeof request);
		if (n < 0 && errno == EINTR) continue;
		break;
	}
}








 	






