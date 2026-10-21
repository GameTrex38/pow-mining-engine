# Low-Level Multi-Threaded Proof-of-Work (PoW) Mining Engine & Telemetry Visualizer

**Production-Grade Fourth-Year Computer Engineering Capstone Miniproject**  
*Systems Programming, Cryptographic Acceleration & Multi-Core Hardware Optimization in Pure C++20*

---

## 1. Project Overview & Systems Architecture

This project is a high-performance, native C++20 Proof-of-Work (PoW) consensus mining engine coupled with an embedded real-time hardware telemetry web dashboard. It features **zero external cryptographic dependencies** (implements SHA-256 and Double-SHA256 from fundamental bitwise mathematical primitives) and introduces **SHA-256 Midstate Caching Optimization**, reducing cryptographic compression overhead by **33.3%** and boosting single-core throughput by **~50–55%**.

```
                              [ Blockchain Coordinator / DDA Controller ]
                                                 │
                   ┌─────────────────────────────┼─────────────────────────────┐
                   ▼                             ▼                             ▼
       [ Worker Core 0 ]                 [ Worker Core 1 ]             [ Worker Core (N-1) ]
     (Nonce = 0, Stride = N)           (Nonce = 1, Stride = N)       (Nonce = N-1, Stride = N)
                   │                             │                             │
    alignas(64) WorkerSlot 0      alignas(64) WorkerSlot 1      alignas(64) WorkerSlot (N-1)
                   └─────────────────────────────┬─────────────────────────────┘
                                                 ▼
                                     [ Embedded HTTP/1.1 API ]
                                            (Port 8080)
                                                 │
                                                 ▼
                            [ Dark-Mode Telemetry Dashboard (Web UI) ]
                     • Live MH/s Oscilloscope     • 80-Byte Header Inspector
                     • Per-Core Striding Gauges   • Pairwise Merkle Tree
                     • Live Benchmark Engine      • Blockchain Explorer
```

---

## 2. Core Technical Innovations

### A. SHA-256 Midstate Caching Optimization
An 80-byte Bitcoin block header consists of:
- **Bytes 0..63 (Chunk 1, 64 bytes / 512 bits):** `Version` (4B) + `PrevBlockHash` (32B) + `MerkleRoot[0..27]` (first 28B).
- **Bytes 64..79 (Chunk 2, 16 bytes / 128 bits):** `MerkleRoot[28..31]` (last 4B) + `Timestamp` (4B) + `nBits` (4B) + `Nonce` (4B).

#### Algorithmic Comparison:
1. **Naive Double-SHA256 (`SHA256d`):**
   - **Pass 1:** Compresses Chunk 1 (64 bytes) $\to$ Compresses Chunk 2 + Padding (64 bytes). Total = **2 transforms**.
   - **Pass 2:** Compresses the 32-byte intermediate hash + padding (64 bytes). Total = **1 transform**.
   - **Total Compute per Nonce:** $2 + 1 = \mathbf{3\text{ SHA-256 block transforms}}$.

2. **Midstate Caching Optimization:**
   - Because Chunk 1 is strictly invariant while searching through nonces for a block, its SHA-256 intermediate state (`H[0..7]`, 256 bits) is **precomputed once** upon template creation.
   - **Pass 1:** Compresses Chunk 2 + Padding using the pre-loaded `Midstate` register directly. Total = **1 transform**.
   - **Pass 2:** Compresses the 32-byte intermediate hash + padding. Total = **1 transform**.
   - **Total Compute per Nonce:** $1 + 1 = \mathbf{2\text{ SHA-256 block transforms}}$.
   - **Theoretical Speedup:** $\frac{3}{2} = 1.50\times$ (**+50% throughput increase**). Real-world measured speedup: **+53% to +55%**.

### B. Cache-Line False Sharing Elimination
In multi-threaded mining loops running at millions of hashes per second, multiple threads updating adjacent counters located on the same 64-byte L1 CPU cache line causes violent **cache ping-ponging** and MESI protocol invalidations.
- Every worker slot is explicitly declared with `alignas(64)` and packed with padding bytes:
  ```cpp
  struct alignas(64) WorkerSlot {
      std::atomic<uint64_t> hashes_computed{0};
      std::atomic<uint32_t> current_nonce{0};
      std::atomic<double> hashrate_mhs{0.0};
      // Explicit 64-byte padding prevents adjacent threads from sharing a cache line
      char padding[64 - sizeof(...)];
  };
  ```
