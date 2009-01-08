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

static int testfs_add_dentry(struct dentry *dentry, struct inode *inode)
{
	int err = testfs_add_link(dentry, inode);
	if (!err) {
		/*
		 * Attach the negative dentry with the inode
		 */
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static int testfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	struct inode *inode = testfs_new_inode(dir, mode);
	int err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {

		testfs_debug("creating new file \"%s\" with inode %lu\n",dentry->d_name.name, inode->i_ino);
		/*
		 * Initialize the new inode with proper
		 * function pointers
		 */
		inode->i_op = &testfs_file_inode_operations;
		inode->i_fop = &testfs_file_operations;
		inode->i_mapping->a_ops = &testfs_aops;

		/* Mark the inode dirty so that it gets written */
		mark_inode_dirty(inode);

		/* Add this inode to the directory */
		err = testfs_add_dentry(dentry, inode);
	}
	testfs_debug("I hope nothing is wrong here err = %d\n",err);
	return err;
}

static struct dentry *testfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = NULL;
	unsigned int ino;

	//testfs_debug("Looking up file \"%s\" in dir inode %lu\n",dentry->d_name.name, dir->i_ino);
	if(dentry->d_name.len > TESTFS_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = testfs_inode_by_name(dir, &dentry->d_name);

	if (ino) {
		inode = testfs_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}

	return d_splice_alias(inode, dentry);
}

const struct inode_operations testfs_dir_inode_operations = {
	.create = testfs_create,
	.lookup = testfs_lookup,
	//.link = testfs_link,
	//.unlink = testfs_unlink,
        //.mkdir = testfs_mkdir,
	//.rmdir = testfs_rmdir,
	//.rename = testfs_rename,
	  .setattr = testfs_setattr,
	  .permission = testfs_permission,
};
