cmake_minimum_required(VERSION 3.24)

project(Vcc VERSION 1.0.0 LANGUAGES CXX)

# RC（资源脚本）只在 Windows 上有意义：Linux/macOS 上声明它会让 CMake 去找一个
# 根本不存在的资源编译器。图标 .ico 也只进 Windows 的 vcc.rc。
if(WIN32)
    enable_language(RC)
endif()

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
        target_compile_options(${target} PRIVATE /utf-8 /Zc:__cplusplus /permissive- /MP)
    endif()
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:VCC_DEBUG_BUILD>)

    # 预编译头。判据是实测：改一个被 47 处包含的 src/core/Result.h 要重编 140 个
    # TU + 66 次 AUTOMOC（--parallel 8 下 58s），而每个 TU 里最贵的就是那批 Qt/STL
    # 头的重复解析。空目录退化成的 INTERFACE 目标没有源文件，不能给它挂 PCH。
    get_target_property(_vcc_target_type ${target} TYPE)
    if(NOT _vcc_target_type STREQUAL "INTERFACE_LIBRARY")
        target_precompile_headers(${target} PRIVATE
            <QString>
            <QByteArray>
            <QList>
            <QVector>
            <QSharedPointer>
            <functional>
            <memory>
            <optional>
            <vector>
        )
    endif()
endfunction()

add_subdirectory(src)

if(VCC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
