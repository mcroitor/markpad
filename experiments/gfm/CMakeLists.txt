enable_language(C)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMARK_TESTS OFF)
set(CMARK_STATIC ON)
set(CMARK_SHARED OFF)
set(CMARK_LIB_FUZZER OFF)
set(CMARK_FUZZ_QUADRATIC OFF)

FetchContent_Declare(cmark_gfm
    URL https://codeload.github.com/github/cmark-gfm/tar.gz/27d942c8b0a62d192f616e5bf3578f4b6a89e180
    URL_HASH SHA256=f8710b90b90a503240c920c6922367e700bb9895bb02018d6448d2f812fc7968
    TLS_VERIFY ON
)
FetchContent_MakeAvailable(cmark_gfm)
set_target_properties(cmark-gfm PROPERTIES EXCLUDE_FROM_ALL TRUE)

find_package(Qt6 REQUIRED COMPONENTS Test Gui)

add_executable(markpad_gfm_spike round_trip_test.cc)
set_target_properties(markpad_gfm_spike PROPERTIES AUTOMOC ON)
target_include_directories(markpad_gfm_spike PRIVATE
    ${cmark_gfm_SOURCE_DIR}/src
    ${cmark_gfm_SOURCE_DIR}/extensions
    ${cmark_gfm_BINARY_DIR}/src
)
target_compile_definitions(markpad_gfm_spike PRIVATE
    CMARK_GFM_STATIC_DEFINE
    CMARK_GFM_EXTENSIONS_STATIC_DEFINE
)
target_link_libraries(markpad_gfm_spike PRIVATE
    libcmark-gfm-extensions_static
    libcmark-gfm_static
    Qt6::Test
    Qt6::Gui
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(markpad_gfm_spike PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME markpad_gfm_round_trip COMMAND markpad_gfm_spike -o -,txt)
set_tests_properties(markpad_gfm_round_trip PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "spike"
)
