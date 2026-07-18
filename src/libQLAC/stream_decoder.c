/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0, then
 * substantially modified for QLAC (single-block, header-free operation).  The
 * original libFLAC copyright and license below are retained and govern the
 * FLAC-derived portions; modifications for QLAC are covered by the notice
 * below.  See FORK_NOTES.md and LICENSE.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2000-2009  Josh Coalson
 * Copyright (C) 2011-2025  Xiph.Org Foundation
 *
 * Modifications for QLAC:
 * Copyright (C) 2025  Leonardo Severi
 * Released under the same BSD-style terms (see COPYING.QLAC).
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
 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This fork of libFLAC is a pure block decoder: the exact inverse of the block
 * encoder in stream_encoder.c.  Each call to QLAC__stream_decoder_decode_block()
 * decodes one independent mono block from a caller-supplied byte buffer into a
 * caller-supplied sample buffer.  There is:
 *   - no stream, no "fLaC" sync, no metadata (STREAMINFO, seektable, ...);
 *   - no frame header, no frame number, no CRC-8 / CRC-16, no MD5;
 *   - no channels, no mid/side, no subframe arrays (the frame holds exactly one
 *     mono predictor);
 *   - a single Rice partition ( order always 0);
 *   - no I/O (no FILE/stdin), no Ogg, no seeking, no multithreading.
 *
 * The on-the-wire layout decoded here is described in BLOCK_FORMAT.md and is
 * produced by QLAC__frame_add_{constant,fixed,lpc,verbatim} in
 * stream_encoder_framing.c.  All of blocksize, bits-per-sample and "mono" are
 * fixed out-of-band parameters configured before init.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h> /* for memset/memcpy() */
#include <QLAC/assert.h>
#include <QLAC/memory.h>
#include <share/alloc.h>
#include <protected/stream_decoder.h>
#include <private/bitreader.h>
#include <private/bitmath.h>
#include <private/cpu.h>
#include <private/fixed.h>
#include <QLAC/format.h>
#include <private/lpc.h>
#include <private/macros.h>


/***********************************************************************
 *
 * Private class method prototypes
 *
 ***********************************************************************/

static void set_defaults_(QLAC__StreamDecoder *decoder);
static bool block_read_callback_(QLAC__byte buffer[], size_t *bytes, void *client_data);
static bool read_frame_constant_(QLAC__StreamDecoder *decoder, uint32_t bps, QLAC__int32 *out);
static bool read_frame_fixed_(QLAC__StreamDecoder *decoder, uint32_t bps, uint32_t order, QLAC__int32 *out);
static bool read_frame_lpc_(QLAC__StreamDecoder *decoder, uint32_t bps, uint32_t order, QLAC__int32 *out);
static bool read_frame_verbatim_(QLAC__StreamDecoder *decoder, uint32_t bps, QLAC__int32 *out);
static bool read_residual_rice_(QLAC__StreamDecoder *decoder, uint32_t predictor_order, QLAC__int32 *residual);
static bool read_zero_padding_(QLAC__StreamDecoder *decoder);

/***********************************************************************
 *
 * Private class data
 *
 ***********************************************************************/

typedef struct QLAC__StreamDecoderPrivate {
	QLAC__BitReader *input;
	QLAC__int32 *residual; /* aligned; the real pointer to free() is residual_unaligned */
	QLAC__int32 *residual_unaligned;
	QLAC__CPUInfo cpuinfo;
	bool (*local_bitreader_read_rice_signed_block)(QLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter);

	/* current-block input buffer, fed to the bitreader via block_read_callback_ */
	const QLAC__byte *in;
	size_t in_bytes;
	size_t in_pos;
} QLAC__StreamDecoderPrivate;

/***********************************************************************
 *
 * Public static class data
 *
 ***********************************************************************/

QLAC_API const char * const QLAC__StreamDecoderStateString[] = {
	"QLAC__STREAM_DECODER_OK",
	"QLAC__STREAM_DECODER_END_OF_STREAM",
	"QLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR",
	"QLAC__STREAM_DECODER_CLIENT_ERROR",
	"QLAC__STREAM_DECODER_UNINITIALIZED"
};

