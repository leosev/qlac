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
#include "private/bitmath.h"
#include "private/bitreader.h"
#include "private/cpu.h"
#include "private/macros.h"
#include "QLAC/assert.h"
#include "share/compat.h"
#include "share/endswap.h"

/* Things should be fastest when this matches the machine word size */
/* WATCHOUT: if you change this you must also change the following #defines down to COUNT_ZERO_MSBS2 below to match */
/* WATCHOUT: there are a few places where the code will not work unless brword is >= 32 bits wide */
/*           also, some sections currently only have fast versions for 4 or 8 bytes per word */

#if (ENABLE_64_BIT_WORDS == 0)

typedef QLAC__uint32 brword;
#define QLAC__BYTES_PER_WORD 4		/* sizeof brword */
#define QLAC__BITS_PER_WORD 32
#define QLAC__WORD_ALL_ONES ((QLAC__uint32)0xffffffff)
/* SWAP_BE_WORD_TO_HOST swaps bytes in a brword (which is always big-endian) if necessary to match host byte order */
#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ENDSWAP_32(x)
#endif
/* counts the # of zero MSBs in a word */
#define COUNT_ZERO_MSBS(word) QLAC__clz_uint32(word)
#define COUNT_ZERO_MSBS2(word) QLAC__clz2_uint32(word)

#else

typedef QLAC__uint64 brword;
#define QLAC__BYTES_PER_WORD 8		/* sizeof brword */
#define QLAC__BITS_PER_WORD 64
#define QLAC__WORD_ALL_ONES ((QLAC__uint64)QLAC__U64L(0xffffffffffffffff))
/* SWAP_BE_WORD_TO_HOST swaps bytes in a brword (which is always big-endian) if necessary to match host byte order */
#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ENDSWAP_64(x)
#endif
/* counts the # of zero MSBs in a word */
#define COUNT_ZERO_MSBS(word) QLAC__clz_uint64(word)
#define COUNT_ZERO_MSBS2(word) QLAC__clz2_uint64(word)

#endif

/*
 * This should be at least twice as large as the largest number of words
 * required to represent any 'number' (in any encoding) you are going to
 * read.  With FLAC this is on the order of maybe a few hundred bits.
 * If the buffer is smaller than that, the decoder won't be able to read
 * in a whole number that is in a variable length encoding (e.g. Rice).
 * But to be practical it should be at least 1K bytes.
 *
 * Increase this number to decrease the number of read callbacks, at the
 * expense of using more memory.  Or decrease for the reverse effect,
 * keeping in mind the limit from the first paragraph.  The optimal size
 * also depends on the CPU cache size and other factors; some twiddling
 * may be necessary to squeeze out the best performance.
 */
static const uint32_t QLAC__BITREADER_DEFAULT_CAPACITY = 65536u / QLAC__BITS_PER_WORD; /* in words */

struct QLAC__BitReader {
	/* any partially-consumed word at the head will stay right-justified as bits are consumed from the left */
	/* any incomplete word at the tail will be left-justified, and bytes from the read callback are added on the right */
	brword *buffer;
	uint32_t capacity; /* in words */
	uint32_t words; /* # of completed words in buffer */
	uint32_t bytes; /* # of bytes in incomplete word at buffer[words] */
	uint32_t consumed_words; /* #words ... */
	uint32_t consumed_bits; /* ... + (#bits of head word) already consumed from the front of buffer */
	bool read_limit_set; /* whether reads are limited */
	uint32_t read_limit; /* the remaining size of what can be read */
	uint32_t last_seen_framesync; /* the location of the last seen framesync, if it is in the buffer, in bits from front of buffer */
	QLAC__BitReaderReadCallback read_callback;
	void *client_data;
};

