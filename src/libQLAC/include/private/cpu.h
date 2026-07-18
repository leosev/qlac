/* QLAC — a low-latency lossless audio codec
 *
 * This file is part of QLAC and is derived from libFLAC 1.5.0.  The changes
 * from upstream are mechanical only: FLAC__*/FLAC_* identifiers and include
 * paths were renamed to their QLAC equivalents.  The original libFLAC copyright
 * and license below are retained and continue to govern this file.  See
 * FORK_NOTES.md and LICENSE for details.
 *
 * libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2001-2009  Josh Coalson
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

#ifndef QLAC__PRIVATE__CPU_H
#define QLAC__PRIVATE__CPU_H

#include "QLAC/ordinals.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef QLAC__CPU_X86_64

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
#define QLAC__CPU_X86_64
#endif

#endif

#ifndef QLAC__CPU_IA32

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) ||defined( __i386) || defined(_M_IX86)
#define QLAC__CPU_IA32
#endif

#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if QLAC__HAS_X86INTRIN
/* SSE intrinsics support by ICC/MSVC/GCC */
#if defined __INTEL_COMPILER
  #define QLAC__SSE_TARGET(x)
  #define QLAC__SSE_SUPPORTED 1
  #define QLAC__SSE2_SUPPORTED 1
  #if (__INTEL_COMPILER >= 1000) /* Intel C++ Compiler 10.0 */
    #define QLAC__SSSE3_SUPPORTED 1
    #define QLAC__SSE4_1_SUPPORTED 1
    #define QLAC__SSE4_2_SUPPORTED 1
  #endif
  #ifdef QLAC__USE_AVX
    #if (__INTEL_COMPILER >= 1110) /* Intel C++ Compiler 11.1 */
      #define QLAC__AVX_SUPPORTED 1
    #endif
    #if (__INTEL_COMPILER >= 1300) /* Intel C++ Compiler 13.0 */
      #define QLAC__AVX2_SUPPORTED 1
      #define QLAC__FMA_SUPPORTED 1
    #endif
  #endif
#elif defined __clang__ && __has_attribute(__target__) /* clang */
  #define QLAC__SSE_TARGET(x) __attribute__ ((__target__ (x)))
  #define QLAC__SSE_SUPPORTED 1
  #define QLAC__SSE2_SUPPORTED 1
  #define QLAC__SSSE3_SUPPORTED 1
  #define QLAC__SSE4_1_SUPPORTED 1
  #define QLAC__SSE4_2_SUPPORTED 1
  #ifdef QLAC__USE_AVX
    #define QLAC__AVX_SUPPORTED 1
    #define QLAC__AVX2_SUPPORTED 1
    #define QLAC__FMA_SUPPORTED 1
    #define QLAC__BMI2_SUPPORTED 1
  #endif
#elif defined __GNUC__ && !defined __clang__ && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9)) /* GCC 4.9+ */
  #define QLAC__SSE_TARGET(x) __attribute__ ((__target__ (x)))
  #define QLAC__SSE_SUPPORTED 1
  #define QLAC__SSE2_SUPPORTED 1
  #define QLAC__SSSE3_SUPPORTED 1
  #define QLAC__SSE4_1_SUPPORTED 1
  #define QLAC__SSE4_2_SUPPORTED 1
  #ifdef QLAC__USE_AVX
    #define QLAC__AVX_SUPPORTED 1
    #define QLAC__AVX2_SUPPORTED 1
    #define QLAC__FMA_SUPPORTED 1
    #define QLAC__BMI2_SUPPORTED 1
  #endif
#elif defined _MSC_VER
  #define QLAC__SSE_TARGET(x)
  #define QLAC__SSE_SUPPORTED 1
  #define QLAC__SSE2_SUPPORTED 1
  #if (_MSC_VER >= 1500) /* MS Visual Studio 2008 */
    #define QLAC__SSSE3_SUPPORTED 1
    #define QLAC__SSE4_1_SUPPORTED 1
    #define QLAC__SSE4_2_SUPPORTED 1
  #endif
  #ifdef QLAC__USE_AVX
    #if (_MSC_FULL_VER >= 160040219) /* MS Visual Studio 2010 SP1 */
      #define QLAC__AVX_SUPPORTED 1
    #endif
    #if (_MSC_VER >= 1700) /* MS Visual Studio 2012 */
      #define QLAC__AVX2_SUPPORTED 1
      #define QLAC__FMA_SUPPORTED 1
    #endif
  #endif
#else
  #define QLAC__SSE_TARGET(x)
  #ifdef __SSE__
    #define QLAC__SSE_SUPPORTED 1
  #endif
  #ifdef __SSE2__
    #define QLAC__SSE2_SUPPORTED 1
  #endif
  #ifdef __SSSE3__
    #define QLAC__SSSE3_SUPPORTED 1
  #endif
  #ifdef __SSE4_1__
    #define QLAC__SSE4_1_SUPPORTED 1
  #endif
  #ifdef __SSE4_2__
    #define QLAC__SSE4_2_SUPPORTED 1
  #endif
  #ifdef QLAC__USE_AVX
    #ifdef __AVX__
      #define QLAC__AVX_SUPPORTED 1
    #endif
    #ifdef __AVX2__
      #define QLAC__AVX2_SUPPORTED 1
    #endif
    #ifdef __FMA__
      #define QLAC__FMA_SUPPORTED 1
    #endif
  #endif
#endif /* compiler version */
#endif /* intrinsics support */


#ifndef QLAC__AVX_SUPPORTED
#define QLAC__AVX_SUPPORTED 0
#endif

typedef enum {
	QLAC__CPUINFO_TYPE_IA32,
	QLAC__CPUINFO_TYPE_X86_64,
	QLAC__CPUINFO_TYPE_UNKNOWN
} QLAC__CPUInfo_Type;

typedef struct {
	bool intel;

	bool cmov;
	bool mmx;
	bool sse;
	bool sse2;

	bool sse3;
	bool ssse3;
	bool sse41;
	bool sse42;
	bool avx;
	bool avx2;
	bool fma;
	bool bmi2;
} QLAC__CPUInfo_x86;

typedef struct {
	bool use_asm;
	QLAC__CPUInfo_Type type;
	QLAC__CPUInfo_x86 x86;
} QLAC__CPUInfo;

void QLAC__cpu_info(QLAC__CPUInfo *info);

QLAC__uint32 QLAC__cpu_have_cpuid_asm_ia32(void);

void         QLAC__cpu_info_asm_ia32(QLAC__uint32 level, QLAC__uint32 *eax, QLAC__uint32 *ebx, QLAC__uint32 *ecx, QLAC__uint32 *edx);

#endif
