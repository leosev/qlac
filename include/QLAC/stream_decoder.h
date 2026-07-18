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

#ifndef QLAC__STREAM_DECODER_H
#define QLAC__STREAM_DECODER_H

#include "export.h"
#include "format.h"

#ifdef __cplusplus
extern "C" {
#endif


/** \file include/QLAC/stream_decoder.h
 *
 *  \brief
 *  This module contains the functions which implement the block decoder.
 *
 *  See the detailed documentation in the
 *  \link QLAC_stream_decoder stream decoder \endlink module.
 */

/** \defgroup QLAC_decoder QLAC/ \*_decoder.h: decoder interfaces
 *  \ingroup flac
 *
 *  \brief
 *  This module describes the decoder layer provided by libFLAC.
 *
 * This fork of libFLAC is a pure block decoder, the exact inverse of the
 * block encoder in QLAC/stream_encoder.h.  It decodes one independent mono
 * block at a time from a caller-supplied byte buffer into a caller-supplied
 * sample buffer.  There is no stream, no \c fLaC sync, no metadata
 * (STREAMINFO etc.), no frame header, no CRC/MD5, no channels and no Ogg.
 */

/** \defgroup QLAC_stream_decoder QLAC/stream_decoder.h: block decoder interface
 *  \ingroup QLAC_decoder
 *
 *  \brief
 *  This module contains the functions which implement the block decoder.
 *
 * Usage mirrors the block encoder:
 * - Create an instance with QLAC__stream_decoder_new().
 * - Set the fixed, out-of-band parameters that the producer and consumer
 *   agreed on (these are NOT carried on the wire):
 *   QLAC__stream_decoder_set_bits_per_sample() and
 *   QLAC__stream_decoder_set_blocksize().
 * - Initialize with QLAC__stream_decoder_init_block().
 * - For each block, call QLAC__stream_decoder_decode_block() passing the
 *   block's bytes and a sample buffer to fill.
 * - Finish with QLAC__stream_decoder_finish() and delete with
 *   QLAC__stream_decoder_delete().
 *
 * The decoder keeps no state between blocks; each call to
 * QLAC__stream_decoder_decode_block() decodes one self-contained block.
 *
 * \{
 */


/** State values for a QLAC__StreamDecoder.
 *
 * The decoder's state can be obtained by calling QLAC__stream_decoder_get_state().
 */
typedef enum {

	QLAC__STREAM_DECODER_OK = 0,
	/**< The decoder is initialized and ready to decode blocks. */

	QLAC__STREAM_DECODER_END_OF_STREAM,
	/**< The decoder ran out of input bytes while decoding a block. */

	QLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR,
	/**< An error occurred allocating memory.  The decoder is in an invalid
	 * state and can no longer be used.
	 */

	QLAC__STREAM_DECODER_CLIENT_ERROR,
	/**< The supplied input was malformed or the decoded data did not fit the
	 * configured bits-per-sample.
	 */

	QLAC__STREAM_DECODER_UNINITIALIZED
	/**< The decoder is in the uninitialized state; the decoder must be
	 * initialized with QLAC__stream_decoder_init_block() before blocks can
	 * be decoded.
	 */

} QLAC__StreamDecoderState;

/** Maps a QLAC__StreamDecoderState to a C string.
 *
 *  Using a QLAC__StreamDecoderState as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern QLAC_API const char * const QLAC__StreamDecoderStateString[];


/** Possible return values for QLAC__stream_decoder_init_block().
 */
typedef enum {

	QLAC__STREAM_DECODER_INIT_STATUS_OK = 0,
	/**< Initialization was successful. */

	QLAC__STREAM_DECODER_INIT_STATUS_INVALID_PARAMETERS,
	/**< The configured bits-per-sample or blocksize is out of range. */

	QLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR,
	/**< An error occurred allocating memory. */

	QLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED
	/**< QLAC__stream_decoder_init_block() was called when the decoder was
	 * already initialized, usually because QLAC__stream_decoder_finish() was
	 * not called.
	 */

} QLAC__StreamDecoderInitStatus;

/** Maps a QLAC__StreamDecoderInitStatus to a C string.
 *
 *  Using a QLAC__StreamDecoderInitStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern QLAC_API const char * const QLAC__StreamDecoderInitStatusString[];


/** The QLAC__StreamDecoder structure.  This structure is used for decoding.
 *  The contents of a QLAC__StreamDecoder should not be accessed directly.
 */
struct QLAC__StreamDecoderProtected;
struct QLAC__StreamDecoderPrivate;
typedef struct {
	struct QLAC__StreamDecoderProtected *protected_; /* avoid the C++ keyword 'protected' */
	struct QLAC__StreamDecoderPrivate *private_; /* avoid the C++ keyword 'private' */
} QLAC__StreamDecoder;


/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

/** Create a new block decoder instance.  The instance is created with
 *  default settings; see the individual QLAC__stream_decoder_set_*()
 *  functions for each setting's default.
 *
 * \retval QLAC__StreamDecoder*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
QLAC_API QLAC__StreamDecoder *QLAC__stream_decoder_new(void);

/** Free a decoder instance.  Deletes the object pointed to by \a decoder.
 *
 * \param decoder  A pointer to an existing decoder.
 * \assert
 *    \code decoder != NULL \endcode
 */
QLAC_API void QLAC__stream_decoder_delete(QLAC__StreamDecoder *decoder);


/***********************************************************************
 *
 * Public class method prototypes
 *
 ***********************************************************************/

/** Set the resolution of the samples, in bits.  This is a fixed out-of-band
 *  parameter and must match the encoder's setting.
 *
 * \default \c 16
 * \param  decoder  A decoder instance to set.
 * \param  value    The sample resolution in bits (QLAC__MIN_BITS_PER_SAMPLE
 *                  to QLAC__MAX_BITS_PER_SAMPLE).
 * \assert
 *    \code decoder != NULL \endcode
 * \retval bool
 *    \c false if the decoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_decoder_set_bits_per_sample(QLAC__StreamDecoder *decoder, uint32_t value);

/** Set the blocksize, in samples.  This is a fixed out-of-band parameter:
 *  every block carries exactly this many samples and must match the encoder's
 *  setting.
 *
 * \default \c 4096
 * \param  decoder  A decoder instance to set.
 * \param  value    The blocksize in samples (QLAC__MIN_BLOCK_SIZE to
 *                  QLAC__MAX_BLOCK_SIZE).
 * \assert
 *    \code decoder != NULL \endcode
 * \retval bool
 *    \c false if the decoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_decoder_set_blocksize(QLAC__StreamDecoder *decoder, uint32_t value);

/** Get the current decoder state.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval QLAC__StreamDecoderState
 *    The current decoder state.
 */
QLAC_API QLAC__StreamDecoderState QLAC__stream_decoder_get_state(const QLAC__StreamDecoder *decoder);

/** Get the current decoder state as a C string.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval const char *
 *    The decoder state as a C string.
 */
QLAC_API const char *QLAC__stream_decoder_get_resolved_state_string(const QLAC__StreamDecoder *decoder);

/** Get the configured sample resolution, in bits.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval uint32_t
 *    See above.
 */
QLAC_API uint32_t QLAC__stream_decoder_get_bits_per_sample(const QLAC__StreamDecoder *decoder);

/** Get the configured blocksize, in samples.
 *
 * \param  decoder  A decoder instance to query.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval uint32_t
 *    See above.
 */
QLAC_API uint32_t QLAC__stream_decoder_get_blocksize(const QLAC__StreamDecoder *decoder);

/** Initialize the decoder as a block decoder.
 *
 *  This validates the out-of-band parameters set with the
 *  QLAC__stream_decoder_set_*() functions and allocates the (single, fixed)
 *  internal buffers once.  After a successful call the decoder is in the
 *  QLAC__STREAM_DECODER_OK state and ready for
 *  QLAC__stream_decoder_decode_block().
 *
 * \param  decoder  An uninitialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval QLAC__StreamDecoderInitStatus
 *    QLAC__STREAM_DECODER_INIT_STATUS_OK if initialization succeeded.
 */
QLAC_API QLAC__StreamDecoderInitStatus QLAC__stream_decoder_init_block(QLAC__StreamDecoder *decoder);

/** Finish decoding.  Frees the internal buffers and returns the decoder to
 *  the uninitialized state.  The instance may then be reconfigured and
 *  initialized again, or deleted.
 *
 * \param  decoder  An initialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval bool
 *    \c true (always succeeds).
 */
QLAC_API bool QLAC__stream_decoder_finish(QLAC__StreamDecoder *decoder);

/** Get the required output sample-buffer size, in QLAC__int32 elements.
 *
 *  The caller-owned output buffer passed to
 *  QLAC__stream_decoder_decode_block() must hold this many elements; it
 *  consists of QLAC__STREAM_DECODER_OUTPUT_LEADING_PAD leading samples (used
 *  by the SIMD LPC restore routines, written as zero) followed by blocksize
 *  decoded samples.
 *
 * \param  decoder  An initialized decoder instance.
 * \assert
 *    \code decoder != NULL \endcode
 * \retval uint32_t
 *    See above.
 */
QLAC_API uint32_t QLAC__stream_decoder_get_output_buffer_size(const QLAC__StreamDecoder *decoder);


/** The number of leading zero samples the caller must reserve in front of the
 *  first decoded sample of the output buffer (== QLAC__STREAM_DECODER_OUTPUT_LEADING_PAD).
 *
 *  These satisfy the negative-index read-behind of the vectorised LPC restore
 *  routines.
 */
#define QLAC__STREAM_DECODER_OUTPUT_LEADING_PAD 4u


/** Decode one independent block.
 *
 *  Reads exactly one block (as produced by
 *  QLAC__stream_encoder_encode_block()) from the caller-owned byte buffer
 *  \a in (of \a in_bytes bytes) and writes \a blocksize decoded samples into
 *  the caller-owned buffer \a out.
 *
 *  \a out must point at the first real sample of a buffer satisfying the
 *  contract described by QLAC__stream_decoder_get_output_buffer_size() /
 *  QLAC__stream_decoder_get_output_alignment() /
 *  QLAC__stream_decoder_get_output_leading_pad() (i.e. allocated with the
 *  helpers in QLAC/memory.h, with the leading-pad samples in front zeroed).
 *
 * \param  decoder   An initialized decoder instance.
 * \param  in        The block's bytes (caller-owned).
 * \param  in_bytes  The number of bytes available in \a in.
 * \param  out       Where to write the decoded samples (caller-owned).
 * \assert
 *    \code decoder != NULL \endcode
 *    \code in != NULL \endcode
 *    \code out != NULL \endcode
 * \retval bool
 *    \c true on success; \c false on error (see
 *    QLAC__stream_decoder_get_state()).
 */
QLAC_API bool QLAC__stream_decoder_decode_block(QLAC__StreamDecoder *decoder, const QLAC__byte *in, size_t in_bytes, QLAC__int32 *out);

/* \} */

#ifdef __cplusplus
}
#endif

#endif