static bool bitreader_read_from_client_(QLAC__BitReader *br)
{
	uint32_t start, end;
	size_t bytes;
	QLAC__byte *target;
#if WORDS_BIGENDIAN
#else
	brword preswap_backup;
#endif

	/* first shift the unconsumed buffer data toward the front as much as possible */
	if(br->consumed_words > 0) {
		/* invalidate last seen framesync */
		br->last_seen_framesync = -1;

		start = br->consumed_words;
		end = br->words + (br->bytes? 1:0);
		memmove(br->buffer, br->buffer+start, QLAC__BYTES_PER_WORD * (end - start));

		br->words -= start;
		br->consumed_words = 0;
	}

	/*
	 * set the target for reading, taking into account word alignment and endianness
	 */
	bytes = (br->capacity - br->words) * QLAC__BYTES_PER_WORD - br->bytes;
	if(bytes == 0)
		return false; /* no space left, buffer is too small; see note for QLAC__BITREADER_DEFAULT_CAPACITY  */
	target = ((QLAC__byte*)(br->buffer+br->words)) + br->bytes;

	/* before reading, if the existing reader looks like this (say brword is 32 bits wide)
	 *   bitstream :  11 22 33 44 55            br->words=1 br->bytes=1 (partial tail word is left-justified)
	 *   buffer[BE]:  11 22 33 44 55 ?? ?? ??   (shown laid out as bytes sequentially in memory)
	 *   buffer[LE]:  44 33 22 11 ?? ?? ?? 55   (?? being don't-care)
	 *                               ^^-------target, bytes=3
	 * on LE machines, have to byteswap the odd tail word so nothing is
	 * overwritten:
	 */
#if WORDS_BIGENDIAN
#else
	preswap_backup = br->buffer[br->words];
	if(br->bytes)
		br->buffer[br->words] = SWAP_BE_WORD_TO_HOST(br->buffer[br->words]);
#endif

	/* now it looks like:
	 *   bitstream :  11 22 33 44 55            br->words=1 br->bytes=1
	 *   buffer[BE]:  11 22 33 44 55 ?? ?? ??
	 *   buffer[LE]:  44 33 22 11 55 ?? ?? ??
	 *                               ^^-------target, bytes=3
	 */

	/* read in the data; note that the callback may return a smaller number of bytes */
	if(!br->read_callback(target, &bytes, br->client_data)){
		/* Despite the read callback failing, the data in the target
		 * might be used later, when the buffer is rewound. Therefore
		 * we revert the swap that was just done */
#if WORDS_BIGENDIAN
#else
		br->buffer[br->words] = preswap_backup;
#endif
		return false;
	}

	/* after reading bytes 66 77 88 99 AA BB CC DD EE FF from the client:
	 *   bitstream :  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
	 *   buffer[BE]:  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF ??
	 *   buffer[LE]:  44 33 22 11 55 66 77 88 99 AA BB CC DD EE FF ??
	 * now have to byteswap on LE machines:
	 */
#if WORDS_BIGENDIAN
#else
	end = (br->words*QLAC__BYTES_PER_WORD + br->bytes + (uint32_t)bytes + (QLAC__BYTES_PER_WORD-1)) / QLAC__BYTES_PER_WORD;
	for(start = br->words; start < end; start++)
		br->buffer[start] = SWAP_BE_WORD_TO_HOST(br->buffer[start]);
#endif

	/* now it looks like:
	 *   bitstream :  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
	 *   buffer[BE]:  11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF ??
	 *   buffer[LE]:  44 33 22 11 88 77 66 55 CC BB AA 99 ?? FF EE DD
	 * finally we'll update the reader values:
	 */
	end = br->words*QLAC__BYTES_PER_WORD + br->bytes + (uint32_t)bytes;
	br->words = end / QLAC__BYTES_PER_WORD;
	br->bytes = end % QLAC__BYTES_PER_WORD;

	return true;
}

/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

QLAC__BitReader *QLAC__bitreader_new(void)
{
	QLAC__BitReader *br = calloc(1, sizeof(QLAC__BitReader));

	/* calloc() implies:
		memset(br, 0, sizeof(QLAC__BitReader));
		br->buffer = 0;
		br->capacity = 0;
		br->words = br->bytes = 0;
		br->consumed_words = br->consumed_bits = 0;
		br->read_callback = 0;
		br->client_data = 0;
	*/
	return br;
}

