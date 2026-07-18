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
#include <config.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h> /* for malloc() */
#include <string.h> /* for memcpy() */
#include <sys/types.h> /* for off_t */
#ifdef _WIN32
#include <windows.h> /* for GetFileType() */
#include <io.h> /* for _get_osfhandle() */
#endif
#include "QLAC/assert.h"
#include "share/alloc.h"
#include "share/compat.h"
#include "protected/stream_encoder.h"
#include "private/bitwriter.h"
#include "private/bitmath.h"
#include "private/cpu.h"
#include "private/fixed.h"
#include "QLAC/format.h"
#include "private/lpc.h"
#include "private/memory.h"
#include "private/macros.h"
#include "private/stream_encoder_framing.h"
#include "private/window.h"

/* Exact Rice codeword length calculation is off by default.  The simple
 * (and fast) estimation (of how many bits a residual value will be
 * encoded with) in this encoder is very good, almost always yielding
 * compression within 0.1% of exact calculation.
 */
#undef EXACT_RICE_BITS_CALCULATION
/* Rice parameter searching is off by default.  The simple (and fast)
 * parameter estimation in this encoder is very good, almost always
 * yielding compression within 0.1% of the optimal parameters.
 */
#undef ENABLE_RICE_PARAMETER_SEARCH

#define local_abs64(x) ((uint64_t)((x) < 0 ? -(x) : (x)))

#ifndef QLAC__INTEGER_ONLY_LIBRARY
typedef struct {
	uint32_t a, b, c;
	QLAC__ApodizationSpecification *current_apodization;
	double autoc_root[QLAC__MAX_LPC_ORDER + 1];
	double autoc[QLAC__MAX_LPC_ORDER + 1];
} apply_apodization_state_struct;
#endif

static const struct CompressionLevels {
	uint32_t max_lpc_order;
	uint32_t qlp_coeff_precision;
	bool do_qlp_coeff_prec_search;
	bool do_exhaustive_model_search;
	const char *apodization;
} compression_levels_[] = {
	{ 0, 0, false, false, "tukey(5e-1)" },
	{ 0, 0, false, false, "tukey(5e-1)" },
	{ 0, 0, false, false, "tukey(5e-1)" },
	{ 6, 0, false, false, "tukey(5e-1)" },
	{ 8, 0, false, false, "tukey(5e-1)" },
	{ 8, 0, false, false, "tukey(5e-1)" },
	{ 8, 0, false, false, "subdivide_tukey(2)" },
	{ 12, 0, false, false, "subdivide_tukey(2)" },
	{ 12, 0, false, false, "subdivide_tukey(3)" }
	/* here we use locale-independent 5e-1 instead of 0.5 or 0,5 */
};

/***********************************************************************
 *
 * Thread-private data
 *
 ***********************************************************************/

/***********************************************************************
 *
 * Private class method prototypes
 *
 ***********************************************************************/

static void set_defaults_(QLAC__StreamEncoder *encoder);
static void free_(QLAC__StreamEncoder *encoder);
static bool init_buffers_(QLAC__StreamEncoder *encoder);
static bool encode_frame_(QLAC__StreamEncoder *encoder);

static bool choose_best_predictor_(
	QLAC__StreamEncoder *encoder,
	uint32_t blocksize,
	uint32_t frame_bps,
	const void *integer_signal,
	QLAC__Frame frame[2],
	QLAC__int32 *residual[2],
	uint32_t *best_frame,
	uint32_t *best_bits);

#ifndef QLAC__INTEGER_ONLY_LIBRARY
static bool apply_apodization_(
	QLAC__StreamEncoder *encoder,
	apply_apodization_state_struct *apply_apodization_state,
	uint32_t blocksize,
	double *lpc_error,
	uint32_t *max_lpc_order_this_apodization,
	uint32_t frame_bps,
	const void *integer_signal,
	uint32_t *guess_lpc_order);
#endif

static bool add_frame_data_(
	QLAC__StreamEncoder *encoder,
	uint32_t blocksize,
	uint32_t frame_bps,
	const QLAC__Frame *frame,
	QLAC__BitWriter *bw);

static uint32_t evaluate_constant_(
	const QLAC__int64 signal,
	uint32_t frame_bps,
	QLAC__Frame *frame);

static uint32_t evaluate_fixed_(
	const void *signal,
	QLAC__int32 residual[],
	uint32_t blocksize,
	uint32_t frame_bps,
	uint32_t order,
	uint32_t rice_parameter_limit,
	uint32_t rice_parameter_search_dist,
	QLAC__Frame *frame);

#ifndef QLAC__INTEGER_ONLY_LIBRARY
static uint32_t evaluate_lpc_(
	QLAC__StreamEncoder *encoder,
	const void *signal,
	QLAC__int32 residual[],
	const QLAC__real lp_coeff[],
	uint32_t blocksize,
	uint32_t frame_bps,
	uint32_t order,
	uint32_t qlp_coeff_precision,
	uint32_t rice_parameter_limit,
	uint32_t rice_parameter_search_dist,
	QLAC__Frame *frame);
#endif

static uint32_t evaluate_verbatim_(
	const void *signal,
	uint32_t blocksize,
	uint32_t frame_bps,
	QLAC__Frame *frame);

/* Compute the single Rice parameter (and optional escape) for the whole
 * residual, store it in the frame's entropy coding fields, and return the
 * estimated number of bits the coded residual will take.
 */
static uint32_t set_rice_parameter_(
	const QLAC__int32 residual[],
	uint32_t residual_samples,
	uint32_t predictor_order,
	uint32_t rice_parameter_limit,
	uint32_t bps,
	uint32_t rice_parameter_search_dist,
	QLAC__Frame *frame);

static uint32_t get_wasted_bits_(QLAC__int32 signal[], uint32_t samples);

/***********************************************************************
 *
 * Private class data
 *
 ***********************************************************************/

