# ProxyManager 的状态机与启动契约。这里绝不真起内核进程：ensurePortAvailable 的第二次
# 重试会清残留代理内核，而本机可能有别的实例正在用 9980。
qt_add_executable(vccProxyManagerTests proxymanager/ProxyManagerTests.cpp)
target_include_directories(vccProxyManagerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccProxyManagerTests PRIVATE
    vccProxy vccKernelConfig vccKernel vccCore vccModels Qt6::Core Qt6::Network
    GTest::gtest)
vcc_target_options(vccProxyManagerTests)
add_test(NAME vccProxyManagerTests COMMAND vccProxyManagerTests)
