# vLLM Integration Sketch

This directory contains the smallest concrete software-side adapter for the
KV-CPU control plane.

## What It Does

- `kv_cpu_allocator.py` opens `/dev/kvcpu0` and issues the same ioctls exposed by
  the kernel UAPI.
- `KvCpuDeviceAwareBlockAllocator` wraps an existing vLLM
  `DeviceAwareBlockAllocator` and adds `advance_decode_step()`, which calls
  `KV_CPU_STEP_ADVANCE` once per decode iteration.

This keeps actual block allocation inside vLLM while making the KV-CPU
step-synchronization handshake runnable today in driver mock mode.

## Example

```python
from integrations.vllm.kv_cpu_allocator import (
    KvCpuController,
    KvCpuDeviceAwareBlockAllocator,
)

controller = KvCpuController("/dev/kvcpu0")
allocator = KvCpuDeviceAwareBlockAllocator(existing_allocator, controller)

for step in range(num_decode_steps):
    allocator.advance_decode_step(step)
    token = run_one_decode_step(...)
```

## Mock-Mode Demo

With the mock driver loaded:

```bash
sudo insmod kv_cpu.ko mock=1
python3 integrations/vllm/kv_cpu_allocator.py 128
```

That path is intentionally simple: it proves the runtime-to-driver handshake
without requiring a patched vLLM tree in this repository.
