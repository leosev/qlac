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

#include <stdlib.h>
#include <string.h>
#include "private/bitwriter.h"
#include "QLAC/format.h"
#include "private/macros.h"
#include "QLAC/assert.h"
#include "share/alloc.h"
#include "share/compat.h"
#include "share/endswap.h"

/* Things should be fastest when this matches the machine word size */
/* WATCHOUT: if you change this you must also change the following #defines down to SWAP_BE_WORD_TO_HOST below to match */
/* WATCHOUT: there are a few places where the code will not work unless bwword is >= 32 bits wide */

#if (ENABLE_64_BIT_WORDS == 0)

typedef QLAC__uint32 bwword;
typedef QLAC__uint64 QLAC__bwtemp;
#define QLAC__BYTES_PER_WORD 4		/* sizeof bwword */
#define QLAC__BITS_PER_WORD 32
#define QLAC__TEMP_BITS 64
#define QLAC__HALF_TEMP_BITS 32
/* SWAP_BE_WORD_TO_HOST swaps bytes in a bwword (which is always big-endian) if necessary to match host byte order */
#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ENDSWAP_32(x)
#endif

#else

typedef QLAC__uint64 bwword;
typedef QLAC__uint64 QLAC__bwtemp;
#define QLAC__BYTES_PER_WORD 8		/* sizeof bwword */
#define QLAC__BITS_PER_WORD 64
#define QLAC__TEMP_BITS 64
#define QLAC__HALF_TEMP_BITS 32
/* SWAP_BE_WORD_TO_HOST swaps bytes in a bwword (which is always big-endian) if necessary to match host byte order */
#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ENDSWAP_64(x)
#endif

#endif

/*
 * The default capacity here doesn't matter too much.  The buffer always grows
 * to hold whatever is written to it.  Usually the encoder will stop adding at
 * a frame or metadata block, then write that out and clear the buffer for the
 * next one.
 */
static const uint32_t QLAC__BITWRITER_DEFAULT_CAPACITY = 32768u / sizeof(bwword); /* size in words */
/* When growing, increment with 1/4th at a time */
static const uint32_t QLAC__BITWRITER_DEFAULT_GROW_FRACTION = 2; /* means grow by >> 2 (1/4th) of current size */

#define QLAC__WORDS_TO_BITS(words) ((words) * QLAC__BITS_PER_WORD)
#define QLAC__TOTAL_BITS(bw) (QLAC__WORDS_TO_BITS((bw)->words) + (bw)->bits)

struct QLAC__BitWriter {
	bwword *buffer;
	bwword accum; /* accumulator; bits are right-justified; when full, accum is appended to buffer */
	uint32_t capacity; /* capacity of buffer in words */
	uint32_t words; /* # of complete words in buffer */
	uint32_t bits; /* # of used bits in accum */
};

/* * WATCHOUT: The current implementation only grows the buffer. */
#ifndef __SUNPRO_C
static
#endif
bool bitwriter_grow_(QLAC__BitWriter *bw, uint32_t bits_to_add)
{
	uint32_t new_capacity;
	bwword *new_buffer;

	QLAC__ASSERT(0 != bw);
	QLAC__ASSERT(0 != bw->buffer);

	/* calculate total words needed to store 'bits_to_add' additional bits */
	new_capacity = bw->words + ((bw->bits + bits_to_add + QLAC__BITS_PER_WORD - 1) / QLAC__BITS_PER_WORD);

	/* it's possible (due to pessimism in the growth estimation that
	 * leads to this call) that we don't actually need to grow
	 */
	if(bw->capacity >= new_capacity)
		return true;

	if(new_capacity * sizeof(bwword) > (1u << QLAC__STREAM_METADATA_LENGTH_LEN))
		/* Requested new capacity is larger than the largest possible metadata block,
		 * which is also larger than the largest sane framesize. That means something
		 * went very wrong somewhere and previous checks failed.
		 * To prevent chrashing, give up */
		return false;

	/* As reallocation can be quite expensive, grow exponentially */
	if((new_capacity - bw->capacity) < (bw->capacity >> QLAC__BITWRITER_DEFAULT_GROW_FRACTION))
		new_capacity = bw->capacity + (bw->capacity >> QLAC__BITWRITER_DEFAULT_GROW_FRACTION);

	/* make sure we got everything right */
	QLAC__ASSERT(new_capacity > bw->capacity);
	QLAC__ASSERT(new_capacity >= bw->words + ((bw->bits + bits_to_add + QLAC__BITS_PER_WORD - 1) / QLAC__BITS_PER_WORD));

	new_buffer = safe_realloc_nofree_mul_2op_(bw->buffer, sizeof(bwword), /*times*/new_capacity);
	if(new_buffer == 0)
		return false;
	bw->buffer = new_buffer;
	bw->capacity = new_capacity;
	return true;
}


