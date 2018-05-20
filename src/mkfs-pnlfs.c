#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <endian.h>
#include <string.h>

#define PNLFS_MAGIC           0x434F5746

#define PNLFS_SB_BLOCK_NR              0

#define PNLFS_BLOCK_SIZE       (1 << 12)  /* 4 KiB */
#define PNLFS_MAX_FILESIZE     (1 << 22)  /* 4 MiB */
#define PNLFS_FILENAME_LEN            28
#define PNLFS_MAX_DIR_ENTRIES        128

struct pnlfs_inode {
	mode_t mode;		  /* File mode */
	uint32_t index_block;	  /* Block with list of block for this file */
	uint32_t filesize;	  /* File size in bytes */
	union {
		uint32_t nr_used_blocks;  /* Number of blocks used by file */
		uint32_t nr_entries;	  /* Number of dirs/files in dir */
	};
};

#define PNLFS_INODES_PER_BLOCK (PNLFS_BLOCK_SIZE / sizeof(struct pnlfs_inode))

struct pnlfs_superblock {
	uint32_t magic;		  /* Magic number */

	uint32_t nr_blocks;	  /* Total number of blocks (incl sb & inodes) */
	uint32_t nr_inodes;       /* Total number of inodes */

	uint32_t nr_istore_blocks;/* Number of inode store blocks */
	uint32_t nr_ifree_blocks; /* Number of free inodes bitmask blocks */
	uint32_t nr_bfree_blocks; /* Number of free blocks bitmask blocks */

	uint32_t nr_free_inodes;  /* Number of free inodes */
	uint32_t nr_free_blocks;  /* Number of free blocks */

	char padding[4064];       /* Padding to match block size */
};

struct pnlfs_file_index_block {
	uint32_t blocks[PNLFS_BLOCK_SIZE >> 2];
};

struct pnlfs_dir_block {
	struct pnlfs_file {
		uint32_t inode;
		char filename[PNLFS_FILENAME_LEN];
	} files[PNLFS_MAX_DIR_ENTRIES];
};

static inline void usage(char *appname)
{
	fprintf(stderr,
		"Usage:\n"
		"%s disk\n",
		appname);
}

/* Returns ceil(a/b) */
static inline uint32_t idiv_ceil(uint32_t a, uint32_t b)
{
	uint32_t ret = a / b;
	if (a % b != 0)
		ret++;
	return ret;
}

static struct pnlfs_superblock *write_superblock(int fd, struct stat *fstats)
{
	int ret;
	struct pnlfs_superblock *sb;
	uint32_t nr_inodes = 0, nr_blocks = 0, nr_ifree_blocks = 0;
	uint32_t nr_bfree_blocks = 0, nr_data_blocks = 0, nr_istore_blocks = 0;
	uint32_t mod;

	sb = malloc(sizeof(struct pnlfs_superblock));
	if (!sb)
		return NULL;

	nr_blocks = fstats->st_size / PNLFS_BLOCK_SIZE;
	nr_inodes = nr_blocks;
	mod = nr_inodes % PNLFS_INODES_PER_BLOCK;
	if (mod != 0)
		nr_inodes += mod;
	nr_istore_blocks = idiv_ceil(nr_inodes, PNLFS_INODES_PER_BLOCK);
	nr_ifree_blocks = idiv_ceil(nr_inodes, PNLFS_BLOCK_SIZE * 8);
	nr_bfree_blocks = idiv_ceil(nr_blocks, PNLFS_BLOCK_SIZE * 8);
	nr_data_blocks = nr_blocks - 1 - nr_istore_blocks - nr_ifree_blocks - nr_bfree_blocks;

	memset(sb, 0, sizeof(struct pnlfs_superblock));
	sb->magic = htole32(PNLFS_MAGIC);
	sb->nr_blocks = htole32(nr_blocks);
	sb->nr_inodes = htole32(nr_inodes);
	sb->nr_istore_blocks = htole32(nr_istore_blocks);
	sb->nr_ifree_blocks = htole32(nr_ifree_blocks);
	sb->nr_bfree_blocks = htole32(nr_bfree_blocks);
	sb->nr_free_inodes = htole32(nr_inodes - 2);
	sb->nr_free_blocks = htole32(nr_data_blocks - 3);

	ret = write(fd, sb, sizeof(struct pnlfs_superblock));
	if (ret != sizeof(struct pnlfs_superblock)) {
		free(sb);
		return NULL;
	}

