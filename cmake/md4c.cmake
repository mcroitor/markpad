enable_language(C)
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(BUILD_SHARED_LIBS OFF)
set(BUILD_HTML_LIBRARY OFF)
set(BUILD_MD2HTML_EXECUTABLE OFF)
FetchContent_Declare(md4c
    URL https://codeload.github.com/mity/md4c/tar.gz/f9e5eb232b95b737373ae706648cb75bd259ed1a
    URL_HASH SHA256=61a3fe09f1c325d738777fb08cf959162bacda5888de0f7578e82a39df473780
    TLS_VERIFY ON
)
FetchContent_MakeAvailable(md4c)
set_target_properties(md4c PROPERTIES AUTOMOC OFF)
configure_file("${md4c_SOURCE_DIR}/LICENSE.md"
    "${CMAKE_CURRENT_BINARY_DIR}/MD4C-LICENSE.md" COPYONLY)
