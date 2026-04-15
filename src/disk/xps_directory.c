#include <dirent.h>    // Required for DIR, opendir, readdir, closedir
#include <time.h>

#include "../xps.h"

static bool append_html(char *buff, size_t buff_size, size_t *offset, const char *fmt, ...)
{
  va_list args;

  assert(buff != NULL);
  assert(offset != NULL);
  assert(fmt != NULL);
  assert(*offset < buff_size);

  va_start(args, fmt);
  int written = vsnprintf(buff + *offset, buff_size - *offset, fmt, args);
  va_end(args);

  if (written < 0 || (size_t)written >= buff_size - *offset)
    return false;

  *offset += (size_t)written;
  return true;
}

static const char *directory_size_label(const struct stat *file_stat, char *size_buf, size_t size_buf_len)
{
  assert(file_stat != NULL);
  assert(size_buf != NULL);

  if (S_ISDIR(file_stat->st_mode))
  {
    snprintf(size_buf, size_buf_len, "-");
  }
  else
  {
    double size_kb = (double)file_stat->st_size / 1024.0;
    snprintf(size_buf, size_buf_len, "%.2f KB", size_kb);
  }

  return size_buf;
}

xps_buffer_t *xps_directory_browsing(const char *dir_path, const char *pathname){
  /* validate parameters */
  assert(dir_path != NULL);
  assert(pathname != NULL);

  // Buffer for HTTP message
  char *buff = (char *)malloc(DEFAULT_BUFFER_SIZE); 
  if (buff == NULL) {
    logger(LOG_ERROR, "xps_directory_browsing()", "malloc() failed for 'buff'");
    return NULL;
  }

  size_t used = 0;

  // Append the html file into this buff starting from the heading and basic styling
  if (!append_html(buff, DEFAULT_BUFFER_SIZE, &used,
                   "<html><head lang='en'><meta http-equiv='Content-Type' "
                   "content='text/html; charset=UTF-8'><meta name='viewport' "
                   "content='width=device-width, initial-scale=1.0'><title>Directory: %s</title>"
                   "<style>body{font-family: monospace; font-size: 15px;}table{border-collapse: collapse;}"
                   "th,td{padding: 4px 10px 4px 0;text-align:left;vertical-align:top;}"
                   "th{border-bottom:1px solid rgba(0,0,0,0.2);}h1{font-family: serif; margin: 0;}"
                   "h3{font-family: serif;margin: 12px 0px; background-color: rgba(0,0,0,0.1);"
                   "padding: 4px 0px;}</style></head><body><h1>eXpServer</h1><h3>Index of&nbsp;%s</h3>"
                   "<hr><table><tr><th>Name</th><th>Last modified</th><th>Size</th></tr>",
                   pathname, pathname)) {
    free(buff);
    return NULL;
  }

  // Open a directory stream using opendir function
  DIR *dir = opendir(dir_path);
  if(dir == NULL){
    logger(LOG_ERROR, "xps_directory_browsing()", "opendir() failed");
    free(buff);
    return NULL;
  }

  struct dirent *dir_entry;

  while((dir_entry = readdir(dir)) != NULL){
    // skip the first two entries such as . and .. in list directory from dir_entry->d_name
    if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0)
        continue;

    char full_path[1024];
    if (snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir_entry->d_name) >= (int)sizeof(full_path)) {
      logger(LOG_ERROR, "xps_directory_browsing()", "path too long for directory entry");
      continue;
    }

    struct stat file_stat;

    // get information about the file
    if(stat(full_path, &file_stat) == -1){
        logger(LOG_ERROR, "xps_directory_browsing()", "stat()");
        continue;
    }
    
    // check if file is regular file or if the file is a direcory
    if(S_ISREG(file_stat.st_mode) || S_ISDIR(file_stat.st_mode)){
      char *is_dir = S_ISDIR(file_stat.st_mode) ? "/" : "";
      char mtime_buf[32];
      char size_buf[32];

      struct tm tm_info;
      if (localtime_r(&file_stat.st_mtime, &tm_info) != NULL)
        strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
      else
        snprintf(mtime_buf, sizeof(mtime_buf), "-");

      directory_size_label(&file_stat, size_buf, sizeof(size_buf));

      char *temp_pathname = str_create(pathname);
      if (temp_pathname == NULL) {
        logger(LOG_ERROR, "xps_directory_browsing()", "str_create() failed for pathname");
        continue;
      }

        if (temp_pathname[0] != '\0' && temp_pathname[strlen(temp_pathname) - 1] == '/')
          temp_pathname[strlen(temp_pathname) - 1] = '\0';

      const char *href_prefix = temp_pathname;
      if (strcmp(href_prefix, "/") == 0)
        href_prefix = "";

      if (!append_html(buff, DEFAULT_BUFFER_SIZE, &used,
                       "<tr><td><a href='%s/%s'>%s%s</a></td><td>%s</td><td>%s</td></tr>\n",
                       href_prefix, dir_entry->d_name, dir_entry->d_name, is_dir,
                       mtime_buf, size_buf)) {
        logger(LOG_ERROR, "xps_directory_browsing()", "HTML buffer overflow");
        free(temp_pathname);
        break;
      }

      free(temp_pathname);
    }
  }
  
  closedir(dir);

  if (!append_html(buff, DEFAULT_BUFFER_SIZE, &used, "</table></body></html>")) {
    free(buff);
    return NULL;
  }

  xps_buffer_t *directory_browsing = xps_buffer_create(used + 1, used, (u_char *)buff);

  if (directory_browsing == NULL) {
      logger(LOG_ERROR, "xps_directory_browsing()","xps_buffer_create() returned NULL");
      free(buff);
      return NULL;
  }

  return directory_browsing;
}