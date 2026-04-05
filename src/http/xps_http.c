#include "../xps.h"

bool http_strcmp(u_char *str, const char *method, size_t length){
  /* complete this */
  for (size_t i = 0; i < length; i++) {
    if ((str[i] | 0x20) != (method[i] | 0x20)) {
      return false;
    }
  }
  return true;
}

int xps_http_parse_request_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
  /*get current parser state*/
  xps_http_parser_state_t parser_state = http_req->parser_state;
  u_char *p_ch = buff->pos;//current buffer postion
  int len = buff->len;  
  for (int i = 0; i < len; i++, p_ch++) {
    char ch = *p_ch;
    switch (parser_state) {
      case RL_START:
        /*assign request_line_start, ignore CR and LF, fail if not upper case, assign method_start*/
        if (ch == CR || ch == LF) {
          continue;
        }
        if (ch < 'A' || ch > 'Z') {
          return E_FAIL;
        }
        http_req->request_line_start = p_ch;
        http_req->method_start = p_ch;

        parser_state = RL_METHOD;
        break;

      case RL_METHOD:
        if (ch == ' ') {
          size_t method_len = p_ch - http_req->method_start;
          if (method_len == 3 && http_strcmp(http_req->method_start, "GET", 3))
            http_req->method_n = HTTP_GET;
          /*similarly check for other methods PUT, HEAD, etc*/
          else if (method_len == 3 && http_strcmp(http_req->method_start, "PUT", 3))
            http_req->method_n = HTTP_PUT;
          else if (method_len == 4 && http_strcmp(http_req->method_start, "HEAD", 4))
            http_req->method_n = HTTP_HEAD;
          else if (method_len == 4 && http_strcmp(http_req->method_start, "POST", 4))
            http_req->method_n = HTTP_POST;
          else if (method_len == 6 && http_strcmp(http_req->method_start, "DELETE", 6))
            http_req->method_n = HTTP_DELETE;
          else if (method_len == 7 && http_strcmp(http_req->method_start, "CONNECT", 7))
            http_req->method_n = HTTP_CONNECT;
          else if (method_len == 7 && http_strcmp(http_req->method_start, "OPTIONS", 7))
            http_req->method_n = HTTP_OPTIONS;
          else if (method_len == 5 && http_strcmp(http_req->method_start, "TRACE", 5))
            http_req->method_n = HTTP_TRACE;
          else
            return E_FAIL;
          /*assign method_end*/
          http_req->method_end = p_ch;
          /*next state is RL_SP_AFTER_METHOD*/
          
          parser_state = RL_SP_AFTER_METHOD;
        }
        break;

      case RL_SP_AFTER_METHOD:
        if (ch == '/') {
          /*assign start and end of schema,host,port and start of uri,path,pathname*/
          http_req->schema_start = http_req->schema_end = http_req->host_start = http_req->host_end = http_req->port_start = http_req->port_end = NULL;
          http_req->uri_start = p_ch;
          http_req->path_start = p_ch;
          http_req->pathname_start = p_ch;
          
          
          /*next state is RL_PATH*/
          parser_state = RL_PATH;
        } else {
          char c = ch | 0x20; //convert to lower case
          /*if lower case alphabets, assign start of schema and uri, next state is RL_SCHEMA*/
          /*if not space(''), fails*/
          if (c < 'a' || c > 'z') return E_FAIL;
          http_req->schema_start = http_req->uri_start = p_ch;
          parser_state = RL_SCHEMA;

        }
        break;

      case RL_SCHEMA:
        /*on lower case alphabets break ie schema can have lower case alphabets*/
        /*schema ends on ':' ,next state is RL_SCHEMA_SLASH*/
        /*fails on all other inputs*/
        if (ch == ':') {
          http_req->schema_end = p_ch;
          parser_state = RL_SCHEMA_SLASH;
        }
        else{
          char c = ch | 0x20; //convert to lower case
          if (c < 'a' || c > 'z') {
            return E_FAIL;
          }
        }
        break;

      case RL_SCHEMA_SLASH:
        /*on '/' assign next state, fails on all other inputs*/
        if (ch == '/') {
          parser_state = RL_SCHEMA_SLASH_SLASH;
        }
        break;

      case RL_SCHEMA_SLASH_SLASH:
        /*on '/' - assign next state, for start of host assign next position, fails on all other inputs*/
        if(ch == '/'){
          parser_state = RL_HOST;
          http_req->host_start = p_ch + 1;
        }
        break;

      case RL_HOST:
        /*host can have lower case alphabets, numbers, '-', '.' */
        /*on ':' - host ends, for start of port assign next position, next state is RL_PORT*/
        /*on '/' - host ends, assign start of path,pathname, end of port, next state is RL_PATH*/
        /*on ' ' - host ends, assign end of uri, start and end of port,path,pathname, next state is RL_VERSION_START*/
        /*on all other input, fails*/
        if(ch == ':'){
          http_req->host_end = p_ch;
          parser_state = RL_PORT;
          http_req->port_start = p_ch + 1;
        }
        else if(ch == '/'){
          http_req->host_end = p_ch;
          http_req->port_end = p_ch;
          http_req->path_start = p_ch;
          http_req->pathname_start = p_ch;
          parser_state = RL_PATH;
        }
        else if(ch == ' '){
          http_req->host_end = p_ch;
          http_req->port_end = p_ch;
          http_req->path_start = p_ch;
          http_req->pathname_start = p_ch;
          http_req->uri_end = p_ch;
          parser_state = RL_VERSION_START;
        }
        else{
          char c = ch | 0x20; //convert to lower case
          if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
            return E_FAIL;
          }
        }
        break;

      case RL_PORT:
        /*port can only have numbers */
        /*on '/' - port ends, assign start of path,pathname, next state is RL_PATH*/
        /*on ' ' - port ends, assign end of uri, start and end of path,pathname, next state is RL_VERSION_START*/
        /*on all other input, fails*/
        if(ch == '/'){
          http_req->port_end = p_ch;
          http_req->path_start = p_ch+1;
          http_req->pathname_start = p_ch+1;
          parser_state = RL_PATH;
        }
        else if(ch == ' '){
          http_req->port_end = p_ch;
          http_req->path_start = p_ch;
          http_req->pathname_start = p_ch;
          http_req->uri_end = p_ch;
          http_req->path_end = p_ch;
          http_req->pathname_end = p_ch;
          parser_state = RL_VERSION_START;
        }
        else{
          if (ch < '0' || ch > '9') {
            return E_FAIL;
          }
        }
        break;

      case RL_PATH:
        /*on ' ' - path ends, assign end of path,pathname,uri next state is RL_VERSION_START*/
        /*on '?'or'&'or'='or'#' - assign end of pathname, next state is RL_PATHNAME*/
        /*on CR or LF, fails*/
        if(ch == ' '){
          http_req->path_end = p_ch;
          http_req->pathname_end = p_ch;
          http_req->uri_end = p_ch;
          parser_state = RL_VERSION_START;
        }
        else if(ch == '?' || ch == '&' || ch == '=' || ch == '#'){
          http_req->pathname_end = p_ch;
          parser_state = RL_PATHNAME;
        }
        else{
          if (ch == CR || ch == LF) {
            return E_FAIL;
          }
        }
        break;

      case RL_PATHNAME:
        /*on ' ' - assign end of uri,path, next state is RL_VERSION_START*/
        /*on CR or LF, fails*/
        if(ch == ' '){
          http_req->path_end = p_ch;
          http_req->pathname_end = p_ch;
          http_req->uri_end = p_ch;
          parser_state = RL_VERSION_START;
        }
        else{
          if (ch == CR || ch == LF) {
            return E_FAIL;
          }
        }
        break;

      case RL_VERSION_START:
        /*can have space*/
        /*on 'H' - next state is RL_VERSION_H*/
        /*fails on all other input*/
        if (ch == 'H') {
          parser_state = RL_VERSION_H;
        } else if (ch != ' ') {
          return E_FAIL;
        }
        break;

      case RL_VERSION_H:
        /*fill this*/
        if (ch == 'T') {
          parser_state = RL_VERSION_HT;
        } else {
          return E_FAIL;
        }
        break;

      case RL_VERSION_HT:
        /*fill this*/
        if (ch == 'T') {
          parser_state = RL_VERSION_HTT;
        } else {
          return E_FAIL;
        }
        break;

      case RL_VERSION_HTT:
        /*fill this*/
        if (ch == 'P') {
          parser_state = RL_VERSION_HTTP;
        } else {
          return E_FAIL;
        }

        break;

      case RL_VERSION_HTTP:
        /*fill this*/
        if (ch == '/') {
          parser_state = RL_VERSION_HTTP_SLASH;
        } else {
          return E_FAIL;
        }
        break;

      case RL_VERSION_HTTP_SLASH:
        /*on '1' - assign major, next state is RL_MAJOR, fails on all other inputs*/
        if (ch == '1') {
          http_req->http_major = p_ch;
          parser_state = RL_VERSION_MAJOR;
        } else {
          return E_FAIL;
        }
        break;

      case RL_VERSION_MAJOR:
        /*fill ths*/
        if (ch == '.') {
          parser_state = RL_VERSION_DOT;
        } else {
          return E_FAIL;
        }
        break;

      case RL_VERSION_DOT:
        /*on '0' or '1' - assign minor to next position, next state is RL_VERSION_MINOR, fails on other inputs*/
        if (ch == '0' || ch == '1') {
          http_req->http_minor = p_ch;
          parser_state = RL_VERSION_MINOR;
        } else {
          return E_FAIL;
        }
        break;

      case RL_VERSION_MINOR:
        if (ch == CR) {
          parser_state = RL_CR;
        } else if (ch == LF) {
          parser_state = RL_LF;
        } else {
          return E_FAIL;
        }
        break;

      case RL_CR:
        /*fill this*/
          if (ch == LF) {
            parser_state = RL_LF;
          } else {
            return E_FAIL;
          }
        break;

      case RL_LF:
        if (http_req->request_line_end == NULL) {
          http_req->request_line_end = p_ch;
        }
        http_req->parser_state = H_START;
        buff->pos = p_ch;
        return OK;

      default:
        return E_FAIL;
    }
  }
  http_req->parser_state = parser_state;
  return E_AGAIN;
}

