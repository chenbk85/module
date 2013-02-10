ifneq ($(KERNELRELEASE),)
	obj-m := fable.o
else
	KERNELDIR ?= /usr/src/linux/
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif
