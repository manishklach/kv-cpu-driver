/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * kv_cpu.h — KV-Cache Companion Processing Unit (KV-CPU)
 *
 * Hardware register map, RTBD entry layout, and internal driver structures.
 *
 * The KV-CPU is a CXL Type 1+3 device that:
 *   - Exposes on-board LPDDR5X as a CXL.mem memory region (Type 3 role)
 *   - Accepts NMCE compute descriptors and HEPC control via CXL.io BAR (Type 1 role)
 *
 * Register map lives in BAR0 (64-bit, 256 KB).
 * Memory region is registered with the kernel as a NUMA node via HMAT.
 *
 * Author: Manish Keshav Lachwani <mlachwani@gmail.com>
 */

#ifndef _KV_CPU_H
#define _KV_CPU_H

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/numa.h>
#include <linux/memory_hotplug.h>
#include <linux/io_uring.h>

/* ── PCI / CXL identity ─────────────────────────────────────────────────── */
#define KV_CPU_VENDOR_ID        0x1DE5   /* hypothetical — to be assigned by PCI-SIG */
#define KV_CPU_DEVICE_ID        0x0A10
#define KV_CPU_SUBSYS_VENDOR    0x1DE5
#define KV_CPU_SUBSYS_ID        0x0001

/* ── BAR0 register offsets (all 64-bit unless noted) ────────────────────── */

/* Identity & capability */
#define KVCPU_REG_IDENT         0x0000   /* RO: 0x4B56_4350_5F43_5055 "KVCPU_CPU" */
#define KVCPU_REG_VERSION       0x0008   /* RO: major[31:16] minor[15:0]            */
#define KVCPU_REG_CAP           0x0010   /* RO: capability flags                    */
#  define KVCPU_CAP_NMCE        BIT(0)   /*     Near-memory compute engine present  */
#  define KVCPU_CAP_HEPC        BIT(1)   /*     Hardware eviction controller present */
#  define KVCPU_CAP_RTBD        BIT(2)   /*     Request-tagged block directory present */
#  define KVCPU_CAP_PREFIX      BIT(3)   /*     Prefix sharing / ref-counting       */
#  define KVCPU_CAP_IORING      BIT(4)   /*     io_uring opcode registration        */

/* Memory geometry */
#define KVCPU_REG_MEM_SIZE      0x0020   /* RO: total T1 LPDDR5X capacity (bytes)  */
#define KVCPU_REG_MEM_BASE      0x0028   /* RO: host-physical base of T1 region    */
#define KVCPU_REG_RTBD_CAP      0x0030   /* RO: max RTBD entries supported          */
#define KVCPU_REG_BLOCK_SZ      0x0038   /* RO: default KV block size (bytes)      */

/* HEPC control — Pillar II */
#define KVCPU_REG_STEP_ADVANCE  0x0100   /* WO: write decode step t to trigger cycle*/
#define KVCPU_REG_EVICT_THRESH  0x0108   /* RW: 16-bit eviction threshold           */
#define KVCPU_REG_PREFETCH_THR  0x0110   /* RW: 16-bit prefetch threshold           */
#define KVCPU_REG_WINDOW_W      0x0118   /* RW: step-proximity window W             */
#define KVCPU_REG_WEIGHT_R      0x0120   /* RW: recency weight w_r  (8-bit)         */
#define KVCPU_REG_WEIGHT_F      0x0128   /* RW: frequency weight w_f (8-bit)        */
#define KVCPU_REG_WEIGHT_S      0x0130   /* RW: step-prox weight w_s (8-bit)        */
#define KVCPU_REG_WEIGHT_D      0x0138   /* RW: prefix dep weight w_d (8-bit)       */
#define KVCPU_REG_HEPC_STATUS   0x0140   /* RO: current HEPC phase + queue depths   */
#  define HEPC_STATUS_IDLE      0x0
#  define HEPC_STATUS_SCAN      0x1
#  define HEPC_STATUS_EVICT     0x2
#  define HEPC_STATUS_PREFETCH  0x3
#define KVCPU_REG_EVICT_COUNT   0x0148   /* RO: blocks evicted since last reset     */
#define KVCPU_REG_PREFETCH_CNT  0x0150   /* RO: blocks prefetched since last reset  */

