# linux-mm RFC: MADV_KV_HOT / MADV_KV_EVICT / MADV_KV_PREFETCH

## What this RFC proposes

Three new `madvise(2)` behavior codes (104–106) that allow LLM inference
runtimes to communicate KV-cache access intent to a registered near-memory
compute device driver.

| Value | Constant | Meaning |
|-------|----------|---------|
| 104 | `MADV_KV_HOT` | KV blocks are actively used — protect from eviction |
| 105 | `MADV_KV_EVICT` | KV blocks no longer needed — migrate to lower tier |
| 106 | `MADV_KV_PREFETCH` | Advance decode step + set prefetch lookahead |

## Files in this directory

| File | Contents |
|------|----------|
| `0000-cover-letter.patch` | Full motivation, design rationale, open questions |
| `0001-mm-madvise-add-kv-handler-registration.patch` | `include/linux/kv_madvise.h` + `mm/kv_madvise.c` |
| `0002-uapi-add-MADV-KV-constants.patch` | UAPI headers (generic + 4 arch overrides + tools/) |
| `0003-mm-madvise-implement-kv-handlers.patch` | `mm/madvise.c` changes |
| `0004-selftests-mm-add-kv-madvise-tests.patch` | kselftest — 5 test groups, 15 assertions |
| `0004b-mm-kconfig-kv-madvise.patch` | `mm/Kconfig` — CONFIG_KV_MADVISE |
| `send_rfc.sh` | Configured `git send-email` script |

## How to apply and test locally

```bash
# Apply to a clean 6.13 or 6.14-rc tree
cd /path/to/linux
git checkout -b kv-madvise-rfc
git am /path/to/rfc_patches/*.patch

# Build with KV_MADVISE enabled
echo "CONFIG_KV_MADVISE=y" >> .config
make olddefconfig
make -j$(nproc)

# Run the selftests
cd tools/testing/selftests/mm
make kv_madvise_test
./kv_madvise_test
# Expected: 15 passed, 0 failed, 0 skipped
```

## Anticipated reviewer questions and prepared answers

### Q1: Why not extend madvise_behavior_valid() instead of a new predicate?

`madvise_behavior_valid()` uses a switch statement that returns false for
unknown values. We need the KV behaviors to return *true* from validation
(so the syscall proceeds rather than returning EINVAL) even when no
handler is registered. The new `madvise_kv_behavior()` predicate handles
this cleanly without touching the existing validation switch.

### Q2: Why values 104–106 and not 26–28?

The dense range 0–25 requires arch-specific overrides in alpha, mips,
parisc, and xtensa (because those arches define their own mman.h and
override any generic value that collides with arch-specific MAP_* flags).
Values ≥100 in the existing kernel (100-103) have already established
the pattern of "device/platform-specific hints that don't require arch
overrides." Values 104–106 continue this pattern. Values 26–99 remain
available for other subsystems.

### Q3: The MADV_KV_PREFETCH length-field encoding is non-standard. Why?

Agreed — it is unusual. The alternatives considered were:

1. **New syscall `madvise_ex(addr, len, advice, aux_lo, aux_hi)`** — cleanest
   interface but highest barrier to acceptance; requires UAPI freeze review.
2. **prctl(PR_KV_MADVISE, addr, len, behavior, aux)** — avoids new syscall
   but prctl is semantically process-scoped, not address-range-scoped.
3. **Length-field encoding (current approach)** — no new syscall, works with
   existing `madvise(2)` signature, precedent exists in some out-of-tree
   patches (Android), but is semantically impure.

The RFC explicitly requests community input on this. If the consensus is
option 1, the series will be respun with a `madvise_ex()` variant.

### Q4: Single global handler pointer — what about multiple KV-CPU devices?

The current hardware landscape has at most one KV-cache companion device
per host (the KV-CPU device occupies a PCIe x16 slot). A linked list of
handlers would add per-dispatch overhead (rcu_read_lock + list_for_each)
for a case that doesn't exist today. The single-pointer approach mirrors
how `dm_io_client` and similar single-device subsystems work. If multiple
devices become common, the upgrade path is: change `kv_madvise_fn` from a
single pointer to an `srcu_notifier_head` — a one-patch change that doesn't
affect the UAPI.

### Q5: Does this interact with memory_hotplug / NUMA balancing?

The `MADV_KV_*` behaviors bypass VMA iteration entirely and do not modify
PTEs, swap entries, or page flags. They translate VA to PA via
`get_user_pages_fast()` and dispatch to a device driver via MMIO writes.
NUMA balancing, kswapd, and the OOM killer are unaware of these calls
and continue to function normally. The only interaction is that a
`MADV_KV_HOT` call may cause the device to protect a KV block from device-
managed eviction — but this does not affect the kernel's own page reclaim
path, which operates independently on the same physical memory.

### Q6: What prevents a buggy LLM runtime from issuing MADV_KV_EVICT
on memory it doesn't own?

The same thing that prevents it from issuing MADV_DONTNEED on memory it
doesn't own: the VMA permission checks in do_madvise(). The `madvise_kv_range()`
function is called only after the standard `!PAGE_ALIGNED(start)` check.
The `madvise_kv_translate_range()` VA-to-PA translation uses
`get_user_pages_fast()` which enforces that the caller has read access to
the page — an unprivileged process cannot translate arbitrary physical
addresses.

### Q7: Why is this in mm/ and not drivers/cxl/ or drivers/misc/?

The registration infrastructure (`kv_madvise.h`, `kv_madvise.c`) belongs
in mm/ because it modifies the behaviour of `madvise(2)` — a core mm
syscall. The device-specific implementation (MMIO writes, DMA management)
lives in the device driver under `drivers/misc/kv_cpu/`. This separation
keeps mm/ generic and the driver-specific logic out of the core.
