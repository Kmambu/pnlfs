.PHONY: all 
#.SECONDARY:

ifneq ($(KERNELRELEASE),)

  obj-m += pnlfs.o
  ccflags-y := -I. -DDEBUG -Og
  pnlfs-objs := pnl_inode.o pnl_iops.o pnl_ifops.o register_pnlfs.o

else
	
  # KERNELDIR ?= /lib/modules/$(shell uname -r)/build
  # KERNELDIR ?= /home/kevin/Documents/M1S2-Cours---Notes---Exercices/PNL/linux-4.9.82
  KERNELDIR ?= ../../sources/linux-4.9.83
  PWD := $(shell pwd)

all :
	make -C $(KERNELDIR) M=$(PWD) modules
clean:
	make -C $(KERNELDIR) M=$(PWD) clean

endif