void QLAC__bitreader_delete(QLAC__BitReader *br)
{
	QLAC__ASSERT(0 != br);

	QLAC__bitreader_free(br);
	free(br);
}

/***********************************************************************
 *
 * Public class methods
 *
 ***********************************************************************/

bool QLAC__bitreader_init(QLAC__BitReader *br, QLAC__BitReaderReadCallback rcb, void *cd)
{
	QLAC__ASSERT(0 != br);

	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->capacity = QLAC__BITREADER_DEFAULT_CAPACITY;
	br->buffer = malloc(sizeof(brword) * br->capacity);
	if(br->buffer == 0)
		return false;
	br->read_callback = rcb;
	br->client_data = cd;
	br->read_limit_set = false;
	br->read_limit = -1;
	br->last_seen_framesync = -1;

	return true;
}

void QLAC__bitreader_free(QLAC__BitReader *br)
{
	QLAC__ASSERT(0 != br);

	if(0 != br->buffer)
		free(br->buffer);
	br->buffer = 0;
	br->capacity = 0;
	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->read_callback = 0;
	br->client_data = 0;
	br->read_limit_set = false;
	br->read_limit = -1;
	br->last_seen_framesync = -1;
}

bool QLAC__bitreader_clear(QLAC__BitReader *br)
{
	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->read_limit_set = false;
	br->read_limit = -1;
	br->last_seen_framesync = -1;
	return true;
}

void QLAC__bitreader_set_framesync_location(QLAC__BitReader *br)
{
	br->last_seen_framesync = br->consumed_words * QLAC__BYTES_PER_WORD + br->consumed_bits / 8;
}

bool QLAC__bitreader_rewind_to_after_last_seen_framesync(QLAC__BitReader *br)
{
	if(br->last_seen_framesync == (uint32_t)-1) {
		br->consumed_words = br->consumed_bits = 0;
		return false;
	}
	else {
		br->consumed_words = (br->last_seen_framesync + 1) / QLAC__BYTES_PER_WORD;
		br->consumed_bits  = ((br->last_seen_framesync + 1) % QLAC__BYTES_PER_WORD) * 8;
		return true;
	}
}

inline bool QLAC__bitreader_is_consumed_byte_aligned(const QLAC__BitReader *br)
{
	return ((br->consumed_bits & 7) == 0);
}

inline uint32_t QLAC__bitreader_bits_left_for_byte_alignment(const QLAC__BitReader *br)
{
	return 8 - (br->consumed_bits & 7);
}

inline uint32_t QLAC__bitreader_get_input_bits_unconsumed(const QLAC__BitReader *br)
{
	return (br->words-br->consumed_words)*QLAC__BITS_PER_WORD + br->bytes*8 - br->consumed_bits;
}

void QLAC__bitreader_set_limit(QLAC__BitReader *br, uint32_t limit)
{
	br->read_limit = limit;
	br->read_limit_set = true;
}

void QLAC__bitreader_remove_limit(QLAC__BitReader *br)
{
	br->read_limit_set = false;
	br->read_limit = -1;
}

uint32_t QLAC__bitreader_limit_remaining(QLAC__BitReader *br)
{
	QLAC__ASSERT(br->read_limit_set);
	return br->read_limit;
}
void QLAC__bitreader_limit_invalidate(QLAC__BitReader *br)
{
	br->read_limit = -1;
}

