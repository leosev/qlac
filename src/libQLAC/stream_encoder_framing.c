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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "private/stream_encoder_framing.h"
#include "QLAC/assert.h"

static bool add_residual_rice_(QLAC__BitWriter *bw, const QLAC__int32 residual[], const uint32_t residual_samples, const uint32_t rice_parameter, const uint32_t raw_bits);

/* This fork is a pure block encoder whose parameters are all fixed and known
 * out of band (mono, fixed sample rate, fixed bits-per-sample, fixed
 * blocksize).  There is no frame header at all: a block begins directly with
 * the frame subtype written by QLAC__frame_add_{constant,fixed,lpc,verbatim}.
 * There is no stream sync, no blocksize/bps code, no frame number and no
 * header CRC.  See BLOCK_FORMAT.md for the on-the-wire layout. */

bool QLAC__frame_add_constant(const QLAC__Frame *frame, uint32_t frame_bps, QLAC__BitWriter *bw)
{
	const uint32_t wasted_bits = frame->wasted_bits;
	bool ok;

	ok =
		QLAC__bitwriter_write_raw_uint32(bw, QLAC__FRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK | (wasted_bits? 1:0), QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN) &&
		(wasted_bits? QLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1) : true) &&
		QLAC__bitwriter_write_raw_int64(bw, frame->constant_value, frame_bps)
	;

	return ok;
}

bool QLAC__frame_add_fixed(const QLAC__Frame *frame, uint32_t residual_samples, uint32_t frame_bps, QLAC__BitWriter *bw)
{
	const uint32_t wasted_bits = frame->wasted_bits;
	uint32_t i;

	if(!QLAC__bitwriter_write_raw_uint32(bw, QLAC__FRAME_TYPE_FIXED_BYTE_ALIGNED_MASK | (frame->order<<1) | (wasted_bits? 1:0), QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN))
		return false;
	if(wasted_bits)
		if(!QLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1))
			return false;

	for(i = 0; i < frame->order; i++)
		if(!QLAC__bitwriter_write_raw_int64(bw, frame->warmup[i], frame_bps))
			return false;

	if(!add_residual_rice_(
		bw,
		frame->residual,
		residual_samples,
		frame->rice_parameter,
		frame->rice_raw_bits
	))
		return false;

	return true;
}

bool QLAC__frame_add_lpc(const QLAC__Frame *frame, uint32_t residual_samples, uint32_t frame_bps, QLAC__BitWriter *bw)
{
	const uint32_t wasted_bits = frame->wasted_bits;
	uint32_t i;

	if(!QLAC__bitwriter_write_raw_uint32(bw, QLAC__FRAME_TYPE_LPC_BYTE_ALIGNED_MASK | ((frame->order-1)<<1) | (wasted_bits? 1:0), QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN))
		return false;
	if(wasted_bits)
		if(!QLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1))
			return false;

	for(i = 0; i < frame->order; i++)
		if(!QLAC__bitwriter_write_raw_int64(bw, frame->warmup[i], frame_bps))
			return false;

	if(!QLAC__bitwriter_write_raw_uint32(bw, frame->qlp_coeff_precision-1, QLAC__FRAME_LPC_QLP_COEFF_PRECISION_LEN))
		return false;
	if(!QLAC__bitwriter_write_raw_int32(bw, frame->quantization_level, QLAC__FRAME_LPC_QLP_SHIFT_LEN))
		return false;
	for(i = 0; i < frame->order; i++)
		if(!QLAC__bitwriter_write_raw_int32(bw, frame->qlp_coeff[i], frame->qlp_coeff_precision))
			return false;

	if(!add_residual_rice_(
		bw,
		frame->residual,
		residual_samples,
		frame->rice_parameter,
		frame->rice_raw_bits
	))
		return false;

	return true;
}

bool QLAC__frame_add_verbatim(const QLAC__Frame *frame, uint32_t samples, uint32_t frame_bps, QLAC__BitWriter *bw)
{
	const uint32_t wasted_bits = frame->wasted_bits;
	uint32_t i;

	if(!QLAC__bitwriter_write_raw_uint32(bw, QLAC__FRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK | (wasted_bits? 1:0), QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN))
		return false;
	if(wasted_bits)
		if(!QLAC__bitwriter_write_unary_unsigned(bw, wasted_bits-1))
			return false;

	/* this fork is <= 32 bps, so the verbatim signal is always 32-bit */
	QLAC__ASSERT(frame_bps < 33);

	for(i = 0; i < samples; i++)
		if(!QLAC__bitwriter_write_raw_int32(bw, frame->verbatim_data[i], frame_bps))
			return false;

	return true;
}

bool add_residual_rice_(QLAC__BitWriter *bw, const QLAC__int32 residual[], const uint32_t residual_samples, const uint32_t rice_parameter, const uint32_t raw_bits)
{
	const uint32_t plen = QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN;
	const uint32_t pesc = QLAC__ENTROPY_CODING_METHOD_RICE2_ESCAPE_PARAMETER;

	if(raw_bits == 0) {
		if(!QLAC__bitwriter_write_raw_uint32(bw, rice_parameter, plen))
			return false;
		if(!QLAC__bitwriter_write_rice_signed_block(bw, residual, residual_samples, rice_parameter))
			return false;
	}
	else {
		uint32_t i;
		QLAC__ASSERT(rice_parameter == 0);
		if(!QLAC__bitwriter_write_raw_uint32(bw, pesc, plen))
			return false;
		if(!QLAC__bitwriter_write_raw_uint32(bw, raw_bits, QLAC__ENTROPY_CODING_METHOD_RICE_RAW_LEN))
			return false;
		for(i = 0; i < residual_samples; i++) {
			if(!QLAC__bitwriter_write_raw_int32(bw, residual[i], raw_bits))
				return false;
		}
	}
	return true;
}
