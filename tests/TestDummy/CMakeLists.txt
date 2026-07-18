# Part of QLAC. Copyright (C) 2025 Leonardo Severi. BSD-3-Clause; see COPYING.QLAC.
add_executable(TestDummy dummy.cpp)
target_link_libraries(TestDummy QLAC)
set_target_properties(TestDummy PROPERTIES CXX_STANDARD 20)