/**
 * kvctl.c — Control utility for KV-CPU Reference Driver
 * 
 * Demonstrates how a userspace LLM runtime interacts with the kernel control plane.
 *
 * Usage:
 *   kvctl step <n>
 *   kvctl hot <addr> <len>
 *   kvctl evict <addr> <len>
 *   kvctl prefetch <addr> <len> <step>
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#include "../include/uapi/linux/kv_cpu.h"

#define DEV_PATH "/dev/kvcpu0"

void print_usage(char *prog) {
	printf("Usage: %s <cmd> <args...>\n", prog);
	printf("  step <n>                       - Signal current decode step\n");
	printf("  hot <addr> <len>               - Mark range as hot (priority boost)\n");
	printf("  evict <addr> <len>             - Force eviction to slow tier\n");
	printf("  prefetch <addr> <len> <step>   - Hint prefetch for future step\n");
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	int fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		perror("open " DEV_PATH);
		return 1;
	}

	char *cmd = argv[1];

	if (strcmp(cmd, "step") == 0 && argc == 3) {
		struct kv_step_info si = { .step = strtoull(argv[2], NULL, 0) };
		if (ioctl(fd, KV_CPU_STEP_ADVANCE, &si) < 0) perror("ioctl STEP_ADVANCE");
		else printf("Signaled decode step %llu\n", si.step);
	} 
	else if (strcmp(cmd, "hot") == 0 && argc == 4) {
		struct kv_block_range br = {
			.start = strtoull(argv[2], NULL, 0),
			.len = strtoull(argv[3], NULL, 0)
		};
		if (ioctl(fd, KV_CPU_MARK_HOT, &br) < 0) perror("ioctl MARK_HOT");
		else printf("Marked 0x%llx + %llu as HOT\n", br.start, br.len);
	}
	else if (strcmp(cmd, "evict") == 0 && argc == 4) {
		struct kv_block_range br = {
			.start = strtoull(argv[2], NULL, 0),
			.len = strtoull(argv[3], NULL, 0)
		};
		if (ioctl(fd, KV_CPU_EVICT, &br) < 0) perror("ioctl EVICT");
		else printf("Requested eviction for 0x%llx + %llu\n", br.start, br.len);
	}
	else {
		print_usage(argv[0]);
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}
