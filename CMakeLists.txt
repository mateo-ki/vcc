cmake_minimum_required(VERSION 3.24)

project(Vcc VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 依赖来自 D:/vcpkg 的已安装树（经典模式）。不要在本目录放 vcpkg.json：
# manifest 模式会重新解析依赖图并把 Qt 整个从源码重编一遍。
option(VCC_BUILD_TESTS "Build Vcc test executables" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Svg)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

qt_standard_project_setup()

set(VCC_THIRD_PARTY_LIBS
    yaml-cpp::yaml-cpp
    unofficial::sqlite3::sqlite3
    LibArchive::LibArchive
    OpenSSL::Crypto
    OpenSSL::SSL
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)

set(VCC_WIN_LIBS "")
if(WIN32)
    list(APPEND VCC_WIN_LIBS
        wininet
        winhttp
        iphlpapi
        advapi32
        user32
        shell32
        ole32
        dwmapi
        gdi32
        ws2_32
        crypt32
        shlwapi
    )
endif()

function(vcc_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
    endif()
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:VCC_DEBUG_BUILD>)
endfunction()

add_subdirectory(src)

if(VCC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/DeployRuntime.cmake OPTIONAL)
