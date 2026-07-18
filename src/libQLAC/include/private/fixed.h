/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0.  The changes
 * from upstream are mechanical only: FLAC__*/FLAC_* identifiers and include
 * paths were renamed to their QLAC equivalents.  The original libFLAC copyright
 * and license below are retained and continue to govern this file.  See
 * FORK_NOTES.md and LICENSE for details.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2000-2009  Josh Coalson
 * Copyright (C) 2011-2025  Xiph.Org Foundation
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

#ifndef QLAC__PRIVATE__FIXED_H
#define QLAC__PRIVATE__FIXED_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "private/cpu.h"
#include "private/float.h"
#include "QLAC/format.h"

/*
 *	QLAC__fixed_compute_best_predictor()
 *	--------------------------------------------------------------------
 *	Compute the best fixed predictor and the expected bits-per-sample
 *  of the residual signal for each order.  The _wide() version uses
 *  64-bit integers which is statistically necessary when bits-per-
 *  sample + log2(blocksize) > 30
 *
 *	IN data[0,data_len-1]
 *	IN data_len
 *	OUT residual_bits_per_sample[0,QLAC__MAX_FIXED_ORDER]
 */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
uint32_t QLAC__fixed_compute_best_predictor(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_wide(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_limit_residual(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_limit_residual_33bit(const QLAC__int64 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
# ifndef QLAC__NO_ASM
#  if (defined QLAC__CPU_IA32 || defined QLAC__CPU_X86_64) && QLAC__HAS_X86INTRIN
#   ifdef QLAC__SSE2_SUPPORTED
uint32_t QLAC__fixed_compute_best_predictor_intrin_sse2(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER + 1]);
#   endif
#   ifdef QLAC__SSSE3_SUPPORTED
uint32_t QLAC__fixed_compute_best_predictor_intrin_ssse3(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
#   endif
#   ifdef QLAC__SSE4_2_SUPPORTED
uint32_t QLAC__fixed_compute_best_predictor_limit_residual_intrin_sse42(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
#   endif
#   ifdef QLAC__AVX2_SUPPORTED
uint32_t QLAC__fixed_compute_best_predictor_wide_intrin_avx2(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_limit_residual_intrin_avx2(const QLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
#   endif
#  endif
# endif
#else
uint32_t QLAC__fixed_compute_best_predictor(const QLAC__int32 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_wide(const QLAC__int32 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_limit_residual(const QLAC__int32 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
uint32_t QLAC__fixed_compute_best_predictor_limit_residual_33bit(const QLAC__int64 data[], uint32_t data_len, QLAC__fixedpoint residual_bits_per_sample[QLAC__MAX_FIXED_ORDER+1]);
#endif

/*
 *	QLAC__fixed_compute_residual()
 *	--------------------------------------------------------------------
 *	Compute the residual signal obtained from sutracting the predicted
 *	signal from the original.
 *
 *	IN data[-order,data_len-1]        original signal (NOTE THE INDICES!)
 *	IN data_len                       length of original signal
 *	IN order <= QLAC__MAX_FIXED_ORDER fixed-predictor order
 *	OUT residual[0,data_len-1]        residual signal
 */
void QLAC__fixed_compute_residual(const QLAC__int32 data[], uint32_t data_len, uint32_t order, QLAC__int32 residual[]);
void QLAC__fixed_compute_residual_wide(const QLAC__int32 data[], uint32_t data_len, uint32_t order, QLAC__int32 residual[]);
void QLAC__fixed_compute_residual_wide_33bit(const QLAC__int64 data[], uint32_t data_len, uint32_t order, QLAC__int32 residual[]);

/*
 *	QLAC__fixed_restore_signal()
 *	--------------------------------------------------------------------
 *	Restore the original signal by summing the residual and the
 *	predictor.
 *
 *	IN residual[0,data_len-1]         residual signal
 *	IN data_len                       length of original signal
 *	IN order <= QLAC__MAX_FIXED_ORDER fixed-predictor order
 *	*** IMPORTANT: the caller must pass in the historical samples:
 *	IN  data[-order,-1]               previously-reconstructed historical samples
 *	OUT data[0,data_len-1]            original signal
 */
void QLAC__fixed_restore_signal(const QLAC__int32 residual[], uint32_t data_len, uint32_t order, QLAC__int32 data[]);
void QLAC__fixed_restore_signal_wide(const QLAC__int32 residual[], uint32_t data_len, uint32_t order, QLAC__int32 data[]);
void QLAC__fixed_restore_signal_wide_33bit(const QLAC__int32 residual[], uint32_t data_len, uint32_t order, QLAC__int64 data[]);

#endif
