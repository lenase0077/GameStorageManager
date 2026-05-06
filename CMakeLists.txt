cmake_minimum_required(VERSION 3.20)

project(GameStorageManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gsm_core STATIC
    core/analyzer/GameAnalyzer.cpp
    core/rules_engine/RecommendationEngine.cpp
    system/filesystem/Filesystem.cpp
    system/process/CompactProcessAdapter.cpp
)

target_include_directories(gsm_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(gsm_core PUBLIC stdc++fs)
endif()

if(MSVC)
    target_compile_options(gsm_core PRIVATE /W4)
else()
    target_compile_options(gsm_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gsm_cli
    app/cli/main.cpp
)

target_link_libraries(gsm_cli PRIVATE gsm_core)

enable_testing()

add_executable(gsm_core_tests
    tests/core_tests.cpp
)

target_link_libraries(gsm_core_tests PRIVATE gsm_core)

add_test(NAME gsm_core_tests COMMAND gsm_core_tests)
