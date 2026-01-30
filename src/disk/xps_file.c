#include "../xps.h"
void file_source_close_handler(void *ptr);
void file_source_handler(void *ptr);

xps_file_t *xps_file_create(xps_core_t *core, const char *file_path, int *error) {
  /*assert*/
    assert(core != NULL);
    assert(file_path != NULL);
    assert(error != NULL);

  *error = E_FAIL;

 // Opening file
  FILE *file_struct = fopen(file_path, "rb");
  /*handle EACCES,ENOENT or any other error*/
  if (file_struct == NULL) {
    /*logs EACCES,ENOENT or any other error*/ 
    logger(LOG_ERROR, "xps_file_create()", "fopen() failed for file: %s", file_path);
    return NULL;
  }

  // Getting size of file
  fseek(file_struct, 0, SEEK_END);

  // Seeking to end
  if (fseek(file_struct, 0, SEEK_END) != 0) {
    /*logs error*/
    logger(LOG_ERROR, "xps_file_create()", "fseek() to end failed for file: %s", file_path);
    fclose(file_struct);
    return NULL;
  }

  // Getting curr position which is the size
  long temp_size = ftell(file_struct);/*get current position using ftell()*/
  if (temp_size < 0) {
    logger(LOG_ERROR, "xps_file_create()", "ftell() failed for file: %s", file_path);
    fclose(file_struct);
    return NULL;
  }

  // Seek back to start
  if (fseek(file_struct, 0, SEEK_SET) != 0) {
    logger(LOG_ERROR, "xps_file_create()", "fseek() to start failed for file: %s", file_path);
    fclose(file_struct);
    return NULL;
  }

  const char *mime_type = xps_get_mime(file_path);/*get mime type*/

  /*Alloc memory for instance of xps_file_t*/
  xps_file_t *file = (xps_file_t *)malloc(sizeof(xps_file_t));
  if (file == NULL) {
    logger(LOG_ERROR, "xps_file_create()", "malloc() failed for 'file'");
    fclose(file_struct);
    return NULL;
  }
  xps_pipe_source_t *source = xps_pipe_source_create((void *)file, file_source_handler, file_source_close_handler);
  /*if source is null, close file_struct and return*/
  
  // Init values
  source->ready = true;
  /*initialise the fields of file instance*/
    file->core = core;
    file->file_path = file_path;
    file->source = source;
    file->file_struct = file_struct;
    file->size = (size_t)temp_size;
    file->mime_type = mime_type;
  
	*error = OK;

  logger(LOG_DEBUG, "xps_file_create()", "created file");

  return file;
}

void xps_file_destroy(xps_file_t *file) {
  /*assert*/
    assert(file != NULL);

  /*fill as mentioned above*/
  fclose(file->file_struct);
  xps_pipe_source_destroy(file->source);
  free(file);

  logger(LOG_DEBUG, "xps_file_destroy()", "destroyed file");
}

void file_source_handler(void *ptr) {
  /*assert*/
  assert(ptr != NULL);

  xps_pipe_source_t *source = ptr;
  /*get file from source ptr*/
  xps_file_t *file = (xps_file_t *)source->ptr;

  /*create buffer and handle any error*/
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
	  /*destroy buff, file and return*/
    logger(LOG_ERROR, "file_source_handler()", "fread() failed for file: %s", file->file_path);
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }

  // If end of file reached
  if (read_n == 0 && feof(file->file_struct)) {
    /*destroy buff, file and return*/
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }

  /*Write to pipe form buff*/
    if (xps_pipe_source_write(file->source, buff) != OK) {
        logger(LOG_ERROR, "file_source_handler()", "xps_pipe_source_write() failed");
        return;
    }
	/*destroy buff*/
    xps_buffer_destroy(buff);
}

void file_source_close_handler(void *ptr) {
  /*assert*/
    assert(ptr != NULL);
	xps_pipe_source_t *source = ptr;
  /*get file from source ptr*/
    xps_file_t *file = (xps_file_t *)source->ptr;
	/*destroy file*/
    xps_file_destroy(file);
}
