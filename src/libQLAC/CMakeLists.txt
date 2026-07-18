# Part of QLAC; derived from libFLAC 1.5.0. See FORK_NOTES.md and LICENSE.
option(WITH_ASM "Use any assembly optimization routines" ON)
option(ENABLE_64_BIT_WORDS "Set QLAC__BYTES_PER_WORD to 8, for 64-bit machines" ON)

check_include_file("cpuid.h" HAVE_CPUID_H)
check_include_file("sys/param.h" HAVE_SYS_PARAM_H)

set(CMAKE_REQUIRED_LIBRARIES m)
check_function_exists(lround HAVE_LROUND)

include(CheckCSourceCompiles)
include(CheckCPUArch)
include(CheckA64NEON)

check_cpu_arch_x64(QLAC__CPU_X86_64)
if(NOT QLAC__CPU_X86_64)
    check_cpu_arch_x86(QLAC__CPU_IA32)
endif()

if(QLAC__CPU_X86_64 OR QLAC__CPU_IA32)
    set(QLAC__ALIGN_MALLOC_DATA 1)
    option(WITH_AVX "Enable AVX, AVX2 optimizations (with runtime detection, resulting binary does not require AVX2, so only necessary when a compiler doesn't know about AVX)" ON)
    if(WITH_AVX AND MSVC)
        set_source_files_properties(fixed_intrin_avx2.c lpc_intrin_avx2.c lpc_intrin_fma.c PROPERTIES COMPILE_FLAGS /arch:AVX2)
    endif()
else()
    check_cpu_arch_arm64(QLAC__CPU_ARM64)
    if(QLAC__CPU_ARM64)
         check_a64neon(QLAC__HAS_A64NEONINTRIN)
    endif()
endif()

if(NOT WITH_ASM)
    add_definitions(-DQLAC__NO_ASM)
endif()

include_directories("include")

add_library(QLAC
    bitmath.c
    bitreader.c
    bitwriter.c
    cpu.c
    fixed.c
    fixed_intrin_sse2.c
    fixed_intrin_ssse3.c
    fixed_intrin_sse42.c
    fixed_intrin_avx2.c
    float.c
    format.c
    lpc.c
    lpc_intrin_neon.c
    lpc_intrin_sse2.c
    lpc_intrin_sse41.c
    lpc_intrin_avx2.c
    lpc_intrin_fma.c
    memory.c
    stream_decoder.c
    stream_encoder.c
    stream_encoder_framing.c
    version.rc
    window.c
    $<$<BOOL:${WIN32}>:../../include/share/win_utf8_io.h>
    $<$<BOOL:${WIN32}>:../share/win_utf8_io/win_utf8_io.c>)
set_property(TARGET QLAC PROPERTY PROJECT_LABEL "libQLAC")
if(TARGET QLAC-asm)
    target_sources(QLAC PRIVATE $<TARGET_OBJECTS:QLAC-asm>)
endif()

target_compile_definitions(QLAC
    PRIVATE $<$<BOOL:${BUILD_SHARED_LIBS}>:QLAC_API_EXPORTS>
    PUBLIC $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:QLAC__NO_DLL>)
if(NOT WIN32)
    target_compile_definitions(QLAC PRIVATE $<$<BOOL:${BUILD_SHARED_LIBS}>:QLAC__USE_VISIBILITY_ATTR>)
endif()
target_include_directories(QLAC INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(QLAC PUBLIC $<$<BOOL:${HAVE_LROUND}>:m>)
if(BUILD_SHARED_LIBS)
    set_target_properties(QLAC PROPERTIES
        VERSION 14.0.0
        SOVERSION 14)
    if(NOT WIN32)
        set_target_properties(QLAC PROPERTIES C_VISIBILITY_PRESET hidden)
    endif()
endif()

check_c_compiler_flag("-fassociative-math -fno-signed-zeros -fno-trapping-math -freciprocal-math" HAVE_ASSOC_MATH)

if(MSVC)
        target_compile_options(QLAC BEFORE PRIVATE "/fp:fast")
else()
    if(HAVE_ASSOC_MATH)
        target_compile_options(QLAC BEFORE PRIVATE -fassociative-math -fno-signed-zeros -fno-trapping-math -freciprocal-math)
    endif()
endif()

add_library(QLAC::QLAC ALIAS QLAC)

install(TARGETS QLAC EXPORT targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/")

if(INSTALL_PKGCONFIG_MODULES)
    set(prefix "${CMAKE_INSTALL_PREFIX}")
    set(exec_prefix "${CMAKE_INSTALL_PREFIX}")
    set(libdir "${CMAKE_INSTALL_FULL_LIBDIR}")
    set(includedir "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
    configure_file(qlac.pc.in qlac.pc @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qlac.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()
