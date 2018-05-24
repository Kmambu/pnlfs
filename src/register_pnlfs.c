#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <uapi/asm-generic/errno-base.h>
#include <uapi/asm-generic/errno.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/fs.h>
#include "pnlfs.h"
#include "pnl_inode.h"

MODULE_DESCRIPTION("PNLfs registration module");
MODULE_AUTHOR("Kevin Mambu, M1 SESI");
MODULE_LICENSE("GPL");

void pnl_put_super (struct super_block *sb) {
	struct pnlfs_sb_info *sb_info = (struct pnlfs_sb_info *) sb->s_fs_info;
	kfree(sb_info->ifree_bitmap);
	kfree(sb_info->bfree_bitmap);
	kfree(sb_info);
}

int pnl_sync_fs(struct super_block *sb, int wait)
{
	struct pnlfs_superblock *raw_sb;
	struct pnlfs_sb_info *sbi;
	struct buffer_head *bh;
	uint32_t bno = PNLFS_SB_BLOCK_NR, k, i, j, lno;
	unsigned long *ifree_bitmap, *bfree_bitmap, *b_data;

	sbi = (struct pnlfs_sb_info *) sb->s_fs_info;
	bh = sb_bread(sb, bno);
	raw_sb = (struct pnlfs_superblock *) bh->b_data;
	raw_sb->nr_free_inodes = cpu_to_le32(sbi->nr_free_inodes);
	raw_sb->nr_free_blocks = cpu_to_le32(sbi->nr_free_blocks);
	mark_buffer_dirty(bh);
	if (wait) {
		pr_warn("[pnlfs] %s : WAIT_ON\n", __func__);
		if(sync_dirty_buffer(bh) != 0)
			pr_warn("FAILURE\n");
	}
	pr_warn("[pnlfs] %s : block sector %d successfully written\n",
			__func__, bno);
	brelse(bh);
	
	ifree_bitmap = sbi->ifree_bitmap;
	bfree_bitmap = sbi->bfree_bitmap;
	lno = PNLFS_BLOCK_SIZE / sizeof(unsigned long);
	bno += 1 + sbi->nr_istore_blocks;
	k = 0;
	for (i=0; i<sbi->nr_ifree_blocks; i++)
	{
		bh = sb_bread(sb, bno + i);
		b_data = (unsigned long *) bh->b_data;
		for (j=0; j<lno; j++)
			b_data[j] = cpu_to_le64(ifree_bitmap[k++]);
		mark_buffer_dirty(bh);
		if (wait) {
			pr_warn("[pnlfs] %s : WAIT_ON\n", __func__);
			if(sync_dirty_buffer(bh) != 0)
				pr_warn("FAILURE\n");
		}
		pr_warn("[pnlfs] %s : block sector %d successfully written\n",
				__func__, bno);
		brelse(bh);
	}
	
	bno += sbi->nr_ifree_blocks;
	k = 0;
	for (i=0; i<sbi->nr_bfree_blocks; i++)
	{
		bh = sb_bread(sb, bno + i);
		b_data = (unsigned long *) bh->b_data;
		for (j=0; j<lno; j++)
			b_data[j] = cpu_to_le64(bfree_bitmap[k++]);
		mark_buffer_dirty(bh);
		if (wait) {
			pr_warn("[pnlfs] %s : WAIT_ON\n", __func__);
			if(sync_dirty_buffer(bh) != 0)
				pr_warn("FAILURE\n");
		}
		pr_warn("[pnlfs] %s : block sector %d successfully written\n",
				__func__, bno);
		brelse(bh);
	}

	return 0;
}

struct super_operations pnl_sops = {
	.put_super = pnl_put_super,
	.alloc_inode = pnl_alloc_inode,
	.destroy_inode = pnl_destroy_inode,
	.sync_fs = pnl_sync_fs,
	.write_inode = pnl_write_inode,
};

