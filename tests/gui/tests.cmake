# 窗口骨架 + 主题装配。这些测试用 offscreen 平台跑（见 add_test 的 ENVIRONMENT）：
# main() 会先清孤儿内核，而开发时本机可能有别的实例的内核在跑。
qt_add_executable(vccGuiTests gui/ShellTests.cpp gui/IconSheetTests.cpp gui/KernelRoutingTests.cpp
    gui/AboutPageTests.cpp
    gui/ProgressThrottleTests.cpp)
target_include_directories(vccGuiTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccGuiTests PRIVATE vccApp vccGui vccDb vccProxy vccTasks vccCore
    Qt6::Core Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccGuiTests)
add_test(NAME vccGuiTests COMMAND vccGuiTests)
# vcpkg 的 Qt 插件在 debug/Qt6/plugins 下，测试 exe 旁边没有 platforms/，所以显式给路径。
set_tests_properties(vccGuiTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