	printf("Superblock: (%ld)\n"
	       "\tmagic=%#x\n"
	       "\tnr_blocks=%u\n"
	       "\tnr_inodes=%u (istore=%u blocks)\n"
	       "\tnr_ifree_blocks=%u\n"
	       "\tnr_bfree_blocks=%u\n"
	       "\tnr_free_inodes=%u\n"
	       "\tnr_free_blocks=%u\n",
	       sizeof(struct pnlfs_superblock),
	       sb->magic, sb->nr_blocks, sb->nr_inodes, sb->nr_istore_blocks,
	       sb->nr_ifree_blocks, sb->nr_bfree_blocks, sb->nr_free_inodes,
	       sb->nr_free_blocks);

	return sb;
}

static int write_inode_store(int fd, struct pnlfs_superblock *sb)
{
	int ret = 0, i;
	struct pnlfs_inode inode;
	uint32_t first_data_block;

	/* Root inode (inode 0) */
	first_data_block = 1 + le32toh(sb->nr_bfree_blocks) +
		le32toh(sb->nr_ifree_blocks) +
		le32toh(sb->nr_istore_blocks);
	memset(&inode, 0, sizeof(inode));
	inode.mode = htole32(S_IFDIR |
			     S_IRUSR | S_IRGRP | S_IROTH |
			     S_IWUSR | S_IWGRP |
			     S_IXUSR | S_IXGRP | S_IXOTH);
	inode.index_block = htole32(first_data_block++);
	inode.filesize = htole32(PNLFS_BLOCK_SIZE);
	inode.nr_entries = htole32(1);

	ret = write(fd, &inode, sizeof(struct pnlfs_inode));
	if (ret != sizeof(struct pnlfs_inode))
		return -1;

	/* /foo inode (inode 1) */
	memset(&inode, 0, sizeof(inode));
	inode.mode = htole32(S_IFREG |
			     S_IRUSR | S_IRGRP | S_IROTH |
			     S_IWUSR | S_IWGRP | S_IWOTH);
	inode.index_block = htole32(first_data_block++);
	inode.filesize = htole32(strlen("foo\n"));
	inode.nr_used_blocks = htole32(1);

	ret = write(fd, &inode, sizeof(struct pnlfs_inode));
	if (ret != sizeof(struct pnlfs_inode))
		return -1;

	/* Other empty inodes (inodes 2 -> end) */
	memset(&inode, 0, sizeof(inode));
	for (i = 2; i < le32toh(sb->nr_inodes); i++) {
		ret = write(fd, &inode, sizeof(struct pnlfs_inode));
		if (ret != sizeof(struct pnlfs_inode))
			return -1;
	}

	printf("Inode store: wrote %ld blocks\n"
	       "\tinode size = %ld\n",
	       i / PNLFS_INODES_PER_BLOCK, sizeof(struct pnlfs_inode));

	return 0;
}

static int write_ifree_blocks(int fd, struct pnlfs_superblock *sb)
{
	int ret = 0, i;
	uint64_t ifree[PNLFS_BLOCK_SIZE / 8];

	/* Set all bits to 1 */
	memset(ifree, 0xff, PNLFS_BLOCK_SIZE);

	/* First ifree block, containing first 2 used inodes */
	ifree[0] = htole64(0xfffffffffffffffc);
	ret = write(fd, ifree, PNLFS_BLOCK_SIZE);
	if (ret != PNLFS_BLOCK_SIZE)
		return -1;

	/* All ifree blocks except the one containing 2 first inodes */
	ifree[0] = 0xffffffffffffffff;
	for (i = 1; i < le32toh(sb->nr_ifree_blocks); i++) {
		ret = write(fd, ifree, PNLFS_BLOCK_SIZE);
		if (ret != PNLFS_BLOCK_SIZE)
			return -1;
	}

	printf("Ifree blocks: wrote %d blocks\n", i);

	return 0;
}

