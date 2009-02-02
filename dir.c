/***********************************************************/
/*  This is the readme for the testfs filesystem           */
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#include<linux/fs.h>
#include<linux/buffer_head.h>
#include<linux/pagemap.h>
#include<linux/swap.h>
#include "testfs.h"


int testfs_permission(struct inode *inode, int mask)
{
	return generic_permission(inode, mask, NULL);
}

static inline void testfs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}




static inline int testfs_inode_pages(struct inode *inode)
{
	return (inode->i_size + PAGE_CACHE_SIZE - 1)/PAGE_CACHE_SIZE;
}

static struct page *testfs_get_page(struct inode *dir, int n)
{
	struct address_space *as = dir->i_mapping;
	struct page *page = read_mapping_page(as, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
	}
	return page;
}

/*
 * Returns the last valid offset for a given page, given
 * the inode size
 */
static unsigned testfs_last_byte_for_page(struct inode *inode,
		int pagenum)
{
	unsigned last_byte = inode->i_size;
	last_byte -= pagenum << PAGE_CACHE_SHIFT;
	return (last_byte > PAGE_CACHE_SIZE ? PAGE_CACHE_SIZE : last_byte);
}

/*
 * Compares if directory entry
 * equals the supplies name
 */
static inline int testfs_match(int len, const char *name , struct testfs_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

void testfs_set_inode_type(struct testfs_dir_entry *dentry, struct inode *inode)
{
	dentry->file_type = 0;
	if(S_ISDIR(inode->i_mode))
		dentry->file_type = S_IFDIR;
	else if(S_ISREG(inode->i_mode))
		dentry->file_type = S_IFREG;
	return;
}

/*
 * commit the changes made to a page on disk
 */
static int testfs_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct inode *dir = mapping->host;
	int err = 0;

	block_write_end(NULL, mapping, pos, len, len, page, NULL);

	if(pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}
	//unlock_page(page);
	err = write_one_page(page,1);
	if (!err)
	err = testfs_write_inode(dir,1);
	return err;
}

/*
 * Add a new dirent entry in the directory
 */
int testfs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct page *page = NULL;
	unsigned long npages = testfs_inode_pages(dir);
	loff_t pos;
	struct testfs_dir_entry *de;
	__u32 reclen = calc_reclen_from_len(dentry->d_name.len);
	char *kaddr;
	int i, err=0;
	int namelen = dentry->d_name.len;
	int rec_len, name_len;
	const char *name = dentry->d_name.name;

	/*
	 * Since we allow a file to grow only one blocksize max, assert here
	 * Relax the assert if page size and blocksize are not same.
	 */
	BUG_ON(!(npages <= 1 || npages <= (dir->i_sb->s_blocksize/PAGE_CACHE_SIZE)));

	/*
	 * Now traverse through all the directory pages to see where we have
	 * got a place for free entry to put
	 */
	for (i=0; i<=npages; i++) {
		char *dir_end;
		page = testfs_get_page(dir, i);
		if (IS_ERR(page))
			goto out;

		/* Lock the page and get its address */
		lock_page(page);
		kaddr = page_address(page);
		dir_end = kaddr + testfs_last_byte_for_page(dir, i);
		de = (struct testfs_dir_entry *)kaddr;

		/*
		 * Max address that we should look for as directory
		 * entries don't span over pages.
		 */
		kaddr += PAGE_CACHE_SIZE - reclen;
		while ((char * )de <= kaddr) {
			if ((char *)de == dir_end) {
				/*
				 * reached end of i_size
				 */
				name_len = 0;
				rec_len = dir->i_sb->s_blocksize;
				de->inode = 0;
				de->rec_len = rec_len;
				goto gotit;
			}
			BUG_ON(de->rec_len == 0);
			testfs_debug("Got \"%s\" : %u reclen = %d\n",de->name, de->inode, de->rec_len);
			err = -EEXIST;
			if (testfs_match(namelen, name, de))
				/* Entry already exists */
				goto page_unlock;

			rec_len = de->rec_len;
			/* This can be different from above if this is the last entry */
			name_len = calc_reclen_from_len(de->name_len);

			if(!de->inode && rec_len >= reclen) {
				/* This means this is an unused entry and has
				 * sufficient space to hold us
				 */
				goto gotit;
			}

			if(rec_len >= name_len + reclen) {
				/* We can accomodate this entry
				*/
				goto gotit;
			}	
			de = (struct testfs_dir_entry *)((char *)de+rec_len);
		}
		unlock_page(page);
		testfs_put_page(page);
	}
	BUG();
	return -EINVAL;
gotit:
	pos = (page->index >> PAGE_CACHE_SHIFT) + (char *)de - (char *)page_address(page);
	err = __testfs_write_begin(NULL, page->mapping, pos, rec_len, 0, &page, NULL);
	if (err)
		goto page_unlock;
	if (de->inode) {
		/* We are at the last entry, shift appropriately */
		struct testfs_dir_entry *de1 = (struct testfs_dir_entry *)((char *)de + name_len);
		de1->rec_len = cpu_to_le32(rec_len - name_len);
		de->rec_len = cpu_to_le32(name_len);
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	de->inode = cpu_to_le32(inode->i_ino);
	testfs_set_inode_type(de, inode);
	testfs_debug("Creating new entry (ino = %u) (name_len = %d) (rec_len = %d) of type %d\n",de->inode, de->name_len, de->rec_len, de->file_type);
	err = testfs_commit_chunk(page, pos, rec_len);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
page_put:
	testfs_put_page(page);
out:
	return err;
page_unlock:
	unlock_page(page);
	goto page_put;
}

