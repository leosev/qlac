/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0.  The changes
 * from upstream are mechanical only: FLAC__*/FLAC_* identifiers and include
 * paths were renamed to their QLAC equivalents.  The original libFLAC copyright
 * and license below are retained and continue to govern this file.  See
 * FORK_NOTES.md and LICENSE for details.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2004-2009  Josh Coalson
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

#ifndef QLAC__PRIVATE__FLOAT_H
#define QLAC__PRIVATE__FLOAT_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "QLAC/ordinals.h"

/*
 * All the code in libFLAC that uses float and double
 * should be protected by checks of the macro
 * QLAC__INTEGER_ONLY_LIBRARY.
 *
 */
#ifndef QLAC__INTEGER_ONLY_LIBRARY
/*
 * QLAC__real is the basic floating point type used in LPC analysis.
 *
 * WATCHOUT: changing QLAC__real will change the signatures of many
 * functions that have assembly language equivalents and break them.
 */
typedef float QLAC__real;
#else
/*
 * The convention for QLAC__fixedpoint is to use the upper 16 bits
 * for the integer part and lower 16 bits for the fractional part.
 */
typedef QLAC__int32 QLAC__fixedpoint;
extern const QLAC__fixedpoint QLAC__FP_ZERO;
extern const QLAC__fixedpoint QLAC__FP_ONE_HALF;
extern const QLAC__fixedpoint QLAC__FP_ONE;
extern const QLAC__fixedpoint QLAC__FP_LN2;
extern const QLAC__fixedpoint QLAC__FP_E;

#define QLAC__fixedpoint_trunc(x) ((x)>>16)

#define QLAC__fixedpoint_mul(x, y) ( (QLAC__fixedpoint) ( ((QLAC__int64)(x)*(QLAC__int64)(y)) >> 16 ) )

#define QLAC__fixedpoint_div(x, y) ( (QLAC__fixedpoint) ( ( ((QLAC__int64)(x)<<32) / (QLAC__int64)(y) ) >> 16 ) )

/*
 *	QLAC__fixedpoint_log2()
 *	--------------------------------------------------------------------
 *	Returns the base-2 logarithm of the fixed-point number 'x' using an
 *	algorithm by Knuth for x >= 1.0
 *
 *	'fracbits' is the number of fractional bits of 'x'.  'fracbits' must
 *	be < 32 and evenly divisible by 4 (0 is OK but not very precise).
 *
 *	'precision' roughly limits the number of iterations that are done;
 *	use (uint32_t)(-1) for maximum precision.
 *
 *	If 'x' is less than one -- that is, x < (1<<fracbits) -- then this
 *	function will punt and return 0.
 *
 *	The return value will also have 'fracbits' fractional bits.
 */
QLAC__uint32 QLAC__fixedpoint_log2(QLAC__uint32 x, uint32_t fracbits, uint32_t precision);

#endif

#endif
