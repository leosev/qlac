/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0.  The changes
 * from upstream are mechanical only: FLAC__*/FLAC_* identifiers and include
 * paths were renamed to their QLAC equivalents.  The original libFLAC copyright
 * and license below are retained and continue to govern this file.  See
 * FORK_NOTES.md and LICENSE for details.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2000-2009  Josh Coalson
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

#ifndef QLAC__EXPORT_H
#define QLAC__EXPORT_H

/** \file include/QLAC/export.h
 *
 *  \brief
 *  This module contains \#defines and symbols for exporting function
 *  calls, and providing version information and compiled-in features.
 *
 *  See the \link QLAC_export export \endlink module.
 */

/** \defgroup QLAC_export QLAC/export.h: export symbols
 *  \ingroup flac
 *
 *  \brief
 *  This module contains \#defines and symbols for exporting function
 *  calls, and providing version information and compiled-in features.
 *
 *  If you are compiling for Windows (with Visual Studio or MinGW for
 *  example) and will link to the static library (libFLAC++.lib) you
 *  should define QLAC__NO_DLL in your project to make sure the symbols
 *  are exported properly.
 *
 * \{
 */

/** This \#define is used internally in libFLAC and its headers to make
 * sure the correct symbols are exported when working with shared
 * libraries. On Windows, this \#define is set to __declspec(dllexport)
 * when compiling libFLAC into a library and to __declspec(dllimport)
 * when the headers are used to link to that DLL. On non-Windows systems
 * it is used to set symbol visibility.
 *
 * Because of this, the define QLAC__NO_DLL must be defined when linking
 * to libFLAC statically or linking will fail.
 */
/* This has grown quite complicated. QLAC__NO_DLL is used by MSVC sln
 * files and CMake, which build either static or shared. autotools can
 * build static, shared or **both**. Therefore, DLL_EXPORT, which is set
 * by libtool, must override QLAC__NO_DLL on building shared components
 */
#if defined(_WIN32)

#if defined(QLAC__NO_DLL) && !(defined(DLL_EXPORT))
#define QLAC_API
#else
#ifdef QLAC_API_EXPORTS
#define	QLAC_API __declspec(dllexport)
#else
#define QLAC_API __declspec(dllimport)
#endif
#endif

#elif defined(QLAC__USE_VISIBILITY_ATTR)
#define QLAC_API __attribute__ ((visibility ("default")))

#else
#define QLAC_API

#endif

/** These \#defines will mirror the libtool-based library version number, see
 * http://www.gnu.org/software/libtool/manual/libtool.html#Libtool-versioning
 */
#define QLAC_API_VERSION_CURRENT 14
#define QLAC_API_VERSION_REVISION 0 /**< see above */
#define QLAC_API_VERSION_AGE 0 /**< see above */

#ifdef __cplusplus
extern "C" {
#endif

/** \c 1 if the library has been compiled with support for Ogg FLAC, else \c 0. */
extern QLAC_API int QLAC_API_SUPPORTS_OGG_FLAC;

#ifdef __cplusplus
}
#endif

/* \} */

#endif
