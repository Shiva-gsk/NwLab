#include  "../xps.h"

//Removed in S10
// void connection_loop_read_handler(void *ptr);
// void connection_loop_write_handler(void *ptr);
void connection_source_handler(void *ptr);
void connection_source_close_handler(void *ptr);
void connection_sink_handler(void *ptr);
void connection_sink_close_handler(void *ptr);
void connection_close(xps_connection_t *connection, bool peer_closed);
void connection_read_handler(void *ptr);
void connection_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_loop_read_handler(void* ptr);
void connection_loop_write_handler(void* ptr);
void strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {

  xps_connection_t *connection = (xps_connection_t *)malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return NULL;
  }
  /* create pipe source and sink for connection */
  xps_pipe_source_t *source = xps_pipe_source_create(connection, connection_source_handler, connection_source_close_handler);
  if (source == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_source_create() failed");
    free(connection);
    return NULL;
    }
  
  xps_pipe_sink_t *sink = xps_pipe_sink_create(connection, connection_sink_handler, connection_sink_close_handler);
  if (sink == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_sink_create() failed");
    xps_pipe_source_destroy(source);
    free(connection);
    return NULL;
    }
  
  /* attach sock_fd to epoll */
  //Changed in S8
   //xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection, connection_loop_read_handler);
   // Inside xps_connection_create() in xps_connection.c
 
  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);
  connection->source = source;
  connection->sink = sink;

  //Removed in S10
  // connection->write_buff_list = xps_buffer_list_create(); //Added in S8
  // connection->read_ready = false;
  // connection->write_ready = false;
  // connection->send_handler = connection_write_handler;
  // connection->recv_handler = connection_read_handler;

  // // Attach connection to loop

  //NTLB: Review this part later for correctness
    if ((xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection, connection_loop_read_handler, connection_loop_write_handler, connection_loop_close_handler)) != OK) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
    /*destroy source*/
    xps_pipe_source_destroy(source);
    /*destroy sink*/
    xps_pipe_sink_destroy(sink);
    free(connection);
    return NULL;
    }

  /* add connection to 'connections' list */
    vec_push(&(core->connections), connection);

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");
  return connection;

}

void xps_connection_destroy(xps_connection_t *connection) {

  /* validate params */
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return;
  }
    
  /* set connection to NULL in 'connections' list */

  for (int i = 0; i < connection->core->connections.length; i++) {
      xps_connection_t *curr = connection->core->connections.data[i];
      if (curr == connection) {
      connection->core->connections.data[i] = NULL;
      break;
      }
  }

  /* detach connection from loop */
    xps_loop_detach(connection->core->loop, connection->sock_fd);

  /* destroy source */
    xps_pipe_source_destroy(connection->source);
  /* destroy sink */
    xps_pipe_sink_destroy(connection->sink);

  /* close connection socket FD */
    close(connection->sock_fd);

  /* free connection->remote_ip */
    free(connection->remote_ip);
  /* free connection instance */
    free(connection);

  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");

}

void connection_read_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;
  /* validate params */
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_read_handler()", "invalid 'connection' parameter");
    return;
  }

  char buff[DEFAULT_BUFFER_SIZE];
  long read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE-1, 0);

  if (read_n < 0) {

    if(errno == EAGAIN || errno == EWOULDBLOCK){
      logger(LOG_INFO, "xps_connection_read_handler()", "recv() would block, try again later");
      connection->source->ready = false;
      return;
    }
    else{ //if error is something else
      logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
      xps_connection_destroy(connection);
      return;
    }
    return;
  }

  if (read_n == 0) {
    logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
    xps_connection_destroy(connection);
    return;
  }

  buff[read_n] = '\0';

  /* print client message */
  logger(LOG_INFO, "xps_connection_read_handler()", "Received message from %s", connection->remote_ip);
  printf("[CLIENT MESSAGE]: %s", buff);
  /* reverse client message */
  strrev(buff);
  /* create a buffer containing the reversed message and append it to the
     connection write buffer list so the write handler will send it */
  xps_buffer_t *response_buffer = xps_buffer_create(read_n, read_n, NULL);
  if (response_buffer == NULL) {
    logger(LOG_ERROR, "xps_connection_read_handler()", "xps_buffer_create() failed");
    xps_connection_destroy(connection);
    return;
  }

  /* copy reversed data into the newly created buffer */
  memcpy(response_buffer->data, buff, read_n);

  /* append to connection's write list; the write handler will pick it up */
  xps_buffer_list_append(connection->sink->pipe->buff_list, response_buffer);
  connection->sink->active = true;
  /* done — do not send here (write handler will perform send()) */
}

