/**********************************************************/
/*  This is the readme for the testfs filesystem           */
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#include<linux/buffer_head.h>
#include<linux/fs.h>
#include<linux/mpage.h>
#include "testfs.h"

static struct testfs_inode *testfs_get_inode(struct super_block *sb, unsigned int ino,
				struct buffer_head **bhp);

typedef struct {
	__le32	*p;
	__le32	key;
	struct buffer_head *bh;
} Indirect;

static inline void add_chain(Indirect *p, struct buffer_head *bh, __le32 *v)
{
	p->key = *(p->p = v);
	p->bh = bh;
}

static inline int verify_chain(Indirect *from, Indirect *to)
{
	while (from <= to && from->key == *from->p)
		from++;
	return (from > to);
}

static int testfs_block_to_path(struct inode *inode, long block, int offsets[1])
{
	int n = 0;
	if(block < 0) {
		testfs_debug("Block number less than zero (%ld)\n",block);
	}
	BUG_ON(block != 0);
	offsets[n++] = block;
	return n;
}

static inline Indirect *testfs_get_branch(struct inode *inode,
					int depth, int *offsets,
					Indirect chain[1],
					int *err)
{
	Indirect *p = chain;
	*err = 0;

	add_chain(chain, NULL, TESTFS_I(inode)->i_data + *offsets);
	testfs_debug("Added the block number %d\n",*(TESTFS_I(inode)->i_data + *offsets));
	if (!p->key)
		goto no_block;
	while (--depth) {
		/*
		 * We should not be reaching here since we have 
		 * only one block number in the inode data itself
		 * so we don't need to load any other
		 */
		testfs_debug("We have entered a red zone\n");
		BUG();
	}
	return NULL;
no_block:
	return p;
}

static int testfs_get_blocks(struct inode *inode, sector_t block,
			unsigned long maxblocks, struct buffer_head *bh,
			int create)
{
	int err = -EIO;
	int offsets[1]; /* Our chain length can be max 1 */
	Indirect chain[1];
	Indirect *partial;
	int depth = 0;

	depth = testfs_block_to_path(inode, block, offsets);
	if (!depth)
		return err;

	partial = testfs_get_branch(inode, depth, offsets, chain, &err);

	/* Block found */
	if (!partial) {
		map_bh(bh, inode->i_sb, chain[depth -1].key);
		partial = chain+depth-1;
		goto cleanup;
	}
cleanup:
	while (partial > chain) {
		brelse(partial->bh);
		partial --;
	}
	return err;
}

int testfs_get_block(struct inode *inode, sector_t block, struct buffer_head *bh, int create)
{
	unsigned maxblocks = bh->b_size / inode->i_sb->s_blocksize;
	int ret = testfs_get_blocks(inode, block, maxblocks, bh, create);
	if(ret > 0) {
		bh->b_size = (ret * 4096);
	}
	return ret;
}

static int testfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, testfs_get_block);
}

static int testfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, testfs_get_block);
}

/*
 * Get a new inode and fill it appropriately
 */
struct inode *testfs_iget(struct super_block *sb, unsigned int ino)
{
	struct testfs_inode_info *tsi;
	struct testfs_inode *raw_inode;
	struct buffer_head *bh;
	struct inode *inode;

	testfs_debug("Trying to get in core inode for inode (%u)\n",ino);
	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	tsi = TESTFS_I(inode);

	/* Get the raw disk inode and fill the buffer head */
	raw_inode = testfs_get_inode(inode->i_sb, ino, &bh);
	if (!raw_inode) {
		return NULL;
	}

	inode->i_mode = le32_to_cpu(raw_inode->type);
	inode->i_uid = le32_to_cpu(raw_inode->uid);
	inode->i_gid = le32_to_cpu(raw_inode->gid);
	inode->i_size = le32_to_cpu(raw_inode->size);
	inode->i_nlink = le32_to_cpu(raw_inode->nlinks);
	inode->i_atime= (raw_inode->atime);
	inode->i_ctime= (raw_inode->ctime);
	inode->i_mtime= (raw_inode->mtime);

	/*
	inode->i_atime.tv_sec = le32_to_cpu(raw_inode->atime.tv_sec);
	inode->i_ctime.tv_sec = le32_to_cpu(raw_inode->ctime.tv_sec);
	inode->i_mtime.tv_sec = le32_to_cpu(raw_inode->mtime.tv_sec);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	*/