QLAC_API const char * const QLAC__StreamDecoderInitStatusString[] = {
	"QLAC__STREAM_DECODER_INIT_STATUS_OK",
	"QLAC__STREAM_DECODER_INIT_STATUS_INVALID_PARAMETERS",
	"QLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR",
	"QLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED"
};

/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/
QLAC_API QLAC__StreamDecoder *QLAC__stream_decoder_new(void)
{
	QLAC__StreamDecoder *decoder;

	QLAC__ASSERT(sizeof(int) >= 4); /* we want to die right away if this is not true */

	decoder = safe_calloc_(1, sizeof(QLAC__StreamDecoder));
	if(decoder == 0) {
		return 0;
	}

	decoder->protected_ = safe_calloc_(1, sizeof(QLAC__StreamDecoderProtected));
	if(decoder->protected_ == 0) {
		free(decoder);
		return 0;
	}

	decoder->private_ = safe_calloc_(1, sizeof(QLAC__StreamDecoderPrivate));
	if(decoder->private_ == 0) {
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	decoder->private_->input = QLAC__bitreader_new();
	if(decoder->private_->input == 0) {
		free(decoder->private_);
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	decoder->private_->residual = 0;
	decoder->private_->residual_unaligned = 0;

	set_defaults_(decoder);

	decoder->protected_->state = QLAC__STREAM_DECODER_UNINITIALIZED;

	return decoder;
}

QLAC_API void QLAC__stream_decoder_delete(QLAC__StreamDecoder *decoder)
{
	if (decoder == NULL)
		return ;

	QLAC__ASSERT(0 != decoder->protected_);
	QLAC__ASSERT(0 != decoder->private_);
	QLAC__ASSERT(0 != decoder->private_->input);

	(void)QLAC__stream_decoder_finish(decoder);

	QLAC__bitreader_delete(decoder->private_->input);

	free(decoder->private_);
	free(decoder->protected_);
	free(decoder);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

QLAC_API bool QLAC__stream_decoder_set_bits_per_sample(QLAC__StreamDecoder *decoder, uint32_t value)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != QLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->protected_->bits_per_sample = value;
	return true;
}

QLAC_API bool QLAC__stream_decoder_set_blocksize(QLAC__StreamDecoder *decoder, uint32_t value)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != QLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->protected_->blocksize = value;
	return true;
}

QLAC_API QLAC__StreamDecoderState QLAC__stream_decoder_get_state(const QLAC__StreamDecoder *decoder)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->state;
}

QLAC_API const char *QLAC__stream_decoder_get_resolved_state_string(const QLAC__StreamDecoder *decoder)
{
	return QLAC__StreamDecoderStateString[decoder->protected_->state];
}

QLAC_API uint32_t QLAC__stream_decoder_get_bits_per_sample(const QLAC__StreamDecoder *decoder)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->bits_per_sample;
}

QLAC_API uint32_t QLAC__stream_decoder_get_blocksize(const QLAC__StreamDecoder *decoder)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->blocksize;
}

QLAC_API QLAC__StreamDecoderInitStatus QLAC__stream_decoder_init_block(QLAC__StreamDecoder *decoder)
{
	QLAC__ASSERT(0 != decoder);

	if(decoder->protected_->state != QLAC__STREAM_DECODER_UNINITIALIZED)
		return QLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED;

	/* validate the fixed out-of-band parameters */
	if(
		decoder->protected_->bits_per_sample < QLAC__MIN_BITS_PER_SAMPLE ||
		decoder->protected_->bits_per_sample > QLAC__MAX_BITS_PER_SAMPLE ||
		decoder->protected_->blocksize < QLAC__MIN_BLOCK_SIZE ||
		decoder->protected_->blocksize > QLAC__MAX_BLOCK_SIZE
	)
		return QLAC__STREAM_DECODER_INIT_STATUS_INVALID_PARAMETERS;

	QLAC__cpu_info(&decoder->private_->cpuinfo);
	decoder->private_->local_bitreader_read_rice_signed_block = QLAC__bitreader_read_rice_signed_block;
#ifdef QLAC__BMI2_SUPPORTED
	if (decoder->private_->cpuinfo.x86.bmi2)
		decoder->private_->local_bitreader_read_rice_signed_block = QLAC__bitreader_read_rice_signed_block_bmi2;
#endif

	if(!QLAC__bitreader_init(decoder->private_->input, block_read_callback_, decoder)) {
		decoder->protected_->state = QLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return QLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR;
	}

	/* allocate the single residual buffer once, at the fixed blocksize */
	if(!QLAC__memory_alloc_aligned_int32_array(decoder->protected_->blocksize, &decoder->private_->residual_unaligned, &decoder->private_->residual)) {
		decoder->protected_->state = QLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return QLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR;
	}

	decoder->private_->in = 0;
	decoder->private_->in_bytes = 0;
	decoder->private_->in_pos = 0;

	decoder->protected_->state = QLAC__STREAM_DECODER_OK;

	return QLAC__STREAM_DECODER_INIT_STATUS_OK;
}