bool QLAC__bitreader_read_raw_uint32(QLAC__BitReader *br, QLAC__uint32 *val, uint32_t bits)
{
	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);

	QLAC__ASSERT(bits <= 32);
	QLAC__ASSERT((br->capacity*QLAC__BITS_PER_WORD) * 2 >= bits);
	QLAC__ASSERT(br->consumed_words <= br->words);

	/* WATCHOUT: code does not work with <32bit words; we can make things much faster with this assertion */
	QLAC__ASSERT(QLAC__BITS_PER_WORD >= 32);

	if(bits == 0) { /* OPT: investigate if this can ever happen, maybe change to assertion */
		*val = 0;
		return true;
	}

	if(br->read_limit_set && br->read_limit < (uint32_t)-1){
		if(br->read_limit < bits) {
			br->read_limit = -1;
			return false;
		}
		else
			br->read_limit -= bits;
	}

	while((br->words-br->consumed_words)*QLAC__BITS_PER_WORD + br->bytes*8 - br->consumed_bits < bits) {
		if(!bitreader_read_from_client_(br))
			return false;
	}
	if(br->consumed_words < br->words) { /* if we've not consumed up to a partial tail word... */
		/* OPT: taking out the consumed_bits==0 "else" case below might make things faster if less code allows the compiler to inline this function */
		if(br->consumed_bits) {
			/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
			const uint32_t n = QLAC__BITS_PER_WORD - br->consumed_bits;
			const brword word = br->buffer[br->consumed_words];
			const brword mask = br->consumed_bits < QLAC__BITS_PER_WORD ? QLAC__WORD_ALL_ONES >> br->consumed_bits : 0;
			if(bits < n) {
				uint32_t shift = n - bits;
				*val = shift < QLAC__BITS_PER_WORD ? (QLAC__uint32)((word & mask) >> shift) : 0; /* The result has <= 32 non-zero bits */
				br->consumed_bits += bits;
				return true;
			}
			/* (QLAC__BITS_PER_WORD - br->consumed_bits <= bits) ==> (QLAC__WORD_ALL_ONES >> br->consumed_bits) has no more than 'bits' non-zero bits */
			*val = (QLAC__uint32)(word & mask);
			bits -= n;
			br->consumed_words++;
			br->consumed_bits = 0;
			if(bits) { /* if there are still bits left to read, there have to be less than 32 so they will all be in the next word */
				uint32_t shift = QLAC__BITS_PER_WORD - bits;
				*val = bits < 32 ? *val << bits : 0;
				*val |= shift < QLAC__BITS_PER_WORD ? (QLAC__uint32)(br->buffer[br->consumed_words] >> shift) : 0;
				br->consumed_bits = bits;
			}
			return true;
		}
		else { /* br->consumed_bits == 0 */
			const brword word = br->buffer[br->consumed_words];
			if(bits < QLAC__BITS_PER_WORD) {
				*val = (QLAC__uint32)(word >> (QLAC__BITS_PER_WORD-bits));
				br->consumed_bits = bits;
				return true;
			}
			/* at this point bits == QLAC__BITS_PER_WORD == 32; because of previous assertions, it can't be larger */
			*val = (QLAC__uint32)word;
			br->consumed_words++;
			return true;
		}
	}
	else {
		/* in this case we're starting our read at a partial tail word;
		 * the reader has guaranteed that we have at least 'bits' bits
		 * available to read, which makes this case simpler.
		 */
		/* OPT: taking out the consumed_bits==0 "else" case below might make things faster if less code allows the compiler to inline this function */
		if(br->consumed_bits) {
			/* this also works when consumed_bits==0, it's just a little slower than necessary for that case */
			QLAC__ASSERT(br->consumed_bits + bits <= br->bytes*8);
			*val = (QLAC__uint32)((br->buffer[br->consumed_words] & (QLAC__WORD_ALL_ONES >> br->consumed_bits)) >> (QLAC__BITS_PER_WORD-br->consumed_bits-bits));
			br->consumed_bits += bits;
			return true;
		}
		else {
			*val = (QLAC__uint32)(br->buffer[br->consumed_words] >> (QLAC__BITS_PER_WORD-bits));
			br->consumed_bits += bits;
			return true;
		}
	}
}

bool QLAC__bitreader_read_raw_int32(QLAC__BitReader *br, QLAC__int32 *val, uint32_t bits)
{
	QLAC__uint32 uval, mask;
	/* OPT: inline raw uint32 code here, or make into a macro if possible in the .h file */
	if (bits < 1 || ! QLAC__bitreader_read_raw_uint32(br, &uval, bits))
		return false;
	/* sign-extend *val assuming it is currently bits wide. */
	/* From: https://graphics.stanford.edu/~seander/bithacks.html#FixedSignExtend */
	mask = bits >= 33 ? 0 : 1lu << (bits - 1);
	*val = (uval ^ mask) - mask;
	return true;
}

