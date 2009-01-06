#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<string.h>
#include<sys/stat.h>
#include<unistd.h>
#include<time.h>
#include<sys/types.h>
#include "../testfs.h"

#define TESTFS_VERSION "1.0.0"
#define TESTFS_TOOL "mktestfs"
#define TESTFS_MIN_BLOCKS 25
#define TESTFS_DFLT_BLOCKSIZE 4096
#define TESTFS_FIRST_NONMETA_INODE 6

#define MIN(a, b) ((a) < (b) ? (a):(b))
char *progname;

/*
 * Every good program has an usage too !!!
 */
static void usage()
{
	fprintf(stderr,"%s (version %s) - Create a testfs filesystem\n",
			TESTFS_TOOL, TESTFS_VERSION);
	return;
}

static inline get_block_from_inode(int ino)
{
	/*
	 * Blocks and inodes have 1:1 correspondence
	 */
	return ino;
}

/*
 * Create the root directory entries on the device
 */
static void create_root_dir(struct testfs_super_block sb, int fd)
{
	int block;
	off_t off;
	char buf[sb.s_blocksize];
	struct testfs_dir_entry dirent;
	struct testfs_inode inode;
	time_t tm;
	memset(&dirent, 0, sizeof(dirent));
	memset(&inode, 0, sizeof(inode));
	memset(buf, 0, sb.s_blocksize);

	/*
	 * Create entry for "."
	 */
	dirent.inode = TESTFS_ROOT_INODE(&sb);
	dirent.name_len = 1;
	dirent.file_type = S_IFDIR;
	strncpy(dirent.name, ".", 1);
	dirent.rec_len = calc_rec_len(&dirent);
	block = get_block_from_inode(dirent.inode);
	/*
	 * Clear the block. Doesn't matter even if it fails
	 */
	off = lseek(fd, block*sb.s_blocksize, SEEK_SET);
	write(fd, buf, sb.s_blocksize); 
	off = lseek(fd, block*sb.s_blocksize, SEEK_SET);
	if (off==-1) {
		perror("Unable to create root dirs on device ");
		exit(-1);
	}
	if (write(fd, (char *)&dirent, dirent.rec_len) == -1) {
		perror("Unable to write root dirent on device ");
		exit(-1);
	}

	/*
	 * Create entry for ".."
	 */
	memset(&dirent, 0, sizeof(dirent));
	dirent.inode = TESTFS_ROOT_INODE(&sb);
	dirent.name_len = 2;
	dirent.file_type = S_IFDIR;
	strncpy(dirent.name, "..", 2);
	dirent.rec_len = sb.s_blocksize - calc_reclen_from_len(1);
	if (write(fd, (char *)&dirent, sizeof(dirent)) == -1) {
		perror("Unable to write root dirent on device ");
		exit(-1);
	}
	testfs_debug("Root inode = %u\n",dirent.inode);

	/*
	 * Create the inode for root inode
	 */
	inode.uid = inode.gid = 0;
	inode.size = sb.s_blocksize;
	inode.type = S_IFDIR|0755;
	inode.nlinks = 2;
	inode.data[0] = dirent.inode;
	time(&tm);
	inode.atime.tv_sec = inode.mtime.tv_sec = inode.ctime.tv_sec = tm;
	off = 3*sb.s_blocksize + sizeof(struct testfs_inode)* (dirent.inode - sb.s_first_nonmeta_inode);
	/*
	 * Clear the block. Doesn't matter even if it fails
	 */
	off = lseek(fd, off, SEEK_SET); /* First inode block */
	write(fd, buf, sb.s_blocksize); 
	off = lseek(fd, off, SEEK_SET); /* First inode block */
	if (off==-1) {
		perror("Unable to create root inode on device ");
		exit(-1);
	}
	if (write(fd, (char *)&inode, sizeof(inode)) == -1) {
		perror("Unable to write root inode on device ");
		exit(-1);
	}
	return;
}

