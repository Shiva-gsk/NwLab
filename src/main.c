#include "xps.h"

xps_cliargs_t *cliargs = NULL;
xps_core_t *global_core = NULL;

void sigint_handler(int signum);
int core_create(xps_config_t *config);
void core_destroy(xps_core_t *core);

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);
    
    cliargs = xps_cliargs_create(argc, argv); // get commandline arguments
    if (cliargs == NULL) {
        exit(EXIT_FAILURE);
    }

    /* create config, create core, start core */
    xps_config_t *config = xps_config_create(cliargs->config_path);
    if (config == NULL) {
        logger(LOG_ERROR, "main()", "Failed to create configuration.");
        xps_cliargs_destroy(cliargs);
        exit(EXIT_FAILURE);
    }

    int cores = core_create(config);

    if (global_core != NULL) {
        logger(LOG_INFO, "main()", "Starting eXpServer...");
        xps_core_start(global_core);
    }

    return EXIT_SUCCESS;
}

int core_create(xps_config_t *config) {
    /* assert */
    assert(config != NULL);

    xps_core_t *core = xps_core_create(config);
    if (core == NULL) return 0;
    
    global_core = core;

    vec_void_t master_listeners;
    vec_init(&master_listeners);

    for (int i = 0; i < config->_all_listeners.length; i++) {
        xps_config_listener_t *conf_listener = config->_all_listeners.data[i];
        
        xps_listener_t *master = xps_listener_create(conf_listener->host, conf_listener->port);
        if (master != NULL) {
            vec_push(&master_listeners, master);
        }
    }

    for (int i = 0; i < master_listeners.length; i++) {
        xps_listener_t *master = master_listeners.data[i];

        xps_listener_t *dup_listener = malloc(sizeof(xps_listener_t));
        dup_listener->core = core;
        dup_listener->host = str_create(master->host);
        dup_listener->port = master->port;
        
        dup_listener->sock_fd = dup(master->sock_fd); 

        xps_loop_attach(core->loop, dup_listener->sock_fd, EPOLLIN, dup_listener, listener_connection_handler, NULL, NULL);

        vec_push(&core->listeners, dup_listener);
    }

    /* Destroy listeners */
    for (int i = 0; i < master_listeners.length; i++) {
        xps_listener_t *master = master_listeners.data[i];
        
        close(master->sock_fd); 
        free(master);
    }
    vec_deinit(&master_listeners);

    return 1;
}

void core_destroy(xps_core_t *core) {
    /* fill this */
    if (core != NULL) {
        xps_core_destroy(core);
    }
}

void sigint_handler(int signum) {
    logger(LOG_WARNING, "sigint_handler()", "SIGINT received. Shutting down...");

    if (global_core != NULL) {
        core_destroy(global_core);
    }
    
    if (cliargs != NULL) {
        xps_cliargs_destroy(cliargs);
    }

    exit(EXIT_SUCCESS);
}