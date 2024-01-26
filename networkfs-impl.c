//
// Created by ne1mnn on 12/10/23.
//

#include "networkfs-impl.h"

#include <linux/fs.h>
#include <linux/fs_context.h>

#include "networkfs-api.h"

#define NETWORKFS_ROOT_INO 1000

/*************************
 * Directories Operations
 *************************/

int networkfs_iterate(struct file *file, struct dir_context *ctx) {
  struct dentry *dentry = file->f_path.dentry;
  struct inode *inode = dentry->d_inode;

  loff_t record_counter = 0;

  while (true) {
    switch (ctx->pos) {
      case 0: {
        dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR);
        break;
      }
      case 1: {
        struct inode *parent_inode = dentry->d_parent->d_inode;
        dir_emit(ctx, "..", 2, parent_inode->i_ino, DT_DIR);
        break;
      }
      case 2: {
        struct entries *entries =
            networkfs_api_list(dentry->d_sb->s_fs_info, inode->i_ino);
        if (entries != NULL) {
          for (struct entry *e = entries->entries + ctx->pos - 2;
               e != entries->entries + entries->entries_count; ++e) {
            dir_emit(ctx, e->name, (int)strlen(e->name), e->ino, e->entry_type);
            ++record_counter;
            ++ctx->pos;
          }
          kfree(entries);
        }
        break;
      }
      default:
        return (int)record_counter;
    }

    ++record_counter;
    ++ctx->pos;
  }
}

struct file_operations networkfs_dir_ops = {.iterate = networkfs_iterate};

/*******************
 * Files operations
 *******************/

int networkfs_file_open(struct inode *inode, struct file *filp) {
  struct content *content;
  int64_t code =
      networkfs_api_read(inode->i_sb->s_fs_info, inode->i_ino, &content);
  if (code == 0) {
    filp->private_data = content->content;
    inode->i_size = (loff_t)content->content_length;

    if (filp->f_flags & O_APPEND) {
      generic_file_llseek(filp, (loff_t)content->content_length, SEEK_SET);
    }
  } else {
    filp->private_data = NULL;
  }
  return (int)code;
}

ssize_t networkfs_file_read(struct file *filp, char *buffer, size_t len,
                            loff_t *offset) {
  ssize_t max_read_count = (ssize_t)(filp->f_inode->i_size) - *offset;
  if (max_read_count < 0) {
    return -EINVAL;
  }

  ssize_t read_count = min((ssize_t)len, max_read_count);
  if (copy_to_user(buffer, filp->private_data + *offset, read_count + 1) != 0) {
    return -EFAULT;
  }
  *offset += read_count;
  return read_count;
}

ssize_t networkfs_file_write(struct file *filp, const char *buffer, size_t len,
                             loff_t *offset) {
  ssize_t max_write_count = NETWORKFS_API_ENTRY_MAX_FILE - *offset;
  if (max_write_count <= 0) {
    return -EDQUOT;
  }

  ssize_t write_count = min((ssize_t)len, max_write_count);
  write_count -= (ssize_t)copy_from_user(filp->private_data + *offset, buffer,
                                         write_count);
  *offset += write_count;

  filp->f_inode->i_size = max(*offset, filp->f_inode->i_size);
  return write_count;
}

int networkfs_file_flush_impl(struct file *filp) {
  struct inode *inode = filp->f_inode;
  char *content = filp->private_data;
  content[inode->i_size] = '\0';

  printk(KERN_INFO "flush %lu, len=%zu, size=%lld, content='%s'",
         filp->f_inode->i_ino, strlen(content), inode->i_size, content);
  return (int)networkfs_api_write(inode->i_sb->s_fs_info, inode->i_ino,
                                  content);
}
int networkfs_file_flush(struct file *filp, fl_owner_t id) {
  return networkfs_file_flush_impl(filp);
}

int networkfs_file_fsync(struct file *filp, loff_t begin, loff_t end,
                         int datasync) {
  return networkfs_file_flush_impl(filp);
}

int networkfs_file_release(struct inode *inode, struct file *filp) {
  // From the documentation of void kfree (const void *objp);
  // ```If objp is NULL, no operation is performed.```
  kfree(filp->private_data);
  return 0;
}

struct file_operations networkfs_file_ops = {.open = networkfs_file_open,
                                             .read = networkfs_file_read,
                                             .write = networkfs_file_write,
                                             .flush = networkfs_file_flush,
                                             .fsync = networkfs_file_fsync,
                                             .release = networkfs_file_release,
                                             .llseek = generic_file_llseek};

/*******************
 * Inode operations
 *******************/

#define NETWORKFS_DTOS(type) type == DT_DIR ? S_IFDIR : S_IFREG
#define NETWORKFS_STOD(type) type == S_IFDIR ? DT_DIR : DT_REG

struct inode *networkfs_get_inode(struct super_block *sb,
                                  const struct inode *parent, umode_t mode,
                                  ino_t i_ino);

struct dentry *networkfs_lookup(struct inode *parent, struct dentry *child,
                                unsigned int flag) {
  const char *name = child->d_name.name;
  if (strlen(name) >= NETWORKFS_API_ENTRY_MAX_NAME_LEN) {
    return ERR_PTR(-1);
  }

  struct entry_info *entry_info =
      networkfs_api_lookup(parent->i_sb->s_fs_info, parent->i_ino, name);

  if (entry_info != NULL) {
    struct inode *inode = networkfs_get_inode(
        parent->i_sb, parent, NETWORKFS_DTOS(entry_info->entry_type),
        entry_info->ino);
    d_add(child, inode);
    kfree(entry_info);
  }

  return NULL;
}

