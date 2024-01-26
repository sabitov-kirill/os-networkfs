//
// Created by ne1mnn on 12/10/23.
//

#ifndef NETWORKFS_NETWORKFS_API_H
#define NETWORKFS_NETWORKFS_API_H

#include <linux/types.h>

#define NETWORKFS_API_ENTRY_MAX_NAME_LEN 256
#define NETWORKFS_API_ENTRY_MAX_FILE 512

struct entry {
  unsigned char entry_type;  // DT_DIR (4) or DT_REG (8)
  ino_t ino;
  char name[NETWORKFS_API_ENTRY_MAX_NAME_LEN];
};

struct entries {
  size_t entries_count;
  struct entry entries[16];
};

struct entry_info {
  unsigned char entry_type;  // DT_DIR (4) or DT_REG (8)
  ino_t ino;
};

struct content {
  u64 content_length;
  char content[NETWORKFS_API_ENTRY_MAX_FILE + 1];
};

struct entries *networkfs_api_list(const char *token, ino_t i_ino);

struct entry_info *networkfs_api_lookup(const char *token, ino_t parent_ino,
                                        const char *name);

int64_t networkfs_api_create(const char *token, ino_t parent_ino,
                             const char *name, int type, ino_t *created_ino);

int64_t networkfs_api_remove(const char *token, ino_t parent_ino,
                             const char *name, int type);

int64_t networkfs_api_link(const char *token, ino_t source_ino,
                           ino_t parent_ino, const char *name);

int64_t networkfs_api_read(const char *token, ino_t ino,
                           struct content **read_content_ptr);

int64_t networkfs_api_write(const char *token, ino_t ino, const char *content);

#endif  // NETWORKFS_NETWORKFS_API_H
