#!/usr/bin/env python3
"""Minimal vLLM-facing KV-CPU control-plane adapter.

This module provides two small building blocks:

1. ``KvCpuController``: a userspace helper that opens ``/dev/kvcpu0`` and
   issues the KV-CPU ioctls defined in ``include/uapi/linux/kv_cpu.h``.
2. ``KvCpuDeviceAwareBlockAllocator``: a thin wrapper around a vLLM
   ``DeviceAwareBlockAllocator`` that exposes an explicit ``advance_decode_step``
   hook for runtimes that want to notify the kernel driver on each decode step.

The wrapper is intentionally small and delegates actual block allocation to an
existing vLLM allocator. Its job is to make the step-synchronization handshake
real and runnable today in driver mock mode.
"""

from __future__ import annotations

import argparse
import ctypes
import fcntl
import os
from typing import Any, List, Optional

try:
    from vllm.core.block.interfaces import DeviceAwareBlockAllocator
except Exception:  # pragma: no cover - vLLM is optional in this repo.
    class DeviceAwareBlockAllocator:  # type: ignore[override]
        """Fallback base when vLLM is not installed locally."""

        pass


_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14

_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE = 0
_IOC_WRITE = 1
_IOC_READ = 2

KV_CPU_MAGIC = ord("K")


def _ioc(direction: int, type_: int, nr: int, size: int) -> int:
    return (
        (direction << _IOC_DIRSHIFT)
        | (type_ << _IOC_TYPESHIFT)
        | (nr << _IOC_NRSHIFT)
        | (size << _IOC_SIZESHIFT)
    )


def _iow(type_: int, nr: int, struct_type: type[ctypes.Structure]) -> int:
    return _ioc(_IOC_WRITE, type_, nr, ctypes.sizeof(struct_type))


class KvCpuStepInfo(ctypes.Structure):
    _fields_ = [("step", ctypes.c_uint64)]


class KvCpuBlockInfo(ctypes.Structure):
    _fields_ = [
        ("va", ctypes.c_uint64),
        ("len", ctypes.c_uint64),
        ("target_step", ctypes.c_uint64),
    ]


KV_CPU_STEP_ADVANCE = _iow(KV_CPU_MAGIC, 0x01, KvCpuStepInfo)
KV_CPU_MARK_HOT = _iow(KV_CPU_MAGIC, 0x02, KvCpuBlockInfo)
KV_CPU_EVICT = _iow(KV_CPU_MAGIC, 0x03, KvCpuBlockInfo)
KV_CPU_PREFETCH = _iow(KV_CPU_MAGIC, 0x04, KvCpuBlockInfo)
KV_CPU_SHARE_PREFIX = _iow(KV_CPU_MAGIC, 0x05, KvCpuBlockInfo)


class KvCpuController:
    """Small userspace shim for the KV-CPU mock or physical device node."""

    def __init__(self, dev_path: str = "/dev/kvcpu0") -> None:
        self.dev_path = dev_path
        self.fd = os.open(dev_path, os.O_RDWR)

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def __enter__(self) -> "KvCpuController":
        return self

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        self.close()

    def _ioctl(self, request: int, payload: ctypes.Structure) -> None:
        fcntl.ioctl(self.fd, request, bytes(payload))

    def step_advance(self, step: int) -> None:
        self._ioctl(KV_CPU_STEP_ADVANCE, KvCpuStepInfo(step=step))

    def mark_hot(self, va: int, length: int) -> None:
        self._ioctl(KV_CPU_MARK_HOT, KvCpuBlockInfo(va=va, len=length))

    def evict(self, va: int, length: int) -> None:
        self._ioctl(KV_CPU_EVICT, KvCpuBlockInfo(va=va, len=length))

    def prefetch(self, va: int, length: int, target_step: int) -> None:
        self._ioctl(
            KV_CPU_PREFETCH,
            KvCpuBlockInfo(va=va, len=length, target_step=target_step),
        )

    def share_prefix(self, va: int, length: int) -> None:
        self._ioctl(KV_CPU_SHARE_PREFIX, KvCpuBlockInfo(va=va, len=length))


