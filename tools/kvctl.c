/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "../include/uapi/linux/kv_cpu.h"

#define DEV_PATH "/dev/kvcpu0"

static void print_usage(const char *prog)
{
	printf("Usage: %s <cmd> <args>\n", prog);
	printf("  step <n>           Advance to decode step n\n");
	printf("  hot <va> <len>     Mark range as hot\n");
	printf("  evict <va> <len>   Mark range for eviction\n");
	printf("  prefetch <va> <len> <step>  Hint prefetch for future step\n");
	printf("  share <va> <len>   Mark range as shared prefix\n");
}

static bool parse_u64(const char *arg, unsigned long long *out)
{
	char *end;

	errno = 0;
	*out = strtoull(arg, &end, 0);
	if (errno != 0 || end == arg || *end != '\0')
		return false;

	return true;
}

int main(int argc, char *argv[])
{
	int fd, ret = 0;
	unsigned long long va, len, step;

	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		perror("open " DEV_PATH);
		return 1;
	}

	if (strcmp(argv[1], "step") == 0) {
		struct kv_cpu_step_info step_info;

		if (argc != 3 || !parse_u64(argv[2], &step)) {
			print_usage(argv[0]);
			ret = 1;
			goto out;
		}

		step_info.step = step;
		if (ioctl(fd, KV_CPU_STEP_ADVANCE, &step_info) < 0) {
			perror("ioctl STEP");
			ret = 1;
		}
	} else if (strcmp(argv[1], "hot") == 0) {
		struct kv_cpu_block_info block = { 0 };

		if (argc != 4 || !parse_u64(argv[2], &va) || !parse_u64(argv[3], &len)) {
			print_usage(argv[0]);
			ret = 1;
			goto out;
		}

		block.va = va;
		block.len = len;
		if (ioctl(fd, KV_CPU_MARK_HOT, &block) < 0) {
			perror("ioctl HOT");
			ret = 1;
		}
	} else if (strcmp(argv[1], "evict") == 0) {
		struct kv_cpu_block_info block = { 0 };

		if (argc != 4 || !parse_u64(argv[2], &va) || !parse_u64(argv[3], &len)) {
			print_usage(argv[0]);
			ret = 1;
			goto out;
		}

		block.va = va;
		block.len = len;
		if (ioctl(fd, KV_CPU_EVICT, &block) < 0) {
			perror("ioctl EVICT");
			ret = 1;
		}
	} else if (strcmp(argv[1], "prefetch") == 0) {
		struct kv_cpu_block_info block = { 0 };

		if (argc != 5 || !parse_u64(argv[2], &va) || !parse_u64(argv[3], &len) ||
		    !parse_u64(argv[4], &step)) {
			print_usage(argv[0]);
			ret = 1;
			goto out;
		}

		block.va = va;
		block.len = len;
		block.target_step = step;
		if (ioctl(fd, KV_CPU_PREFETCH, &block) < 0) {
			perror("ioctl PREFETCH");
			ret = 1;
		}
	} else if (strcmp(argv[1], "share") == 0) {
		struct kv_cpu_block_info block = { 0 };

		if (argc != 4 || !parse_u64(argv[2], &va) || !parse_u64(argv[3], &len)) {
			print_usage(argv[0]);
			ret = 1;
			goto out;
		}

		block.va = va;
		block.len = len;
		if (ioctl(fd, KV_CPU_SHARE_PREFIX, &block) < 0) {
			perror("ioctl SHARE");
			ret = 1;
		}
	} else {
		print_usage(argv[0]);
		ret = 1;
	}

out:
	close(fd);
	return ret;
}