/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

QLAC__BitWriter *QLAC__bitwriter_new(void)
{
	QLAC__BitWriter *bw = calloc(1, sizeof(QLAC__BitWriter));
	/* note that calloc() sets all members to 0 for us */
	return bw;
}

void QLAC__bitwriter_delete(QLAC__BitWriter *bw)
{
	QLAC__ASSERT(0 != bw);

	QLAC__bitwriter_free(bw);
	free(bw);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

bool QLAC__bitwriter_init(QLAC__BitWriter *bw)
{
	QLAC__ASSERT(0 != bw);

	bw->words = bw->bits = 0;
	bw->capacity = QLAC__BITWRITER_DEFAULT_CAPACITY;
	bw->buffer = malloc(sizeof(bwword) * bw->capacity);
	if(bw->buffer == 0)
		return false;

	return true;
}

void QLAC__bitwriter_free(QLAC__BitWriter *bw)
{
	QLAC__ASSERT(0 != bw);

	if(0 != bw->buffer)
		free(bw->buffer);
	bw->buffer = 0;
	bw->capacity = 0;
	bw->words = bw->bits = 0;
}

void QLAC__bitwriter_clear(QLAC__BitWriter *bw)
{
	bw->words = bw->bits = 0;
}


bool QLAC__bitwriter_is_byte_aligned(const QLAC__BitWriter *bw)
{
	return ((bw->bits & 7) == 0);
}

uint32_t QLAC__bitwriter_get_input_bits_unconsumed(const QLAC__BitWriter *bw)
{
	return QLAC__TOTAL_BITS(bw);
}

bool QLAC__bitwriter_get_buffer(QLAC__BitWriter *bw, const QLAC__byte **buffer, size_t *bytes)
{
	QLAC__ASSERT((bw->bits & 7) == 0);
	/* double protection */
	if(bw->bits & 7)
		return false;
	/* if we have bits in the accumulator we have to flush those to the buffer first */
	if(bw->bits) {
		QLAC__ASSERT(bw->words <= bw->capacity);
		if(bw->words == bw->capacity && !bitwriter_grow_(bw, QLAC__BITS_PER_WORD))
			return false;
		/* append bits as complete word to buffer, but don't change bw->accum or bw->bits */
		bw->buffer[bw->words] = SWAP_BE_WORD_TO_HOST(bw->accum << (QLAC__BITS_PER_WORD-bw->bits));
	}
	/* now we can just return what we have */
	*buffer = (QLAC__byte*)bw->buffer;
	*bytes = (QLAC__BYTES_PER_WORD * bw->words) + (bw->bits >> 3);
	return true;
}


inline bool QLAC__bitwriter_write_zeroes(QLAC__BitWriter *bw, uint32_t bits)
{
	uint32_t n;

	QLAC__ASSERT(0 != bw);
	QLAC__ASSERT(0 != bw->buffer);

	if(bits == 0)
		return true;
	/* slightly pessimistic size check but faster than "<= bw->words + (bw->bits+bits+QLAC__BITS_PER_WORD-1)/QLAC__BITS_PER_WORD" */
	if(bw->capacity <= bw->words + bits && !bitwriter_grow_(bw, bits))
		return false;
	/* first part gets to word alignment */
	if(bw->bits) {
		n = QLAC_min(QLAC__BITS_PER_WORD - bw->bits, bits);
		bw->accum <<= n;
		bits -= n;
		bw->bits += n;
		if(bw->bits == QLAC__BITS_PER_WORD) {
			bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
			bw->bits = 0;
		}
		else
			return true;
	}
	/* do whole words */
	while(bits >= QLAC__BITS_PER_WORD) {
		bw->buffer[bw->words++] = 0;
		bits -= QLAC__BITS_PER_WORD;
	}
	/* do any leftovers */
	if(bits > 0) {
		bw->accum = 0;
		bw->bits = bits;
	}
	return true;
}

static inline bool QLAC__bitwriter_write_raw_uint32_nocheck(QLAC__BitWriter *bw, QLAC__uint32 val, uint32_t bits)
{
	register uint32_t left;

	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	QLAC__ASSERT(QLAC__BITS_PER_WORD >= 32);

	if(bw == 0 || bw->buffer == 0)
		return false;

	if (bits > 32)
		return false;

	if(bits == 0)
		return true;

	QLAC__ASSERT((bits == 32) || (val>>bits == 0));

	/* slightly pessimistic size check but faster than "<= bw->words + (bw->bits+bits+QLAC__BITS_PER_WORD-1)/QLAC__BITS_PER_WORD" */
	if(bw->capacity <= bw->words + bits && !bitwriter_grow_(bw, bits))
		return false;

	left = QLAC__BITS_PER_WORD - bw->bits;
	if(bits < left) {
		bw->accum <<= bits;
		bw->accum |= val;
		bw->bits += bits;
	}
	else if(bw->bits) { /* WATCHOUT: if bw->bits == 0, left==QLAC__BITS_PER_WORD and bw->accum<<=left is a NOP instead of setting to 0 */
		bw->accum <<= left;
		bw->accum |= val >> (bw->bits = bits - left);
		bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);
		bw->accum = val; /* unused top bits can contain garbage */
	}
	else { /* at this point bits == QLAC__BITS_PER_WORD == 32  and  bw->bits == 0 */
		bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST((bwword)val);
	}

	return true;
}

