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

#ifndef QLAC__STREAM_ENCODER_H
#define QLAC__STREAM_ENCODER_H

#include "export.h"
#include "format.h"

#ifdef __cplusplus
extern "C" {
#endif


/** \file include/QLAC/stream_encoder.h
 *
 *  \brief
 *  This module contains the functions which implement the block encoder.
 *
 *  See the detailed documentation in the
 *  \link QLAC_stream_encoder block encoder \endlink module.
 */

/** \defgroup QLAC_encoder QLAC/ \*_encoder.h: encoder interfaces
 *  \ingroup flac
 *
 *  \brief
 *  This module describes the encoder layer provided by libFLAC.
 *
 * This fork of libFLAC is a pure block encoder, the exact inverse of the
 * block decoder in QLAC/stream_decoder.h.  It encodes one independent mono
 * block at a time from a caller-supplied sample buffer into a caller-supplied
 * byte buffer.  There is no stream, no \c fLaC sync, no metadata
 * (STREAMINFO etc.), no frame header, no CRC/MD5, no channels and no Ogg.
 */

/** \defgroup QLAC_stream_encoder QLAC/stream_encoder.h: block encoder interface
 *  \ingroup QLAC_encoder
 *
 *  \brief
 *  This module contains the functions which implement the block encoder.
 *
 * Each call to QLAC__stream_encoder_encode_block() encodes exactly one
 * independent block of mono samples and returns its bitstream bytes; the
 * encoder keeps no state between blocks.  All of blocksize, sample rate,
 * bits-per-sample and "mono" are fixed out-of-band parameters that the
 * producer and consumer must agree on -- they are not carried on the wire.
 * See BLOCK_FORMAT.md for the on-the-wire layout.
 *
 * The basic usage of this encoder is as follows:
 * - Create an instance with QLAC__stream_encoder_new().
 * - Override the default settings with the QLAC__stream_encoder_set_*()
 *   functions.  At a minimum set QLAC__stream_encoder_set_bits_per_sample()
 *   and QLAC__stream_encoder_set_blocksize(); optionally control the
 *   compression with QLAC__stream_encoder_set_compression_level() and the
 *   finer-grained set functions.
 * - Initialize the instance with QLAC__stream_encoder_init_stream(); check
 *   that it returns QLAC__STREAM_ENCODER_INIT_STATUS_OK.
 * - For each block, call QLAC__stream_encoder_encode_block() passing a
 *   sample buffer and an output byte buffer to fill.  The caller owns both
 *   buffers and must honour the alignment / leading-pad contract described
 *   by QLAC__stream_encoder_get_input_buffer_size() /
 *   QLAC__stream_encoder_get_input_alignment() /
 *   QLAC__stream_encoder_get_input_leading_pad() (allocate with the helpers
 *   in QLAC/memory.h).  Note that encode_block() may modify the sample
 *   buffer in place (the wasted-bits step right-shifts the samples).
 * - Finish with QLAC__stream_encoder_finish() and delete with
 *   QLAC__stream_encoder_delete().
 *
 * \note
 * The "set" functions may only be called when the encoder is in the
 * state QLAC__STREAM_ENCODER_UNINITIALIZED, i.e. after
 * QLAC__stream_encoder_new() or QLAC__stream_encoder_finish(), but
 * before QLAC__stream_encoder_init_stream().  If this is the case they will
 * return \c true, otherwise \c false.
 *
 * \note
 * QLAC__stream_encoder_finish() resets all settings to the constructor
 * defaults.
 *
 * \{
 */


/** State values for a QLAC__StreamEncoder.
 *
 * The encoder's state can be obtained by calling QLAC__stream_encoder_get_state().
 *
 * If the encoder gets into any other state besides \c QLAC__STREAM_ENCODER_OK
 * or \c QLAC__STREAM_ENCODER_UNINITIALIZED, it becomes invalid for encoding and
 * must be deleted with QLAC__stream_encoder_delete().
 */
typedef enum {

	QLAC__STREAM_ENCODER_OK = 0,
	/**< The encoder is in the normal OK state and samples can be processed. */

	QLAC__STREAM_ENCODER_UNINITIALIZED,
	/**< The encoder is in the uninitialized state; one of the
	 * QLAC__stream_encoder_init_*() functions must be called before samples
	 * can be processed.
	 */

	QLAC__STREAM_ENCODER_CLIENT_ERROR,
	/**< One of the callbacks returned a fatal error. */

	QLAC__STREAM_ENCODER_IO_ERROR,
	/**< An I/O error occurred while opening/reading/writing a file.
	 * Check \c errno.
	 */

	QLAC__STREAM_ENCODER_FRAMING_ERROR,
	/**< An error occurred while writing the stream; usually, the
	 * write_callback returned an error.
	 */

	QLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR
	/**< Memory allocation failed. */

} QLAC__StreamEncoderState;

/** Maps a QLAC__StreamEncoderState to a C string.
 *
 *  Using a QLAC__StreamEncoderState as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern QLAC_API const char * const QLAC__StreamEncoderStateString[];


/** Possible return values for the QLAC__stream_encoder_init_*() functions.
 */
typedef enum {

	QLAC__STREAM_ENCODER_INIT_STATUS_OK = 0,
	/**< Initialization was successful. */

	QLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR,
	/**< General failure to set up encoder; call QLAC__stream_encoder_get_state() for cause. */

	QLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER,
	/**< The library was not compiled with support for the given container
	 * format.
	 */

	QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE,
	/**< The encoder has an invalid setting for bits-per-sample.
	 * FLAC supports 4-32 bps.
	 */

	QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE,
	/**< The encoder has an invalid setting for the block size. */

	QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_MAX_LPC_ORDER,
	/**< The encoder has an invalid setting for the maximum LPC order. */

	QLAC__STREAM_ENCODER_INIT_STATUS_INVALID_QLP_COEFF_PRECISION,
	/**< The encoder has an invalid setting for the precision of the quantized linear predictor coefficients. */

	QLAC__STREAM_ENCODER_INIT_STATUS_BLOCK_SIZE_TOO_SMALL_FOR_LPC_ORDER,
	/**< The specified block size is less than the maximum LPC order. */

	QLAC__STREAM_ENCODER_INIT_STATUS_NOT_STREAMABLE,
	/**< The encoder is bound to the <A HREF="https://xiph.org/flac/format.html#subset">Subset</A> but other settings violate it. */

	QLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED
	/**< QLAC__stream_encoder_init_*() was called when the encoder was
	 * already initialized, usually because
	 * QLAC__stream_encoder_finish() was not called.
	 */

} QLAC__StreamEncoderInitStatus;

/** Maps a QLAC__StreamEncoderInitStatus to a C string.
 *
 *  Using a QLAC__StreamEncoderInitStatus as the index to this array
 *  will give the string equivalent.  The contents should not be modified.
 */
extern QLAC_API const char * const QLAC__StreamEncoderInitStatusString[];


/***********************************************************************
 *
 * class QLAC__StreamEncoder
 *
 ***********************************************************************/

struct QLAC__StreamEncoderProtected;
struct QLAC__StreamEncoderPrivate;
/** The opaque structure definition for the stream encoder type.
 *  See the \link QLAC_stream_encoder stream encoder module \endlink
 *  for a detailed description.
 */
typedef struct {
	struct QLAC__StreamEncoderProtected *protected_; /* avoid the C++ keyword 'protected' */
	struct QLAC__StreamEncoderPrivate *private_; /* avoid the C++ keyword 'private' */
} QLAC__StreamEncoder;

/***********************************************************************
 *
 * Class constructor/destructor
 *
 ***********************************************************************/

/** Create a new stream encoder instance.  The instance is created with
 *  default settings; see the individual QLAC__stream_encoder_set_*()
 *  functions for each setting's default.
 *
 * \retval QLAC__StreamEncoder*
 *    \c NULL if there was an error allocating memory, else the new instance.
 */
QLAC_API QLAC__StreamEncoder *QLAC__stream_encoder_new(void);

/** Free an encoder instance.  Deletes the object pointed to by \a encoder.
 *
 * \param encoder  A pointer to an existing encoder.
 * \assert
 *    \code encoder != NULL \endcode
 */
QLAC_API void QLAC__stream_encoder_delete(QLAC__StreamEncoder *encoder);


/***********************************************************************
 *
 * Public class method prototypes
 *
 ***********************************************************************/

/** Set the <A HREF="https://xiph.org/flac/format.html#subset">Subset</A> flag.  If \c true,
 *  the encoder will comply with the Subset and will check the
 *  settings during QLAC__stream_encoder_init_*() to see if all settings
 *  comply.  If \c false, the settings may take advantage of the full
 *  range that the format allows.
 *
 *  Make sure you know what it entails before setting this to \c false.
 *
 * \default \c true
 * \param  encoder  An encoder instance to set.
 * \param  value    Flag value (see above).
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_streamable_subset(QLAC__StreamEncoder *encoder, bool value);

/** Set the sample resolution of the input to be encoded.
 *
 * \warning
 * Do not feed the encoder data that is wider than the value you
 * set here or you will generate an invalid stream.
 *
 * \default \c 16
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_bits_per_sample(QLAC__StreamEncoder *encoder, uint32_t value);


/** Set the compression level
 *
 * The compression level is roughly proportional to the amount of effort
 * the encoder expends to compress the file.  A higher level usually
 * means more computation but higher compression.  The default level is
 * suitable for most applications.
 *
 * Currently the levels range from \c 0 (fastest, least compression) to
 * \c 8 (slowest, most compression).  A value larger than \c 8 will be
 * treated as \c 8.
 *
 * This function automatically calls the following other \c _set_
 * functions with appropriate values, so the client does not need to
 * unless it specifically wants to override them:
 * - QLAC__stream_encoder_set_do_mid_side_stereo()
 * - QLAC__stream_encoder_set_loose_mid_side_stereo()
 * - QLAC__stream_encoder_set_apodization()
 * - QLAC__stream_encoder_set_max_lpc_order()
 * - QLAC__stream_encoder_set_qlp_coeff_precision()
 * - QLAC__stream_encoder_set_do_qlp_coeff_prec_search()
 * - QLAC__stream_encoder_set_do_exhaustive_model_search()
 * - QLAC__stream_encoder_set_min_residual_order()
 * - QLAC__stream_encoder_set_max_residual_order()
 * - QLAC__stream_encoder_set_rice_parameter_search_dist()
 *
 * The actual values set for each level are:
 * <table>
 * <tr>
 *  <td><b>level</b></td>
 *  <td>do mid-side stereo</td>
 *  <td>loose mid-side stereo</td>
 *  <td>apodization</td>
 *  <td>max lpc order</td>
 *  <td>qlp coeff precision</td>
 *  <td>qlp coeff prec search</td>
 *  <td>escape coding</td>
 *  <td>exhaustive model search</td>
 *  <td>min residual  order</td>
 *  <td>max residual  order</td>
 *  <td>rice parameter search dist</td>
 * </tr>
 * <tr>  <td><b>0</b></td> <td>false</td> <td>false</td> <td>tukey(0.5)</td>         <td>0</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>3</td> <td>0</td> </tr>
 * <tr>  <td><b>1</b></td> <td>true</td>  <td>true</td>  <td>tukey(0.5)</td>         <td>0</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>3</td> <td>0</td> </tr>
 * <tr>  <td><b>2</b></td> <td>true</td>  <td>false</td> <td>tukey(0.5)</td>         <td>0</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>3</td> <td>0</td> </tr>
 * <tr>  <td><b>3</b></td> <td>false</td> <td>false</td> <td>tukey(0.5)</td>         <td>6</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>4</td> <td>0</td> </tr>
 * <tr>  <td><b>4</b></td> <td>true</td>  <td>true</td>  <td>tukey(0.5)</td>         <td>8</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>4</td> <td>0</td> </tr>
 * <tr>  <td><b>5</b></td> <td>true</td>  <td>false</td> <td>tukey(0.5)</td>         <td>8</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>5</td> <td>0</td> </tr>
 * <tr>  <td><b>6</b></td> <td>true</td>  <td>false</td> <td>subdivide_tukey(2)</td> <td>8</td>  <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>6</td> <td>0</td> </tr>
 * <tr>  <td><b>7</b></td> <td>true</td>  <td>false</td> <td>subdivide_tukey(2)</td> <td>12</td> <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>6</td> <td>0</td> </tr>
 * <tr>  <td><b>8</b></td> <td>true</td>  <td>false</td> <td>subdivide_tukey(3)</td> <td>12</td> <td>0</td> <td>false</td> <td>false</td> <td>false</td> <td>0</td> <td>6</td> <td>0</td> </tr>
 * </table>
 *
 * \default \c 5
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_compression_level(QLAC__StreamEncoder *encoder, uint32_t value);

/** Set the blocksize to use while encoding.
 *
 * The number of samples to use per frame.  Use \c 0 to let the encoder
 * estimate a blocksize; this is usually best.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_blocksize(QLAC__StreamEncoder *encoder, uint32_t value);

/** Sets the apodization function(s) the encoder will use when windowing
 *  audio data for LPC analysis.
 *
 * The \a specification is a plain ASCII string which specifies exactly
 * which functions to use.  There may be more than one (up to 32),
 * separated by \c ';' characters.  Some functions take one or more
 * comma-separated arguments in parentheses.
 *
 * The available functions are \c bartlett, \c bartlett_hann,
 * \c blackman, \c blackman_harris_4term_92db, \c connes, \c flattop,
 * \c gauss(STDDEV), \c hamming, \c hann, \c kaiser_bessel, \c nuttall,
 * \c rectangle, \c triangle, \c tukey(P), \c partial_tukey(n[/ov[/P]]),
 * \c punchout_tukey(n[/ov[/P]]), \c subdivide_tukey(n[/P]), \c welch.
 *
 * For \c gauss(STDDEV), STDDEV specifies the standard deviation
 * (0<STDDEV<=0.5).
 *
 * For \c tukey(P), P specifies the fraction of the window that is
 * tapered (0<=P<=1).  P=0 corresponds to \c rectangle and P=1
 * corresponds to \c hann.
 *
 * Specifying \c partial_tukey or \c punchout_tukey works a little
 * different. These do not specify a single apodization function, but
 * a series of them with some overlap. partial_tukey specifies a series
 * of small windows (all treated separately) while punchout_tukey
 * specifies a series of windows that have a hole in them. In this way,
 * the predictor is constructed with only a part of the block, which
 * helps in case a block consists of dissimilar parts.
 *
 * The three parameters that can be specified for the functions are
 * n, ov and P. n is the number of functions to add, ov is the overlap
 * of the windows in case of partial_tukey and the overlap in the gaps
 * in case of punchout_tukey. P is the fraction of the window that is
 * tapered, like with a regular tukey window. The function can be
 * specified with only a number, a number and an overlap, or a number
 * an overlap and a P, for example, partial_tukey(3), partial_tukey(3/0.3)
 * and partial_tukey(3/0.3/0.5) are all valid. ov should be smaller than 1
 * and can be negative.
 *
 * subdivide_tukey(n) is a more efficient reimplementation of
 * partial_tukey and punchout_tukey taken together, recycling as much data
 * as possible. It combines all possible non-redundant partial_tukey(n)
 * and punchout_tukey(n) up to the n specified. Specifying
 * subdivide_tukey(3) is equivalent to specifying tukey, partial_tukey(2),
 * partial_tukey(3) and punchout_tukey(3), specifying subdivide_tukey(5)
 * equivalently adds partial_tukey(4), punchout_tukey(4), partial_tukey(5)
 * and punchout_tukey(5). To be able to reuse data as much as possible,
 * the tukey taper is taken equal for all windows, and the P specified is
 * applied for the smallest used window. In other words,
 * subdivide_tukey(2/0.5) results in a taper equal to that of tukey(0.25)
 * and subdivide_tukey(5) in a taper equal to that of tukey(0.1). The
 * default P for subdivide_tukey when none is specified is 0.5.
 *
 * Example specifications are \c "blackman" or
 * \c "hann;triangle;tukey(0.5);tukey(0.25);tukey(0.125)"
 *
 * Any function that is specified erroneously is silently dropped.  Up
 * to 32 functions are kept, the rest are dropped.  If the specification
 * is empty the encoder defaults to \c "tukey(0.5)".
 *
 * When more than one function is specified, then for every subframe the
 * encoder will try each of them separately and choose the window that
 * results in the smallest compressed subframe.
 *
 * Note that each function specified causes the encoder to occupy a
 * floating point array in which to store the window. Also note that the
 * values of P, STDDEV and ov are locale-specific, so if the comma
 * separator specified by the locale is a comma, a comma should be used.
 * A locale-independent way is to specify using scientific notation,
 * e.g. 5e-1 instad of 0.5 or 0,5.
 *
 * \default \c "tukey(0.5)"
 * \param  encoder        An encoder instance to set.
 * \param  specification  See above.
 * \assert
 *    \code encoder != NULL \endcode
 *    \code specification != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_apodization(QLAC__StreamEncoder *encoder, const char *specification);

/** Set the maximum LPC order, or \c 0 to use only the fixed predictors.
 *
 * \default \c 8
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_max_lpc_order(QLAC__StreamEncoder *encoder, uint32_t value);

/** Set the precision, in bits, of the quantized linear predictor
 *  coefficients, or \c 0 to let the encoder select it based on the
 *  blocksize.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_qlp_coeff_precision(QLAC__StreamEncoder *encoder, uint32_t value);

/** Get the descriptor of the most recently processed frame.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval QLAC__Frame *
 *    A pointer to the current frame descriptor, owned by the encoder.
 */
QLAC_API QLAC__Frame * QLAC__stream_encoder_get_frame_descriptor(QLAC__StreamEncoder *encoder);
/** Set to \c false to use only the specified quantized linear predictor
 *  coefficient precision, or \c true to search neighboring precision
 *  values and use the best one.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_do_qlp_coeff_prec_search(QLAC__StreamEncoder *encoder, bool value);

/** Set to \c false to let the encoder estimate the best model order
 *  based on the residual signal energy, or \c true to force the
 *  encoder to evaluate all order models and select the best.
 *
 * \default \c false
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_do_exhaustive_model_search(QLAC__StreamEncoder *encoder, bool value);

/** Set an estimate of the total samples that will be encoded.
 *  This is merely an estimate and may be set to \c 0 if unknown.
 *
 *  \note In this block-encoder fork there is no STREAMINFO, so this value is
 *  stored for the caller's convenience but has no effect on the encoded output.
 *
 * \default \c 0
 * \param  encoder  An encoder instance to set.
 * \param  value    See above.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c false if the encoder is already initialized, else \c true.
 */
QLAC_API bool QLAC__stream_encoder_set_total_samples_estimate(QLAC__StreamEncoder *encoder, QLAC__uint64 value);


/** Get the current encoder state.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval QLAC__StreamEncoderState
 *    The current encoder state.
 */
QLAC_API QLAC__StreamEncoderState QLAC__stream_encoder_get_state(const QLAC__StreamEncoder *encoder);

/** Get the current encoder state as a C string.
 *
 * \param  encoder  A encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval const char *
 *    The encoder state as a C string.  Do not modify the contents.
 */
QLAC_API const char *QLAC__stream_encoder_get_resolved_state_string(const QLAC__StreamEncoder *encoder);

/** Get the <A HREF="https://xiph.org/flac/format.html#subset">Subset</A> flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    See QLAC__stream_encoder_set_streamable_subset().
 */
QLAC_API bool QLAC__stream_encoder_get_streamable_subset(const QLAC__StreamEncoder *encoder);

/** Get the input sample resolution setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval uint32_t
 *    See QLAC__stream_encoder_set_bits_per_sample().
 */
QLAC_API uint32_t QLAC__stream_encoder_get_bits_per_sample(const QLAC__StreamEncoder *encoder);


/** Get the blocksize setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval uint32_t
 *    See QLAC__stream_encoder_set_blocksize().
 */
QLAC_API uint32_t QLAC__stream_encoder_get_blocksize(const QLAC__StreamEncoder *encoder);

/** Get the maximum LPC order setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval uint32_t
 *    See QLAC__stream_encoder_set_max_lpc_order().
 */
QLAC_API uint32_t QLAC__stream_encoder_get_max_lpc_order(const QLAC__StreamEncoder *encoder);

/** Get the quantized linear predictor coefficient precision setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval uint32_t
 *    See QLAC__stream_encoder_set_qlp_coeff_precision().
 */
QLAC_API uint32_t QLAC__stream_encoder_get_qlp_coeff_precision(const QLAC__StreamEncoder *encoder);

/** Get the qlp coefficient precision search flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    See QLAC__stream_encoder_set_do_qlp_coeff_prec_search().
 */
QLAC_API bool QLAC__stream_encoder_get_do_qlp_coeff_prec_search(const QLAC__StreamEncoder *encoder);

/** Get the exhaustive model search flag.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    See QLAC__stream_encoder_set_do_exhaustive_model_search().
 */
QLAC_API bool QLAC__stream_encoder_get_do_exhaustive_model_search(const QLAC__StreamEncoder *encoder);

/** Get the Rice parameter search distance setting.
 *
 * \param  encoder  An encoder instance to query.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval uint32_t
 *    See QLAC__stream_encoder_set_rice_parameter_search_dist().
 */
QLAC_API uint32_t QLAC__stream_encoder_get_rice_parameter_search_dist(const QLAC__StreamEncoder *encoder);

/** Get the previously set estimate of the total samples to be encoded.
 *  The encoder merely mimics back the value given to
 *  QLAC__stream_encoder_set_total_samples_estimate() since it has no
 *  other way of knowing how many samples the client will encode.
 *
 * \param  encoder  An encoder instance to set.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval QLAC__uint64
 *    See QLAC__stream_encoder_get_total_samples_estimate().
 */
QLAC_API QLAC__uint64 QLAC__stream_encoder_get_total_samples_estimate(const QLAC__StreamEncoder *encoder);



/** Initialize the encoder as a block encoder.
 *
 *  This validates the out-of-band parameters set with the
 *  QLAC__stream_encoder_set_*() functions and allocates the internal work
 *  buffers once (the apodization windows are computed here, since the
 *  blocksize is fixed).  After a successful call the encoder is in the
 *  QLAC__STREAM_ENCODER_OK state and ready for
 *  QLAC__stream_encoder_encode_block().
 *
 *  This function should be called after QLAC__stream_encoder_new() and
 *  QLAC__stream_encoder_set_*().
 *
 * \param  encoder            An uninitialized encoder instance.
 * \param  client_data        Opaque value retained by the encoder; not used
 *                            internally in this fork.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval QLAC__StreamEncoderInitStatus
 *    \c QLAC__STREAM_ENCODER_INIT_STATUS_OK if initialization was successful;
 *    see QLAC__StreamEncoderInitStatus for the meanings of other return values.
 */
QLAC_API QLAC__StreamEncoderInitStatus QLAC__stream_encoder_init_stream(QLAC__StreamEncoder *encoder, void *client_data);

/** Finish encoding.  Releases the internal buffers, resets the encoder
 *  settings to their defaults, and returns the encoder to the
 *  QLAC__STREAM_ENCODER_UNINITIALIZED state.  The instance may then be
 *  reconfigured and initialized again, or deleted.
 *
 *  This fork keeps no cross-block state, so there is no partial block to
 *  flush here.
 *
 * \param  encoder  An initialized encoder instance.
 * \assert
 *    \code encoder != NULL \endcode
 * \retval bool
 *    \c true (always succeeds in this fork).
 */
QLAC_API bool QLAC__stream_encoder_finish(QLAC__StreamEncoder *encoder);

/** Number of leading zero samples the caller must place in front of the real
 *  audio samples in the input buffer passed to
 *  QLAC__stream_encoder_encode_block().  These satisfy the negative-index
 *  read-ahead the vectorised LPC routines perform and must be zero.
 */
#define QLAC__STREAM_ENCODER_INPUT_LEADING_PAD 4u

/** Number of QLAC__int32 elements the caller must allocate for the input
 *  buffer: QLAC__STREAM_ENCODER_INPUT_LEADING_PAD + blocksize.
 *
 * \param  encoder  An initialized encoder instance.
 * \retval uint32_t  Required element count.
 */
QLAC_API uint32_t QLAC__stream_encoder_get_input_buffer_size(const QLAC__StreamEncoder *encoder);

/** Byte alignment the input buffer must satisfy (see QLAC/memory.h).
 *
 * \param  encoder  An initialized encoder instance.
 * \retval uint32_t  Required alignment in bytes.
 */
QLAC_API uint32_t QLAC__stream_encoder_get_input_alignment(const QLAC__StreamEncoder *encoder);

/** Number of leading zero-padding samples the input buffer must contain
 *  (== QLAC__STREAM_ENCODER_INPUT_LEADING_PAD).
 *
 * \param  encoder  An initialized encoder instance.
 * \retval uint32_t  Leading pad element count.
 */
QLAC_API uint32_t QLAC__stream_encoder_get_input_leading_pad(const QLAC__StreamEncoder *encoder);

/** Encode exactly one independent block of \a blocksize mono samples and write
 *  the resulting bitstream bytes into the caller-provided \a out buffer.
 *
 *  This is a pure, reentrant block encoder: it keeps no cross-block state.
 *  The caller owns both buffers:
 *  - \a samples points at the first real sample.  The buffer must have
 *    QLAC__STREAM_ENCODER_INPUT_LEADING_PAD zero samples in front of it (at
 *    negative indices) and be aligned to
 *    QLAC__stream_encoder_get_input_alignment() bytes.  Allocate it with the
 *    helpers in QLAC/memory.h.
 *  - WATCHOUT: \a samples may be modified in place (wasted-bits shifting).
 *  - \a out receives the encoded frame bytes; \a out_capacity is its size in
 *    bytes; \a *out_bytes is set to the number of bytes written.
 *
 * \param  encoder       An initialized encoder instance in the OK state.
 * \param  samples       Caller-owned input samples (see contract above).
 * \param  out           Caller-owned output byte buffer.
 * \param  out_capacity  Size of \a out in bytes.
 * \param  out_bytes     Receives the number of bytes written to \a out.
 * \retval bool
 *    \c true on success, else \c false; check QLAC__stream_encoder_get_state().
 */
QLAC_API bool QLAC__stream_encoder_encode_block(QLAC__StreamEncoder *encoder, QLAC__int32 *samples, QLAC__byte *out, size_t out_capacity, size_t *out_bytes);

/* \} */
QLAC_API bool QLAC__stream_encoder_disable_instruction_set(QLAC__StreamEncoder *encoder, bool value);

#ifdef __cplusplus
}
#endif

#endif