/* Priority boost / immediate-evict — madvise control path */
#define KVCPU_REG_BOOST_ADDR    0x0200   /* WO: phys addr of block to set max prio  */
#define KVCPU_REG_BOOST_LEN     0x0208   /* WO: length; write BOOST_LEN triggers op */
#define KVCPU_REG_IMEVICT_ADDR  0x0210   /* WO: phys addr for immediate eviction    */
#define KVCPU_REG_IMEVICT_LEN   0x0218   /* WO: length; write triggers immediate evict */

/* NMCE submission / completion — Pillar I */
#define KVCPU_REG_SQ_BASE       0x0300   /* RW: host-phys addr of submission queue  */
#define KVCPU_REG_SQ_SIZE       0x0308   /* RW: number of SQ entries (power of 2)   */
#define KVCPU_REG_SQ_HEAD       0x0310   /* RO: device-side SQ head index           */
#define KVCPU_REG_SQ_TAIL       0x0318   /* RW: host writes tail to submit work     */
#define KVCPU_REG_CQ_BASE       0x0320   /* RW: host-phys addr of completion queue  */
#define KVCPU_REG_CQ_SIZE       0x0328   /* RW: number of CQ entries                */
#define KVCPU_REG_CQ_HEAD       0x0330   /* RW: host advances head after consuming  */
#define KVCPU_REG_CQ_TAIL       0x0338   /* RO: device writes tail when complete    */
#define KVCPU_REG_NMCE_STATUS   0x0340   /* RO: active descriptors / error flags    */

/* RTBD command interface — Pillar III */
#define KVCPU_REG_RTBD_CMD      0x0400   /* WO: command opcode (see RTBD_CMD_*)     */
#define KVCPU_REG_RTBD_REQ_ID   0x0408   /* RW: request_id for SHARE/RELEASE        */
#define KVCPU_REG_RTBD_BLKADDR  0x0410   /* RW: physical address of target block    */
#define KVCPU_REG_RTBD_STATUS   0x0418   /* RO: last command status                 */

#define RTBD_CMD_SHARE          0x01     /* increment ref_count for block           */
#define RTBD_CMD_RELEASE        0x02     /* decrement ref_count for block           */
#define RTBD_CMD_QUERY          0x03     /* read RTBD entry to RTBD_QUERY_* regs    */
#define RTBD_CMD_FLUSH          0x04     /* flush all entries for a request_id      */

/* RTBD query result registers (populated after RTBD_CMD_QUERY) */
#define KVCPU_REG_RTBD_TIER     0x0420   /* RO: tier_location (2-bit)               */
#define KVCPU_REG_RTBD_PRIO     0x0428   /* RO: current priority_score              */
#define KVCPU_REG_RTBD_REFCNT   0x0430   /* RO: reference_count                     */
#define KVCPU_REG_RTBD_ACCSTEP  0x0438   /* RO: access_step                         */

/* Interrupt control */
#define KVCPU_REG_IRQ_STATUS    0x0500   /* RW1C: interrupt status bits             */
#  define KVCPU_IRQ_EVICT_DONE  BIT(0)
#  define KVCPU_IRQ_PREFETCH_DN BIT(1)
#  define KVCPU_IRQ_NMCE_CQ     BIT(2)
#  define KVCPU_IRQ_ERROR       BIT(31)
#define KVCPU_REG_IRQ_MASK      0x0508   /* RW: interrupt enable mask               */

/* Telemetry */
#define KVCPU_REG_T1_USED       0x0600   /* RO: bytes of T1 currently allocated     */
#define KVCPU_REG_T1_FREE       0x0608   /* RO: bytes of T1 currently free          */
#define KVCPU_REG_NMCE_OPS      0x0610   /* RO: NMCE operations completed           */
#define KVCPU_REG_NMCE_BYTES_IN 0x0618   /* RO: bytes received from GPU (Q vectors) */
#define KVCPU_REG_NMCE_BYTES_OUT 0x0620  /* RO: bytes sent to GPU (score results)   */