bool QLAC__bitreader_read_raw_uint64(QLAC__BitReader *br, QLAC__uint64 *val, uint32_t bits)
{
	QLAC__uint32 hi, lo;

	if(bits > 32) {
		if(!QLAC__bitreader_read_raw_uint32(br, &hi, bits-32))
			return false;
		if(!QLAC__bitreader_read_raw_uint32(br, &lo, 32))
			return false;
		*val = hi;
		*val <<= 32;
		*val |= lo;
	}
	else {
		if(!QLAC__bitreader_read_raw_uint32(br, &lo, bits))
			return false;
		*val = lo;
	}
	return true;
}

bool QLAC__bitreader_read_raw_int64(QLAC__BitReader *br, QLAC__int64 *val, uint32_t bits)
{
	QLAC__uint64 uval, mask;
	/* OPT: inline raw uint64 code here, or make into a macro if possible in the .h file */
	if (bits < 1 || ! QLAC__bitreader_read_raw_uint64(br, &uval, bits))
		return false;
	/* sign-extend *val assuming it is currently bits wide. */
	/* From: https://graphics.stanford.edu/~seander/bithacks.html#FixedSignExtend */
	mask = bits >= 65 ? 0 : 1llu << (bits - 1);
	*val = (uval ^ mask) - mask;
	return true;
}

inline bool QLAC__bitreader_read_uint32_little_endian(QLAC__BitReader *br, QLAC__uint32 *val)
{
	QLAC__uint32 x8, x32 = 0;

	/* this doesn't need to be that fast as currently it is only used for vorbis comments */

	if(!QLAC__bitreader_read_raw_uint32(br, &x32, 8))
		return false;

	if(!QLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 8);

	if(!QLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 16);

	if(!QLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 24);

	*val = x32;
	return true;
}

bool QLAC__bitreader_skip_bits_no_crc(QLAC__BitReader *br, uint32_t bits)
{
	/*
	 * OPT: a faster implementation is possible but probably not that useful
	 * since this is only called a couple of times in the metadata readers.
	 */
	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);

	if(bits > 0) {
		const uint32_t n = br->consumed_bits & 7;
		uint32_t m;
		QLAC__uint32 x;

		if(n != 0) {
			m = QLAC_min(8-n, bits);
			if(!QLAC__bitreader_read_raw_uint32(br, &x, m))
				return false;
			bits -= m;
		}
		m = bits / 8;
		if(m > 0) {
			if(!QLAC__bitreader_skip_byte_block_aligned_no_crc(br, m))
				return false;
			bits %= 8;
		}
		if(bits > 0) {
			if(!QLAC__bitreader_read_raw_uint32(br, &x, bits))
				return false;
		}
	}

	return true;
}

bool QLAC__bitreader_skip_byte_block_aligned_no_crc(QLAC__BitReader *br, uint32_t nvals)
{
	QLAC__uint32 x;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);
	QLAC__ASSERT(QLAC__bitreader_is_consumed_byte_aligned(br));

	if(br->read_limit_set && br->read_limit < (uint32_t)-1){
		if(br->read_limit < nvals*8){
			br->read_limit = -1;
			return false;
		}
	}

	/* step 1: skip over partial head word to get word aligned */
	while(nvals && br->consumed_bits) { /* i.e. run until we read 'nvals' bytes or we hit the end of the head word */
		if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		nvals--;
	}
	if(0 == nvals)
		return true;

	/* step 2: skip whole words in chunks */
	while(nvals >= QLAC__BYTES_PER_WORD) {
		if(br->consumed_words < br->words) {
			br->consumed_words++;
			nvals -= QLAC__BYTES_PER_WORD;
			if(br->read_limit_set)
				br->read_limit -= QLAC__BITS_PER_WORD;
		}
		else if(!bitreader_read_from_client_(br))
			return false;
	}
	/* step 3: skip any remainder from partial tail bytes */
	while(nvals) {
		if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		nvals--;
	}

	return true;
}

