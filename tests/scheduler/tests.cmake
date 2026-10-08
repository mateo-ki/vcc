# tests/scheduler/tests.cmake —— 由 scheduler 移植层自己维护，被 tests/CMakeLists.txt 的
# file(GLOB .../tests/*/tests.cmake) 自动挂载。
qt_add_executable(vccSchedulerTests
    scheduler/SchedulerTests.cpp
)

target_include_directories(vccSchedulerTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 只用得到 SchedulerRules 那两个纯函数：MSVC 按目标文件抽取静态库，Scheduler.obj
# （要 Collector/ProxyProbe）不会被拉进来，所以这里不需要等别的层落地。
set(vccSchedulerTestLibs
    vccScheduler
    vccSpeedtest
    vccModels
    vccCore
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
foreach(vccSchedulerOptionalLib vccDb vccParser vccTasks vccKernel vccNet vccProxy vccProbe vccKernelManager)
    if(TARGET ${vccSchedulerOptionalLib})
        list(APPEND vccSchedulerTestLibs ${vccSchedulerOptionalLib})
    endif()
endforeach()

target_link_libraries(vccSchedulerTests PRIVATE ${vccSchedulerTestLibs})

vcc_target_options(vccSchedulerTests)

add_test(NAME vccSchedulerTests COMMAND vccSchedulerTests)