static int testfs_readdir(struct file *filep, void *dirent, filldir_t filldir)
{
	loff_t pos = filep->f_pos;
	struct inode *inode = filep->f_path.dentry->d_inode;
	unsigned int offset = pos & ~PAGE_CACHE_MASK;
	unsigned long pages = testfs_inode_pages(inode); /* Total number of pages to iterate */
	unsigned n = pos >> PAGE_CACHE_SHIFT; /* Page from where we have to start iterating */
	if (pos > inode->i_size) /* We have crossed our inode size */
		return 0;

	testfs_debug("readdir called at pos %llu and offset %u\n", pos, offset);
	testfs_debug("n = %d, pages = %d\n",n , pages);
	/* Start looking at the entries */
	for(;n < pages; n++, offset = 0) {
		char *kaddr, *limit;
		struct testfs_dir_entry *de;
		struct page *page = testfs_get_page(inode, n);
		if (IS_ERR(page)) {
			testfs_error("Bad page (%u) found in inode %lu\n",n ,inode->i_ino);
			filep->f_pos += PAGE_CACHE_SIZE - offset; /* Increment the pointer */
			return PTR_ERR(page);
		}

		/* Now start looking for something useful in the page */
		kaddr = page_address(page);
		de = (struct testfs_dir_entry *)(kaddr + offset);

		/*
		 * Get the limit to traverse. subtrace calc_reclen_from_len because that is the max
		 * that we can have at last, else we will satisfy the !de.rec_len condition because
		 * of the check de<=limit in for loop.
		 */
		limit = kaddr + testfs_last_byte_for_page(inode, n) - calc_reclen_from_len(1);
		for (; (char *)de <= limit; de = (struct testfs_dir_entry *)((char *)de + de->rec_len)) {
			if (!de->rec_len) {
				testfs_error("Zero length rec_len for inode (%d) \"%s\"\n", de->inode, de->name);
				testfs_put_page(page);
				return -EIO;
			}
			if (de->inode) {
				int over;
				offset = (char *)de - kaddr;
				testfs_debug("Got entry \"%s\"\n",de->name);
				over = filldir(dirent, de->name, de->name_len, (n<<PAGE_CACHE_SHIFT)|offset,
						de->inode, de->file_type);
				if(over) {
					testfs_put_page(page);
					return 0;
				}
			}
			filep->f_pos += de->rec_len;
		}
		testfs_put_page(page);
	}
	return 0;
}

struct testfs_dir_entry *testfs_find_dentry(struct inode *dir,
		struct qstr *child, struct page **respage)
{
	struct testfs_dir_entry *de = NULL;
	int n, pages = testfs_inode_pages(dir);
	struct page *page;
	char *kaddr = NULL, *limit;

	testfs_debug("Trying to find \"%s\" in dir ino (%u)\n",child->name, dir->i_ino);
	for (n=0;n<=pages;n++) {
		page = testfs_get_page(dir, n);
		if (IS_ERR(page)) {
			testfs_error("Error reading page# (%d) of inode %lu\n", n, dir->i_ino);
			goto out;
		}
		kaddr = page_address(page);
		limit = kaddr + testfs_last_byte_for_page(dir, n);
		de = (struct testfs_dir_entry *)kaddr;
		for (;(char *)de <= limit ; de = (struct testfs_dir_entry *)((char *)de + de->rec_len)) {
			if (!de->rec_len) {
				testfs_error("Zero length rec_len for inode (%d) \"%s\"\n", de->inode, de->name);
				testfs_put_page(page);
				goto out;
			}
			if (testfs_match(child->len, child->name, de)) {
				*respage = page;
				return de;
			}
		}
	}
out:
	return NULL;
}

/*
 * Lookup a file by its name in a directory
 * and return the corresponding inode
 */
unsigned int testfs_inode_by_name(struct inode *dir, struct qstr *child)
{
	unsigned ino = 0;
	struct testfs_dir_entry *dentry;
	struct page *page;
	dentry = testfs_find_dentry(dir, child, &page);
	if (dentry && !(IS_ERR(dentry))) {
		ino = le32_to_cpu(dentry->inode);
		testfs_put_page(page);
	}
	return ino;
}

int testfs_delete_entry (struct testfs_dir_entry *dir, struct page *page)
{
	struct testfs_dir_entry *old, *cur;
	char *kaddr = page_address(page);
	int err = -ENOENT;
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	int pos, from = (((char *)dir) - kaddr) & ~(inode->i_sb->s_blocksize - 1);
	int to = (((char *)dir) - kaddr) + calc_reclen_from_len(dir->name_len);
	old = cur= (struct testfs_dir_entry *)kaddr;
	testfs_debug("here\n");
	for (;cur < dir;cur= (struct testfs_dir_entry *)((char *)cur+ cur->rec_len)) {
		testfs_debug("ino (%lu - %s)\n",cur->inode, cur->name);
		old = cur;
	}
	if ((char *)cur!= kaddr) {
		/* This is not the first entry on page */
		old->rec_len += cur->rec_len;
		cur->inode = 0;
	}
	pos = page_offset(page) + from;
	lock_page(page);
	err = __testfs_write_begin(NULL, mapping, pos, to - from, 0,
						&page, NULL);

	err = testfs_commit_chunk(page, pos, to - from);
	mark_inode_dirty(inode);
	testfs_put_page(page);
	return err;
}

const struct file_operations testfs_dir_operations = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.readdir = testfs_readdir,
};
