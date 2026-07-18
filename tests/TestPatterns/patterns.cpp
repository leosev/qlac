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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <chrono>
static constexpr uint32_t BLOCKSIZE = 128;

struct TestCase {
	const char *name;
	uint32_t bps;
	std::array<int32_t, BLOCKSIZE> samples;
};

static bool run(const TestCase &tc)
{
	const qlac::Config cfg { .bits_per_sample = tc.bps, .blocksize = BLOCKSIZE };

	qlac::Encoder enc(cfg, /*compression_level=*/12, "rectangle");
	qlac::Decoder dec(cfg);

	auto &in = enc.input_samples();
    	std::array<std::byte, 1 << 20> frame_buf { };
	auto start = std::chrono::steady_clock::now();
	std::array<int32_t, BLOCKSIZE> orig = tc.samples;
	for(uint32_t i = 0; i < BLOCKSIZE; ++i)
		in[i] = orig[i];
	std::span<std::byte> frame = enc.encode(frame_buf);
	auto after_enc = std::chrono::steady_clock::now();
	std::span<int32_t> out = dec.decode(frame);
	auto after_dec = std::chrono::steady_clock::now();
	bool ok = true;
	for(uint32_t i = 0; i < BLOCKSIZE; ++i) {
		if(out[i] != orig[i]) {
			std::fprintf(stderr, "  MISMATCH sample %u: expected %d, got %d\n",
						 i, orig[i], out[i]);
			ok = false;
		}
	}
	std::printf("%-30s  %2u-bit  %4zu bytes  %s, enc_time: %ld, dec_time: %ld\n",
				tc.name, tc.bps, frame.size(), ok ? "PASS" : "FAIL", std::chrono::duration_cast<std::chrono::nanoseconds>(after_enc - start).count(), std::chrono::duration_cast<std::chrono::nanoseconds>(after_dec - after_enc).count());
	return ok;
}

static std::array<int32_t, BLOCKSIZE> make_constant(int32_t value)
{
	std::array<int32_t, BLOCKSIZE> a { };
	a.fill(value);
	return a;
}

static std::array<int32_t, BLOCKSIZE> make_sine(uint32_t bps)
{
	const double amp = (bps == 32)
						   ? static_cast<double>(INT32_MAX)
						   : static_cast<double>((1 << 15) - 1);

	std::array<int32_t, BLOCKSIZE> a { };
	for(uint32_t i = 0; i < BLOCKSIZE; ++i)
		a[i] = static_cast<int32_t>(
			std::round(amp * std::sin(4.0 * std::numbers::pi * i / BLOCKSIZE))); // 4 periods
	return a;
}

int main()
{
	std::array<TestCase, 5> cases { {
		{ "constant INT32_MIN", 32, make_constant(INT32_MIN) },
		{ "constant INT32_MAX", 32, make_constant(INT32_MAX) },
		{ "constant INT16_MAX", 16, make_constant(INT16_MAX) },
		{ "sine", 32, make_sine(32) },
		{ "sine", 16, make_sine(16) },
	} };

	bool all_ok = true;
	for(auto &tc : cases) {
		all_ok &= run(tc);
	}

	std::puts(all_ok ? "\nALL PASS" : "\nSOME FAILED");
	return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
