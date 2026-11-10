cmake_minimum_required(VERSION 3.21)

project(Markpad VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
include(cmake/md4c.cmake)
configure_file(lang/en.ini "${CMAKE_BINARY_DIR}/lang/en.ini" COPYONLY)

add_library(markpad_markdown src/markdown_document.cc src/markdown_document.h)
target_include_directories(markpad_markdown PUBLIC src)
target_link_libraries(markpad_markdown PUBLIC Qt6::Core md4c)
set_target_properties(markpad_markdown PROPERTIES AUTOMOC OFF)

add_library(markpad_document
    src/document_session.cc
    src/document_session.h
)
target_include_directories(markpad_document PUBLIC src)
target_compile_features(markpad_document PUBLIC cxx_std_20)
set_target_properties(markpad_document PROPERTIES AUTOMOC OFF)

add_library(markpad_files
    src/qt_document_file_service.cc
    src/qt_document_file_service.h
)
target_link_libraries(markpad_files PUBLIC markpad_document PRIVATE Qt6::Core)

add_library(markpad_export
    src/html_export.cc
    src/html_export.h
    src/qt_html_export_service.cc
    src/qt_html_export_service.h
)
target_include_directories(markpad_export PUBLIC src)
target_link_libraries(markpad_export PUBLIC markpad_markdown Qt6::Core)
target_compile_features(markpad_export PUBLIC cxx_std_20)

add_library(markpad_settings
    src/settings.cc
    src/settings.h
)
target_include_directories(markpad_settings PUBLIC src)
target_link_libraries(markpad_settings PUBLIC Qt6::Core Qt6::Gui)
target_compile_features(markpad_settings PUBLIC cxx_std_20)

add_library(markpad_layout
    src/page_layout.cc
    src/page_layout.h
)
target_include_directories(markpad_layout PUBLIC src)
target_link_libraries(markpad_layout PUBLIC markpad_settings Qt6::Gui)
target_compile_features(markpad_layout PUBLIC cxx_std_20)

add_library(markpad_ui
    src/application_controller.cc
    src/application_controller.h
    src/document_dialogs.cc
    src/document_dialogs.h
    src/markdown_highlighter.cc
    src/markdown_highlighter.h
    src/main_window.cc
    src/main_window.h
    src/continuous_editor.cc
    src/continuous_editor.h
    src/page_text_edit.cc
    src/page_text_edit.h
    src/settings_dialog.cc
    src/settings_dialog.h
)
target_include_directories(markpad_ui PUBLIC src)
target_link_libraries(markpad_ui PUBLIC Qt6::Widgets markpad_files markpad_markdown
    markpad_export markpad_settings markpad_layout)
target_compile_features(markpad_ui PUBLIC cxx_std_20)

add_executable(markpad
    src/main.cc
)
target_link_libraries(markpad PRIVATE markpad_ui)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(markpad_markdown PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad_document PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad_files PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad_export PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad_settings PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad_layout PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad_ui PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(markpad PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

option(MARKPAD_BUILD_GFM_SPIKE "Build the isolated Task 002 GFM experiment" OFF)
option(MARKPAD_BUILD_MD4C_SPIKE "Build the isolated Task 002 MD4C experiment" OFF)
if((MARKPAD_BUILD_GFM_SPIKE OR MARKPAD_BUILD_MD4C_SPIKE) AND NOT BUILD_TESTING)
    message(FATAL_ERROR "Markdown spikes require BUILD_TESTING=ON")
endif()
if(MARKPAD_BUILD_GFM_SPIKE)
    add_subdirectory(experiments/gfm)
endif()
if(MARKPAD_BUILD_MD4C_SPIKE)
    add_subdirectory(experiments/md4c)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(markpad_document_tests tests/document_session_test.cc)
    target_link_libraries(markpad_document_tests PRIVATE markpad_files Qt6::Test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(markpad_document_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME markpad_document COMMAND markpad_document_tests)

    add_executable(markpad_export_tests tests/html_export_test.cc)
    target_link_libraries(markpad_export_tests PRIVATE markpad_export Qt6::Test)
    target_compile_features(markpad_export_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(markpad_export_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME markpad_html_export COMMAND markpad_export_tests)

    add_executable(markpad_settings_tests tests/settings_test.cc)
    target_link_libraries(markpad_settings_tests PRIVATE markpad_settings
        Qt6::Test)
    target_compile_features(markpad_settings_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(markpad_settings_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME markpad_settings COMMAND markpad_settings_tests)
    set_tests_properties(markpad_settings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(markpad_layout_tests tests/page_layout_test.cc)
    target_link_libraries(markpad_layout_tests PRIVATE markpad_layout Qt6::Test)
    target_compile_features(markpad_layout_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(markpad_layout_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME markpad_layout COMMAND markpad_layout_tests)
    set_tests_properties(markpad_layout PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(markpad_tests
        tests/smoke_test.cc
    )
    target_link_libraries(markpad_tests PRIVATE markpad_ui Qt6::Test)
    target_compile_features(markpad_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(markpad_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME markpad_smoke COMMAND markpad_tests)
    set_tests_properties(markpad_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
