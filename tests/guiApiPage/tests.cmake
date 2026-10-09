qt_add_executable(vccApiPageTests guiApiPage/ApiPageTests.cpp)
target_include_directories(vccApiPageTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccApiPageTests PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels vccApi Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccApiPageTests)
add_test(NAME vccApiPageTests COMMAND vccApiPageTests)
set_tests_properties(vccApiPageTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
