add_library(vccCore STATIC
    core/Encoding.cpp
    core/JsonUtil.cpp
    core/Crypto.cpp
    core/HostUtil.cpp
    core/Ids.cpp
    core/Log.cpp
    core/TimeUtil.cpp
)

target_include_directories(vccCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccCore PUBLIC
    Qt6::Core
    Qt6::Network
    nlohmann_json::nlohmann_json
)

vcc_target_options(vccCore)

add_library(vccModels STATIC
    models/Node.cpp
    models/Settings.cpp
    models/Source.cpp
    models/Types.cpp
)

target_include_directories(vccModels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccModels PUBLIC
    vccCore
)

vcc_target_options(vccModels)

add_library(vccParser STATIC
    parser/Common.cpp
    parser/Clash.cpp
    parser/Fingerprint.cpp
    parser/Subscription.cpp
    parser/Uri.cpp
)

target_include_directories(vccParser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccParser PUBLIC
    vccCore
    vccModels
    yaml-cpp::yaml-cpp
)

vcc_target_options(vccParser)

add_library(vccDb STATIC
    db/Sqlite.cpp
    db/ConnectionPool.cpp
    db/Database.cpp
    db/DatabaseSchema.cpp
    db/DatabaseNodes.cpp
    db/DatabaseSources.cpp
    db/DatabaseSettings.cpp
    db/DatabaseSpeedtest.cpp
    db/DatabaseStats.cpp
    db/DatabaseTelemetry.cpp
    db/DatabaseExport.cpp
)

target_include_directories(vccDb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccDb PUBLIC
    vccCore
    vccModels
    vccParser
    unofficial::sqlite3::sqlite3
    Qt6::Concurrent
    Qt6::Core
)

vcc_target_options(vccDb)

add_library(vccKernel STATIC
    kernel/KernelTypes.cpp
)

target_include_directories(vccKernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccKernel PUBLIC
    vccCore
)

vcc_target_options(vccKernel)

add_library(vccProxy STATIC
    proxy/ConfigTypes.cpp
    proxy/MihomoConfig.cpp
    proxy/WorkDir.cpp
    proxy/Diagnostics.cpp
    proxy/SystemProxySession.cpp
)

target_include_directories(vccProxy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccProxy PUBLIC
    vccCore
    vccModels
    vccKernel
    vccExport
    vccSysProxy
    vccNet
    yaml-cpp::yaml-cpp
)

vcc_target_options(vccProxy)

add_library(vccNet STATIC
    net/HttpClient.cpp
)

target_include_directories(vccNet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccNet PUBLIC
    vccCore
    Qt6::Core
    Qt6::Network
)

vcc_target_options(vccNet)

add_library(vccSysProxy STATIC
    sysproxy/RegRunner.cpp
    sysproxy/Messages.cpp
    sysproxy/SystemProxy.cpp
    sysproxy/TerminalProxy.cpp
)

target_include_directories(vccSysProxy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccSysProxy PUBLIC
    vccCore
    Qt6::Core
)

if(WIN32)
    target_link_libraries(vccSysProxy PUBLIC wininet user32)
endif()

vcc_target_options(vccSysProxy)

add_library(vccApp STATIC
    app/MainWindow.cpp
)

target_include_directories(vccApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccApp PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    vccCore
    vccModels
)

vcc_target_options(vccApp)

qt_add_executable(vcc
    app/main.cpp
)

target_link_libraries(vcc PRIVATE vccApp)

set_target_properties(vcc PROPERTIES WIN32_EXECUTABLE TRUE)

add_library(vccExport STATIC
    export/ClashProxy.cpp
    export/UriExport.cpp
    export/ClashYamlExport.cpp
    export/QualityExport.cpp
)
target_include_directories(vccExport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vccExport PUBLIC vccCore vccModels Qt6::Core Qt6::Network yaml-cpp::yaml-cpp)
vcc_target_options(vccExport)

add_library(vccPlatform STATIC
    platform/ProcessControl.cpp
    platform/SingleInstance.cpp
    platform/Firewall.cpp
    platform/Tray.cpp
    platform/WindowHelpers.cpp
    platform/Messages.cpp
)
target_include_directories(vccPlatform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# vccSysProxy 只为复用 RegRunner 的 CREATE_NO_WINDOW 进程启动；vccKernel 提供内核 exe 名单。
target_link_libraries(vccPlatform PUBLIC vccCore vccSysProxy vccKernel Qt6::Core Qt6::Gui Qt6::Widgets)
if(WIN32)
    target_link_libraries(vccPlatform PUBLIC shell32 user32 advapi32 iphlpapi ole32 dwmapi gdi32)
endif()
vcc_target_options(vccPlatform)

add_library(vccKernelManager STATIC
    kernel/KernelManager.cpp
    kernel/KernelMessages.cpp
    kernel/KernelRelease.cpp
    kernel/KernelSource.cpp
)
target_include_directories(vccKernelManager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vccKernelManager PUBLIC vccCore vccModels vccKernel Qt6::Core)
vcc_target_options(vccKernelManager)