bool QLAC__bitreader_read_byte_block_aligned_no_crc(QLAC__BitReader *br, QLAC__byte *val, uint32_t nvals)
{
	QLAC__uint32 x;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);
	QLAC__ASSERT(QLAC__bitreader_is_consumed_byte_aligned(br));

	if(br->read_limit_set && br->read_limit < (uint32_t)-1){
		if(br->read_limit < nvals*8){
			br->read_limit = -1;
			return false;
		}
	}

	/* step 1: read from partial head word to get word aligned */
	while(nvals && br->consumed_bits) { /* i.e. run until we read 'nvals' bytes or we hit the end of the head word */
		if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		*val++ = (QLAC__byte)x;
		nvals--;
	}
	if(0 == nvals)
		return true;
	/* step 2: read whole words in chunks */
	while(nvals >= QLAC__BYTES_PER_WORD) {
		if(br->consumed_words < br->words) {
			const brword word = br->buffer[br->consumed_words++];
#if QLAC__BYTES_PER_WORD == 4
			val[0] = (QLAC__byte)(word >> 24);
			val[1] = (QLAC__byte)(word >> 16);
			val[2] = (QLAC__byte)(word >> 8);
			val[3] = (QLAC__byte)word;
#elif QLAC__BYTES_PER_WORD == 8
			val[0] = (QLAC__byte)(word >> 56);
			val[1] = (QLAC__byte)(word >> 48);
			val[2] = (QLAC__byte)(word >> 40);
			val[3] = (QLAC__byte)(word >> 32);
			val[4] = (QLAC__byte)(word >> 24);
			val[5] = (QLAC__byte)(word >> 16);
			val[6] = (QLAC__byte)(word >> 8);
			val[7] = (QLAC__byte)word;
#else
			for(x = 0; x < QLAC__BYTES_PER_WORD; x++)
				val[x] = (QLAC__byte)(word >> (8*(QLAC__BYTES_PER_WORD-x-1)));
#endif
			val += QLAC__BYTES_PER_WORD;
			nvals -= QLAC__BYTES_PER_WORD;
			if(br->read_limit_set)
				br->read_limit -= QLAC__BITS_PER_WORD;
		}
		else if(!bitreader_read_from_client_(br))
			return false;
	}
	/* step 3: read any remainder from partial tail bytes */
	while(nvals) {
		if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		*val++ = (QLAC__byte)x;
		nvals--;
	}

	return true;
}

bool QLAC__bitreader_read_unary_unsigned(QLAC__BitReader *br, uint32_t *val)
#if 0 /* slow but readable version */
{
	uint32_t bit;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);

	*val = 0;
	while(1) {
		if(!QLAC__bitreader_read_bit(br, &bit))
			return false;
		if(bit)
			break;
		else
			*val++;
	}
	return true;
}
#else
{
	uint32_t i;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);

	*val = 0;
	while(1) {
		while(br->consumed_words < br->words) { /* if we've not consumed up to a partial tail word... */
			brword b = br->consumed_bits < QLAC__BITS_PER_WORD ? br->buffer[br->consumed_words] << br->consumed_bits : 0;
			if(b) {
				i = COUNT_ZERO_MSBS(b);
				*val += i;
				i++;
				br->consumed_bits += i;
				if(br->consumed_bits >= QLAC__BITS_PER_WORD) { /* faster way of testing if(br->consumed_bits == QLAC__BITS_PER_WORD) */
					br->consumed_words++;
					br->consumed_bits = 0;
				}
				return true;
			}
			else {
				*val += QLAC__BITS_PER_WORD - br->consumed_bits;
				br->consumed_words++;
				br->consumed_bits = 0;
				/* didn't find stop bit yet, have to keep going... */
			}
		}
		/* at this point we've eaten up all the whole words; have to try
		 * reading through any tail bytes before calling the read callback.
		 * this is a repeat of the above logic adjusted for the fact we
		 * don't have a whole word.  note though if the client is feeding
		 * us data a byte at a time (unlikely), br->consumed_bits may not
		 * be zero.
		 */
		if(br->bytes*8 > br->consumed_bits) {
			const uint32_t end = br->bytes * 8;
			brword b = (br->buffer[br->consumed_words] & (QLAC__WORD_ALL_ONES << (QLAC__BITS_PER_WORD-end))) << br->consumed_bits;
			if(b) {
				i = COUNT_ZERO_MSBS(b);
				*val += i;
				i++;
				br->consumed_bits += i;
				QLAC__ASSERT(br->consumed_bits < QLAC__BITS_PER_WORD);
				return true;
			}
			else {
				*val += end - br->consumed_bits;
				br->consumed_bits = end;
				QLAC__ASSERT(br->consumed_bits < QLAC__BITS_PER_WORD);
				/* didn't find stop bit yet, have to keep going... */
			}
		}
		if(!bitreader_read_from_client_(br))
			return false;
	}
}
#endif

