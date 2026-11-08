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
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 4096
#define PORT_MIN 1024
#define PORT_MAX 65535

typedef struct {
	int fd;
	int port;
	int failed;
} Server;

static int parse_port(const char *value) {
	char *end;
	errno = 0;
	long port = strtol(value, &end, 10);
	if (errno || end == value || *end || port < PORT_MIN || port > PORT_MAX)
		return -1;
	return (int)port;
}

static int server_init(Server *server, int port) {
	// set default param values for 
	server->fd = -1;
	server->port = port;
	server->failed = 0;
	// connect socket
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket error");
		return -1;
	}

	// allow restart without waiting on old connections
	int reuse = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
		perror("setsockopt");

	// configure address options 
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons((unsigned short)port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK)
	};
	// bind that socket to that address
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}
	// have socket listen on that port
	if (listen(fd, 16) < 0) {
		perror("listen");
		close(fd);
		return -1;
	}
	server->fd = fd;
	return 0;
}

static void server_close(Server *server) {
	if (server->fd >= 0) {
		close(server->fd);
		server->fd = -1;
	}
}

/* A successful send can still write fewer bytes than requested. */
static int send_all(int fd, const char *data, size_t length) {
	while (length > 0) {
		ssize_t sent = send(fd, data, length, 0);
		if (sent < 0 && errno == EINTR)
			continue;
		if (sent <= 0)
			return -1;
		data += sent;
		length -= (size_t)sent;
	}
	return 0;
}

static int send_headers(int fd, const char *status, off_t length) {
	char headers[256];
	int size = snprintf(headers, sizeof(headers),
		"HTTP/1.1 %s\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Content-Length: %jd\r\n"
		"Connection: close\r\n\r\n", status, (intmax_t)length);
	if (size < 0 || (size_t)size >= sizeof(headers))
		return -1;
	return send_all(fd, headers, (size_t)size);
}

static void send_error(int fd, const char *status, int head_only) {
	size_t length = strlen(status);
	if (send_headers(fd, status, (off_t)length) == 0 && !head_only)
		send_all(fd, status, length);
}

static void server_handle_client(Server *server, int fd) {
	(void)server;
	char buffer[BUFFER_SIZE];
	size_t used = 0;

	/* Don't let a browser's idle connection hold up subsequent requests. */
	struct timeval timeout = {.tv_sec = 5};
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
		return;

	/* TCP may deliver the request headers across several reads. */
	buffer[0] = '\0';
	while (!strstr(buffer, "\r\n\r\n")) {
		if (used == sizeof(buffer) - 1) {
			send_error(fd, "431 Request Header Fields Too Large", 0);
			return;
		}
		ssize_t received = recv(fd, buffer + used, sizeof(buffer) - 1 - used, 0);
		if (received < 0 && errno == EINTR)
			continue;
		if (received <= 0)
			return;
		used += (size_t)received;
		buffer[used] = '\0';
	}

	char method[16], path[4096], version[16];
	if (sscanf(buffer, "%15s %4095s %15s", method, path, version) != 3 ||
		(strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1"))) {
		send_error(fd, "400 Bad Request", 0);
		return;
	}
	int head_only = strcmp(method, "HEAD") == 0;
	if (strcmp(method, "GET") && !head_only) {
		send_error(fd, "501 Not Implemented", 0);
		return;
	}

	/* Ignore query parameters, e.g. /index.html?refresh=1. */
	path[strcspn(path, "?")] = '\0';
	if (strcmp(path, "/") && strcmp(path, "/index.html")) {
		send_error(fd, "404 Not Found", head_only);
		return;
	}

	int file = open("index.html", O_RDONLY);
	struct stat info;
	if (file < 0 || fstat(file, &info) < 0 || !S_ISREG(info.st_mode)) {
		if (file >= 0)
			close(file);
		send_error(fd, "404 Not Found", head_only);
		return;
	}

	if (send_headers(fd, "200 OK", info.st_size) == 0 && !head_only) {
		off_t remaining = info.st_size;
		while (remaining > 0) {
			size_t amount = remaining < (off_t)sizeof(buffer)
				? (size_t)remaining : sizeof(buffer);
			ssize_t count = read(file, buffer, amount);
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0 || send_all(fd, buffer, (size_t)count) < 0)
				break;
			remaining -= count;
		}
	}
	close(file);
}

static volatile sig_atomic_t stopping = 0;

static void handle_sigint(int signal_number) {
	(void)signal_number;
	stopping = 1;
}

static void server_loop(Server *server) {
	while (!stopping) {
		int client = accept(server->fd, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			server->failed = 1;
			return;
		}
		server_handle_client(server, client);
		close(client);
	}
}

int main(int argc, char **argv) {
	// parse port if arg provided, else default
	int port = argc == 2 ? parse_port(argv[1]) : DEFAULT_PORT;
	// display usage if args incorrect
	if (argc > 2 || port < 0) {
		fprintf(stderr, "Usage: %s [port: %d-%d]\n", argv[0], PORT_MIN, PORT_MAX);
		return EXIT_FAILURE;
	}

	struct sigaction action = {0};
	action.sa_handler = handle_sigint;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) < 0) {
		perror("sigaction");
		return EXIT_FAILURE;
	}

	// prevent client discon from terminating server
	signal(SIGPIPE, SIG_IGN);
	// create server on port
	Server server;
	if (server_init(&server, port) < 0)
		return EXIT_FAILURE;

	printf("Serving ./index.html at http://127.0.0.1:%d/ (Ctrl+C to stop)\n", server.port);
	fflush(stdout);
	server_loop(&server);
	server_close(&server);
	return server.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
