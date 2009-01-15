/***********************************************************/
/*  This is the readme for the testfs filesystem           */
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#include<linux/fs.h>
#include "testfs.h"

const struct inode_operations testfs_symlink_inode_operations = {
	.readlink = generic_readlink,
};