#if 0 /* unused */
bool QLAC__bitreader_read_rice_signed(QLAC__BitReader *br, int *val, uint32_t parameter)
{
	QLAC__uint32 lsbs = 0, msbs = 0;
	uint32_t uval;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);
	QLAC__ASSERT(parameter <= 31);

	/* read the unary MSBs and end bit */
	if(!QLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	/* read the binary LSBs */
	if(!QLAC__bitreader_read_raw_uint32(br, &lsbs, parameter))
		return false;

	/* compose the value */
	uval = (msbs << parameter) | lsbs;
	if(uval & 1)
		*val = -((int)(uval >> 1)) - 1;
	else
		*val = (int)(uval >> 1);

	return true;
}
#endif

/* this is by far the most heavily used reader call.  it ain't pretty but it's fast */
bool QLAC__bitreader_read_rice_signed_block(QLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter)
#include "deduplication/bitreader_read_rice_signed_block.c"

#ifdef QLAC__BMI2_SUPPORTED
QLAC__SSE_TARGET("bmi2")
bool QLAC__bitreader_read_rice_signed_block_bmi2(QLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter)
#include "deduplication/bitreader_read_rice_signed_block.c"
#endif

#if 0 /* UNUSED */
bool QLAC__bitreader_read_golomb_signed(QLAC__BitReader *br, int *val, uint32_t parameter)
{
	QLAC__uint32 lsbs = 0, msbs = 0;
	uint32_t bit, uval, k;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);

	k = QLAC__bitmath_ilog2(parameter);

	/* read the unary MSBs and end bit */
	if(!QLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	/* read the binary LSBs */
	if(!QLAC__bitreader_read_raw_uint32(br, &lsbs, k))
		return false;

	if(parameter == 1u<<k) {
		/* compose the value */
		uval = (msbs << k) | lsbs;
	}
	else {
		uint32_t d = (1 << (k+1)) - parameter;
		if(lsbs >= d) {
			if(!QLAC__bitreader_read_bit(br, &bit))
				return false;
			lsbs <<= 1;
			lsbs |= bit;
			lsbs -= d;
		}
		/* compose the value */
		uval = msbs * parameter + lsbs;
	}

	/* unfold uint32_t to signed */
	if(uval & 1)
		*val = -((int)(uval >> 1)) - 1;
	else
		*val = (int)(uval >> 1);

	return true;
}

