# 控制台页的三组离屏测试：页面骨架/徽章规则、"可用节点"栏的真数据行为，
# 以及切页 refresh() 的重建次数。分开 exe 是因为每组都要自带 main()
# （QApplication + Theme::apply），一个 exe 只能有一个 main。
set(vccProxyPageTests_SRCS guiProxyPage/ProxyPageTests.cpp)
set(vccProxyNodesTests_SRCS guiProxyPage/ProxyPageNodesTests.cpp)
set(vccProxyPerfTests_SRCS guiProxyPage/ProxyPagePerfTests.cpp)
set(vccProxyPanelsTests_SRCS guiProxyPage/ProxyPagePanelsTests.cpp)

foreach(suite ProxyPageTests ProxyNodesTests ProxyPerfTests ProxyPanelsTests)
    qt_add_executable(vcc${suite} ${vcc${suite}_SRCS})
    target_include_directories(vcc${suite} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(vcc${suite} PRIVATE
        vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels Qt6::Core
        Qt6::Gui Qt6::Widgets GTest::gtest)
    vcc_target_options(vcc${suite})
    add_test(NAME vcc${suite} COMMAND vcc${suite})
    set_tests_properties(vcc${suite} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
endforeach()

# vccProxyPerfTests 量的是墙钟时间地板（120 条规则重灌 <600ms、进内核管理同步部分 <60ms），
# 和另外 7 个 GUI 套件并发跑时这些数必然被抢 CPU 打爆 —— 实测 -j8 下偶发红/卡住、
# 单跑 1.3s 全绿。它必须串行跑。
set_tests_properties(vccProxyPerfTests PROPERTIES RUN_SERIAL TRUE)
