qt_add_executable(vccPrepareTests guiPrepare/PrepareTests.cpp)
target_include_directories(vccPrepareTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccPrepareTests PRIVATE vccGui vccCore Qt6::Core Qt6::Gui Qt6::Widgets
    GTest::gtest GTest::gtest_main)
vcc_target_options(vccPrepareTests)
add_test(NAME vccPrepareTests COMMAND vccPrepareTests)
