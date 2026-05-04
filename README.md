# kv-cpu-driver

Linux kernel driver for the **KV-Cache Companion Processing Unit (KV-CPU)** — a purpose-built PCIe/CXL 3.0 device that implements a closed-loop hardware KV-cache lifecycle system for large language model inference servers.

**Patent Pending** — Provisional patent filed in India (Form 2A, Patents Act 1970).  
Docket No 65779  
App Number 202641056309  
Reference Number TEMP/E1/61503/2026-CHE  
CBR Number 37184  

---

## What is the KV-CPU?

The KV-CPU sits between GPU HBM (T0) and host DRAM (T2) in the inference server memory hierarchy, providing three capabilities that no existing device combines:

| Pillar | Component | What it does |
|--------|-----------|--------------|
| I | **NMCE** — Near-Memory Compute Engine | Computes dot-product attention scores from key vectors resident in on-device LPDDR5X, returning only scalar scores to GPU. **25.6×–128× PCIe traffic reduction** for KV scoring. |
| II | **HEPC** — Hardware Eviction & Prefetch Controller | Computes `P(Bᵢ) = wᵣR + w_fF + w_sS + w_dD` in fixed-function silicon. Step-proximity component `S` makes eviction decode-step-aware. Triggered by GPU MMIO write at ~50 ns. |
| III | **RTBD** — Request-Tagged Block Directory | Hardware SRAM CAM tracking every KV block (request_id, layer, head, token range, tier, priority). Hardware reference-counted prefix block sharing — saves C × prefix_KV_GB for C concurrent requests. |
| IV | **Kernel Driver** (this repo) | Registers T1 as a NUMA tier via HMAT. Exposes `MADV_KV_HOT`, `MADV_KV_EVICT`, `MADV_KV_PREFETCH` madvise extensions. Provides `IORING_OP_KV_STAGE` / `IORING_OP_KV_EVICT` io_uring opcodes. Every API call terminates in a hardware register write. |

**Result:** A single H100 GPU can serve contexts up to 256k tokens (vs ~20k without KV-CPU) with GPU pipeline stalls from KV cache misses effectively eliminated.

---

## Repository Structure

```
kv-cpu-driver/
├── include/
│   └── kv_cpu.h              # Hardware register map, SQE/CQE structs, driver state
├── src/
│   ├── kv_cpu_main.c         # PCI probe/remove, IRQ, char device, ioctl dispatch
│   ├── kv_cpu_hepc.c         # HEPC init, step-advance, boost/evict/prefetch control
│   ├── kv_cpu_nmce.c         # NMCE SQ/CQ DMA queue management
│   ├── kv_cpu_mem.c          # T1 memory hot-add + HMAT NUMA tier registration
│   ├── kv_cpu_madvise.c      # MADV_KV_* extension handlers
│   ├── kv_cpu_ioring.c       # IORING_OP_KV_STAGE / KV_EVICT opcodes
│   ├── kv_cpu_rtbd.c         # RTBD share/release/query/flush MMIO interface
│   ├── kv_cpu_sysfs.c        # /sys/bus/pci/.../kvcpu/ telemetry + control nodes
│   └── kv_cpu_ioctl.h        # ioctl definitions shared with userspace
├── tools/
│   └── test/
│       └── kvcpu_test.c      # Driver smoke test + step-advance latency benchmark
└── Makefile
```

---

## Building

### Prerequisites

- Linux kernel **6.6+** (6.11+ recommended for memory tiering APIs)
- Kernel headers for the target kernel
- GCC / Clang

```bash
# Install kernel headers (Ubuntu/Debian)
sudo apt install linux-headers-$(uname -r)

# Build the module
make

# Build test tool
gcc -O2 -o tools/test/kvcpu_test tools/test/kvcpu_test.c

# Load the module
sudo insmod kv_cpu.ko

# Verify
dmesg | grep kv_cpu
ls /dev/kvcpu*
```

### Module parameters

None currently — all tunables are exposed via sysfs after load.

---

## sysfs Interface

After loading, telemetry and HEPC controls appear under:
```
/sys/bus/pci/devices/<BDF>/kvcpu/
```

**Read-only telemetry:**
```bash
cat /sys/bus/pci/devices/0000:03:00.0/kvcpu/t1_size_bytes
cat /sys/bus/pci/devices/0000:03:00.0/kvcpu/evictions_total
cat /sys/bus/pci/devices/0000:03:00.0/kvcpu/nmce_ops_total
cat /sys/bus/pci/devices/0000:03:00.0/kvcpu/nmce_bytes_saved
cat /sys/bus/pci/devices/0000:03:00.0/kvcpu/hepc_status
```

**Read-write HEPC control:**
```bash
# Tune eviction aggressiveness
echo 20 > /sys/bus/pci/devices/0000:03:00.0/kvcpu/hepc_evict_threshold

# Increase step-proximity weight for sparse-attention models
echo 80 > /sys/bus/pci/devices/0000:03:00.0/kvcpu/hepc_weight_s

# Set prefetch lookahead window
echo 256 > /sys/bus/pci/devices/0000:03:00.0/kvcpu/hepc_window_w
```