- Workers accumulate counts locally and flush to the aligned atomic slot in 4,096-hash batches, completely eliminating lock contention in the hot loop.

### C. Lock-Free Nonce Striding
For a pool with $N$ worker threads:
$$\text{Thread } i \text{ starts at } \text{nonce} = i, \quad \text{strides by } \text{nonce} \leftarrow \text{nonce} + N$$
This guarantees 100% disjoint, non-overlapping coverage of the $2^{32}$ nonce space without requiring mutexes or centralized work queues. When the 32-bit nonce space wraps around, the engine increments the coinbase transaction's `extraNonce`, updates the Merkle Root, precomputes a new Midstate, and resumes seamless execution.

### D. Dynamic Difficulty Adjustment (DDA)
Recalculates the target $nBits$ every 5 blocks using a damped proportional feedback controller:
$$\text{Target}_{\text{new}} = \text{Target}_{\text{old}} \times \left( \frac{\text{Actual Timespan}}{\text{Target Timespan}} \right)$$
Clamped to the interval $[0.25, 4.0]$ to prevent wild difficulty swings.

---

## 3. Directory Structure

```
pow_mining_engine/
├── CMakeLists.txt              # Production C++20 CMake build script
├── include/
│   ├── crypto/
│   │   └── sha256.hpp          # Custom SHA-256, SHA256d, and Midstate caching primitives
│   ├── chain/
│   │   ├── block.hpp           # Packed 80-byte BlockHeader struct & 256-bit target math
│   │   ├── transaction.hpp     # Raw transactions & Coinbase extraNonce serialization
│   │   └── merkle.hpp          # Binary Merkle tree builder with odd-leaf duplicate balancing
│   ├── miner/
│   │   ├── miner_pool.hpp      # Lock-free multi-threaded coordinator with alignas(64) slots
│   │   └── dda.hpp             # Dynamic Difficulty Adjustment controller
│   └── server/
│       └── http_server.hpp     # Lightweight cross-platform socket HTTP server (Winsock2/POSIX)
├── src/
│   ├── crypto/sha256.cpp       # Pure C++20 bitwise SHA-256 implementation
│   ├── chain/block.cpp         # 256-bit target expansion & PoW verification
│   ├── chain/merkle.cpp        # Merkle tree generation & UI node structure
│   ├── miner/miner_pool.cpp    # Multi-threaded mining coordinator & benchmark runner
│   ├── server/http_server.cpp  # Non-blocking HTTP/1.1 API & static file server
│   └── main.cpp                # CLI entry point, signal handling, ANSI terminal dashboard
├── tests/
│   └── test_pow.cpp            # NIST test vectors, midstate bitwise equivalence verification
└── web/
    └── index.html              # Sleek dark-mode hardware telemetry dashboard (Tailwind + Chart.js)
```

---

## 4. Build & Installation Instructions

### Prerequisites
- Modern C++20 compliant compiler: `Clang 16+`, `GCC 12+`, or `MSVC 2022`.
- `CMake 3.20+`.
- Windows (`ws2_32.lib` automatically linked) or Linux / macOS (`pthread` automatically linked).

### Build Steps

```powershell
# 1. Clone or navigate to the project directory
cd pow_mining_engine

# 2. Generate build system
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Compile executable and test suite
cmake --build build --config Release
```

---

## 5. Execution & Verification

