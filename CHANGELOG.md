# Changelog

All notable changes to QLAC are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

> **Note:** QLAC is 0.x software. The bitstream/payload format and the codec API are
> **unstable and may change without backward compatibility between releases.**

## [0.1.0] - 2026-07-16

### Added

- Initial public release of QLAC, derived from libFLAC 1.5.0.
- Single-block, header-free lossless codec: each block is independently decodable.
- C library `libQLAC` plus a header-only C++20 wrapper (`QLAC++`).
- CMake-only build.
- Round-trip correctness and timing tests.
- A Linux real-time assessment harness (the benchmark from the paper).

[0.1.0]: https://github.com/leosev/qlac/releases/tag/v0.1.0
