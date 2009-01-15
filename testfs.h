/***********************************************************/
/*  This is the readme for the testfs filesystem           */
/*  Author : Manish Katiyar <mkatiyar@gmail.com>           */
/*  Description : A simple disk based filesystem for linux */
/*  Date   : 08/01/09                                      */
/*  Version : 0.01                                         */
/*  Distributed under GPL                                  */
/***********************************************************/
#ifndef __TEST_FS__ 
#define __TEST_FS__ 

#ifdef __KERNEL__
#include<linux/types.h>
#include<linux/magic.h>
/*
 * In memory structure of testfs disk inode
 */
struct testfs_inode_info {
	struct inode vfs_inode;
	__u32 flags;
	__u32 state;
	__u32 i_data[1];
} ;
#else
#define __u32 unsigned int
#define __le16 unsigned short
#define __u8 unsigned char
#endif

/*
 * On disk inode structure of testfs
 */
struct testfs_inode {
	__u32 uid;
	__u32 gid;
	__u32 size;
	__u32 type;
	__u32 nlinks;
	struct timespec atime;
	struct timespec ctime;
	struct timespec mtime;
	__u32 data[1];
} ;

#ifdef __KERNEL__
/*
 * In memory superblock info of Testfs
 */
struct testfs_sb_info {
	struct testfs_super_block *s_ts;
	struct buffer_head *s_bh; /* buffer head for the superblock */
	struct buffer_head *inode_bitmap;
	__u32 s_free_inodes;
	__u32 s_max_inodes;
	__u32 s_first_nonmeta_inode;
} ;
#endif

struct testfs_super_block {
	__u32 s_magic;
	__u32 s_blocksize;
	__u32 s_first_nonmeta_inode;
	__u32 s_free_inodes;
	__u32 s_max_inodes;
} ;

/*
 * Shamelessly copied from ext2
 */
#define TESTFS_MAX_NAME_LEN 12
struct testfs_dir_entry {
	__u32 inode;
      	__u32 name_len;	
      	__u32 file_type;	
	__u32 rec_len;
	char name[TESTFS_MAX_NAME_LEN];
} ;

/*
 * Given a dirent, calculate its record length
 */
#define TESTFS_NAME_ROUND 3 /* 4 -1 */
static inline __u32 calc_rec_len(struct testfs_dir_entry *dirent)
{
	return (dirent->name_len + (sizeof(struct testfs_dir_entry) - TESTFS_MAX_NAME_LEN) +
			TESTFS_NAME_ROUND) & ~TESTFS_NAME_ROUND; 
}

/*
 * Given a dirent namelen, calculate its record length
 */
static inline __u32 calc_reclen_from_len(unsigned int name_len)
{
	return (name_len + (sizeof(struct testfs_dir_entry) - TESTFS_MAX_NAME_LEN) +
			TESTFS_NAME_ROUND) & ~TESTFS_NAME_ROUND; 
}
/*
 * File types for testfs
 */
enum {
	TESTFS_FT_UNKNOWN,
	TESTFS_FT_FILE,
	TESTFS_FT_DIR,
	TESTFS_FT_SYMLINK,
	TESTFS_FT_SOCKET,
	TESTFS_FT_CHRDEV,
	TESTFS_FT_BLKDEV,
	TESTFS_FT_PIPE,
	TESTFS_FT_MAX
};

#define TESTFS_MAGIC 0x4D4B4653 /* "MKFS" */
#define TESTFS_ROOT_INODE(sb) ((sb)->s_first_nonmeta_inode)

#define TESTFS_ISDIR(m)      ((m) & TESTFS_FT_DIR)
#define TESTFS_ISFILE(m)     ((m) & TESTFS_FT_FILE)
#define TESTFS_ISSYMLINK(m)  ((m) & TESTFS_FT_SYMLINK)
#define TESTFS_ISSOCKET(m)   ((m) & TESTFS_FT_SOCKET)
#define TESTFS_ISCHRDEV(m)   ((m) & TESTFS_FT_CHRDEV)
#define TESTFS_ISBLKDEV(m)   ((m) & TESTFS_FT_BLKDEV)
#define TESTFS_ISPIPE(m)     ((m) & TESTFS_FT_PIPE)
#ifdef TESTFS_DEBUG
#ifdef __KERNEL__
#define testfs_debug(f, a...) { \
	printk("TESTFS-Debug (%s, %d) : %s:", \
			__FILE__,__LINE__,__func__); \
	printk(f,##a); \
}
#else
#define testfs_debug(f, a...) { \
	fprintf(stderr, "TESTFS-Debug (%s, %d) : %s:", \
			__FILE__,__LINE__,__func__); \
	fprintf(stderr, f,##a); \
}
#endif
#else
#define testfs_debug(f, a...)  /* Do nothing */
#endif

#ifdef __KERNEL__
#define testfs_error(f, a...) { \
	printk(KERN_CRIT "TESTFS-Debug (%s, %d) : %s:", \
			__FILE__,__LINE__,__func__); \
	printk(f,##a); \
}
#else
#define testfs_error(f, a...) { \
	fprintf(stderr, "TESTFS-Debug (%s, %d) : %s:", \
			__FILE__,__LINE__,__func__); \
	fprintf(stderr, f,##a); \
}
#endif

#ifdef __KERNEL__
static inline struct testfs_sb_info *TESTFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct testfs_inode_info *TESTFS_I(struct inode *inode)
{
	return container_of(inode, struct testfs_inode_info, vfs_inode);
}

/* structure definitions */
extern const struct inode_operations testfs_file_inode_operations;
extern const struct inode_operations testfs_dir_inode_operations;
extern const struct inode_operations testfs_symlink_inode_operations;
extern const struct file_operations testfs_file_operations;
extern const struct file_operations testfs_dir_operations;
extern const struct address_space_operations testfs_aops;

/*
 * Function definitions
 */
extern struct inode *testfs_iget (struct super_block *sb, unsigned int ino);
extern int testfs_setattr(struct dentry *, struct iattr *);
extern void testfs_truncate(struct inode *);
extern int testfs_permission(struct inode *inode, int mask);

/* ialloc.c */
extern struct inode *testfs_new_inode(struct inode *dir, int mode);
extern void testfs_free_inode (struct inode *inode);

/* inode.c */
int __testfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags, struct page **pagep,
		void **fsdata);
int testfs_write_inode(struct inode *inode, int wait);
void testfs_delete_inode(struct inode *inode);
/* dir.c */
extern unsigned int testfs_inode_by_name(struct inode *dir, struct qstr *child);
extern int testfs_add_link(struct dentry *, struct inode *);
#endif
#endif /* __TEST_FS__ */