/* ── NMCE Submission Queue Entry (SQE) — 64 bytes ──────────────────────── */
struct kvcpu_sqe {
	__u64 query_phys;       /* host-physical addr of query vector Q_h         */
	__u32 query_len;        /* D * sizeof(dtype) bytes                         */
	__u16 head_idx;         /* attention head index h                          */
	__u16 layer_idx;        /* transformer layer index                         */
	__u64 key_block_phys;   /* T1 physical addr of key block K[0..B-1]        */
	__u32 key_block_len;    /* B * D * sizeof(dtype) bytes                     */
	__u32 request_id;       /* owning inference request ID                     */
	__u64 score_phys;       /* host-physical addr for score output (B*2 bytes) */
	__u16 block_token_start;/* first token position in this block              */
	__u16 dtype;            /* 0=FP16, 1=BF16                                  */
	__u32 flags;            /* KVCPU_SQE_F_* flags                             */
	__u64 user_data;        /* opaque: echoed in CQE for correlation           */
} __packed;

#define KVCPU_SQE_F_SOFTMAX_EXP BIT(0)  /* apply exp() approximation on scores    */
#define KVCPU_SQE_F_SCALE       BIT(1)  /* apply 1/sqrt(D) scaling                */
#define KVCPU_SQE_F_MULTI_HEAD  BIT(2)  /* Q vector is for multiple heads (QVB)   */

/* ── NMCE Completion Queue Entry (CQE) — 16 bytes ──────────────────────── */
struct kvcpu_cqe {
	__u64 user_data;        /* echoed from SQE                                 */
	__u32 result;           /* 0 = success, else errno                         */
	__u32 score_count;      /* number of scores written to score_phys          */
} __packed;

/* ── RTBD entry as read by the driver (for sysfs / debug) ──────────────── */
struct kvcpu_rtbd_entry {
	__u16 request_id;
	__u8  layer_idx;
	__u8  head_idx;
	__u32 block_token_start;
	__u32 block_token_end;
	__u8  tier_location;    /* 0=GPU, 1=T1, 2=T2, 3=T3                        */
	__u8  is_prefix;
	__u8  ref_count;
	__u8  _pad;
	__u64 phys_addr;
	__u16 priority_score;
	__u32 access_step;
	__u32 dirty    : 1;
	__u32 _flags   : 31;
} __packed;

/* ── Driver private state ───────────────────────────────────────────────── */
#define KVCPU_SQ_ENTRIES        1024
#define KVCPU_CQ_ENTRIES        1024
#define KVCPU_MAX_REQUESTS      65536

struct kvcpu_queue {
	struct kvcpu_sqe *sq;
	struct kvcpu_cqe *cq;
	dma_addr_t        sq_dma;
	dma_addr_t        cq_dma;
	u32               sq_tail;
	u32               cq_head;
	spinlock_t        lock;
};

struct kvcpu_mem_region {
	phys_addr_t       base;          /* physical base of T1 LPDDR5X region     */
	resource_size_t   size;          /* total size in bytes                     */
	int               numa_node;     /* NUMA node ID assigned by kernel         */
	struct memory_dev_type *mtype;   /* memory tiering type descriptor          */
	struct resource   *res;          /* iomem resource                          */
};

struct kvcpu_hepc_config {
	u16  evict_threshold;
	u16  prefetch_threshold;
	u32  window_w;
	u8   weight_r, weight_f, weight_s, weight_d;
};

struct kvcpu_dev {
	struct pci_dev           *pdev;
	void __iomem             *bar;          /* BAR0 MMIO base                 */
	struct cdev               cdev;
	struct device            *dev;
	struct class             *cls;
	int                       major;

	/* Memory tier (Pillar IV / HMAT) */
	struct kvcpu_mem_region   mem;

	/* HEPC defaults */
	struct kvcpu_hepc_config  hepc;

	/* NMCE queues */
	struct kvcpu_queue        queue;

	/* io_uring ops */
	bool                      ioring_registered;

	/* Telemetry */
	u64                       stat_evictions;
	u64                       stat_prefetches;
	u64                       stat_nmce_ops;

	/* IRQ */
	int                       irq;

	/* sysfs */
	struct kobject           *kobj;

	/* Mock Mode state */
	bool                      is_mock;
	void                     *mock_bar_mem;  /* Page allocated for mock BAR */
	void                     *mock_t1_mem;   /* vmalloc region for mock T1 */
	struct task_struct       *mock_thread;   /* Thread to simulate HEPC logic */
	void                     *mock_state;    /* Internal state for mock logic */
};

