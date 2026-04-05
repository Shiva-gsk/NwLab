#include "../xps.h"

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client);
void client_source_handler(void *ptr);
void client_source_close_handler(void *ptr);
void client_sink_handler(void *ptr);
void client_sink_close_handler(void *ptr);
void upstream_source_handler(void *ptr);  
void upstream_source_close_handler(void *ptr);
void upstream_sink_handler(void *ptr);
void upstream_sink_close_handler(void *ptr);
void file_sink_handler(void *ptr);
void file_sink_close_handler(void *ptr);
void session_check_destroy(xps_session_t *session);
void xps_session_destroy(xps_session_t *session);
void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff);
void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff);
void upstream_error_res(xps_session_t *session);
void session_process_request(xps_session_t *session);

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client) {
  logger(LOG_DEBUG, "xps_session_create()", "creating session for client %p", (void *)client);
  /* validate parameters */
    if (core == NULL) {
        logger(LOG_ERROR, "xps_session_create()", "invalid 'core' parameter");
        return NULL;
    }
    if (client == NULL) {
        logger(LOG_ERROR, "xps_session_create()", "invalid 'client' parameter");
        return NULL;
    }

  // Alloc memory for session instance
  xps_session_t *session = (xps_session_t *)malloc(sizeof(xps_session_t));
  if (session == NULL) {
    logger(LOG_ERROR, "xps_session_create()", "malloc() failed for 'session'");
    return NULL;
  }

  session->client_source = xps_pipe_source_create(session, client_source_handler, client_source_close_handler);
  session->client_sink = xps_pipe_sink_create(session, client_sink_handler, client_sink_close_handler);
  session->upstream_source = xps_pipe_source_create(session, upstream_source_handler, upstream_source_close_handler);
  session->upstream_sink = xps_pipe_sink_create(session, upstream_sink_handler, upstream_sink_close_handler);
  session->file_sink = xps_pipe_sink_create(session, file_sink_handler, file_sink_close_handler);

  if (!(session->client_source && session->client_sink && session->upstream_source &&
        session->upstream_sink && session->file_sink)) {
    logger(LOG_ERROR, "xps_session_create()", "failed to create some sources/sinks");

    if (session->client_source)
      xps_pipe_source_destroy(session->client_source);
    if (session->client_sink)
      xps_pipe_sink_destroy(session->client_sink);
    if (session->upstream_source)
      xps_pipe_source_destroy(session->upstream_source);
    if (session->upstream_sink)
      xps_pipe_sink_destroy(session->upstream_sink);
    if (session->file_sink)
      xps_pipe_sink_destroy(session->file_sink);  

    free(session);
    return NULL;
  }

  // Init values
  session->core = core;
  session->client = client;
  //NTLB
  session->upstream = NULL;
  session->upstream_connected = false;
  session->upstream_error_res_set = false;
  session->upstream_write_bytes = 0;
  session->file = NULL;
  session->to_client_buff = NULL;
  session->from_client_buff = NULL;
  session->client_sink->ready = true;
  session->upstream_sink->ready = true;
  session->file_sink->ready = true;
  session->http_req = NULL;

  logger(LOG_DEBUG, "xps_session_create()", "initialized session %p", (void *)session);

  /*NOTE: We will be adding list of sessions as vec_void_t sessions to xps_core_s in xps_core module below*/
  // Add current session to core->sessions
  vec_push(&(core->sessions), session);

  // Attach client
  if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, client->source, session->client_sink) ==
        NULL ||
      xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->client_source, client->sink) ==
        NULL) {
    logger(LOG_ERROR, "xps_session_create()", "failed to create client pipes");

    if (session->client_source)
      xps_pipe_source_destroy(session->client_source);
    if (session->client_sink)
      xps_pipe_sink_destroy(session->client_sink);
    if (session->upstream_source)
      xps_pipe_source_destroy(session->upstream_source);
    if (session->upstream_sink)
      xps_pipe_sink_destroy(session->upstream_sink);
    if (session->file_sink)
      xps_pipe_sink_destroy(session->file_sink);


    free(session);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_session_create()", "created session");
  // session_process_request(session);
//Removed in S14
  // if (client->listener->port == 8001) {
  //   xps_connection_t *upstream = xps_upstream_create(core, "127.0.0.1", 3000);
  //   if (upstream == NULL) {
  //     logger(LOG_ERROR, "xps_session_create()", "xps_upstream_create() failed");
  //     perror("Error message");
  //     /* destroy session */
  //     xps_session_destroy(session);
  //     return NULL;
  //   }
  //   session->upstream = upstream;
  //   if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->upstream_source, upstream->sink) == NULL ||
  //       xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, upstream->source, session->upstream_sink) == NULL) {
  //     logger(LOG_ERROR, "xps_session_create()", "failed to create upstream pipes");
  //     xps_session_destroy(session);
  //     return NULL;
  //   }
  // }

  // else if (client->listener->port == 8002) {
  //   int error;
  //   xps_file_t *file = xps_file_create(core, "../public/sample.txt", &error);
  //   if (file == NULL) {
  //     logger(LOG_ERROR, "xps_session_create()", "xps_file_create() failed");
  //     perror("Error message");
  //     /*destory session*/
  //     xps_session_destroy(session);
  //     return NULL;
  //   }
  //   /* assign to the file member */
  //   session->file = file;
  //   if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, file->source, session->file_sink) == NULL) {
  //     logger(LOG_ERROR, "xps_session_create()", "failed to create file pipe");
  //     xps_session_destroy(session);
  //     return NULL;
  //   }
  // }

  return session;
}