int pnl_fill_super(struct super_block *sb, void *data, int silent)
{
	uint32_t nr_inodes, nr_blocks, lno, i, j, k, nr_istore_blocks,
		 nr_ifree_blocks, bno, nr_bfree_blocks;
	unsigned long *ifree_bitmap, *bfree_bitmap, *b_data;
	struct inode *root_inode;
	struct buffer_head *bh;
	struct pnlfs_superblock *raw_sb;
	struct pnlfs_sb_info *sb_info;

	sb->s_magic = PNLFS_MAGIC;
	sb->s_blocksize = PNLFS_BLOCK_SIZE;
	sb->s_maxbytes = PNLFS_MAX_FILESIZE;
	sb->s_op = &pnl_sops;
	sb_info = (struct pnlfs_sb_info *) kmalloc
		(sizeof(struct pnlfs_sb_info), GFP_KERNEL);
	sb->s_fs_info = (void *)sb_info;

	bh = sb_bread(sb, PNLFS_SB_BLOCK_NR);
	raw_sb = (struct pnlfs_superblock *) bh->b_data;
	sb_info->nr_blocks	= nr_blocks
				= le32_to_cpu(raw_sb->nr_blocks);
	sb_info->nr_inodes 	= nr_inodes
				= le32_to_cpu(raw_sb->nr_inodes);
	sb_info->nr_istore_blocks = nr_istore_blocks
				  = le32_to_cpu(raw_sb->nr_istore_blocks);
	sb_info->nr_ifree_blocks = nr_ifree_blocks
				 = le32_to_cpu(raw_sb->nr_ifree_blocks);
	sb_info->nr_bfree_blocks = nr_bfree_blocks
				 = le32_to_cpu(raw_sb->nr_bfree_blocks);
	sb_info->nr_free_inodes = le32_to_cpu(raw_sb->nr_free_inodes);
	sb_info->nr_free_blocks = le32_to_cpu(raw_sb->nr_free_blocks);
	brelse(bh);

	lno = PNLFS_BLOCK_SIZE / sizeof(unsigned long);
	ifree_bitmap = (unsigned long *) kmalloc
		(PNLFS_BLOCK_SIZE * nr_ifree_blocks, GFP_KERNEL);
	sb_info->ifree_bitmap = ifree_bitmap;
	bfree_bitmap = (unsigned long *) kmalloc
		(PNLFS_BLOCK_SIZE * nr_bfree_blocks, GFP_KERNEL);
	sb_info->bfree_bitmap = bfree_bitmap;

	bno = 1 + nr_istore_blocks;
	k = 0;
	for (i=0; i<nr_ifree_blocks; i++)
	{
		bh = sb_bread(sb, bno + i);
		b_data = (unsigned long *) bh->b_data;
		for (j=0; j<lno; j++)
			ifree_bitmap[k++] = le64_to_cpu(b_data[j]);
		brelse(bh);
	}
	
	bno += nr_ifree_blocks;
	k = 0;
	for (i=0; i<nr_bfree_blocks; i++)
	{
		bh = sb_bread(sb, bno + i);
		b_data = (unsigned long *) bh->b_data;
		for (j=0; j<lno; j++)
			bfree_bitmap[k++] = le64_to_cpu(b_data[j]);
		brelse(bh);
	}

	root_inode = pnl_iget(sb, 0);
	if(IS_ERR(root_inode))
		return PTR_ERR(root_inode);
	inode_init_owner(root_inode, NULL, S_IFDIR | root_inode->i_mode);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
		return -ENOMEM;
	pr_info("[pnlfs] pnl_fill_super() : success\n");
	return 0;
}

struct dentry *pnl_mount(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, pnl_fill_super);
}

void pnl_kill_sb(struct super_block *sb)
{
	pr_warn("[pnlfs] %s : calling kill_block_super()\n", __func__);
	kill_block_super(sb);
}

struct file_system_type pnlfs_type = {
	.name = "pnlfs",
	.fs_flags = (FS_REQUIRES_DEV),
	.mount = pnl_mount,
	.kill_sb = pnl_kill_sb,
	.owner = THIS_MODULE,
	.next = NULL,
};

int init_module(void)
{
	if (register_filesystem(&pnlfs_type) != 0)
		pr_err("[pnlfs] registration failed unexpectedly\n");
	pr_info("[pnlfs] registration successful\n");
	return 0;
}

void cleanup_module(void)
{
	if (unregister_filesystem(&pnlfs_type) != 0)
		pr_err("[pnlfs] unregistration failed unexpectedly\n");
	pr_info("[pnlfs] unregistration successful\n");
}
