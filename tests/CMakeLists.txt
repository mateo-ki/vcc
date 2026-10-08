find_package(GTest CONFIG REQUIRED)

qt_add_executable(vccCoreTests
    core/TimeUtilTests.cpp
    core/HostUtilTests.cpp
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
