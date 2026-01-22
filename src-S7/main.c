#include "xps.h"

xps_core_t *core;

void sigint_handler(int signum);

int main() {
  signal(SIGINT, sigint_handler);

  core = xps_core_create();
  if (core == NULL) {
    logger(LOG_ERROR, "main()", "xps_core_create() failed");
    exit(EXIT_FAILURE);
  }

  /* 'start' core instance */
  xps_core_start(core);

}

void sigint_handler(int signum) {
  logger(LOG_WARNING, "sigint_handler()", "SIGINT received");

  xps_core_destroy(core);

  exit(EXIT_SUCCESS);
}