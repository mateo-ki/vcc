# 本层（真实连通性探针）的测试：纯逻辑，不起内核进程、不联网。
qt_add_executable(vccProbeTests
    proxy_probe/ProbeLogicTests.cpp
    proxy_probe/ProbeBatchTests.cpp
)

target_include_directories(vccProbeTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# vccProbe 的 GLOB 挂载里没有这三个目标，测试先把它们显式链上（详见交付说明）：
#   vccKernelConfig  -> Proxy::buildSingBoxBatchProbePlan / buildXrayBatchProbePlan / Messages
#   vccKernelManager -> Kernel::findKernelExecutable
#   vccPlatform      -> Platform::assignChildProcessId
target_link_libraries(vccProbeTests PRIVATE
    vccProbe
    vccProxy
    vccKernelConfig
    vccKernelManager
    vccPlatform
    vccExport
    vccKernel
    vccNet
    vccCore
    vccModels
    Qt6::Core
    Qt6::Concurrent
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccProbeTests)

add_test(NAME vccProbeTests COMMAND vccProbeTests)
