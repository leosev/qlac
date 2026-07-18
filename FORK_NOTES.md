# QLAC fork notes and provenance

## What QLAC is

QLAC is a low-latency lossless audio codec **derived from libFLAC 1.5.0**
(the BSD-licensed FLAC reference library, © Josh Coalson and the Xiph.Org
Foundation). It reuses FLAC's proven signal-coding core — the intra-block
**fixed / LPC predictors**, the **Rice (Golomb-Rice) residual coding**, and the
**SIMD-accelerated DSP kernels** (autocorrelation, LPC residual, windowing,
CPU dispatch) — but discards FLAC's container. QLAC operates on a **single,
header-free block** at a time: it encodes and decodes exactly one mono block of
a fixed, out-of-band block size and bit depth, with no stream, no frame header,
no metadata and no synchronization/seek layer.

Because of that reformulation, large parts of upstream libFLAC are **absent**
from QLAC, by design:

| Absent upstream component | Reason it is gone |
|---|---|
| `crc.c`, `crc.h` (and the CRC-8/CRC-16 hooks in the bit reader/writer) | No frame headers or stream sync, so there is nothing to checksum. |
| `md5.c`, `md5.h` | No whole-stream MD5 signature; verification is done block-by-block by the caller/tests. |
| `metadata_iterators.c`, `metadata_object.c`, `metadata.h`, `include/FLAC/metadata.h` | QLAC carries no STREAMINFO/SEEKTABLE/VORBIS_COMMENT/etc. metadata. |
| `ogg_decoder_aspect.c`, `ogg_encoder_aspect.c`, `ogg_helper.c`, `ogg_mapping.c` (+ headers) | No Ogg transport. |
| `stream_encoder_intrin_{sse2,ssse3,avx2}.c` | These accelerate stereo/precision search paths that the single-block, mono encoder does not use. |
| `libFLAC++` (`include/FLAC++/*`, `src/libFLAC++/*`) | Not ported. QLAC ships its own, original C++ wrapper under `include/QLAC++/` (see below). |
| `include/FLAC/{all,callback}.h`, most of `include/share/*` (getopt, grabbag, replaygain, private.h, …) | Command-line/tool scaffolding not needed by the library. |

## Rename convention

The fork was produced by a mechanical, project-wide identifier rename plus the
structural surgery described above. The rename map is:

| Upstream | QLAC |
|---|---|
| `FLAC__…` (public/CPP-macro namespace) | `QLAC__…` |
| `FLAC_…` / `flac_…` (e.g. `flac_min`, `flac_restrict`, `flac_fprintf`) | `QLAC_…` |
| `libFLAC`, `FLAC/…`, `FLAC++`, `FLACPP__…` | `libQLAC`, `QLAC/…`, `QLAC++`, `QLACPP__…` |
| lowercase `flac` in paths / pkg-config / file names | `qlac` |
| `FLAC__bool` typedef (was `typedef int`) | replaced by C99 `bool` (`<stdbool.h>`); the typedef itself was dropped |
| `FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN` | `QLAC__FRAME_LPC_QLP_SHIFT_LEN` (same value; "subframe" → "frame" terminology) |

Files whose **only** differences from upstream are those mechanical renames
(plus trivial whitespace/case) are classified **verbatim-renamed**. Files that
also add, remove, or restructure code, declarations, or struct members are
classified **modified**. Files with no upstream counterpart are **original**.

Every FLAC-derived file keeps its original Xiph BSD-style copyright header
verbatim; a QLAC banner is prepended, and modified files additionally carry a
Leonardo Severi modification notice.

## Provenance table

Path mapping used below: `QLAC`→`FLAC`, `libQLAC`→`libFLAC`, `QLAC++`→`FLAC++`;
`src/libQLAC/…` ↔ `src/libFLAC/…`; `include/share/…` ↔ itself.

### Verbatim-renamed (mechanical rename only)

