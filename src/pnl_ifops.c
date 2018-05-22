#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <uapi/asm-generic/errno-base.h>
#include <uapi/asm-generic/errno.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/fs.h>
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
	// Reads without pejudice about the data as long as in the boundary of
	// the file (no \0 check!)
{
	struct buffer_head *bh, *bh2;
	struct pnlfs_file_index_block *index_block;
	struct pnlfs_inode_info *i_info;
	int start = 0, len = size, idx, i, blk, nb, slen;
	struct inode *inode = filp->f_inode;
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);

	if (inode->i_size < (*off))
	{
		pr_warn("[pnlfs] %s : attempt to read out of bounds\n", __func__);
		return -EFAULT;
	}

	pr_warn("[pnlfs] %s : fops=%lld isize=%lld \n", __func__,
			filp->f_pos, inode->i_size);

	if (filp->f_pos >= inode->i_size) { // EOF
		pr_warn("[pnlfs] %s : EOF\n", __func__);
		return 0;
	}
	if (S_ISDIR(filp->f_mode))
	{
		pr_warn("[pnlfs] %s : not a directory\n", __func__);
		return -ENOTDIR;
	}
	idx = i_info->index_block;
	bh = sb_bread(inode->i_sb, idx);
	if (!bh)
	{
		pr_warn("[pnlfs] %s : error when opening block sector\n",
				__func__);
		return -EIO;
	}
	index_block = (struct pnlfs_file_index_block *) bh->b_data;
	i = nb = 0;

	blk = (*off) / PNLFS_BLOCK_SIZE;
	start = (*off) % PNLFS_BLOCK_SIZE;
	while (len != 0)
	{
		pr_warn("[pnlfs] %s : entering loop\n", __func__);
		blk = pnl_find_index_block(index_block, blk);
		// if no blocks were found (unlikely)
		//
		if (blk == PNLFS_MAX_BLOCKS_PER_FILE)
		{
			pr_warn("[pnlfs] %s : no block sectors to read\n",
					__func__);
			brelse(bh);
			return -EIO;
		}

		blk = le32_to_cpu(index_block->blocks[blk]);
		bh2 = sb_bread(inode->i_sb, blk);
		if (!bh2)
		{
			pr_warn("[pnlfs] %s : error when opening block sector\n",
					__func__);
			brelse(bh);
			return -EIO;
		}

		slen = strnlen(start + bh2->b_data, PNLFS_BLOCK_SIZE - start);
		pr_warn("[pnlfs] %s : start = %d\n", __func__, start);
		if (slen >= PNLFS_BLOCK_SIZE - start) {
			pr_warn("[pnlfs] %s : exceeding block sector : %d\n", __func__,
					slen);
			memcpy(nb + buf, (char *)(start + bh2->b_data),
					slen);
			pr_warn("[pnlfs] %s : %s\n", __func__, (char *)(start + bh2->b_data));
			nb += slen;
			len -= slen;
			start = 0;
			blk++;
			/* filp->f_pos += slen;*/
			*off+=slen;
		} else {
			pr_warn("[pnlfs] %s : not exceeding block sector : %d\n", __func__, slen);
			memcpy(nb + buf, (char *)(start + bh2->b_data),
					slen);
			pr_warn("[pnlfs] %s : %s\n", __func__, (char *)(start + bh2->b_data));
			nb += slen;
			len = 0;
			/* filp->f_pos += slen;*/
			brelse(bh2);
			*off+=slen;
			break;
		}
		brelse(bh2);
	}
	brelse(bh);
	pr_warn("[pnlfs] %s : exiting (off = %lld, pos = %lld, size=%lld)\n", __func__, (*off), filp->f_pos, inode->i_size);
	return nb;
}


ssize_t pnl_write(struct file *filp, const char __user *buf, size_t size,
		loff_t *off)
	// Reads without pejudice about the data as long as in the boundary of
	// the file (no \0 check!)
{
	struct buffer_head *bh, *bh2;
	struct pnlfs_file_index_block *index_block;
	struct pnlfs_inode_info *i_info;
	int append = 0;
	int old_size;

	int start = 0, len = size, idx, i, blk, nb, slen;
	struct inode *inode = filp->f_inode;
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	pr_warn("[pnlfs] %s : entering\n", __func__);

	old_size = inode->i_size;

	/* quand on fait echo toto > foo ou >> le comportement est completement
	 * different, le chevron simple remet inode->i_size a 0 tout seul et le
	 * double chevron laisse la valeur
	 * VERIFIER AVEC UN FICHIER VIDE 
	 */
	pr_warn("[pnlfs] %s : non virgin file %lld \n", __func__,
			inode->i_size);

	if (inode->i_size + size >
			PNLFS_MAX_BLOCKS_PER_FILE * PNLFS_BLOCK_SIZE){
		return -1;
	}

	if (S_ISDIR(filp->f_mode)) {
		return -ENOTDIR;
	}

	idx = i_info->index_block;
	if (i_info->nr_entries == 0){
		//aloue un block
	}

	bh = sb_bread(inode->i_sb, idx);
	if (!bh){
		return -EIO;
	}

	index_block = (struct pnlfs_file_index_block *) bh->b_data;
	i = nb = 0;

	if (filp->f_flags & O_APPEND){
		// le denier
		blk = i_info->nr_entries - 1;
		start = inode->i_size % PNLFS_BLOCK_SIZE;
		append = 1;
		// ALLOCATION SI CA DEBORDE
	} else {
		//normal offset
		blk = (*off) / PNLFS_BLOCK_SIZE;
		start = (*off) % PNLFS_BLOCK_SIZE;
		/* inode->i_size = 0;*/
	}

	while (len != 0) {
		blk = pnl_find_index_block(index_block, blk);
		if (blk == PNLFS_MAX_BLOCKS_PER_FILE){
			brelse(bh);
			return -EIO;
		}

		blk = le32_to_cpu(index_block->blocks[blk]);
		bh2 = sb_bread(inode->i_sb, blk);
		if (!bh2) {
			brelse(bh);
			return -EIO;
		}

		slen = strnlen(buf, PNLFS_BLOCK_SIZE - start);
		if (slen >= PNLFS_BLOCK_SIZE - start) {
			pr_warn("[pnlfs] %s : not exceeding block sector : %d\n", __func__, slen);
			memcpy((char *)(start + bh2->b_data), buf, slen);
			nb += slen;
			len -= slen;
			start = 0;
			blk++;
			// ALLOCATION SI CA DEBORDE
			*off+=slen;
			if (append)
				inode->i_size += slen;
			/* inode->i_size += slen;*/

		} else {
			pr_warn("[pnlfs] %s : not exceeding block sector : %d\n", __func__, slen);
			memcpy((char *)(start + bh2->b_data), buf, slen);
			nb += slen;
			len = 0;
			brelse(bh2);
			*off+=slen;
			// ALLOCATION SI CA DEBORDE
			if (append)
				inode->i_size += slen;
			/* inode->i_size += slen;*/
			break;
		}

		brelse(bh2);
	}

	if (old_size){
		pr_warn("[pnlfs] %s : append is false patch me\n", __func__);
		inode->i_size = strlen(buf);
		*off = strlen(buf);
	}
	brelse(bh);
	pr_warn("[pnlfs] %s : exiting (off = %lld, pos = %lld, size=%lld)\n",
			__func__, (*off), filp->f_pos, inode->i_size);

	return inode->i_size;
}


