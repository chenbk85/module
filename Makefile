ifneq ($(KERNELRELEASE),)
	obj-m := fable.o
else
	KERNELDIR ?= /home/zausiu/sksm/
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif
