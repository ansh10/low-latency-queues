# Low-Latency Queues in C++

A systems-programming project exploring how different queue designs behave under contention, cache pressure, and synchronization overhead.

The repository focuses on building and iteratively optimizing **single-producer/single-consumer (SPSC)** and **multi-producer/multi-consumer (MPMC)** queues in C++, with emphasis on:

* atomic operations
* C++ memory ordering
* cache-line alignment
* false sharing
* ring-buffer design
* branch and indexing costs
* synchronization overhead
* throughput and latency benchmarking

The goal is not only to build concurrent queues, but to understand **why one implementation performs better than another on modern hardware**.

---

# Project Overview

```text
Low-Latency Queues
│
├── SPSC Queue
│   ├── V1 — baseline implementation
│   ├── V2 — atomic synchronization
│   ├── V3 — reduced ordering strength
│   ├── V4 — cache-line separation
│   ├── V5 — cached producer/consumer state
│   └── V6 — power-of-two capacity + bitmask indexing
│
└── MPMC Queue
    └── mutex + condition-variable baseline
```

The SPSC implementations are deliberately versioned so each optimization can be measured independently.

This makes the project an experiment in:

```text
implementation
    ↓
measurement
    ↓
profiling
    ↓
optimization
    ↓
re-measurement
```

rather than simply presenting one final queue implementation.

---

# Why Queues?

Queues are a fundamental building block in:

* trading systems
* market-data pipelines
* logging systems
* event processing
* messaging
* telemetry
* game engines
* real-time systems

A common pipeline looks like:

```text
Producer Thread
      │
      ▼
┌───────────────┐
│  Ring Buffer  │
└───────────────┘
      │
      ▼
Consumer Thread
```

In latency-sensitive systems, the synchronization mechanism itself can become part of the critical path.

This repository explores how that overhead changes as the implementation moves from simple synchronization toward a cache-aware lock-free design.

---

# SPSC Queue

The main part of the project is a bounded **single-producer/single-consumer ring buffer**.

The SPSC model is simpler than a general MPMC queue because ownership of shared indices is naturally divided:

```text
Producer owns:
    head

Consumer owns:
    tail
```

Only visibility of the opposing index must be synchronized.

This makes SPSC queues an excellent environment for studying:

* atomics
* memory ordering
* cache coherence
* false sharing
* branch reduction
* low-latency communication

---

# Iterative Optimization

The project contains multiple versions of the same queue.

Each version introduces a specific optimization so the performance impact can be measured independently.

## V1 — Baseline

The first implementation establishes correctness and the basic bounded ring-buffer design.

Core concepts:

* fixed-size storage
* head/tail indices
* wraparound
* producer/consumer synchronization

This provides the baseline against which later versions are compared.

---

# V2 — Atomic Indices

Shared producer and consumer indices are moved to:

```cpp
std::atomic<size_t>
```

allowing producer and consumer threads to communicate without protecting the queue with a global mutex.

The producer publishes newly written elements through the head index, while the consumer publishes freed capacity through the tail index.

---

# V3 — Explicit Memory Ordering

Instead of relying only on sequentially consistent atomics, the implementation experiments with:

```cpp
std::memory_order_relaxed
std::memory_order_acquire
std::memory_order_release
```

The synchronization pattern is conceptually:

```text
Producer

write queue slot
      |
      v
release head
      |
      | synchronizes-with
      v
acquire head
      |
      v
Consumer reads slot
```

and in the opposite direction:

```text
Consumer

consume queue slot
      |
      v
release tail
      |
      | synchronizes-with
      v
acquire tail
      |
      v
Producer reuses slot
```

This allows local index reads to use relaxed ordering while using acquire/release only where inter-thread visibility is required.

---

# V4 — Cache-Line Separation

Producer and consumer indices are updated by different CPU cores.

If both indices occupy the same cache line, each update can cause the cache line to bounce between cores even though the threads are modifying unrelated variables.

This is known as **false sharing**.

The optimized implementation uses:

```cpp
alignas(64)
```

to place producer and consumer state on separate cache lines.

Conceptually:

