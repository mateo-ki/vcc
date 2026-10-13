qt_add_executable(vccNodesPageTests guiNodesPage/NodesPageTests.cpp)
target_include_directories(vccNodesPageTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccNodesPageTests PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccNodesPageTests)
add_test(NAME vccNodesPageTests COMMAND vccNodesPageTests)
set_tests_properties(vccNodesPageTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")

set_tests_properties(vccNodesPageTests PROPERTIES RUN_SERIAL TRUE)
