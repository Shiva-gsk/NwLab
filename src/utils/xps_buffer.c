#include "../xps.h"

// xps_buffer

xps_buffer_t *xps_buffer_create(size_t size, size_t len, u_char *data) {
  assert(size > 0);

  // Alloc memory for instance
  xps_buffer_t *buff = malloc(sizeof(xps_buffer_t));
  if (buff == NULL) {
    logger(LOG_ERROR, "xps_buffer_create()", "malloc() failed for 'buff'");
    return NULL;
  }
  logger(LOG_DEBUG, "xps_buffer_create()", "allocated buffer %p (size=%zu, len=%zu)", (void*)buff, size, len);

  // Alloc memory for 'data' if it is NULL
  if (data == NULL)
    data = malloc(size);

  if (data == NULL) {
    logger(LOG_ERROR, "xps_buffer_create()", "malloc() failed for 'data'");
    logger(LOG_DEBUG, "xps_buffer_create()", "freeing buffer %p due to data allocation failure", (void*)buff);
    free(buff);
    return NULL;
  }
  logger(LOG_DEBUG, "xps_buffer_create()", "allocated buffer data %p for buffer %p", (void*)data, (void*)buff);

  // Init values
  buff->size = size;
  buff->len = len;
  buff->data = data;
  buff->pos = data;

  return buff;
}

void xps_buffer_destroy(xps_buffer_t *buff) {
  if(!buff) {
    logger(LOG_ERROR, "xps_buffer_destroy()", "invalid 'buff' parameter");
    return;
  }
  assert(buff != NULL);
  logger(LOG_DEBUG, "xps_buffer_destroy()", "destroying buffer %p with data %p (len=%zu)", (void*)buff, (void*)buff->data, buff->len);
  free(buff->data);
  logger(LOG_DEBUG, "xps_buffer_destroy()", "freed buffer data %p", (void*)buff->data);
  free(buff);
  logger(LOG_DEBUG, "xps_buffer_destroy()", "freed buffer %p", (void*)buff);
}

xps_buffer_t *xps_buffer_duplicate(xps_buffer_t *buff) {
  assert(buff != NULL);

  xps_buffer_t *dup_buff = xps_buffer_create(buff->size, buff->len, NULL);
  if (dup_buff == NULL) {
    logger(LOG_ERROR, "xps_buffer_duplicate()", "xps_buffer_create() failed");
    return NULL;
  }

  // Set 'pos' of dup_buff
  dup_buff->pos = dup_buff->data + (buff->pos - buff->data);

  // Copy over data
  memcpy(dup_buff->data, buff->data, dup_buff->len);

  return dup_buff;
}

// xps_buffer_list

xps_buffer_list_t *xps_buffer_list_create() {
  // Alloc memory for instance
  xps_buffer_list_t *buff_list = malloc(sizeof(xps_buffer_list_t));
  if (buff_list == NULL) {
    logger(LOG_ERROR, "xps_buffer_list_create()", "malloc() failed for 'buff_list'");
    return NULL;
  }

  // Init values
  vec_init(&(buff_list->list));
  buff_list->len = 0;

  return buff_list;
}

void xps_buffer_list_destroy(xps_buffer_list_t *buff_list) {
  assert(buff_list != NULL);
  logger(LOG_DEBUG, "xps_buffer_list_destroy()", "destroying buffer list %p with %d buffers", (void*)buff_list, buff_list->list.length);

  // Destroy buffers in the list
  for (int i = 0; i < buff_list->list.length; i++) {
    xps_buffer_t *curr_buff = buff_list->list.data[i];
    if (curr_buff != NULL) {
      logger(LOG_DEBUG, "xps_buffer_list_destroy()", "destroying buffer %d/%d in list", i+1, buff_list->list.length);
      xps_buffer_destroy(curr_buff);
    }
  }
  vec_deinit(&(buff_list->list));
  logger(LOG_DEBUG, "xps_buffer_list_destroy()", "freeing buffer list %p", (void*)buff_list);

  free(buff_list);
}

void xps_buffer_list_append(xps_buffer_list_t *buff_list, xps_buffer_t *buff) {
  assert(buff_list != NULL);
  assert(buff != NULL);

  vec_push(&(buff_list->list), (void *)buff);

  buff_list->len += buff->len;
}

xps_buffer_t *xps_buffer_list_read(xps_buffer_list_t *buff_list, size_t len) {
  assert(buff_list != NULL);
  assert(len > 0);

  // Check if requested length is available
  if (buff_list->len < len) {
    logger(LOG_ERROR, "xps_buffer_list_read()", "requested length not available");
    return NULL;
  }

  // Buffer to be returned
  xps_buffer_t *buff = xps_buffer_create(len, len, NULL);
  if (buff == NULL) {
    logger(LOG_ERROR, "xps_buffer_list_read()", "xps_buffer_create() failed");
    return NULL;
  }

  size_t curr_len = 0;
  for (int i = 0; i < buff_list->list.length && curr_len < len; i++) {
    xps_buffer_t *curr_buff = buff_list->list.data[i];

    // Condition where full buffer can be copied
    if ((curr_len + curr_buff->len) <= len) {
      memcpy(buff->data + curr_len, curr_buff->data, curr_buff->len);
      curr_len += curr_buff->len;
    }
    // Condition where partial buffer has to be copied
    else {
      size_t len_diff = len - curr_len;
      memcpy(buff->data + curr_len, curr_buff->data, len_diff);
      curr_len += len_diff;
    }
  }

  return buff;
}

int xps_buffer_list_clear(xps_buffer_list_t *buff_list, size_t len) {
  assert(buff_list != NULL);

  if (len == 0)
    return OK;

  // Check if requested length is available
  if (buff_list->len < len) {
    logger(LOG_ERROR, "xps_buffer_list_clear()", "requested length not available");
    return E_FAIL;
  }

  size_t to_clear_len = len;
  for (int i = 0; i < buff_list->list.length && to_clear_len > 0; i++) {
    xps_buffer_t *curr_buff = buff_list->list.data[i];

    // Condition where full buffer can be destroyed
    if (to_clear_len >= curr_buff->len) {
      to_clear_len -= curr_buff->len;
      xps_buffer_destroy(curr_buff);
      buff_list->list.data[i] = NULL;
    }
    // Condition where partial buffer has to be cleared
    else {
      memmove(curr_buff->data, curr_buff->data + to_clear_len, curr_buff->len - to_clear_len);
      curr_buff->len -= to_clear_len;
      to_clear_len = 0;
    }
  }

  buff_list->len -= len;
  vec_filter_null(&(buff_list->list));

  return OK;
}