```text
Without alignment

Cache Line
┌────────────────────────────┐
│ head | producer data | tail│
└────────────────────────────┘
       ↕ cache bouncing


With alignment

Cache Line A
┌────────────────────────────┐
│ head + producer state      │
└────────────────────────────┘

Cache Line B
┌────────────────────────────┐
│ tail + consumer state      │
└────────────────────────────┘
```

This reduces unnecessary cache-coherence traffic between producer and consumer cores.

---

# V5 — Cached Remote Indices

Reading an atomic variable modified by another core may require cache-coherence traffic.

The producer does not need to reload `tail` on every operation.

Similarly, the consumer does not need to reload `head` on every operation.

The optimized queue therefore keeps local cached copies:

```cpp
cachedTail
cachedHead
```

The producer reloads the consumer's tail only when the queue appears full.

The consumer reloads the producer's head only when the queue appears empty.

Conceptually:

```text
Producer fast path:

head
  ↓
cached tail
  ↓
space available?
  ├── yes → push
  └── no  → reload atomic tail
```

This keeps most operations on thread-local state.

---

# V6 — Power-of-Two Ring Buffer

Modulo arithmetic is typically used for circular-buffer indexing:

```cpp
index = (index + 1) % capacity;
```

The final implementation rounds queue storage to a power of two and replaces modulo with a bitmask:

```cpp
index = (index + 1) & mask;
```

where:

```cpp
mask = capacity - 1;
```

This enables efficient wraparound while maintaining the ring-buffer structure.

The queue uses:

```cpp
std::bit_ceil(...)
```

to round storage capacity to an appropriate power of two.

---

# Final SPSC Design

The optimized queue combines:

* bounded preallocated storage
* no allocation on the hot path
* atomic producer/consumer indices
* relaxed loads for thread-owned state
* acquire/release synchronization
* cache-line separation
* cached remote indices
* power-of-two indexing
* bitmask wraparound

The resulting hot path is approximately:

```text
Producer

load local head
      ↓
check cached tail
      ↓
write element
      ↓
release-store head
```

and:

```text
Consumer

load local tail
      ↓
check cached head
      ↓
read element
      ↓
release-store tail
```

---

# MPMC Baseline

The repository also contains a bounded **multi-producer/multi-consumer queue** implemented using:

```cpp
std::mutex
std::condition_variable
```

The implementation uses two condition variables:

```text
notEmpty
notFull
```

allowing producers to block when the queue is full and consumers to block when the queue is empty.

This provides a conventional synchronization baseline against which future lock-free MPMC implementations can be compared.

Architecture:

```text
Producer ─┐
Producer ─┼──> mutex + notFull ──> Queue
Producer ─┘

Consumer ─┐
Consumer ─┼──> mutex + notEmpty ─> Queue
Consumer ─┘
```

---

# Correctness Testing

The SPSC implementations are tested against the same suite.

Tests include:

* basic FIFO behavior
* interleaved push/pop
* queue fill and drain
* wraparound behavior
* capacity-one edge cases
* invalid capacity handling
* generic payload types
* two-thread producer/consumer execution
* slow producer scenarios
* slow consumer scenarios
* randomized timing jitter
* repeated stress tests

Example stress configuration:

```text
50 iterations
×
20,000 queue operations
```

The goal is to validate both:

```text
functional correctness
+
concurrent ordering correctness
```

before comparing performance.

---

# Benchmarking

The repository includes a throughput benchmark that transfers:

```text
10,000,000 items
```

through each SPSC implementation.

For every version, the benchmark records:

```text
elapsed time
million operations / second
nanoseconds / operation
```

Example output format:

```text
[BENCH SPSCQueueV1] 10,000,000 items -> XX.XX M ops/s (XX.X ns/op)
[BENCH SPSCQueueV2] 10,000,000 items -> XX.XX M ops/s (XX.X ns/op)
...
[BENCH SPSCQueueV6] 10,000,000 items -> XX.XX M ops/s (XX.X ns/op)
```

Performance results are intentionally not hard-coded into the documentation because results depend on:

