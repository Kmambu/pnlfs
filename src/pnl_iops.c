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

struct dentry *pnl_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	uint32_t i;
	ino_t ino = -1;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_inode_info *i_info;
	struct pnlfs_file file;
	struct inode *inode;
	blkcnt_t bno;
	i_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	bno = i_info->index_block;
	bh = sb_bread(dir->i_sb,bno);
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	for (i=0; i<PNLFS_MAX_DIR_ENTRIES; i++)
	{
		file = dir_block->files[i];
		if (!strcmp(dentry->d_name.name, file.filename)) {
			ino = le32_to_cpu(file.inode);
			break;
		}
	}
	brelse(bh);

	if(ino == -1) {
		d_add(dentry, NULL);
	} else {
		inode = pnl_iget(dir->i_sb, ino);
		d_add(dentry, inode);
	}
	return NULL;
}

struct inode *pnl_new_inode(struct inode *dir, umode_t mode, int *error)
{
	struct inode *inode;
	struct pnlfs_inode_info *i_info;
	struct super_block *sb;
	struct pnlfs_sb_info *sb_info;
	unsigned long *ifree_bitmap, *bfree_bitmap;
	uint32_t nr_inodes, nr_blocks, index_block;
	ino_t ino;

	if(!S_ISDIR(dir->i_mode)) {
		pr_warn("[pnlfs] --- new_inode ---\n");
		pr_warn("[pnlfs] \"dir\" is not a directory\n");
		return ERR_PTR(-EFAULT);
	}

	sb = dir->i_sb;
	sb_info = (struct pnlfs_sb_info *) sb->s_fs_info;
	ifree_bitmap = sb_info->ifree_bitmap;
	bfree_bitmap = sb_info->bfree_bitmap;
	nr_blocks = sb_info->nr_blocks;
	nr_inodes = sb_info->nr_inodes;
	index_block = find_first_bit(bfree_bitmap, nr_blocks);
	if (index_block == nr_blocks) {
		pr_warn("[pnlfs] --- new_inode ---\n");
		pr_warn("[pnlfs] No more available blocks\n");
		return ERR_PTR(-ENOSPC);
	}
	ino = find_first_bit(ifree_bitmap, nr_inodes);
	if (ino == nr_inodes) {
		pr_warn("[pnlfs] --- new_inode ---\n");
		pr_warn("[pnlfs] No more available inode numbers : %ld\n", ino);
		return ERR_PTR(-ENOMEM);
	}

	inode = pnl_iget(sb, ino);
	if (IS_ERR(inode))
		return inode;
	inode->i_mode = mode;
	bitmap_clear(ifree_bitmap, ino, 1);
	bitmap_clear(bfree_bitmap, index_block, 1);
	sb_info->nr_free_inodes--;
	sb_info->nr_free_blocks--;
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	i_info->index_block = index_block;
	i_info->nr_entries = 0;
	inode->i_size = 0;
	mark_inode_dirty(inode);
	pr_info("[pnlfs] pnl_new_inode() : success\n");
	return inode;
}

int pnl_find_dir_entry(struct pnlfs_dir_block *dir_block, struct dentry *dentry)
{
	const char *src = dentry->d_name.name;
	char *dst;
	uint32_t i;
	for (i=0; i<PNLFS_MAX_DIR_ENTRIES; i++)
	{
		dst = dir_block->files[i].filename;
		if (!strcmp(dst, src))
			return i;
	}
	return PNLFS_MAX_DIR_ENTRIES;
}

int pnl_free_dir_entry(struct pnlfs_dir_block *dir_block)
{
	uint32_t i;
	for (i=0; i<PNLFS_MAX_DIR_ENTRIES; i++)
	{
		if (dir_block->files[i].inode == 0)
			return i;
	}
	return PNLFS_MAX_DIR_ENTRIES;
}

