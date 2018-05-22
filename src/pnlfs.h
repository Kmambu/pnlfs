#ifndef _PNLFS_H
#define _PNLFS_H

#define PNLFS_MAGIC           0x434F5746

#define PNLFS_SB_BLOCK_NR              0
#define PNLFS_ISTORE_NR				   1

#define PNLFS_BLOCK_SIZE       (1 << 12)  /* 4 KiB */
#define PNLFS_MAX_FILESIZE     (1 << 22)  /* 4 MiB */
#define PNLFS_FILENAME_LEN            28
#define PNLFS_MAX_DIR_ENTRIES        128
#define PNLFS_MAX_BLOCKS_PER_FILE    1024


/*
 * pnlFS partition layout
 *
 * +---------------+
 * |  superblock   |  1 block
 * +---------------+
 * |  inode store  |  sb->nr_istore_blocks blocks
 * +---------------+
 * | ifree bitmap  |  sb->nr_ifree_blocks blocks
 * +---------------+
 * | bfree bitmap  |  sb->nr_bfree_blocks blocks
 * +---------------+
 * |    data       |
 * |      blocks   |  rest of the blocks
 * +---------------+
 *
 */

struct pnlfs_inode {
	__le32 mode;		  /* File mode */
	__le32 index_block;	  /* Block with list of blocks for this file */
	__le32 filesize;	  /* File size in bytes */
	union {
		__le32 nr_used_blocks;  /* Number of blocks used by file */
		__le32 nr_entries;     /* Number of files/dirs in directory */
	};
};

struct pnlfs_inode_info {
	uint32_t index_block;
	uint32_t nr_entries;
	struct inode vfs_inode;
};

#define PNLFS_INODES_PER_BLOCK (PNLFS_BLOCK_SIZE / sizeof(struct pnlfs_inode))

struct pnlfs_superblock {
	__le32 magic;	        /* Magic number */

	__le32 nr_blocks;       /* Total number of blocks (incl sb & inodes) */
	__le32 nr_inodes;       /* Total number of inodes */

	__le32 nr_istore_blocks;/* Number of inode store blocks */
	__le32 nr_ifree_blocks; /* Number of inode free bitmap blocks */
	__le32 nr_bfree_blocks; /* Number of block free bitmap blocks */

	__le32 nr_free_inodes;  /* Number of free inodes */
	__le32 nr_free_blocks;  /* Number of free blocks */

	char padding[4064];     /* Padding to match block size */
};

struct pnlfs_sb_info {
	uint32_t nr_blocks;      /* Total number of blocks (incl sb & inodes) */
	uint32_t nr_inodes;      /* Total number of inodes */

	uint32_t nr_istore_blocks;/* Number of inode store blocks */
	uint32_t nr_ifree_blocks; /* Number of inode free bitmap blocks */
	uint32_t nr_bfree_blocks; /* Number of block free bitmap blocks */

	uint32_t nr_free_inodes;  /* Number of free inodes */
	uint32_t nr_free_blocks;  /* Number of free blocks */

	unsigned long *ifree_bitmap;
	unsigned long *bfree_bitmap;
};

struct pnlfs_file_index_block {
	__le32 blocks[PNLFS_BLOCK_SIZE >> 2];
};

struct pnlfs_dir_block {
	struct pnlfs_file {
		__le32 inode;
		char filename[PNLFS_FILENAME_LEN];
	} files[PNLFS_MAX_DIR_ENTRIES];
};

#endif	/* _PNLFS_H */
