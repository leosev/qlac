/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0.  The changes
 * from upstream are mechanical only: FLAC__*/FLAC_* identifiers and include
 * paths were renamed to their QLAC equivalents.  The original libFLAC copyright
 * and license below are retained and continue to govern this file.  See
 * FORK_NOTES.md and LICENSE for details.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2006-2009  Josh Coalson
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

#ifndef QLAC__PRIVATE__WINDOW_H
#define QLAC__PRIVATE__WINDOW_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "private/float.h"
#include "QLAC/format.h"

#ifndef QLAC__INTEGER_ONLY_LIBRARY

/*
 *	QLAC__window_*()
 *	--------------------------------------------------------------------
 *	Calculates window coefficients according to different apodization
 *	functions.
 *
 *	OUT window[0,L-1]
 *	IN L (number of points in window)
 */
void QLAC__window_bartlett(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_bartlett_hann(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_blackman(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_blackman_harris_4term_92db_sidelobe(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_connes(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_flattop(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_gauss(QLAC__real *window, const QLAC__int32 L, const QLAC__real stddev); /* 0.0 < stddev <= 0.5 */
void QLAC__window_hamming(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_hann(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_kaiser_bessel(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_nuttall(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_rectangle(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_triangle(QLAC__real *window, const QLAC__int32 L);
void QLAC__window_tukey(QLAC__real *window, const QLAC__int32 L, const QLAC__real p);
void QLAC__window_partial_tukey(QLAC__real *window, const QLAC__int32 L, const QLAC__real p, const QLAC__real start, const QLAC__real end);
void QLAC__window_punchout_tukey(QLAC__real *window, const QLAC__int32 L, const QLAC__real p, const QLAC__real start, const QLAC__real end);
void QLAC__window_welch(QLAC__real *window, const QLAC__int32 L);

#endif /* !defined QLAC__INTEGER_ONLY_LIBRARY */

#endif