QLAC_API bool QLAC__stream_decoder_finish(QLAC__StreamDecoder *decoder)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->private_);
	QLAC__ASSERT(0 != decoder->protected_);

	if(decoder->protected_->state == QLAC__STREAM_DECODER_UNINITIALIZED)
		return true;

	QLAC__bitreader_free(decoder->private_->input);

	if(0 != decoder->private_->residual_unaligned) {
		free(decoder->private_->residual_unaligned);
		decoder->private_->residual_unaligned = decoder->private_->residual = 0;
	}

	set_defaults_(decoder);

	decoder->protected_->state = QLAC__STREAM_DECODER_UNINITIALIZED;

	return true;
}

QLAC_API uint32_t QLAC__stream_decoder_get_output_buffer_size(const QLAC__StreamDecoder *decoder)
{
	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->protected_);
	/* leading pad for the SIMD LPC restore read-behind, then blocksize samples */
	return decoder->protected_->blocksize + QLAC__STREAM_DECODER_OUTPUT_LEADING_PAD;
}
QLAC_API bool QLAC__stream_decoder_decode_block(QLAC__StreamDecoder *decoder, const QLAC__byte *in, size_t in_bytes, QLAC__int32 *out)
{
	const uint32_t blocksize = decoder->protected_->blocksize;
	const uint32_t bps = decoder->protected_->bits_per_sample;
	const int shift_bits = 32 - (int)bps;
	const QLAC__int32 lower_limit = INT32_MIN >> shift_bits;
	const QLAC__int32 upper_limit = INT32_MAX >> shift_bits;
	QLAC__uint32 x;
	uint32_t wasted_bits, frame_bps;
	uint32_t i;

	QLAC__ASSERT(0 != decoder);
	QLAC__ASSERT(0 != decoder->private_);
	QLAC__ASSERT(0 != decoder->protected_);
	QLAC__ASSERT(0 != in);
	QLAC__ASSERT(0 != out);

	if(decoder->protected_->state != QLAC__STREAM_DECODER_OK)
		return false;

	/* point the bitreader at the caller's block */
	decoder->private_->in = in;
	decoder->private_->in_bytes = in_bytes;
	decoder->private_->in_pos = 0;
	if(!QLAC__bitreader_clear(decoder->private_->input)) {
		decoder->protected_->state = QLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/* read the subtype byte: 1 reserved bit (0) + 6 subtype bits + 1 wasted-bits flag */
	if(!QLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
		return false; /* block_read_callback_ sets END_OF_STREAM */

	if(x & 1) { /* wasted-bits flag */
		uint32_t u;
		if(!QLAC__bitreader_read_unary_unsigned(decoder->private_->input, &u))
			return false;
		wasted_bits = u + 1;
		if(wasted_bits >= bps) {
			decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
			return false;
		}
	}
	else
		wasted_bits = 0;
	x &= 0xfe;

	frame_bps = bps - wasted_bits;

	/* dispatch on the 6-bit subtype (same encoding the encoder writes) */
	if(x & 0x80) { /* reserved high bit must be 0 */
		decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
		return false;
	}
	else if(x == 0) { /* CONSTANT */
		if(!read_frame_constant_(decoder, frame_bps, out))
			return false;
	}
	else if(x == 2) { /* VERBATIM */
		if(!read_frame_verbatim_(decoder, frame_bps, out))
			return false;
	}
	else if(x < 16) {
		decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
		return false;
	}
	else if(x <= 24) { /* FIXED, order 0..4 */
		uint32_t order = (x>>1)&7;
		if(blocksize <= order) {
			decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
			return false;
		}
		if(!read_frame_fixed_(decoder, frame_bps, order, out))
			return false;
	}
	else if(x < 64) {
		decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
		return false;
	}
	else { /* LPC, order 1..32 */
		uint32_t order = ((x>>1)&31)+1;
		if(blocksize <= order) {
			decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
			return false;
		}
		if(!read_frame_lpc_(decoder, frame_bps, order, out))
			return false;
	}

	/* undo the wasted-bits shift */
	if(wasted_bits) {
		for(i = 0; i < blocksize; i++)
			out[i] = (QLAC__int32)((QLAC__uint32)out[i] << wasted_bits);
	}

	/* consume the trailing byte-alignment padding */
	if(!read_zero_padding_(decoder))
		return false;

	/* sanity-check that the decoded samples fit the configured bits-per-sample */
	for(i = 0; i < blocksize; i++) {
		if(out[i] < lower_limit || out[i] > upper_limit) {
			decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
			return false;
		}
	}

	decoder->private_->in = 0;
	return true;
}

/***********************************************************************
 *
 * Private class methods
 *
 ***********************************************************************/

void set_defaults_(QLAC__StreamDecoder *decoder)
{
	decoder->protected_->bits_per_sample = 16;
	decoder->protected_->blocksize = 4096;
	decoder->private_->in = 0;
	decoder->private_->in_bytes = 0;
	decoder->private_->in_pos = 0;
}

/* trivial read callback feeding the bitreader from the caller's fixed block
 * buffer; signals end-of-stream when the buffer is exhausted */
bool block_read_callback_(QLAC__byte buffer[], size_t *bytes, void *client_data)
{
	QLAC__StreamDecoder *decoder = (QLAC__StreamDecoder *)client_data;
	size_t want = *bytes;
	size_t avail = decoder->private_->in_bytes - decoder->private_->in_pos;

	if(avail == 0) {
		*bytes = 0;
		decoder->protected_->state = QLAC__STREAM_DECODER_END_OF_STREAM;
		return false;
	}
	if(want > avail)
		want = avail;
	memcpy(buffer, decoder->private_->in + decoder->private_->in_pos, want);
	decoder->private_->in_pos += want;
	*bytes = want;
	return true;
}

bool read_frame_constant_(QLAC__StreamDecoder *decoder, uint32_t bps, QLAC__int32 *out)
{
	QLAC__int32 x;
	uint32_t i;

	if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &x, bps))
		return false;

	for(i = 0; i < decoder->protected_->blocksize; i++)
		out[i] = x;

	return true;
}

