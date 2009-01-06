#include<linux/fs.h>
#include "testfs.h"

const struct inode_operations testfs_symlink_inode_operations = {
	.readlink = generic_readlink,
};
