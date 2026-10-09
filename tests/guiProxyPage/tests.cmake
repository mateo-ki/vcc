# 控制台页的两组离屏测试：页面骨架/徽章规则，以及"可用节点"这一栏的真数据行为。
# 分开两个 exe 是因为两边都要自带 main()（QApplication + Theme::apply）。
foreach(suite ProxyPageTests ProxyNodesTests)
    if(suite STREQUAL ProxyPageTests)
        set(_srcs guiProxyPage/ProxyPageTests.cpp)
    else()
        set(_srcs guiProxyPage/ProxyPageNodesTests.cpp)
    endif()
    qt_add_executable(vcc${suite} ${_srcs})
    target_include_directories(vcc${suite} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(vcc${suite} PRIVATE
        vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels Qt6::Core
        Qt6::Gui Qt6::Widgets GTest::gtest)
    vcc_target_options(vcc${suite})
    add_test(NAME vcc${suite} COMMAND vcc${suite})
    set_tests_properties(vcc${suite} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
endforeach()
