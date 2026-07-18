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

#ifndef QLAC__PRIVATE__BITREADER_H
#define QLAC__PRIVATE__BITREADER_H

#include <stdio.h> /* for FILE */
#include "QLAC/ordinals.h"
#include "cpu.h"

/*
 * opaque structure definition
 */
struct QLAC__BitReader;
typedef struct QLAC__BitReader QLAC__BitReader;

typedef bool (*QLAC__BitReaderReadCallback)(QLAC__byte buffer[], size_t *bytes, void *client_data);

/*
 * construction, deletion, initialization, etc functions
 */
QLAC__BitReader *QLAC__bitreader_new(void);
void QLAC__bitreader_delete(QLAC__BitReader *br);
bool QLAC__bitreader_init(QLAC__BitReader *br, QLAC__BitReaderReadCallback rcb, void *cd);
void QLAC__bitreader_free(QLAC__BitReader *br); /* does not 'free(br)' */
bool QLAC__bitreader_clear(QLAC__BitReader *br);
void QLAC__bitreader_set_framesync_location(QLAC__BitReader *br);
bool QLAC__bitreader_rewind_to_after_last_seen_framesync(QLAC__BitReader *br);

/*
 * info functions
 */
bool QLAC__bitreader_is_consumed_byte_aligned(const QLAC__BitReader *br);
uint32_t QLAC__bitreader_bits_left_for_byte_alignment(const QLAC__BitReader *br);
uint32_t QLAC__bitreader_get_input_bits_unconsumed(const QLAC__BitReader *br);
void QLAC__bitreader_set_limit(QLAC__BitReader *br, uint32_t limit);
void QLAC__bitreader_remove_limit(QLAC__BitReader *br);
uint32_t QLAC__bitreader_limit_remaining(QLAC__BitReader *br);
void QLAC__bitreader_limit_invalidate(QLAC__BitReader *br);

/*
 * read functions
 */

bool QLAC__bitreader_read_raw_uint32(QLAC__BitReader *br, QLAC__uint32 *val, uint32_t bits);
bool QLAC__bitreader_read_raw_int32(QLAC__BitReader *br, QLAC__int32 *val, uint32_t bits);
bool QLAC__bitreader_read_raw_uint64(QLAC__BitReader *br, QLAC__uint64 *val, uint32_t bits);
bool QLAC__bitreader_read_raw_int64(QLAC__BitReader *br, QLAC__int64 *val, uint32_t bits);
bool QLAC__bitreader_read_uint32_little_endian(QLAC__BitReader *br, QLAC__uint32 *val); /*only for bits=32*/
bool QLAC__bitreader_skip_bits_no_crc(QLAC__BitReader *br, uint32_t bits); /* WATCHOUT: does not CRC the skipped data! */ /*@@@@ add to unit tests */
bool QLAC__bitreader_skip_byte_block_aligned_no_crc(QLAC__BitReader *br, uint32_t nvals); /* WATCHOUT: does not CRC the read data! */
bool QLAC__bitreader_read_byte_block_aligned_no_crc(QLAC__BitReader *br, QLAC__byte *val, uint32_t nvals); /* WATCHOUT: does not CRC the read data! */
bool QLAC__bitreader_read_unary_unsigned(QLAC__BitReader *br, uint32_t *val);
bool QLAC__bitreader_read_rice_signed(QLAC__BitReader *br, int *val, uint32_t parameter);
bool QLAC__bitreader_read_rice_signed_block(QLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter);
#ifdef QLAC__BMI2_SUPPORTED
bool QLAC__bitreader_read_rice_signed_block_bmi2(QLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter);
#endif

#if 0 /* UNUSED */
bool QLAC__bitreader_read_golomb_signed(QLAC__BitReader *br, int *val, uint32_t parameter);
bool QLAC__bitreader_read_golomb_unsigned(QLAC__BitReader *br, uint32_t *val, uint32_t parameter);
#endif
bool QLAC__bitreader_read_utf8_uint32(QLAC__BitReader *br, QLAC__uint32 *val, QLAC__byte *raw, uint32_t *rawlen);
bool QLAC__bitreader_read_utf8_uint64(QLAC__BitReader *br, QLAC__uint64 *val, QLAC__byte *raw, uint32_t *rawlen);
#endif
