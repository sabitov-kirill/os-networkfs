#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "networkfs-impl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sabitov Kirill");
MODULE_VERSION("0.01");

int networkfs_module_init(void) { return networkfs_init(); }

void networkfs_module_exit(void) { networkfs_exit(); }

module_init(networkfs_module_init);
module_exit(networkfs_module_exit);
