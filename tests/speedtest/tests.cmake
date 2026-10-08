# tests/speedtest/tests.cmake —— 由 speedtest 移植层自己维护，被 tests/CMakeLists.txt 的
# file(GLOB .../tests/*/tests.cmake) 自动挂载。
qt_add_executable(vccSpeedtestTests
    speedtest/SpeedtestTests.cpp
)

target_include_directories(vccSpeedtestTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# vccProbe / vccKernelManager 提供 run_speedtest 依赖的两个外部符号
# （ProxyProbe::verifyNodesWithKernel、Kernel::selectProxyKernel）；它们由别的移植层负责，
# 目标还没落地时先让配置通过，链接期再报缺符号——那才是真的缺。
set(vccSpeedtestTestLibs
    vccSpeedtest
    vccDb
    vccParser
    vccNet
    vccProxy
    vccKernel
    vccTasks
    vccCore
    vccModels
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    GTest::gtest
    GTest::gtest_main
)
foreach(vccSpeedtestOptionalLib vccProbe vccKernelManager vccExport vccGeo vccSysProxy)
    if(TARGET ${vccSpeedtestOptionalLib})
        list(APPEND vccSpeedtestTestLibs ${vccSpeedtestOptionalLib})
    endif()
endforeach()

target_link_libraries(vccSpeedtestTests PRIVATE ${vccSpeedtestTestLibs})

vcc_target_options(vccSpeedtestTests)

add_test(NAME vccSpeedtestTests COMMAND vccSpeedtestTests)