int pnl_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	struct inode *inode;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_inode_info *i_info;
	char *dst;
	const char *src;
	int err;
	uint32_t nr_entries, idx;
	i_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	nr_entries = i_info->nr_entries;

	if (i_info->nr_entries >= PNLFS_MAX_DIR_ENTRIES) {
		pr_warn("[pnlfs] --- create ---\n");
		pr_warn("[pnlfs] Too much entries\n");
		return -ENOSPC;
	}
	if (dentry->d_name.len > PNLFS_FILENAME_LEN) {
		pr_warn("[pnlfs] --- create ---\n");
		pr_warn("[pnlfs] Filename too long\n");
		return -ENAMETOOLONG;
	}
	inode = pnl_new_inode(dir, mode, &err);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		return err;
	}

	bh = sb_bread(inode->i_sb, i_info->index_block); 
	if(!bh) {
		pr_warn("[pnlfs] --- create ---\n");
		pr_warn("[pnlfs] sb_bread failed\n");
		iput(inode);
		return -ENOMEM;
	}
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	idx = pnl_find_dir_entry(dir_block, dentry);
	if (idx != PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- create ---\n");
		pr_warn("[pnlfs] File exists\n");
		brelse(bh);
		iput(inode);
		return -EEXIST;
	}
	idx = pnl_free_dir_entry(dir_block);
	if (idx == PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- create ---\n");
		pr_warn("[pnlfs] Exceeded max enteies in directory\n");
		brelse(bh);
		iput(inode);
		return -ENOSPC;
	}
	dst = dir_block->files[idx].filename;
	src = dentry->d_name.name;
	if (!strncpy(dst, src, PNLFS_FILENAME_LEN)) {
		pr_warn("[pnlfs] --- create ---\n");
		pr_warn("[pnlfs] Filename copy into dir_block failed\n");
		brelse(bh);
		iput(inode);
		return -ENOMEM;
	}
	dir_block->files[idx].inode = cpu_to_le32(inode->i_ino);
	brelse(bh);

	i_info->nr_entries++;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	pr_info("[pnlfs] pnl_create() : success\n");
	pr_info("[pnlfs] inode : <%p>\n", inode);
	pr_info("[pnlfs] i_info : <%p>\n", i_info);
	pr_info("[pnlfs] i_mode : %d\n", inode->i_mode);
	pr_info("[pnlfs] i_ino : %ld\n", inode->i_ino);
	pr_info("[pnlfs] i_size : %lld\n", inode->i_size);
	pr_info("[pnlfs] i_blocks : %ld\n", inode->i_blocks);
	pr_info("[pnlfs] index_block : %d\n", i_info->index_block);
	pr_info("[pnlfs] nr_entries : %d\n", i_info->nr_entries);
	return 0;
}

int pnl_unlink(struct inode *dir, struct dentry *dentry)
{
	uint32_t idx = 0, dir_index, ino;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_inode_info *dir_info, *i_info;
	struct inode *inode;
	struct pnlfs_sb_info *sb_info;
	const char * name = dentry->d_name.name;
	unsigned long *ifree_bitmap, *bfree_bitmap;
	dir_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	dir_index = dir_info->index_block;
	bh = sb_bread(sb, dir_index);
	if (!bh) {
		pr_warn("[pnlfs] --- unlink ---\n");
		pr_warn("[pnlfs] sb_read() failed\n");
		brelse(bh);
		return -EIO;
	}
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	idx = pnl_find_dir_entry(dir_block, dentry);
	if (idx == PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- unlink ---\n");
		pr_warn("[pnlfs] %s doesn't exist\n", name);
		brelse(bh);
		return -ENOENT;
	}
	sb_info = (struct pnlfs_sb_info *) sb->s_fs_info;
	ino = le32_to_cpu(dir_block->files[idx].inode);
	dir_block->files[idx].inode = cpu_to_le32(0);
	brelse(bh);
	inode = pnl_iget(sb, ino);
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	ifree_bitmap = sb_info->ifree_bitmap;
	bfree_bitmap = sb_info->bfree_bitmap;
	bitmap_set(ifree_bitmap, inode->i_ino, 1);
	bitmap_set(bfree_bitmap, i_info->index_block, 1);
	inode_dec_link_count(inode);
	iput(inode);
	dir_info->nr_entries--;
	return 0;
}

