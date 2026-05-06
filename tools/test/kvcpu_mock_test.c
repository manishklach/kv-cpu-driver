/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../../include/uapi/linux/kv_cpu.h"

#define DEV_PATH "/dev/kvcpu0"

static int run_step_test(int fd)
{
	struct kv_cpu_step_info step = {
		.step = 128,
	};

	if (ioctl(fd, KV_CPU_STEP_ADVANCE, &step) < 0) {
		perror("KV_CPU_STEP_ADVANCE");
		return 1;
	}

	return 0;
}

static int run_hot_test(int fd)
{
	struct kv_cpu_block_info block = {
		.va = 0x7f001000ULL,
		.len = 4096,
	};

	if (ioctl(fd, KV_CPU_MARK_HOT, &block) < 0) {
		perror("KV_CPU_MARK_HOT");
		return 1;
	}

	return 0;
}

static int run_evict_test(int fd)
{
	struct kv_cpu_block_info block = {
		.va = 0x7f002000ULL,
		.len = 8192,
	};

	if (ioctl(fd, KV_CPU_EVICT, &block) < 0) {
		perror("KV_CPU_EVICT");
		return 1;
	}

	return 0;
}

static int run_prefetch_test(int fd)
{
	struct kv_cpu_block_info block = {
		.va = 0x7f003000ULL,
		.len = 4096,
		.target_step = 256,
	};

	if (ioctl(fd, KV_CPU_PREFETCH, &block) < 0) {
		perror("KV_CPU_PREFETCH");
		return 1;
	}

	return 0;
}

static int run_share_test(int fd)
{
	struct kv_cpu_block_info block = {
		.va = 0x7f004000ULL,
		.len = 16384,
	};

	if (ioctl(fd, KV_CPU_SHARE_PREFIX, &block) < 0) {
		perror("KV_CPU_SHARE_PREFIX");
		return 1;
	}

	return 0;
}

static int run_weights_test(int fd)
{
	struct kv_cpu_weights_info weights = {
		.w_r = 3,
		.w_f = 5,
		.w_s = 7,
		.w_d = 11,
		.evict_thresh = 1024,
		.prefetch_thresh = 40960,
	};

	if (ioctl(fd, KV_CPU_SET_WEIGHTS, &weights) < 0) {
		perror("KV_CPU_SET_WEIGHTS");
		return 1;
	}

	return 0;
}

static int run_telemetry_test(int fd)
{
	struct kv_cpu_telemetry_info telemetry;

	memset(&telemetry, 0, sizeof(telemetry));
	if (ioctl(fd, KV_CPU_GET_TELEMETRY, &telemetry) < 0) {
		perror("KV_CPU_GET_TELEMETRY");
		return 1;
	}

	if (telemetry.step_count != 1 || telemetry.hot_count != 1 ||
	    telemetry.evict_count != 1 || telemetry.prefetch_count != 1 ||
	    telemetry.share_count != 1) {
		fprintf(stderr, "Unexpected telemetry counters: step=%llu hot=%llu evict=%llu prefetch=%llu share=%llu\n",
			telemetry.step_count, telemetry.hot_count, telemetry.evict_count,
			telemetry.prefetch_count, telemetry.share_count);
		return 1;
	}

	if (telemetry.w_r != 3 || telemetry.w_f != 5 || telemetry.w_s != 7 ||
	    telemetry.w_d != 11 || telemetry.evict_thresh != 1024 ||
	    telemetry.prefetch_thresh != 40960) {
		fprintf(stderr, "Unexpected runtime weights in telemetry snapshot\n");
		return 1;
	}

	return 0;
}

static int run_invalid_length_test(int fd)
{
	struct kv_cpu_block_info block = {
		.va = 0x7f005000ULL,
		.len = 0,
	};

	errno = 0;
	if (ioctl(fd, KV_CPU_MARK_HOT, &block) == 0) {
		fprintf(stderr, "KV_CPU_MARK_HOT unexpectedly accepted zero length\n");
		return 1;
	}

	if (errno != EINVAL) {
		fprintf(stderr, "KV_CPU_MARK_HOT returned errno=%d, expected %d\n",
			errno, EINVAL);
		return 1;
	}

	return 0;
}

int main(void)
{
	int fd;
	int rc = 0;

	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		perror("open " DEV_PATH);
		return 1;
	}

	rc |= run_step_test(fd);
	rc |= run_hot_test(fd);
	rc |= run_evict_test(fd);
	rc |= run_prefetch_test(fd);
	rc |= run_share_test(fd);
	rc |= run_weights_test(fd);
	rc |= run_telemetry_test(fd);
	rc |= run_invalid_length_test(fd);

	if (rc == 0)
		printf("kvcpu mock ioctl smoke tests passed\n");

	close(fd);
	return rc ? 1 : 0;
}