void connection_write_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  /* validate params */
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_write_handler()", "invalid 'connection' parameter");
    return;
  }

  xps_buffer_list_t *wb_list = connection->sink->pipe->buff_list;
  if (wb_list == NULL)
    return;

  /* nothing to send */
  if (wb_list->len == 0)
    return;

  /* read the entire length available in the buffer list */
  size_t to_read = wb_list->len;
  xps_buffer_t *send_buff = xps_buffer_list_read(wb_list, to_read);
  if (send_buff == NULL) {
    logger(LOG_ERROR, "xps_connection_write_handler()", "xps_buffer_list_read() failed");
    return;
  }

  /* attempt to send the buffer */
  ssize_t write_n = send(connection->sock_fd, send_buff->data, send_buff->len, 0);
  if (write_n < 0) {
    if(errno == EAGAIN || errno == EWOULDBLOCK){
        logger(LOG_INFO, "xps_connection_write_handler()", "send() would block, try again later");
        connection->sink->ready = false;
        return;
      }
      else{ //if error is something else
        logger(LOG_ERROR, "xps_connection_write_handler()", "send() failed");
        xps_connection_destroy(connection);
        return;
      }
  }

  /* on success, clear the sent bytes from write buffer list and free temp */
  if (write_n > 0) {
    xps_buffer_list_clear(wb_list, (size_t)write_n);
  }

  xps_buffer_destroy(send_buff);
}

void connection_loop_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;
  logger(LOG_INFO, "connection_loop_close_handler()", "Closing connection with %s", connection->remote_ip);
  // xps_connection_destroy(connection);
  connection_close(connection, true);
}

void connection_loop_read_handler(void* ptr) {
  logger(LOG_DEBUG, "connection_loop_read_handler()", "Setting read_ready to true ");
    assert(ptr != NULL);
	  /*set read_ready flag to true*/
  xps_connection_t *connection = ptr;
  connection->source->ready = true;
}

void connection_loop_write_handler(void* ptr) {
  logger(LOG_DEBUG, "connection_loop_write_handler()", "Setting write_ready to true ");
    assert(ptr != NULL);
   /*set write_ready flag to true*/
  xps_connection_t *connection = ptr;
  connection->sink->ready = true;
}   

void connection_source_handler(void *ptr) {
    /*assert ptr not null*/
    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;

    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
    if (buff == NULL) {
    logger(LOG_DEBUG, "connection_source_handler()", "xps_buffer_create() failed");
    return;
    }

    /*Read from socket using recv()*/
    int read_n = recv(connection->sock_fd, buff->data, DEFAULT_BUFFER_SIZE, 0);
    buff->len = read_n;
    logger(LOG_DEBUG, "connection_source_handler()", "recv() read %d bytes", read_n);
    // Socket would block
    if (read_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      xps_buffer_destroy(buff);
      connection->source->ready = false;
      return;
    }

    // Socket error
    if (read_n < 0) {
      xps_buffer_destroy(buff);
      logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
      connection_close(connection, false);
      return;
    }

    // Peer closed connection
    if (read_n == 0) {
      xps_buffer_destroy(buff);
      connection_close(connection, true);
      return;
    }

    if (xps_pipe_source_write(connection->source, buff) != OK) {
      logger(LOG_ERROR, "connection_source_handler()", "xps_pipe_source_write() failed");
      /*destroy buff*/
      logger(LOG_DEBUG, "connection_source_handler()", "destroying buffer %p after failed write", (void*)buff);
      xps_buffer_destroy(buff);
      /*close connection*/
      connection_close(connection, false);
      return;
    }

    logger(LOG_DEBUG, "connection_source_handler()", "destroying original buffer %p after successful write", (void*)buff);
    xps_buffer_destroy(buff);
}