int pnl_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_inode_info *i_info;
	char *dst;
	const char *src;
	int err;
	uint32_t nr_entries, idx;
	i_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	nr_entries = i_info->nr_entries;
	if (i_info->nr_entries >= PNLFS_MAX_DIR_ENTRIES) {
		pr_warn("[pnlfs] --- mkdir ---\n");
		pr_warn("[pnlfs] Too much entries\n");
		return -ENOSPC;
	}
	if (dentry->d_name.len > PNLFS_FILENAME_LEN) {
		pr_warn("[pnlfs] --- mkdir ---\n");
		pr_warn("[pnlfs] Filename too long\n");
		return -ENAMETOOLONG;
	}
	inode = pnl_new_inode(dir, S_IFDIR | mode, &err);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		return err;
	}
	bh = sb_bread(inode->i_sb, i_info->index_block); 
	if(!bh) {
		pr_warn("[pnlfs] --- mkdir ---\n");
		pr_warn("[pnlfs] sb_bread failed\n");
		iput(inode);
		return -ENOMEM;
	}
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	idx = pnl_find_dir_entry(dir_block, dentry);
	if (idx != PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- mkdir ---\n");
		pr_warn("[pnlfs] File exists\n");
		brelse(bh);
		iput(inode);
		return -EEXIST;
	}
	idx = pnl_free_dir_entry(dir_block);
	if (idx == PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- mkdir ---\n");
		pr_warn("[pnlfs] Exceeded max entries in directory\n");
		brelse(bh);
		iput(inode);
		return -ENOSPC;
	}
	dst = dir_block->files[idx].filename;
	src = dentry->d_name.name;
	if(!strncpy(dst, src, PNLFS_FILENAME_LEN)) {
		pr_warn("[pnlfs] --- mkdir ---\n");
		pr_warn("[pnlfs] Filename copy into dir_block failed\n");
		brelse(bh);
		iput(inode);
		return -ENOMEM;
	}
	dir_block->files[idx].inode = cpu_to_le32(inode->i_ino);
	brelse(bh);
	i_info->nr_entries++;
	inode_inc_link_count(dir);
	inode_inc_link_count(inode);
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	pr_info("[pnlfs] pnl_mkdir() : success\n");
	pr_info("[pnlfs] inode : <%p>\n", inode);
	pr_info("[pnlfs] i_info : <%p>\n", i_info);
	pr_info("[pnlfs] i_mode : %d\n", inode->i_mode);
	pr_info("[pnlfs] i_ino : %ld\n", inode->i_ino);
	pr_info("[pnlfs] i_size : %lld\n", inode->i_size);
	pr_info("[pnlfs] i_blocks : %ld\n", inode->i_blocks);
	pr_info("[pnlfs] index_block : %d\n", i_info->index_block);
	pr_info("[pnlfs] nr_entries : %d\n", i_info->nr_entries);
	return 0;
}

int pnl_rmdir(struct inode *dir, struct dentry *dentry)
{
	uint32_t idx = 0, dir_index, ino;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct pnlfs_dir_block *dir_block;
	struct pnlfs_inode_info *dir_info, *i_info;
	struct inode *inode;
	struct pnlfs_sb_info *sb_info;
	const char * name = dentry->d_name.name;
	unsigned long *ifree_bitmap, *bfree_bitmap;
	dir_info = container_of(dir, struct pnlfs_inode_info, vfs_inode);
	dir_index = dir_info->index_block;
	bh = sb_bread(sb, dir_index);
	if (!bh) {
		pr_warn("[pnlfs] --- unlink ---\n");
		pr_warn("[pnlfs] sb_read() failed\n");
		brelse(bh);
		return -EIO;
	}
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	idx = pnl_find_dir_entry(dir_block, dentry);
	if (idx == PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- unlink ---\n");
		pr_warn("[pnlfs] %s doesn't exist\n", name);
		brelse(bh);
		return -ENOENT;
	}
	sb_info = (struct pnlfs_sb_info *) sb->s_fs_info;
	ino = le32_to_cpu(dir_block->files[idx].inode);
	dir_block->files[idx].inode = cpu_to_le32(0);
	brelse(bh);
	inode = pnl_iget(sb, ino);
	i_info = container_of(inode, struct pnlfs_inode_info, vfs_inode);
	ifree_bitmap = sb_info->ifree_bitmap;
	bfree_bitmap = sb_info->bfree_bitmap;
	bitmap_set(ifree_bitmap, inode->i_ino, 1);
	bitmap_set(bfree_bitmap, i_info->index_block, 1);
	inode_dec_link_count(inode);
	inode_dec_link_count(dir);
	iput(inode);
	dir_info->nr_entries--;
	return 0;
}

