#include "../xps.h"

void file_source_close_handler(void *ptr);
void file_source_handler(void *ptr);

static char *resolve_public_path(void) {
  char *resolved_public = realpath("public", NULL);
  if (resolved_public != NULL)
    return resolved_public;

  return realpath("../public", NULL);
}

static char *resolve_file_path(const char *file_path) {
  char *resolved = realpath(file_path, NULL);
  if (resolved != NULL)
    return resolved;

  if (file_path[0] == '/')
    return NULL;

  char alt_path[PATH_MAX];
  int written = snprintf(alt_path, sizeof(alt_path), "../%s", file_path);
  if (written <= 0 || written >= (int)sizeof(alt_path))
    return NULL;

  return realpath(alt_path, NULL);
}

xps_file_t *xps_file_create(xps_core_t *core, const char *file_path, int *error) {
  /*assert*/
    assert(core != NULL);
    assert(file_path != NULL);
    assert(error != NULL);

  *error = E_FAIL;

  char *resolved_path = resolve_file_path(file_path);
  char *resolved_public = resolve_public_path();

  if (resolved_path == NULL || resolved_public == NULL) {
    logger(LOG_ERROR, "xps_file_create()", "realpath() failed");
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  size_t public_len = strlen(resolved_public);
  bool inside_public = strncmp(resolved_path, resolved_public, public_len) == 0 &&
                      (resolved_path[public_len] == '/' || resolved_path[public_len] == '\0');
  if (!inside_public) {
    logger(LOG_WARNING, "xps_file_create()", "file requested is outside of public directory");
    *error = E_PERMISSION;
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  struct stat file_stat;
  if (stat(resolved_path, &file_stat) != 0) {
    logger(LOG_ERROR, "xps_file_create()", "stat() failed");
    perror("Error message");
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  if (!(file_stat.st_mode & S_IROTH)) {
    logger(LOG_WARNING, "xps_file_create()", "others do not have read permission");
    *error = E_PERMISSION;
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  FILE *file_struct = fopen(resolved_path, "rb");
  if (file_struct == NULL) {
    if (errno == EACCES)
      logger(LOG_WARNING, "xps_file_create()", "permission denied opening %s", resolved_path);
    else if (errno == ENOENT)
      logger(LOG_WARNING, "xps_file_create()", "file not found: %s", resolved_path);
    else
      logger(LOG_ERROR, "xps_file_create()", "fopen() failed for file: %s", resolved_path);
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  const char *mime_type = xps_get_mime(resolved_path);

  xps_file_t *file = malloc(sizeof(xps_file_t));
  if (file == NULL) {
    logger(LOG_ERROR, "xps_file_create()", "malloc() failed for 'file'");
    fclose(file_struct);
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  xps_pipe_source_t *source = xps_pipe_source_create(file, file_source_handler, file_source_close_handler);
  if (source == NULL) {
    fclose(file_struct);
    free(file);
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  source->ready = true;
  file->core = core;
  file->file_path = resolved_path;
  file->source = source;
  file->file_struct = file_struct;
  file->size = (size_t)file_stat.st_size;
  file->mime_type = mime_type;

  *error = OK;

  free(resolved_public);

  logger(LOG_DEBUG, "xps_file_create()", "created file");

  return file;
}

void xps_file_destroy(xps_file_t *file) {
  assert(file != NULL);

  if (file->file_struct != NULL)
    fclose(file->file_struct);

  if (file->source != NULL)
    xps_pipe_source_destroy(file->source);

  free((void *)file->file_path);
  free(file);

  logger(LOG_DEBUG, "xps_file_destroy()", "destroyed file struct");
}

void file_source_handler(void *ptr) {
  /*assert*/
  assert(ptr != NULL);

  xps_pipe_source_t *source = ptr;
  xps_file_t *file = source->ptr;

  xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
  if (buff == NULL) {
    logger(LOG_ERROR, "file_source_handler()", "xps_buffer_create() failed");
    return;
  }

  // Read from file
  size_t read_n = fread(buff->data, 1, buff->size, file->file_struct);
  buff->len = read_n;

  // Checking for read errors
  if (ferror(file->file_struct)) {
    xps_buffer_destroy(buff);
    source->ready = false;
    return;
  }

  // If end of file reached
  if (read_n == 0 && feof(file->file_struct)) {
      xps_buffer_destroy(buff);
      source->ready = false;  // stop being called, close handler will clean up
      return;
  }

  if (xps_pipe_source_write(file->source, buff) != OK) {
    logger(LOG_ERROR, "file_source_handler()", "xps_pipe_source_write() failed");
    xps_buffer_destroy(buff);
    return;
  }

  xps_buffer_destroy(buff);
}

void file_source_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_pipe_source_t *source = ptr;
  xps_file_t *file = source->ptr;
  xps_file_destroy(file);
}