typedef struct QLAC__StreamEncoderPrivate {
	/* per-frame working state (formerly QLAC__StreamEncoderTask).  Since this
	 * fork is single-threaded and block-oriented, parallelism is achieved by
	 * running independent encoders, so this state lives directly in the encoder. */
	QLAC__int32 *integer_signal; /* caller-supplied integer input signal for the current block (not owned; may be modified in place by wasted-bits shifting) */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	QLAC__real *windowed_signal; /* the integer_signal * current window[] */
#endif
	uint32_t frame_bps; /* the effective bits per sample of the input signal (stream bps - wasted bits) */
	QLAC__int32 *residual_workspace[2]; /* candidate and best workspace where the residual signals will be stored */
	QLAC__Frame frame_workspace[2]; /* candidate and best flattened frame (single mono subframe, single rice partition) */
	uint32_t best_frame; /* index (0 or 1) into the above workspaces */
	uint32_t best_frame_bits; /* size in bits of the best frame */
	QLAC__BitWriter *frame; /* the current frame being worked on */
	/* unaligned (original) pointers to internally-allocated data */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	QLAC__real *windowed_signal_unaligned;
#endif
	QLAC__int32 *residual_workspace_unaligned[2];
	/*
	 * These fields have been moved here from private function local
	 * declarations merely to save stack space during encoding.
	 */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	QLAC__real lp_coeff[QLAC__MAX_LPC_ORDER][QLAC__MAX_LPC_ORDER]; /* from choose_best_predictor_() */
#endif
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	QLAC__real *window[QLAC__MAX_APODIZATION_FUNCTIONS]; /* the pre-computed floating-point window for each apodization function */
	QLAC__real *window_unaligned[QLAC__MAX_APODIZATION_FUNCTIONS];
#endif
	QLAC__CPUInfo cpuinfo;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	uint32_t (*local_fixed_compute_best_predictor)(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
	uint32_t (*local_fixed_compute_best_predictor_wide)(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
	uint32_t (*local_fixed_compute_best_predictor_limit_residual)(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
#else
	uint32_t (*local_fixed_compute_best_predictor)(const QLAC__int32 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
	uint32_t (*local_fixed_compute_best_predictor_wide)(const QLAC__int32 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
	uint32_t (*local_fixed_compute_best_predictor_limit_residual)(const QLAC__int32 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
#endif
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	void (*local_lpc_compute_autocorrelation)(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients)(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients_64bit)(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
	void (*local_lpc_compute_residual_from_qlp_coefficients_16bit)(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
#endif
	bool disable_mmx;
	bool disable_sse2;
	bool disable_ssse3;
	bool disable_sse41;
	bool disable_sse42;
	bool disable_avx2;
	bool disable_fma;
	void *client_data;
	bool is_being_deleted; /* set while ..._finish() is called from ..._delete() */
} QLAC__StreamEncoderPrivate;

/***********************************************************************
 *
 * Public static class data
 *
 ***********************************************************************/

QLAC_API const char *const QLAC__StreamEncoderStateString[] = {
	"QLAC__STREAM_ENCODER_OK",
	"QLAC__STREAM_ENCODER_UNINITIALIZED",
	"QLAC__STREAM_ENCODER_CLIENT_ERROR",
	"QLAC__STREAM_ENCODER_IO_ERROR",
	"QLAC__STREAM_ENCODER_FRAMING_ERROR",
};

QLAC_API const char *const QLAC__StreamEncoderInitStatusString[] = {
	"QLAC__STREAM_ENCODER_INIT_STATUS_OK",
	"QLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR",
	"QLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER",
	"QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE",
	"QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE",
	"QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_MAX_LPC_ORDER",
	"QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_QLP_COEFF_PRECISION",
	"QLAC__STREAM_ENCODER_INIT_STATUS_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER",
	"QLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE",
	"QLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED"
};

/***********************************************************************
 *
 * Class constructor/destructor
 *
 */
QLAC_API QLAC__StreamEncoder *QLAC__stream_encoder_new(void)
{
	QLAC__StreamEncoder *encoder;
	QLAC__ASSERT(sizeof(int) >= 4); /* we want to die right away if this is not true */

	encoder = safe_calloc_(1, sizeof(QLAC__StreamEncoder));
	if(encoder == 0) {
		return 0;
	}

	encoder->protected_ = safe_calloc_(1, sizeof(QLAC__StreamEncoderProtected));
	if(encoder->protected_ == 0) {
		free(encoder);
		return 0;
	}

	encoder->private_ = safe_calloc_(1, sizeof(QLAC__StreamEncoderPrivate));
	if(encoder->private_ == 0) {
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->private_->frame = QLAC__bitwriter_new();
	if(encoder->private_->frame == 0) {
		free(encoder->private_);
		free(encoder->protected_);
		free(encoder);
		return 0;
	}

	encoder->protected_->state = QLAC__STREAM_ENCODER_UNINITIALIZED;

	set_defaults_(encoder);

	encoder->private_->is_being_deleted = false;

	return encoder;
}

QLAC_API void QLAC__stream_encoder_delete(QLAC__StreamEncoder *encoder)
{
	if(encoder == NULL)
		return;

	QLAC__ASSERT(0 != encoder->protected_);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->private_->frame);

	encoder->private_->is_being_deleted = true;

	(void)QLAC__stream_encoder_finish(encoder);

	QLAC__bitwriter_delete(encoder->private_->frame);
	free(encoder->private_);
	free(encoder->protected_);
	free(encoder);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

static QLAC__StreamEncoderInitStatus init_stream_internal_(
	QLAC__StreamEncoder *encoder,
	void *client_data)
{
	uint32_t i;

	QLAC__ASSERT(0 != encoder);

	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return QLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED;

	if(encoder->protected_->bits_per_sample < QLAC__MIN_BITS_PER_SAMPLE || encoder->protected_->bits_per_sample > QLAC__MAX_BITS_PER_SAMPLE)
		return QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE;

	if(encoder->protected_->blocksize == 0) {
		if(encoder->protected_->max_lpc_order == 0)
			encoder->protected_->blocksize = 1152;
		else
			encoder->protected_->blocksize = 4096;
	}

	if(encoder->protected_->blocksize < QLAC__MIN_BLOCK_SIZE || encoder->protected_->blocksize > QLAC__MAX_BLOCK_SIZE)
		return QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE;

	if(encoder->protected_->max_lpc_order > QLAC__MAX_LPC_ORDER)
		return QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_MAX_LPC_ORDER;

	if(encoder->protected_->blocksize < encoder->protected_->max_lpc_order)
		return QLAC__STREAM_ENCODER_INIT_STATUS_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER;

	if(encoder->protected_->qlp_coeff_precision == 0) {
		if(encoder->protected_->bits_per_sample < 16) {
			/* @@@ need some data about how to set this here w.r.t. blocksize and sample rate */
			/* @@@ until then we'll make a guess */
			encoder->protected_->qlp_coeff_precision = QLAC_max(QLAC__MIN_QLP_COEFF_PRECISION, 2 + encoder->protected_->bits_per_sample / 2);
		}
		else if(encoder->protected_->bits_per_sample == 16) {
			if(encoder->protected_->blocksize <= 192)
				encoder->protected_->qlp_coeff_precision = 7;
			else if(encoder->protected_->blocksize <= 384)
				encoder->protected_->qlp_coeff_precision = 8;
			else if(encoder->protected_->blocksize <= 576)
				encoder->protected_->qlp_coeff_precision = 9;
			else if(encoder->protected_->blocksize <= 1152)
				encoder->protected_->qlp_coeff_precision = 10;
			else if(encoder->protected_->blocksize <= 2304)
				encoder->protected_->qlp_coeff_precision = 11;
			else if(encoder->protected_->blocksize <= 4608)
				encoder->protected_->qlp_coeff_precision = 12;
			else
				encoder->protected_->qlp_coeff_precision = 13;
		}
		else {
			if(encoder->protected_->blocksize <= 384)
				encoder->protected_->qlp_coeff_precision = QLAC__MAX_QLP_COEFF_PRECISION - 2;
			else if(encoder->protected_->blocksize <= 1152)
				encoder->protected_->qlp_coeff_precision = QLAC__MAX_QLP_COEFF_PRECISION - 1;
			else
				encoder->protected_->qlp_coeff_precision = QLAC__MAX_QLP_COEFF_PRECISION;
		}
		QLAC__ASSERT(encoder->protected_->qlp_coeff_precision <= QLAC__MAX_QLP_COEFF_PRECISION);
	}
	else if(encoder->protected_->qlp_coeff_precision < QLAC__MIN_QLP_COEFF_PRECISION || encoder->protected_->qlp_coeff_precision > QLAC__MAX_QLP_COEFF_PRECISION)
		return QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_QLP_COEFF_PRECISION;

	/*
	 * get the CPU info and set the function pointers
	 */
	QLAC__cpu_info(&encoder->private_->cpuinfo);
	/* remove cpu info as requested by
	 * QLAC__stream_encoder_disable_instruction_set */
	if(encoder->private_->disable_mmx)
		encoder->private_->cpuinfo.x86.mmx = false;
	if(encoder->private_->disable_sse2)
		encoder->private_->cpuinfo.x86.sse2 = false;
	if(encoder->private_->disable_ssse3)
		encoder->private_->cpuinfo.x86.ssse3 = false;
	if(encoder->private_->disable_sse41)
		encoder->private_->cpuinfo.x86.sse41 = false;
	if(encoder->private_->disable_sse42)
		encoder->private_->cpuinfo.x86.sse42 = false;
	if(encoder->private_->disable_avx2)
		encoder->private_->cpuinfo.x86.avx2 = false;
	if(encoder->private_->disable_fma)
		encoder->private_->cpuinfo.x86.fma = false;
	/* first default to the non-asm routines */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation;
#endif
	encoder->private_->local_fixed_compute_best_predictor = QLAC__fixed_compute_best_predictor;
	encoder->private_->local_fixed_compute_best_predictor_wide = QLAC__fixed_compute_best_predictor_wide;
	encoder->private_->local_fixed_compute_best_predictor_limit_residual = QLAC__fixed_compute_best_predictor_limit_residual;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = QLAC__lpc_compute_residual_from_qlp_coefficients_wide;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = QLAC__lpc_compute_residual_from_qlp_coefficients;
#endif
	/* now override with asm where appropriate */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
#ifndef QLAC__NO_ASM
#if defined QLAC__CPU_ARM64 && QLAC__HAS_NEONINTRIN
#if QLAC__HAS_A64NEONINTRIN
	if(encoder->protected_->max_lpc_order < 8)
		encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_neon_lag_8;
	else if(encoder->protected_->max_lpc_order < 10)
		encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_neon_lag_10;
	else if(encoder->protected_->max_lpc_order < 14)
		encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_neon_lag_14;
	else
		encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation;
#endif
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_neon;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_neon;
	encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_neon;
#endif /* defined QLAC__CPU_ARM64 && QLAC__HAS_NEONINTRIN */

	if(encoder->private_->cpuinfo.use_asm) {
#ifdef QLAC__CPU_IA32
		QLAC__ASSERT(encoder->private_->cpuinfo.type == QLAC__CPUINFO_TYPE_IA32);
#if QLAC__HAS_X86INTRIN
#ifdef QLAC__SSE2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse2) {
			if(encoder->protected_->max_lpc_order < 8)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_8;
			else if(encoder->protected_->max_lpc_order < 10)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_10;
			else if(encoder->protected_->max_lpc_order < 14)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_14;

			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse2;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = QLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_sse2;
		}
#endif
#ifdef QLAC__SSE4_1_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse41) {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse41;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_sse41;
		}
#endif
#ifdef QLAC__AVX2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.avx2) {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = QLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_avx2;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_avx2;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_avx2;
		}
#endif

#ifdef QLAC__SSE2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse2) {
			encoder->private_->local_fixed_compute_best_predictor = QLAC__fixed_compute_best_predictor_intrin_sse2;
		}
#endif
#ifdef QLAC__SSSE3_SUPPORTED
		if(encoder->private_->cpuinfo.x86.ssse3) {
			encoder->private_->local_fixed_compute_best_predictor = QLAC__fixed_compute_best_predictor_intrin_ssse3;
		}
#endif
#ifdef QLAC__SSE4_2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse42) {
			encoder->private_->local_fixed_compute_best_predictor_limit_residual = QLAC__fixed_compute_best_predictor_limit_residual_intrin_sse42;
		}
#endif
#ifdef QLAC__AVX2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.avx2) {
			encoder->private_->local_fixed_compute_best_predictor_wide = QLAC__fixed_compute_best_predictor_wide_intrin_avx2;
			encoder->private_->local_fixed_compute_best_predictor_limit_residual = QLAC__fixed_compute_best_predictor_limit_residual_intrin_avx2;
		}
#endif
#endif /* QLAC__HAS_X86INTRIN */
#elif defined QLAC__CPU_X86_64
		QLAC__ASSERT(encoder->private_->cpuinfo.type == QLAC__CPUINFO_TYPE_X86_64);
#if QLAC__HAS_X86INTRIN
#ifdef QLAC__SSE2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse2) { /* For fuzzing */
			if(encoder->protected_->max_lpc_order < 8)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_8;
			else if(encoder->protected_->max_lpc_order < 10)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_10;
			else if(encoder->protected_->max_lpc_order < 14)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_14;

			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = QLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_sse2;
		}
#endif
#ifdef QLAC__SSE4_1_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse41) {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse41;
		}
#endif
#ifdef QLAC__AVX2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.avx2) {
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit = QLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_avx2;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients = QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_avx2;
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit = QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_avx2;
		}
#endif
#ifdef QLAC__FMA_SUPPORTED
		if(encoder->private_->cpuinfo.x86.fma) {
			if(encoder->protected_->max_lpc_order < 8)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_fma_lag_8;
			else if(encoder->protected_->max_lpc_order < 12)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_fma_lag_12;
			else if(encoder->protected_->max_lpc_order < 16)
				encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation_intrin_fma_lag_16;
		}
#endif

#ifdef QLAC__SSE2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse2) { /* For fuzzing */
			encoder->private_->local_fixed_compute_best_predictor = QLAC__fixed_compute_best_predictor_intrin_sse2;
		}
#endif
#ifdef QLAC__SSSE3_SUPPORTED
		if(encoder->private_->cpuinfo.x86.ssse3) {
			encoder->private_->local_fixed_compute_best_predictor = QLAC__fixed_compute_best_predictor_intrin_ssse3;
		}
#endif
#ifdef QLAC__SSE4_2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.sse42) {
			encoder->private_->local_fixed_compute_best_predictor_limit_residual = QLAC__fixed_compute_best_predictor_limit_residual_intrin_sse42;
		}