void client_source_handler(void *ptr) {
  /* validate parameters */
  assert(ptr != NULL);

  logger(LOG_DEBUG, "client_source_handler()", "handling client source event");

  xps_pipe_source_t *source = ptr;
  xps_session_t *session = source->ptr;

  // write to session->to_client_buff
  if (xps_pipe_source_write(source, session->to_client_buff) != OK) {
    logger(LOG_ERROR, "client_source_handler()", "xps_pipe_source_write() failed");
    return;
  }
  logger(LOG_DEBUG, "client_source_handler()", "wrote buffer %p to client pipe", (void *)session->to_client_buff);
  // xps_buffer_destroy(session->to_client_buff);

  set_to_client_buff(session, NULL);
  session_check_destroy(session);
}

void client_source_close_handler(void *ptr) {
  assert(ptr != NULL);

  logger(LOG_DEBUG, "client_source_close_handler()", "closing client source");

  xps_pipe_source_t *source = ptr;
  xps_session_t *session = source->ptr;

  session_check_destroy(session);
}
void client_sink_handler(void *ptr) {
  assert(ptr != NULL);

  logger(LOG_DEBUG, "client_sink_handler()", "handling client sink event");

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  size_t available = sink->pipe->buff_list->len;
  if (available == 0) {
    logger(LOG_DEBUG, "client_sink_handler()", "no data available in pipe");
    return;
  }

  xps_buffer_t *buff = xps_pipe_sink_read(sink, available);
  if (buff == NULL) {
    logger(LOG_ERROR, "client_sink_handler()", "xps_pipe_sink_read() failed");
    return;
  }

  if (session->http_req == NULL) {
    int error;
    xps_http_req_t *http_req = xps_http_req_create(session->core, buff, &error);
    if(error != OK) {
      error = HTTP_BAD_REQUEST;
      // xps_buffer_destroy(buff);
      logger(LOG_ERROR, "client_sink_handler()", "xps_http_req_create() failed with error code %d", error);
      session_process_request(session);
      return;
    }
    xps_buffer_destroy(buff);

    if (error == E_FAIL) {
      logger(LOG_ERROR, "client_sink_handler()", "xps_http_req_create() failed");
      session_process_request(session);
      return;
    }

    if (error == E_AGAIN) {
      // incomplete data, wait for more
      return;
    }

    // error == OK
    session->http_req = http_req;
    logger(LOG_DEBUG, "client_sink_handler()", "parsed http request %p", (void *)http_req);
    xps_buffer_t *http_req_buff = xps_http_req_serialize(http_req);
    set_from_client_buff(session, http_req_buff);
    xps_pipe_sink_clear(sink, available);
    // xps_buffer_destroy(buff);
    session_process_request(session);

  } else {
    set_from_client_buff(session, buff);
    xps_pipe_sink_clear(sink, available);
  }
}


void client_sink_close_handler(void *ptr) {

  assert(ptr != NULL);

  logger(LOG_DEBUG, "client_sink_close_handler()", "closing client sink");

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  session_check_destroy(session);

}

void upstream_source_handler(void *ptr) {
  /*assert*/
  assert(ptr != NULL);
  logger(LOG_DEBUG, "upstream_source_handler()", "handling upstream source event");
  
  /*set ptr as source*/
  xps_pipe_source_t *source = ptr;
  /*set session as source->ptr*/
  xps_session_t *session = source->ptr;

  if (xps_pipe_source_write(source, session->from_client_buff) != OK) {
    logger(LOG_ERROR, "upstream_source_handler()", "xps_pipe_source_write() failed");
    return;
  }

  logger(LOG_DEBUG, "upstream_source_handler()", "wrote buffer %p upstream", (void *)session->from_client_buff);

  // Checking if upstream is connected
  if (session->upstream_connected == false) {
    session->upstream_write_bytes += session->from_client_buff->len;
    if (session->upstream_write_bytes > session->upstream_source->pipe->buff_list->len)
      session->upstream_connected = true;
  }

  xps_buffer_destroy(session->from_client_buff);

  set_from_client_buff(session, NULL);
  session_check_destroy(session);
}

