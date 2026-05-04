# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile — KV-CPU Reference Linux Driver
#

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m := kv_cpu.o

kv_cpu-y := src/kv_cpu_main.o  \
             src/kv_cpu_ioctl.o \
             src/kv_cpu_mmio.o  \
             src/kv_cpu_dma.o   \
             src/kv_cpu_policy.o

ccflags-y := -I$(src)/include -Wall -Wextra

all: modules tools/kvctl

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

tools/kvctl: tools/kvctl.c
	$(CC) -O2 -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f tools/kvctl

.PHONY: all modules clean
