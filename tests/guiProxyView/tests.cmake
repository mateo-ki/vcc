qt_add_executable(vccProxyViewTests guiProxyView/ProxyViewTests.cpp)
target_include_directories(vccProxyViewTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccProxyViewTests PRIVATE vccGui vccParser vccModels vccCore Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest GTest::gtest_main)
vcc_target_options(vccProxyViewTests)
add_test(NAME vccProxyViewTests COMMAND vccProxyViewTests)
