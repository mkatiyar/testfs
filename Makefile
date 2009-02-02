PROG = testfs
obj-m := ${PROG}.o
${PROG}-objs := super.o inode.o ialloc.o file.o namei.o dir.o symlink.o

EXTRA_CFLAGS += -g3 #-DTESTFS_DEBUG
SRC_PATH = /mnt/host/home/mkatiyar/personal/uml/linux-git
CLEANUP_FILES = *.o *.ko [Mm]odule* ${PROG}.mod.[co]

${PROG}:
	make -C ${SRC_PATH} M=`pwd` modules
clean:
	rm -rf ${CLEANUP_FILES}