/* ── Register accessors (MMIO) ──────────────────────────────────────────── */
static inline u64 kvcpu_readq(struct kvcpu_dev *kv, u32 off)
{
	if (unlikely(kv->is_mock))
		return *(u64 *)(kv->mock_bar_mem + off);
	return readq(kv->bar + off);
}

static inline void kvcpu_writeq(struct kvcpu_dev *kv, u32 off, u64 val)
{
	if (unlikely(kv->is_mock)) {
		*(u64 *)(kv->mock_bar_mem + off) = val;
		return;
	}
	writeq(val, kv->bar + off);
}

/* ── Function prototypes (implemented across source files) ──────────────── */

/* kv_cpu_hepc.c */
int  kvcpu_hepc_init(struct kvcpu_dev *kv);
void kvcpu_hepc_step_advance(struct kvcpu_dev *kv, u64 step);
void kvcpu_hepc_boost_range(struct kvcpu_dev *kv, phys_addr_t pa, size_t len);
void kvcpu_hepc_evict_range(struct kvcpu_dev *kv, phys_addr_t pa, size_t len);
void kvcpu_hepc_prefetch_hint(struct kvcpu_dev *kv, phys_addr_t pa,
			      size_t len, u64 step, u32 lookahead);
int  kvcpu_hepc_set_weights(struct kvcpu_dev *kv, struct kvcpu_hepc_config *cfg);

/* kv_cpu_nmce.c */
int  kvcpu_nmce_init(struct kvcpu_dev *kv);
void kvcpu_nmce_teardown(struct kvcpu_dev *kv);
int  kvcpu_nmce_submit(struct kvcpu_dev *kv, struct kvcpu_sqe *sqe);

/* kv_cpu_mem.c */
int  kvcpu_mem_register(struct kvcpu_dev *kv);
void kvcpu_mem_unregister(struct kvcpu_dev *kv);

/* kv_cpu_madvise.c */
int  kvcpu_madvise_register(struct kvcpu_dev *kv);
void kvcpu_madvise_unregister(struct kvcpu_dev *kv);
int  kvcpu_madvise_handler(struct kvcpu_dev *kv, unsigned long start,
			   size_t len, int behavior, u64 aux);

/* kv_cpu_ioring.c */
int  kvcpu_ioring_register(struct kvcpu_dev *kv);
void kvcpu_ioring_unregister(struct kvcpu_dev *kv);

/* kv_cpu_sysfs.c */
int  kvcpu_sysfs_init(struct kvcpu_dev *kv);
void kvcpu_sysfs_teardown(struct kvcpu_dev *kv);

/* kv_cpu_rtbd.c */
int  kvcpu_rtbd_share(struct kvcpu_dev *kv, phys_addr_t block_pa, u16 req_id);
int  kvcpu_rtbd_release(struct kvcpu_dev *kv, phys_addr_t block_pa, u16 req_id);
int  kvcpu_rtbd_query(struct kvcpu_dev *kv, phys_addr_t block_pa,
		      struct kvcpu_rtbd_entry *out);
int  kvcpu_rtbd_flush(struct kvcpu_dev *kv, u16 req_id);

/* kv_cpu_mock.c */
int  kvcpu_mock_init(struct kvcpu_dev *kv);
void kvcpu_mock_teardown(struct kvcpu_dev *kv);
void kvcpu_mock_step_advance(struct kvcpu_dev *kv, u64 step);
int  kvcpu_mock_nmce_submit(struct kvcpu_dev *kv, struct kvcpu_sqe *sqe);

/* Extended madvise behavior codes (to be added to linux/mman.h upstream) */
#define MADV_KV_HOT      25   /* KV blocks are hot — boost priority to max   */
#define MADV_KV_EVICT    26   /* KV blocks no longer needed — evict now       */
#define MADV_KV_PREFETCH 27   /* Hint decode step + lookahead for prefetch    */

/* io_uring opcodes (to be upstreamed to include/uapi/linux/io_uring.h) */
#define IORING_OP_KV_STAGE   64   /* Async migrate block from T2/T3 → T1      */
#define IORING_OP_KV_EVICT   65   /* Async migrate block from T1 → T2/T3      */

#endif /* _KV_CPU_H */
