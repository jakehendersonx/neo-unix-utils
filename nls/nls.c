
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#define BUF_SIZE 1024

int main(void) {
    int fd;
    char buf[BUF_SIZE];
    long nread;
    char *bpos;
    struct dirent64 *d;

    /* open() is a thin wrapper over the open system call */
    fd = open(".", O_RDONLY | O_DIRECTORY);
    if (fd == -1) {
        char err_msg[] = "you can't open that one...\n";
        write(2, err_msg, sizeof(err_msg) - 1);
        return 1;
    }

    /* Read directory entries into the buffer */
    for (;;) {
        nread = getdents64(fd, buf, BUF_SIZE);
        if (nread == -1) {
            return 1;
        }

        if (nread == 0)
            break;

        for (bpos = buf; bpos < buf + nread;) {
            d = (struct dirent64 *) bpos;
            
            /* Filter out hidden files that start with . */
            if (d->d_name[0] != '.') {
                int len = 0;
                while (d->d_name[len] != '\0') {
                    len++;
                }
                /* write() is a thin wrapper over the write system call */
                write(1, d->d_name, len);
                write(1, "\n", 1);
            }
            bpos += d->d_reclen;
        }
    }

    /* close() is a thin wrapper over the close system call */
    close(fd);
    return 0;
}
