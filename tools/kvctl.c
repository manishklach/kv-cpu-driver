/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "../include/uapi/linux/kv_cpu.h"

#define DEV_PATH "/dev/kvcpu0"

void print_usage(const char *prog) {
	printf("Usage: %s <cmd> <args>\n", prog);
	printf("  step <n>           Advance to decode step n\n");
	printf("  hot <va> <len>     Mark range as hot\n");
	printf("  evict <va> <len>   Mark range for eviction\n");
	printf("  prefetch <va> <len> <step>  Hint prefetch for future step\n");
	printf("  share <va> <len>   Mark range as shared prefix\n");
}

int main(int argc, char *argv[]) {
	int fd;
	if (argc < 3) {
		print_usage(argv[0]);
		return 1;
	}

	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		perror("open " DEV_PATH);
		return 1;
	}

	if (strcmp(argv[1], "step") == 0) {
		struct kv_cpu_step_info step = { .step = strtoull(argv[2], NULL, 0) };
		if (ioctl(fd, KV_CPU_STEP_ADVANCE, &step) < 0) perror("ioctl STEP");
	} else if (strcmp(argv[1], "hot") == 0) {
		struct kv_cpu_block_info block = {
			.va = strtoull(argv[2], NULL, 0),
			.len = strtoull(argv[3], NULL, 0)
		};
		if (ioctl(fd, KV_CPU_MARK_HOT, &block) < 0) perror("ioctl HOT");
	} else if (strcmp(argv[1], "evict") == 0) {
		struct kv_cpu_block_info block = {
			.va = strtoull(argv[2], NULL, 0),
			.len = strtoull(argv[3], NULL, 0)
		};
		if (ioctl(fd, KV_CPU_EVICT, &block) < 0) perror("ioctl EVICT");
	} else if (strcmp(argv[1], "prefetch") == 0 && argc == 5) {
		struct kv_cpu_block_info block = {
			.va = strtoull(argv[2], NULL, 0),
			.len = strtoull(argv[3], NULL, 0),
			.target_step = strtoull(argv[4], NULL, 0)
		};
		if (ioctl(fd, KV_CPU_PREFETCH, &block) < 0) perror("ioctl PREFETCH");
	} else if (strcmp(argv[1], "share") == 0) {
		struct kv_cpu_block_info block = {
			.va = strtoull(argv[2], NULL, 0),
			.len = strtoull(argv[3], NULL, 0)
		};
		if (ioctl(fd, KV_CPU_SHARE_PREFIX, &block) < 0) perror("ioctl SHARE");
	} else {
		print_usage(argv[0]);
	}

	close(fd);
	return 0;
}