inline bool QLAC__bitwriter_write_raw_uint32(QLAC__BitWriter *bw, QLAC__uint32 val, uint32_t bits)
{
	/* check that unused bits are unset */
	if((bits < 32) && (val>>bits != 0))
		return false;

	return QLAC__bitwriter_write_raw_uint32_nocheck(bw, val, bits);
}

inline bool QLAC__bitwriter_write_raw_int32(QLAC__BitWriter *bw, QLAC__int32 val, uint32_t bits)
{
	/* zero-out unused bits */
	if(bits < 32)
		val &= (~(0xffffffff << bits));

	return QLAC__bitwriter_write_raw_uint32_nocheck(bw, (QLAC__uint32)val, bits);
}

inline bool QLAC__bitwriter_write_raw_uint64(QLAC__BitWriter *bw, QLAC__uint64 val, uint32_t bits)
{
	/* this could be a little faster but it's not used for much */
	if(bits > 32) {
		return
			QLAC__bitwriter_write_raw_uint32(bw, (QLAC__uint32)(val>>32), bits-32) &&
			QLAC__bitwriter_write_raw_uint32_nocheck(bw, (QLAC__uint32)val, 32);
	}
	else
		return QLAC__bitwriter_write_raw_uint32(bw, (QLAC__uint32)val, bits);
}

inline bool QLAC__bitwriter_write_raw_int64(QLAC__BitWriter *bw, QLAC__int64 val, uint32_t bits)
{
	QLAC__uint64 uval = val;
	/* zero-out unused bits */
	if(bits < 64)
		uval &= (~(UINT64_MAX << bits));
	return QLAC__bitwriter_write_raw_uint64(bw, uval, bits);
}

inline bool QLAC__bitwriter_write_raw_uint32_little_endian(QLAC__BitWriter *bw, QLAC__uint32 val)
{
	/* this doesn't need to be that fast as currently it is only used for vorbis comments */

	if(!QLAC__bitwriter_write_raw_uint32_nocheck(bw, val & 0xff, 8))
		return false;
	if(!QLAC__bitwriter_write_raw_uint32_nocheck(bw, (val>>8) & 0xff, 8))
		return false;
	if(!QLAC__bitwriter_write_raw_uint32_nocheck(bw, (val>>16) & 0xff, 8))
		return false;
	if(!QLAC__bitwriter_write_raw_uint32_nocheck(bw, val>>24, 8))
		return false;

	return true;
}

