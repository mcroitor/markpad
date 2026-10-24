enable_language(C)
include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_AUTOMOC OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_HTML_LIBRARY OFF)
set(BUILD_MD2HTML_EXECUTABLE OFF)
FetchContent_Declare(md4c
    URL https://codeload.github.com/mity/md4c/tar.gz/f9e5eb232b95b737373ae706648cb75bd259ed1a
    URL_HASH SHA256=61a3fe09f1c325d738777fb08cf959162bacda5888de0f7578e82a39df473780
    TLS_VERIFY ON
)
FetchContent_MakeAvailable(md4c)

find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(markpad_md4c_spike
    source_document.h
    source_document.cc
    source_document_test.cc
)
set_target_properties(markpad_md4c_spike PROPERTIES AUTOMOC ON)
target_link_libraries(markpad_md4c_spike PRIVATE md4c Qt6::Test)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(markpad_md4c_spike PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME markpad_md4c_source_round_trip COMMAND markpad_md4c_spike -o -,txt)
set_tests_properties(markpad_md4c_source_round_trip PROPERTIES LABELS "spike;md4c")
