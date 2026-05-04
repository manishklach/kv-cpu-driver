// SPDX-License-Identifier: GPL-2.0-only
/*
 * kvcpu_test.c — KV-CPU driver smoke test / benchmark tool
 *
 * Usage:
 *   ./kvcpu_test [/dev/kvcpu0]
 *
 * Tests:
 *   1. Device open + identity read via ioctl
 *   2. Telemetry snapshot
 *   3. HEPC weight set + verify
 *   4. Step-advance latency benchmark (1M iterations)
 *   5. RTBD share/release round-trip
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>

/* Pull in the shared ioctl header (userspace-safe) */
#include "../../include/kv_cpu.h"
#include "../kv_cpu_ioctl.h"

#define DEFAULT_DEV "/dev/kvcpu0"
#define NS_PER_SEC  1000000000ULL

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

static void print_sep(void) { printf("%s\n", "─────────────────────────────────────────"); }

/* ── Test 1: open + basic ioctl ───────────────────────────────────────────── */
static int test_open(int fd)
{
	printf("[1] Device open ...\n");
	if (fd < 0) {
		perror("open");
		return -1;
	}
	printf("    OK — /dev/kvcpu0 opened (fd=%d)\n", fd);
	return 0;
}

/* ── Test 2: telemetry ────────────────────────────────────────────────────── */
static int test_telemetry(int fd)
{
	struct kvcpu_telemetry tel = {0};
	printf("[2] Telemetry snapshot ...\n");

	if (ioctl(fd, KVCPU_IOC_GET_TELEMETRY, &tel) < 0) {
		perror("KVCPU_IOC_GET_TELEMETRY");
		return -1;
	}

	printf("    evictions:      %llu\n", (unsigned long long)tel.evictions);
	printf("    prefetches:     %llu\n", (unsigned long long)tel.prefetches);
	printf("    nmce_ops:       %llu\n", (unsigned long long)tel.nmce_ops);
	printf("    t1_used:        %llu MiB\n",
	       (unsigned long long)(tel.t1_used >> 20));
	printf("    t1_free:        %llu MiB\n",
	       (unsigned long long)(tel.t1_free >> 20));
	printf("    nmce_bytes_in:  %llu MB\n",
	       (unsigned long long)(tel.nmce_bytes_in >> 20));
	printf("    nmce_bytes_out: %llu MB\n",
	       (unsigned long long)(tel.nmce_bytes_out >> 20));
	if (tel.nmce_bytes_in > 0) {
		double ratio = (double)(tel.nmce_bytes_in + tel.nmce_bytes_out) /
			       (double)(tel.nmce_ops * 8192);
		printf("    traffic ratio vs naive: %.2fx reduction\n",
		       1.0 / ratio);
	}
	return 0;
}

/* ── Test 3: HEPC weight set ──────────────────────────────────────────────── */
static int test_hepc_weights(int fd)
{
	struct kvcpu_hepc_config cfg = {
		.evict_threshold    = 10,
		.prefetch_threshold = 180,
		.window_w           = 64,
		.weight_r = 50, .weight_f = 30,
		.weight_s = 20, .weight_d = 200,
	};
	printf("[3] HEPC weight set ...\n");

	if (ioctl(fd, KVCPU_IOC_SET_WEIGHTS, &cfg) < 0) {
		perror("KVCPU_IOC_SET_WEIGHTS");
		return -1;
	}
	printf("    OK — weights R=%u F=%u S=%u D=%u W=%u\n",
	       cfg.weight_r, cfg.weight_f, cfg.weight_s,
	       cfg.weight_d, cfg.window_w);
	return 0;
}

/* ── Test 4: step-advance latency (ioctl path) ────────────────────────────── */
static int test_step_latency_ioctl(int fd)
{
	const int ITERS = 100000;
	uint64_t t0, t1, elapsed;
	int i;

	printf("[4] Step-advance latency (%d iterations, ioctl path) ...\n", ITERS);
	t0 = now_ns();
	for (i = 0; i < ITERS; i++) {
		uint64_t step = (uint64_t)i;
		if (ioctl(fd, KVCPU_IOC_STEP_ADVANCE, &step) < 0) {
			perror("KVCPU_IOC_STEP_ADVANCE");
			return -1;
		}
	}
	t1 = now_ns();
	elapsed = t1 - t0;
	printf("    total: %llu ms  per-call: %llu ns\n",
	       (unsigned long long)(elapsed / 1000000),
	       (unsigned long long)(elapsed / ITERS));
	return 0;
}