int pnl_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry,
	       unsigned int flags)
{
	struct pnlfs_file *file, *file2;
	struct buffer_head *bh, *bh2;
	struct pnlfs_sb_info *sb_info;
	struct pnlfs_dir_block *dir_block, *dir_block2;
	struct pnlfs_inode_info *i_info, *ni_info;
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode;
	unsigned long *ifree_bitmap;
	const char *name;
	uint32_t idx, idx2, err;
	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;
	sb_info = (struct pnlfs_sb_info *) old_inode->i_sb->s_fs_info;
	i_info = container_of(old_dir, struct pnlfs_inode_info, vfs_inode);
	
	bh = sb_bread(old_inode->i_sb, i_info->index_block);
	if (!bh) {
		pr_warn("[pnlfs] --- rename ---\n");
		pr_warn("[pnlfs] old_dir does not exist\n");
		return -EEXIST;
	}
	name = old_dentry->d_name.name;
	dir_block = (struct pnlfs_dir_block *) bh->b_data;
	idx = pnl_find_dir_entry(dir_block, old_dentry);
	if (idx == PNLFS_MAX_DIR_ENTRIES)
	{
		pr_warn("[pnlfs] --- rename ---\n");
		pr_warn("[pnlfs] old_dentry %s does not exist\n", name);
		brelse(bh);
		return idx;
	}
	new_inode = pnl_new_inode(new_dir, old_inode->i_mode, &err);
	if (IS_ERR(new_inode))
	{
		pr_warn("[pnlfs] --- rename ---\n");
		pr_warn("[pnlfs] error when creating new_inode\n");
		brelse(bh);
		return -EIO;
	}

	name = new_dentry->d_name.name;
	file = &dir_block->files[idx];
	if (old_dir == new_dir) {
		if (!strncpy(file->filename, name, PNLFS_FILENAME_LEN))
		{
			pr_warn("[pnlfs] --- rename ---\n");
			pr_warn("[pnlfs] error when adding entry %s\n", name);
			brelse(bh);
			return -EIO;
		}
		file->inode = cpu_to_le32(new_inode->i_ino);
		brelse(bh);
		new_inode->i_size = old_inode->i_size;
		i_info = container_of(old_inode, struct pnlfs_inode_info,
				vfs_inode);
		ni_info = container_of(new_inode, struct pnlfs_inode_info,
				vfs_inode);
		ni_info->nr_entries = i_info->nr_entries;
		ni_info->index_block = i_info->index_block;
		ifree_bitmap = sb_info->ifree_bitmap;
		bitmap_set(ifree_bitmap, old_inode->i_ino, 1);
		mark_inode_dirty(old_inode);
		iput(old_inode);
		mark_inode_dirty(new_inode);
		d_instantiate(new_dentry, new_inode);
		return 0;
	}
	ni_info = container_of(new_dir, struct pnlfs_inode_info, vfs_inode);
	bh2 = sb_bread(new_dir->i_sb, ni_info->index_block);
	if (!bh2)
	{
		pr_warn("[pnlfs] --- rename ---\n");
		pr_warn("[pnlfs] opening block no %d failed\n",
				ni_info->index_block);
		brelse(bh2);
		brelse(bh);
		return -EIO;
	}
	dir_block2 = (struct pnlfs_dir_block *) bh2->b_data;
	idx2 = pnl_free_dir_entry(dir_block2);
	if (idx2 == PNLFS_MAX_DIR_ENTRIES) {
		pr_warn("[pnlfs] --- rename ---\n");
		pr_warn("[pnlfs] old_dentry %s does not exist\n", name);
		brelse(bh2);
		brelse(bh);
		return idx;
	}
	file2 = &dir_block2->files[idx2];
	if(!strncpy(file2->filename, name, PNLFS_FILENAME_LEN))
	{
		pr_warn("[pnlfs] --- rename ---\n");
		pr_warn("[pnlfs] error when adding dentry %s\n", name);
		brelse(bh2);
		brelse(bh);
		return -EIO;
	}
	file2->inode = cpu_to_le32(new_inode->i_ino);
	file->inode = cpu_to_le32(0);
	brelse(bh2);
	brelse(bh);
	ni_info->nr_entries++;
	i_info->nr_entries--;
	new_inode->i_size = old_inode->i_size;
	i_info = container_of(old_inode, struct pnlfs_inode_info, vfs_inode);
	ni_info = container_of(new_inode, struct pnlfs_inode_info, vfs_inode);
	ni_info->nr_entries = i_info->nr_entries;
	ni_info->index_block = i_info->index_block;
	ifree_bitmap = sb_info->ifree_bitmap;
	bitmap_set(ifree_bitmap, old_inode->i_ino, 1);
	mark_inode_dirty(old_inode);
	iput(old_inode);
	mark_inode_dirty(new_inode);
	d_instantiate(new_dentry, new_inode);
	return 0;
}