bool read_frame_fixed_(QLAC__StreamDecoder *decoder, uint32_t bps, uint32_t order, QLAC__int32 *out)
{
	const uint32_t blocksize = decoder->protected_->blocksize;
	QLAC__int32 x;
	uint32_t u;

	/* read warm-up samples */
	for(u = 0; u < order; u++) {
		if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &x, bps))
			return false;
		out[u] = x;
	}

	/* entropy coding is always RICE2 in this fork (no method type field) */
	if(!read_residual_rice_(decoder, order, decoder->private_->residual))
		return false;

	/* restore the signal (bps <= 32 always in this fork) */
	if(bps + order <= 32)
		QLAC__fixed_restore_signal(decoder->private_->residual, blocksize-order, order, out+order);
	else
		QLAC__fixed_restore_signal_wide(decoder->private_->residual, blocksize-order, order, out+order);

	return true;
}

bool read_frame_lpc_(QLAC__StreamDecoder *decoder, uint32_t bps, uint32_t order, QLAC__int32 *out)
{
	const uint32_t blocksize = decoder->protected_->blocksize;
	QLAC__int32 i32;
	QLAC__uint32 u32;
	uint32_t u;
	uint32_t qlp_coeff_precision;
	int quantization_level;
	QLAC__int32 qlp_coeff[QLAC__MAX_LPC_ORDER];

	/* read warm-up samples */
	for(u = 0; u < order; u++) {
		if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, bps))
			return false;
		out[u] = i32;
	}

	/* read qlp coeff precision */
	if(!QLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, QLAC__FRAME_LPC_QLP_COEFF_PRECISION_LEN))
		return false;
	if(u32 == (1u << QLAC__FRAME_LPC_QLP_COEFF_PRECISION_LEN) - 1) {
		decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
		return false;
	}
	qlp_coeff_precision = u32 + 1;

	/* read qlp shift */
	if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, QLAC__FRAME_LPC_QLP_SHIFT_LEN))
		return false;
	if(i32 < 0) {
		decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
		return false;
	}
	quantization_level = i32;

	/* read quantized lp coefficients */
	for(u = 0; u < order; u++) {
		if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, qlp_coeff_precision))
			return false;
		qlp_coeff[u] = i32;
	}

	/* entropy coding is always RICE2 in this fork (no method type field) */
	if(!read_residual_rice_(decoder, order, decoder->private_->residual))
		return false;

	/* restore the signal (bps <= 32 always in this fork) */
	if(QLAC__lpc_max_residual_bps(bps, qlp_coeff, order, quantization_level) <= 32 &&
	   QLAC__lpc_max_prediction_before_shift_bps(bps, qlp_coeff, order) <= 32)
		QLAC__lpc_restore_signal(decoder->private_->residual, blocksize-order, qlp_coeff, order, quantization_level, out+order);
	else
		QLAC__lpc_restore_signal_wide(decoder->private_->residual, blocksize-order, qlp_coeff, order, quantization_level, out+order);

	return true;
}

