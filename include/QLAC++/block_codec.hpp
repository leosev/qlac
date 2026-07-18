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

/* QLAC++ - C++ wrapper for the block-codec libQLAC fork
 *
 * A thin, RAII C++ wrapper around the format-breaking single-block libQLAC
 * fork (see FORK_NOTES.md).  Each Encoder/Decoder instance handles exactly one
 * mono QLAC block per call, with fixed out-of-band parameters (bits-per-sample,
 * blocksize) agreed between the two sides.
 *
 * The wrapper exposes:
 *   - qlac::AlignedSamples : a std::span-like view that owns an aligned int32
 *     sample buffer honouring the library's alignment + leading-pad contract.
 *   - qlac::Encoder        : owns its (aligned) input sample buffer; encode().
 *   - qlac::Decoder        : owns its (aligned) output sample buffer; decode().
 *
 * Requires C++20 (std::span).
 */

#ifndef QLACPP__BLOCK_CODEC_HPP
#define QLACPP__BLOCK_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include "QLAC/stream_encoder.h"
#include "QLAC/stream_decoder.h"
#include <QLAC/memory.h>
#include "compat_block_info.hpp"
namespace qlac {

	/** Error thrown by the wrapper on any setup, allocation or codec failure.
	 *  The message embeds the underlying FLAC state/init string when available.
	 */
	class Error : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	/** A std::span-like owning view over an aligned QLAC__int32 sample buffer.
	 *
	 *  The block codecs require their sample buffers to be:
	 *    - aligned to QLAC__MEMORY_ALIGNMENT (== get_*_alignment()), and
	 *    - preceded by a small zero-filled "leading pad" of samples at negative
	 *      indices (read-behind by the vectorized LPC / autocorrelation kernels).
	 *
	 *  AlignedSamples allocates `pad + size` aligned int32, zeroes the pad, and
	 *  presents a span starting at the first *real* sample.  It behaves like a
	 *  std::span<QLAC__int32> for the real region (data()/size()/operator[]/
	 *  begin()/end()), while retaining ownership of the whole padded allocation.
	 *
	 *  Move-only (it owns a heap allocation).
	 */
	class AlignedSamples {
	public:
		using value_type = QLAC__int32;
		using size_type = std::size_t;
		using iterator = QLAC__int32 *;
		using const_iterator = const QLAC__int32 *;

		AlignedSamples() = default;

		/** Allocate `count` real samples preceded by `leading_pad` zeroed samples,
		 *  with the aligned base honouring `alignment` (must be a power of two and
		 *  >= alignof(QLAC__int32)).  Throws flac::Error on allocation failure.
		 *
		 *  Prefer the for_encoder_input() / for_decoder_output() factories, which
		 *  pull the exact count/pad/alignment from a configured codec instance.
		 */
		AlignedSamples(size_type count, size_type leading_pad, size_type alignment)
			: count_(count)
			, pad_(leading_pad)
		{
			// The library's allocator guarantees QLAC__MEMORY_ALIGNMENT and is
			// the only alignment the block codecs ask for; anything stricter
			// would not be honoured here.
			if(alignment > QLAC__MEMORY_ALIGNMENT)
				throw Error("flac::AlignedSamples: alignment exceeds QLAC__MEMORY_ALIGNMENT");

			// Allocate the padded buffer through the library so the alignment
			// (and lead-in) contract lives in one place (QLAC/memory.h).
			QLAC__int32 *raw = nullptr;
			if(!QLAC__memory_alloc_aligned_int32_array(pad_ + count_, &raw, &base_))
				throw Error("flac::AlignedSamples: out of memory");
			raw_ = raw;

			// Zero the leading pad; the first real sample sits at base_[pad_].
			for(size_type i = 0; i < pad_; ++i)
				base_[i] = 0;
			samples_ = base_ + pad_;
		}

		~AlignedSamples() { std::free(raw_); }

		AlignedSamples(const AlignedSamples &) = delete;
		AlignedSamples &operator=(const AlignedSamples &) = delete;

		AlignedSamples(AlignedSamples &&o) noexcept { move_from(o); }
		AlignedSamples &operator=(AlignedSamples &&o) noexcept
		{
			if(this != &o) {
				std::free(raw_);
				move_from(o);
			}
			return *this;
		}

