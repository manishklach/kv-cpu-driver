#!/usr/bin/env python3
"""Local vLLM-facing KV-CPU control-plane adapter.

This module keeps allocation policy in vLLM while making the KV-CPU handshake
concrete enough to evaluate locally:

- ``KV_CPU_STEP_ADVANCE`` is issued once per decode step.
- ``KV_CPU_SHARE_PREFIX`` is issued for immutable/prefix-cached blocks.

Because this repository does not vendor vLLM itself, the adapter is written as
an optional wrapper around an existing vLLM ``DeviceAwareBlockAllocator``.
Where a real virtual address is not available, the wrapper derives a stable
synthetic span from block identity so mock-mode driver testing still exercises
the intended control path.
"""

from __future__ import annotations

import argparse
import ctypes
import os
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Optional, Tuple

try:
    import fcntl
except ImportError:  # pragma: no cover - unavailable on Windows.
    fcntl = None  # type: ignore[assignment]

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

_IOC_WRITE = 1

KV_CPU_MAGIC = ord("K")
DEFAULT_SYNTHETIC_BLOCK_BYTES = 16 * 1024
_SYNTHETIC_BASE_VA = 0x7000_0000_0000


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


@dataclass(frozen=True)
class KvCpuSpan:
    """Semantic block span that can be signaled to the driver."""

    va: int
    length: int
    source: str


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
        if fcntl is None:
            raise RuntimeError("fcntl is unavailable on this platform")
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
    """Delegate allocator plus concrete KV-CPU signaling hooks.

    The wrapper keeps actual block allocation behavior inside the inner vLLM
    allocator, but mirrors two commercially important control-plane events into
    the driver:

    - decode-step progression via ``advance_decode_step()``
    - prefix-cache participation via ``KV_CPU_SHARE_PREFIX``
    """

    def __init__(
        self,
        inner_allocator: Any,
        controller: KvCpuController,
        initial_step: int = 0,
        synthetic_block_bytes: int = DEFAULT_SYNTHETIC_BLOCK_BYTES,
        share_prefix_on_alloc: bool = True,
        share_prefix_on_cache_hit: bool = True,
    ) -> None:
        self._inner = inner_allocator
        self._controller = controller
        self._current_step = initial_step
        self._synthetic_block_bytes = synthetic_block_bytes
        self._share_prefix_on_alloc = share_prefix_on_alloc
        self._share_prefix_on_cache_hit = share_prefix_on_cache_hit
        self._block_spans: Dict[Tuple[str, int], KvCpuSpan] = {}

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
        block = self._inner.allocate_mutable_block(
            prev_block, device, extra_hash=extra_hash
        )
        self._remember_block_span(block, device)
        return block

    def allocate_immutable_block(
        self,
        prev_block: Optional[Any],
        token_ids: List[int],
        device: Any,
        extra_hash: Optional[int] = None,
    ) -> Any:
        block = self._inner.allocate_immutable_block(
            prev_block, token_ids, device, extra_hash=extra_hash
        )
        self._remember_block_span(block, device)
        if self._share_prefix_on_alloc:
            self._share_block(block, device)
        return block

    def allocate_immutable_blocks(
        self,
        prev_block: Optional[Any],
        block_token_ids: List[List[int]],
        device: Any,
        extra_hash: Optional[int] = None,
    ) -> List[Any]:
        blocks = self._inner.allocate_immutable_blocks(
            prev_block, block_token_ids, device, extra_hash=extra_hash
        )
        for block in blocks:
            self._remember_block_span(block, device)
            if self._share_prefix_on_alloc:
                self._share_block(block, device)
        return blocks

    def allocate_or_get_null_block(self) -> Any:
        return self._inner.allocate_or_get_null_block()

    def clear_copy_on_writes(self) -> List[tuple[int, int]]:
        return self._inner.clear_copy_on_writes()

    def find_cached_blocks_prefix(
        self, block_hashes: List[int], device: Any = None
    ) -> List[int]:
        if device is None:
            cached_ids = self._inner.find_cached_blocks_prefix(block_hashes)
        else:
            cached_ids = self._inner.find_cached_blocks_prefix(block_hashes, device)

        if self._share_prefix_on_cache_hit:
            device_key = self._device_key(device)
            for block_id in cached_ids:
                span = self._span_for_block_id(block_id, device_key)
                self._controller.share_prefix(span.va, span.length)

        return cached_ids

    def fork(self, last_block: Any) -> List[Any]:
        return self._inner.fork(last_block)

    def free(self, block: Any) -> None:
        self._drop_block_span(block)
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

    def _remember_block_span(self, block: Any, device: Any) -> KvCpuSpan:
        block_id = self._extract_block_id(block)
        device_key = self._device_key(device)
        span = self._span_from_block(block, device_key)
        self._block_spans[(device_key, block_id)] = span
        return span

    def _drop_block_span(self, block: Any) -> None:
        block_id = self._extract_block_id(block)
        for device_key in list(self._block_spans):
            if device_key[1] == block_id:
                del self._block_spans[device_key]

    def _share_block(self, block: Any, device: Any) -> None:
        span = self._remember_block_span(block, device)
        self._controller.share_prefix(span.va, span.length)

    def _span_for_block_id(self, block_id: int, device_key: str) -> KvCpuSpan:
        key = (device_key, block_id)
        if key not in self._block_spans:
            self._block_spans[key] = self._synthetic_span(block_id, device_key)
        return self._block_spans[key]

    def _span_from_block(self, block: Any, device_key: str) -> KvCpuSpan:
        explicit = self._explicit_span(block)
        if explicit is not None:
            return explicit
        return self._synthetic_span(self._extract_block_id(block), device_key)

    def _explicit_span(self, block: Any) -> Optional[KvCpuSpan]:
        if hasattr(block, "kv_cpu_span"):
            span = getattr(block, "kv_cpu_span")
            if isinstance(span, KvCpuSpan):
                return span
            if isinstance(span, tuple) and len(span) >= 2:
                return KvCpuSpan(int(span[0]), int(span[1]), "tuple")

        va_candidates = ("kv_cpu_va", "device_ptr", "data_ptr", "ptr", "va")
        len_candidates = ("kv_cpu_len", "nbytes", "length", "len")
        va = next((getattr(block, name) for name in va_candidates if hasattr(block, name)), None)
        length = next(
            (getattr(block, name) for name in len_candidates if hasattr(block, name)),
            None,
        )
        if va is None or length is None:
            return None
        return KvCpuSpan(int(va), int(length), "block-attrs")

    def _synthetic_span(self, block_id: int, device_key: str) -> KvCpuSpan:
        device_salt = (sum(ord(ch) for ch in device_key) & 0xFFFF) << 32
        va = _SYNTHETIC_BASE_VA + device_salt + block_id * self._synthetic_block_bytes
        return KvCpuSpan(va, self._synthetic_block_bytes, "synthetic")

    @staticmethod
    def _extract_block_id(block: Any) -> int:
        for attr in ("block_id", "physical_block_id", "block_number", "id"):
            value = getattr(block, attr, None)
            if value is not None:
                return int(value)
        raise AttributeError("Block object does not expose a stable block id")

    @staticmethod
    def _device_key(device: Any) -> str:
        return "none" if device is None else str(device)


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
