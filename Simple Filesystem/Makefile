ifneq ($(KERNELRELEASE),) 
obj-m := lab5fs.o
lab5fs-objs := inode.o dir.o file.o
else 

KERNELDIR ?= /lib/modules/$(shell uname -r)/build 

PWD := $(shell pwd)

default:
	gcc mklab5fs.c -o mklab5fs
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules 
endif 
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -rf mklab5fs



