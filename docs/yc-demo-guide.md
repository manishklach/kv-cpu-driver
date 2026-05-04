# YC Demo Guide: The KV-CPU Pitch

This document provides a structured narrative and script for demonstrating the KV-CPU semantic memory control plane to technical investors and partners.

---

## 1. The Thesis
**"LLM inference is memory-orchestration bound."**  
As models scale, the bottleneck shifts from compute to the efficient placement of the KV-cache. The KV-CPU introduces a **semantic control plane** that allows the inference runtime to signal high-level intent directly to hardware via the Linux kernel.

---

## 2. Demo Walkthrough: "The 100k Token Context"

### Step 1: Initialize the Control Plane
We load the driver in **Mock Mode** to demonstrate the architectural bridge without requiring physical hardware.

```bash
# Load the reference driver
sudo insmod kv_cpu.ko mock=1

# Verify the semantic interface
ls -l /dev/kvcpu0
```
**Talking Point:** *"Notice /dev/kvcpu0. This is the industry's first standardized 'Inference Control Plane.' It allows the OS to understand what is happening inside the LLM runtime."*

### Step 2: Signal the Decode Step
During inference, the runtime signals every decode iteration.

```bash
# Signal current decode step (e.g., token 256)
./tools/kvctl step 256
```
**Talking Point:** *"As the model generates tokens, we signal the 'Step' to the kernel. In a real KV-CPU, this triggers an autonomous hardware scan that re-scores the priority of every block in memory based on its proximity to the current token."*

### Step 3: Enforce Semantic Policies
Mark shared prefixes or high-priority blocks as `HOT`.

```bash
# Mark a 4KB range as HOT (protected prefix)
./tools/kvctl hot 0x7f001000 4096
```
**Talking Point:** *"Standard Linux memory management is 'semantic-blind'—it might evict a critical shared prefix because it hasn't been accessed in a few milliseconds. We explicitly signal the hardware that this is HOT, ensuring it stays in the fastest memory tier."*

### Step 4: Proactive Orchestration
Hint at future access patterns to eliminate prefetch latency.

```bash
# Hint that block 0x8f0000 will be needed at step 512
./tools/kvctl prefetch 0x8f0000 4096 512
```
**Talking Point:** *"Finally, we signal the future. We tell the hardware exactly when a block will be needed. The KV-CPU can then move data from slow DRAM to fast memory in the background, completely hiding the latency of memory tiering."*

---

## 3. What This Proves
1.  **Standardized Signaling:** We have a working UAPI that translates LLM semantics into hardware register writes.
2.  **Decoupled Orchestration:** We prove that memory policy can be managed as a control-plane signal, independent of the data plane.
3.  **Hardware Moat:** It demonstrates why a dedicated hardware controller (HEPC) is required to handle these placement decisions at sub-microsecond speeds.

---

## 4. YC Partner Interpretation
*"The KV-CPU team is attacking the context-window bottleneck where it is most expensive: the memory orchestration layer. By moving eviction logic from slow software to step-aware hardware, they are building the 'Control Plane for AI Infrastructure'—a high-leverage position in the AI stack."*
