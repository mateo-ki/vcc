qt_add_executable(vccKernelPanelTests guiKernelPanel/KernelPanelTests.cpp)
target_include_directories(vccKernelPanelTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccKernelPanelTests PRIVATE vccGui vccProxy vccKernel vccCore Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest GTest::gtest_main)
vcc_target_options(vccKernelPanelTests)
add_test(NAME vccKernelPanelTests COMMAND vccKernelPanelTests)
