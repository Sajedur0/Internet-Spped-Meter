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
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    )
else()
    target_compile_options(InternetSpeedMeter PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
    )
endif()

# GNU/MinGW needs this explicitly to select the wide-character GUI entry
# point used by wWinMain. MSVC selects it automatically for this target.
if(MINGW)
    # CMake supplies -mwindows for WIN32 targets. The explicit subsystem is
    # retained for Clang/Zig MinGW cross drivers as well as GNU ld.
    target_link_options(InternetSpeedMeter PRIVATE
        -municode
        -Wl,--subsystem,windows
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT MSVC)
    # Native Visual Studio generators already supply this for WIN32 targets.
    target_link_options(InternetSpeedMeter PRIVATE
        -municode
        -Wl,--subsystem,windows
    )
endif()

set_target_properties(InternetSpeedMeter PROPERTIES
    OUTPUT_NAME "InternetSpeedMeter"
)
