# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile — KV-CPU Linux Kernel Driver
#
# Build:
#   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
#
# Install:
#   sudo make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
#   sudo depmod -a
#   sudo modprobe kv_cpu
#
# Out-of-tree build against a specific kernel source tree:
#   make KDIR=/path/to/linux M=$(pwd) modules

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m := kv_cpu.o

kv_cpu-y := src/kv_cpu_main.o   \
             src/kv_cpu_hepc.o   \
             src/kv_cpu_nmce.o   \
             src/kv_cpu_mem.o    \
             src/kv_cpu_madvise.o\
             src/kv_cpu_ioring.o \
             src/kv_cpu_rtbd.o   \
             src/kv_cpu_sysfs.o

ccflags-y := -I$(src)/include -Wall -Wextra -Wno-unused-parameter

# Optional: enable io_uring upstream opcode registration
# (requires patched kernel — see tools/patches/0002-ioring-kv.patch)
# ccflags-y += -DCONFIG_KVCPU_IORING_UPSTREAM

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

modules_install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

.PHONY: all clean modules_install