### A. Run Cryptographic Verification Test Suite
Verifies NIST test vectors, bit-for-bit identity between standard `SHA256d` and `Midstate SHA256d`, Merkle tree balancing, and 256-bit target math:
```powershell
./build/test_pow.exe
```
*Expected Output:*
```
========================================================================
  PoW Mining Engine Cryptographic Verification Test Suite
========================================================================

[Test 1] Verifying NIST SHA-256 Test Vectors...
  ✓ Vector 1 (Empty string): e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 [PASS]
  ✓ Vector 2 ('abc'): ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad [PASS]
  ✓ Vector 3 (448-bit NIST message): 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 [PASS]

[Test 2] Verifying Exact Equivalence: Standard SHA256d vs Midstate SHA256d...
  ✓ Tested nonces across dynamic boundaries.
  ✓ Bit-for-bit identity verified: SHA256d(80B) == SHA256d_Midstate(Chunk1, Chunk2) [PASS]

[Test 3] Verifying Merkle Tree Root Determinism & Odd-Leaf Balancing...
  ✓ 1-Leaf Merkle Root: 8e8509b7959de31658798cb78baf709cc786f43fafe26a4ee5a913cf48395cb7 [PASS]
  ✓ 3-Leaf Balanced Merkle Root: 37bc073a8c4a17b2dbf8c397df8e51829aea44bb2f66943876f1d587d3adaa41 [PASS]
  ✓ Tree Levels: 3 [PASS]

[Test 4] Verifying nBits 256-Bit Target Expansion & PoW Verification...
  ✓ Genesis Target 0x1d00ffff: 00000000ffff0000000000000000000000000000000000000000000000000000
  ✓ Target to nBits roundtrip matched: 0x1d00ffff [PASS]
  ✓ PoW verification against 256-bit target [PASS]

========================================================================
  ALL CRYPTOGRAPHIC & MATHEMATICAL VERIFICATION TESTS PASSED (100%)
========================================================================
```

### B. Run Automated Viva Voce Benchmark
Runs an automated multi-phase benchmark measuring Single-Threaded Naive vs. Single-Threaded Midstate vs. Multi-Threaded Midstate:
```powershell
./build/pow_engine.exe --benchmark
```
*Sample Output:*
```
------------------------------------------------------------------------
  Benchmark Execution Report:
------------------------------------------------------------------------
  • 1-Thread Naive SHA256d:    1.71 MH/s (3 transforms/nonce)
  • 1-Thread Midstate SHA256d: 2.62 MH/s (2 transforms/nonce)
  • Midstate Speedup Factor:   +53.30%
  • 16-Thread Midstate Peak:   17.46 MH/s
  • Multi-Core Scaling Factor: 6.75x
------------------------------------------------------------------------
```

### C. Launch Real-Time Mining Engine & Web Dashboard
```powershell
./build/pow_engine.exe --port 8080 --threads 8
```
Then open your browser to **`http://localhost:8080/`**.

### D. Run Headless / Rich ANSI Terminal Mode
```powershell
./build/pow_engine.exe --cli-only
```

---

## 6. HTTP API Specification

| Endpoint | Method | Payload | Description |
|---|---|---|---|
| `/` | `GET` | None | Serves the single-page hardware telemetry dashboard |
| `/api/telemetry` | `GET` | None | Returns JSON containing live MH/s, per-core nonces, active 80B header hex, midstate hex, recent blocks, and Merkle tree nodes |
| `/api/config` | `POST` | `{"threads": 8, "midstate": true, "bits": "1f0fffff"}` | Live runtime reconfiguration of thread count, midstate caching toggle, and manual difficulty |
| `/api/benchmark` | `POST` | `{"duration": 2}` | Triggers viva benchmark suite and returns comparative throughput metrics |
| `/api/transaction` | `POST` | `{"sender": "Alice", "recipient": "Bob", "amount": 2.5}` | Injects a live transaction into mempool and dynamically regenerates the Merkle tree |

---

## 7. Viva Voce & Academic Defense Q&A

**Q1: Why does Chunk 1 of the block header never change during the mining loop?**  
*Answer:* The 80-byte header layout places Version (bytes 0..3), Previous Block Hash (bytes 4..35), and the first 28 bytes of the Merkle Root in the first 64-byte SHA-256 block. The 4-byte Nonce resides at the very tail of the header (bytes 76..79 in Chunk 2). Therefore, while cycling through nonces for a given block template, Chunk 1 remains bit-invariant.

**Q2: What is false sharing and how does this engine mitigate it?**  
*Answer:* False sharing occurs when independent threads write to distinct variables that reside within the same 64-byte CPU cache line. The CPU cache coherence protocol (MESI) constantly invalidates the cache line across all CPU cores, causing extreme memory bus stalls. We mitigate this using `alignas(64)` on per-worker telemetry structures to guarantee each worker has its own dedicated cache line, combined with local batching every 4,096 iterations.

**Q3: What happens when the 32-bit nonce space ($2^{32} \approx 4.29 \times 10^9$) is exhausted?**  
*Answer:* When nonces wrap around, the coordinator increments the `extraNonce` field in the Coinbase transaction. This changes the Coinbase transaction hash, which bubbles up the Merkle tree to alter the 32-byte Merkle root. A new template and new Midstate are computed, opening up a fresh $2^{32}$ nonce search space.
