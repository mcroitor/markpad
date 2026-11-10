find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(markpad_md4c_spike
    source_document_test.cc
)
set_target_properties(markpad_md4c_spike PROPERTIES AUTOMOC ON)
target_link_libraries(markpad_md4c_spike PRIVATE markpad_markdown Qt6::Test)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(markpad_md4c_spike PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME markpad_md4c_source_round_trip COMMAND markpad_md4c_spike -o -,txt)
set_tests_properties(markpad_md4c_source_round_trip PROPERTIES LABELS "spike;md4c")
