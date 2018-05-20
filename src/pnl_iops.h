#ifndef _PNL_IOPS_H
#define _PNL_IOPS_H
struct inode *pnl_new_inode(struct inode *dir, umode_t mode, int *error);
int pnl_find_dir_entry(struct pnlfs_dir_block *dir_block,
		struct dentry *dentry);
int pnl_free_dir_entry(struct pnlfs_dir_block *dir_block);
struct dentry *pnl_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags);
int pnl_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl);
int pnl_unlink(struct inode *dir, struct dentry *dentry);
int pnl_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
int pnl_rmdir(struct inode *dir, struct dentry *dentry);
int pnl_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry,
	       unsigned int flags);


#endif
