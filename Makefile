# SPDX-License-Identifier: GPL-2.0-only
#
# Root Makefile for KV-CPU Reference Driver
#

KDIR ?= /lib/modules/$(shell uname -r)/build
MOD_DIR = drivers/misc/kv_cpu

all: modules tools/kvctl

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MOD_DIR) CONFIG_KV_CPU=m modules

tools/kvctl: tools/kvctl.c
	$(CC) -O2 -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MOD_DIR) clean
	rm -f tools/kvctl

.PHONY: all modules clean
