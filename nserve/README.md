# neoserve

A tiny C executable replacement for Python's http.server.
Primarily for displaying static frontends. 

### What neoserve does
- static file serving for HTML, CSS, JS, images, fonts, other common assets
- GET and HEAD support with appropriate content types and file lengths
- configurable port, defaulting to 8080
- relative or absolute document roots using --path flag
- validated URL paths, including percent decoding, traversal rejection, blocking symlinks
- Bounded memory usage through fixed size buffers and streamed file transfers
- sequential request hadling in a single thread
- local only access through 127.0.0.1
- five second recv and send timeouts for blocking waits
- shutdown with "Ctrl + c"
- help flag w examples -h --help
- C / POSIX minimal implementation

 ### Next Features
 - File System serving and sharing over network


 ### What neoserve DOES NOT do that http.server does
 - concurrent clients w threaded request handling
 - directory listings when no index file exists
 - cache validation using last-modified and 304 Not Modified
 - request logging to the terminal
 - configurable network binding, including LAN access and IPv6
 - optional persistent connections with HTTP/1.1
 - HTTPS support with supplied certs in Python 3.14


