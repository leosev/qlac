# Part of QLAC. Copyright (C) 2025 Leonardo Severi. BSD-3-Clause; see COPYING.QLAC.
# libsndfile is provided by the system (libsndfile-dev) via pkg-config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

add_executable(Assessment assessment.cpp)
#target_compile_definitions(Assessment PRIVATE DO_VERIFY)
target_link_libraries(Assessment QLAC argparse::argparse PkgConfig::SNDFILE)
set_target_properties(Assessment PROPERTIES CXX_STANDARD 20)
