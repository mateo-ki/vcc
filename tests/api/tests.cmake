# src/api 的测试：路由表 / 鉴权 / handler 全部不绑 socket（本机偶尔会吞掉新建的
# loopback 监听，所以 socket 测试一律不做，只测 handler 与解析器）。

# 兼容垫片：find_package(LibArchive) 给的导入目标名是 LibArchive::LibArchive，
# 而 src/CMakeLists.txt 与部分测试写的是小写 LibArchive::libarchive。这里补一个
# alias，让整棵树的 generate 不因为这个拼写差异失败（谁先修好都不冲突：有目标就跳过）。
if(TARGET LibArchive::LibArchive AND NOT TARGET LibArchive::libarchive)
    add_library(LibArchive::libarchive ALIAS LibArchive::LibArchive)
endif()

qt_add_executable(vccApiTests
    api/HttpMessageTests.cpp
    api/RouterTests.cpp
    api/HandlerTests.cpp
)

target_include_directories(vccApiTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccApiTests PRIVATE
    vccApi
    vccCore
    vccModels
    vccDb
    vccParser
    vccProxy
    vccKernel
    vccTasks
    vccExport
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    GTest::gtest
    GTest::gtest_main
)

# 内核/收集器层可能还没挂载成 target；谁先落地谁就被链进来，不影响本目标配置。
foreach(optional_target vccKernelManager vccCollector vccNet vccGeo vccSysProxy)
    if(TARGET ${optional_target})
        target_link_libraries(vccApiTests PRIVATE ${optional_target})
    endif()
endforeach()

if(MSVC)
    target_compile_options(vccApiTests PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
endif()

vcc_target_options(vccApiTests)

add_test(NAME vccApiTests COMMAND vccApiTests)
