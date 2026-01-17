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
set(DEFAULTS_FROM_CONF_PATH "" CACHE STRING "Path to uploader.conf to embed into compiled defaults")

if (DEFAULTS_FROM_CONF_PATH)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LoadDefaults.cmake)
    load_defaults_from_conf("${DEFAULTS_FROM_CONF_PATH}")

    if (DEFAULT_EMAIL STREQUAL "" AND CONF_EMAIL)
        set(DEFAULT_EMAIL "${CONF_EMAIL}" CACHE STRING "Compile-time default email for uploader" FORCE)
    endif()
    if (DEFAULT_APP_PASSWORD STREQUAL "" AND CONF_APP_PASSWORD)
        set(DEFAULT_APP_PASSWORD "${CONF_APP_PASSWORD}" CACHE STRING "Compile-time default app password for uploader" FORCE)
    endif()
    if (DEFAULT_SOURCE STREQUAL "" AND CONF_SOURCE)
        set(DEFAULT_SOURCE "${CONF_SOURCE}" CACHE STRING "Compile-time default source path" FORCE)
    endif()
    if (DEFAULT_REMOTE STREQUAL "" AND CONF_REMOTE)
        set(DEFAULT_REMOTE "${CONF_REMOTE}" CACHE STRING "Compile-time default remote path" FORCE)
    endif()
    if (DEFAULT_BASE_URL STREQUAL "" AND CONF_BASE_URL)
        set(DEFAULT_BASE_URL "${CONF_BASE_URL}" CACHE STRING "Compile-time default base URL" FORCE)
    endif()
    if (DEFAULT_THREADS STREQUAL "0" AND CONF_THREADS)
        if (CONF_THREADS MATCHES "^[0-9]+$" AND NOT CONF_THREADS STREQUAL "0")
            set(DEFAULT_THREADS "${CONF_THREADS}" CACHE STRING "Compile-time default threads (>0 to override)" FORCE)
        else()
            message(FATAL_ERROR "Invalid threads value in uploader.conf: ${CONF_THREADS}")
        endif()
    endif()
    if (DEFAULT_COMPARE STREQUAL "" AND CONF_COMPARE)
        string(TOLOWER "${CONF_COMPARE}" CONF_COMPARE_LOWER)
        if (CONF_COMPARE_LOWER STREQUAL "size-mtime" OR CONF_COMPARE_LOWER STREQUAL "size-only")
            set(DEFAULT_COMPARE "${CONF_COMPARE_LOWER}" CACHE STRING "Compile-time default compare mode (size-mtime or size-only)" FORCE)
        else()
            message(FATAL_ERROR "Invalid compare value in uploader.conf: ${CONF_COMPARE}")
        endif()
    endif()
    if (DEFAULT_DRY_RUN STREQUAL "-1" AND CONF_DRY_RUN)
        string(TOLOWER "${CONF_DRY_RUN}" CONF_DRY_RUN_LOWER)
        if (CONF_DRY_RUN_LOWER MATCHES "^(1|true|yes|on)$")
            set(DEFAULT_DRY_RUN "1" CACHE STRING "Compile-time default dry-run (0/1 to override)" FORCE)
        elseif (CONF_DRY_RUN_LOWER MATCHES "^(0|false|no|off)$")
            set(DEFAULT_DRY_RUN "0" CACHE STRING "Compile-time default dry-run (0/1 to override)" FORCE)
        else()
            message(FATAL_ERROR "Invalid dry_run value in uploader.conf: ${CONF_DRY_RUN}")
        endif()
    endif()
    if (DEFAULT_EXCLUDES STREQUAL "" AND CONF_EXCLUDES)
        set(DEFAULT_EXCLUDES "${CONF_EXCLUDES}" CACHE STRING "Compile-time default excludes (semicolon separated)" FORCE)
    endif()
endif()

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
