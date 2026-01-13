xps_listener_t *xps_listener_create(int epoll_fd, const char *host, u_int port) {
  assert(host != NULL);
  assert(is_valid_port(port)); // Will be explained later

  // Create socket instance
  int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock_fd < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "socket() failed");
    perror("Error message");
    return NULL;
  }

  // Make address reusable
  const int enable = 1;
  if (/* make socket address reusable using setsockopt() */) < 0) {
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

  // Binding to port
  if (bind(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "failed to bind() to %s:%u", host, port);
    perror("Error message");
    freeaddrinfo(addr_info); // Will be explained later
    close(sock_fd);
    return NULL;
  }
  freeaddrinfo(addr_info); // Will be explained later

  // Listening on port
  if (listen(sock_fd, DEFAULT_BACKLOG) < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "listen() failed");
    perror("Error message");
    close(sock_fd);
    return NULL;
  }

  // Create & allocate memory for a listener instance
  xps_listener_t *listener = malloc(sizeof(xps_listener_t));
  if (listener == NULL) {
    logger(LOG_ERROR, "xps_listener_create()", "malloc() failed for 'listener'");
    close(sock_fd);
    return NULL;
  }

  // Init values
  listener->epoll_fd = /* fill this */
  listener->host = /* fill this */
  listener->port = /* fill this */
  listener->sock_fd = /* fill this */

  // Attach listener to loop
  xps_loop_attach(epoll_fd, sock_fd, EPOLLIN);

  // Add listener to global listeners list
  vec_push(&listeners, listener);

  logger(LOG_DEBUG, "xps_listener_create()", "created listener on port %d", port);

  return listener;
}

