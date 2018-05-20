#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <uapi/asm-generic/errno-base.h>
#include <uapi/asm-generic/errno.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/fs.h>
#include "pnlfs.h"

int pnl_readdir(struct file *file, struct dir_context *ctx)
{
	uint32_t i=0, ino, index_block, nr_entries, err;
	struct super_block *sb;
	struct inode *inode = file->f_inode;
	struct pnlfs_inode_info *i_info;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_file raw_child;
	ssize_t len;
	char *name;
	if (ctx->pos) {
		return 0;
	}
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	if (!i_info)
		return 0;
	if (!dir_emit_dots(file, ctx))
		return 0;
	sb = inode->i_sb;
	index_block = i_info->index_block;
	nr_entries = i_info->nr_entries;
	if (!sb)
		return 0;
	bh = sb_bread(sb, index_block);
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	for(i=0; i<PNLFS_MAX_DIR_ENTRIES; i++)
	{
		raw_child = dir_block->files[i];
		ino = le32_to_cpu(raw_child.inode);
		len = strnlen(raw_child.filename, PNLFS_FILENAME_LEN);
		name = raw_child.filename;
		if(ino)
		{
			err = dir_emit(ctx, name, len, ino, DT_UNKNOWN);
			if(!err)
			{
				brelse(bh);
				return 0;
			}
			ctx->pos += sizeof(struct pnlfs_file);
		}
	}
	brelse(bh);
	return 0;
}

ssize_t pnl_read(struct file *filp, char __user *buf, size_t size,
		loff_t *off)
{
	struct buffer_head *bh;
	struct pnlfs_file_index_block *index_block;
	struct pnlfs_sb_info *sb_info;
	struct pnlfs_inode_info *i_info;
	int len = 0, idx, i;
	struct inode *inode = filp->f_inode;
	return len;
}
