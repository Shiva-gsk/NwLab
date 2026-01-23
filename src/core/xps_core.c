#include "../xps.h"

xps_core_t *xps_core_create() {

  xps_core_t *core = (xps_core_t *)malloc(sizeof(xps_core_t));
  /* handle error where core == NULL */
  if (core == NULL) {
    logger(LOG_ERROR, "xps_core_create()", "malloc() failed for 'core'");
    return NULL;
  }

  xps_loop_t *loop = xps_loop_create(core);
  /* handle error where loop == NULL */
  if (loop == NULL) {
    logger(LOG_ERROR, "xps_core_create()", "xps_loop_create() failed");
    free(core);
    return NULL;
  }

  // Init values
  core->loop = loop;
  vec_init(&(core->listeners));
  /* initialize core->connections */
  vec_init(&(core->connections));
  core->n_null_listeners = 0;
  /* initialize core->n_null_connections */
  core->n_null_connections = 0;

  logger(LOG_DEBUG, "xps_core_create()", "created core");

  return core;
}

//NTLB-SEG FAULT HERE
void xps_core_destroy(xps_core_t *core) {
  assert(core != NULL);
  // printf("In xps_core_destroy()\n");
  // Destroy connections
  int len = core->connections.length;
  for (int i = 0; i < len; i++) {         //Problem HERE
    // printf("In xps_core_destroy()\n");
    xps_connection_t *connection = core->connections.data[i]; 
    if (connection != NULL) {
      logger(LOG_DEBUG, "xps_core_destroy()", "destroying connection %d", i);
      xps_connection_destroy(connection); // modification of xps_connection_destroy() will be look at later
    }
  }
  vec_deinit(&(core->connections));
  
  /* destory all the listeners and de-initialize core->listeners */
  len = core->listeners.length;
  printf("In xps_core_destroy(), number of listeners: %d\n", len);
  for (int i = 0; i < len; i++) {
    
    xps_listener_t *listener = core->listeners.data[i];
    if (listener != NULL) {
      printf("In xps_core_destroy() for listener %d\n", i);
      logger(LOG_DEBUG, "xps_core_destroy()", "destroying listener %d", i);
      xps_listener_destroy(listener); 
      printf("In xps_core_destroy()\n");
    }
  }
  vec_deinit(&(core->listeners));

  /* destory loop attached to core */
  logger(LOG_DEBUG, "xps_core_destroy()", "destroying loop");
  xps_loop_destroy(core->loop); 

  /* free core instance */
  printf("Freeing core instance\n");
  free(core);

  logger(LOG_DEBUG, "xps_core_destroy()", "destroyed core");
}

void xps_core_start(xps_core_t *core) {

  /* validate params */
  assert(core != NULL);

  logger(LOG_DEBUG, "xps_start()", "starting core");

  /* create listeners from port 8001 to 8004 */
  for (u_int port = 8001; port <= 8004; port++) {

    xps_listener_t *listener = xps_listener_create(core, "0.0.0.0", port);
    /* handle error where listener == NULL */
    if (listener == NULL) {
      logger(LOG_ERROR, "xps_core_start()", "xps_listener_create() failed for port %u", port);
      continue; // or return, depending on desired behavior
    }
    vec_push(&(core->listeners), listener);
    logger(LOG_INFO, "xps_core_start()", "Server listening on port %u", port);
  }

  for(int i=0; i<core->listeners.length; i++) {
    xps_listener_t *listener = core->listeners.data[i];
    if (listener != NULL) {
      logger(LOG_DEBUG, "xps_core_start()", "Active listener on port %u", listener->port);
    }
  }

  /* run loop instance using xps_loop_run() */
  xps_loop_run(core->loop);

}