/*
 * Minimal loopback static server: nserve [port] [--path directory].
 * The original nserve [port [directory]] syntax is also supported.
 * Serve GET/HEAD requests sequentially, then close each connection. Walk paths
 * relative to an open document root without following asset symlinks; serve
 * regular files and directory indexes with bounded headers and streaming I/O.
 * Build frontend assets separately before pointing nserve at their directory.
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
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 4096
#define TARGET_SIZE 2048
#define PORT_MIN 1024
#define PORT_MAX 65535

typedef struct {
	int listen_fd;
	int root_fd;
	int port;
	int failed;
} Server;

typedef struct {
	int head_only;
	char target[TARGET_SIZE];
	char path[TARGET_SIZE];
} Request;

typedef struct {
	int fd;
	off_t size;
	const char *content_type;
} File;

typedef enum {
	FILE_READY, DIRECTORY_REDIRECT, MISSING, FORBIDDEN, INTERNAL_ERROR
} ResolveResult;

static volatile sig_atomic_t stopping;

static void handle_signal(int number) {
	(void)number;
	stopping = 1;
}

static int parse_port(const char *value) {
	char *end;
	errno = 0;
	long port = strtol(value, &end, 10);
	if (errno || end == value || *end || port < PORT_MIN || port > PORT_MAX)
		return -1;
	return (int)port;
}

static void print_help(const char *program) {
	printf("Usage: %s [port] [--path directory]\n"
	       "       %s [port [directory]]\n"
	       "       %s -h | --help\n\n"
	       "Serve static files on 127.0.0.1. Stop with Ctrl+C.\n\n"
	       "Arguments and options:\n"
	       "  port              Port number, %d-%d (default: %d).\n"
	       "  --path directory  Relative or absolute document root (default: .).\n"
	       "                    Relative paths start at the current working directory.\n"
	       "  -h, --help        Show this help and exit.\n\n"
	       "Options may appear before or after the port. --path=directory also works.\n"
	       "Provide the directory once, using --path or the second positional argument.\n\n"
	       "Examples:\n"
	       "  nserve                         Serve the current directory on port 8080.\n"
	       "  nserve 9000                    Use port 9000.\n"
	       "  nserve --path ./dist           Serve a relative directory on port 8080.\n"
	       "  nserve 9000 --path /srv/www     Serve an absolute directory on port 9000.\n"
	       "  nserve --path \"./my site\" 9000 Quote paths containing spaces.\n"
	       "  nserve 8080 ./dist             Original positional syntax.\n",
	       program, program, program, PORT_MIN, PORT_MAX, DEFAULT_PORT);
}

static int server_init(Server *server, const char *root) {
	server->root_fd = open(root, O_RDONLY | O_DIRECTORY);
	if (server->root_fd < 0) {
		perror(root);
		return -1;
	}
	server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server->listen_fd < 0) {
		perror("socket");
		return -1;
	}
	int reuse = 1;
	if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR,
	              &reuse, sizeof(reuse)) < 0) {
		perror("setsockopt");
		return -1;
	}
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons((unsigned short)server->port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK)
	};
	if (bind(server->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind");
		return -1;
	}
	if (listen(server->listen_fd, 16) < 0) {
		perror("listen");
		return -1;
	}
	return 0;
}

static int send_all(int fd, const char *data, size_t length) {
	while (length > 0 && !stopping) {
		ssize_t sent = send(fd, data, length, 0);
		if (sent < 0 && errno == EINTR)
			continue;
		if (sent <= 0)
			return -1;
		data += sent;
		length -= (size_t)sent;
	}
	return length == 0 ? 0 : -1;
}

/* extra contains only server-generated headers and validated encoded targets. */
static int send_headers(int fd, const char *status, const char *type,
                        off_t length, const char *extra) {
	char headers[BUFFER_SIZE];
	int size = snprintf(headers, sizeof(headers),
		"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %jd\r\n"
		"Connection: close\r\n%s\r\n", status, type, (intmax_t)length, extra);
	if (size < 0 || (size_t)size >= sizeof(headers))
		return -1;
	return send_all(fd, headers, (size_t)size);
}

static void send_error(int fd, const char *status, int head_only) {
	size_t length = strlen(status);
	const char *extra = strncmp(status, "405", 3) == 0 ? "Allow: GET, HEAD\r\n" : "";
	if (send_headers(fd, status, "text/plain; charset=utf-8", (off_t)length, extra) == 0
	    && !head_only)
		send_all(fd, status, length);
}

static void send_redirect(int fd, const Request *request) {
	char extra[TARGET_SIZE + 32];
	size_t path_length = strcspn(request->target, "?");
	int size = snprintf(extra, sizeof(extra), "Location: %.*s/%s\r\n",
	                    (int)path_length, request->target, request->target + path_length);
	if (size < 0 || (size_t)size >= sizeof(extra)) {
		send_error(fd, "414 URI Too Long", request->head_only);
		return;
	}
	send_headers(fd, "301 Moved Permanently", "text/plain; charset=utf-8", 0, extra);
}

