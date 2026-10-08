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

qt_add_executable(vccProxyTests
    proxy/ConfigTypesTests.cpp
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
