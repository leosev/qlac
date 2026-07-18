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

/* TestWav - encode/decode a mono WAV file block-by-block and collect timings.
 *
 * Usage:  TestWav <input.wav> <blocksize>
 *
 * Reads a mono WAV file (libsndfile), splits its samples into fixed-size blocks
 * and, for every whole block, round-trips it through the libQLAC block codec.
 * For each block it records, into pre-sized arrays (no reallocation):
 *   - the encode time (nanoseconds),
 *   - the decode time (nanoseconds),
 *   - the encoded frame size (bytes),
 * and verifies that the decoded samples exactly match the input.
 *
 * Trailing samples that do not fill a whole block are dropped.  Nothing is
 * printed to stdout/stderr on success; the exit code reports the result.
 */

#include <QLAC++/block_codec.hpp>

#include <argparse/argparse.hpp>
#include <sndfile.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <vector>
#include <cstdio>

namespace {

	using Clock = std::chrono::steady_clock;
	using Nanos = std::chrono::nanoseconds;

	/** Parsed command-line arguments. */
	struct Args {
		std::string wav_path;
		std::uint32_t blocksize = 0;
	};

	/** A mono signal loaded fully into memory as int32 samples. */
	struct MonoSignal {
		std::vector<std::int32_t> samples;
		int bits_per_sample = 0;
	};
	template <typename T>
	static double mean(const std::vector<T> &data)
	{
		double acc = 0.;
		for(const T &t : data) {
			acc += t;
		}
		return acc / data.size();
	}
	template <typename T>
	static T max(const std::vector<T> &data)
	{
		T acc = 0.;
		for(const T &t : data) {
			if(t > acc) {
				acc = t;
			}
		}
		return acc;
	}
	/** Per-block measurements, each vector sized to block_count up front. */
	struct Measurements {
		std::vector<std::int64_t> encode_ns;
		std::vector<std::int64_t> decode_ns;
		std::vector<std::size_t> encoded_bytes;
		std::size_t const_frames = 0,
					verb_frames = 0,
					fixed_frames = 0,
					lpc_frames = 0;

		explicit Measurements(std::size_t block_count)
			: encode_ns(block_count)
			, decode_ns(block_count)
			, encoded_bytes(block_count)
		{
		}
	};
	/** Parse argv via argparse: positional WAV path and integral blocksize. */
	Args parse_args(int argc, char **argv)
	{
		argparse::ArgumentParser program("TestWav");
		program.add_argument("wav").help("path to a mono WAV input file");
		program.add_argument("blocksize")
			.help("samples per encoded block")
			.scan<'u', std::uint32_t>();

		program.parse_args(argc, argv);

		Args args;
		args.wav_path = program.get<std::string>("wav");
		args.blocksize = program.get<std::uint32_t>("blocksize");
		if(args.blocksize == 0)
			throw std::runtime_error("blocksize must be > 0");
		return args;
	}

	/** Map libsndfile's subtype to the bits-per-sample the codec config expects. */
	std::uint32_t bits_for_config(int bits_per_sample)
	{
		// The codec supports 4..32 bps; libsndfile reports 8/16/24/32 for PCM.
		return static_cast<std::uint32_t>(bits_per_sample);
	}

	/** Read an entire mono WAV file into int32 samples. */
	MonoSignal load_mono_wav(const std::string &path)
	{
		SF_INFO info { };
		SNDFILE *snd = sf_open(path.c_str(), SFM_READ, &info);
		if(!snd)
			throw std::runtime_error("sf_open failed: " + std::string(sf_strerror(nullptr)));

		if(info.channels != 1) {
			sf_close(snd);
			throw std::runtime_error("expected a mono file, got " + std::to_string(info.channels) + " channels");
		}

		MonoSignal sig;
		sig.samples.resize(static_cast<std::size_t>(info.frames));
		sig.bits_per_sample = info.format & SF_FORMAT_SUBMASK; // refined below

		// Map common PCM subtypes to a bit depth; default to 16 otherwise.
		switch(info.format & SF_FORMAT_SUBMASK) {
			case SF_FORMAT_PCM_S8:
			case SF_FORMAT_PCM_U8:
				sig.bits_per_sample = 8;
				break;
			case SF_FORMAT_PCM_16:
				sig.bits_per_sample = 16;
				break;
			case SF_FORMAT_PCM_24:
				sig.bits_per_sample = 24;
				break;
			case SF_FORMAT_PCM_32:
				sig.bits_per_sample = 32;
				break;
			default:
				sig.bits_per_sample = 16;
				break;
		}

		const sf_count_t got = info.frames
								   ? sf_readf_int(snd, sig.samples.data(), info.frames)
								   : 0;
		sf_close(snd);

		if(got != info.frames)
			throw std::runtime_error("short read from WAV file");

		// libsndfile's sf_readf_int returns samples left-justified in 32 bits.
		// Right-shift so the value range matches the declared bits-per-sample.
		const int shift = 32 - sig.bits_per_sample;
		if(shift > 0) {
			for(std::int32_t &s : sig.samples)
				s >>= shift;
		}

		return sig;
	}

