// neo-cat
//
// take a file arg
// get that file descriptor
// inter
// 	read chunk
// 	write chunk
//
// This is an inefficient program. Does not
// mmap to minimize syscalls. A good opportunity to 
// use strace to optimize.
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {

	if (argc < 2) {
		perror("gotta gimme some to cat");
		return 1;
	}

	int fd;
	char *filename = argv[1];
	// read only, close after exec, no perms applied
	fd = open(filename, O_RDONLY | O_CLOEXEC, 0);
	if (fd == -1) {
		perror("can't open that ...");
		return 1;
	}

	int buffer_size = 1024;
	char buffer[buffer_size];
	ssize_t bytes_read;
	// read(file_descroptor, buffer, buffer_size)
	while ((bytes_read = read(fd, buffer, buffer_size)) > 0) {
		ssize_t bytes_written = 0;

		while (bytes_written < bytes_read) {
			ssize_t result = write(STDOUT_FILENO, buffer + bytes_written, bytes_read - bytes_written);
			if (result < 0) {
				break;
			}
			bytes_written += result;
		}
	}
	if (bytes_read < 0) {
		// a read error or some
	}
	close(fd);
}
