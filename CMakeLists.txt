cmake_minimum_required(VERSION 3.20)

project(MailRuUploader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(uploader_core
    src/cli.cpp
    src/decision.cpp
    src/exclude.cpp
    src/logger.cpp
    src/path_utils.cpp
    src/sync_engine.cpp
    src/webdav_client.cpp
)
target_include_directories(uploader_core PUBLIC include)

if (WIN32)
    target_link_libraries(uploader_core PUBLIC winhttp)
else()
    message(FATAL_ERROR "This project targets Windows (WinHTTP required).")
endif()

add_executable(uploader src/main.cpp)
target_link_libraries(uploader PRIVATE uploader_core)

enable_testing()
add_subdirectory(tests)
