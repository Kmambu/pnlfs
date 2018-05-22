#ifndef _PNL_IFOPS_H
#define _PNL_IFOPS_H
int pnl_readdir(struct file *file, struct dir_context *ctx);
ssize_t pnl_read(struct file *filp, char __user *buf, size_t size,
		loff_t *off);
ssize_t pnl_write(struct file *filp, const char __user *buf, size_t size,
		loff_t *off);

#endif
