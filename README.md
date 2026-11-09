# unix-utils

Custom implementations of classic UNIX cli utils and a few more useful ones.

The rules: 
- yes C std lib
- yes UNIX/POSIX interfaces
- no third parties.

### classic utils

touch - open, utimensat, close
ls    - opendir, readdir, closedir, stat, lstat
cat   - open, read, write, close
mkdir - mkdir
mv    - rename
rm    - unlink, rmdir, lstat, opendir, readdir, closedir
find  - lstat, opendir, readdir, closedir, fnmatch

### more

nserve - http.server replacement for quick file servers and frontends
ninfo  - custom info page

### installing system wide

create the binary
```
clang program.c -o program
```
install executable into directory on `PATH`
```
sudo install -m 755 program /usr/local/bin/program
```

