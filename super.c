/***********************************************************/
/*  This is the readme for the testfs filesystem           */
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#include<linux/module.h>
#include<linux/buffer_head.h>
#include<linux/vfs.h>
#include<linux/mount.h>
#include "testfs.h"

#define TESTFS_DFLT_BLOCKSIZE 4096
#define TESTFS_SUPERBLOCK 1 /* Default blocknumber for superblock */
static struct kmem_cache *testfs_inode_cachep; /* Testfs inode cache pointer */

/*
 * Allocate an inode from the cache
 */
static struct inode *testfs_alloc_inode(struct super_block *sb)
{
	struct testfs_inode_info *tsi;
	tsi = (struct testfs_inode_info *)kmem_cache_alloc(testfs_inode_cachep, GFP_KERNEL);
	if (!tsi)
		return NULL;
	tsi->vfs_inode.i_version = 1;
	return &tsi->vfs_inode;
}

static void testfs_commit_super(struct super_block *sb, struct testfs_super_block *ts)
{
	mark_buffer_dirty(TESTFS_SB(sb)->s_bh);
	sb->s_dirt = 0;
}
static void testfs_sync_super(struct super_block *sb, struct testfs_super_block *ts)
{
	mark_buffer_dirty(TESTFS_SB(sb)->s_bh);
	sync_dirty_buffer(TESTFS_SB(sb)->s_bh);
	sb->s_dirt = 0;
}

/*
 * Free the allocated structures for testfs superblock
 */
static void testfs_put_super(struct super_block *sb)
{
	struct testfs_sb_info *tsi = TESTFS_SB(sb);
	struct testfs_super_block *ts = tsi->s_ts;
	testfs_sync_super(sb, ts);
	brelse(tsi->inode_bitmap);
	brelse(tsi->s_bh);
	sb->s_fs_info = NULL;
	kfree(tsi);
}

void testfs_write_super(struct super_block *sb)
{
	struct testfs_sb_info *tsi = TESTFS_SB(sb);
	struct testfs_super_block *ts = tsi->s_ts;
	lock_kernel();
	testfs_sync_super(sb, ts);
	sb->s_dirt = 0;
	unlock_kernel();
}
/*
 * Free an inode in inode cache
 */
static void testfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(testfs_inode_cachep, TESTFS_I(inode));
	return;
}
static const struct super_operations testfs_sops = {
	.alloc_inode   = testfs_alloc_inode,
	.write_inode   = testfs_write_inode,
	.delete_inode  = testfs_delete_inode,
	.destroy_inode = testfs_destroy_inode,
	.put_super     = testfs_put_super,
	.write_super   = testfs_write_super,
};
static int testfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct testfs_sb_info *tsi;
	struct testfs_super_block *ts;
	struct inode *root;
	struct buffer_head *bh = NULL;
	unsigned int blocksize ;
	tsi = kzalloc(sizeof(*tsi), GFP_KERNEL);
	if(!tsi)
		return -ENOMEM;
	sb->s_fs_info = tsi;

	/* Read the superblock */
	blocksize = sb_min_blocksize(sb, TESTFS_DFLT_BLOCKSIZE);
	testfs_debug("Got blocksize as %u\n", blocksize);
	bh = sb_bread(sb, TESTFS_SUPERBLOCK);
	if(!bh) {
		printk("Unable to read superblock\n");
		goto fail;
	}

	/* Fill in memory data structures */
	ts = (struct testfs_super_block *)((char *)bh->b_data);
	tsi->s_ts = ts;
	tsi->s_bh = bh;
	tsi->s_free_inodes = ts->s_free_inodes;
	tsi->s_max_inodes = ts->s_max_inodes;
	tsi->s_first_nonmeta_inode = ts->s_first_nonmeta_inode;
	sb->s_magic = le32_to_cpu(ts->s_magic);
	testfs_debug("Read magic number as 0x%x\n", (unsigned int)sb->s_magic);
	if(sb->s_magic != le32_to_cpu(TESTFS_MAGIC))
		goto bad_magic;

	/*
	 * Setup other usefule fields of superblock
	 */
	sb->s_op = &testfs_sops;
	root = testfs_iget(sb, TESTFS_ROOT_INODE(ts));
	if (IS_ERR(root) || !root) {
		testfs_debug("Unable to read root inode\n");
		goto fail1;
	}
	if (!S_ISDIR(root->i_mode)) {
		iput(root);
		testfs_debug("Unable to read root inode\n");
		goto fail1;
	}
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		iput(root);
		testfs_debug("Unable to read root inode\n");
		goto fail1;
	}
	return 0;
bad_magic:
	printk("Can't find a valid \"Testfs\" Filesystem on device\n");
fail1:
	brelse(bh);
fail:
	testfs_debug("Something bad happened. Unable to mount\n");
	sb->s_fs_info = NULL;
	kfree(tsi);
	return -EINVAL;
}

static int testfs_get_sb(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, testfs_fill_super, mnt);
}

/*
 * Definition of testfs filesystem type
 */
static struct file_system_type testfs_type = {
	.owner = THIS_MODULE,
	.name  = "testfs",
	.get_sb = testfs_get_sb,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static void init_once(void *object)
{
	struct testfs_inode_info * tsi = (struct testfs_inode_info *)object;
	inode_init_once(&tsi->vfs_inode);
	return;
}

static int init_inodecache(void)
{
	testfs_inode_cachep = kmem_cache_create("testfs_inode_cache",
						sizeof(struct testfs_inode_info),
						0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD),
						init_once);
	if(!testfs_inode_cachep)
		return -ENOMEM;
	return 0;

}

static void destroy_inode_cache(void)
{
	kmem_cache_destroy(testfs_inode_cachep);
}
static int __init init_testfs(void)
{
	int err = 0;
	err = init_inodecache(); 
	err += register_filesystem(&testfs_type);
	testfs_debug("Registering testfs\n");
	return err;
}

static void __exit exit_testfs(void)
{
	testfs_debug("Unregistering testfs\n");
	destroy_inode_cache();
	unregister_filesystem(&testfs_type);
	return;
}

module_init(init_testfs);
module_exit(exit_testfs);
MODULE_AUTHOR("Manish Katiyar <mkatiyar@gmail.com>");
MODULE_DESCRIPTION("Simple test filesystem for fun");
MODULE_LICENSE("GPL");
