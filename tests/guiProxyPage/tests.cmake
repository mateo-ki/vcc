# 控制台页第一层：状态卡 + 六分栏 + 日志栏。离屏跑，不启动任何内核。
qt_add_executable(vccProxyPageTests guiProxyPage/ProxyPageTests.cpp)
target_include_directories(vccProxyPageTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccProxyPageTests PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccCore vccModels Qt6::Core Qt6::Gui Qt6::Widgets
    GTest::gtest)
vcc_target_options(vccProxyPageTests)
add_test(NAME vccProxyPageTests COMMAND vccProxyPageTests)
set_tests_properties(vccProxyPageTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