inline bool QLAC__bitwriter_write_byte_block(QLAC__BitWriter *bw, const QLAC__byte vals[], uint32_t nvals)
{
	uint32_t i;

	/* grow capacity upfront to prevent constant reallocation during writes */
	if(bw->capacity <= bw->words + nvals / (QLAC__BITS_PER_WORD / 8) + 1 && !bitwriter_grow_(bw, nvals * 8))
		return false;

	/* this could be faster but currently we don't need it to be since it's only used for writing metadata */
	for(i = 0; i < nvals; i++) {
		if(!QLAC__bitwriter_write_raw_uint32_nocheck(bw, (QLAC__uint32)(vals[i]), 8))
			return false;
	}

	return true;
}

bool QLAC__bitwriter_write_unary_unsigned(QLAC__BitWriter *bw, uint32_t val)
{
	if(val < 32)
		return QLAC__bitwriter_write_raw_uint32_nocheck(bw, 1, ++val);
	else
		return
			QLAC__bitwriter_write_zeroes(bw, val) &&
			QLAC__bitwriter_write_raw_uint32_nocheck(bw, 1, 1);
}

#if 0 /* UNUSED */
uint32_t QLAC__bitwriter_rice_bits(QLAC__int32 val, uint32_t parameter)
{
	QLAC__uint32 uval;

	QLAC__ASSERT(parameter < 32);

	/* fold signed to uint32_t; actual formula is: negative(v)? -2v-1 : 2v */
	uval = val;
	uval <<= 1;
	uval ^= (val>>31);

	return 1 + parameter + (uval >> parameter);
}

uint32_t QLAC__bitwriter_golomb_bits_signed(int val, uint32_t parameter)
{
	uint32_t bits, msbs, uval;
	uint32_t k;

	QLAC__ASSERT(parameter > 0);

	/* fold signed to uint32_t */
	if(val < 0)
		uval = (uint32_t)(((-(++val)) << 1) + 1);
	else
		uval = (uint32_t)(val << 1);

	k = QLAC__bitmath_ilog2(parameter);
	if(parameter == 1u<<k) {
		QLAC__ASSERT(k <= 30);

		msbs = uval >> k;
		bits = 1 + k + msbs;
	}
	else {
		uint32_t q, r, d;

		d = (1 << (k+1)) - parameter;
		q = uval / parameter;
		r = uval - (q * parameter);

		bits = 1 + q + k;
		if(r >= d)
			bits++;
	}
	return bits;
}

uint32_t QLAC__bitwriter_golomb_bits_unsigned(uint32_t uval, uint32_t parameter)
{
	uint32_t bits, msbs;
	uint32_t k;

	QLAC__ASSERT(parameter > 0);

	k = QLAC__bitmath_ilog2(parameter);
	if(parameter == 1u<<k) {
		QLAC__ASSERT(k <= 30);

		msbs = uval >> k;
		bits = 1 + k + msbs;
	}
	else {
		uint32_t q, r, d;

		d = (1 << (k+1)) - parameter;
		q = uval / parameter;
		r = uval - (q * parameter);

		bits = 1 + q + k;
		if(r >= d)
			bits++;
	}
	return bits;
}

