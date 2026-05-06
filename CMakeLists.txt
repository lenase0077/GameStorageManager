cmake_minimum_required(VERSION 3.20)

project(GameStorageManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 COMPONENTS Widgets Concurrent QUIET)

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

if(MINGW)
    target_link_options(gsm_core PUBLIC -static-libgcc -static-libstdc++)
endif()

add_executable(gsm_cli
    app/cli/main.cpp
)

target_link_libraries(gsm_cli PRIVATE gsm_core)

if(Qt6_FOUND)
    add_executable(gsm_app
        app/gui/main.cpp
        ui/controllers/AnalysisController.cpp
        ui/controllers/AnalysisController.h
        ui/views/MainWindow.cpp
        ui/views/MainWindow.h
    )

    target_link_libraries(gsm_app
        PRIVATE
            gsm_core
            Qt6::Widgets
            Qt6::Concurrent
    )

    if(MSVC)
        target_compile_options(gsm_app PRIVATE /W4)
    else()
        target_compile_options(gsm_app PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

enable_testing()

add_executable(gsm_core_tests
    tests/core_tests.cpp
)

target_link_libraries(gsm_core_tests PRIVATE gsm_core)

add_test(NAME gsm_core_tests COMMAND gsm_core_tests)