void upstream_source_close_handler(void *ptr) {
  /* fill this */
    assert(ptr != NULL);
  logger(LOG_DEBUG, "upstream_source_close_handler()", "closing upstream source");

  xps_pipe_source_t *source = ptr;
  xps_session_t *session = source->ptr;

  if (!session->upstream_connected && !session->upstream_error_res_set) {
    upstream_error_res(session);
  }

  /* fill this */
  session_check_destroy(session);
}

void upstream_sink_handler(void *ptr) {
  /* fill this */
  assert(ptr != NULL);
  logger(LOG_DEBUG, "upstream_sink_handler()", "handling upstream sink event");

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  session->upstream_connected = true;

  size_t available = sink->pipe->buff_list->len;
    if (available == 0) {
        logger(LOG_DEBUG, "connection_sink_handler()", "no data available in pipe");
        return;
    }

  xps_buffer_t *buff = xps_pipe_sink_read(sink, available);
  if (buff == NULL) {
    logger(LOG_ERROR, "upstream_sink_handler()", "xps_pipe_sink_read() failed");
    return;
  }

  logger(LOG_DEBUG, "upstream_sink_handler()", "read buffer %p from upstream pipe", (void *)buff);

  set_to_client_buff(session, buff);
  xps_pipe_sink_clear(sink, available);
}

void upstream_sink_close_handler(void *ptr) {
  /* fill this */
  assert(ptr != NULL);
  logger(LOG_DEBUG, "upstream_sink_close_handler()", "closing upstream sink");

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  if (!session->upstream_connected && !session->upstream_error_res_set) {
    upstream_error_res(session);
  }

  /* fill this */
  session_check_destroy(session);
}

void upstream_error_res(xps_session_t *session) {
  assert(session != NULL);

  logger(LOG_DEBUG, "upstream_error_res()", "marking upstream error response for session %p", (void *)session);

  session->upstream_error_res_set = true;
}

void file_sink_handler(void *ptr) {
  /* fill this */
  assert(ptr != NULL);
  logger(LOG_DEBUG, "file_sink_handler()", "handling file sink event");

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  size_t available = sink->pipe->buff_list->len;
    if (available == 0) {
        logger(LOG_DEBUG, "file_sink_handler()", "no data available in pipe");
        return;
    }

  xps_buffer_t *buff = xps_pipe_sink_read(sink, available);
  if (buff == NULL) {
    logger(LOG_ERROR, "file_sink_handler()", "xps_pipe_sink_read() failed");
    return;
  }

  logger(LOG_DEBUG, "file_sink_handler()", "read buffer %p from file pipe", (void *)buff);

  set_to_client_buff(session, buff);
  xps_pipe_sink_clear(sink, available);
}

void file_sink_close_handler(void *ptr) {

  /* fill this */
  assert(ptr != NULL);

  logger(LOG_DEBUG, "file_sink_close_handler()", "closing file sink");

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  session_check_destroy(session);
}
void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff) {
  /* validate parameters */
  assert(session != NULL);

  logger(LOG_DEBUG, "set_to_client_buff()", "session=%p buff=%p", (void *)session, (void *)buff);

  session->to_client_buff = buff;

  if (buff == NULL) {
    session->client_source->ready = false;
    session->upstream_sink->ready = true;
    session->file_sink->ready = true;
  } else {
    session->client_source->ready = true;
    session->upstream_sink->ready = false;
    session->file_sink->ready = false;
  }
}

void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff) {
  /* validate parameters */
  assert(session != NULL);

  logger(LOG_DEBUG, "set_from_client_buff()", "session=%p buff=%p", (void *)session, (void *)buff);

  session->from_client_buff = buff;

  if (buff == NULL) {
    session->client_sink->ready = true;
    session->upstream_source->ready = false;
  } else {
    session->client_sink->ready = false;
    session->upstream_source->ready = true;
  }
}

