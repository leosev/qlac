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

#ifndef QLAC__PRIVATE__BITWRITER_H
#define QLAC__PRIVATE__BITWRITER_H

#include <stdio.h> /* for FILE */
#include "QLAC/ordinals.h"

/*
 * opaque structure definition
 */
struct QLAC__BitWriter;
typedef struct QLAC__BitWriter QLAC__BitWriter;

/*
 * construction, deletion, initialization, etc functions
 */
QLAC__BitWriter *QLAC__bitwriter_new(void);
void QLAC__bitwriter_delete(QLAC__BitWriter *bw);
bool QLAC__bitwriter_init(QLAC__BitWriter *bw);
void QLAC__bitwriter_free(QLAC__BitWriter *bw); /* does not 'free(buffer)' */
void QLAC__bitwriter_clear(QLAC__BitWriter *bw);

/*
 * CRC functions
 *
 * non-const *bw because they have to cal QLAC__bitwriter_get_buffer()
 */
bool QLAC__bitwriter_get_write_crc16(QLAC__BitWriter *bw, QLAC__uint16 *crc);
bool QLAC__bitwriter_get_write_crc8(QLAC__BitWriter *bw, QLAC__byte *crc);

/*
 * info functions
 */
bool QLAC__bitwriter_is_byte_aligned(const QLAC__BitWriter *bw);
uint32_t QLAC__bitwriter_get_input_bits_unconsumed(const QLAC__BitWriter *bw); /* can be called anytime, returns total # of bits unconsumed */

/*
 * direct buffer access
 *
 * there may be no calls on the bitwriter between get and release.
 * the bitwriter continues to own the returned buffer.
 * before get, bitwriter MUST be byte aligned: check with QLAC__bitwriter_is_byte_aligned()
 */
bool QLAC__bitwriter_get_buffer(QLAC__BitWriter *bw, const QLAC__byte **buffer, size_t *bytes);

/*
 * write functions
 */
bool QLAC__bitwriter_write_zeroes(QLAC__BitWriter *bw, uint32_t bits);
bool QLAC__bitwriter_write_raw_uint32(QLAC__BitWriter *bw, QLAC__uint32 val, uint32_t bits);
bool QLAC__bitwriter_write_raw_int32(QLAC__BitWriter *bw, QLAC__int32 val, uint32_t bits);
bool QLAC__bitwriter_write_raw_uint64(QLAC__BitWriter *bw, QLAC__uint64 val, uint32_t bits);
bool QLAC__bitwriter_write_raw_int64(QLAC__BitWriter *bw, QLAC__int64 val, uint32_t bits);
bool QLAC__bitwriter_write_raw_uint32_little_endian(QLAC__BitWriter *bw, QLAC__uint32 val); /*only for bits=32*/
bool QLAC__bitwriter_write_byte_block(QLAC__BitWriter *bw, const QLAC__byte vals[], uint32_t nvals);
bool QLAC__bitwriter_write_unary_unsigned(QLAC__BitWriter *bw, uint32_t val);
#if 0 /* UNUSED */
uint32_t QLAC__bitwriter_rice_bits(QLAC__int32 val, uint32_t parameter);
uint32_t QLAC__bitwriter_golomb_bits_signed(int val, uint32_t parameter);
uint32_t QLAC__bitwriter_golomb_bits_unsigned(uint32_t val, uint32_t parameter);
bool QLAC__bitwriter_write_rice_signed(QLAC__BitWriter *bw, QLAC__int32 val, uint32_t parameter);
#endif
bool QLAC__bitwriter_write_rice_signed_block(QLAC__BitWriter *bw, const QLAC__int32 *vals, uint32_t nvals, uint32_t parameter);
#if 0 /* UNUSED */
bool QLAC__bitwriter_write_golomb_signed(QLAC__BitWriter *bw, int val, uint32_t parameter);
bool QLAC__bitwriter_write_golomb_unsigned(QLAC__BitWriter *bw, uint32_t val, uint32_t parameter);
#endif
bool QLAC__bitwriter_write_utf8_uint32(QLAC__BitWriter *bw, QLAC__uint32 val);
bool QLAC__bitwriter_write_utf8_uint64(QLAC__BitWriter *bw, QLAC__uint64 val);
bool QLAC__bitwriter_zero_pad_to_byte_boundary(QLAC__BitWriter *bw);

#endif
