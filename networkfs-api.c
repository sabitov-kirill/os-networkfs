//
// Created by ne1mnn on 12/10/23.
//

#include "networkfs-api.h"

#include <linux/fs_types.h>
#include <linux/slab.h>

#include "http.h"

static const char *networkfs_api_error[9] = {
    "success",
    "object with specified inode number not found",
    "object is not a file",
    "object is not a directory",
    "no entry with specified name in the directory",
    "entry with specified name already exists in the directory",
    "file size limit exceeded (512 bytes)",
    "directory entry limit exceeded (16 entries)",
    "directory is not empty"};

static void networkfs_api_print_status(const char *method, int64_t code) {
  printk(KERN_INFO "networkfs-api: Method %s returned code %lld: %s", method,
         code,
         code >= 0 && code < 9 ? networkfs_api_error[code] : "unknown code");
}

#define NETWORKFS_API_METHOD_WITH_RESPONSE(                                  \
    token, method, response_buffer_size, response_type, arg_size, ...)       \
  char *response = kmalloc(response_buffer_size, GFP_KERNEL);                \
  if (response == NULL) {                                                    \
    return NULL;                                                             \
  }                                                                          \
  int64_t code = networkfs_http_call(                                        \
      token, method, response, response_buffer_size, arg_size, __VA_ARGS__); \
  networkfs_api_print_status(method, code);                                  \
  if (code != 0) {                                                           \
    kfree(response);                                                         \
    return NULL;                                                             \
  }                                                                          \
  return (struct response_type *)response

static void networkfs_api_process_string(char *dest, const char *src) {
  size_t len = strlen(src);
  for (size_t i = 0; i < len; ++i) {
    sprintf(dest + i * 3, "%%%02x", src[i]);
  }
  dest[len * 3] = '\0';
}

struct entries *networkfs_api_list(const char *token, ino_t i_ino) {
  char i_ino_str[sizeof(unsigned long) + 1];
  sprintf(i_ino_str, "%lu", i_ino);

  NETWORKFS_API_METHOD_WITH_RESPONSE(token, "list", sizeof(struct entries),
                                     entries, 1, "inode", i_ino_str);
}

struct entry_info *networkfs_api_lookup(const char *token, ino_t parent_ino,
                                        const char *name) {
  char parent_ino_str[sizeof(unsigned long) + 1];
  sprintf(parent_ino_str, "%lu", parent_ino);

  char processed_name[NETWORKFS_API_ENTRY_MAX_NAME_LEN * 3 + 1];
  networkfs_api_process_string(processed_name, name);

  NETWORKFS_API_METHOD_WITH_RESPONSE(token, "lookup", sizeof(struct entries),
                                     entry_info, 2, "parent", parent_ino_str,
                                     "name", processed_name);
}

int64_t networkfs_api_create(const char *token, ino_t parent_ino,
                             const char *name, int type, ino_t *created_ino) {
  char parent_ino_str[sizeof(unsigned long) + 1];
  sprintf(parent_ino_str, "%lu", parent_ino);

  char processed_name[NETWORKFS_API_ENTRY_MAX_NAME_LEN * 3 + 1];
  networkfs_api_process_string(processed_name, name);

  int64_t code =
      networkfs_http_call(token, "create", (char *)created_ino, sizeof(ino_t),
                          3, "parent", parent_ino_str, "name", processed_name,
                          "type", type == DT_DIR ? "directory" : "file");
  networkfs_api_print_status("create", code);
  return code;
}

int64_t networkfs_api_remove(const char *token, ino_t parent_ino,
                             const char *name, int type) {
  const char *method = type == DT_DIR ? "rmdir" : "unlink";

  char parent_ino_str[sizeof(unsigned long) + 1];
  sprintf(parent_ino_str, "%lu", parent_ino);

  char processed_name[NETWORKFS_API_ENTRY_MAX_NAME_LEN * 3 + 1];
  networkfs_api_process_string(processed_name, name);

  int64_t code = networkfs_http_call(token, method, NULL, 0, 2, "parent",
                                     parent_ino_str, "name", processed_name);

  networkfs_api_print_status(method, code);
  return code;
}

int64_t networkfs_api_link(const char *token, ino_t source_ino,
                           ino_t parent_ino, const char *name) {
  char parent_ino_str[sizeof(unsigned long) + 1];
  sprintf(parent_ino_str, "%lu", parent_ino);

  char source_ino_str[sizeof(unsigned long) + 1];
  sprintf(source_ino_str, "%lu", source_ino);

  char processed_name[NETWORKFS_API_ENTRY_MAX_NAME_LEN * 3 + 1];
  networkfs_api_process_string(processed_name, name);

  int64_t code =
      networkfs_http_call(token, "link", NULL, 0, 3, "source", source_ino_str,
                          "parent", parent_ino_str, "name", processed_name);

  networkfs_api_print_status("link", code);
  return code;
}

int64_t networkfs_api_read(const char *token, ino_t ino,
                           struct content **read_content_ptr) {
  *read_content_ptr = kmalloc(sizeof(struct content), GFP_KERNEL);
  if (*read_content_ptr == NULL) {
    return -ENOMEM;
  }

  char ino_str[sizeof(unsigned long) + 1];
  sprintf(ino_str, "%lu", ino);

  int64_t code =
      networkfs_http_call(token, "read", (char *)(*read_content_ptr),
                          sizeof(struct content), 1, "inode", ino_str);
  networkfs_api_print_status("read", code);
  if (code != 0) {
    kfree(*read_content_ptr);
  }
  return code;
}

int64_t networkfs_api_write(const char *token, ino_t ino, const char *content) {
  char *processed_content =
      kmalloc(NETWORKFS_API_ENTRY_MAX_FILE * 3 + 1, GFP_KERNEL);
  if (processed_content == NULL) {
    return -ENOMEM;
  }
  networkfs_api_process_string(processed_content, content);

  char ino_str[sizeof(unsigned long) + 1];
  sprintf(ino_str, "%lu", ino);

  int64_t code = networkfs_http_call(token, "write", NULL, 0, 2, "inode",
                                     ino_str, "content", processed_content);
  networkfs_api_print_status("write", code);

  kfree(processed_content);
  return code;
}
