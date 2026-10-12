# 分流规则栏的离屏断言：一条 gap 一个 TEST，全部只读页面自己的可观察面。
# 两个 exe 是因为每组都要自带 main()（QApplication + Theme::apply），一个 exe 只能有一个 main。
set(vccRulesPanelTests_SRCS guiRulesPanel/RulesPanelTests.cpp)
set(vccRuleHitsTests_SRCS guiRulesPanel/RuleHitsTests.cpp)

foreach(suite RulesPanelTests RuleHitsTests)
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
