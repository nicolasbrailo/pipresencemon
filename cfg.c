#include "cfg.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *cfg_init(const char *fpath) {
  FILE *fp = fopen(fpath, "r");
  if (!fp) {
    perror("Error opening file");
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  size_t fsz = ftell(fp);
  rewind(fp);

  char *buf = malloc(fsz + 1);
  if (!buf) {
    perror("gpio_active_monitor_init_cfg_from_file bad alloc");
    fclose(fp);
    return NULL;
  }

  fread(buf, 1, fsz, fp);
  buf[fsz] = '\0';
  fclose(fp);

  return buf;
}

void cfg_free(void *handle) { free(handle); }

static const char *advance_ptr_to_val(const void *handle, const char *key_name) {
  const char *buf = handle;
  const char *start_pos = strstr(buf, key_name);
  if (!start_pos) {
    fprintf(stderr, "Can't find config key \"%s\"\n", key_name);
    return NULL;
  }

  start_pos += strlen(key_name);
  while (*++start_pos != '=')
    ;
  while (*++start_pos == ' ')
    ;
  return start_pos;
}

bool cfg_read_size_t(const void *handle, const char *key_name, size_t *val) {
  const char *start_pos = advance_ptr_to_val(handle, key_name);
  if (!start_pos) {
    return false;
  }

  if (sscanf(start_pos, "%lu\n", val) == 1) {
    return true;
  }

  fprintf(stderr, "Can't parse number from config key \"%s\"\n", key_name);
  return false;
}

bool cfg_read_str(const void *handle, const char *key_name, char **val) {
  *val = NULL;

  const char *start_pos = advance_ptr_to_val(handle, key_name);
  if (!start_pos) {
    return false;
  }

  const char *end_pos = start_pos;
  while (*end_pos != '\n' && *end_pos != '\0')
    end_pos++;

  int sz = end_pos - start_pos - 1;
  if (sz <= 0) {
    fprintf(stderr, "Can't find string value for config key \"%s\"\n", key_name);
    return false;
  }

  *val = malloc(sz + 1);
  if (!*val) {
    fprintf(stderr, "bad alloc");
    return false;
  }

  (*val)[sz] = 0;
  strncpy(*val, start_pos, sz+1);

  return true;
}