bool QLAC__bitwriter_write_rice_signed(QLAC__BitWriter *bw, QLAC__int32 val, uint32_t parameter)
{
	uint32_t total_bits, interesting_bits, msbs;
	QLAC__uint32 uval, pattern;

	QLAC__ASSERT(0 != bw);
	QLAC__ASSERT(0 != bw->buffer);
	QLAC__ASSERT(parameter < 32);

	/* fold signed to uint32_t; actual formula is: negative(v)? -2v-1 : 2v */
	uval = val;
	uval <<= 1;
	uval ^= (val>>31);

	msbs = uval >> parameter;
	interesting_bits = 1 + parameter;
	total_bits = interesting_bits + msbs;
	pattern = 1 << parameter; /* the unary end bit */
	pattern |= (uval & ((1<<parameter)-1)); /* the binary LSBs */

	if(total_bits <= 32)
		return QLAC__bitwriter_write_raw_uint32(bw, pattern, total_bits);
	else
		return
			QLAC__bitwriter_write_zeroes(bw, msbs) && /* write the unary MSBs */
			QLAC__bitwriter_write_raw_uint32(bw, pattern, interesting_bits); /* write the unary end bit and binary LSBs */
}
#endif /* UNUSED */

#if (ENABLE_64_BIT_WORDS == 0)

#define WIDE_ACCUM_TO_BW {  \
	bw->accum = wide_accum >> QLAC__HALF_TEMP_BITS;  \
	bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum); \
	wide_accum <<= QLAC__HALF_TEMP_BITS;  \
	bitpointer += QLAC__HALF_TEMP_BITS;  \
}

#else

#define WIDE_ACCUM_TO_BW {  \
	QLAC__ASSERT(bw->bits % QLAC__HALF_TEMP_BITS == 0);  \
	if(bw->bits == 0) {  \
		bw->accum = wide_accum >> QLAC__HALF_TEMP_BITS;  \
		wide_accum <<= QLAC__HALF_TEMP_BITS;  \
		bw->bits = QLAC__HALF_TEMP_BITS;  \
	}  \
	else {  \
		bw->accum <<= QLAC__HALF_TEMP_BITS;  \
		bw->accum += wide_accum >> QLAC__HALF_TEMP_BITS;  \
		bw->buffer[bw->words++] = SWAP_BE_WORD_TO_HOST(bw->accum);  \
		wide_accum <<= QLAC__HALF_TEMP_BITS;  \
		bw->bits = 0;  \
	}  \
	bitpointer += QLAC__HALF_TEMP_BITS;  \
}

#endif

