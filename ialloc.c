/***********************************************************/
/*  This is the readme for the testfs filesystem           */
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#include<linux/vfs.h>
#include<linux/sched.h> /* For using "current" variable */
#include<linux/fs.h>
#include<linux/buffer_head.h>
#include "testfs.h"
#define TESTFS_INODE_BM_BLOCK 2 /* Inode bitmap block number */

/*
 * In memory states of testfs inode
 */
#define TESTFS_INODE_FREE 0x1
#define TESTFS_INODE_ALLOCATED 0x2

/*
 * Returns the block number which holds the
 * inode bitmap. Currently fixed
 */
static inline int get_testfs_inode_bitmap(void)
{
	return le32_to_cpu(TESTFS_INODE_BM_BLOCK);
}

/*
 * Read the buffer head corresponding to the inode bitmap
 */
static struct buffer_head *
read_inode_bitmap(struct super_block *sb)
{
	struct testfs_sb_info *sbi = TESTFS_SB(sb);
	if(!sbi->inode_bitmap) {
		sbi->inode_bitmap = sb_bread(sb, get_testfs_inode_bitmap());
	}
	BUG_ON(!sbi->inode_bitmap);
	return sbi->inode_bitmap;
}

/*
 * Check if the bit for a corresponding inode
 * is already marked as zero in bitmap. Returns 
 * true if already 0. False if 1
 */
static int inode_already_freed(char *bitmap, unsigned int ino)
{
	int shift = (ino)/8;
	int offset = (ino)%8;
	testfs_debug("Mask for inode(%u) = 0x%x\n", ino, *(bitmap+shift));
	return !((1<<offset)&(*(bitmap+shift)));
}

/*
 * Update the free bit in inode bitmap
 */
void testfs_set_inode_bit(char *bitmap, unsigned int ino)
{
	int shift = (ino)/8;
	int offset = (ino)%8;
	*(bitmap+shift) = ((1<<offset)|(*(bitmap+shift)));
	testfs_debug("New mask for inode(%u) = 0x%x\n", ino, *(bitmap+shift));
	return;
}

/*
 * Update the free bit in inode bitmap
 */
static void testfs_clear_inode_bit(char *bitmap, unsigned int ino)
{
	int shift = (ino)/8;
	int offset = (ino)%8;
	testfs_debug("Mask for inode(%u) = 0x%x\n", ino, *(bitmap+shift));
	*(bitmap+shift) = ((~(1<<offset))&(*(bitmap+shift)));
	return;
}

/*
 * Go and update the various counters related to inodes
 */
static void testfs_release_inode(struct super_block *sb)
{
	struct testfs_sb_info *tsb = TESTFS_SB(sb);
	tsb->s_free_inodes++;
	BUG_ON(tsb->s_free_inodes > tsb->s_max_inodes);
	sb->s_dirt = 1;
	return;
}

/*
 * Free an inode from the filesystem
 */
void testfs_free_inode(struct inode *inode)
{
	struct buffer_head *bitmap_bh = NULL;
	struct super_block *sb = inode->i_sb;
	struct testfs_super_block *tsb = TESTFS_SB(sb)->s_ts;
	unsigned int ino = inode->i_ino;

	BUG_ON(!tsb);
	testfs_debug("Freeing inode %u\n",ino);
	if (ino <= tsb->s_first_nonmeta_inode || ino >= tsb->s_max_inodes) {
		testfs_error("Invalid inode number to be freed %u\n",ino);
		goto error_return;
	}
	clear_inode(inode);
	bitmap_bh = read_inode_bitmap(sb);
	if (inode_already_freed(bitmap_bh->b_data, ino)) {
		testfs_error("Inode already free %u\n",ino);
		goto error_return;
	}
	testfs_clear_inode_bit(bitmap_bh->b_data, ino);
	testfs_release_inode(sb);
	mark_buffer_dirty(bitmap_bh);
error_return:
	return;
}

static unsigned int testfs_find_free_inode(unsigned char *bitmap, struct super_block *sb)
{
	struct testfs_sb_info *tsbi = TESTFS_SB(sb);
	unsigned char *end = bitmap + sb->s_blocksize;
	unsigned int ino = 0;
	unsigned char *start = bitmap + (tsbi->s_first_nonmeta_inode)/8;
	/*
	 * A quick and dirty way to find the free inode
	 * in filesystem. Just loop over all the inodes
	 */
	while (start <= end) {
		int i;
		if (*start == 0xff) {
		       start++;
		       continue;	       
		}
		for(i=0;i<8;i++)
			if (!((*start)&(1<<i))) {
				ino = ((int)(start-bitmap))*8 + i;
				return ino;
			}
	
	}
	return ino;
}

struct inode *testfs_new_inode(struct inode *dir, int mode)
{
	struct super_block *sb = dir->i_sb;
	struct testfs_sb_info *tsbi = TESTFS_SB(sb);
	struct testfs_inode_info *tsi;
	struct buffer_head *bitmap_bh = NULL;
	struct inode *inode;
	unsigned int ino = 0;

	inode = new_inode(sb);
	if(!inode)
	{
		testfs_debug("Could not allocate inode from inode cache\n");
		return ERR_PTR(-ENOMEM);
	}
	tsi = TESTFS_I(inode);

	bitmap_bh = read_inode_bitmap(sb);
	ino = testfs_find_free_inode(bitmap_bh->b_data, sb);
	if(!ino)
	{
		testfs_debug("Could not find any free inode. File system full\n");
		return ERR_PTR(-ENOSPC);
	}
	testfs_debug("Allocated new inode (%u)\n",ino);
	testfs_set_inode_bit(bitmap_bh->b_data, ino);
	inode->i_ino = ino;
	inode->i_mode = mode;
	inode->i_gid = current->fsgid;
	inode->i_uid = current->fsuid;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;

	testfs_debug("Successfully allocated inodes....\n");
	memset(tsi->i_data, 0 ,sizeof(tsi->i_data));
	tsi->i_data[0] = ino;
	tsi->state = TESTFS_INODE_ALLOCATED;
	tsbi->s_free_inodes--;
	sb->s_dirt = 1;
	insert_inode_hash(inode);
	mark_inode_dirty(inode);
	mark_buffer_dirty(bitmap_bh);
	sync_dirty_buffer(bitmap_bh);
	testfs_debug("returning now\n");
	return inode;
}
