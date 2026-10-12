# 内存归因夹具：不是回归测试，是拿来量化"哪一步吃掉多少私有内存"的探针。
set(vccMemProbe_SRCS guiMemProbe/MemProbeTests.cpp)

qt_add_executable(vccMemProbe ${vccMemProbe_SRCS})
target_include_directories(vccMemProbe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccMemProbe PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels vccPlatform vccNet vccApi
    vccTasks vccSysProxy vccKernelManager vccUpdater vccExport vccGeo vccSpeedtest vccScheduler
    Qt6::Core Qt6::Gui Qt6::Widgets GTest::gtest)
if (WIN32)
    target_link_libraries(vccMemProbe PRIVATE psapi)
endif()
vcc_target_options(vccMemProbe)
