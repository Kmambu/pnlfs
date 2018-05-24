#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <uapi/asm-generic/errno-base.h>
#include <uapi/asm-generic/errno.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/fs.h>
#include "pnlfs.h"
#include "pnl_iops.h"
#include "pnl_ifops.h"

const struct inode_operations pnl_iops = {
	.lookup = pnl_lookup,
	.create = pnl_create,
	.mkdir  = pnl_mkdir,
	.unlink = pnl_unlink,
	.rename = pnl_rename,
};

struct file_operations pnl_ifops = {
	.owner = THIS_MODULE,
	.iterate_shared = pnl_readdir,
	.read = pnl_read,
	.write = pnl_write,
};

struct inode *pnl_alloc_inode(struct super_block *sb)
{
	struct pnlfs_inode_info *i_info = (struct pnlfs_inode_info *) kmalloc
		(sizeof(struct pnlfs_inode_info), GFP_KERNEL);
	if(!i_info)
		return ERR_PTR(-ENOMEM);
	inode_init_once(&i_info->vfs_inode);
	return &i_info->vfs_inode;
}

void pnl_destroy_inode(struct inode *inode)
{
	kfree(container_of(inode, struct pnlfs_inode_info, vfs_inode));
}

struct inode *pnl_iget(struct super_block *sb, unsigned long ino)
{
	uint32_t bno, sno;
	struct buffer_head *bh;
	struct inode *inode;
	struct pnlfs_inode *raw_inode;
	struct pnlfs_inode_info *i_info;

	inode = iget_locked(sb, ino);
	if (!inode) {
		pr_warn("[pnlfs] --- iget() ---\n");
		pr_warn("[pnlfs] No more memory to allocate an inode\n");
		iget_failed(inode);
		return ERR_PTR(-ENOMEM);
	}
	else if (!(inode->i_state & I_NEW))
		return inode;

	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	bno = ino / (PNLFS_BLOCK_SIZE / sizeof(struct pnlfs_inode)) + 1;
	sno = ino % (PNLFS_BLOCK_SIZE / sizeof(struct pnlfs_inode));
	inode->i_op = &pnl_iops;
	inode->i_fop = &pnl_ifops;
	inode->i_sb = sb;
	inode->i_ino = ino;
	bh = sb_bread(sb, bno);
	raw_inode = (struct pnlfs_inode *)&bh->b_data[sno];
	inode->i_mode = le32_to_cpu(raw_inode->mode);
	inode->i_size = le32_to_cpu(raw_inode->filesize);
	inode->i_blocks = le32_to_cpu(raw_inode->nr_used_blocks);
	i_info->index_block = le32_to_cpu(raw_inode->index_block);
	i_info->nr_entries = le32_to_cpu(raw_inode->nr_entries);
	brelse(bh);

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	pr_info("[pnlfs] pnl_iget() : success\n");
	pr_info("[pnlfs] inode : <%p>\n", inode);
	pr_info("[pnlfs] i_info : <%p>\n", i_info);
	pr_info("[pnlfs] i_mode : %d\n", inode->i_mode);
	pr_info("[pnlfs] i_ino : %ld\n", inode->i_ino);
	pr_info("[pnlfs] i_size : %lld\n", inode->i_size);
	pr_info("[pnlfs] i_blocks : %ld\n", inode->i_blocks);
	pr_info("[pnlfs] index_block : %d\n", i_info->index_block);
	pr_info("[pnlfs] nr_entries : %d\n", i_info->nr_entries);
	unlock_new_inode(inode);
	return inode;
}

