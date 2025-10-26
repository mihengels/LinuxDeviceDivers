ifneq ($(KERNELRELEASE),)
# In kbuild context
module-objs := scull_ring_buffer.o
obj-m := scull_ring_buffer.o

CFLAGS_scull_ring_buffer.o := -DDEBUG

else
# In normal make context
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: modules
modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

endif
