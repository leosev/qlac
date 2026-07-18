# QLAC

![version](https://img.shields.io/badge/version-0.1.0-blue)
![license](https://img.shields.io/badge/license-BSD--3--Clause-green)
![status](https://img.shields.io/badge/status-experimental-orange)

A low-latency, block-reentrant **lossless** audio codec for Networked Music
Performance (NMP), derived from the FLAC reference implementation.

QLAC targets the gap between uncompressed PCM (full fidelity, high bandwidth)
and low-latency lossy codecs such as Opus. NMP works within a tight end-to-end
latency budget (on the order of 20–30 ms), and QLAC is built to show that
lossless coding can be a viable third option in that budget: it cuts bandwidth
without discarding signal and without adding look-ahead delay beyond the length
of a single block.

QLAC accompanies the paper:

> Leonardo Severi, *Wasted Bandwidth: The Case for Lossless Audio in Networked
> Music Performance*, Politecnico di Torino, 2026.

Please cite via [`CITATION.cff`](CITATION.cff) (see [Citation](#citation)).
Repository: <https://github.com/leosev/qlac>

## Status and stability

QLAC is **0.x** and is published as a **demonstration accompanying the paper**.
It is not production-hardened.

**A large part of this codebase was produced with AI assistance ("vibe
coded")** and has not been reviewed as carefully as hand-written code would
be. The core path behind the paper's results — the codec itself, exercised
through `tests/` and the `assessment/` benchmark on Linux/Raspberry Pi — is
the part that has actually been tested, since it underpins the reported
figures. Most everything else has only been lightly tested, and **cross-platform
building and functioning (macOS, Windows, MSVC/MinGW) has not been tested at
all**; those paths are believed correct but unverified. If you're relying on
QLAC outside of reproducing the paper's setup, confirm the relevant path
yourself first.

**There are no backward-compatibility guarantees.** Neither the on-the-wire
block/payload format nor the C/C++ codec API is stable, and either may change
between commits or releases without preserving compatibility. Do not build a
system on the current byte format or API expecting it to keep working across
updates.

A more stable version may follow if there is interest.

## Relationship to FLAC

QLAC is a fork of the FLAC reference implementation (libFLAC 1.5.0). It reuses
FLAC's per-block coding machinery while removing everything that ties a block to
a continuous stream.

**Kept from FLAC:**

- Intra-block linear prediction: the fixed polynomial predictors (order 0–4) and
  LPC.
- Rice residual entropy coding, partitioned within a block.
- The optimized DSP kernels (x86 and ARM SIMD).

**Stripped away:**

- No `STREAMINFO` or other metadata blocks; no seektable.
- No frame headers, sync codes, or per-frame CRC.
- No stream/whole-file MD5 signature.
- No Ogg mapping.

In place of the frame/stream scaffolding, QLAC emits a compact per-block payload:
a single header byte (identifying the block's subframe/predictor type and its
parameters) followed by the coded residual. Blocksize and bits-per-sample are
agreed once, out-of-band, rather than carried in every block. Every encoded
block is therefore self-contained and independently decodable.

All `FLAC__*` symbols were renamed to `QLAC__*`. Per-file provenance and the
exact list of changes are documented in [`FORK_NOTES.md`](FORK_NOTES.md).

## Design

Each block is coded exactly as a FLAC subframe would be: the encoder picks
between a constant, verbatim, fixed-order, or LPC model, then Rice-codes the
prediction residual. QLAC keeps that intra-block model and its SIMD
implementations verbatim, and drops the surrounding stream format.

Because nothing in an encoded block references a neighbouring block, the codec is
fully block-reentrant:

- A lost or corrupted packet costs at most one block. There is no resync search
  and no error propagation across blocks, which suits lossy, packet-loss-prone
  transport.
- Coding latency is bounded by the block length alone; there is no inter-block
  look-ahead.
- Decoding requires only the out-of-band parameters (bits-per-sample, blocksize)
  and the block bytes themselves.

## Reported results

The following are the figures reported in the paper; they are not reproduced or
re-measured here.

- Roughly **30–60% bandwidth reduction** versus uncompressed PCM, depending on
  the material and the block size.
- On a **Raspberry Pi 4**, encoding a 128-sample block runs about **156× faster
  than real time at 44.1 kHz**, with decoding faster still.
- Block sizes from **16 to 512 samples** were tested.
- Blocks are independently decodable, which suits lossy / packet-loss-prone
  transport.

## Building

QLAC builds with **CMake only** (≥ 3.31).

**Prerequisites:**

- A C compiler and a C++20 compiler.
- **Network access during `cmake` configure.** The header-only `argparse`
  library is fetched from GitHub via CMake `FetchContent` at configure time.
- **libsndfile** (`libsndfile-dev`, located via pkg-config) — required only for
  the `tests` and `assessment` targets.
- The core library `libQLAC` itself has **no external runtime dependencies**.

**Commands:**

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The test programs are also standalone executables and can be run directly. For
example, `TestWav` (built under `build/tests/TestWav/`) round-trips a mono WAV
file block-by-block and reports timings:

```
TestWav <input.wav> <blocksize>
```

**Options:**

- `BUILD_SHARED_LIBS` (default `OFF`) — QLAC builds **static** libraries by
  default; set this `ON` for shared libraries.
- `WITH_ASM` (default `ON`) — build the SIMD-optimized kernels (selected at
  runtime by CPU detection).

## Platform support

| Component | Platforms | Notes |
|---|---|---|
| `libQLAC` (codec library) | Linux, macOS, Windows (MSVC/MinGW) | x86 (SSE2/SSSE3/SSE4/AVX2/FMA) and ARM (NEON) intrinsics with runtime CPU detection; endian-independent. No external runtime dependencies. |
| `tests/` | Linux, macOS, Windows | Require system libsndfile. |
| `assessment/` | **Linux only** | Uses POSIX `pthread` + `sched.h` `SCHED_FIFO` real-time scheduling to emulate a real-time audio thread. Developed and measured on a Raspberry Pi 4 (ARM64, NEON) under a Linux PREEMPT_RT kernel. It does not build or run meaningfully on non-Linux platforms. |

## Repository layout

```
src/libQLAC/       Codec implementation (C)
include/QLAC/      C API headers (stream_encoder.h, stream_decoder.h, ...)
include/QLAC++/    Header-only C++20 block wrapper (block_codec.hpp)
tests/             Round-trip correctness + timing programs
assessment/        The Linux real-time benchmark from the paper
cmake/             CMake helper modules (CPU / architecture detection)
CMakeLists.txt     Top-level build
```

## Usage

The intended entry point is the header-only C++20 wrapper
[`include/QLAC++/block_codec.hpp`](include/QLAC++/block_codec.hpp). It provides
RAII `Encoder` and `Decoder` types (namespace `qlac`) that each own an aligned
sample buffer and handle exactly **one mono block per call**. Bits-per-sample and
blocksize are fixed out-of-band and must match on both endpoints.

API shape:

- `qlac::Config { bits_per_sample /* 4..32 */, blocksize }` — the shared,
  out-of-band parameters.
- `qlac::Encoder(cfg, compression_level = 5, apodization = nullptr)` — write the
  block into `encoder.input_samples()`, then call
  `encode(std::span<std::byte> out)`, which returns a subspan of `out` holding
  the encoded bytes. Note: `encode()` may modify the input buffer in place
  (wasted-bits shift), so refill it before each call.
- `qlac::Decoder(cfg)` — call `decode(std::span<const std::byte> in)`, which
  returns a `std::span<QLAC__int32>` of exactly `blocksize` reconstructed
  samples (also available via `decoder.output_samples()`).
- Both types are move-only. Setup, allocation, and codec failures are reported by
  throwing `qlac::Error`.

Minimal round-trip (types and calls taken directly from the wrapper):

```cpp
#include <QLAC++/block_codec.hpp>

qlac::Config cfg{ .bits_per_sample = 16, .blocksize = 128 };

qlac::Encoder enc(cfg);   // compression_level defaults to 5
qlac::Decoder dec(cfg);   // same cfg on the receiving endpoint

// Fill the encoder's aligned input buffer with one block of mono samples.
auto& in = enc.input_samples();
for (std::uint32_t i = 0; i < cfg.blocksize; ++i)
    in[i] = /* i-th sample */ 0;

// Encode into a caller-owned buffer; `frame` views the bytes written.
std::vector<std::byte> buf(cfg.blocksize * sizeof(std::int32_t) + 1024);
std::span<std::byte> frame = enc.encode(buf);

// Decode the block back (possibly on another host).
std::span<std::int32_t> out = dec.decode(frame);   // out.size() == cfg.blocksize
```

For a complete, compilable example — including reading audio with libsndfile,
per-block timing, and round-trip verification — see
[`tests/TestWav/wav_bench.cpp`](tests/TestWav/wav_bench.cpp).

## License

QLAC is licensed under the BSD-3-Clause family of licenses. The codebase is a
mix of two provenances:

- **FLAC-derived files** retain the Xiph.Org BSD-like license
  ([`COPYING.Xiph`](COPYING.Xiph)).
- **QLAC-original code and modifications** are © 2025 Leonardo Severi, licensed
  under BSD-3-Clause ([`COPYING.QLAC`](COPYING.QLAC)).

See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE) for the combined terms, and
[`FORK_NOTES.md`](FORK_NOTES.md) for per-file provenance.

## Citation

If you use QLAC in academic work, please cite the paper. Machine-readable
metadata is provided in [`CITATION.cff`](CITATION.cff).

Plain text:

> Leonardo Severi, "Wasted Bandwidth: The Case for Lossless Audio in Networked
> Music Performance," Politecnico di Torino, 2026.

## Acknowledgments

This work was supported by the MUSMET project, funded by the European Union's EIC
Pathfinder Open scheme (grant agreement no. 101184379). Views and opinions
expressed are those of the author only and do not necessarily reflect those of
the European Union or the European Innovation Council.