| File(s) | Upstream (libFLAC 1.5.0) | Class | Notes |
|---|---|---|---|
| `include/QLAC/{assert,export,ordinals}.h` | `include/FLAC/{assert,export,ordinals}.h` | verbatim-renamed | `ordinals.h` drops the `FLAC__bool` typedef; `export.h` doxygen group case only. |
| `include/share/{alloc,compat,endswap}.h` | `include/share/{alloc,compat,endswap}.h` | verbatim-renamed | `alloc.h` keeps its own `alloc - …` descriptor line. |
| `src/libQLAC/{bitmath,cpu,fixed,float,lpc,memory,window}.c` | `src/libFLAC/{same}.c` | verbatim-renamed | `FLAC__bool`→`bool`, macro-name case, `SUBFRAME`→`FRAME` constant. |
| `src/libQLAC/fixed_intrin_{avx2,sse2,sse42,ssse3}.c` | `src/libFLAC/fixed_intrin_{…}.c` | verbatim-renamed | SIMD fixed-predictor kernels. |
| `src/libQLAC/lpc_intrin_{avx2,fma,neon,sse2,sse41}.c` | `src/libFLAC/lpc_intrin_{…}.c` | verbatim-renamed | SIMD LPC kernels. |
| `src/libQLAC/include/private/{bitmath,cpu,fixed,float,lpc,macros,window}.h` | `src/libFLAC/include/private/{same}.h` | verbatim-renamed | `bitmath.h` adds a `share/compat.h` include; others `FLAC__bool`→`bool`. |
| `src/libQLAC/deduplication/{bitreader_read_rice_signed_block, lpc_compute_autocorrelation_intrin, lpc_compute_autocorrelation_intrin_neon, lpc_compute_autocorrelation_intrin_sse2}.c` | `src/libFLAC/deduplication/{same}.c` | verbatim-renamed | **`#include`d code fragments**, not standalone units; header-less upstream and here (see note below). |

### Modified (FLAC-derived, then substantially changed)

| File | Upstream (libFLAC 1.5.0) | Class | Notes |
|---|---|---|---|
| `include/QLAC/format.h` | `include/FLAC/format.h` | modified | Reduced to the single-block subset; stream/metadata/Ogg format types removed. |
| `include/QLAC/stream_encoder.h` | `include/FLAC/stream_encoder.h` | modified | Public API reshaped for block-at-a-time, caller-owned-buffer encoding. |
| `include/QLAC/stream_decoder.h` | `include/FLAC/stream_decoder.h` | modified | Public API reshaped for block-at-a-time decoding. |
| `include/QLAC/memory.h` | `src/libFLAC/include/private/memory.h` | modified | Private helper **promoted to public** and rewritten: new `QLAC__MEMORY_ALIGNMENT (32)` contract, `extern "C"`, `QLAC_API` export, curated aligned-alloc subset, new documentation for the caller-owned buffer model. |
| `src/libQLAC/format.c` | `src/libFLAC/format.c` | modified | Trimmed to the retained format definitions/limits. |
| `src/libQLAC/stream_encoder.c` | `src/libFLAC/stream_encoder.c` | modified | Rewritten as a single-block, header-free encoder (see modifications summary). |
| `src/libQLAC/stream_decoder.c` | `src/libFLAC/stream_decoder.c` | modified | Rewritten as a single-block, header-free decoder. |
| `src/libQLAC/stream_encoder_framing.c` | `src/libFLAC/stream_encoder_framing.c` | modified | Frame/subframe/metadata framing replaced by QLAC per-block payload writers. |
| `src/libQLAC/bitreader.c` | `src/libFLAC/bitreader.c` | modified | CRC-16 running-checksum machinery (state fields, `crc16_update_*`, `reset_read_crc16`, `get_read_crc16`, `private/crc.h`) removed. |
| `src/libQLAC/bitwriter.c` | `src/libFLAC/bitwriter.c` | modified | CRC-8/CRC-16 buffer helpers, `release_buffer`, and the unused Golomb-write block removed; CRC/format/stream-encoder includes dropped. |
| `src/libQLAC/include/private/bitreader.h` | `src/libFLAC/include/private/bitreader.h` | modified | CRC function declarations removed (matches `bitreader.c`). |
| `src/libQLAC/include/private/bitwriter.h` | `src/libFLAC/include/private/bitwriter.h` | modified | `release_buffer` declaration removed. |
| `src/libQLAC/include/private/memory.h` | `src/libFLAC/include/private/memory.h` | modified | Restructured: now includes the new public `QLAC/memory.h`; declares only the internal-only aligned-array variants. |
| `src/libQLAC/include/private/stream_encoder_framing.h` | `src/libFLAC/include/private/stream_encoder_framing.h` | modified | Interface redesigned: FLAC `subframe`/`frame_header`/`metadata` writers replaced by QLAC `frame_add_{constant,fixed,lpc,verbatim}`. |
| `src/libQLAC/include/protected/stream_encoder.h` | `src/libFLAC/include/protected/stream_encoder.h` | modified | Protected state struct greatly reduced (no channels, sample rate, mid/side, escape coding, partition-order search, metadata, threads, seek/audio offsets, Ogg). |
| `src/libQLAC/include/protected/stream_decoder.h` | `src/libFLAC/include/protected/stream_decoder.h` | modified | Protected state struct reduced to fixed out-of-band block size + bit depth; no channels/sample-rate/MD5/Ogg; `get_input_bytes_unconsumed` dropped. |