		/** Pointer to the first real sample (what the codec wants passed in). */
		QLAC__int32 *data() noexcept { return samples_; }
		const QLAC__int32 *data() const noexcept { return samples_; }

		/** Number of real samples (== blocksize). */
		size_type size() const noexcept { return count_; }
		bool empty() const noexcept { return count_ == 0; }

		/** Size of the zero-filled leading pad, in samples. */
		size_type leading_pad() const noexcept { return pad_; }

		QLAC__int32 &operator[](size_type i) noexcept { return samples_[i]; }
		const QLAC__int32 &operator[](size_type i) const noexcept { return samples_[i]; }

		iterator begin() noexcept { return samples_; }
		iterator end() noexcept { return samples_ + count_; }
		const_iterator begin() const noexcept { return samples_; }
		const_iterator end() const noexcept { return samples_ + count_; }

		/** A std::span view of the real sample region. */
		std::span<QLAC__int32> span() noexcept { return { samples_, count_ }; }
		std::span<const QLAC__int32> span() const noexcept { return { samples_, count_ }; }

		operator std::span<QLAC__int32>() noexcept { return span(); }
		operator std::span<const QLAC__int32>() const noexcept { return span(); }

	private:
		void move_from(AlignedSamples &o) noexcept
		{
			raw_ = o.raw_;
			base_ = o.base_;
			samples_ = o.samples_;
			count_ = o.count_;
			pad_ = o.pad_;
			o.raw_ = nullptr;
			o.base_ = nullptr;
			o.samples_ = nullptr;
			o.count_ = 0;
			o.pad_ = 0;
		}

		void *raw_ = nullptr; // free()-able base
		QLAC__int32 *base_ = nullptr; // aligned base (start of pad)
		QLAC__int32 *samples_ = nullptr; // first real sample (base_ + pad_)
		size_type count_ = 0;
		size_type pad_ = 0;
	};

	/** Out-of-band parameters shared by both sides of the codec. */
	struct Config {
		std::uint32_t bits_per_sample = 16; ///< 4..32
		std::uint32_t blocksize = 128; ///< fixed samples per block
	};

	/** Single-block QLAC encoder.
	 *
	 *  Owns its aligned input sample buffer (allocated to the encoder's exact
	 *  contract).  Fill input_samples() with one block, then call encode() to
	 *  produce the frame bytes into a caller-provided output span.
	 */
	class Encoder {
	private:
		QLAC__StreamEncoder *enc_ = nullptr;
		AlignedSamples input_;

		// --- thin member wrappers over the QLAC__ C API ---
		static QLAC__StreamEncoder *new_encoder() noexcept { return QLAC__stream_encoder_new(); }
		void delete_encoder() noexcept { QLAC__stream_encoder_delete(enc_); }
		void finish() noexcept { QLAC__stream_encoder_finish(enc_); }

		bool set_bits_per_sample(std::uint32_t v) noexcept
		{
			return QLAC__stream_encoder_set_bits_per_sample(enc_, v);
		}
		bool set_blocksize(std::uint32_t v) noexcept
		{
			return QLAC__stream_encoder_set_blocksize(enc_, v);
		}
		bool set_compression_level(std::uint32_t v) noexcept
		{
			return QLAC__stream_encoder_set_compression_level(enc_, v);
		}
		QLAC__StreamEncoderInitStatus init_stream() noexcept
		{
			return QLAC__stream_encoder_init_stream(enc_, nullptr);
		}
		std::uint32_t input_buffer_size() const noexcept
		{
			return QLAC__stream_encoder_get_input_buffer_size(enc_);
		}
		std::uint32_t input_leading_pad() const noexcept
		{
			return QLAC__stream_encoder_get_input_leading_pad(enc_);
		}
		std::uint32_t input_alignment() const noexcept
		{
			return QLAC__stream_encoder_get_input_alignment(enc_);
		}
		bool encode_block(QLAC__int32 *samples, QLAC__byte *out, std::size_t out_size, std::size_t *written) noexcept
		{
			return QLAC__stream_encoder_encode_block(enc_, samples, out, out_size, written);
		}
		const char *state_string() const noexcept
		{
			return QLAC__StreamEncoderStateString[QLAC__stream_encoder_get_state(enc_)];
		}
		static const char *init_status_string(QLAC__StreamEncoderInitStatus st) noexcept
		{
			return QLAC__StreamEncoderInitStatusString[st];
		}