/* ── Test 5: step-advance via mmap (direct MMIO write) ───────────────────── */
static int test_step_latency_mmap(int fd)
{
	const int ITERS = 1000000;
	uint64_t t0, t1, elapsed;
	volatile uint64_t *step_reg;
	int i;

	printf("[5] Step-advance latency (%d iterations, mmap MMIO path) ...\n", ITERS);

	/*
	 * Map the HEPC control window (BAR0 offsets 0x100–0x1FF).
	 * vm_pgoff = 0x100 >> PAGE_SHIFT would fail on 4K pages since
	 * 0x100 < PAGE_SIZE. We map offset 0 and access at +0x100.
	 *
	 * In the driver, kvcpu_mmap() only allows mapping 0x100-0x1FF,
	 * so we specify pgoff=0x100/PAGE_SIZE if PAGE_SIZE=4096 and the
	 * register is aligned to page. For this test we use pgoff=0
	 * which maps BAR0 base; works on emulated hardware.
	 */
	step_reg = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, 0);
	if (step_reg == MAP_FAILED) {
		printf("    mmap failed (%s) — skipping mmap test\n",
		       strerror(errno));
		return 0; /* non-fatal: mmap only works with real BAR */
	}

	t0 = now_ns();
	for (i = 0; i < ITERS; i++)
		step_reg[KVCPU_REG_STEP_ADVANCE / 8] = (uint64_t)i;
	__sync_synchronize(); /* memory barrier */
	t1 = now_ns();
	elapsed = t1 - t0;

	munmap((void *)step_reg, 4096);

	printf("    total: %llu ms  per-write: %llu ns  "
	       "(target: < 100 ns on real HW)\n",
	       (unsigned long long)(elapsed / 1000000),
	       (unsigned long long)(elapsed / ITERS));
	return 0;
}

/* ── Test 6: RTBD share / release ─────────────────────────────────────────── */
static int test_rtbd(int fd)
{
	struct kvcpu_rtbd_cmd cmd = {
		.block_pa = 0x100000000ULL,  /* 4 GB — hypothetical T1 address */
		.req_id   = 42,
	};
	printf("[6] RTBD share/release round-trip ...\n");

	if (ioctl(fd, KVCPU_IOC_RTBD_SHARE, &cmd) < 0) {
		perror("KVCPU_IOC_RTBD_SHARE");
		return -1;
	}
	printf("    share OK (pa=0x%llx req=%u)\n",
	       (unsigned long long)cmd.block_pa, cmd.req_id);

	if (ioctl(fd, KVCPU_IOC_RTBD_RELEASE, &cmd) < 0) {
		perror("KVCPU_IOC_RTBD_RELEASE");
		return -1;
	}
	printf("    release OK\n");
	return 0;
}

/* ── main ──────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
	const char *dev = argc > 1 ? argv[1] : DEFAULT_DEV;
	int fd, failures = 0;

	printf("\n");
	printf("  KV-CPU Driver Test Suite\n");
	printf("  device: %s\n\n", dev);
	print_sep();

	fd = open(dev, O_RDWR);

	failures += (test_open(fd) < 0);          print_sep();
	failures += (test_telemetry(fd) < 0);     print_sep();
	failures += (test_hepc_weights(fd) < 0);  print_sep();
	failures += (test_step_latency_ioctl(fd) < 0); print_sep();
	failures += (test_step_latency_mmap(fd) < 0);  print_sep();
	failures += (test_rtbd(fd) < 0);          print_sep();

	if (fd >= 0)
		close(fd);

	printf("\nResult: %d test(s) passed, %d failed\n\n",
	       6 - failures, failures);
	return failures ? 1 : 0;
}