### Original (no upstream counterpart, QLAC-authored)

| File | Upstream | Class | Notes |
|---|---|---|---|
| `include/QLAC++/block_codec.hpp` | — | original | RAII C++20 wrapper (`qlac::Encoder`/`Decoder`/`AlignedSamples`) around the block codec. |
| `include/QLAC++/compat_block_info.hpp` | — | original | Block-info compatibility helper. |
| `assessment/assessment.cpp` | — | original | Assessment/benchmark driver. |
| `tests/TestDummy/dummy.cpp` | — | original | Smoke test. |
| `tests/TestPatterns/patterns.cpp` | — | original | Pattern-based round-trip test. |
| `tests/TestRandom/random_blocks.cpp` | — | original | Randomized round-trip test. |
| `tests/TestWav/wav_bench.cpp` | — | original | WAV block-by-block round-trip + timing benchmark. |

### Build glue

| File(s) | Class | Notes |
|---|---|---|
| `CMakeLists.txt`, `src/libQLAC/CMakeLists.txt`, `config.cmake.h.in`, `cmake/CheckA64NEON.{c.in,cmake}`, `cmake/CheckCPUArch.{c.in,cmake}`, `cmake/UseSystemExtensions.cmake`, `src/libQLAC/qlac.pc.in`, `src/libQLAC/version.rc` | derived | Adapted from the corresponding libFLAC 1.5.0 build files. A one-line provenance comment was added; build logic unchanged. |
| `tests/CMakeLists.txt`, `tests/Test{Dummy,Patterns,Random,Wav}/CMakeLists.txt`, `assessment/CMakeLists.txt` | original | QLAC build glue for the original tests/assessment. |

**Note on the `deduplication/*.c` fragments:** these files are `#include`d in the
middle of functions in `bitreader.c`, `lpc.c`, `lpc_intrin_sse2.c` and
`lpc_intrin_neon.c` (the same "textual template" pattern libFLAC uses). They are
not standalone translation units and carry no license header of their own,
either upstream or here; they are covered by the license of the including file
and by `LICENSE`. They were left header-less deliberately.

## Substantive modifications in the MODIFIED files

The FLAC stream/frame state machine was reformulated into a **single-block,
header-free codec**. `stream_encoder.c`/`stream_decoder.c` no longer manage a
stream: there is no STREAMINFO/metadata emission, no seektable, no whole-stream
MD5, no Ogg, no channel decorrelation or mid/side stereo, no multithreading, and
no verify decoder. Each call encodes or decodes exactly one mono block whose bit
depth and block size are fixed and agreed **out of band** (they are not written
into the bitstream). The encoder no longer owns the audio-sample buffer: the
**caller supplies an aligned buffer** honoring the library's SIMD alignment and
lead-in contract, which is why the aligned-allocation helpers were promoted from
`private/memory.h` to the public `QLAC/memory.h` with an explicit
`QLAC__MEMORY_ALIGNMENT` constant.

Correspondingly, `stream_encoder_framing.c/.h` replace FLAC's subframe/frame-header/
metadata framing with a compact **per-block payload format**: the QLAC frame
writers (`QLAC__frame_add_constant/fixed/lpc/verbatim`) emit just the predictor
selection, warm-up samples, quantized LPC coefficients, and Rice-coded residual
partitions for one block, with none of FLAC's frame header, sync code, blocking
strategy bits, or per-frame CRC. Because there is no frame header or stream sync
to protect, the CRC-8/CRC-16 support was removed from `bitreader.c`/`bitwriter.c`
and their private headers, and the protected encoder/decoder state structs were
reduced to only the fields the block codec actually uses.

## Licensing

FLAC-derived files (verbatim-renamed and modified) **retain the upstream Xiph
BSD-style license** shipped in `COPYING.Xiph` and continue to be governed by it.
QLAC-original files, and the QLAC modifications to derived files, are
**© 2025 Leonardo Severi** and released under the **same BSD-3-Clause / Xiph-style
terms** (`COPYING.QLAC`). `LICENSE` explains the split and how the two notices
apply per file.
