cmake_minimum_required(VERSION 3.20)

project(GameStorageManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 COMPONENTS Widgets Concurrent QUIET)

add_library(gsm_core STATIC
    core/scanner/SteamScanner.cpp
    core/analyzer/GameAnalyzer.cpp
    core/compressor/CompressionTask.cpp
    core/rules_engine/RecommendationEngine.cpp
    core/safety/SafetyMetadataStore.cpp
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

if(WIN32)
    target_link_libraries(gsm_core PUBLIC advapi32)
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
        ui/controllers/CompressionController.cpp
        ui/controllers/CompressionController.h
        ui/views/MainWindow.cpp
        ui/views/MainWindow.h
    )

    target_link_libraries(gsm_app
        PRIVATE
            gsm_core
            Qt6::Widgets
            Qt6::Concurrent
    )

    set_target_properties(gsm_app PROPERTIES
        OUTPUT_NAME GameStorageManager
    )

    if(MSVC)
        target_compile_options(gsm_app PRIVATE /W4)
    else()
        target_compile_options(gsm_app PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32)
        find_program(WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            HINTS
                "${Qt6_DIR}/../../../bin"
                "C:/Qt/6.8.3/mingw_64/bin"
        )

        if(WINDEPLOYQT_EXECUTABLE)
            set(GSM_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy/GameStorageManager")

            add_custom_target(gsm_app_deploy
                COMMAND ${CMAKE_COMMAND} -E remove_directory "${GSM_DEPLOY_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${GSM_DEPLOY_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:gsm_app>" "${GSM_DEPLOY_DIR}/$<TARGET_FILE_NAME:gsm_app>"
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-opengl-sw "${GSM_DEPLOY_DIR}/$<TARGET_FILE_NAME:gsm_app>"
                DEPENDS gsm_app
                COMMENT "Deploying GameStorageManager runtime files"
                VERBATIM
            )
        endif()
    endif()
endif()

enable_testing()

add_executable(gsm_core_tests
    tests/core_tests.cpp
)

target_link_libraries(gsm_core_tests PRIVATE gsm_core)

add_test(NAME gsm_core_tests COMMAND gsm_core_tests)
