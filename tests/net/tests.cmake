# Net::send 的分段超时：响应头预算只约束"等头"，读体阶段按块间空闲计时。
# 全部走 127.0.0.1 上的假服务器，不碰真实网络。
qt_add_executable(vccNetTests net/HttpClientTests.cpp)
target_include_directories(vccNetTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccNetTests PRIVATE
    vccNet vccCore vccModels Qt6::Core Qt6::Network
    GTest::gtest)
vcc_target_options(vccNetTests)
add_test(NAME vccNetTests COMMAND vccNetTests)

# 真网络取证工具：只在手动设了 VCC_NET_PROBE_URL 时才有输出，故意不进 ctest 名单。
qt_add_executable(vccNetProbeTests net/NetProbeTests.cpp)
target_include_directories(vccNetProbeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccNetProbeTests PRIVATE
    vccNet vccCore vccModels vccGeo vccKernelManager Qt6::Core Qt6::Network
    GTest::gtest)
vcc_target_options(vccNetProbeTests)
if (WIN32)
    target_link_libraries(vccNetProbeTests PRIVATE psapi)
endif()
