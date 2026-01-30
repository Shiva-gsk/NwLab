#include "../xps.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port) {
  /* validate parameter */
  assert(core != NULL);
  assert(host != NULL);
  assert(port > 0 && port <= 65535);

  /* create a socket and connect to host and port to upstream using xps_getaddrinfo and connect function */
    int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd < 0) {
      logger(LOG_ERROR, "xps_upstream_create()", "socket() failed");
      perror("Error message");
      return NULL;
    }
    const int enable = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        logger(LOG_ERROR, "xps_listener_create()", "setsockopt() failed");
        perror("Error message");
        close(sock_fd);
        return NULL;
    }

    // Setup listener address
    struct addrinfo *addr_info = xps_getaddrinfo(host, port); // Will be explained later
    if (addr_info == NULL) {
        logger(LOG_ERROR, "xps_listener_create()", "xps_getaddrinfo() failed");
        close(sock_fd);
        return NULL;
    }       
    // Connecting to upstream
    int connect_error = connect(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen);
    freeaddrinfo(addr_info); 

    if (!(connect_error == 0 || errno == EINPROGRESS)) {
        logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
        perror("Error message");
        close(sock_fd);
        return NULL;
    }
  
 /* create a connection to upstream with core and sock_fd*/
    xps_connection_t *connection = xps_connection_create(core, sock_fd);
    if (connection == NULL) {
      logger(LOG_ERROR, "xps_upstream_create()", "xps_connection_create() failed");
      close(sock_fd);
      return NULL;
    }
    connection->listener = NULL;
  

  return connection;
}