#endif
#ifdef QLAC__AVX2_SUPPORTED
		if(encoder->private_->cpuinfo.x86.avx2) {
			encoder->private_->local_fixed_compute_best_predictor_wide = QLAC__fixed_compute_best_predictor_wide_intrin_avx2;
			encoder->private_->local_fixed_compute_best_predictor_limit_residual = QLAC__fixed_compute_best_predictor_limit_residual_intrin_avx2;
		}
#endif
#endif /* QLAC__HAS_X86INTRIN */
#endif /* QLAC__CPU_... */
	}
#endif /* !QLAC__NO_ASM */

#endif /* !QLAC__INTEGER_ONLY_LIBRARY */
	/* set state to OK; from here on, errors are fatal and we'll override the state then */
	encoder->protected_->state = QLAC__STREAM_ENCODER_OK;
	encoder->private_->client_data = client_data;

#ifndef QLAC__INTEGER_ONLY_LIBRARY
	for(i = 0; i < encoder->protected_->num_apodizations; i++)
		encoder->private_->window_unaligned[i] = encoder->private_->window[i] = 0;
#endif
	/* the audio sample buffer (integer_signal) is owned by the caller and
	 * supplied per block to QLAC__stream_encoder_encode_block() */
	encoder->private_->integer_signal = 0;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	encoder->private_->windowed_signal_unaligned = encoder->private_->windowed_signal = 0;
#endif
	encoder->private_->residual_workspace_unaligned[0] = encoder->private_->residual_workspace[0] = 0;
	encoder->private_->residual_workspace_unaligned[1] = encoder->private_->residual_workspace[1] = 0;
	encoder->private_->best_frame = 0;

	if(!init_buffers_(encoder)) {
		/* the above function sets the state for us in case of an error */
		return QLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	if(!QLAC__bitwriter_init(encoder->private_->frame)) {
		encoder->protected_->state = QLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return QLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR;
	}

	/* This fork is a pure block encoder: there is no stream, no stream header
	 * and no metadata.  Each QLAC__stream_encoder_encode_block() call produces
	 * the bitstream for one independent block. */

	return QLAC__STREAM_ENCODER_INIT_STATUS_OK;
}

QLAC_API QLAC__StreamEncoderInitStatus QLAC__stream_encoder_init_stream(
	QLAC__StreamEncoder *encoder,
	void *client_data)
{
	return init_stream_internal_(
		encoder,
		client_data);
}

QLAC_API bool QLAC__stream_encoder_finish(QLAC__StreamEncoder *encoder)
{
	bool error = false;

	if(encoder == NULL)
		return false;

	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);

	if(encoder->protected_->state == QLAC__STREAM_ENCODER_UNINITIALIZED) {
		return true;
	}

	/* This fork is a pure block encoder with no cross-block state: there is no
	 * partial last block to flush here.  finish() just releases the internal
	 * buffers and resets to the uninitialized state. */

	free_(encoder);
	set_defaults_(encoder);

	if(!error)
		encoder->protected_->state = QLAC__STREAM_ENCODER_UNINITIALIZED;

	return !error;
}

QLAC_API bool QLAC__stream_encoder_set_streamable_subset(QLAC__StreamEncoder *encoder, bool value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->streamable_subset = value;
	return true;
}

QLAC_API bool QLAC__stream_encoder_set_bits_per_sample(QLAC__StreamEncoder *encoder, uint32_t value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->bits_per_sample = value;
	return true;
}

QLAC_API bool QLAC__stream_encoder_set_compression_level(QLAC__StreamEncoder *encoder, uint32_t value)
{
	bool ok = true;
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	if(value >= sizeof(compression_levels_) / sizeof(compression_levels_[0]))
		value = sizeof(compression_levels_) / sizeof(compression_levels_[0]) - 1;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
#if 1
	ok &= QLAC__stream_encoder_set_apodization(encoder, compression_levels_[value].apodization);
#else
	/* equivalent to -A tukey(0.5) */
	encoder->protected_->num_apodizations = 1;
	encoder->protected_->apodizations[0].type = QLAC__APODIZATION_TUKEY;
	encoder->protected_->apodizations[0].parameters.tukey.p = 0.5;
#endif
#endif
	ok &= QLAC__stream_encoder_set_max_lpc_order(encoder, compression_levels_[value].max_lpc_order);
	ok &= QLAC__stream_encoder_set_qlp_coeff_precision(encoder, compression_levels_[value].qlp_coeff_precision);
	ok &= QLAC__stream_encoder_set_do_qlp_coeff_prec_search(encoder, compression_levels_[value].do_qlp_coeff_prec_search);
	ok &= QLAC__stream_encoder_set_do_exhaustive_model_search(encoder, compression_levels_[value].do_exhaustive_model_search);
	return ok;
}

QLAC_API bool QLAC__stream_encoder_set_blocksize(QLAC__StreamEncoder *encoder, uint32_t value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->blocksize = value;
	return true;
}

/*@@@@add to tests*/
QLAC_API bool QLAC__stream_encoder_set_apodization(QLAC__StreamEncoder *encoder, const char *specification)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	QLAC__ASSERT(0 != specification);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
#ifdef QLAC__INTEGER_ONLY_LIBRARY
	(void)specification; /* silently ignore since we haven't integerized; will always use a rectangular window */
