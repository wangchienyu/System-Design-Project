This is a multi-threaded server with load balancing implemented.
It is divided into three parts:

Part 1: An HTTP server
This server processes basic PUT/GET/HEAD requests with files.

Part 2: Multi-threading and Logging
The server from part 1 now supports multi-threading, and has a feature to log the request history.

Part 3: Load balancing and Caching
A load balancer is made to support assigning tasks to multiple servers, and uses cache memory for recent requests.