	/** Encode then decode every whole block, recording timings and checking
	 *  round-trip equality.  Returns true iff all blocks matched. */
	bool run_blocks(const MonoSignal &sig, std::uint32_t blocksize, Measurements &m)
	{
		const qlac::Config cfg {
			.bits_per_sample = bits_for_config(sig.bits_per_sample),
			.blocksize = blocksize,
		};

		qlac::Encoder enc(cfg, /*compression_level=*/7,"rectangle");
		qlac::Decoder dec(cfg);

		// One reusable output buffer; sized generously for a single frame.
		std::vector<std::byte> frame_buf(static_cast<std::size_t>(blocksize) * sizeof(std::int32_t) + 1024);

		const std::size_t block_count = sig.samples.size() / blocksize;
		bool all_match = true;

		for(std::size_t b = 0; b < block_count; ++b) {
			const std::int32_t *block = sig.samples.data() + b * blocksize;

			// Load the block into the encoder's aligned input buffer.
			auto &in = enc.input_samples();
			for(std::uint32_t i = 0; i < blocksize; ++i)
				in[i] = block[i];

			const auto t0 = Clock::now();
			std::span<std::byte> frame = enc.encode(frame_buf);
			const auto t1 = Clock::now();
			int x = (int)frame[0];
			if(x == 0) { /* CONSTANT */
				++m.const_frames;
			}
			else if(x == 2) { /* VERBATIM */
				++m.verb_frames;
			}
			else if(x <= 24) { /* FIXED, order 0..4 */
				++m.fixed_frames;
			}
			else {
				++m.lpc_frames;
			}
			const auto t2 = Clock::now();
			std::span<std::int32_t> out = dec.decode(frame);
			const auto t3 = Clock::now();

			m.encode_ns[b] = std::chrono::duration_cast<Nanos>(t1 - t0).count();
			m.decode_ns[b] = std::chrono::duration_cast<Nanos>(t3 - t2).count();
			m.encoded_bytes[b] = frame.size();

			// encode() may shift the input in place (wasted bits), so compare the
			// decoder output against the pristine source block.
			for(std::uint32_t i = 0; i < blocksize; ++i) {
				if(out[i] != block[i]) {
					all_match = false;
					break;
				}
			}
		}

		return all_match;
	}

} // namespace

int main(int argc, char **argv)
{
	try {
		const Args args = parse_args(argc, argv);
		const MonoSignal sig = load_mono_wav(args.wav_path);

		const std::size_t block_count = sig.samples.size() / args.blocksize;
		Measurements m(block_count);

		const bool ok = run_blocks(sig, args.blocksize, m);
		std::printf("Mean... Encode: %f, Decode: %f, Size: %f\n", mean(m.encode_ns), mean(m.decode_ns), mean(m.encoded_bytes));
		std::printf("Max... Encode: %ld, Decode: %ld Size: %lu\n", max(m.encode_ns), max(m.decode_ns), max(m.encoded_bytes));
		std::printf("Const: %lu, Verbatim: %lu, Fixed: %lu, LPC: %lu\n", m.const_frames,m.verb_frames,m.fixed_frames,m.lpc_frames);
		return ok ? EXIT_SUCCESS : EXIT_FAILURE;
	} catch(const std::exception &) {
		return EXIT_FAILURE;
	}
}
