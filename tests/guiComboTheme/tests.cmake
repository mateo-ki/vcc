# 下拉框底色探针：逐个页面真建控件、真渲染，采样角像素证明它画了面板色。
qt_add_executable(vccComboThemeTests guiComboTheme/ComboThemeTests.cpp
    guiComboTheme/ArrowSheetTests.cpp)
target_include_directories(vccComboThemeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccComboThemeTests PRIVATE vccApp vccGui vccDb vccProxy vccTasks vccCore
    Qt6::Core Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccComboThemeTests)
add_test(NAME vccComboThemeTests COMMAND vccComboThemeTests)
set_tests_properties(vccComboThemeTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
