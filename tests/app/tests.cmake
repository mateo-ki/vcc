# 退出路径的关代理：Rust 的 on_exit 在进程走人之前同步 stop()，并给 8s 上限。
qt_add_executable(vccAppTests app/ExitCleanupTests.cpp)
target_include_directories(vccAppTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccAppTests PRIVATE
    vccApp vccCore vccModels vccProxy Qt6::Core
    GTest::gtest GTest::gtest_main)
vcc_target_options(vccAppTests)
add_test(NAME vccAppTests COMMAND vccAppTests)