		void destroy() noexcept
		{
			if(enc_) {
				finish();
				delete_encoder();
				enc_ = nullptr;
			}
		}

		[[noreturn]] void fail_setup(const char *what)
		{
			std::string msg = what;
			msg += " failed: ";
			msg += state_string();
			destroy();
			throw Error(msg);
		}

		[[noreturn]] void fail_state(const char *what)
		{
			std::string msg = what;
			msg += " failed: ";
			msg += state_string();
			throw Error(msg);
		}
		void set_apodization(const char *apodization)
		{
			if(!QLAC__stream_encoder_set_apodization(enc_, apodization)) {
				throw Error("QLAC__stream_encoder_set_apodization");
			}
		}
	public:
		/** Construct, configure and initialize a block encoder, then allocate the
		 *  aligned input buffer.  `compression_level` maps to
		 *  QLAC__stream_encoder_set_compression_level (0..8).  Throws flac::Error
		 *  on any setup failure.
		 */
		Encoder(const Config &cfg, std::uint32_t compression_level = 5, const char *apodization = nullptr)
			: enc_(new_encoder())
		{
			if(!enc_)
				throw Error("QLAC__stream_encoder_new failed");

			if(!set_bits_per_sample(cfg.bits_per_sample) || !set_blocksize(cfg.blocksize) || !set_compression_level(compression_level) /* || !set_do_exhaustive_model_search(true) */) {
				fail_setup("QLAC__stream_encoder_set_*");
			}
			if(apodization != nullptr && std::string_view("default") != apodization){
				set_apodization(apodization);
			}
			const QLAC__StreamEncoderInitStatus st = init_stream();
			if(st != QLAC__STREAM_ENCODER_INIT_STATUS_OK) {
				std::string msg = "QLAC__stream_encoder_init_stream failed: ";
				msg += init_status_string(st);
				destroy();
				throw Error(msg);
			}

			const uint32_t buf_size = input_buffer_size();
			const uint32_t pad = input_leading_pad();
			const uint32_t align = input_alignment();
			input_ = AlignedSamples(buf_size - pad, pad, align);
		}

		~Encoder() { destroy(); }

		Encoder(const Encoder &) = delete;
		Encoder &operator=(const Encoder &) = delete;

		Encoder(Encoder &&o) noexcept
			: enc_(std::exchange(o.enc_, nullptr))
			, input_(std::move(o.input_))
		{
		}
		Encoder &operator=(Encoder &&o) noexcept
		{
			if(this != &o) {
				destroy();
				enc_ = std::exchange(o.enc_, nullptr);
				input_ = std::move(o.input_);
			}
			return *this;
		}

		/** The owned, aligned input sample buffer (blocksize samples).  Write the
		 *  block to encode here.  NOTE: encode() may modify these samples in place
		 *  (wasted-bits right-shift), so refill before each call.
		 */
		AlignedSamples &input_samples() noexcept { return input_; }
		const AlignedSamples &input_samples() const noexcept { return input_; }

		/** Encode the current input block into `out`.  Returns a span over the
		 *  portion of `out` that was written (the encoded frame bytes).  Throws
		 *  flac::Error if `out` is too small or encoding fails.
		 */
		std::span<std::byte> encode(std::span<std::byte> out)
		{
			std::size_t written = 0;
			const bool ok = encode_block(
				input_.data(),
				reinterpret_cast<QLAC__byte *>(out.data()),
				out.size(),
				&written);
			if(!ok)
				fail_state("QLAC__stream_encoder_encode_block");
			return out.subspan(0, written);
		}
		block_info create_block_info()
		{
			return { *QLAC__stream_encoder_get_frame_descriptor(enc_) };
		}
		QLAC__StreamEncoder *handle() noexcept { return enc_; }

	};
	/** Single-block FLAC decoder.
	 *
	 *  Owns its aligned output sample buffer (allocated to the decoder's exact
	 *  contract).  Call decode() with the encoded frame bytes; decoded samples
	 *  land in output_samples().
	 */
	class Decoder {
	private:
		QLAC__StreamDecoder *dec_ = nullptr;
		AlignedSamples output_;

		// --- thin member wrappers over the QLAC__ C API ---
		void delete_decoder() noexcept { QLAC__stream_decoder_delete(dec_); }
		void finish() noexcept { QLAC__stream_decoder_finish(dec_); }