int xps_http_parse_header_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
  assert(http_req != NULL);
  assert(buff != NULL);

  u_char *p_ch = buff->pos;
  xps_http_parser_state_t parser_state = http_req->parser_state;

  int len = buff->len;  
  for (int i = 0; i < len; i++, p_ch++) {
    char ch = *p_ch;
    // logger(LOG_DEBUG, "xps_http_parse_header_line()", "parsing header line, current char: %c, parser state: %d", ch, parser_state);
    switch (parser_state) {
      case H_START:
        /*fill this*/
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
          http_req->header_key_start = p_ch;
          parser_state = H_NAME;
        } else {
          return E_FAIL;
        }
        break;

      case H_NAME:
        /*fill this*/
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-') {
          continue;
        } else if (ch == ':') {
          //NTLB
          http_req->header_key_end = p_ch;
          parser_state = H_COLON;
        } else {
          return E_FAIL;
        }

        break;

      case H_COLON:
        /*fill this*/
        if (ch == ' ') {
          parser_state = H_SP_AFTER_COLON;
        } else {
          http_req->header_val_start = p_ch;
          parser_state = H_VAL;
        }
        break;

      case H_SP_AFTER_COLON:
        /*fill this*/
        if(ch == ' '){
          continue;
        }
        else{
          http_req->header_val_start = p_ch;
          parser_state = H_VAL;
        }
        break;

      case H_VAL:
        if (ch == CR || ch == LF) {
          /*fill this*/
          http_req->header_val_end = p_ch;
          parser_state = (ch == CR) ? H_CR : H_LF;
        }
        break;

      case H_CR:
        /*fill this*/
        if (ch == LF) {
          parser_state = H_LF;
        } else {
          return E_FAIL;
        }
        break;

      case H_LF:
        if (ch == LF) {
          parser_state = H_LF_LF;
        } else if (ch == CR) {
          parser_state = H_LF_CR;
        } else {
          parser_state = H_START;
          buff->pos = p_ch;
          http_req->parser_state = parser_state;
          return E_NEXT; // This header is done, repeat for the next
        }
        break;
        
      case H_LF_LF:
        buff->pos = p_ch;
        http_req->parser_state = H_START;
        return OK; // HTTP complete header section done

      case H_LF_CR:
        if (ch == LF) {
          buff->pos = p_ch;
          http_req->parser_state = H_START;
          return OK; // HTTP complete header section done
        } else {
          return E_FAIL;
        }
        break;

      default:
        return E_FAIL;
    }
  }
  http_req->parser_state = parser_state;
  return E_AGAIN;
}

