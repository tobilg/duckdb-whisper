cmake_minimum_required(VERSION 3.14)

# Set extension name here
set(TARGET_NAME whisper)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME} VERSION 0.1.0)

# Set extension version
set(EXT_VERSION_WHISPER "v${PROJECT_VERSION}")
add_definitions(-DEXT_VERSION_WHISPER="${EXT_VERSION_WHISPER}")

# Option to enable/disable audio recording (requires SDL2)
option(WHISPER_ENABLE_RECORDING "Enable audio recording via SDL2" ON)

# Option to enable/disable voice-to-SQL feature (enabled by default)
option(WHISPER_ENABLE_VOICE_QUERY "Enable voice-to-SQL feature" ON)

# Find FFmpeg via pkg-config (installed via vcpkg or system)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libswresample
    libavutil
)

# Find SDL2 if recording is enabled
# Try CMake find_package first (vcpkg), then fall back to pkg-config (system packages)
if(WHISPER_ENABLE_RECORDING)
    find_package(SDL2 QUIET)
    if(TARGET SDL2::SDL2)
        message(STATUS "SDL2 found via CMake - audio recording enabled")
        add_definitions(-DWHISPER_ENABLE_RECORDING)
        set(SDL2_LINK_TARGET SDL2::SDL2)
    else()
        # Try pkg-config for system-installed SDL2
        pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
        if(SDL2_FOUND)
            message(STATUS "SDL2 found via pkg-config - audio recording enabled")
            add_definitions(-DWHISPER_ENABLE_RECORDING)
            set(SDL2_LINK_TARGET PkgConfig::SDL2)
        else()
            message(WARNING "SDL2 not found - audio recording disabled. Install SDL2 or set -DWHISPER_ENABLE_RECORDING=OFF")
            set(WHISPER_ENABLE_RECORDING OFF)
        endif()
    endif()
endif()

# Find libcurl for voice-to-SQL if enabled
set(WHISPER_VOICE_QUERY_ENABLED OFF)
if(WHISPER_ENABLE_VOICE_QUERY)
    find_package(CURL REQUIRED)
    if(CURL_FOUND)
        message(STATUS "libcurl found - voice-to-SQL enabled")
        add_definitions(-DWHISPER_ENABLE_VOICE_QUERY)
        set(WHISPER_VOICE_QUERY_ENABLED ON)
    else()
        message(WARNING "libcurl not found - disabling voice-to-SQL")
    endif()
endif()

# Configure whisper.cpp build options
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)

# Enable GPU acceleration on macOS (Metal)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
    set(GGML_ACCELERATE ON CACHE BOOL "" FORCE)
else()
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
endif()
set(GGML_BLAS OFF CACHE BOOL "" FORCE)

# Apply patches to whisper.cpp before building
if(APPLE)
    execute_process(
        COMMAND git apply --check ${CMAKE_CURRENT_SOURCE_DIR}/patches/ggml-metal-disable-rsets-assert.patch
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp
        RESULT_VARIABLE PATCH_CHECK_RESULT
        ERROR_QUIET
    )
    if(PATCH_CHECK_RESULT EQUAL 0)
        execute_process(
            COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/ggml-metal-disable-rsets-assert.patch
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp
        )
        message(STATUS "Applied ggml-metal rsets assertion patch")
    else()
        message(STATUS "ggml-metal rsets assertion patch already applied or not needed")
    endif()
endif()

# Add whisper.cpp subdirectory
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

include_directories(
    src/include
    third_party/whisper.cpp/include
    third_party/whisper.cpp/ggml/include
)

set(EXTENSION_SOURCES
    src/whisper_extension.cpp
    src/audio_utils.cpp
    src/whisper_config.cpp
    src/model_manager.cpp
    src/whisper_context.cpp
    src/transcription_engine.cpp
    src/functions/model_functions.cpp
    src/functions/transcribe_scalar.cpp
    src/functions/transcribe_table.cpp
    src/functions/utility_functions.cpp
)

# Add recording sources if enabled
if(WHISPER_ENABLE_RECORDING)
    list(APPEND EXTENSION_SOURCES
        src/audio_recorder.cpp
        src/functions/record_functions.cpp
    )
endif()

# Add voice query sources if enabled (requires both recording and curl)
if(WHISPER_VOICE_QUERY_ENABLED AND WHISPER_ENABLE_RECORDING)
    list(APPEND EXTENSION_SOURCES
        src/voice_query/http_client.cpp
        src/voice_query/ddl_extractor.cpp
        src/voice_query/voice_to_sql_function.cpp
        src/voice_query/voice_query_function.cpp
    )
else()
    if(NOT WHISPER_DISABLE_VOICE_QUERY AND NOT WHISPER_ENABLE_RECORDING)
        message(WARNING "Voice query requires recording support - disabling voice-to-SQL")
    endif()
endif()

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link dependencies for static extension
target_link_libraries(${EXTENSION_NAME}
    whisper
    PkgConfig::LIBAV
)

# Link dependencies for loadable extension
target_link_libraries(${LOADABLE_EXTENSION_NAME}
    whisper
    PkgConfig::LIBAV
)

# Link SDL2 if recording is enabled
if(WHISPER_ENABLE_RECORDING)
    target_link_libraries(${EXTENSION_NAME} ${SDL2_LINK_TARGET})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${SDL2_LINK_TARGET})
endif()

# Link libcurl if voice query is enabled
if(WHISPER_VOICE_QUERY_ENABLED AND WHISPER_ENABLE_RECORDING)
    target_link_libraries(${EXTENSION_NAME} CURL::libcurl)
    target_link_libraries(${LOADABLE_EXTENSION_NAME} CURL::libcurl)
endif()

# Make whisper and its dependencies exportable by adding them to the DuckDB export set
# This must be done AFTER build_static_extension creates the extension target
if(DEFINED DUCKDB_EXPORT_SET)
    set(WHISPER_EXPORT_TARGETS whisper ggml ggml-base ggml-cpu ${EXTENSION_NAME})
    # Include ggml-metal if Metal is enabled
    if(APPLE AND TARGET ggml-metal)
        list(APPEND WHISPER_EXPORT_TARGETS ggml-metal)
    endif()
    install(TARGETS ${WHISPER_EXPORT_TARGETS}
        EXPORT "${DUCKDB_EXPORT_SET}"
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
endif()