static int write_bfree_blocks(int fd, struct pnlfs_superblock *sb)
{
	int ret = 0, i;
	uint64_t bfree[PNLFS_BLOCK_SIZE / 8], mask, line;
	uint32_t nr_used = le32toh(sb->nr_istore_blocks) +
		le32toh(sb->nr_ifree_blocks) +
		le32toh(sb->nr_bfree_blocks) + 4;

	/*
	 * First blocks (incl. sb + istore + ifree + bfree + 3 used blocks)
	 * we suppose it won't go further than the first block
	 */
	memset(bfree, 0xff, PNLFS_BLOCK_SIZE);
	i = 0;
	while (nr_used) {
		line = 0xffffffffffffffff;
		for (mask = 0x1; mask != 0x0; mask <<= 1) {
			line &= ~mask;
			nr_used--;
			if (!nr_used)
				break;
		}
		bfree[i] = htole64(line);
		i++;
	}
	ret = write(fd, bfree, PNLFS_BLOCK_SIZE);
	if (ret != PNLFS_BLOCK_SIZE)
		return -1;

	/* other blocks */
	memset(bfree, 0xff, PNLFS_BLOCK_SIZE);
	for (i = 1; i < le32toh(sb->nr_bfree_blocks); i++) {
		ret = write(fd, bfree, PNLFS_BLOCK_SIZE);
		if (ret != PNLFS_BLOCK_SIZE)
			return errno;
	}

	printf("Bfree blocks: wrote %d blocks\n", i);

	return 0;
}

static int write_data_blocks(int fd, struct pnlfs_superblock *sb)
{
	int ret = 0;
	struct pnlfs_dir_block root_block;
	struct pnlfs_file_index_block foo_block;
	char foo[PNLFS_BLOCK_SIZE];
	uint32_t first_block = le32toh(sb->nr_istore_blocks) +
		le32toh(sb->nr_ifree_blocks) + le32toh(sb->nr_bfree_blocks) + 3;

	/* Root block (/) */
	memset(&root_block, 0, sizeof(root_block));
	strncpy(root_block.files[0].filename, "foo", PNLFS_FILENAME_LEN);
	root_block.files[0].inode = htole32(1);
	ret = write(fd, &root_block, sizeof(root_block));
	if (ret != PNLFS_BLOCK_SIZE)
		return errno;

	/* foo index block (/foo) */
	memset(&foo_block, 0, sizeof(foo_block));
	foo_block.blocks[0] = htole32(first_block);
	ret = write(fd, &foo_block, sizeof(foo_block));
	if (ret != PNLFS_BLOCK_SIZE)
		return errno;

	/* /foo data block */
	memset(foo, 0, PNLFS_BLOCK_SIZE);
	strncpy(foo, "foo\n", strlen("foo\n"));
	ret = write(fd, foo, PNLFS_BLOCK_SIZE);
	if (ret != PNLFS_BLOCK_SIZE)
		return errno;

	return 0;
}

int main(int argc, char **argv)
{
	int ret = EXIT_SUCCESS, fd;
	long int min_size;
	struct stat stat_buf;
	struct pnlfs_superblock *sb = NULL;

	if (argc != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	/* Open disk image */
	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("open():");
		return EXIT_FAILURE;
	}

	/* Get image size */
	ret = fstat(fd, &stat_buf);
	if (ret != 0) {
		perror("fstat():");
		ret = EXIT_FAILURE;
		goto fclose;
	}

	/* Check if image is large enough */
	min_size = 100 * PNLFS_BLOCK_SIZE;
	if (stat_buf.st_size <= min_size) {
		fprintf(stderr,
			"File is not large enough (size=%ld, min size=%ld)\n",
			stat_buf.st_size,
			min_size);
		ret = EXIT_FAILURE;
		goto fclose;
	}

	/* Write superblock (block 0) */
	sb = write_superblock(fd, &stat_buf);
	if (!sb) {
		perror("write_superblock():");
		ret = EXIT_FAILURE;
		goto fclose;
	}

	/* Write inode store blocks (from block 1) */
	ret = write_inode_store(fd, sb);
	if (ret != 0) {
		perror("write_inode_store():");
		ret = EXIT_FAILURE;
		goto free_sb;
	}

	/* Write inode free bitmap blocks */
	ret = write_ifree_blocks(fd, sb);
	if (ret != 0) {
		perror("write_ifree_blocks()");
		ret = EXIT_FAILURE;
		goto free_sb;
	}

	/* Write block free bitmap blocks */
	ret = write_bfree_blocks(fd, sb);
	if (ret != 0) {
		perror("write_bfree_blocks()");
		ret = EXIT_FAILURE;
		goto free_sb;
	}

	/* Write data blocks */
	ret = write_data_blocks(fd, sb);
	if (ret != 0) {
		perror("write_data_blocks():");
		ret = EXIT_FAILURE;
		goto free_sb;
	}

free_sb:
	free(sb);
fclose:
	close(fd);

	return ret;
}
