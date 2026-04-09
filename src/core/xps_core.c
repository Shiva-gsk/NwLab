#include "../xps.h"

xps_core_t *xps_core_create(xps_config_t *config) {

  xps_core_t *core = (xps_core_t *)malloc(sizeof(xps_core_t));
  /* handle error where core == NULL */
  if (core == NULL) {
    logger(LOG_ERROR, "xps_core_create()", "malloc() failed for 'core'");
    return NULL;
  }

  xps_loop_t *loop = xps_loop_create(core);
  /* handle error where loop == NULL */

  // Init values
  core->loop = loop;
  vec_init(&(core->listeners));
  /* initialize core->connections */
  vec_init(&(core->connections));
  core->n_null_listeners = 0;
  /* initialize core->n_null_connections */
  core->n_null_connections = 0;
  /* initialize core->pipes */
  vec_init(&(core->pipes));
  core->n_null_pipes = 0;
  /* initialize core->sessions */
  vec_init(&(core->sessions));
  core->n_null_sessions = 0;
  core->config = config;
  logger(LOG_DEBUG, "xps_core_create()", "created core");

  return core;
}

//NTLB-SEG FAULT HERE
void xps_core_destroy(xps_core_t *core) {
  assert(core != NULL);
  logger(LOG_DEBUG, "xps_core_destroy()", "In xps_core_destroy()");

  /* 1. Destroy sessions first - they depend on pipes and connections */
  for (int i = core->sessions.length - 1; i >= 0; i--) {
    xps_session_t *session = core->sessions.data[i];
    if (session != NULL) {
      logger(LOG_DEBUG, "xps_core_destroy()", "destroying session %d", i);
      xps_session_destroy(session);
    }
  }
  vec_deinit(&(core->sessions));

  /* 2. Destroy pipes */
  for (int i = core->pipes.length - 1; i >= 0; i--) {
    xps_pipe_t *pipe = core->pipes.data[i];
    if (pipe != NULL) {
      logger(LOG_DEBUG, "xps_core_destroy()", "destroying pipe %d", i);
      xps_pipe_destroy(pipe); 
    }
  }
  vec_deinit(&(core->pipes));

  /* 3. Destroy connections */
  for (int i = core->connections.length - 1; i >= 0; i--) {
    xps_connection_t *connection = core->connections.data[i]; 
    if (connection != NULL) {
      logger(LOG_DEBUG, "xps_core_destroy()", "destroying connection %d", i);
      xps_connection_destroy(connection);
    }
  }
  vec_deinit(&(core->connections));

  /* 4. Destroy listeners */
  for (int i = core->listeners.length - 1; i >= 0; i--) {
    xps_listener_t *listener = core->listeners.data[i];
    if (listener != NULL) {
      logger(LOG_DEBUG, "xps_core_destroy()", "destroying listener %d", i);
      xps_listener_destroy(listener); 
    }
  }
  vec_deinit(&(core->listeners));

  logger(LOG_DEBUG, "xps_core_destroy()", "destroying loop");
  xps_loop_destroy(core->loop); 

  free(core);
  logger(LOG_DEBUG, "xps_core_destroy()", "destroyed core");
}

void xps_core_start(xps_core_t *core) {

  /* validate params */
  assert(core != NULL);

  logger(LOG_DEBUG, "xps_start()", "starting core");

  /* create listeners from port 8001 to 8004 */
  // for (u_int port = 8001; port <= 8004; port++) {
  //   xps_listener_t *listener = xps_listener_create(core, "0.0.0.0", port);
  //   /* handle error where listener == NULL */
  //   if (listener == NULL) {
  //     logger(LOG_ERROR, "xps_core_start()", "xps_listener_create() failed for port %u", port);
  //     continue; // or return, depending on desired behavior
  //   }
  //   vec_push(&(core->listeners), listener);
  //   logger(LOG_INFO, "xps_core_start()", "Server listening on port %u", port);
  // }

  /* run loop instance using xps_loop_run() */
  xps_loop_run(core->loop);

}