bool QLAC__bitwriter_write_rice_signed_block(QLAC__BitWriter *bw, const QLAC__int32 *vals, uint32_t nvals, uint32_t parameter)
{
	const QLAC__uint32 mask1 = (QLAC__uint32)0xffffffff << parameter; /* we val|=mask1 to set the stop bit above it... */
	const QLAC__uint32 mask2 = (QLAC__uint32)0xffffffff >> (31-parameter); /* ...then mask off the bits above the stop bit with val&=mask2 */
	QLAC__uint32 uval;
	const uint32_t lsbits = 1 + parameter;
	uint32_t msbits, total_bits;
	QLAC__bwtemp wide_accum = 0;
	QLAC__uint32 bitpointer = QLAC__TEMP_BITS;

	QLAC__ASSERT(0 != bw);
	QLAC__ASSERT(0 != bw->buffer);
	QLAC__ASSERT(parameter < 31);
	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	QLAC__ASSERT(QLAC__BITS_PER_WORD >= 32);
#if (ENABLE_64_BIT_WORDS == 0)
	if(bw->bits > 0) {
		bitpointer -= bw->bits;
		wide_accum = (QLAC__bwtemp)(bw->accum) << bitpointer;
		bw->bits = 0;
	}
#else
	if(bw->bits > 0 && bw->bits < QLAC__HALF_TEMP_BITS) {
		bitpointer -= bw->bits;
		wide_accum = bw->accum << bitpointer;
		bw->bits = 0;
	}
	else if(bw->bits > QLAC__HALF_TEMP_BITS) {
		bitpointer -= (bw->bits - QLAC__HALF_TEMP_BITS);
		wide_accum = bw->accum << bitpointer;
		bw->accum >>= (bw->bits - QLAC__HALF_TEMP_BITS);
		bw->bits = QLAC__HALF_TEMP_BITS;
	}
#endif

	/* Reserve one QLAC__TEMP_BITS per symbol, so checks for space are only necessary when very large symbols are encountered
	 * this might be considered wasteful, but is only at most 8kB more than necessary for a blocksize of 4096 */
	if(bw->capacity * QLAC__BITS_PER_WORD <= bw->words * QLAC__BITS_PER_WORD + nvals * QLAC__TEMP_BITS + bw->bits && !bitwriter_grow_(bw, nvals * QLAC__TEMP_BITS))
		return false;

	while(nvals) {
		/* fold signed to uint32_t; actual formula is: negative(v)? -2v-1 : 2v */
		uval = *vals;
		uval <<= 1;
		uval ^= (*vals>>31);

		msbits = uval >> parameter;
		total_bits = lsbits + msbits;

		uval |= mask1; /* set stop bit */
		uval &= mask2; /* mask off unused top bits */


		if(total_bits <= bitpointer) {
			/* There is room enough to store the symbol whole at once */
			wide_accum |= (QLAC__bwtemp)(uval) << (bitpointer - total_bits);
			bitpointer -= total_bits;
			if(bitpointer <= QLAC__HALF_TEMP_BITS) {
				/* A word is finished, copy the upper 32 bits of the wide_accum */
				WIDE_ACCUM_TO_BW
			}
		}
		else {
			/* The symbol needs to be split. This code isn't used often */
			/* First check for space in the bitwriter */
			if(total_bits > QLAC__TEMP_BITS) {
				QLAC__uint32 oversize_in_bits = total_bits - QLAC__TEMP_BITS;
				QLAC__uint32 capacity_needed = bw->words * QLAC__BITS_PER_WORD + bw->bits + nvals * QLAC__TEMP_BITS + oversize_in_bits;
				if(bw->capacity * QLAC__BITS_PER_WORD <= capacity_needed && !bitwriter_grow_(bw, nvals * QLAC__TEMP_BITS + oversize_in_bits))
					return false;
			}
			if(msbits > bitpointer) {
				/* We have a lot of 0 bits to write, first align with bitwriter word */
				msbits -= bitpointer - QLAC__HALF_TEMP_BITS;
				bitpointer = QLAC__HALF_TEMP_BITS;
				WIDE_ACCUM_TO_BW
				while(msbits > bitpointer) {
					/* As the accumulator is already zero, we only need to
					 * assign zeroes to the bitbuffer */
					WIDE_ACCUM_TO_BW
					bitpointer -= QLAC__HALF_TEMP_BITS;
					msbits -= QLAC__HALF_TEMP_BITS;
				}
				/* The remaining bits are zero, and the accumulator already is zero,
				 * so just subtract the number of bits from bitpointer. When storing,
				 * we can also just store 0 */
				bitpointer -= msbits;
				if(bitpointer <= QLAC__HALF_TEMP_BITS)
					WIDE_ACCUM_TO_BW
			}
			else {
				bitpointer -= msbits;
				if(bitpointer <= QLAC__HALF_TEMP_BITS)
					WIDE_ACCUM_TO_BW
			}
			/* The lsbs + stop bit always fit 32 bit, so this code mirrors the code above */
                        wide_accum |= (QLAC__bwtemp)(uval) << (bitpointer - lsbits);
                        bitpointer -= lsbits;
                        if(bitpointer <= QLAC__HALF_TEMP_BITS) {
                                /* A word is finished, copy the upper 32 bits of the wide_accum */
                                WIDE_ACCUM_TO_BW
                        }
		}
		vals++;
		nvals--;
	}
	/* Now fixup remainder of wide_accum */
#if (ENABLE_64_BIT_WORDS == 0)
	if(bitpointer < QLAC__TEMP_BITS) {
		bw->accum = wide_accum >> bitpointer;
		bw->bits = QLAC__TEMP_BITS - bitpointer;
	}
#else
	if(bitpointer < QLAC__TEMP_BITS) {
		if(bw->bits == 0) {
			bw->accum = wide_accum >> bitpointer;
			bw->bits = QLAC__TEMP_BITS - bitpointer;
		}
		else if (bw->bits == QLAC__HALF_TEMP_BITS) {
			bw->accum <<= QLAC__TEMP_BITS - bitpointer;
			bw->accum |= (wide_accum >> bitpointer);
			bw->bits = QLAC__HALF_TEMP_BITS + QLAC__TEMP_BITS - bitpointer;
		}
		else {
			QLAC__ASSERT(0);
		}
	}
#endif


	return true;
}

