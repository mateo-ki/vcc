# tests/updater/tests.cmake —— 内核下载/安装层（src/updater）的测试挂载点。
# tests/CMakeLists.txt 用 file(GLOB .../*/tests.cmake) 自动 include，本文件新增即生效。
#
# 只链本层和它已有的依赖：LibArchive 用来在内存里造 ZIP 夹具（对应 Rust 测试里的
# zip::ZipWriter），ZLIB 是 libarchive 的 deflate 依赖。
qt_add_executable(vccUpdaterTests
    updater/UpdaterTests.cpp
)

target_include_directories(vccUpdaterTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccUpdaterTests PRIVATE
    vccUpdater
    vccKernel
    vccKernelManager
    vccNet
    vccGeo
    vccCore
    vccModels
    LibArchive::LibArchive
    ZLIB::ZLIB
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccUpdaterTests)

add_test(NAME vccUpdaterTests COMMAND vccUpdaterTests)
