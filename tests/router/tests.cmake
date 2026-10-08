# vccRouter 层的测试挂载点（对应 Rust src/router.rs 的 mod tests 与 mod backup_tests）。
# tests/CMakeLists.txt 会 glob 所有 tests/*/tests.cmake 并 include 进来，所以这一层不用改共享文件。

qt_add_executable(vccRouterTests
    ${CMAKE_CURRENT_LIST_DIR}/RouterTests.cpp
)

# 第二个 include 目录给生成的 RouterTestCjk.h（Rust 测试夹具里的中文节点名，勿手改）。
target_include_directories(vccRouterTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vccRouterTests PRIVATE
    vccRouter
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccRouterTests)

add_test(NAME vccRouterTests COMMAND vccRouterTests)