---

## ioctl Interface (`/dev/kvcpu0`)

```c
#include "src/kv_cpu_ioctl.h"

int fd = open("/dev/kvcpu0", O_RDWR);

// Signal decode step t=42 → triggers HEPC scan/evict/prefetch
uint64_t step = 42;
ioctl(fd, KVCPU_IOC_STEP_ADVANCE, &step);

// Read telemetry snapshot
struct kvcpu_telemetry tel;
ioctl(fd, KVCPU_IOC_GET_TELEMETRY, &tel);
printf("evictions=%llu nmce_ops=%llu\n", tel.evictions, tel.nmce_ops);

// Share a prefix KV block across requests
struct kvcpu_rtbd_cmd cmd = { .block_pa = 0x100000000ULL, .req_id = 7 };
ioctl(fd, KVCPU_IOC_RTBD_SHARE, &cmd);

// Tune HEPC weights at runtime
struct kvcpu_hepc_config cfg = {
    .evict_threshold=10, .prefetch_threshold=180,
    .window_w=128, .weight_r=50, .weight_f=30, .weight_s=20, .weight_d=200
};
ioctl(fd, KVCPU_IOC_SET_WEIGHTS, &cfg);
```

### Low-latency step-advance via mmap

For GPU-side processes that need sub-100 ns step-advance (bypassing syscall):

```c
// Map HEPC control registers directly
volatile uint64_t *bar = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, 0);

// Write step t directly to hardware (~50 ns, no syscall)
bar[KVCPU_REG_STEP_ADVANCE / 8] = decode_step_t;
__sync_synchronize();
```

---

## madvise Extensions

The driver registers three extended madvise behaviors (pending kernel RFC):

```c
// Mark KV blocks as hot — HEPC sets priority to max (0xFFFF)
madvise(kv_cache_ptr, kv_cache_len, MADV_KV_HOT);     // 25

// Signal blocks are no longer needed — immediate background eviction
madvise(old_kv_ptr, old_kv_len, MADV_KV_EVICT);       // 26

// Advance decode step + set prefetch lookahead
// aux = (lookahead << 32) | step
madvise_ex(kv_ptr, kv_len, MADV_KV_PREFETCH, (128ULL << 32) | 42); // 27
```

---

## Integration with vLLM / SGLang

```python
# vLLM integration sketch (pseudocode)
import ctypes, fcntl

KVCPU_DEV = "/dev/kvcpu0"
KVCPU_IOC_STEP_ADVANCE = 0x40084B01  # _IOW('K', 1, u64)

kvcpu_fd = open(KVCPU_DEV, "rb+")

def on_decode_step(step: int):
    """Call this at the start of each autoregressive decode iteration."""
    buf = ctypes.c_uint64(step)
    fcntl.ioctl(kvcpu_fd.fileno(), KVCPU_IOC_STEP_ADVANCE, buf)
```

A full vLLM integration PR is tracked in [issues](../../issues).

---

## Emulation / Development Without Hardware

The driver will load on any x86_64 or ARM64 machine with a CXL-capable PCIe slot. For development without physical hardware:

```bash
# QEMU emulation (CXL Type 3 device)
qemu-system-x86_64 \
  -machine q35,cxl=on \
  -device pxb-cxl,id=cxl.0 \
  -device cxl-rp,id=rp0,bus=cxl.0 \
  -object memory-backend-file,id=cxl-mem,share=on,mem-path=/tmp/cxlmem,size=4G \
  -device cxl-type3,bus=rp0,memdev=cxl-mem,id=cxl-pmem0 \
  ...

# The driver will bind to the emulated device.
# NMCE compute ops will fail gracefully (wrong device identity)
# but HEPC/RTBD MMIO paths and memory tier registration work.
```

---

## Kernel RFC Status

| Feature | Status | Upstream target |
|---------|--------|----------------|
| T1 HMAT NUMA registration | `add_memory_driver_managed()` — upstream 5.15+ | ✅ Already merged |
| `memory_dev_type` tiering | `alloc_memory_type()` — upstream 6.1+ | ✅ Already merged |
| madvise extension hook | RFC posted, not yet merged | linux-mm 6.13+ |
| io_uring custom opcodes | RFC posted (Axboe 2023), pending | linux-block 6.14+ |
| `MADV_KV_*` behavior codes | Part of madvise RFC | linux-mm 6.13+ |

---

## License

GPL-2.0-only — see SPDX identifiers in each source file.

---

## Contributing

Issues and PRs welcome. Areas most in need of work:

1. **Hardware emulation layer** — a software NMCE that emulates attention scoring for CI testing
2. **vLLM BlockAllocator integration** — a custom allocator that calls `KVCPU_IOC_RTBD_SHARE`
3. **Benchmark harness** — reproduce Figure 10 (throughput vs context length) on real hardware
4. **Kernel RFC patch** — finalize `mm/madvise.c` extension hook for upstreaming

---

*KV-CPU — Patent Pending (India). Docket No 65779, App Number 202641056309.*