/* Return an HTTP error, or NULL on success; an empty error means disconnect. */
static const char *read_request(int fd, char buffer[BUFFER_SIZE], Request *request) {
	size_t used = 0, checked = 0;
	while (!stopping) {
		if (used == BUFFER_SIZE - 1)
			return "431 Request Header Fields Too Large";
		ssize_t count = recv(fd, buffer + used, BUFFER_SIZE - 1 - used, 0);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return "";
		used += (size_t)count;
		if (used >= 5 && memcmp(buffer, "HEAD ", 5) == 0)
			request->head_only = 1;
		/* Scan bytes, not C strings: reject NULs before the terminator, while
		 * ignoring arbitrary body/pipelined bytes received after it. */
		for (; checked < used; checked++) {
			unsigned char c = (unsigned char)buffer[checked];
			if ((c < 32 && c != '\r' && c != '\n' && c != '\t') || c == 127)
				return "400 Bad Request";
			if (checked >= 3 && memcmp(buffer + checked - 3, "\r\n\r\n", 4) == 0) {
				buffer[checked + 1] = '\0';
				return NULL;
			}
		}
	}
	return "";
}

static const char *parse_request(char *buffer, Request *request) {
	char *end = strstr(buffer, "\r\n");
	if (!end)
		return "400 Bad Request";
	*end = '\0';
	char *target = strchr(buffer, ' ');
	if (!target)
		return "400 Bad Request";
	*target++ = '\0';
	request->head_only = strcmp(buffer, "HEAD") == 0;
	char *version = strchr(target, ' ');
	if (!version)
		return "400 Bad Request";
	*version++ = '\0';
	if (!*buffer || (strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1")))
		return "400 Bad Request";
	/* HTTP methods are tokens, even when unsupported. */
	for (const unsigned char *p = (unsigned char *)buffer; *p; p++)
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
		      (*p >= '0' && *p <= '9') || strchr("!#$%&'*+-.^_`|~", *p)))
			return "400 Bad Request";
	if (target[0] != '/' || target[1] == '/')
		return "400 Bad Request";
	for (const unsigned char *p = (unsigned char *)target; *p; p++)
		if (*p <= 32 || *p == 127 || *p == '#' || *p == '\\')
			return "400 Bad Request";
	if (strlen(target) >= sizeof(request->target))
		return "414 URI Too Long";
	strcpy(request->target, target);
	if (strcmp(buffer, "GET") && !request->head_only)
		return "405 Method Not Allowed";
	return NULL;
}

static int hex_digit(unsigned char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static const char *decode_path(Request *request) {
	size_t length = strcspn(request->target, "?"), used = 0;
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)request->target[i];
		if (c == '%') {
			if (length - i < 3)
				return "400 Bad Request";
			int high = hex_digit(request->target[i + 1]);
			int low = hex_digit(request->target[i + 2]);
			if (high < 0 || low < 0)
				return "400 Bad Request";
			c = (unsigned char)(high * 16 + low);
			i += 2;
		}
		if (c < 32 || c == 127 || c == '\\')
			return "400 Bad Request";
		if (used == sizeof(request->path) - 1)
			return "414 URI Too Long";
		request->path[used++] = (char)c;
	}
	request->path[used] = '\0';
	return NULL;
}

