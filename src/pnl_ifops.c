#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <uapi/asm-generic/errno-base.h>
#include <uapi/asm-generic/errno.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/fs.h>
#include "pnl_iops.h"
#include "pnlfs.h"
// /!\ Open the index_block beforehand on a buffer head with sb_bread()
int pnl_find_index_block(struct pnlfs_file_index_block *index_block,
		uint32_t idx)
{
	int i = 0, cpt = 0;
	while (i != PNLFS_MAX_BLOCKS_PER_FILE)
	{
		if (index_block->blocks[i] != 0) {
			if (cpt == idx)
				break;
			else
				cpt++;
			i++;
		} else {
			i++;
			continue;
		}
	}
	pr_warn("[pnlfs] %s : block %d found at index %d\n", __func__, i, le32_to_cpu(index_block->blocks[i]));
	return i;
}

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
	for(i=0; i<i_info->nr_entries; i++)
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
	// Reads without pejudice about the data as long as in the boundary of
	// the file (no \0 check!)
{
	struct inode *inode = filp->f_inode;
	struct pnlfs_inode_info *i_info;
	struct buffer_head *bh, *bh2;
	struct pnlfs_file_index_block *file_index_block;
	u32 bno, r_start, s_len = 0, bidx, ret = 0, r_done = 0;

	if (inode->i_size == PNLFS_MAX_BLOCKS_PER_FILE)
	{
		pr_warn("[pnlfs] %s : exceeding max file capacity\n", __func__);
		return -ENOSPC;
	}
	if (S_ISDIR(filp->f_mode))
	{
		pr_warn("[pnlfs] %s : not a directory\n", __func__);
		return -EISDIR;
	}

	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	if (i_info->nr_entries == 0)
		return 0;
	bh = sb_bread(inode->i_sb, i_info->index_block);
	if (!bh)
	{
		pr_warn("[pnlfs] %s : error when opening block sector %d\n",
				__func__, i_info->index_block);
		return -EIO;
	}
	file_index_block = (struct pnlfs_file_index_block *) bh->b_data;

	bidx = (*off) / PNLFS_BLOCK_SIZE;
	r_start = (*off) % PNLFS_BLOCK_SIZE;
	while (!r_done) {
		bno = pnl_find_index_block(file_index_block, bidx);
		bno = le32_to_cpu(file_index_block->blocks[bno]);
		bh2 = sb_bread(inode->i_sb, bno);
		s_len = strnlen(bh2->b_data + (*off),
				PNLFS_BLOCK_SIZE - r_start);
		if (s_len == PNLFS_BLOCK_SIZE - r_start) // fin du bloc
		{
			r_start = 0;
			if (!((char)bh2->b_data[PNLFS_BLOCK_SIZE])) // \0
				r_done = 1;
			bidx++;
		} else {
			r_done = 1;
		}
		memcpy(buf + ret, bh2->b_data, s_len);
		ret += s_len;
		brelse(bh2);
	}
	brelse(bh);
	(*off) += ret;
	mark_inode_dirty(inode);
	return ret;
}


ssize_t pnl_write(struct file *filp, const char __user *buf, size_t size,
		loff_t *off)
	// Reads without pejudice about the data as long as in the boundary of
	// the file (no \0 check!)
{
	struct inode *inode = filp->f_inode;
	struct pnlfs_inode_info *i_info;
	struct buffer_head *bh, *bh2;
	struct pnlfs_file_index_block *file_index_block;
	u32 bno, w_start, s_len = 0, w_end = 0, bidx, ret = 0, nr_entries = 0;

	if (inode->i_size == PNLFS_MAX_BLOCKS_PER_FILE)
	{
		pr_warn("[pnlfs] %s : exceeding max file capacity\n", __func__);
		return -ENOSPC;
	}
	if (S_ISDIR(filp->f_mode))
	{
		pr_warn("[pnlfs] %s : not a directory\n", __func__);
		return -EISDIR;
	}

	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	bh = sb_bread(inode->i_sb, i_info->index_block);
	if (!bh)
	{
		pr_warn("[pnlfs] %s : error when opening block sector %d\n",
				__func__, i_info->index_block);
		return -EIO;
	}
	file_index_block = (struct pnlfs_file_index_block *) bh->b_data;
	if ((i_info->nr_entries == 0) && (inode->i_size == 0))
	{
		bno = pnl_new_index_block(inode->i_sb, inode);
		file_index_block->blocks[0] = cpu_to_le32(bno);
		i_info->nr_entries++;
	}

	if (filp->f_flags & O_APPEND) {
		bidx = i_info->nr_entries - 1;
		w_start = inode->i_size % PNLFS_BLOCK_SIZE;
	} else {
		bidx = (*off) / PNLFS_BLOCK_SIZE;
		w_start = (*off) % PNLFS_BLOCK_SIZE;
	}

	while(buf[s_len++] != '\0');
	if (size < s_len)
		s_len = size;
	if (s_len == 0)
		goto write_out;
	pr_warn("buf = \"%s\", len = %ld\n", buf, strlen(buf));
	//nr_entries = bidx + 1;
	nr_entries = bidx;
	while (s_len > 0) {
		bno = pnl_find_index_block(file_index_block, bidx);
		if (bno == PNLFS_MAX_BLOCKS_PER_FILE)
		{
			bno = pnl_new_index_block(inode->i_sb, inode);
			file_index_block->blocks[bidx] = cpu_to_le32(bno);
		}
		bno = le32_to_cpu(file_index_block->blocks[bno]);
		bh2 = sb_bread(inode->i_sb, bno);
		w_end = w_start + s_len;
		pr_warn("w_start = %d\n", w_start);
		pr_warn("w_end = %d\n", w_end);
		pr_warn("bidx = %d\n", bidx);
		pr_warn("s_len = %d\n", s_len);
		pr_warn("size = %ld\n", size);
		if (w_end > PNLFS_BLOCK_SIZE) {
			memcpy(bh2->b_data + w_start, buf + ret,
					PNLFS_BLOCK_SIZE - w_start);
			s_len -= (PNLFS_BLOCK_SIZE - w_start);
			w_start = 0;
			ret += (PNLFS_BLOCK_SIZE - w_start);
			bidx++;
		} else {
			memcpy(bh2->b_data + w_start, buf + ret, s_len);
			ret += s_len;
			s_len = 0;
		}
		mark_buffer_dirty(bh2);
		brelse(bh2);
		nr_entries++;
	}
write_out :
	i_info->nr_entries = nr_entries;
	mark_buffer_dirty(bh);
	brelse(bh);
	(*off) += ret;
	filp->f_pos = (*off);
	inode->i_size = (*off);
	mark_inode_dirty(inode);
	return ret;
}