bool read_frame_verbatim_(QLAC__StreamDecoder *decoder, uint32_t bps, QLAC__int32 *out)
{
	QLAC__int32 x;
	uint32_t i;

	for(i = 0; i < decoder->protected_->blocksize; i++) {
		if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &x, bps))
			return false;
		out[i] = x;
	}

	return true;
}

/* RICE2 residual.  Reads one Rice
 * parameter for the whole residual, with optional escape to raw values. */
bool read_residual_rice_(QLAC__StreamDecoder *decoder, uint32_t predictor_order, QLAC__int32 *residual)
{
	const uint32_t blocksize = decoder->protected_->blocksize;
	const uint32_t plen = QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN;
	const uint32_t pesc = QLAC__ENTROPY_CODING_METHOD_RICE2_ESCAPE_PARAMETER;
	const uint32_t nresidual = blocksize - predictor_order;
	QLAC__uint32 rice_parameter;
	uint32_t u;

	/* single partition (no partition order field in this fork) */
	if(!QLAC__bitreader_read_raw_uint32(decoder->private_->input, &rice_parameter, plen))
		return false;

	if(rice_parameter < pesc) {
		if(!decoder->private_->local_bitreader_read_rice_signed_block(decoder->private_->input, residual, nresidual, rice_parameter)) {
			if(decoder->protected_->state == QLAC__STREAM_DECODER_OK)
				decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
			return false;
		}
	}
	else {
		/* escape: raw values of 'raw_bits' bits each */
		if(!QLAC__bitreader_read_raw_uint32(decoder->private_->input, &rice_parameter, QLAC__ENTROPY_CODING_METHOD_RICE_RAW_LEN))
			return false;
		if(rice_parameter == 0) {
			for(u = 0; u < nresidual; u++)
				residual[u] = 0;
		}
		else {
			QLAC__int32 i;
			for(u = 0; u < nresidual; u++) {
				if(!QLAC__bitreader_read_raw_int32(decoder->private_->input, &i, rice_parameter))
					return false;
				residual[u] = i;
			}
		}
	}

	return true;
}

bool read_zero_padding_(QLAC__StreamDecoder *decoder)
{
	if(!QLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input)) {
		QLAC__uint32 zero = 0;
		if(!QLAC__bitreader_read_raw_uint32(decoder->private_->input, &zero, QLAC__bitreader_bits_left_for_byte_alignment(decoder->private_->input)))
			return false;
		if(zero != 0) {
			decoder->protected_->state = QLAC__STREAM_DECODER_CLIENT_ERROR;
			return false;
		}
	}
	return true;
}
