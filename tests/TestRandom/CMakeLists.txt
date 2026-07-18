# Part of QLAC. Copyright (C) 2025 Leonardo Severi. BSD-3-Clause; see COPYING.QLAC.
add_executable(TestRandom random_blocks.cpp)
target_link_libraries(TestRandom QLAC)
set_target_properties(TestRandom PROPERTIES CXX_STANDARD 20)
