# SPDX-License-Identifier: GPL-2.0-only
#
# Root Makefile for KV-CPU Reference Driver
#

KDIR ?= /lib/modules/$(shell uname -r)/build
MOD_DIR = drivers/misc/kv_cpu
TEST_DIR = tools/test

all: modules tools/kvctl $(TEST_DIR)/kvcpu_mock_test

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MOD_DIR) CONFIG_KV_CPU=m modules

tools/kvctl: tools/kvctl.c
	$(CC) -O2 -o $@ $<

$(TEST_DIR)/kvcpu_mock_test: $(TEST_DIR)/kvcpu_mock_test.c
	$(CC) -O2 -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MOD_DIR) clean
	rm -f tools/kvctl $(TEST_DIR)/kvcpu_mock_test

.PHONY: all modules clean
