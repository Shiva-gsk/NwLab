#include "../xps.h"
#include <time.h>



xps_buffer_t *xps_http_res_serialize(xps_http_res_t *res);
void xps_http_res_set_body(xps_http_res_t *http_res, xps_buffer_t *buff);

xps_http_res_t *xps_http_res_create(xps_core_t *core, u_int code){
    assert(core != NULL);
    xps_http_res_t *http_res = (xps_http_res_t *)malloc(sizeof(xps_http_res_t));
    if (http_res == NULL) {
        logger(LOG_ERROR, "xps_http_res_create()", "malloc() failed for 'http_res'");
        return NULL;
    }
    // Initialize the response
    snprintf(http_res->response_line, sizeof(http_res->response_line), "HTTP/1.1 %u %s\r\n", code, xps_http_status_text(code));
    vec_init(&http_res->headers);
    http_res->body = NULL;
    // time_t now = time(NULL);
    // char time_buf[100];
    // strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    // xps_http_set_header(&(http_res->headers), "Date", time_buf);
    
    // xps_http_set_header(&(http_res->headers), "Access-Control-Allow-Origin", "*");
    return http_res;

}

void xps_http_res_destroy(xps_http_res_t *res){
    if (res == NULL) {   
        return; 
    }
    logger(LOG_DEBUG, "xps_http_res_destroy()", "destroying http response %p", (void *)res);
    /* destroy headers */
    for (int i = 0; i < res->headers.length; i++) {
        xps_keyval_t *header = (xps_keyval_t*)res->headers.data[i];
        if (header != NULL) {
            free(header->key);
            free(header->val);
            free(header);   
        }
    }
    vec_deinit(&res->headers);
    free(res->body);
    free(res);

}

xps_buffer_t *xps_http_res_serialize(xps_http_res_t *http_res) {
    if (http_res == NULL) return NULL;
    // xps_http_set_header(&(http_res->headers), "Server", SERVER_NAME);

  xps_buffer_t *headers_str = xps_http_serialize_headers(&http_res->headers);
  if (headers_str == NULL) return NULL;

  size_t res_line_len = strlen(http_res->response_line);
  size_t headers_len = headers_str->len;
  size_t body_len = http_res->body ? http_res->body->len : 0;

  // Length: response line + headers + final CRLF + body
  size_t final_len = res_line_len + headers_len + 2 + body_len;

  xps_buffer_t *buff = xps_buffer_create(final_len, final_len, NULL);
  if (buff == NULL) {
    xps_buffer_destroy(headers_str);
    return NULL;
  }
  buff->pos = buff->data;

  // Copy elements
  memcpy(buff->pos, http_res->response_line, res_line_len);
  buff->pos += res_line_len;
  
  memcpy(buff->pos, headers_str->data, headers_len);
  buff->pos += headers_len;

  *buff->pos = '\r'; buff->pos += 1;
  *buff->pos = '\n'; buff->pos += 1;

  if (http_res->body && body_len > 0) {
    memcpy(buff->pos, http_res->body->data, body_len);
    buff->pos += body_len;
  }

  xps_buffer_destroy(headers_str);
  buff->len = final_len;
  buff->pos = buff->data;

  return buff;
}

void xps_http_res_set_body(xps_http_res_t *http_res, xps_buffer_t *buff){
    assert(http_res != NULL);
    assert(buff != NULL);
    http_res->body = buff;
    // char content_len_str[20];
    // sprintf(content_len_str, "%zu", buff->len);
    // xps_http_set_header(&(http_res->headers), "Content-Length", content_len_str);
}