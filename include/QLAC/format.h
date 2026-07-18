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

#ifndef QLAC__FORMAT_H
#define QLAC__FORMAT_H

#include "export.h"
#include "ordinals.h"

#ifdef __cplusplus
extern "C" {
#endif



/** \defgroup QLAC_format QLAC/format.h: format components
 *  \ingroup flac
 *
 *  \brief
 *  This module contains structure definitions for the representation
 *  of FLAC format components in memory.  These are the basic
 *  structures used by the rest of the interfaces.
 *
 *  First, you should be familiar with the
 *  <A HREF="https://xiph.org/flac/format.html">FLAC format</A>.  Many of the values here
 *  follow directly from the specification.  As a user of libFLAC, the
 *  interesting parts really are the structures that describe the frame
 *  header and metadata blocks.
 *
 *  The format structures here are very primitive, designed to store
 *  information in an efficient way.  Reading information from the
 *  structures is easy but creating or modifying them directly is
 *  more complex.  For the most part, as a user of a library, editing
 *  is not necessary; however, for metadata blocks it is, so there are
 *  convenience functions provided in the \link QLAC_metadata metadata
 *  module \endlink to simplify the manipulation of metadata blocks.
 *
 * \note
 * It's not the best convention, but symbols ending in _LEN are in bits
 * and _LENGTH are in bytes.  _LENGTH symbols are \#defines instead of
 * global variables because they are usually used when declaring byte
 * arrays and some compilers require compile-time knowledge of array
 * sizes when declared on the stack.
 *
 * \{
 */


/*
	Most of the values described in this file are defined by the FLAC
	format specification.  There is nothing to tune here.
*/

/** The largest legal metadata type code. */
#define QLAC__MAX_METADATA_TYPE_CODE (126u)

/** The minimum block size, in samples, permitted by the format. */
#define QLAC__MIN_BLOCK_SIZE (16u)

/** The maximum block size, in samples, permitted by the format. */
#define QLAC__MAX_BLOCK_SIZE (65535u)

/** The maximum block size, in samples, permitted by the FLAC subset for
 *  sample rates up to 48kHz. */
#define QLAC__SUBSET_MAX_BLOCK_SIZE_48000HZ (4608u)

/** The maximum number of channels permitted by the format. */
#define QLAC__MAX_CHANNELS (8u)

/** The minimum sample resolution permitted by the format. */
#define QLAC__MIN_BITS_PER_SAMPLE (4u)

/** The maximum sample resolution permitted by the format. */
#define QLAC__MAX_BITS_PER_SAMPLE (32u)

/** The maximum sample resolution permitted by libFLAC.
 *
 * QLAC__MAX_BITS_PER_SAMPLE is the limit of the FLAC format.  However,
 * the reference encoder/decoder used to be limited to 24 bits. This
 * value was used to signal that limit.
 */
#define QLAC__REFERENCE_CODEC_MAX_BITS_PER_SAMPLE (32u)

/** The maximum sample rate permitted by the format.  The value is
 *  ((2 ^ 20) - 1)
 */
#define QLAC__MAX_SAMPLE_RATE (1048575u)

/** The maximum LPC order permitted by the format. */
#define QLAC__MAX_LPC_ORDER (32u)

/** The maximum LPC order permitted by the FLAC subset for sample rates
 *  up to 48kHz. */
#define QLAC__SUBSET_MAX_LPC_ORDER_48000HZ (12u)

/** The minimum quantized linear predictor coefficient precision
 *  permitted by the format.
 */
#define QLAC__MIN_QLP_COEFF_PRECISION (5u)

/** The maximum quantized linear predictor coefficient precision
 *  permitted by the format.
 */
#define QLAC__MAX_QLP_COEFF_PRECISION (15u)

/** The maximum order of the fixed predictors permitted by the format. */
#define QLAC__MAX_FIXED_ORDER (4u)


/** The version string of the release, stamped onto the libraries and binaries.
 *
 * \note
 * This does not correspond to the shared library version number, which
 * is used to determine binary compatibility.
 */
extern QLAC_API const char *QLAC__VERSION_STRING;

/** The vendor string inserted by the encoder into the VORBIS_COMMENT block.
 *  This is a NUL-terminated ASCII string; when inserted into the
 *  VORBIS_COMMENT the trailing null is stripped.
 */
extern QLAC_API const char *QLAC__VENDOR_STRING;

/** The byte string representation of the beginning of a FLAC stream. */
extern QLAC_API const QLAC__byte QLAC__STREAM_SYNC_STRING[4]; /* = "fLaC" */

/** The 32-bit integer big-endian representation of the beginning of
 *  a FLAC stream.
 */
extern QLAC_API const uint32_t QLAC__STREAM_SYNC; /* = 0x664C6143 */

/** The length of the FLAC signature in bits. */
extern QLAC_API const uint32_t QLAC__STREAM_SYNC_LEN; /* = 32 bits */

/** The length of the FLAC signature in bytes. */
#define QLAC__STREAM_SYNC_LENGTH (4u)


/*****************************************************************************
 *
 * Frame data structures
 *
 *****************************************************************************/

/*****************************************************************************/

/* Entropy coding is always RICE2 with a single partition in this fork; there is
 * no method type field and no partition order field in the bitstream, so the
 * method-type enum and its string table are gone. */

extern QLAC_API const uint32_t QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN; /**< == 5 (bits) */
extern QLAC_API const uint32_t QLAC__ENTROPY_CODING_METHOD_RICE_RAW_LEN; /**< == 5 (bits) */

extern QLAC_API const uint32_t QLAC__ENTROPY_CODING_METHOD_RICE2_ESCAPE_PARAMETER;
/**< == (1<<QLAC__ENTROPY_CODING_METHOD_RICE2_PARAMETER_LEN)-1 */

/*****************************************************************************/

/** An enumeration of the available frame (predictor) types. */
typedef enum {
	QLAC__FRAME_TYPE_CONSTANT = 0, /**< constant signal */
	QLAC__FRAME_TYPE_VERBATIM = 1, /**< uncompressed signal */
	QLAC__FRAME_TYPE_FIXED = 2, /**< fixed polynomial prediction */
	QLAC__FRAME_TYPE_LPC = 3 /**< linear prediction */
} QLAC__FrameType;

/** Maps a QLAC__FrameType to a C string.
 *
 *  Using a QLAC__FrameType as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern QLAC_API const char * const QLAC__FrameTypeString[];


extern QLAC_API const uint32_t QLAC__FRAME_LPC_QLP_COEFF_PRECISION_LEN; /**< == 4 (bits) */
extern QLAC_API const uint32_t QLAC__FRAME_LPC_QLP_SHIFT_LEN; /**< == 5 (bits) */

/** == 1 (bit)
 *
 * This used to be a zero-padding bit (hence the name
 * QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN) but is now a reserved bit.  It still has a
 * mandatory value of \c 0 but in the future may take on the value \c 0 or \c 1
 * to mean something else.
 */
extern QLAC_API const uint32_t QLAC__FRAME_SUBTYPE_ZERO_PAD_LEN;
extern QLAC_API const uint32_t QLAC__FRAME_SUBTYPE_LEN; /**< == 6 (bits) */
extern QLAC_API const uint32_t QLAC__FRAME_WASTED_BITS_FLAG_LEN; /**< == 1 (bit) */

extern QLAC_API const uint32_t QLAC__FRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK; /**< = 0x00 */
extern QLAC_API const uint32_t QLAC__FRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK; /**< = 0x02 */
extern QLAC_API const uint32_t QLAC__FRAME_TYPE_FIXED_BYTE_ALIGNED_MASK; /**< = 0x10 */
extern QLAC_API const uint32_t QLAC__FRAME_TYPE_LPC_BYTE_ALIGNED_MASK; /**< = 0x40 */

/*****************************************************************************/


/*****************************************************************************
 *
 * Frame structures
 *
 *****************************************************************************/

/** FLAC frame structure.
 *
 * In this fork a frame holds exactly one (mono) subframe and a single Rice
 * , so all per-subframe and per-residual information is stored
 * directly here rather than in nested subframe/ structures.  There is
 * no frame header: blocksize, bits-per-sample and "mono" are fixed out-of-band
 * parameters, and a block begins directly with the frame subtype on the wire
 * (see BLOCK_FORMAT.md).
 */
typedef struct {
	QLAC__FrameType type;
	/**< The frame (predictor) type. */

	uint32_t wasted_bits;
	/**< Number of wasted (trailing-zero) bits shifted out of every sample. */

	/* CONSTANT */
	QLAC__int64 constant_value;
	/**< CONSTANT only: the constant signal value. */

	/* VERBATIM */
	const QLAC__int32 *verbatim_data;
	/**< VERBATIM only: the verbatim signal (always 32-bit in this <=32 bps fork). */

	/* FIXED and LPC shared predictor/residual fields */
	uint32_t order;
	/**< FIXED/LPC only: the predictor order. */

	QLAC__int64 warmup[QLAC__MAX_LPC_ORDER];
	/**< FIXED/LPC only: warmup samples priming the predictor, length == order. */

	const QLAC__int32 *residual;
	/**< FIXED/LPC only: the residual signal, length == (blocksize minus order). */

	uint32_t rice_parameter;
	/**< FIXED/LPC only: the single Rice parameter for the whole residual. */

	uint32_t rice_raw_bits;
	/**< FIXED/LPC only: escape width.  Non-zero means the residual is stored as
	 * raw values of this bit width instead of Rice coded. */

	/* LPC only */
	uint32_t qlp_coeff_precision;
	/**< LPC only: quantized FIR coefficient precision in bits. */

	int quantization_level;
	/**< LPC only: the qlp coeff shift. */

	QLAC__int32 qlp_coeff[QLAC__MAX_LPC_ORDER];
	/**< LPC only: FIR filter coefficients, length == order. */
} QLAC__Frame;

/*****************************************************************************/


/*****************************************************************************
 *
 * Meta-data structures
 *
 *****************************************************************************/

/** An enumeration of the available metadata block types. */
/** Maps a QLAC__MetadataType to a C string.
 *
 *  Using a QLAC__MetadataType as the index to this array will
 *  give the string equivalent.  The contents should not be modified.
 */
extern QLAC_API const char * const QLAC__MetadataTypeString[];


/** FLAC STREAMINFO structure.  (c.f. <A HREF="https://xiph.org/flac/format.html#metadata_block_streaminfo">format specification</A>)
 */
typedef struct {
	uint32_t min_blocksize, max_blocksize;
	uint32_t min_framesize, max_framesize;
	uint32_t bits_per_sample;
	QLAC__uint64 total_samples;
} QLAC__StreamMetadata_StreamInfo;

extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN; /**< == 16 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN; /**< == 16 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN; /**< == 24 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN; /**< == 24 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN; /**< == 3 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN; /**< == 5 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN; /**< == 36 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_STREAMINFO_MD5SUM_LEN; /**< == 128 (bits) */

/** The total stream length of the STREAMINFO block in bytes. */
#define QLAC__STREAM_METADATA_STREAMINFO_LENGTH (34u)

/** FLAC PADDING structure.  (c.f. <A HREF="https://xiph.org/flac/format.html#metadata_block_padding">format specification</A>)
 */
typedef struct {
	int dummy;
	/**< Conceptually this is an empty struct since we don't store the
	 * padding bytes.  Empty structs are not allowed by some C compilers,
	 * hence the dummy.
	 */
} QLAC__StreamMetadata_Padding;




/** Structure that is used when a metadata block of unknown type is loaded.
 *  The contents are opaque.  The structure is used only internally to
 *  correctly handle unknown metadata.
 */
typedef struct {
	QLAC__byte *data;
} QLAC__StreamMetadata_Unknown;


extern QLAC_API const uint32_t QLAC__STREAM_METADATA_IS_LAST_LEN; /**< == 1 (bit) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_TYPE_LEN; /**< == 7 (bits) */
extern QLAC_API const uint32_t QLAC__STREAM_METADATA_LENGTH_LEN; /**< == 24 (bits) */

/** The total stream length of a metadata block header in bytes. */
#define QLAC__STREAM_METADATA_HEADER_LENGTH (4u)

/*****************************************************************************/


/*****************************************************************************
 *
 * Utility functions
 *
 *****************************************************************************/

/** Tests that a blocksize is valid for the FLAC subset.
 *
 * \param blocksize    The blocksize to test for compliance.
 * \retval bool
 *    \c true if the given blocksize conforms to the specification for the
 *    subset, else \c false.
 */
QLAC_API bool QLAC__format_blocksize_is_subset(uint32_t blocksize);

/* \} */

#ifdef __cplusplus
}
#endif

#endif
