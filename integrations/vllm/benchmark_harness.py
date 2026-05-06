#!/usr/bin/env python3
"""Tiny local benchmark scaffold for the KV-CPU vLLM adapter.

This is not a claim of end-to-end Figure 10 reproduction. It is a harness that
lets us compare baseline allocator overhead versus wrapped allocator overhead
across context lengths while exercising the same decode-step/share-prefix hooks
the real integration would use.
"""

from __future__ import annotations

import csv
import time
from dataclasses import dataclass
from types import SimpleNamespace
from typing import List

from kv_cpu_allocator import KvCpuDeviceAwareBlockAllocator


@dataclass
class BenchmarkResult:
    context_tokens: int
    decode_steps: int
    elapsed_seconds: float
    share_events: int
    step_events: int


class CountingController:
    def __init__(self) -> None:
        self.step_events = 0
        self.share_events = 0

    def step_advance(self, step: int) -> None:
        self.step_events += 1

    def share_prefix(self, va: int, length: int) -> None:
        self.share_events += 1


class SyntheticAllocator:
    def __init__(self) -> None:
        self.all_block_ids = set()
        self._next_block_id = 0

    def allocate_mutable_block(self, prev_block, device, extra_hash=None):
        block = SimpleNamespace(block_id=self._next_block_id)
        self._next_block_id += 1
        return block

    def allocate_immutable_block(self, prev_block, token_ids, device, extra_hash=None):
        block = SimpleNamespace(block_id=self._next_block_id)
        self.all_block_ids.add(self._next_block_id)
        self._next_block_id += 1
        return block

    def allocate_immutable_blocks(self, prev_block, block_token_ids, device, extra_hash=None):
        return [
            self.allocate_immutable_block(prev_block, token_ids, device, extra_hash)
            for token_ids in block_token_ids
        ]

    def allocate_or_get_null_block(self):
        return None

    def clear_copy_on_writes(self):
        return []

    def find_cached_blocks_prefix(self, block_hashes, device=None):
        return list(range(min(len(block_hashes), 4)))

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


def run_synthetic_benchmark(
    context_tokens: int,
    block_size_tokens: int = 16,
    decode_steps: int = 128,
) -> BenchmarkResult:
    controller = CountingController()
    allocator = KvCpuDeviceAwareBlockAllocator(SyntheticAllocator(), controller)

    prev_block = None
    blocks_per_context = max(1, context_tokens // block_size_tokens)

    start = time.perf_counter()
    for block_idx in range(blocks_per_context):
        token_ids = list(range(block_idx * block_size_tokens, (block_idx + 1) * block_size_tokens))
        prev_block = allocator.allocate_immutable_block(prev_block, token_ids, "gpu")

    for step in range(decode_steps):
        allocator.advance_decode_step(step)
        allocator.find_cached_blocks_prefix([step, step + 1, step + 2], "gpu")

    elapsed = time.perf_counter() - start
    return BenchmarkResult(
        context_tokens=context_tokens,
        decode_steps=decode_steps,
        elapsed_seconds=elapsed,
        share_events=controller.share_events,
        step_events=controller.step_events,
    )


def main() -> int:
    rows: List[BenchmarkResult] = [
        run_synthetic_benchmark(context_tokens) for context_tokens in (4096, 8192, 16384, 32768)
    ]

    with open("integrations/vllm/benchmark_results.csv", "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            ["context_tokens", "decode_steps", "elapsed_seconds", "share_events", "step_events"]
        )
        for row in rows:
            writer.writerow(
                [
                    row.context_tokens,
                    row.decode_steps,
                    f"{row.elapsed_seconds:.6f}",
                    row.share_events,
                    row.step_events,
                ]
            )

    for row in rows:
        print(
            f"context={row.context_tokens} steps={row.decode_steps} "
            f"elapsed={row.elapsed_seconds:.6f}s shares={row.share_events} "
            f"step_events={row.step_events}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