static int networkfs_create_impl(struct inode *parent, struct dentry *child,
                                 int type) {
  const char *name = child->d_name.name;
  ino_t created_ino;
  int64_t code = networkfs_api_create(parent->i_sb->s_fs_info, parent->i_ino,
                                      name, NETWORKFS_STOD(type), &created_ino);
  if (code == 0) {
    struct inode *inode =
        networkfs_get_inode(parent->i_sb, parent, type, created_ino);
    d_add(child, inode);
  }
  return (int)code;
}

int networkfs_remove_impl(const struct inode *parent,
                          const struct dentry *child, int type) {
  const char *name = child->d_name.name;
  return (int)networkfs_api_remove(parent->i_sb->s_fs_info, parent->i_ino, name,
                                   NETWORKFS_STOD(type));
}

int networkfs_create(struct user_namespace *user_ns, struct inode *parent,
                     struct dentry *child, umode_t mode, bool b) {
  return networkfs_create_impl(parent, child, S_IFREG);
}

int networkfs_unlink(struct inode *parent, struct dentry *child) {
  return networkfs_remove_impl(parent, child, S_IFREG);
}

int networkfs_mkdir(struct user_namespace *user_ns, struct inode *parent,
                    struct dentry *child, umode_t mode) {
  return networkfs_create_impl(parent, child, S_IFDIR);
}

int networkfs_rmdir(struct inode *parent, struct dentry *child) {
  return networkfs_remove_impl(parent, child, S_IFDIR);
}

int networkfs_link(struct dentry *target, struct inode *parent,
                   struct dentry *child) {
  const char *name = child->d_name.name;
  struct inode *inode = target->d_inode;

  int64_t code = networkfs_api_link(parent->i_sb->s_fs_info, inode->i_ino,
                                    parent->i_ino, name);
  if (code == 0) {
    d_add(child, inode);
  }
  return (int)code;
}

int networkfs_setattr(struct user_namespace *user_ns, struct dentry *entry,
                      struct iattr *attr) {
  setattr_prepare(user_ns, entry, attr);
  if (attr->ia_valid & ATTR_OPEN) {
    entry->d_inode->i_size = attr->ia_size;
  }
  return 0;
}

struct inode_operations networkfs_inode_ops = {.lookup = networkfs_lookup,
                                               .create = networkfs_create,
                                               .unlink = networkfs_unlink,
                                               .mkdir = networkfs_mkdir,
                                               .rmdir = networkfs_rmdir,
                                               .link = networkfs_link,
                                               .setattr = networkfs_setattr};

/*****************
 * Initialization
 *****************/

struct inode *networkfs_get_inode(struct super_block *sb,
                                  const struct inode *parent, umode_t mode,
                                  ino_t i_ino) {
  struct inode *inode = new_inode(sb);
  if (inode != NULL) {
    inode->i_ino = i_ino;
    inode->i_op = &networkfs_inode_ops;
    inode->i_fop = mode == S_IFDIR ? &networkfs_dir_ops : &networkfs_file_ops;
    inode_init_owner(&init_user_ns, inode, parent, mode | S_IRWXUGO);
  }

  return inode;
}

int networkfs_fill_super(struct super_block *sb, struct fs_context *fc) {
  printk(KERN_INFO "networkfs: Initializing superblock (token=%s)", fc->source);

  // Create root inode
  struct inode *inode =
      networkfs_get_inode(sb, NULL, S_IFDIR, NETWORKFS_ROOT_INO);

  // Create file system root
  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) return -ENOMEM;

  // Fill user access token
  if ((sb->s_fs_info = kmalloc(strlen(fc->source), GFP_KERNEL)) == NULL)
    return -ENOMEM;
  strcpy(sb->s_fs_info, fc->source);

  // Fill file max length field
  sb->s_maxbytes = NETWORKFS_API_ENTRY_MAX_FILE;

  return 0;
}

int networkfs_get_tree(struct fs_context *fc) {
  int ret = get_tree_nodev(fc, networkfs_fill_super);
  if (ret != 0) {
    printk(KERN_ERR "networkfs: Unable to mount: error code %d", ret);
  }

  return ret;
}

struct fs_context_operations networkfs_context_ops = {.get_tree =
                                                          networkfs_get_tree};

int networkfs_init_fs_context(struct fs_context *fc) {
  fc->ops = &networkfs_context_ops;
  return 0;
}

void networkfs_kill_sb(struct super_block *sb) {
  // Release user access token
  printk(KERN_INFO "networkfs: Superblock (token=%s) is destroyed",
         (char *)sb->s_fs_info);
  kfree(sb->s_fs_info);
}

struct file_system_type networkfs_fs_type = {
    .name = "networkfs",
    .init_fs_context = &networkfs_init_fs_context,
    .kill_sb = networkfs_kill_sb};

/*******************
 * Public Interface
 *******************/

int networkfs_init(void) {
  printk(KERN_INFO "networkfs: Initializing networkfs module\n");

  int ret = register_filesystem(&networkfs_fs_type);
  if (ret != 0) {
    printk(KERN_ERR
           "networkfs: Error during 'register_filesystem': error code %d",
           ret);
    return ret;
  }
  return ret;
}

void networkfs_exit(void) {
  printk(KERN_INFO "networkfs: Exiting networkfs module\n");

  int ret = unregister_filesystem(&networkfs_fs_type);
  if (ret != 0)
    printk(KERN_ERR
           "networkfs: Error during 'unregister_filesystem': error code %d",
           ret);
}
