#include <linux/fs.h>
#include <linux/dcache.h>
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
	struct pnlfs_inode raw_inode;
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
	raw_inode = ((struct pnlfs_inode *) bh->b_data)[sno];
	inode->i_mode = le32_to_cpu(raw_inode.mode);
	inode->i_size = le32_to_cpu(raw_inode.filesize);
	inode->i_blocks = le32_to_cpu(raw_inode.nr_used_blocks);
	i_info->index_block = le32_to_cpu(raw_inode.index_block);
	i_info->nr_entries = le32_to_cpu(raw_inode.nr_entries);
	brelse(bh);

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	pr_info("[pnlfs] pnl_iget() : success\n");
	unlock_new_inode(inode);
	return inode;
}
