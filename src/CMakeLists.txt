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
    proxy/KernelConfigMessages.cpp
    proxy/MihomoConfig.cpp
    proxy/SingBoxConfig.cpp
    proxy/XrayConfig.cpp
    proxy/ProxyManager.cpp
    proxy/WorkDir.cpp
    proxy/Diagnostics.cpp
    proxy/SystemProxySession.cpp
)

target_include_directories(vccProxy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccProxy PUBLIC
    vccCore
    vccModels
    vccKernel
    vccKernelManager
    vccGeo
    vccExport
    vccSysProxy
    vccPlatform
    vccNet
    yaml-cpp::yaml-cpp
)

vcc_target_options(vccProxy)

add_library(vccGeo STATIC
    geo/Geo.cpp
    geo/GeoMessages.cpp
    ip/IpRisk.cpp
)

target_include_directories(vccGeo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccGeo PUBLIC
    vccCore
    vccKernel
    vccNet
    vccSysProxy
    Qt6::Core
)

vcc_target_options(vccGeo)

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

# 界面层：主题 + 侧栏文案 + 五个页面。页面 .cpp 也进这个 GLOB，新增文件不用改 CMake。
file(GLOB VCC_GUI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui/*.cpp)
add_library(vccGui STATIC ${VCC_GUI_SOURCES})
target_include_directories(vccGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vccGui PUBLIC
    vccCore
    vccModels
    vccDb
    vccProxy
    vccTasks
    vccKernel
    vccKernelManager
    vccUpdater
    vccExport
    vccGeo
    vccSpeedtest
    vccCollector
    vccTelegram
    vccPlatform
    vccApi
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
vcc_target_options(vccGui)

add_library(vccApp STATIC
    app/MainWindow.cpp
    app/AppContext.cpp
)

target_include_directories(vccApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccApp PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    vccCore
    vccGui
    vccModels
    vccDb
    vccProxy
    vccTasks
    vccPlatform
    vccApi
    vccKernelManager
)

vcc_target_options(vccApp)

qt_add_executable(vcc
    app/main.cpp
)

target_link_libraries(vcc PRIVATE
    vccApp
    vccKernelConfig
    vccCollector
    vccTelegram
    vccGeo
    vccExport
)

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

# sing-box / xray 的配置生成器就住在 vccProxy 里（ProxyManager 直接调它们，
# 拆成独立目标会形成 vccProxy <-> vccKernelConfig 的链接环）。保留别名，
# 按 vccKernelConfig 写好的测试和可执行目标不用改。
add_library(vccKernelConfig ALIAS vccProxy)

# Telegram 频道源（t.me/s/<频道> 网页预览）。中文文案与正则由 tools/gen-telegram-cjk.mjs
# 从 telegram.rs 生成，写在 Telegram.cpp 的 BEGIN/END GENERATED 块里，不要手改。
add_library(vccTelegram STATIC
    collector/Telegram.cpp
)
target_include_directories(vccTelegram PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vccTelegram PUBLIC vccCore vccModels vccDb vccParser vccNet Qt6::Core Qt6::Network)
vcc_target_options(vccTelegram)

# ---------------------------------------------------------------------------
# 并行移植层的挂载点。
#
# 每个未完成 layer 独占 src/<dir>/：新增 .cpp 不用改 CMake（GLOB+CONFIGURE_DEPENDS）。
# 目录还空着时退化成 INTERFACE 目标，这样"某一层还没移植完"不会把整个工程配置卡住——
# 但链接期缺符号会真实报错，所以空目录绝不会伪装成"这层已经有了"。
# ---------------------------------------------------------------------------
function(vcc_port_layer target dir)
    file(GLOB layer_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp)
    # 只挂已经存在的依赖：并行的移植任务随时会新增/改名目标，不能因为别人还没落地就配置失败。
    set(layer_libs "")
    foreach(dependency ${ARGN})
        if(TARGET ${dependency})
            list(APPEND layer_libs ${dependency})
        endif()
    endforeach()
    if(layer_sources)
        add_library(${target} STATIC ${layer_sources})
        target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${target} PUBLIC ${layer_libs})
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8 /Zc:__cplusplus /permissive-)
        endif()
    else()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${target} INTERFACE ${layer_libs})
    endif()
endfunction()

vcc_port_layer(vccTasks tasks vccCore vccModels Qt6::Core)
vcc_port_layer(vccProbe proxy_probe vccProxy vccExport vccKernel vccKernelManager vccPlatform vccNet
    Qt6::Core Qt6::Concurrent)
vcc_port_layer(vccSpeedtest speedtest vccDb vccNet vccProxy vccProbe vccKernel vccTasks Qt6::Core Qt6::Concurrent)
vcc_port_layer(vccScheduler scheduler vccDb vccCollector vccTelegram vccSpeedtest vccTasks vccProxy vccKernel Qt6::Core Qt6::Concurrent)
vcc_port_layer(vccRouter router vccCore vccModels Qt6::Core)
vcc_port_layer(vccApi api vccDb vccProxy vccCollector vccTelegram vccSpeedtest vccScheduler
    vccTasks vccRouter vccExport vccKernel vccNet Qt6::Widgets)
vcc_port_layer(vccUpdater updater vccKernel vccKernelManager vccNet vccGeo vccTasks
    LibArchive::LibArchive ZLIB::ZLIB Qt6::Core)

add_library(vccCollector STATIC
    collector/Collector.cpp
    collector/CollectorMessages.cpp
    collector/FetchLimits.cpp
    collector/GistDiscovery.cpp
)
target_include_directories(vccCollector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vccCollector PUBLIC vccCore vccModels vccDb vccParser vccNet vccGeo vccTasks yaml-cpp::yaml-cpp)
vcc_target_options(vccCollector)
