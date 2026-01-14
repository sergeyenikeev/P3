cmake_minimum_required(VERSION 3.20)

project(MailRuUploader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DEFAULT_EMAIL "" CACHE STRING "Compile-time default email for uploader")
set(DEFAULT_APP_PASSWORD "" CACHE STRING "Compile-time default app password for uploader")
set(DEFAULT_SOURCE "" CACHE STRING "Compile-time default source path")
set(DEFAULT_REMOTE "" CACHE STRING "Compile-time default remote path")
set(DEFAULT_BASE_URL "" CACHE STRING "Compile-time default base URL")
set(DEFAULT_THREADS 0 CACHE STRING "Compile-time default threads (>0 to override)")
set(DEFAULT_COMPARE "" CACHE STRING "Compile-time default compare mode (size-mtime or size-only)")
set(DEFAULT_DRY_RUN -1 CACHE STRING "Compile-time default dry-run (0/1 to override)")
set(DEFAULT_EXCLUDES "" CACHE STRING "Compile-time default excludes (semicolon separated)")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/config_defaults.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/config_defaults.h
    @ONLY
)

add_library(uploader_core
    src/cli.cpp
    src/decision.cpp
    src/exclude.cpp
    src/logger.cpp
    src/path_utils.cpp
    src/sync_engine.cpp
    src/webdav_client.cpp
)
target_include_directories(uploader_core PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if (WIN32)
    target_link_libraries(uploader_core PUBLIC winhttp)
else()
    message(FATAL_ERROR "This project targets Windows (WinHTTP required).")
endif()

add_executable(uploader src/main.cpp)
target_link_libraries(uploader PRIVATE uploader_core)

enable_testing()
add_subdirectory(tests)