		bool set_bits_per_sample(std::uint32_t v) noexcept
		{
			return QLAC__stream_decoder_set_bits_per_sample(dec_, v);
		}
		bool set_blocksize(std::uint32_t v) noexcept
		{
			return QLAC__stream_decoder_set_blocksize(dec_, v);
		}
		QLAC__StreamDecoderInitStatus init_block() noexcept
		{
			return QLAC__stream_decoder_init_block(dec_);
		}
		std::uint32_t output_buffer_size() const noexcept
		{
			return QLAC__stream_decoder_get_output_buffer_size(dec_);
		}
		constexpr static std::uint32_t output_leading_pad()
		{
			return QLAC__STREAM_DECODER_OUTPUT_LEADING_PAD;
		}
		constexpr static std::uint32_t output_alignment()
		{
			return QLAC__MEMORY_ALIGNMENT;
		}
		bool decode_block(const QLAC__byte *in, std::size_t in_size, QLAC__int32 *out) noexcept
		{
			return QLAC__stream_decoder_decode_block(dec_, in, in_size, out);
		}
		const char *state_string() const noexcept
		{
			return QLAC__StreamDecoderStateString[QLAC__stream_decoder_get_state(dec_)];
		}
		static const char *init_status_string(QLAC__StreamDecoderInitStatus st) noexcept
		{
			return QLAC__StreamDecoderInitStatusString[st];
		}

		void destroy() noexcept
		{
			if(dec_) {
				finish();
				delete_decoder();
				dec_ = nullptr;
			}
		}

		[[noreturn]] void fail_setup(const char *what)
		{
			std::string msg = what;
			msg += " failed: ";
			msg += state_string();
			destroy();
			throw Error(msg);
		}

		[[noreturn]] void fail_state(const char *what)
		{
			std::string msg = what;
			msg += " failed: ";
			msg += state_string();
			throw Error(msg);
		}

	public:
		/** Construct, configure and initialize a block decoder, then allocate the
		 *  aligned output buffer.  The Config must match the encoder's.  Throws
		 *  flac::Error on any setup failure.
		 */
		Decoder(const Config &cfg)
			: dec_(QLAC__stream_decoder_new())
		{
			if(!dec_)
				throw Error("QLAC__stream_decoder_new failed");

			if(!set_bits_per_sample(cfg.bits_per_sample) || !set_blocksize(cfg.blocksize)) {
				fail_setup("QLAC__stream_decoder_set_*");
			}

			const QLAC__StreamDecoderInitStatus st = init_block();
			if(st != QLAC__STREAM_DECODER_INIT_STATUS_OK) {
				std::string msg = "QLAC__stream_decoder_init_block failed: ";
				msg += init_status_string(st);
				destroy();
				throw Error(msg);
			}

			const uint32_t pad = output_leading_pad();
			output_ = AlignedSamples(output_buffer_size() - pad, pad, output_alignment());
		}

		~Decoder() { destroy(); }

		Decoder(const Decoder &) = delete;
		Decoder &operator=(const Decoder &) = delete;

		Decoder(Decoder &&o) noexcept
			: dec_(std::exchange(o.dec_, nullptr))
			, output_(std::move(o.output_))
		{
		}
		Decoder &operator=(Decoder &&o) noexcept
		{
			if(this != &o) {
				destroy();
				dec_ = std::exchange(o.dec_, nullptr);
				output_ = std::move(o.output_);
			}
			return *this;
		}

		/** The owned, aligned output sample buffer (blocksize samples).  After a
		 *  successful decode() the decoded block lives here.
		 */
		AlignedSamples &output_samples() noexcept { return output_; }
		const AlignedSamples &output_samples() const noexcept { return output_; }

		/** Decode one frame from `in` into output_samples().  Returns a span over
		 *  the decoded samples (== blocksize).  Throws flac::Error on malformed or
		 *  truncated input.
		 */
		std::span<QLAC__int32> decode(std::span<const std::byte> in)
		{
			const bool ok = decode_block(
				reinterpret_cast<const QLAC__byte *>(in.data()),
				in.size(),
				output_.data());
			if(!ok)
				fail_state("QLAC__stream_decoder_decode_block");
			return output_.span();
		}

		QLAC__StreamDecoder *handle() noexcept { return dec_; }
	};

} // namespace flac

#endif // QLACPP__BLOCK_CODEC_HPP
