/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0, then
 * substantially modified for QLAC (single-block, header-free operation).  The
 * original libFLAC copyright and license below are retained and govern the
 * FLAC-derived portions; modifications for QLAC are covered by the notice
 * below.  See FORK_NOTES.md and LICENSE.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2001-2009  Josh Coalson
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

#ifndef QLAC__MEMORY_H
#define QLAC__MEMORY_H

#include <stdlib.h> /* for size_t */

#include "export.h"
#include "ordinals.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file include/QLAC/memory.h
 *
 *  \brief
 *  Aligned-allocation helpers exposed so that callers of the block encoder
 *  can allocate the sample and residual buffers the library requires with
 *  the correct SIMD alignment.
 *
 *  This fork's block encoder no longer owns the audio-sample buffer: the
 *  caller supplies it (see QLAC__stream_encoder_encode_block()).  Because the
 *  vectorised LPC / autocorrelation routines require the integer signal to be
 *  aligned (and to have a small zero-padded lead-in), the caller must honour
 *  the library's alignment contract.  These functions let the caller obtain
 *  conforming storage without hard-coding the internal constants; query the
 *  exact element count and lead-in from the encoder
 *  (QLAC__stream_encoder_get_input_buffer_size() and friends) and allocate it
 *  here.
 */

/** The alignment, in bytes, that QLAC__memory_alloc_aligned() guarantees for
 *  the returned aligned address.  Buffers passed to the block encoder must be
 *  aligned to at least this boundary.
 */
#define QLAC__MEMORY_ALIGNMENT 32u

/** Allocate \a bytes of memory, returning both the raw (free()-able) pointer
 *  and an aligned address within that allocation.
 *
 * \param  bytes            Number of bytes to allocate.
 * \param  aligned_address  Receives the aligned address to use.
 * \retval void*
 *    The unaligned pointer to pass to free(), or \c NULL on failure.
 */
QLAC_API void *QLAC__memory_alloc_aligned(size_t bytes, void **aligned_address);

/** Allocate an aligned array of \a elements QLAC__int32.  On success the
 *  previous contents of \a *unaligned_pointer (if non-NULL) are freed.
 *
 * \param  elements           Number of int32 elements.
 * \param  unaligned_pointer  In/out: the raw free()-able pointer.
 * \param  aligned_pointer    Out: the aligned pointer to use.
 * \retval bool         \c true on success, \c false on failure.
 */
QLAC_API bool QLAC__memory_alloc_aligned_int32_array(size_t elements, QLAC__int32 **unaligned_pointer, QLAC__int32 **aligned_pointer);

/** Allocate an aligned array of \a elements QLAC__int64.  See
 *  QLAC__memory_alloc_aligned_int32_array().
 */
QLAC_API bool QLAC__memory_alloc_aligned_int64_array(size_t elements, QLAC__int64 **unaligned_pointer, QLAC__int64 **aligned_pointer);

#ifdef __cplusplus
}
#endif

#endif
