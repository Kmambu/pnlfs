#ifndef _PNL_INODE_H
#define _PNL_INODE_H
struct inode *pnl_iget(struct super_block *sb, unsigned long ino);
struct inode *pnl_alloc_inode(struct super_block *sb);
void pnl_destroy_inode(struct inode *inode);
#endif