static const char *mime_type(const char *name) {
	static const struct { const char *extension, *type; } types[] = {
		{"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
		{"css", "text/css; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"},
		{"mjs", "text/javascript; charset=utf-8"}, {"json", "application/json"},
		{"map", "application/json"}, {"svg", "image/svg+xml"},
		{"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
		{"gif", "image/gif"}, {"webp", "image/webp"}, {"ico", "image/vnd.microsoft.icon"},
		{"woff", "font/woff"}, {"woff2", "font/woff2"}, {"wasm", "application/wasm"},
		{"txt", "text/plain; charset=utf-8"}
	};
	const char *extension = strrchr(name, '.');
	if (extension)
		for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
			if (strcasecmp(extension + 1, types[i].extension) == 0)
				return types[i].type;
	return "application/octet-stream";
}

static ResolveResult open_error(void) {
	switch (errno) {
	case ENOENT: case ENOTDIR: case EACCES: case EPERM: case ELOOP:
	case ENAMETOOLONG: case ENXIO: case ENODEV:
		return MISSING;
	default:
		return INTERNAL_ERROR;
	}
}

static ResolveResult resolve_file(int root_fd, const Request *request, File *file) {
	char path[TARGET_SIZE];
	strcpy(path, request->path);
	/* Validate every component before touching the filesystem. */
	for (const char *p = path; *p;) {
		if (*p == '/') { p++; continue; }
		size_t length = strcspn(p, "/");
		if (length == 2 && p[0] == '.' && p[1] == '.')
			return FORBIDDEN;
		p += length;
	}
	int trailing_slash = path[strlen(path) - 1] == '/';
	int fd = dup(root_fd);
	if (fd < 0)
		return INTERNAL_ERROR;
	ResolveResult result = INTERNAL_ERROR;
	const char *name = "index.html";
	char *save;
	for (char *part = strtok_r(path, "/", &save); part;) {
		char *next = strtok_r(NULL, "/", &save);
		if (strcmp(part, ".") != 0) {
			int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK;
			if (next || trailing_slash)
				flags |= O_DIRECTORY;
			int child = openat(fd, part, flags);
			if (child < 0) {
				result = open_error();
				goto cleanup;
			}
			close(fd);
			fd = child;
			name = part;
		}
		part = next;
	}
	struct stat info;
	if (fstat(fd, &info) < 0)
		goto cleanup;
	if (S_ISDIR(info.st_mode)) {
		if (!trailing_slash) {
			result = DIRECTORY_REDIRECT;
			goto cleanup;
		}
		int index = openat(fd, "index.html", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
		if (index < 0) {
			result = open_error();
			goto cleanup;
		}
		close(fd);
		fd = index;
		name = "index.html";
		if (fstat(fd, &info) < 0)
			goto cleanup;
	}
	if (!S_ISREG(info.st_mode)) {
		result = MISSING;
		goto cleanup;
	}
	if (info.st_size < 0)
		goto cleanup;
	file->fd = fd;
	file->size = info.st_size;
	file->content_type = mime_type(name);
	return FILE_READY;
cleanup:
	close(fd);
	return result;
}

static void stream_file(int client, const File *file) {
	char buffer[BUFFER_SIZE];
	off_t remaining = file->size;
	while (remaining > 0 && !stopping) {
		size_t amount = remaining < (off_t)sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
		ssize_t count = read(file->fd, buffer, amount);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0 || send_all(client, buffer, (size_t)count) < 0)
			return;
		remaining -= count;
	}
}

static void handle_client(const Server *server, int fd) {
	/* Bound individual I/O waits, not the total request duration. */
	struct timeval timeout = {.tv_sec = 5};
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
	    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("client timeout");
		return;
	}
	char buffer[BUFFER_SIZE];
	Request request = {0};
	File file = {.fd = -1};
	const char *error = read_request(fd, buffer, &request);
	if (!error) error = parse_request(buffer, &request);
	if (!error) error = decode_path(&request);
	if (error) {
		if (*error) send_error(fd, error, request.head_only);
		return;
	}
	switch (resolve_file(server->root_fd, &request, &file)) {
	case FILE_READY:
		if (send_headers(fd, "200 OK", file.content_type, file.size, "") == 0 && !request.head_only)
			stream_file(fd, &file);
		close(file.fd);
		return;
	case DIRECTORY_REDIRECT:
		send_redirect(fd, &request);
		return;
	case MISSING: error = "404 Not Found"; break;
	case FORBIDDEN: error = "403 Forbidden"; break;
	case INTERNAL_ERROR: error = "500 Internal Server Error"; break;
	}
	send_error(fd, error, request.head_only);
}

static void server_loop(Server *server) {
	while (!stopping) {
		int client = accept(server->listen_fd, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			server->failed = 1;
			return;
		}
		handle_client(server, client);
		close(client);
	}
}

int main(int argc, char **argv) {
	int port = DEFAULT_PORT, port_seen = 0, options = 1;
	const char *root = NULL;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (options && (!strcmp(arg, "-h") || !strcmp(arg, "--help"))) {
			print_help(argv[0]);
			return EXIT_SUCCESS;
		}
		if (options && !strcmp(arg, "--")) {
			options = 0;
			continue;
		}
		if (options && (!strcmp(arg, "--path") || !strncmp(arg, "--path=", 7))) {
			if (root) goto invalid_arguments;
			if (arg[6] == '=') {
				root = arg + 7;
			} else {
				if (++i == argc || argv[i][0] == '-') goto invalid_arguments;
				root = argv[i];
			}
			if (!*root) goto invalid_arguments;
		} else if (options && arg[0] == '-') {
			goto invalid_arguments;
		} else if (!port_seen) {
			port = parse_port(arg);
			if (port < 0) goto invalid_arguments;
			port_seen = 1;
		} else {
			if (root || !*arg) goto invalid_arguments;
			root = arg;
		}
	}
	if (!root) root = ".";
	struct sigaction action = {0};
	action.sa_handler = handle_signal;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
	action.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &action, NULL) < 0) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
	Server server = {.listen_fd = -1, .root_fd = -1, .port = port};
	if (server_init(&server, root) < 0) {
		server.failed = 1;
	} else {
		printf("Serving %s at http://127.0.0.1:%d/ (Ctrl+C to stop)\n", root, server.port);
		fflush(stdout);
		server_loop(&server);
	}
	if (server.listen_fd >= 0) close(server.listen_fd);
	if (server.root_fd >= 0) close(server.root_fd);
	return server.failed ? EXIT_FAILURE : EXIT_SUCCESS;

invalid_arguments:
	fprintf(stderr, "Invalid arguments. Usage: %s [port] [--path directory]\n"
	        "Port must be %d-%d. Run '%s --help' for examples.\n",
	        argv[0], PORT_MIN, PORT_MAX, argv[0]);
	return EXIT_FAILURE;
}