void connection_source_close_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;

    if (source->active == false && connection->sink->active == false)
      connection_close(connection, false);
    /*close connection*/
}

void connection_sink_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;

    // First, read data from the pipe
    size_t available = sink->pipe->buff_list->len;
    if (available == 0) {
        logger(LOG_DEBUG, "connection_sink_handler()", "no data available in pipe");
        return;
    }

    // Read from pipe into buffer
    logger(LOG_DEBUG, "connection_sink_handler()", "reading %zu bytes from pipe buffer", available);
    xps_buffer_t *buff = xps_pipe_sink_read(sink, available);
    if (buff == NULL) {
        logger(LOG_ERROR, "connection_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }
    logger(LOG_DEBUG, "connection_sink_handler()", "successfully read buffer %p from pipe, clearing pipe buffers", (void*)buff);

    // Clear the buffers from the pipe after reading
    if (xps_pipe_sink_clear(sink, available) != OK) {
        logger(LOG_ERROR, "connection_sink_handler()", "xps_pipe_sink_clear() failed");
        xps_buffer_destroy(buff);
        return;
    }
    logger(LOG_DEBUG, "connection_sink_handler()", "cleared %zu bytes from pipe buffer list", available);

    // Write to socket
    int write_n = send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);
    logger(LOG_DEBUG, "connection_sink_handler()", "sent %d bytes to socket", write_n);

    /*destroy buff*/
    logger(LOG_DEBUG, "connection_sink_handler()", "destroying read buffer %p", (void*)buff);
    xps_buffer_destroy(buff);

    // Socket would block
    if (write_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    /*sink made not ready*/
      connection->sink->ready = false;
    return;
    }

    // Socket error
    if (write_n < 0) {
      logger(LOG_ERROR, "connection_sink_handler()", "send() failed");
      /*close connection*/
      connection_close(connection, false);
      return;
    }

    if (write_n == 0)
      return;

}

void connection_sink_close_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;

    if (connection->source->active == false && connection->sink->active == false)
      connection_close(connection, false);
}

void connection_close(xps_connection_t *connection, bool peer_closed) {
    /*assert*/
    assert(connection != NULL);
    logger(LOG_INFO, "connection_close()",
            peer_closed ? "peer closed connection" : "closing connection");
    /*destroy connection*/
    xps_connection_destroy(connection);
}

//Removed in S7

// void xps_connection_read_handler(xps_connection_t *connection) {

//   /* validate params */
//   if (connection == NULL) {
//     logger(LOG_ERROR, "xps_connection_read_handler()", "invalid 'connection' parameter");
//     return;
//   }

//   char buff[DEFAULT_BUFFER_SIZE];
//   long read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE-1, 0);

//   if (read_n < 0) {
//     logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
//     perror("Error message");
//     xps_connection_destroy(connection);
//     return;
//   }

//   if (read_n == 0) {
//     logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
//     xps_connection_destroy(connection);
//     return;
//   }

//   buff[read_n] = '\0';

//   /* print client message */
//   logger(LOG_INFO, "xps_connection_read_handler()", "Received message from %s: %s", connection->remote_ip, buff);

//   /* reverse client message */
//   strrev(buff);

//   // Sending reversed message to client
//   long bytes_written = 0;
//   long message_len = read_n;
//   while (bytes_written < message_len) {
//     long write_n = send(connection->sock_fd, buff + bytes_written, message_len - bytes_written, 0);
//     if (write_n < 0) {
//       logger(LOG_ERROR, "xps_connection_read_handler()", "send() failed");
//       perror("Error message");
//       xps_connection_destroy(connection);
//       return;
//     }
//     bytes_written += write_n;
//   }

// }