#else
	encoder->protected_->num_apodizations = 0;
	while(1) {
		const char *s = strchr(specification, ';');
		const size_t n = s ? (size_t)(s - specification) : strlen(specification);
		if(n == 8 && 0 == strncmp("bartlett", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_BARTLETT;
		else if(n == 13 && 0 == strncmp("bartlett_hann", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_BARTLETT_HANN;
		else if(n == 8 && 0 == strncmp("blackman", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_BLACKMAN;
		else if(n == 26 && 0 == strncmp("blackman_harris_4term_92db", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_BLACKMAN_HARRIS_4TERM_92DB_SIDELOBE;
		else if(n == 6 && 0 == strncmp("connes", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_CONNES;
		else if(n == 7 && 0 == strncmp("flattop", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_FLATTOP;
		else if(n > 7 && 0 == strncmp("gauss(", specification, 6)) {
			QLAC__real stddev = (QLAC__real)strtod(specification + 6, 0);
			if(stddev > 0.0 && stddev <= 0.5) {
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.gauss.stddev = stddev;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_GAUSS;
			}
		}
		else if(n == 7 && 0 == strncmp("hamming", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_HAMMING;
		else if(n == 4 && 0 == strncmp("hann", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_HANN;
		else if(n == 13 && 0 == strncmp("kaiser_bessel", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_KAISER_BESSEL;
		else if(n == 7 && 0 == strncmp("nuttall", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_NUTTALL;
		else if(n == 9 && 0 == strncmp("rectangle", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_RECTANGLE;
		else if(n == 8 && 0 == strncmp("triangle", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_TRIANGLE;
		else if(n > 7 && 0 == strncmp("tukey(", specification, 6)) {
			QLAC__real p = (QLAC__real)strtod(specification + 6, 0);
			if(p >= 0.0 && p <= 1.0) {
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.tukey.p = p;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_TUKEY;
			}
		}
		else if(n > 15 && 0 == strncmp("partial_tukey(", specification, 14)) {
			QLAC__int32 tukey_parts = (QLAC__int32)strtod(specification + 14, 0);
			const char *si_1 = strchr(specification, '/');
			QLAC__real overlap = si_1 ? QLAC_min((QLAC__real)strtod(si_1 + 1, 0), 0.99f) : 0.1f;
			QLAC__real overlap_units = 1.0f / (1.0f - overlap) - 1.0f;
			const char *si_2 = strchr((si_1 ? (si_1 + 1) : specification), '/');
			QLAC__real tukey_p = si_2 ? (QLAC__real)strtod(si_2 + 1, 0) : 0.2f;

			if(tukey_parts <= 1) {
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.tukey.p = tukey_p;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_TUKEY;
			}
			else if(encoder->protected_->num_apodizations + tukey_parts < 32) {
				QLAC__int32 m;
				for(m = 0; m < tukey_parts; m++) {
					encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.multiple_tukey.p = tukey_p;
					encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.multiple_tukey.start = m / (tukey_parts + overlap_units);
					encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.multiple_tukey.end = (m + 1 + overlap_units) / (tukey_parts + overlap_units);
					encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_PARTIAL_TUKEY;
				}
			}
		}
		else if(n > 16 && 0 == strncmp("punchout_tukey(", specification, 15)) {
			QLAC__int32 tukey_parts = (QLAC__int32)strtod(specification + 15, 0);
			const char *si_1 = strchr(specification, '/');
			QLAC__real overlap = si_1 ? QLAC_min((QLAC__real)strtod(si_1 + 1, 0), 0.99f) : 0.2f;
			QLAC__real overlap_units = 1.0f / (1.0f - overlap) - 1.0f;
			const char *si_2 = strchr((si_1 ? (si_1 + 1) : specification), '/');
			QLAC__real tukey_p = si_2 ? (QLAC__real)strtod(si_2 + 1, 0) : 0.2f;

			if(tukey_parts <= 1) {
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.tukey.p = tukey_p;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_TUKEY;
			}
			else if(encoder->protected_->num_apodizations + tukey_parts < 32) {
				QLAC__int32 m;
				for(m = 0; m < tukey_parts; m++) {
					encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.multiple_tukey.p = tukey_p;
					encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.multiple_tukey.start = m / (tukey_parts + overlap_units);
					encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.multiple_tukey.end = (m + 1 + overlap_units) / (tukey_parts + overlap_units);
					encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_PUNCHOUT_TUKEY;
				}
			}
		}
		else if(n > 17 && 0 == strncmp("subdivide_tukey(", specification, 16)) {
			QLAC__int32 parts = (QLAC__int32)strtod(specification + 16, 0);
			if(parts > 1) {
				const char *si_1 = strchr(specification, '/');
				QLAC__real p = si_1 ? (QLAC__real)strtod(si_1 + 1, 0) : 5e-1;
				if(p > 1)
					p = 1;
				else if(p < 0)
					p = 0;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.subdivide_tukey.parts = parts;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations].parameters.subdivide_tukey.p = p / parts;
				encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_SUBDIVIDE_TUKEY;
			}
		}
		else if(n == 5 && 0 == strncmp("welch", specification, n))
			encoder->protected_->apodizations[encoder->protected_->num_apodizations++].type = QLAC__APODIZATION_WELCH;
		if(encoder->protected_->num_apodizations == 32)
			break;
		if(s)
			specification = s + 1;
		else
			break;
	}
	if(encoder->protected_->num_apodizations == 0) {
		encoder->protected_->num_apodizations = 1;
		encoder->protected_->apodizations[0].type = QLAC__APODIZATION_TUKEY;
		encoder->protected_->apodizations[0].parameters.tukey.p = 0.5;
	}
#endif
	return true;
}

QLAC_API bool QLAC__stream_encoder_set_max_lpc_order(QLAC__StreamEncoder *encoder, uint32_t value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->max_lpc_order = value;
	return true;
}

QLAC_API bool QLAC__stream_encoder_set_qlp_coeff_precision(QLAC__StreamEncoder *encoder, uint32_t value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->qlp_coeff_precision = value;
	return true;
}

QLAC_API QLAC__Frame *QLAC__stream_encoder_get_frame_descriptor(QLAC__StreamEncoder *encoder)
{
	return &encoder->private_->frame_workspace[encoder->private_->best_frame];
}
QLAC_API bool QLAC__stream_encoder_set_do_qlp_coeff_prec_search(QLAC__StreamEncoder *encoder, bool value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_qlp_coeff_prec_search = value;
	return true;
}

QLAC_API bool QLAC__stream_encoder_set_do_exhaustive_model_search(QLAC__StreamEncoder *encoder, bool value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->protected_->do_exhaustive_model_search = value;
	return true;
}

QLAC_API bool QLAC__stream_encoder_set_total_samples_estimate(QLAC__StreamEncoder *encoder, QLAC__uint64 value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	value = QLAC_min(value, (QLAC__U64L(1) << QLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN) - 1);
	encoder->protected_->total_samples_estimate = value;
	return true;
}

/*
 * These four functions are not static, but not publicly exposed in
 * include/QLAC/ either.  They are used by the test suite and in fuzzing
 */
QLAC_API bool QLAC__stream_encoder_disable_instruction_set(QLAC__StreamEncoder *encoder, bool value)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	if(encoder->protected_->state != QLAC__STREAM_ENCODER_UNINITIALIZED)
		return false;
	encoder->private_->disable_mmx = value & 1;
	encoder->private_->disable_sse2 = value & 2;
	encoder->private_->disable_ssse3 = value & 4;
	encoder->private_->disable_sse41 = value & 8;
	encoder->private_->disable_avx2 = value & 16;
	encoder->private_->disable_fma = value & 32;
	encoder->private_->disable_sse42 = value & 64;
	return true;
}

QLAC_API QLAC__StreamEncoderState QLAC__stream_encoder_get_state(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->state;
}

QLAC_API const char *QLAC__stream_encoder_get_resolved_state_string(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return QLAC__StreamEncoderStateString[encoder->protected_->state];
}

QLAC_API bool QLAC__stream_encoder_get_streamable_subset(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->streamable_subset;
}

QLAC_API uint32_t QLAC__stream_encoder_get_bits_per_sample(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->bits_per_sample;
}

QLAC_API uint32_t QLAC__stream_encoder_get_blocksize(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->blocksize;
}

QLAC_API uint32_t QLAC__stream_encoder_get_max_lpc_order(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->max_lpc_order;
}

QLAC_API uint32_t QLAC__stream_encoder_get_qlp_coeff_precision(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->qlp_coeff_precision;
}

QLAC_API bool QLAC__stream_encoder_get_do_qlp_coeff_prec_search(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_qlp_coeff_prec_search;
}
QLAC_API bool QLAC__stream_encoder_get_do_exhaustive_model_search(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->do_exhaustive_model_search;
}

QLAC_API uint32_t QLAC__stream_encoder_get_rice_parameter_search_dist(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->rice_parameter_search_dist;
}

QLAC_API QLAC__uint64 QLAC__stream_encoder_get_total_samples_estimate(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	return encoder->protected_->total_samples_estimate;
}

QLAC_API uint32_t QLAC__stream_encoder_get_input_buffer_size(const QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->protected_);
	/* The caller-owned input buffer must hold QLAC__STREAM_ENCODER_INPUT_LEADING_PAD
	 * leading zero samples (for SIMD alignment) followed by blocksize real samples. */
	return encoder->protected_->blocksize + QLAC__STREAM_ENCODER_INPUT_LEADING_PAD;
}

QLAC_API uint32_t QLAC__stream_encoder_get_input_alignment(const QLAC__StreamEncoder *encoder)
{
	(void)encoder;
	return QLAC__MEMORY_ALIGNMENT;
}

QLAC_API uint32_t QLAC__stream_encoder_get_input_leading_pad(const QLAC__StreamEncoder *encoder)
{
	(void)encoder;
	return QLAC__STREAM_ENCODER_INPUT_LEADING_PAD;
}

QLAC_API bool QLAC__stream_encoder_encode_block(QLAC__StreamEncoder *encoder, QLAC__int32 *samples, QLAC__byte *out, size_t out_capacity, size_t *out_bytes)
{
	const QLAC__byte *buffer;
	size_t bytes;

	QLAC__ASSERT(0 != encoder);
	QLAC__ASSERT(0 != encoder->private_);
	QLAC__ASSERT(0 != encoder->protected_);
	QLAC__ASSERT(0 != samples);
	QLAC__ASSERT(0 != out);
	QLAC__ASSERT(0 != out_bytes);

	if(encoder->protected_->state != QLAC__STREAM_ENCODER_OK)
		return false;

	/* point the (stateless) encode path at the caller's buffer.  NOTE: the
	 * wasted-bits step may shift these samples in place. */
	encoder->private_->integer_signal = samples;

	/* encode the single mono frame into the frame bitbuffer */
	if(!encode_frame_(encoder))
		return false; /* state set by encode_frame_ */

	/* byte-align */
	if(!QLAC__bitwriter_zero_pad_to_byte_boundary(encoder->private_->frame)) {
		encoder->protected_->state = QLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/* hand the encoded bytes back to the caller's output buffer */
	if(!QLAC__bitwriter_get_buffer(encoder->private_->frame, &buffer, &bytes)) {
		encoder->protected_->state = QLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(bytes > out_capacity) {
		QLAC__bitwriter_clear(encoder->private_->frame);
		encoder->protected_->state = QLAC__STREAM_ENCODER_CLIENT_ERROR;
		return false;
	}
	memcpy(out, buffer, bytes);
	*out_bytes = bytes;

	QLAC__bitwriter_clear(encoder->private_->frame);

	encoder->private_->integer_signal = 0;

	return true;
}

/***********************************************************************
 *
 * Private class methods
 *
 ***********************************************************************/

void set_defaults_(QLAC__StreamEncoder *encoder)
{
	QLAC__ASSERT(0 != encoder);

	encoder->protected_->streamable_subset = true;
	encoder->protected_->bits_per_sample = 16;
	encoder->protected_->blocksize = 0;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	encoder->protected_->num_apodizations = 1;
	encoder->protected_->apodizations[0].type = QLAC__APODIZATION_TUKEY;
	encoder->protected_->apodizations[0].parameters.tukey.p = 0.5;
#endif
	encoder->protected_->max_lpc_order = 0;
	encoder->protected_->qlp_coeff_precision = 0;
	encoder->protected_->do_qlp_coeff_prec_search = false;
	encoder->protected_->do_exhaustive_model_search = false;
	encoder->protected_->rice_parameter_search_dist = 0;
	encoder->protected_->total_samples_estimate = 0;
	encoder->private_->disable_mmx = false;
	encoder->private_->disable_sse2 = false;
	encoder->private_->disable_ssse3 = false;
	encoder->private_->disable_sse41 = false;
	encoder->private_->disable_sse42 = false;
	encoder->private_->disable_avx2 = false;
	encoder->private_->client_data = 0;

	QLAC__stream_encoder_set_compression_level(encoder, 5);
}

void free_(QLAC__StreamEncoder *encoder)
{
	uint32_t i;

	QLAC__ASSERT(0 != encoder);
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	for(i = 0; i < encoder->protected_->num_apodizations; i++) {
		if(0 != encoder->private_->window_unaligned[i]) {
			free(encoder->private_->window_unaligned[i]);
			encoder->private_->window_unaligned[i] = 0;
		}
	}
#endif
	/* integer_signal is caller-owned; do not free it here */
	encoder->private_->integer_signal = 0;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	if(0 != encoder->private_->windowed_signal_unaligned) {
		free(encoder->private_->windowed_signal_unaligned);
		encoder->private_->windowed_signal_unaligned = 0;
	}
#endif
	for(i = 0; i < 2; i++) {
		if(0 != encoder->private_->residual_workspace_unaligned[i]) {
			free(encoder->private_->residual_workspace_unaligned[i]);
			encoder->private_->residual_workspace_unaligned[i] = 0;
		}
	}
}

/* Allocate the encoder's internal work buffers once, at the fixed blocksize.
 *
 * This fork does a single fixed allocation: there is no grow-on-demand and no
 * per-block resizing.  The audio sample buffer (integer_signal) is NOT
 * allocated here -- the caller owns it and supplies it to
 * QLAC__stream_encoder_encode_block() (see include/QLAC/memory.h for the
 * alignment/padding contract).  Only the internal windows, windowed_signal and
 * residual workspaces are allocated, and the apodization windows are computed
 * once here since the blocksize is fixed.
 */
bool init_buffers_(QLAC__StreamEncoder *encoder)
{
	const uint32_t blocksize = encoder->protected_->blocksize;
	bool ok = true;
	uint32_t i;

	QLAC__ASSERT(blocksize > 0);
	QLAC__ASSERT(encoder->protected_->state == QLAC__STREAM_ENCODER_OK);

#ifndef QLAC__INTEGER_ONLY_LIBRARY
	if(ok && encoder->protected_->max_lpc_order > 0) {
		for(i = 0; ok && i < encoder->protected_->num_apodizations; i++)
			ok = ok && QLAC__memory_alloc_aligned_real_array(blocksize, &encoder->private_->window_unaligned[i], &encoder->private_->window[i]);
	}
	if(ok && encoder->protected_->max_lpc_order > 0) {
		ok = ok && QLAC__memory_alloc_aligned_real_array(blocksize, &encoder->private_->windowed_signal_unaligned, &encoder->private_->windowed_signal);
	}
#endif
	for(i = 0; ok && i < 2; i++) {
		ok = ok && QLAC__memory_alloc_aligned_int32_array(blocksize, &encoder->private_->residual_workspace_unaligned[i], &encoder->private_->residual_workspace[i]);
	}

	if(!ok) {
		encoder->protected_->state = QLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	/* compute the apodization windows once (blocksize is fixed) */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	if(encoder->protected_->max_lpc_order > 0 && blocksize > 1) {
		for(i = 0; i < encoder->protected_->num_apodizations; i++) {
			switch(encoder->protected_->apodizations[i].type) {
				case QLAC__APODIZATION_BARTLETT:
					QLAC__window_bartlett(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_BARTLETT_HANN:
					QLAC__window_bartlett_hann(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_BLACKMAN:
					QLAC__window_blackman(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_BLACKMAN_HARRIS_4TERM_92DB_SIDELOBE:
					QLAC__window_blackman_harris_4term_92db_sidelobe(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_CONNES:
					QLAC__window_connes(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_FLATTOP:
					QLAC__window_flattop(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_GAUSS:
					QLAC__window_gauss(encoder->private_->window[i], blocksize, encoder->protected_->apodizations[i].parameters.gauss.stddev);
					break;
				case QLAC__APODIZATION_HAMMING:
					QLAC__window_hamming(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_HANN:
					QLAC__window_hann(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_KAISER_BESSEL:
					QLAC__window_kaiser_bessel(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_NUTTALL:
					QLAC__window_nuttall(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_RECTANGLE:
					QLAC__window_rectangle(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_TRIANGLE:
					QLAC__window_triangle(encoder->private_->window[i], blocksize);
					break;
				case QLAC__APODIZATION_TUKEY:
					QLAC__window_tukey(encoder->private_->window[i], blocksize, encoder->protected_->apodizations[i].parameters.tukey.p);
					break;
				case QLAC__APODIZATION_PARTIAL_TUKEY:
					QLAC__window_partial_tukey(encoder->private_->window[i], blocksize, encoder->protected_->apodizations[i].parameters.multiple_tukey.p, encoder->protected_->apodizations[i].parameters.multiple_tukey.start, encoder->protected_->apodizations[i].parameters.multiple_tukey.end);
					break;
				case QLAC__APODIZATION_PUNCHOUT_TUKEY:
					QLAC__window_punchout_tukey(encoder->private_->window[i], blocksize, encoder->protected_->apodizations[i].parameters.multiple_tukey.p, encoder->protected_->apodizations[i].parameters.multiple_tukey.start, encoder->protected_->apodizations[i].parameters.multiple_tukey.end);
					break;
				case QLAC__APODIZATION_SUBDIVIDE_TUKEY:
					QLAC__window_tukey(encoder->private_->window[i], blocksize, encoder->protected_->apodizations[i].parameters.tukey.p);
					break;
				case QLAC__APODIZATION_WELCH:
					QLAC__window_welch(encoder->private_->window[i], blocksize);
					break;
				default:
					QLAC__ASSERT(0);
					/* double protection */
					QLAC__window_hann(encoder->private_->window[i], blocksize);
					break;
			}
		}
	}
	if(blocksize <= QLAC__MAX_LPC_ORDER) {
		/* intrinsics autocorrelation routines do not all handle cases in which lag might be
		 * larger than data_len. Lag is one larger than the LPC order */
		encoder->private_->local_lpc_compute_autocorrelation = QLAC__lpc_compute_autocorrelation;
	}
#endif

	return true;
}

bool encode_frame_(QLAC__StreamEncoder *encoder)
{
	const uint32_t blocksize = encoder->protected_->blocksize;

	/*
	 * Check for wasted bits; set effective bps for the frame
	 */
	{
		uint32_t w = get_wasted_bits_(encoder->private_->integer_signal, encoder->protected_->blocksize);
		if(w > encoder->protected_->bits_per_sample)
			w = encoder->protected_->bits_per_sample;
		encoder->private_->frame_workspace[0].wasted_bits = encoder->private_->frame_workspace[1].wasted_bits = w;
		encoder->private_->frame_bps = encoder->protected_->bits_per_sample - w;
	}

	if(!choose_best_predictor_(
		   encoder,
		   blocksize,
		   encoder->private_->frame_bps,
		   encoder->private_->integer_signal,
		   encoder->private_->frame_workspace,
		   encoder->private_->residual_workspace,
		   &encoder->private_->best_frame,
		   &encoder->private_->best_frame_bits))
		return false;

	/*
	 * Compose the frame bitbuffer.  This fork writes no frame header: the block
	 * begins directly with the frame subtype (see BLOCK_FORMAT.md).
	 */
	QLAC__ASSERT(QLAC__bitwriter_is_byte_aligned(encoder->private_->frame));
	if(!add_frame_data_(encoder, blocksize, encoder->private_->frame_bps, &encoder->private_->frame_workspace[encoder->private_->best_frame], encoder->private_->frame))
		return false;

	return true;
}

bool choose_best_predictor_(
	QLAC__StreamEncoder *encoder,
	uint32_t blocksize,
	uint32_t frame_bps,
	const void *integer_signal,
	QLAC__Frame frame[2],
	QLAC__int32 *residual[2],
	uint32_t *best_frame,
	uint32_t *best_bits)
{
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	float fixed_residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1];
#else
	QLAC__fixedpoint fixed_residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1];
#endif
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	double lpc_residual_bits_per_sample;
	apply_apodization_state_struct apply_apodization_state;
	double lpc_error[QLAC__MAX_LPC_ORDER];
	uint32_t min_lpc_order, max_lpc_order, lpc_order, guess_lpc_order;
	uint32_t min_qlp_coeff_precision, max_qlp_coeff_precision, qlp_coeff_precision;
#endif
	uint32_t min_fixed_order, max_fixed_order, guess_fixed_order, fixed_order;
	uint32_t _candidate_bits, _best_bits;
	uint32_t _best_frame;
	/* entropy coding is always RICE2 (5-bit parameter) in this fork */
	const uint32_t rice_parameter_limit = QLAC__ENTROPY_CODING_METHOD_RICE2_ESCAPE_PARAMETER;

	QLAC__ASSERT(blocksize > 0);

	/* verbatim is the baseline against which we measure the other (compressed) predictors */
	_best_frame = 0;
	_best_bits = evaluate_verbatim_(integer_signal, blocksize, frame_bps, &frame[_best_frame]);
	*best_bits = _best_bits;

	if(blocksize > QLAC__MAX_FIXED_ORDER) {
		uint32_t signal_is_constant = false;
		/* The next formula determines when to use a 64-bit accumulator
		 * for the error of a fixed predictor, and when a 32-bit one. As
		 * the error of a 4th order predictor for a given sample is the
		 * sum of 17 sample values (1+4+6+4+1) and there are blocksize -
		 * order error values to be summed, the maximum total error is
		 * maximum_sample_value * (blocksize - order) * 17. As ilog2(x)
		 * calculates floor(2log(x)), the result must be 31 or lower
		 */
		if(frame_bps < 28) {
			if(frame_bps + QLAC__bitmath_ilog2((blocksize - QLAC__MAX_FIXED_ORDER) * 17) < 32)
				guess_fixed_order = encoder->private_->local_fixed_compute_best_predictor(((QLAC__int32 *)integer_signal) + QLAC__MAX_FIXED_ORDER, blocksize - QLAC__MAX_FIXED_ORDER, fixed_residual_bits_per_sample);
			else
				guess_fixed_order = encoder->private_->local_fixed_compute_best_predictor_wide(((QLAC__int32 *)integer_signal) + QLAC__MAX_FIXED_ORDER, blocksize - QLAC__MAX_FIXED_ORDER, fixed_residual_bits_per_sample);
		}
		else
			guess_fixed_order = encoder->private_->local_fixed_compute_best_predictor_limit_residual(((QLAC__int32 *)integer_signal + QLAC__MAX_FIXED_ORDER), blocksize - QLAC__MAX_FIXED_ORDER, fixed_residual_bits_per_sample);

		/* check for constant frame */
		if(
#ifndef QLAC__INTEGER_ONLY_LIBRARY
			fixed_residual_bits_per_sample[1] == 0.0
#else
			fixed_residual_bits_per_sample[1] == QLAC__FP_ZERO
#endif
		) {
			/* the above means it's possible all samples are the same value; now double-check it: */
			uint32_t i;
			const QLAC__int32 *integer_signal_ = integer_signal;
			signal_is_constant = true;
			for(i = 1; i < blocksize; i++) {
				if(integer_signal_[0] != integer_signal_[i]) {
					signal_is_constant = false;
					break;
				}
			}
		}
		if(signal_is_constant) {
			_candidate_bits = evaluate_constant_(((QLAC__int32 *)integer_signal)[0], frame_bps, &frame[!_best_frame]);

			if(_candidate_bits < _best_bits) {
				_best_frame = !_best_frame;
				_best_bits = _candidate_bits;
			}
		}
		else {

			/* encode fixed */
			if(encoder->protected_->do_exhaustive_model_search) {
				min_fixed_order = 0;
				max_fixed_order = QLAC__MAX_FIXED_ORDER;
			}
			else {
				min_fixed_order = max_fixed_order = guess_fixed_order;
			}
			if(max_fixed_order >= blocksize)
				max_fixed_order = blocksize - 1;
			for(fixed_order = min_fixed_order; fixed_order <= max_fixed_order; fixed_order++) {
#ifndef QLAC__INTEGER_ONLY_LIBRARY
				if(fixed_residual_bits_per_sample[fixed_order] >= (float)frame_bps)
					continue; /* don't even try */
#else
				if(QLAC__fixedpoint_trunc(fixed_residual_bits_per_sample[fixed_order]) >= (int)frame_bps)
					continue; /* don't even try */
#endif
				_candidate_bits =
					evaluate_fixed_(
						integer_signal,
						residual[!_best_frame],
						blocksize,
						frame_bps,
						fixed_order,
						rice_parameter_limit,
						encoder->protected_->rice_parameter_search_dist,
						&frame[!_best_frame]);
				if(_candidate_bits < _best_bits) {
					_best_frame = !_best_frame;
					_best_bits = _candidate_bits;
				}
			}

#ifndef QLAC__INTEGER_ONLY_LIBRARY
			/* encode lpc */
			if(encoder->protected_->max_lpc_order > 0) {
				if(encoder->protected_->max_lpc_order >= blocksize)
					max_lpc_order = blocksize - 1;
				else
					max_lpc_order = encoder->protected_->max_lpc_order;
				if(max_lpc_order > 0) {
					apply_apodization_state.a = 0;
					apply_apodization_state.b = 1;
					apply_apodization_state.c = 0;
					while(apply_apodization_state.a < encoder->protected_->num_apodizations) {
						uint32_t max_lpc_order_this_apodization = max_lpc_order;

						if(!apply_apodization_(encoder, &apply_apodization_state,
											   blocksize, lpc_error,
											   &max_lpc_order_this_apodization,
											   frame_bps, integer_signal,
											   &guess_lpc_order))
							/* If apply_apodization_ fails, try next apodization */
							continue;

						if(encoder->protected_->do_exhaustive_model_search) {
							min_lpc_order = 1;
						}
						else {
							min_lpc_order = max_lpc_order_this_apodization = guess_lpc_order;
						}
						for(lpc_order = min_lpc_order; lpc_order <= max_lpc_order_this_apodization; lpc_order++) {
							lpc_residual_bits_per_sample = QLAC__lpc_compute_expected_bits_per_residual_sample(lpc_error[lpc_order - 1], blocksize - lpc_order);
							if(lpc_residual_bits_per_sample >= (double)frame_bps)
								continue; /* don't even try */
							if(encoder->protected_->do_qlp_coeff_prec_search) {
								min_qlp_coeff_precision = QLAC__MIN_QLP_COEFF_PRECISION;
								/* try to keep qlp coeff precision such that only 32-bit math is required for decode of <=16bps(+1bps for side channel) streams */
								if(frame_bps <= 17) {
									max_qlp_coeff_precision = QLAC_min(32 - frame_bps - QLAC__bitmath_ilog2(lpc_order), QLAC__MAX_QLP_COEFF_PRECISION);
									max_qlp_coeff_precision = QLAC_max(max_qlp_coeff_precision, min_qlp_coeff_precision);
								}
								else
									max_qlp_coeff_precision = QLAC__MAX_QLP_COEFF_PRECISION;
							}
							else {
								min_qlp_coeff_precision = max_qlp_coeff_precision = encoder->protected_->qlp_coeff_precision;
							}
							for(qlp_coeff_precision = min_qlp_coeff_precision; qlp_coeff_precision <= max_qlp_coeff_precision; qlp_coeff_precision++) {
								_candidate_bits =
									evaluate_lpc_(
										encoder,
										integer_signal,
										residual[!_best_frame],
										encoder->private_->lp_coeff[lpc_order - 1],
										blocksize,
										frame_bps,
										lpc_order,
										qlp_coeff_precision,
										rice_parameter_limit,
										encoder->protected_->rice_parameter_search_dist,
										&frame[!_best_frame]);
								if(_candidate_bits > 0) { /* if == 0, there was a problem quantizing the lpcoeffs */
									if(_candidate_bits < _best_bits) {
										_best_frame = !_best_frame;
										_best_bits = _candidate_bits;
									}
								}
							}
						}
					}
				}
			}
#endif /* !defined QLAC__INTEGER_ONLY_LIBRARY */
		}
	}

	/* under rare circumstances this can happen when all but the lpc predictor are disabled: */
	if(_best_bits == UINT32_MAX) {
		QLAC__ASSERT(_best_frame == 0);
		_best_bits = evaluate_verbatim_(integer_signal, blocksize, frame_bps, &frame[_best_frame]);
	}

	*best_frame = _best_frame;
	*best_bits = _best_bits;

	return true;
}

#ifndef QLAC__INTEGER_ONLY_LIBRARY
static inline void set_next_subdivide_tukey(QLAC__int32 parts, uint32_t *apodizations, uint32_t *current_depth, uint32_t *current_part)
{
	// current_part is interleaved: even are partial, odd are punchout
	if(*current_depth == 2) {
		// For depth 2, we only do partial, no punchout as that is almost redundant
		if(*current_part == 0) {
			*current_part = 2;
		}
		else { /* *current_path == 2 */
			*current_part = 0;
			(*current_depth)++;
		}
	}
	else if((*current_part) < (2 * (*current_depth) - 1)) {
		(*current_part)++;
	}
	else { /* (*current_part) >= (2*(*current_depth)-1) */
		*current_part = 0;
		(*current_depth)++;
	}

	/* Now check if we are done with this SUBDIVIDE_TUKEY apodization */
	if(*current_depth > (uint32_t)parts) {
		(*apodizations)++;
		*current_depth = 1;
		*current_part = 0;
	}
}

bool apply_apodization_(QLAC__StreamEncoder *encoder,
						apply_apodization_state_struct *apply_apodization_state,
						uint32_t blocksize,
						double *lpc_error,
						uint32_t *max_lpc_order_this_apodization,
						uint32_t frame_bps,
						const void *integer_signal,
						uint32_t *guess_lpc_order)
{
	apply_apodization_state->current_apodization = &encoder->protected_->apodizations[apply_apodization_state->a];

	if(apply_apodization_state->b == 1) {
		/* window full subblock */
		QLAC__lpc_window_data(integer_signal, encoder->private_->window[apply_apodization_state->a], encoder->private_->windowed_signal, blocksize);
		encoder->private_->local_lpc_compute_autocorrelation(encoder->private_->windowed_signal, blocksize, (*max_lpc_order_this_apodization) + 1, apply_apodization_state->autoc);
		if(apply_apodization_state->current_apodization->type == QLAC__APODIZATION_SUBDIVIDE_TUKEY) {
			uint32_t i;
			for(i = 0; i < *max_lpc_order_this_apodization; i++)
				memcpy(apply_apodization_state->autoc_root, apply_apodization_state->autoc, *max_lpc_order_this_apodization * sizeof(apply_apodization_state->autoc[0]));

			(apply_apodization_state->b)++;
		}
		else {
			(apply_apodization_state->a)++;
		}
	}
	else {
		/* window part of subblock */
		if(blocksize / apply_apodization_state->b <= QLAC__MAX_LPC_ORDER) {
			/* intrinsics autocorrelation routines do not all handle cases in which lag might be
			 * larger than data_len, and some routines round lag up to the nearest multiple of 4
			 * As little gain is expected from using LPC on part of a signal as small as 32 samples
			 * and to enable widening this rounding up to larger values in the future, windowing
			 * parts smaller than or equal to QLAC__MAX_LPC_ORDER (which is 32) samples is not supported */
			set_next_subdivide_tukey(apply_apodization_state->current_apodization->parameters.subdivide_tukey.parts, &apply_apodization_state->a, &apply_apodization_state->b, &apply_apodization_state->c);
			return false;
		}
		if(!(apply_apodization_state->c % 2)) {
			/* on even c, evaluate the (c/2)th partial window of size blocksize/b  */
			QLAC__lpc_window_data_partial(integer_signal, encoder->private_->window[apply_apodization_state->a], encoder->private_->windowed_signal, blocksize, blocksize / apply_apodization_state->b / 2, (apply_apodization_state->c / 2 * blocksize) / apply_apodization_state->b);
			encoder->private_->local_lpc_compute_autocorrelation(encoder->private_->windowed_signal, blocksize / apply_apodization_state->b, (*max_lpc_order_this_apodization) + 1, apply_apodization_state->autoc);
		}
		else {
			/* on uneven c, evaluate the root window (over the whole block) minus the previous partial window
			 * similar to tukey_punchout apodization but more efficient */
			uint32_t i;
			for(i = 0; i < *max_lpc_order_this_apodization; i++)
				apply_apodization_state->autoc[i] = apply_apodization_state->autoc_root[i] - apply_apodization_state->autoc[i];
		}
		/* Next function sets a, b and c appropriate for next iteration */
		set_next_subdivide_tukey(apply_apodization_state->current_apodization->parameters.subdivide_tukey.parts, &apply_apodization_state->a, &apply_apodization_state->b, &apply_apodization_state->c);
	}

	if(apply_apodization_state->autoc[0] == 0.0) /* Signal seems to be constant, so we can't do lp. Constant detection is probably disabled */
		return false;
	QLAC__lpc_compute_lp_coefficients(apply_apodization_state->autoc, max_lpc_order_this_apodization, encoder->private_->lp_coeff, lpc_error);
	*guess_lpc_order =
		QLAC__lpc_compute_best_order(
			lpc_error,
			*max_lpc_order_this_apodization,
			blocksize,
			frame_bps + (encoder->protected_->do_qlp_coeff_prec_search ? QLAC__MIN_QLP_COEFF_PRECISION : /* have to guess; use the min possible size to avoid accidentally favoring lower orders */
							 encoder->protected_->qlp_coeff_precision));
	return true;
}
#endif

bool add_frame_data_(
	QLAC__StreamEncoder *encoder,
	uint32_t blocksize,
	uint32_t frame_bps,
	const QLAC__Frame *frame,
	QLAC__BitWriter *bw)
{
	switch(frame->type) {
		case QLAC__FRAME_TYPE_CONSTANT:
			if(!QLAC__frame_add_constant(frame, frame_bps, bw)) {
				encoder->protected_->state = QLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		case QLAC__FRAME_TYPE_FIXED:
			if(!QLAC__frame_add_fixed(frame, blocksize - frame->order, frame_bps, bw)) {
				encoder->protected_->state = QLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		case QLAC__FRAME_TYPE_LPC:
			if(!QLAC__frame_add_lpc(frame, blocksize - frame->order, frame_bps, bw)) {
				encoder->protected_->state = QLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		case QLAC__FRAME_TYPE_VERBATIM:
			if(!QLAC__frame_add_verbatim(frame, blocksize, frame_bps, bw)) {
				encoder->protected_->state = QLAC__STREAM_ENCODER_FRAMING_ERROR;
				return false;
			}
			break;
		default:
			QLAC__ASSERT(0);
	}

	return true;
}

uint32_t evaluate_constant_(
	const QLAC__int64 signal,
	uint32_t frame_bps,
	QLAC__Frame *frame)
{
	uint32_t estimate;
	frame->type = QLAC__FRAME_TYPE_CONSTANT;
	frame->constant_value = signal;

	estimate = QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN + frame->wasted_bits + frame_bps;

	return estimate;
}

uint32_t evaluate_fixed_(
	const void *signal,
	QLAC__int32 residual[],
	uint32_t blocksize,
	uint32_t frame_bps,
	uint32_t order,
	uint32_t rice_parameter_limit,
	uint32_t rice_parameter_search_dist,
	QLAC__Frame *frame)
{
	uint32_t i, residual_bits, estimate;
	const uint32_t residual_samples = blocksize - order;

	if((frame_bps + order) <= 32)
		QLAC__fixed_compute_residual(((QLAC__int32 *)signal) + order, residual_samples, order, residual);
	else
		QLAC__fixed_compute_residual_wide(((QLAC__int32 *)signal) + order, residual_samples, order, residual);

	frame->type = QLAC__FRAME_TYPE_FIXED;
	frame->residual = residual;
	frame->order = order;

	residual_bits =
		set_rice_parameter_(
			residual,
			residual_samples,
			order,
			rice_parameter_limit,
			frame_bps,
			rice_parameter_search_dist,
			frame);

	for(i = 0; i < order; i++)
		frame->warmup[i] = ((QLAC__int32 *)signal)[i];

	estimate = QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN + frame->wasted_bits + (order * frame_bps);
	if(residual_bits < UINT32_MAX - estimate) // To make sure estimate doesn't overflow
		estimate += residual_bits;
	else
		estimate = UINT32_MAX;

	return estimate;
}

#ifndef QLAC__INTEGER_ONLY_LIBRARY
uint32_t evaluate_lpc_(
	QLAC__StreamEncoder *encoder,
	const void *signal,
	QLAC__int32 residual[],
	const QLAC__real lp_coeff[],
	uint32_t blocksize,
	uint32_t frame_bps,
	uint32_t order,
	uint32_t qlp_coeff_precision,
	uint32_t rice_parameter_limit,
	uint32_t rice_parameter_search_dist,
	QLAC__Frame *frame)
{
	QLAC__int32 qlp_coeff[QLAC__MAX_LPC_ORDER]; /* WATCHOUT: the size is important; some x86 intrinsic routines need more than lpc order elements */
	uint32_t i, residual_bits, estimate;
	int quantization, ret;
	const uint32_t residual_samples = blocksize - order;

	/* try to keep qlp coeff precision such that only 32-bit math is required for decode of <=16bps(+1bps for side channel) streams */
	if(frame_bps <= 17) {
		QLAC__ASSERT(order > 0);
		QLAC__ASSERT(order <= QLAC__MAX_LPC_ORDER);
		qlp_coeff_precision = QLAC_min(qlp_coeff_precision, 32 - frame_bps - QLAC__bitmath_ilog2(order));
	}

	ret = QLAC__lpc_quantize_coefficients(lp_coeff, order, qlp_coeff_precision, qlp_coeff, &quantization);
	if(ret != 0)
		return 0; /* this is a hack to indicate to the caller that we can't do lp at this order on this frame */

	if(QLAC__lpc_max_residual_bps(frame_bps, qlp_coeff, order, quantization) > 32) {
		if(!QLAC__lpc_compute_residual_from_qlp_coefficients_limit_residual(((QLAC__int32 *)signal) + order, residual_samples, qlp_coeff, order, quantization, residual))
			return 0;
	}
	else if(QLAC__lpc_max_prediction_before_shift_bps(frame_bps, qlp_coeff, order) <= 32)
		if(frame_bps <= 16 && qlp_coeff_precision <= 16)
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_16bit(((QLAC__int32 *)signal) + order, residual_samples, qlp_coeff, order, quantization, residual);
		else
			encoder->private_->local_lpc_compute_residual_from_qlp_coefficients(((QLAC__int32 *)signal) + order, residual_samples, qlp_coeff, order, quantization, residual);
	else
		encoder->private_->local_lpc_compute_residual_from_qlp_coefficients_64bit(((QLAC__int32 *)signal) + order, residual_samples, qlp_coeff, order, quantization, residual);

	frame->type = QLAC__FRAME_TYPE_LPC;
	frame->residual = residual;
	frame->order = order;

	residual_bits =
		set_rice_parameter_(
			residual,
			residual_samples,
			order,
			rice_parameter_limit,
			frame_bps,
			rice_parameter_search_dist,
			frame);

	frame->qlp_coeff_precision = qlp_coeff_precision;
	frame->quantization_level = quantization;
	memcpy(frame->qlp_coeff, qlp_coeff, sizeof(QLAC__int32) * QLAC__MAX_LPC_ORDER);
	for(i = 0; i < order; i++)
		frame->warmup[i] = ((QLAC__int32 *)signal)[i];

	estimate = QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN + frame->wasted_bits + QLAC__FRAME_LPC_QLP_COEFF_PRECISION_LEN + QLAC__FRAME_LPC_QLP_SHIFT_LEN + (order * (qlp_coeff_precision + frame_bps));
	if(residual_bits < UINT32_MAX - estimate) // To make sure estimate doesn't overflow
		estimate += residual_bits;
	else
		estimate = UINT32_MAX;

	return estimate;
}
#endif

uint32_t evaluate_verbatim_(
	const void *signal,
	uint32_t blocksize,
	uint32_t frame_bps,
	QLAC__Frame *frame)
{
	uint32_t estimate;

	frame->type = QLAC__FRAME_TYPE_VERBATIM;
	frame->verbatim_data = signal;

	estimate = QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN + QLAC__FRAME_SUBTYPE_LEN + QLAC__FRAME_WASTED_BITS_FLAG_LEN + frame->wasted_bits + (blocksize * frame_bps);

	return estimate;
}

#ifdef EXACT_RICE_BITS_CALCULATION
static inline uint32_t count_rice_bits_(
	const uint32_t rice_parameter,
	const uint32_t residual_samples,
	const QLAC__int32 *residual)
{
	uint32_t i;
	uint64_t bits =
		QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN + /* always RICE2 (5-bit parameter) in this fork */
		(1 + rice_parameter) * residual_samples /* 1 for unary stop bit + rice_parameter for the binary portion */
		;
	for(i = 0; i < residual_samples; i++)
		bits += ((QLAC__uint32)((residual[i] << 1) ^ (residual[i] >> 31)) >> rice_parameter);
	return (uint32_t)(QLAC_min(bits, UINT32_MAX)); // To make sure the return value doesn't overflow
}
#else
static inline uint32_t count_rice_bits_(
	const uint32_t rice_parameter,
	const uint32_t residual_samples,
	const QLAC__uint64 abs_residual_sum)
{
	return (uint32_t)(QLAC_min( // To make sure the return value doesn't overflow
		QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN + /* always RICE2 (5-bit parameter) in this fork */
			(1 + rice_parameter) * residual_samples + /* 1 for unary stop bit + rice_parameter for the binary portion */
			(
				rice_parameter ? (abs_residual_sum >> (rice_parameter - 1)) /* rice_parameter-1 because the real coder sign-folds instead of using a sign bit */
							   : (abs_residual_sum << 1) /* can't shift by negative number, so reverse */
				) -
			(residual_samples >> 1),
		UINT32_MAX));
	/* -(residual_samples>>1) to subtract out extra contributions to the abs_residual_sum.
	 * The actual number of bits used is closer to the sum(for all i) of abs(residual[i])>>(rice_parameter-1)
	 * By using the abs_residual sum, we also add in bits in the LSBs that would normally be shifted out.
	 * So the subtraction term tries to guess how many extra bits were contributed.
	 * If the LSBs are randomly distributed, this should average to 0.5 extra bits per sample.
	 */
	;
}
#endif

/* Compute the single Rice parameter for the whole residual (one partition).
 * Stores the chosen parameter / escape into the frame's entropy coding fields
 * and returns the estimated number of bits the coded residual takes, including
 * the entropy-coding-method type and (always-zero) partition-order fields.
 */
uint32_t set_rice_parameter_(
	const QLAC__int32 residual[],
	uint32_t residual_samples,
	uint32_t predictor_order,
	uint32_t rice_parameter_limit,
	uint32_t bps,
	uint32_t rice_parameter_search_dist,
	QLAC__Frame *frame)
{
	uint32_t rice_parameter, rice_bits;
	uint32_t best_bits, best_rice_parameter = 0;
	uint32_t best_raw_bits = 0;
	/* no method type or partition order field in this fork */
	uint32_t bits = 0;
	uint32_t fixed_point_divisor;
	QLAC__uint64 abs_residual_sum;
	QLAC__uint32 raw_bits;
	uint32_t i;
#ifdef ENABLE_RICE_PARAMETER_SEARCH
	uint32_t min_rice_parameter, max_rice_parameter;
#else
	(void)rice_parameter_search_dist;
#endif

	(void)predictor_order;
	(void)bps;

	QLAC__ASSERT(rice_parameter_limit <= QLAC__ENTROPY_CODING_METHOD_RICE2_ESCAPE_PARAMETER);
	QLAC__ASSERT(residual_samples > 0);

	/* Sum the magnitudes of all residual values (single partition), and at the
	 * same time track the largest magnitude for escape (raw) coding. */
	abs_residual_sum = 0;
	raw_bits = 0;
	for(i = 0; i < residual_samples; i++) {
		QLAC__int32 r = residual[i];
		abs_residual_sum += abs(r); /* abs(INT_MIN) is undefined, but if the residual is INT_MIN we have bigger problems */
		raw_bits |= (r < 0) ? (QLAC__uint32)(~r) : (QLAC__uint32)r;
	}
	/* now all residual values are in the range [-raw_bits-1, raw_bits] */
	raw_bits = raw_bits ? QLAC__bitmath_ilog2(raw_bits) + 2 : 1;

	/* Estimate the optimal Rice parameter from the mean magnitude.
	 * 18-bit fixed-point divisor as in the original partitioned code. */
	fixed_point_divisor = 0x40000 / residual_samples;
	if(abs_residual_sum < 2 || (((abs_residual_sum - 1) * fixed_point_divisor) >> 18) == 0)
		rice_parameter = 0;
	else
		rice_parameter = QLAC__bitmath_ilog2_wide(((abs_residual_sum - 1) * fixed_point_divisor) >> 18) + 1;

	if(rice_parameter >= rice_parameter_limit)
		rice_parameter = rice_parameter_limit - 1;

	best_bits = UINT32_MAX;
#ifdef ENABLE_RICE_PARAMETER_SEARCH
	if(rice_parameter_search_dist) {
		if(rice_parameter < rice_parameter_search_dist)
			min_rice_parameter = 0;
		else
			min_rice_parameter = rice_parameter - rice_parameter_search_dist;
		max_rice_parameter = rice_parameter + rice_parameter_search_dist;
		if(max_rice_parameter >= rice_parameter_limit)
			max_rice_parameter = rice_parameter_limit - 1;
	}
	else
		min_rice_parameter = max_rice_parameter = rice_parameter;

	for(rice_parameter = min_rice_parameter; rice_parameter <= max_rice_parameter; rice_parameter++) {
#endif
#ifdef EXACT_RICE_BITS_CALCULATION
		rice_bits = count_rice_bits_(rice_parameter, residual_samples, residual);
#else
	rice_bits = count_rice_bits_(rice_parameter, residual_samples, abs_residual_sum);
#endif
		if(rice_bits < best_bits) {
			best_rice_parameter = rice_parameter;
			best_bits = rice_bits;
		}
#ifdef ENABLE_RICE_PARAMETER_SEARCH
	}
#endif
	frame->rice_parameter = best_rice_parameter;
	frame->rice_raw_bits = best_raw_bits;
	/* entropy coding is always RICE2 in this fork (5-bit parameter, no method
	 * type field in the bitstream) */

	if(best_bits < UINT32_MAX - bits) // To make sure bits doesn't overflow
		bits += best_bits;
	else
		bits = UINT32_MAX;

	return bits;
}

uint32_t get_wasted_bits_(QLAC__int32 signal[], uint32_t samples)
{
	uint32_t i, shift;
	QLAC__int32 x = 0;

	for(i = 0; i < samples && !(x & 1); i++)
		x |= signal[i];

	if(x == 0) {
		shift = 0;
	}
	else {
		for(shift = 0; !(x & 1); shift++)
			x >>= 1;
	}

	if(shift > 0) {
		for(i = 0; i < samples; i++)
			signal[i] >>= shift;
	}

	return shift;
}
