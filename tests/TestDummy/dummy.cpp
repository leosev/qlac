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

static constexpr uint32_t BLOCKSIZE = 128;
static constexpr uint32_t BPS = 32;

int main()
{
    const qlac::Config cfg{ .bits_per_sample = BPS, .blocksize = BLOCKSIZE };

    qlac::Encoder enc(cfg, /*compression_level=*/5);
    qlac::Decoder dec(cfg);

    // Fill with a recognisable pattern that exercises the full int32 range.
    auto &in = enc.input_samples();
    for(uint32_t i = 0; i < BLOCKSIZE; ++i)
        in[i] = static_cast<QLAC__int32>(i * 1000000 - 63500000);

    // Save originals before encode() may right-shift them (wasted-bits step).
    std::array<QLAC__int32, BLOCKSIZE> orig;
    for(uint32_t i = 0; i < BLOCKSIZE; ++i)
        orig[i] = in[i];

    // Encode into a stack buffer large enough for any block this size.
    std::array<std::byte, 1 << 20> frame_buf{};
    std::span<std::byte> frame = enc.encode(frame_buf);

    std::printf("encoded %zu bytes\n", frame.size());

    // Decode the frame.
    std::span<QLAC__int32> out = dec.decode(frame);

    // Verify.
    bool ok = true;
    for(uint32_t i = 0; i < BLOCKSIZE; ++i) {
        if(out[i] != orig[i]) {
            std::fprintf(stderr, "MISMATCH at sample %u: expected %d, got %d\n",
                         i, orig[i], out[i]);
            ok = false;
        }
    }

    if(ok)
        std::puts("PASS: all 128 samples match");
    else
        std::puts("FAIL");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