int pnl_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int wait = 1;
	//int wait = (wbc->sync_mode == WB_SYNC_ALL);
	struct buffer_head *bh, *bh2;
	struct pnlfs_inode *raw_inode;
	struct super_block *sb = inode->i_sb;
	struct pnlfs_inode_info *i_info;
	struct pnlfs_file_index_block *file_index_block;
	ino_t ino = inode->i_ino;
	u32 bno, sno, i;

	inode_lock(inode);
	bno = ino / (PNLFS_BLOCK_SIZE / sizeof(struct pnlfs_inode)) + 1;
	sno = ino % (PNLFS_BLOCK_SIZE / sizeof(struct pnlfs_inode));
	bh = sb_bread(sb, bno);
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	raw_inode = (struct pnlfs_inode *)&bh->b_data[sno];
	//raw_inode->mode = 0;
	//raw_inode->filesize = 0;
	//raw_inode->index_block = 0;
	//raw_inode->nr_entries = 0;
	raw_inode->mode = cpu_to_le32((uint32_t) inode->i_mode);
	raw_inode->filesize = cpu_to_le32((uint32_t) inode->i_size);
	raw_inode->index_block = cpu_to_le32((uint32_t) i_info->index_block);
	raw_inode->nr_entries = cpu_to_le32((uint32_t) i_info->nr_entries);
	pr_warn("[pnlfs] %s : raw_inode->mode = %d\n",
			__func__, le32_to_cpu(raw_inode->mode));
	pr_warn("[pnlfs] %s : raw_inode->filesize = %d\n",
			__func__, le32_to_cpu(raw_inode->filesize));
	pr_warn("[pnlfs] %s : raw_inode->index_block = %d\n",
			__func__, le32_to_cpu(raw_inode->index_block));
	pr_warn("[pnlfs] %s : raw_inode->nr_entries = %d\n",
			__func__, le32_to_cpu(raw_inode->nr_entries));
	mark_buffer_dirty(bh);
	if (wait) {
		pr_warn("[pnlfs] %s : WAIT_ON\n", __func__);
		if(sync_dirty_buffer(bh) != 0)
			pr_warn("FAILURE\n");
	}
	pr_warn("[pnlfs] %s : block sector %d successfully written\n",
			__func__, bno);
	brelse(bh);
	bh = sb_bread(sb, i_info->index_block);
	mark_buffer_dirty(bh);
	if (wait) {
		pr_warn("[pnlfs] %s : WAIT_ON\n", __func__);
		if(sync_dirty_buffer(bh) != 0)
			pr_warn("FAILURE\n");
	}
	pr_warn("[pnlfs] %s : block sector %d successfully written\n",
			__func__, i_info->index_block);
	if (S_ISREG(inode->i_mode))
	{
		file_index_block = (struct pnlfs_file_index_block *) bh->b_data;
		for (i = 0; i < i_info->nr_entries; i++)
		{
			bno = le32_to_cpu(file_index_block->blocks[i]);
			bh2 = sb_bread(sb, bno);
			mark_buffer_dirty(bh2);
			if (wait) {
				pr_warn("[pnlfs] %s : WAIT_ON\n", __func__);
				if(sync_dirty_buffer(bh2) != 0)
					pr_warn("FAILURE\n");
			}
			pr_warn("[pnlfs] %s : block sector %d successfully written\n",
					__func__, bno);
			brelse(bh2);
		}
	}
	brelse(bh);
	pr_warn("[pnlfs] %s : inode <%p> ino %ld successfully written\n",
			__func__, inode, inode->i_ino);
	pr_info("[pnlfs] inode : <%p>\n", inode);
	pr_info("[pnlfs] i_info : <%p>\n", i_info);
	pr_info("[pnlfs] i_mode : %d\n", inode->i_mode);
	pr_info("[pnlfs] i_ino : %ld\n", inode->i_ino);
	pr_info("[pnlfs] i_size : %lld\n", inode->i_size);
	pr_info("[pnlfs] i_blocks : %ld\n", inode->i_blocks);
	pr_info("[pnlfs] index_block : %d\n", i_info->index_block);
	pr_info("[pnlfs] nr_entries : %d\n", i_info->nr_entries);
	inode_unlock(inode);
	return 0;
}
