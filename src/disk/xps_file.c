#include "../xps.h"
#include <limits.h>
#include <linux/limits.h>

void file_source_close_handler(void *ptr);
void file_source_handler(void *ptr);

static char *resolve_public_path(void) {
  logger(LOG_DEBUG, "resolve_public_path()", "resolving public directory");
  char *resolved_public = realpath("public", NULL);
  if (resolved_public != NULL) {
    logger(LOG_DEBUG, "resolve_public_path()", "resolved public directory to %s", resolved_public);
    return resolved_public;
  }

  resolved_public = realpath("../public", NULL);
  if (resolved_public != NULL) {
    logger(LOG_DEBUG, "resolve_public_path()", "resolved fallback public directory to %s", resolved_public);
    return resolved_public;
  }

  logger(LOG_ERROR, "resolve_public_path()", "failed to resolve public directory");
  return NULL;
}

static char *resolve_file_path(const char *file_path) {
  logger(LOG_DEBUG, "resolve_file_path()", "resolving file path: %s", file_path);
  char *resolved = realpath(file_path, NULL);
  if (resolved != NULL) {
    logger(LOG_DEBUG, "resolve_file_path()", "resolved file path to %s", resolved);
    return resolved;
  }

  if (file_path[0] == '/') {
    logger(LOG_DEBUG, "resolve_file_path()", "absolute path could not be resolved: %s", file_path);
    return NULL;
  }

  char alt_path[PATH_MAX];
  int written = snprintf(alt_path, sizeof(alt_path), "../%s", file_path);
  if (written <= 0 || written >= (int)sizeof(alt_path)) {
    logger(LOG_ERROR, "resolve_file_path()", "failed to build fallback path for %s", file_path);
    return NULL;
  }

  resolved = realpath(alt_path, NULL);
  if (resolved != NULL) {
    logger(LOG_DEBUG, "resolve_file_path()", "resolved fallback file path to %s", resolved);
    return resolved;
  }

  logger(LOG_ERROR, "resolve_file_path()", "failed to resolve file path: %s", file_path);
  return NULL;
}

xps_file_t *xps_file_create(xps_core_t *core, const char *file_path, int *error) {
  /*assert*/
    assert(core != NULL);
    assert(file_path != NULL);
    assert(error != NULL);

  logger(LOG_DEBUG, "xps_file_create()", "creating file for path %s", file_path);

  *error = E_FAIL;

  char *resolved_path = resolve_file_path(file_path);
  char *resolved_public = resolve_public_path();

  if (resolved_path == NULL || resolved_public == NULL) {
    logger(LOG_ERROR, "xps_file_create()", "realpath() failed");
    if (resolved_path == NULL)
      *error = E_NOTFOUND;
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_file_create()", "resolved path %s and public root %s", resolved_path, resolved_public);

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

  logger(LOG_DEBUG, "xps_file_create()", "file is inside public directory");

  struct stat file_stat;
  if (stat(resolved_path, &file_stat) != 0) {
    logger(LOG_ERROR, "xps_file_create()", "stat() failed");
    perror("Error message");
    if (errno == ENOENT)
      *error = E_NOTFOUND;
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_file_create()", "file size is %zu bytes", (size_t)file_stat.st_size);

  if (!(file_stat.st_mode & S_IROTH)) {
    logger(LOG_WARNING, "xps_file_create()", "others do not have read permission");
    *error = E_PERMISSION;
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_file_create()", "file is readable by others");

  FILE *file_struct = fopen(resolved_path, "rb");
  if (file_struct == NULL) {
    if (errno == EACCES) {
      *error = E_PERMISSION;
      logger(LOG_WARNING, "xps_file_create()", "permission denied opening %s", resolved_path);
    } else if (errno == ENOENT) {
      *error = E_NOTFOUND;
      logger(LOG_WARNING, "xps_file_create()", "file not found: %s", resolved_path);
    } else {
      logger(LOG_ERROR, "xps_file_create()", "fopen() failed for file: %s", resolved_path);
    }
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_file_create()", "opened file %s", resolved_path);

  const char *mime_type = xps_get_mime(resolved_path);

  xps_file_t *file = malloc(sizeof(xps_file_t));
  if (file == NULL) {
    logger(LOG_ERROR, "xps_file_create()", "malloc() failed for 'file'");
    fclose(file_struct);
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_file_create()", "allocated file struct %p", (void *)file);

  xps_pipe_source_t *source = xps_pipe_source_create(file, file_source_handler, file_source_close_handler);
  if (source == NULL) {
    logger(LOG_ERROR, "xps_file_create()", "xps_pipe_source_create() failed");
    fclose(file_struct);
    free(file);
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_file_create()", "created pipe source %p", (void *)source);

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

  logger(LOG_DEBUG, "xps_file_destroy()", "destroying file struct %p", (void *)file);

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

  logger(LOG_DEBUG, "file_source_handler()", "handling file source event");

  xps_pipe_source_t *source = ptr;
  xps_file_t *file = source->ptr;

  xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
  if (buff == NULL) {
    logger(LOG_ERROR, "file_source_handler()", "xps_buffer_create() failed");
    return;
  }

  logger(LOG_DEBUG, "file_source_handler()", "allocated read buffer %p", (void *)buff);

  // Read from file
  size_t read_n = fread(buff->data, 1, buff->size, file->file_struct);
  buff->len = read_n;

  logger(LOG_DEBUG, "file_source_handler()", "read %zu bytes from %s", read_n, file->file_path);

  // Checking for read errors
  if (ferror(file->file_struct)) {
	  /*destroy buff, file and return*/
    logger(LOG_ERROR, "file_source_handler()", "fread() failed for file: %s", file->file_path);
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }

  // If end of file reached
  if (read_n == 0 && feof(file->file_struct)) {
    /*destroy buff, file and return*/
    logger(LOG_DEBUG, "file_source_handler()", "reached end of file for %s", file->file_path);
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }

  if (xps_pipe_source_write(file->source, buff) != OK) {
    logger(LOG_ERROR, "file_source_handler()", "xps_pipe_source_write() failed");
    xps_buffer_destroy(buff);
    return;
  }

  logger(LOG_DEBUG, "file_source_handler()", "wrote buffer %p into pipe", (void *)buff);

  xps_buffer_destroy(buff);
}

void file_source_close_handler(void *ptr) {
  assert(ptr != NULL);
  logger(LOG_DEBUG, "file_source_close_handler()", "closing file source");
  xps_pipe_source_t *source = ptr;
  xps_file_t *file = source->ptr;
  xps_file_destroy(file);
}
