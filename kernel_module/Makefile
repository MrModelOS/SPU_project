# SPU kernel module Makefile
# Compatible with Arch Linux (linux-headers required)

obj-m += spu_driver.o

PWD       := $(shell pwd)
KVER      := $(shell uname -r)
KDIR      := /lib/modules/$(KVER)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Convenience targets
load:
	sudo insmod spu_driver.ko emulation=1
unload:
	sudo rmmod spu_driver
reload: unload load

test: $(PWD)/test_userspace
	$(PWD)/test_userspace

$(PWD)/test_userspace: test_userspace.c ../emulator/spu_device.h
	$(CC) -Wall -Wextra -O2 -std=c11 -o $@ $<