bool QLAC__bitreader_read_golomb_unsigned(QLAC__BitReader *br, uint32_t *val, uint32_t parameter)
{
	QLAC__uint32 lsbs, msbs = 0;
	uint32_t bit, k;

	QLAC__ASSERT(0 != br);
	QLAC__ASSERT(0 != br->buffer);

	k = QLAC__bitmath_ilog2(parameter);

	/* read the unary MSBs and end bit */
	if(!QLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	/* read the binary LSBs */
	if(!QLAC__bitreader_read_raw_uint32(br, &lsbs, k))
		return false;

	if(parameter == 1u<<k) {
		/* compose the value */
		*val = (msbs << k) | lsbs;
	}
	else {
		uint32_t d = (1 << (k+1)) - parameter;
		if(lsbs >= d) {
			if(!QLAC__bitreader_read_bit(br, &bit))
				return false;
			lsbs <<= 1;
			lsbs |= bit;
			lsbs -= d;
		}
		/* compose the value */
		*val = msbs * parameter + lsbs;
	}

	return true;
}
#endif /* UNUSED */

/* on return, if *val == 0xffffffff then the utf-8 sequence was invalid, but the return value will be true */
bool QLAC__bitreader_read_utf8_uint32(QLAC__BitReader *br, QLAC__uint32 *val, QLAC__byte *raw, uint32_t *rawlen)
{
	QLAC__uint32 v = 0;
	QLAC__uint32 x;
	uint32_t i;

	if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
		return false;
	if(raw)
		raw[(*rawlen)++] = (QLAC__byte)x;
	if(!(x & 0x80)) { /* 0xxxxxxx */
		v = x;
		i = 0;
	}
	else if((x & 0xE0) == 0xC0) { /* 110xxxxx */
		v = x & 0x1F;
		i = 1;
	}
	else if((x & 0xF0) == 0xE0) { /* 1110xxxx */
		v = x & 0x0F;
		i = 2;
	}
	else if((x & 0xF8) == 0xF0) { /* 11110xxx */
		v = x & 0x07;
		i = 3;
	}
	else if((x & 0xFC) == 0xF8) { /* 111110xx */
		v = x & 0x03;
		i = 4;
	}
	else if((x & 0xFE) == 0xFC) { /* 1111110x */
		v = x & 0x01;
		i = 5;
	}
	else {
		*val = 0xffffffff;
		return true;
	}
	for( ; i; i--) {
		if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		if(raw)
			raw[(*rawlen)++] = (QLAC__byte)x;
		if(!(x & 0x80) || (x & 0x40)) { /* 10xxxxxx */
			*val = 0xffffffff;
			return true;
		}
		v <<= 6;
		v |= (x & 0x3F);
	}
	*val = v;
	return true;
}

/* on return, if *val == 0xffffffffffffffff then the utf-8 sequence was invalid, but the return value will be true */
bool QLAC__bitreader_read_utf8_uint64(QLAC__BitReader *br, QLAC__uint64 *val, QLAC__byte *raw, uint32_t *rawlen)
{
	QLAC__uint64 v = 0;
	QLAC__uint32 x;
	uint32_t i;

	if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
		return false;
	if(raw)
		raw[(*rawlen)++] = (QLAC__byte)x;
	if(!(x & 0x80)) { /* 0xxxxxxx */
		v = x;
		i = 0;
	}
	else if((x & 0xE0) == 0xC0) { /* 110xxxxx */
		v = x & 0x1F;
		i = 1;
	}
	else if((x & 0xF0) == 0xE0) { /* 1110xxxx */
		v = x & 0x0F;
		i = 2;
	}
	else if((x & 0xF8) == 0xF0) { /* 11110xxx */
		v = x & 0x07;
		i = 3;
	}
	else if((x & 0xFC) == 0xF8) { /* 111110xx */
		v = x & 0x03;
		i = 4;
	}
	else if((x & 0xFE) == 0xFC) { /* 1111110x */
		v = x & 0x01;
		i = 5;
	}
	else if(x == 0xFE) { /* 11111110 */
		v = 0;
		i = 6;
	}
	else {
		*val = QLAC__U64L(0xffffffffffffffff);
		return true;
	}
	for( ; i; i--) {
		if(!QLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		if(raw)
			raw[(*rawlen)++] = (QLAC__byte)x;
		if(!(x & 0x80) || (x & 0x40)) { /* 10xxxxxx */
			*val = QLAC__U64L(0xffffffffffffffff);
			return true;
		}
		v <<= 6;
		v |= (x & 0x3F);
	}
	*val = v;
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
extern bool QLAC__bitreader_is_consumed_byte_aligned(const QLAC__BitReader *br);
extern uint32_t QLAC__bitreader_bits_left_for_byte_alignment(const QLAC__BitReader *br);
extern uint32_t QLAC__bitreader_get_input_bits_unconsumed(const QLAC__BitReader *br);
extern bool QLAC__bitreader_read_uint32_little_endian(QLAC__BitReader *br, QLAC__uint32 *val);
