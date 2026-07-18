# Part of QLAC. Copyright (C) 2025 Leonardo Severi. BSD-3-Clause; see COPYING.QLAC.
add_executable(TestPatterns patterns.cpp)
target_link_libraries(TestPatterns QLAC)
set_target_properties(TestPatterns PROPERTIES CXX_STANDARD 20)
