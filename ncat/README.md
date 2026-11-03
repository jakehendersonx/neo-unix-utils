test neo-cat vs cat

```
strace -o cat.txt cat shakespeare.txt
strace -o ncat.txt ./ncat shakespeare.txt

# compare syscall counts
wc -l cat.txt ncat.txt
```
