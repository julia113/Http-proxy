juni

CSE130 Asgn3 - httpproxy.c

README

## Program Description
Httpproxy acts as a http server and client that forwards received HEAD, GET, and PUT requests to the server and then forwards responses from the origin server to the original client. It is a http reverse proxy with a cache (implemented only for GET requests) that can also handle persistent connections.


## How to run
Use unix or ubuntu (linux) environment and must use gcc.

Run make. In one terminal(server), run ./httpserver with an available port.

In another terminal, run ./httpproxy with 2 available port numbers where the second one matches the the port number ran with ./httpserver. 

You can also include 3 optional arguments: -c for capacity of cache, -m for max size of files in the cache, and -u for LRU cache replacement. 

In another terminal(client), use curl to send HEAD, GET, and PUT requests to httpproxy.