class KvCpuDeviceAwareBlockAllocator(DeviceAwareBlockAllocator):
    """Delegate allocator plus explicit KV-CPU decode-step signaling.

    vLLM's current allocator integration point is ``DeviceAwareBlockAllocator``
    (for example ``CpuGpuBlockAllocator``). This wrapper preserves that surface
    while adding one explicit method, ``advance_decode_step()``, that the decode
    loop can call once per token generation step.
    """

    def __init__(
        self,
        inner_allocator: Any,
        controller: KvCpuController,
        initial_step: int = 0,
    ) -> None:
        self._inner = inner_allocator
        self._controller = controller
        self._current_step = initial_step

    @property
    def all_block_ids(self) -> Any:
        return self._inner.all_block_ids

    def advance_decode_step(self, step: Optional[int] = None) -> int:
        if step is None:
            self._current_step += 1
        else:
            self._current_step = step
        self._controller.step_advance(self._current_step)
        return self._current_step

    def allocate_mutable_block(
        self,
        prev_block: Optional[Any],
        device: Any,
        extra_hash: Optional[int] = None,
    ) -> Any:
        return self._inner.allocate_mutable_block(
            prev_block, device, extra_hash=extra_hash
        )

    def allocate_immutable_block(
        self,
        prev_block: Optional[Any],
        token_ids: List[int],
        device: Any,
        extra_hash: Optional[int] = None,
    ) -> Any:
        return self._inner.allocate_immutable_block(
            prev_block, token_ids, device, extra_hash=extra_hash
        )

    def allocate_immutable_blocks(
        self,
        prev_block: Optional[Any],
        block_token_ids: List[List[int]],
        device: Any,
        extra_hash: Optional[int] = None,
    ) -> List[Any]:
        return self._inner.allocate_immutable_blocks(
            prev_block, block_token_ids, device, extra_hash=extra_hash
        )

    def allocate_or_get_null_block(self) -> Any:
        return self._inner.allocate_or_get_null_block()

    def clear_copy_on_writes(self) -> List[tuple[int, int]]:
        return self._inner.clear_copy_on_writes()

    def find_cached_blocks_prefix(
        self, block_hashes: List[int], device: Any = None
    ) -> List[int]:
        if device is None:
            return self._inner.find_cached_blocks_prefix(block_hashes)
        return self._inner.find_cached_blocks_prefix(block_hashes, device)

    def fork(self, last_block: Any) -> List[Any]:
        return self._inner.fork(last_block)

    def free(self, block: Any) -> None:
        self._inner.free(block)

    def get_common_computed_block_ids(
        self, computed_seq_block_ids: List[List[int]]
    ) -> List[int]:
        return self._inner.get_common_computed_block_ids(computed_seq_block_ids)

    def get_num_free_blocks(self, device: Any) -> int:
        return self._inner.get_num_free_blocks(device)

    def get_num_full_blocks_touched(self, blocks: List[Any], device: Any) -> int:
        return self._inner.get_num_full_blocks_touched(blocks, device)

    def get_num_total_blocks(self, device: Any) -> int:
        return self._inner.get_num_total_blocks(device)

    def get_physical_block_id(self, device: Any, absolute_id: int) -> int:
        return self._inner.get_physical_block_id(device, absolute_id)

    def get_prefix_cache_hit_rate(self, device: Any) -> float:
        return self._inner.get_prefix_cache_hit_rate(device)

    def mark_blocks_as_accessed(self, block_ids: List[int], now: float) -> None:
        self._inner.mark_blocks_as_accessed(block_ids, now)

    def mark_blocks_as_computed(self, block_ids: List[int]) -> None:
        self._inner.mark_blocks_as_computed(block_ids)

    def reset_prefix_cache(self, device: Any = None) -> bool:
        if device is None:
            return self._inner.reset_prefix_cache()
        return self._inner.reset_prefix_cache(device)

    def swap(self, blocks: List[Any], src_device: Any, dst_device: Any) -> Any:
        return self._inner.swap(blocks, src_device, dst_device)


def _main() -> int:
    parser = argparse.ArgumentParser(
        description="Issue KV-CPU STEP_ADVANCE from userspace."
    )
    parser.add_argument("step", type=int, help="Decode step to signal")
    parser.add_argument(
        "--device",
        default="/dev/kvcpu0",
        help="KV-CPU device node (default: /dev/kvcpu0)",
    )
    args = parser.parse_args()

    with KvCpuController(args.device) as controller:
        controller.step_advance(args.step)

    print(f"signaled KV_CPU_STEP_ADVANCE step={args.step} via {args.device}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
