qt_add_executable(vccNodeTextTests guiNodeText/NodeTextTests.cpp)
target_include_directories(vccNodeTextTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccNodeTextTests PRIVATE vccGui vccModels vccCore Qt6::Core Qt6::Gui
    Qt6::Widgets GTest::gtest GTest::gtest_main)
vcc_target_options(vccNodeTextTests)
add_test(NAME vccNodeTextTests COMMAND vccNodeTextTests)
