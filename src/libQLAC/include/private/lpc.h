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

#ifndef QLAC__PRIVATE__LPC_H
#define QLAC__PRIVATE__LPC_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "private/cpu.h"
#include "private/float.h"
#include "QLAC/format.h"

#ifndef QLAC__INTEGER_ONLY_LIBRARY

/*
 *	QLAC__lpc_window_data()
 *	--------------------------------------------------------------------
 *	Applies the given window to the data.
 *  OPT: asm implementation
 *
 *	IN in[0,data_len-1]
 *	IN window[0,data_len-1]
 *	OUT out[0,lag-1]
 *	IN data_len
 */
void QLAC__lpc_window_data(const QLAC__int32 in[], const QLAC__real window[], QLAC__real out[], uint32_t data_len);
void QLAC__lpc_window_data_wide(const QLAC__int64 in[], const QLAC__real window[], QLAC__real out[], uint32_t data_len);
void QLAC__lpc_window_data_partial(const QLAC__int32 in[], const QLAC__real window[], QLAC__real out[], uint32_t data_len, uint32_t part_size, uint32_t data_shift);
void QLAC__lpc_window_data_partial_wide(const QLAC__int64 in[], const QLAC__real window[], QLAC__real out[], uint32_t data_len, uint32_t part_size, uint32_t data_shift);

/*
 *	QLAC__lpc_compute_autocorrelation()
 *	--------------------------------------------------------------------
 *	Compute the autocorrelation for lags between 0 and lag-1.
 *	Assumes data[] outside of [0,data_len-1] == 0.
 *	Asserts that lag > 0.
 *
 *	IN data[0,data_len-1]
 *	IN data_len
 *	IN 0 < lag <= data_len
 *	OUT autoc[0,lag-1]
 */
