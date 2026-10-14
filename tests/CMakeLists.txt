find_package(GTest CONFIG REQUIRED)

qt_add_executable(vccCoreTests
    core/TimeUtilTests.cpp
    core/HostUtilTests.cpp
    core/VersionTests.cpp
    models/SettingsTests.cpp
    models/SourceJsonTests.cpp
)

target_include_directories(vccCoreTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccCoreTests PRIVATE
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccCoreTests)

add_test(NAME vccCoreTests COMMAND vccCoreTests)

qt_add_executable(vccSysProxyTests
    sysproxy/SystemProxyTests.cpp
)

target_include_directories(vccSysProxyTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccSysProxyTests PRIVATE
    vccSysProxy
    vccCore
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccSysProxyTests)

add_test(NAME vccSysProxyTests COMMAND vccSysProxyTests)

qt_add_executable(vccProxyTests
    proxy/ConfigTypesTests.cpp
    proxy/DiagnosticsTests.cpp
)

target_include_directories(vccProxyTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccProxyTests PRIVATE
    vccProxy
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccProxyTests)

add_test(NAME vccProxyTests COMMAND vccProxyTests)

qt_add_executable(vccDbTelemetryTests
    db/TelemetryExportTests.cpp
)

target_include_directories(vccDbTelemetryTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccDbTelemetryTests PRIVATE
    vccDb
    vccParser
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccDbTelemetryTests)

add_test(NAME vccDbTelemetryTests COMMAND vccDbTelemetryTests)

qt_add_executable(vccDbTests
    db/DatabaseTests.cpp
)

target_include_directories(vccDbTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccDbTests PRIVATE
    vccDb
    vccParser
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccDbTests)

add_test(NAME vccDbTests COMMAND vccDbTests)

qt_add_executable(vccPlatformTests
    platform/PlatformTests.cpp
)

target_include_directories(vccPlatformTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccPlatformTests PRIVATE
    vccPlatform
    vccCore
    vccKernel
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccPlatformTests)

add_test(NAME vccPlatformTests COMMAND vccPlatformTests)

qt_add_executable(vccKernelManagerTests
    kernel/KernelManagerTests.cpp
)

target_include_directories(vccKernelManagerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccKernelManagerTests PRIVATE
    vccKernelManager
    vccKernel
    vccCore
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccKernelManagerTests)

add_test(NAME vccKernelManagerTests COMMAND vccKernelManagerTests)

qt_add_executable(vccParserTests
    parser/ParserTests.cpp
)

target_include_directories(vccParserTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccParserTests PRIVATE
    vccParser
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccParserTests)

add_test(NAME vccParserTests COMMAND vccParserTests)

qt_add_executable(vccExportTests export/ExportTests.cpp)
target_include_directories(vccExportTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccExportTests PRIVATE vccExport vccCore vccModels Qt6::Core GTest::gtest GTest::gtest_main)
vcc_target_options(vccExportTests)
add_test(NAME vccExportTests COMMAND vccExportTests)

qt_add_executable(vccTelegramTests collector/TelegramTests.cpp)
# 第二个 include 目录给生成的 collector/TelegramCjkGenerated.h（中文与夹具，勿手改）。
target_include_directories(vccTelegramTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vccTelegramTests PRIVATE
    vccTelegram
    vccDb
    vccParser
    vccNet
    vccSysProxy
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
vcc_target_options(vccTelegramTests)
add_test(NAME vccTelegramTests COMMAND vccTelegramTests)

# ---------------------------------------------------------------------------
# 每个并行移植层在这里挂自己的测试：在 tests/<layer>/tests.cmake 里 add_executable，
# 文件一新增就自动生效（不用改这个共享文件，也就不会和别人抢同一处编辑）。
# ---------------------------------------------------------------------------
file(GLOB VCC_LAYER_TEST_HOOKS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*/tests.cmake)
foreach(hook ${VCC_LAYER_TEST_HOOKS})
    include(${hook})
endforeach()

qt_add_executable(vccGeoTests
    geo/GeoIpRiskTests.cpp
)

target_include_directories(vccGeoTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccGeoTests PRIVATE
    vccGeo
    vccNet
    vccCore
    vccKernelManager
    Qt6::Core
    Qt6::Network
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccGeoTests)

add_test(NAME vccGeoTests COMMAND vccGeoTests)

qt_add_executable(vccKernelConfigTests
    proxy/KernelConfigTests.cpp
)

target_include_directories(vccKernelConfigTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccKernelConfigTests PRIVATE
    vccKernelConfig
    vccProxy
    vccCore
    vccModels
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccKernelConfigTests)

add_test(NAME vccKernelConfigTests COMMAND vccKernelConfigTests)

qt_add_executable(vccCollectorTests
    collector/CollectorTests.cpp
)

target_include_directories(vccCollectorTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(vccCollectorTests PRIVATE
    vccCollector
    vccDb
    vccParser
    vccNet
    vccTasks
    vccCore
    vccModels
    Qt6::Core
    Qt6::Concurrent
    GTest::gtest
    GTest::gtest_main
)

vcc_target_options(vccCollectorTests)

add_test(NAME vccCollectorTests COMMAND vccCollectorTests)
