#!/usr/bin/env python3
"""Small local tests for the KV-CPU vLLM adapter."""

from __future__ import annotations

import unittest
from types import SimpleNamespace

from kv_cpu_allocator import (
    DEFAULT_SYNTHETIC_BLOCK_BYTES,
    KvCpuDeviceAwareBlockAllocator,
)


class FakeController:
    def __init__(self) -> None:
        self.steps = []
        self.shares = []

    def step_advance(self, step: int) -> None:
        self.steps.append(step)

    def share_prefix(self, va: int, length: int) -> None:
        self.shares.append((va, length))


class FakeAllocator:
    def __init__(self) -> None:
        self.all_block_ids = {0, 1, 2}
        self._next_block_id = 0
        self.cached_ids = [0, 1]

    def allocate_mutable_block(self, prev_block, device, extra_hash=None):
        block = SimpleNamespace(block_id=self._next_block_id)
        self._next_block_id += 1
        return block

    def allocate_immutable_block(self, prev_block, token_ids, device, extra_hash=None):
        block = SimpleNamespace(block_id=self._next_block_id)
        self._next_block_id += 1
        return block

    def allocate_immutable_blocks(self, prev_block, block_token_ids, device, extra_hash=None):
        blocks = []
        for _ in block_token_ids:
            blocks.append(SimpleNamespace(block_id=self._next_block_id))
            self._next_block_id += 1
        return blocks

    def allocate_or_get_null_block(self):
        return None

    def clear_copy_on_writes(self):
        return []

    def find_cached_blocks_prefix(self, block_hashes, device=None):
        return list(self.cached_ids)

    def fork(self, last_block):
        return [last_block]

    def free(self, block):
        return None

    def get_common_computed_block_ids(self, computed_seq_block_ids):
        return []

    def get_num_free_blocks(self, device):
        return 0

    def get_num_full_blocks_touched(self, blocks, device):
        return len(blocks)

    def get_num_total_blocks(self, device):
        return 0

    def get_physical_block_id(self, device, absolute_id):
        return absolute_id

    def get_prefix_cache_hit_rate(self, device):
        return 0.0

    def mark_blocks_as_accessed(self, block_ids, now):
        return None

    def mark_blocks_as_computed(self, block_ids):
        return None

    def reset_prefix_cache(self, device=None):
        return True

    def swap(self, blocks, src_device, dst_device):
        return []


class KvCpuAllocatorTests(unittest.TestCase):
    def test_step_advance_tracks_explicit_and_implicit_steps(self) -> None:
        controller = FakeController()
        allocator = KvCpuDeviceAwareBlockAllocator(FakeAllocator(), controller)

        self.assertEqual(allocator.advance_decode_step(), 1)
        self.assertEqual(allocator.advance_decode_step(7), 7)
        self.assertEqual(controller.steps, [1, 7])

    def test_immutable_allocation_shares_prefix_with_synthetic_span(self) -> None:
        controller = FakeController()
        allocator = KvCpuDeviceAwareBlockAllocator(FakeAllocator(), controller)

        block = allocator.allocate_immutable_block(None, [1, 2, 3], "gpu")
        self.assertEqual(block.block_id, 0)
        self.assertEqual(len(controller.shares), 1)
        self.assertEqual(controller.shares[0][1], DEFAULT_SYNTHETIC_BLOCK_BYTES)

    def test_batch_immutable_allocation_shares_each_block(self) -> None:
        controller = FakeController()
        allocator = KvCpuDeviceAwareBlockAllocator(FakeAllocator(), controller)

        blocks = allocator.allocate_immutable_blocks(None, [[1], [2], [3]], "gpu")
        self.assertEqual([block.block_id for block in blocks], [0, 1, 2])
        self.assertEqual(len(controller.shares), 3)

    def test_cached_prefix_hits_are_shared_using_known_block_identity(self) -> None:
        controller = FakeController()
        allocator = KvCpuDeviceAwareBlockAllocator(FakeAllocator(), controller)

        allocator.allocate_immutable_blocks(None, [[1], [2]], "gpu")
        controller.shares.clear()

        cached_ids = allocator.find_cached_blocks_prefix([111, 222], "gpu")
        self.assertEqual(cached_ids, [0, 1])
        self.assertEqual(len(controller.shares), 2)
        self.assertEqual(controller.shares[0][1], DEFAULT_SYNTHETIC_BLOCK_BYTES)

    def test_explicit_block_span_is_preferred_when_present(self) -> None:
        controller = FakeController()
        inner = FakeAllocator()
        allocator = KvCpuDeviceAwareBlockAllocator(inner, controller)

        inner.allocate_immutable_block = lambda prev_block, token_ids, device, extra_hash=None: SimpleNamespace(
            block_id=99, kv_cpu_va=0x12340000, kv_cpu_len=8192
        )
        allocator.allocate_immutable_block(None, [1], "gpu")
        self.assertEqual(controller.shares[-1], (0x12340000, 8192))


if __name__ == "__main__":
    unittest.main()
