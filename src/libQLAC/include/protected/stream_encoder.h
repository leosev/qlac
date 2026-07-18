/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0, then
 * substantially modified for QLAC (single-block, header-free operation).  The
 * original libFLAC copyright and license below are retained and govern the
 * FLAC-derived portions; modifications for QLAC are covered by the notice
 * below.  See FORK_NOTES.md and LICENSE.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2001-2009  Josh Coalson
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

#ifndef QLAC__PROTECTED__STREAM_ENCODER_H
#define QLAC__PROTECTED__STREAM_ENCODER_H

#include "QLAC/stream_encoder.h"

#ifndef QLAC__INTEGER_ONLY_LIBRARY

#include "private/float.h"

#define QLAC__MAX_APODIZATION_FUNCTIONS 32

typedef enum {
	QLAC__APODIZATION_BARTLETT,
	QLAC__APODIZATION_BARTLETT_HANN,
	QLAC__APODIZATION_BLACKMAN,
	QLAC__APODIZATION_BLACKMAN_HARRIS_4TERM_92DB_SIDELOBE,
	QLAC__APODIZATION_CONNES,
	QLAC__APODIZATION_FLATTOP,
	QLAC__APODIZATION_GAUSS,
	QLAC__APODIZATION_HAMMING,
	QLAC__APODIZATION_HANN,
	QLAC__APODIZATION_KAISER_BESSEL,
	QLAC__APODIZATION_NUTTALL,
	QLAC__APODIZATION_RECTANGLE,
	QLAC__APODIZATION_TRIANGLE,
	QLAC__APODIZATION_TUKEY,
	QLAC__APODIZATION_PARTIAL_TUKEY,
	QLAC__APODIZATION_PUNCHOUT_TUKEY,
	QLAC__APODIZATION_SUBDIVIDE_TUKEY,
	QLAC__APODIZATION_WELCH
} QLAC__ApodizationFunction;

typedef struct {
	QLAC__ApodizationFunction type;
	union {
		struct {
			QLAC__real stddev;
		} gauss;
		struct {
			QLAC__real p;
		} tukey;
		struct {
			QLAC__real p;
			QLAC__real start;
			QLAC__real end;
		} multiple_tukey;
		struct {
			QLAC__real p;
			QLAC__int32 parts;
		} subdivide_tukey;
	} parameters;
} QLAC__ApodizationSpecification;

#endif // #ifndef QLAC__INTEGER_ONLY_LIBRARY

typedef struct QLAC__StreamEncoderProtected {
	QLAC__StreamEncoderState state;
	bool streamable_subset;
	uint32_t bits_per_sample;
	uint32_t blocksize;
#ifndef QLAC__INTEGER_ONLY_LIBRARY
	uint32_t num_apodizations;
	QLAC__ApodizationSpecification apodizations[QLAC__MAX_APODIZATION_FUNCTIONS];
#endif
	uint32_t max_lpc_order;
	uint32_t qlp_coeff_precision;
	bool do_qlp_coeff_prec_search;
	bool do_exhaustive_model_search;
	uint32_t rice_parameter_search_dist;
	QLAC__uint64 total_samples_estimate;
} QLAC__StreamEncoderProtected;

#endif