void QLAC__lpc_compute_autocorrelation(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#ifndef QLAC__NO_ASM
#  if (defined QLAC__CPU_IA32 || defined QLAC__CPU_X86_64) && QLAC__HAS_X86INTRIN
#    ifdef QLAC__SSE2_SUPPORTED
void QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_8(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_10(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void QLAC__lpc_compute_autocorrelation_intrin_sse2_lag_14(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#    endif
#  endif
#  if defined QLAC__CPU_X86_64 && QLAC__HAS_X86INTRIN
#    ifdef QLAC__FMA_SUPPORTED
void QLAC__lpc_compute_autocorrelation_intrin_fma_lag_8(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void QLAC__lpc_compute_autocorrelation_intrin_fma_lag_12(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void QLAC__lpc_compute_autocorrelation_intrin_fma_lag_16(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#    endif
#  endif
#if defined QLAC__CPU_ARM64 && QLAC__HAS_NEONINTRIN && QLAC__HAS_A64NEONINTRIN
void QLAC__lpc_compute_autocorrelation_intrin_neon_lag_8(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void QLAC__lpc_compute_autocorrelation_intrin_neon_lag_10(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void QLAC__lpc_compute_autocorrelation_intrin_neon_lag_14(const QLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#endif
#endif /* QLAC__NO_ASM */

/*
 *	QLAC__lpc_compute_lp_coefficients()
 *	--------------------------------------------------------------------
 *	Computes LP coefficients for orders 1..max_order.
 *	Do not call if autoc[0] == 0.0.  This means the signal is zero
 *	and there is no point in calculating a predictor.
 *
 *	IN autoc[0,max_order]                      autocorrelation values
 *	IN 0 < max_order <= QLAC__MAX_LPC_ORDER    max LP order to compute
 *	OUT lp_coeff[0,max_order-1][0,max_order-1] LP coefficients for each order
 *	*** IMPORTANT:
 *	*** lp_coeff[0,max_order-1][max_order,QLAC__MAX_LPC_ORDER-1] are untouched
 *	OUT error[0,max_order-1]                   error for each order (more
 *	                                           specifically, the variance of
 *	                                           the error signal times # of
 *	                                           samples in the signal)
 *
 *	Example: if max_order is 9, the LP coefficients for order 9 will be
 *	         in lp_coeff[8][0,8], the LP coefficients for order 8 will be
 *			 in lp_coeff[7][0,7], etc.
 */
void QLAC__lpc_compute_lp_coefficients(const double autoc[], uint32_t *max_order, QLAC__real lp_coeff[][QLAC__MAX_LPC_ORDER], double error[]);

/*
 *	QLAC__lpc_quantize_coefficients()
 *	--------------------------------------------------------------------
 *	Quantizes the LP coefficients.  NOTE: precision + bits_per_sample
 *	must be less than 32 (sizeof(QLAC__int32)*8).
 *
 *	IN lp_coeff[0,order-1]    LP coefficients
 *	IN order                  LP order
 *	IN QLAC__MIN_QLP_COEFF_PRECISION < precision
 *	                          desired precision (in bits, including sign
 *	                          bit) of largest coefficient
 *	OUT qlp_coeff[0,order-1]  quantized coefficients
 *	OUT shift                 # of bits to shift right to get approximated
 *	                          LP coefficients.  NOTE: could be negative.
 *	RETURN 0 => quantization OK
 *	       1 => coefficients require too much shifting for *shift to
 *              fit in the LPC subframe header.  'shift' is unset.
 *         2 => coefficients are all zero, which is bad.  'shift' is
 *              unset.
 */
int QLAC__lpc_quantize_coefficients(const QLAC__real lp_coeff[], uint32_t order, uint32_t precision, QLAC__int32 qlp_coeff[], int *shift);

/*
 *	QLAC__lpc_compute_residual_from_qlp_coefficients()
 *	--------------------------------------------------------------------
 *	Compute the residual signal obtained from sutracting the predicted
 *	signal from the original.
 *
 *	IN data[-order,data_len-1] original signal (NOTE THE INDICES!)
 *	IN data_len                length of original signal
 *	IN qlp_coeff[0,order-1]    quantized LP coefficients
 *	IN order > 0               LP order
 *	IN lp_quantization         quantization of LP coefficients in bits
 *	OUT residual[0,data_len-1] residual signal
 */
void QLAC__lpc_compute_residual_from_qlp_coefficients(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
void QLAC__lpc_compute_residual_from_qlp_coefficients_wide(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
bool QLAC__lpc_compute_residual_from_qlp_coefficients_limit_residual(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
bool QLAC__lpc_compute_residual_from_qlp_coefficients_limit_residual_33bit(const QLAC__int64 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
#ifndef QLAC__NO_ASM
#   ifdef QLAC__CPU_ARM64
void QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_neon(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
void QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_neon(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
#   endif

#  if (defined QLAC__CPU_IA32 || defined QLAC__CPU_X86_64) && QLAC__HAS_X86INTRIN
#    ifdef QLAC__SSE2_SUPPORTED
void QLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_sse2(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
void QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse2(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
#    endif
#    ifdef QLAC__SSE4_1_SUPPORTED
void QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse41(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
void QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_sse41(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
#    endif
#    ifdef QLAC__AVX2_SUPPORTED
void QLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_avx2(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
void QLAC__lpc_compute_residual_from_qlp_coefficients_intrin_avx2(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
void QLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_avx2(const QLAC__int32 *data, uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 residual[]);
#    endif
#  endif
#endif

#endif /* !defined QLAC__INTEGER_ONLY_LIBRARY */

QLAC__uint64 QLAC__lpc_max_prediction_value_before_shift(uint32_t subframe_bps, const QLAC__int32 qlp_coeff[], uint32_t order);
uint32_t QLAC__lpc_max_prediction_before_shift_bps(uint32_t subframe_bps, const QLAC__int32 qlp_coeff[], uint32_t order);
uint32_t QLAC__lpc_max_residual_bps(uint32_t subframe_bps, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization);

/*
 *	QLAC__lpc_restore_signal()
 *	--------------------------------------------------------------------
 *	Restore the original signal by summing the residual and the
 *	predictor.
 *
 *	IN residual[0,data_len-1]  residual signal
 *	IN data_len                length of original signal
 *	IN qlp_coeff[0,order-1]    quantized LP coefficients
 *	IN order > 0               LP order
 *	IN lp_quantization         quantization of LP coefficients in bits
 *	*** IMPORTANT: the caller must pass in the historical samples:
 *	IN  data[-order,-1]        previously-reconstructed historical samples
 *	OUT data[0,data_len-1]     original signal
 */
void QLAC__lpc_restore_signal(const QLAC__int32 residual[], uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 data[]);
void QLAC__lpc_restore_signal_wide(const QLAC__int32 residual[], uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int32 data[]);
void QLAC__lpc_restore_signal_wide_33bit(const QLAC__int32 residual[], uint32_t data_len, const QLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, QLAC__int64 data[]);

#ifndef QLAC__INTEGER_ONLY_LIBRARY

/*
 *	QLAC__lpc_compute_expected_bits_per_residual_sample()
 *	--------------------------------------------------------------------
 *	Compute the expected number of bits per residual signal sample
 *	based on the LP error (which is related to the residual variance).
 *
 *	IN lpc_error >= 0.0   error returned from calculating LP coefficients
 *	IN total_samples > 0  # of samples in residual signal
 *	RETURN                expected bits per sample
 */
double QLAC__lpc_compute_expected_bits_per_residual_sample(double lpc_error, uint32_t total_samples);
double QLAC__lpc_compute_expected_bits_per_residual_sample_with_error_scale(double lpc_error, double error_scale);

/*
 *	QLAC__lpc_compute_best_order()
 *	--------------------------------------------------------------------
 *	Compute the best order from the array of signal errors returned
 *	during coefficient computation.
 *
 *	IN lpc_error[0,max_order-1] >= 0.0  error returned from calculating LP coefficients
 *	IN max_order > 0                    max LP order
 *	IN total_samples > 0                # of samples in residual signal
 *	IN overhead_bits_per_order          # of bits overhead for each increased LP order
 *	                                    (includes warmup sample size and quantized LP coefficient)
 *	RETURN [1,max_order]                best order
 */
uint32_t QLAC__lpc_compute_best_order(const double lpc_error[], uint32_t max_order, uint32_t total_samples, uint32_t overhead_bits_per_order);

#endif /* !defined QLAC__INTEGER_ONLY_LIBRARY */

#endif
