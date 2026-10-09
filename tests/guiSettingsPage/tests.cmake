qt_add_executable(vccSettingsPageTests guiSettingsPage/SettingsPageTests.cpp)
target_include_directories(vccSettingsPageTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccSettingsPageTests PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccSettingsPageTests)
add_test(NAME vccSettingsPageTests COMMAND vccSettingsPageTests)
set_tests_properties(vccSettingsPageTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
