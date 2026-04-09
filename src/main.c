#include "xps.h"

xps_cliargs_t *cliargs = NULL;
xps_config_t  *config  = NULL;
xps_core_t    *core    = NULL;

void sigint_handler(int signum);

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);

    // Parse command line arguments to get config file path
    cliargs = xps_cliargs_create(argc, argv);
    if (cliargs == NULL) {
        logger(LOG_ERROR, "main()", "xps_cliargs_create() failed");
        exit(EXIT_FAILURE);
    }

    // Parse the JSON config file
    config = xps_config_create(cliargs->config_path);
    if (config == NULL) {
        logger(LOG_ERROR, "main()", "xps_config_create() failed");
        xps_cliargs_destroy(cliargs);
        exit(EXIT_FAILURE);
    }

    // Create core, passing config into it
    core = xps_core_create(config);
    if (core == NULL) {
        logger(LOG_ERROR, "main()", "xps_core_create() failed");
        xps_config_destroy(config);
        xps_cliargs_destroy(cliargs);
        exit(EXIT_FAILURE);
    }

    // Create one listener per unique entry in _all_listeners
    // xps_listener_create handles binding, listening, and epoll attachment internally
    for (int i = 0; i < config->_all_listeners.length; i++) {
        xps_config_listener_t *cfg_listener = config->_all_listeners.data[i];

        xps_listener_t *listener = xps_listener_create(core, cfg_listener->host, cfg_listener->port);
        if (listener == NULL) {
            logger(LOG_ERROR, "main()", "xps_listener_create() failed for port %u", cfg_listener->port);
            xps_core_destroy(core);
            xps_config_destroy(config);
            xps_cliargs_destroy(cliargs);
            exit(EXIT_FAILURE);
        }

        logger(LOG_INFO, "main()", "listening on %s:%u", cfg_listener->host, cfg_listener->port);
    }

    // Start the event loop (blocks until shutdown)
    xps_core_start(core);

    return 0;
}

void sigint_handler(int signum) {
    logger(LOG_WARNING, "sigint_handler()", "SIGINT received");

    if (core)    xps_core_destroy(core);
    if (config)  xps_config_destroy(config);
    if (cliargs) xps_cliargs_destroy(cliargs);

    exit(EXIT_SUCCESS);
}