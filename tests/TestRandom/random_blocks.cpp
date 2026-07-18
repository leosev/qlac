/* QLAC — a low-latency lossless audio codec
 *
 * This file is original QLAC code and is not derived from FLAC.
 *
 * Copyright (C) 2025  Leonardo Severi
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <QLAC++/block_codec.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

static constexpr uint32_t BLOCKSIZE = 128;
static constexpr int      NUM_BLOCKS = 5;

static bool test_blocks(uint32_t bps, std::mt19937 &rng)
{
    const qlac::Config cfg{ .bits_per_sample = bps, .blocksize = BLOCKSIZE };

    qlac::Encoder enc(cfg, /*compression_level=*/5);
    qlac::Decoder dec(cfg);

    // Mask to clamp random values to the valid signed range for this bps.
    const int32_t max_val = (bps == 32) ? INT32_MAX : ((1 << (bps - 1)) - 1);
    const int32_t min_val = -max_val - 1;

    std::uniform_int_distribution<int32_t> dist(min_val, max_val);

    std::array<std::byte, 1 << 20> frame_buf{};
    std::array<int32_t, BLOCKSIZE>  orig{};

    bool all_ok = true;

    for(int blk = 0; blk < NUM_BLOCKS; ++blk) {
        auto &in = enc.input_samples();
        for(uint32_t i = 0; i < BLOCKSIZE; ++i) {
            in[i] = dist(rng);
            orig[i] = in[i];
        }

        std::span<std::byte> frame = enc.encode(frame_buf);

        std::span<int32_t> out = dec.decode(frame);

        bool blk_ok = true;
        for(uint32_t i = 0; i < BLOCKSIZE; ++i) {
            if(out[i] != orig[i]) {
                std::fprintf(stderr,
                    "[%u-bit block %d] MISMATCH sample %u: expected %d, got %d\n",
                    bps, blk, i, orig[i], out[i]);
                blk_ok = false;
            }
        }

        std::printf("[%u-bit block %d] encoded %zu bytes — %s\n",
            bps, blk, frame.size(), blk_ok ? "PASS" : "FAIL");

        all_ok &= blk_ok;
    }

    return all_ok;
}

int main()
{
    std::mt19937 rng(42);

    bool ok = true;
    ok &= test_blocks(32, rng);
    ok &= test_blocks(16, rng);

    std::puts(ok ? "\nALL BLOCKS PASS" : "\nSOME BLOCKS FAILED");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
