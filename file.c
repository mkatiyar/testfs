/***********************************************************/
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#include<linux/fs.h>
#include "testfs.h"

const struct inode_operations testfs_file_inode_operations = {
	.truncate = testfs_truncate,
	.setattr  = testfs_setattr,
	.permission = testfs_permission,
};

const struct file_operations testfs_file_operations = {
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.open = generic_file_open,
};