/*
 * Update the inode and block bitmaps required for initial FS creation.
 * Since inode and block have 1:1 correspondence they reside in the same block#2
 */
static void update_bitmaps(struct testfs_super_block sb, int fd)
{
	char buf[sb.s_blocksize];
	char *c;
	int off;
	memset(buf, 0, sb.s_blocksize);

	/* Mark blocks 0-6 in use. Block 0 will never be used */
	c = buf;
	*c = 0x7f;  /* 01111111 */
	off = lseek(fd, 2*sb.s_blocksize, SEEK_SET); /* bitmap block */
	if (off==-1) {
		perror("Unable to lseek to bitmap block on device ");
		exit(-1);
	}
	if (write(fd, (char *)buf, sb.s_blocksize) == -1) {
		perror("Unable to write bitmap on device ");
		exit(-1);
	}
	return;
}

/*
 * Create the filesystem ie... create superblock and other
 * required stuff so as to make this device mountable as testfs
 */
static void create_testfs(char *device)
{
	int fd;
	off_t off;
	int total_inodes ;
	int max_inode_entries;
	struct testfs_super_block sb;
	memset(&sb, 0 , sizeof(sb));
	fd = open(device, O_RDWR);
	if (fd==-1) {
		perror("Error opening device ");
		exit(-1);
	}

	/* Get the size of the device. Should be minimum 25*4KB */
	off = lseek(fd, 0, SEEK_END);
	if (off==-1) {
		perror("Error lseeking device ");
		exit(-1);
	}
	if (off/TESTFS_DFLT_BLOCKSIZE < TESTFS_MIN_BLOCKS) {
		fprintf(stderr, "Too small device file. Atleast 100KB is needed\n");
		exit(-1);
	}

	/*
	 * Currently we have 1:1 correspondence of blocks with inode number
	 * Total inodes only contains user usable inodes. This also means that
	 * in a directory we can have only 4096/(size of dirent) entries.
	 *
	 * An exception to this is the inode block. We reserve 3 blocks for inode
	 * table blocknumbers 3,4 & 5. I guess that should be enough for our testfs :-)
	 */
	total_inodes = off/TESTFS_DFLT_BLOCKSIZE;
	sb.s_blocksize = TESTFS_DFLT_BLOCKSIZE;
	sb.s_magic = TESTFS_MAGIC;
	sb.s_first_nonmeta_inode = TESTFS_FIRST_NONMETA_INODE;
	sb.s_max_inodes = total_inodes - sb.s_first_nonmeta_inode;

	/* Cap the max inodes based on inode table */
	max_inode_entries = (3*TESTFS_DFLT_BLOCKSIZE)/sizeof(struct testfs_inode);
	sb.s_max_inodes = MIN(max_inode_entries, sb.s_max_inodes);
	sb.s_free_inodes = sb.s_max_inodes - 1; /* 1 less due to root */
	testfs_debug("Max number of inodes in filesystem = %u\n", sb.s_max_inodes);

	if (off/sb.s_blocksize > sb.s_max_inodes) {
		fprintf(stderr, "TESTFS-warning : Too large device for (%u) inodes. Some blocks will be wasted\n", sb.s_max_inodes);
	}

	/* Write the superblock to device. 1st block is the superblock not the
	 * zeroeth one */
	off = lseek(fd, 4096, SEEK_SET);
	if (off==-1) {
		perror("Error lseeking device ");
		exit(-1);
	}
	if(write(fd, (char *)&sb, sizeof(sb)) == -1) {
		perror("Unable to write superblock ");
		exit(-1);
	}

	update_bitmaps(sb, fd);
	create_root_dir(sb, fd);
	close(fd);
}

int main(int argc, char **argv)
{
	char device[50];
	progname = argv[0];
	if (argc < 2) {
		fprintf(stderr, "Enter the device name : ");
		scanf("%[^\n]s",device);
	} else
		strcpy(device, argv[1]);
	create_testfs(device);
	return 0;
}