* CPU
* cache topology
* scheduler
* operating system
* compiler
* optimization flags
* CPU frequency scaling
* thread placement

For reproducible measurements, benchmark results should be reported together with the test environment.

---

# Recommended Benchmark Environment

Record:

```text
CPU:
Physical cores:
Logical cores:
L1/L2/L3 cache:
Operating system:
Compiler:
Compiler version:
Compiler flags:
Queue capacity:
Operations:
```

Example build configuration:

```bash
g++ -std=c++20 -O3 -march=native -pthread
```

---

# Performance Metrics

Current benchmarks focus on:

```text
throughput
M operations / second
ns / operation
```

Future measurements will investigate:

```text
p50 latency
p95 latency
p99 latency
p99.9 latency
```

as well as hardware performance counters such as:

```text
cache misses
LLC misses
branch misses
instructions per cycle
context switches
```

using Linux `perf`.

---

# Profiling

A useful profiling workflow for the project is:

```bash
perf stat ./queue_benchmark
```

to inspect:

```text
cycles
instructions
branches
branch-misses
cache-references
cache-misses
```

and:

```bash
perf record ./queue_benchmark
perf report
```

for hotspot analysis.

This makes it possible to answer not only:

> Which version is faster?

but:

> Why is it faster?

---

# Repository Structure

```text
.
├── SPSC/
│   ├── SPSCQueue.cpp
│   ├── SPSCQueueV2.cpp
│   ├── SPSCQueueV3.cpp
│   ├── SPSCQueueV4.cpp
│   ├── SPSCQueueV5.cpp
│   ├── SPSCQueueV6.cpp
│   ├── main.cpp
│   └── Makefile
│
├── mpmc/
│   ├── mpmcV1.cpp
│   ├── main.cpp
│   ├── usage.cpp
│   └── Makefile
│
└── README.md
```

---

# Build and Run

## SPSC

```bash
cd SPSC
make
./main
```

The executable runs:

```text
correctness tests
stress tests
throughput benchmarks
```

## MPMC

```bash
cd mpmc
make
./main
```

---

# Concepts Demonstrated

## C++

* templates
* move semantics
* `std::vector`
* `std::atomic`
* `std::thread`
* `std::mutex`
* `std::condition_variable`

## Concurrency

* producer/consumer coordination
* synchronization
* blocking vs non-blocking communication
* atomic visibility
* acquire/release ordering

## Computer Architecture

* cache lines
* cache coherence
* false sharing
* cross-core communication
* locality

## Performance Engineering

* iterative optimization
* microbenchmarking
* throughput measurement
* nanosecond-scale operation cost
* synchronization overhead
* cache-aware data layout

---

# Why This Project Matters

The asymptotic complexity of every SPSC implementation is essentially the same:

```text
push  -> O(1)
pop   -> O(1)
```

yet the implementations may perform very differently.

The difference comes from factors that Big-O notation does not capture:

```text
memory ordering
cache coherence
false sharing
remote cache-line reads
branching
index arithmetic
scheduler behavior
```

This project explores that gap between:

```text
algorithmic complexity
          and
real hardware performance
```

which is particularly important in latency-sensitive systems.

---

# Planned Improvements

* [ ] Add reproducible benchmark tables for each implementation
* [ ] Benchmark with CPU affinity
* [ ] Measure p50/p95/p99 latency
* [ ] Profile each optimization using Linux `perf`
* [ ] Add cache-miss and branch-miss comparisons
* [ ] Validate with ThreadSanitizer
* [ ] Add Google Benchmark support
* [ ] Implement a bounded lock-free MPMC queue
* [ ] Compare against established queue implementations
* [ ] Experiment with spin/yield/backoff strategies
* [ ] Investigate NUMA effects
* [ ] Add huge-page and allocator experiments where relevant

---

# Key Takeaway

The project demonstrates a simple systems-programming principle:

```text
Measure first.
Optimize one thing.
Measure again.
```

The goal is not merely to produce the shortest queue implementation, but to understand how **C++ synchronization primitives, memory ordering, cache layout, and CPU behavior interact in a real concurrent program**.