void session_check_destroy(xps_session_t *session) {
  /* validate parameters */
    assert(session != NULL);

  logger(LOG_DEBUG, "session_check_destroy()", "checking session %p for destruction", (void *)session);

  bool c2u_flow =
    session->upstream_source->active && (session->client_sink->active || session->from_client_buff);

  bool u2c_flow = session->client_source->active && (session->upstream_sink->active || session->to_client_buff);

  bool f2c_flow = session->client_source->active && (session->file_sink->active || session->to_client_buff);

  bool flowing = c2u_flow || u2c_flow || f2c_flow;

  logger(LOG_DEBUG, "session_check_destroy()", "flow state c2u=%d u2c=%d f2c=%d", c2u_flow, u2c_flow, f2c_flow);

  if (!flowing)
    xps_session_destroy(session);
}
void session_process_request(xps_session_t *session) {
  assert(session != NULL);

  char *reply = malloc(DEFAULT_BUFFER_SIZE);
  if (reply == NULL) {
    logger(LOG_ERROR, "session_process_request()", "malloc() failed for reply");
    return;
  }

  if (session->http_req == NULL || session->http_req->path == NULL) {
    sprintf(reply, "HTTP/1.1 400 Bad Request\r\nServer: eXpServer\r\n\r\n");
    xps_buffer_t *buff = xps_buffer_create(strlen(reply)+1, strlen(reply)+1, strdup(reply));
    set_to_client_buff(session, buff);
    free(reply);
    return;
  }

  char file_path[DEFAULT_BUFFER_SIZE];
  strcpy(file_path, "../public");
  strcat(file_path, session->http_req->path);

  int error;
  session->file = xps_file_create(session->core, file_path, &error);

  if (error == E_PERMISSION) {
    sprintf(reply, "HTTP/1.1 403 Forbidden\r\nServer: eXpServer\r\n\r\n");
    xps_buffer_t *buff = xps_buffer_create(strlen(reply)+1, strlen(reply)+1, strdup(reply));
    set_to_client_buff(session, buff);

  } else if (error == E_NOTFOUND) {
    sprintf(reply, "HTTP/1.1 404 Not Found\r\nServer: eXpServer\r\n\r\n");
    xps_buffer_t *buff = xps_buffer_create(strlen(reply)+1, strlen(reply)+1, strdup(reply));
    set_to_client_buff(session, buff);

  } else if (error != OK) {
    sprintf(reply, "HTTP/1.1 500 Internal Server Error\r\nServer: eXpServer\r\n\r\n");
    xps_buffer_t *buff = xps_buffer_create(strlen(reply)+1, strlen(reply)+1, strdup(reply));
    set_to_client_buff(session, buff);

  } else {
    if (session->file->mime_type) {
      sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nServer: eXpServer\r\n\r\n",
              session->file->mime_type);
    } else {
      sprintf(reply, "HTTP/1.1 200 OK\r\nServer: eXpServer\r\n\r\n");
    }
    xps_buffer_t *buff = xps_buffer_create(strlen(reply)+1, strlen(reply)+1, strdup(reply));
    set_to_client_buff(session, buff);

    if (xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH, session->file->source, session->file_sink) == NULL) {
      logger(LOG_ERROR, "session_process_request()", "failed to create file pipe");
      free(reply);
      xps_session_destroy(session);
      return;
    }
  }

  free(reply);
}

void xps_session_destroy(xps_session_t *session) {
  /* validate parameters */
  assert(session != NULL);

  logger(LOG_DEBUG, "xps_session_destroy()", "destroying session %p", (void *)session);

  /* destroy client_source, client_sink, upstream_source, upstream_sink and file_sink attached to session */
  if (session->client_source != NULL)
    xps_pipe_source_destroy(session->client_source);
  if (session->client_sink != NULL)
    xps_pipe_sink_destroy(session->client_sink);
  if (session->upstream_source != NULL)
    xps_pipe_source_destroy(session->upstream_source);
  if (session->upstream_sink != NULL)
    xps_pipe_sink_destroy(session->upstream_sink);
  if (session->file_sink != NULL)
    xps_pipe_sink_destroy(session->file_sink);  

  if (session->to_client_buff != NULL)
    xps_buffer_destroy(session->to_client_buff);
  if (session->from_client_buff != NULL)
    xps_buffer_destroy(session->from_client_buff);
  if (session->http_req != NULL) {
    xps_http_req_destroy(session->core, session->http_req);
  }

  // Set NULL in core's list of sessions
  /* fill this */
  for (int i = 0; i < session->core->sessions.length; i++) {
    xps_session_t *curr = session->core->sessions.data[i];
    if (curr == session) {
      session->core->sessions.data[i] = NULL;
      session->core->n_null_sessions++;
      break;
    }
  }
  
  free(session);

  logger(LOG_DEBUG, "xps_session_destroy()", "destroyed session");
}