	tsi->i_data[0] = raw_inode->data[0];
	/*
	 * Setup the proper operation routines depending
	 * on the file type.
	 */
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &testfs_file_inode_operations;
		inode->i_fop = &testfs_file_operations;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &testfs_dir_inode_operations;
		inode->i_fop = &testfs_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &testfs_symlink_inode_operations;
	}

	inode->i_mapping->a_ops = &testfs_aops;

	brelse(bh);
	unlock_new_inode(inode);

	return inode;
}

static struct testfs_inode *testfs_get_inode(struct super_block *sb, unsigned int ino,
				struct buffer_head **bhp)
{
	struct buffer_head *bh;
	unsigned int offset;
	unsigned int block;
	unsigned int inodes_per_block;
	struct testfs_super_block *ts = TESTFS_SB(sb)->s_ts;
	*bhp = NULL;

	inodes_per_block = sb->s_blocksize/(sizeof(struct testfs_inode));
	block = (ino - ts->s_first_nonmeta_inode)/inodes_per_block;
	BUG_ON(block > 2 || block < 0);
	/* This offset is within a particular inode block */
	offset = (ino - ts->s_first_nonmeta_inode)*sizeof(struct testfs_inode);

	bh = sb_bread(sb, block + 3); /* Inode blocks are 3,4 & 5 */
	if (!bh) {
		testfs_debug("Unable to read inode block (%d)\n", block+3);
		return NULL;
	}
	*bhp = bh;
	return (struct testfs_inode *)(bh->b_data + offset);
}

void testfs_truncate(struct inode *inode)
{
	testfs_debug("In testfs_truncate\n");
	return;
}

int testfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	testfs_debug("In testfs_setattr\n");
	return 0;
}

int __testfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags, struct page **pagep,
		void **fsdata)
{
	return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata, testfs_get_block);
}

int testfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags, struct page **pagep,
		void **fsdata)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	*pagep = NULL;
	testfs_debug("filesize = %lld, pos = %lld, len = %u\n",inode->i_size, pos, len);
	return __testfs_write_begin(file, mapping, pos, len, flags, pagep, fsdata);
}

static int testfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, testfs_get_block, wbc);
}

/* sync the ondisk inode with the in memory one */
static int testfs_update_inode(struct inode *inode)
{
	struct testfs_inode_info *tsi = TESTFS_I(inode);
	struct super_block *sb = inode->i_sb;
	unsigned int ino = inode->i_ino;
	struct buffer_head *bh;
	struct testfs_inode *raw = testfs_get_inode(sb, ino, &bh);
	int err = 0;

	if (!raw || IS_ERR(raw))
		return -EIO;

	/* Update the fields of on disk inode with those from memory */
	testfs_debug("Inode (%lu) size = %lld , mode = 0x%x\n",inode->i_ino, inode->i_size, inode->i_mode);
	raw->size = cpu_to_le32(inode->i_size);
	raw->atime = (inode->i_atime);
	raw->mtime = (inode->i_mtime);
	raw->ctime = (inode->i_ctime);
	raw->nlinks = cpu_to_le32(inode->i_nlink);
	raw->gid = cpu_to_le32(inode->i_gid);
	raw->uid = cpu_to_le32(inode->i_uid);
	raw->type = cpu_to_le32(inode->i_mode);
	testfs_debug("Data block = %u\n",tsi->i_data[0]);
	raw->data[0] = tsi->i_data[0];

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	if (buffer_req(bh) && !buffer_uptodate(bh)) {
		testfs_error("I/O error while syncing inode to disk\n");
		err = -EIO;
	}
	brelse(bh);
	return err;
}

void testfs_delete_inode(struct inode *inode)
{
	testfs_debug("Deleting inode (%lu)\n",inode->i_ino);
	if(is_bad_inode(inode))
		goto no_delete;
	mark_inode_dirty(inode);
	testfs_update_inode(inode);
	inode->i_size = 0;
	testfs_free_inode(inode);
	return;
no_delete:
	clear_inode(inode);
}

int testfs_write_inode(struct inode *inode, int wait)
{
	return testfs_update_inode(inode);
}

const struct address_space_operations testfs_aops = {
	.readpage = testfs_readpage,
	.readpages = testfs_readpages,
	.writepage = testfs_writepage,
	.write_begin = testfs_write_begin,
	.write_end = generic_write_end,
	.sync_page = block_sync_page,
};
