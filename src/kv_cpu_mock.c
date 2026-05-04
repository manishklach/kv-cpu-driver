// SPDX-License-Identifier: GPL-2.0-only
/*
 * kv_cpu_mock.c — Hardware Emulation (Mock Mode) for KV-CPU
 *
 * Simulates the HEPC decision engine and NMCE compute ops in software
 * when no physical CXL hardware is present.
 */

#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "../include/kv_cpu.h"
#include "kv_cpu_ioctl.h"

struct kvcpu_mock_state {
	struct kvcpu_dev *kv;
	u64 current_step;
	bool step_pending;
	wait_queue_head_t wq;
};

static int kvcpu_mock_thread(void *data)
{
	struct kvcpu_mock_state *ms = data;
	struct kvcpu_dev *kv = ms->kv;

	pr_info("kv_cpu: mock thread started for kvcpu%d\n", MINOR(kv->cdev.dev));

	while (!kthread_should_stop()) {
		/* Wait for a step advance signal */
		wait_event_interruptible(ms->wq, ms->step_pending || kthread_should_stop());
		if (kthread_should_stop())
			break;

		ms->step_pending = false;
		
		/* 1. Simulate HEPC SCAN phase */
		kvcpu_writeq(kv, KVCPU_REG_HEPC_STATUS, HEPC_STATUS_SCAN);
		usleep_range(100, 200); /* Simulate hardware latency */

		/* 2. Simulate random evictions/prefetches based on step */
		kvcpu_writeq(kv, KVCPU_REG_HEPC_STATUS, HEPC_STATUS_EVICT);
		{
			u64 evicts, prefetches;
			get_random_bytes(&evicts, sizeof(evicts));
			get_random_bytes(&prefetches, sizeof(prefetches));
			evicts %= 10;
			prefetches %= 5;

			kv->stat_evictions += evicts;
			kv->stat_prefetches += prefetches;

			/* Update telemetry registers */
			kvcpu_writeq(kv, KVCPU_REG_EVICT_COUNT, kv->stat_evictions);
			kvcpu_writeq(kv, KVCPU_REG_PREFETCH_CNT, kv->stat_prefetches);
		}
		usleep_range(50, 100);

		/* 3. Back to IDLE */
		kvcpu_writeq(kv, KVCPU_REG_HEPC_STATUS, HEPC_STATUS_IDLE);
		
		/* 4. Trigger a mock interrupt */
		/* In a real system, the ISR would handle this. Here we just log. */
		dev_dbg(kv->dev, "mock: HEPC cycle complete for step %llu\n", ms->current_step);
	}

	return 0;
}

int kvcpu_mock_init(struct kvcpu_dev *kv)
{
	struct kvcpu_mock_state *ms;

	ms = kzalloc(sizeof(*ms), GFP_KERNEL);
	if (!ms)
		return -ENOMEM;

	ms->kv = kv;
	init_waitqueue_head(&ms->wq);
	
	/* Store mock state in the device pointer (we'll reuse mock_thread pointer) */
	kv->mock_thread = kthread_run(kvcpu_mock_thread, ms, "kvcpu_mock/%d", MINOR(kv->cdev.dev));
	if (IS_ERR(kv->mock_thread)) {
		kfree(ms);
		return PTR_ERR(kv->mock_thread);
	}

	kv->mock_state = ms;
	return 0;
}

void kvcpu_mock_teardown(struct kvcpu_dev *kv)
{
	struct kvcpu_mock_state *ms = (struct kvcpu_mock_state *)kv->mock_state;

	if (kv->mock_thread) {
		kthread_stop(kv->mock_thread);
	}
	kfree(ms);
}

/* Entry point for STEP_ADVANCE in mock mode */
void kvcpu_mock_step_advance(struct kvcpu_dev *kv, u64 step)
{
	struct kvcpu_mock_state *ms = (struct kvcpu_mock_state *)kv->mock_state;

	if (!ms) return;

	ms->current_step = step;
	ms->step_pending = true;
	wake_up_interruptible(&ms->wq);
}

/* Emulate NMCE compute job */
int kvcpu_mock_nmce_submit(struct kvcpu_dev *kv, struct kvcpu_sqe *sqe)
{
	/* Simulate DMA and compute delay */
	usleep_range(10, 20);
	
	kv->stat_nmce_ops++;
	kvcpu_writeq(kv, KVCPU_REG_NMCE_OPS, kv->stat_nmce_ops);
	
	/* In a real mock, we would write results to sqe->score_phys. 
	 * For now, we just simulate the operation completion. */
	
	return 0;
}