bool QLAC__bitwriter_write_utf8_uint32(QLAC__BitWriter *bw, QLAC__uint32 val)
{
	bool ok = 1;

	QLAC__ASSERT(0 != bw);
	QLAC__ASSERT(0 != bw->buffer);

	if((val & 0x80000000) != 0) /* this version only handles 31 bits */
		return false;

	if(val < 0x80) {
		return QLAC__bitwriter_write_raw_uint32_nocheck(bw, val, 8);
	}
	else if(val < 0x800) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xC0 | (val>>6), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (val&0x3F), 8);
	}
	else if(val < 0x10000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xE0 | (val>>12), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (val&0x3F), 8);
	}
	else if(val < 0x200000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xF0 | (val>>18), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (val&0x3F), 8);
	}
	else if(val < 0x4000000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xF8 | (val>>24), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>18)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (val&0x3F), 8);
	}
	else {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xFC | (val>>30), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>24)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>18)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | ((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (val&0x3F), 8);
	}

	return ok;
}

bool QLAC__bitwriter_write_utf8_uint64(QLAC__BitWriter *bw, QLAC__uint64 val)
{
	bool ok = 1;

	QLAC__ASSERT(0 != bw);
	QLAC__ASSERT(0 != bw->buffer);

	if((val & QLAC__U64L(0xFFFFFFF000000000)) != 0) /* this version only handles 36 bits */
		return false;

	if(val < 0x80) {
		return QLAC__bitwriter_write_raw_uint32_nocheck(bw, (QLAC__uint32)val, 8);
	}
	else if(val < 0x800) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xC0 | (QLAC__uint32)(val>>6), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x10000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xE0 | (QLAC__uint32)(val>>12), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x200000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xF0 | (QLAC__uint32)(val>>18), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x4000000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xF8 | (QLAC__uint32)(val>>24), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>18)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)(val&0x3F), 8);
	}
	else if(val < 0x80000000) {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xFC | (QLAC__uint32)(val>>30), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>24)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>18)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)(val&0x3F), 8);
	}
	else {
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0xFE, 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>30)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>24)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>18)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>12)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)((val>>6)&0x3F), 8);
		ok &= QLAC__bitwriter_write_raw_uint32_nocheck(bw, 0x80 | (QLAC__uint32)(val&0x3F), 8);
	}

	return ok;
}

bool QLAC__bitwriter_zero_pad_to_byte_boundary(QLAC__BitWriter *bw)
{
	/* 0-pad to byte boundary */
	if(bw->bits & 7u)
		return QLAC__bitwriter_write_zeroes(bw, 8 - (bw->bits & 7u));
	else
		return true;
}

/* These functions are declared inline in this file but are also callable as
 * externs from elsewhere.
 * According to the C99 spec, section 6.7.4, simply providing a function
 * prototype in a header file without 'inline' and making the function inline
 * in this file should be sufficient.
 * Unfortunately, the Microsoft VS compiler doesn't pick them up externally. To
 * fix that we add extern declarations here.
 */
extern bool QLAC__bitwriter_write_zeroes(QLAC__BitWriter *bw, uint32_t bits);
extern bool QLAC__bitwriter_write_raw_uint32(QLAC__BitWriter *bw, QLAC__uint32 val, uint32_t bits);
extern bool QLAC__bitwriter_write_raw_int32(QLAC__BitWriter *bw, QLAC__int32 val, uint32_t bits);
extern bool QLAC__bitwriter_write_raw_uint64(QLAC__BitWriter *bw, QLAC__uint64 val, uint32_t bits);
extern bool QLAC__bitwriter_write_raw_int64(QLAC__BitWriter *bw, QLAC__int64 val, uint32_t bits);
extern bool QLAC__bitwriter_write_raw_uint32_little_endian(QLAC__BitWriter *bw, QLAC__uint32 val);
extern bool QLAC__bitwriter_write_byte_block(QLAC__BitWriter *bw, const QLAC__byte vals[], uint32_t nvals);