const char *xps_http_get_header(vec_void_t *headers, const char *key) {
  /*Validate params*/
  assert(headers != NULL);
  assert(key != NULL);

  for (int i = 0; i < headers->length; i++) {
    xps_keyval_t *header = (headers->data[i]);
    if (strcasecmp(header->key, key) == 0)
      return header->val;
  }
	return NULL;
}


xps_buffer_t *xps_http_serialize_headers(vec_void_t *headers) {
  /*assert*/
  assert(headers != NULL);
	/*create a buffer and initialize first byte to null terminator*/
  xps_buffer_t *buff = malloc(sizeof(*buff));
  if (buff == NULL) {
    return NULL;
  }
  buff->size = DEFAULT_BUFFER_SIZE; //initial size
  buff->len = 0;
  buff->data = malloc(buff->size);
  buff->pos = buff->data;
  if (buff->data == NULL) {
    free(buff);
    return NULL;
  }
  buff->data[0] = '\0';
  for (int i = 0; i < headers->length; i++) {
    xps_keyval_t *header = (xps_keyval_t*)headers->data[i];
    /*get required length to store a header*/
    size_t header_str_len = strlen(header->key) + strlen(header->val) + 5; // for :, space, newline and null terminator
    char header_str[header_str_len];
    sprintf(header_str, "%s: %s\r\n", header->key, header->val);
    if ((buff->size - buff->len) < header_str_len) { //buffer is small
      u_char *new_data = realloc(buff->data, buff->size * 2);
      /*handle error*/
      if (new_data == NULL) {
        free(buff->data);
        free(buff);
        return NULL;
      }
      /*update buff->data and buff->size*/
      buff->data = new_data;
      buff->size *= 2;
      buff->pos = buff->data;
    }
    strcat(buff->data, header_str);
    buff->len = strlen(buff->data);
  }
	return buff;
}