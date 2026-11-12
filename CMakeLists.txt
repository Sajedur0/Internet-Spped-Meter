cmake_minimum_required(VERSION 3.21)

project(InternetSpeedMeter VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Internet Speed Meter is a Windows-only Win32 application.")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/speed_meter.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/speed_meter.rc"
    @ONLY
)

add_executable(InternetSpeedMeter WIN32
    src/main.cpp
    src/resource.h
    "${CMAKE_CURRENT_BINARY_DIR}/speed_meter.rc"
)

target_compile_features(InternetSpeedMeter PRIVATE cxx_std_17)
target_compile_definitions(InternetSpeedMeter PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
target_link_libraries(InternetSpeedMeter PRIVATE
    advapi32
    gdi32
    iphlpapi
    shell32
    user32
)

if(MSVC)
    target_compile_options(InternetSpeedMeter PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
    )
else()
    target_compile_options(InternetSpeedMeter PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set_target_properties(InternetSpeedMeter PROPERTIES
    OUTPUT_NAME "InternetSpeedMeter"
)
