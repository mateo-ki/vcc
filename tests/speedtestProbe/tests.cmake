# 真内核泳道耗时取证工具：会起 mihomo 进程并经由一个活着的出口发外网请求，
# 所以**故意不进 ctest 名单**（和 vccNetProbeTests 同一套做法），并且用例名带
# DISABLED_，只有显式 --gtest_also_run_disabled_tests + 设了环境变量才会真跑。
#
# Net::send 内部要跑局部事件循环，所以这里自备一个 QCoreApplication 的 main
# （不能链 gtest_main —— 没有 app 实例时 Qt 会直接断言）。
qt_add_executable(vccSpeedtestProbeTests
    speedtestProbe/SpeedtestProbeTests.cpp
)

target_include_directories(vccSpeedtestProbeTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vccSpeedtestProbeTests PRIVATE
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
    Qt6::Network
    Qt6::Concurrent
    GTest::gtest
)

vcc_target_options(vccSpeedtestProbeTests)
