qt_add_executable(vccTaskTests tasks/TaskTrackerTests.cpp)
target_include_directories(vccTaskTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccTaskTests PRIVATE vccTasks vccCore vccModels Qt6::Core
    GTest::gtest GTest::gtest_main)
vcc_target_options(vccTaskTests)
add_test(NAME vccTaskTests COMMAND vccTaskTests)
