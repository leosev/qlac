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

#include <stdio.h>
#include <stdlib.h> /* for qsort() */
#include <string.h> /* for memset() */
#include "QLAC/assert.h"
#include "QLAC/format.h"
#include "share/alloc.h"
#include "share/compat.h"
#include "QLAC/format.h"
#include "private/macros.h"



QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN = 16; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN = 16; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN = 24; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN = 24; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN = 3; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN = 5; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN = 36; /* bits */




QLAC_API const uint32_t QLAC__STREAM_METADATA_IS_LAST_LEN = 1; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_TYPE_LEN = 7; /* bits */
QLAC_API const uint32_t QLAC__STREAM_METADATA_LENGTH_LEN = 24; /* bits */

QLAC_API const uint32_t QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN = 5; /* bits */
QLAC_API const uint32_t QLAC__ENTROPY_CODING_METHOD_RICE_RAW_LEN = 5; /* bits */

QLAC_API const uint32_t QLAC__ENTROPY_CODING_METHOD_RICE2_ESCAPE_PARAMETER = 31; /* == (1<<QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN)-1 */

QLAC_API const uint32_t QLAC__FRAME_LPC_QLP_COEFF_PRECISION_LEN = 4; /* bits */
QLAC_API const uint32_t QLAC__FRAME_LPC_QLP_SHIFT_LEN = 5; /* bits */

QLAC_API const uint32_t QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN = 1; /* bits */
QLAC_API const uint32_t QLAC__FRAME_SUBTYPE_LEN = 6; /* bits */
QLAC_API const uint32_t QLAC__FRAME_WASTED_BITS_FLAG_LEN = 1; /* bits */

QLAC_API const uint32_t QLAC__FRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK = 0x00;
QLAC_API const uint32_t QLAC__FRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK = 0x02;
QLAC_API const uint32_t QLAC__FRAME_TYPE_FIXED_BYTE_ALIGNED_MASK = 0x10;
QLAC_API const uint32_t QLAC__FRAME_TYPE_LPC_BYTE_ALIGNED_MASK = 0x40;
