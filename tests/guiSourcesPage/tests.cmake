qt_add_executable(vccSourcesPageTests guiSourcesPage/SourcesPageTests.cpp
    guiSourcesPage/PasteShortcutTests.cpp)
target_include_directories(vccSourcesPageTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccSourcesPageTests PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccSourcesPageTests)
add_test(NAME vccSourcesPageTests COMMAND vccSourcesPageTests)
set_tests_properties(vccSourcesPageTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
