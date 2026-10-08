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
    parser/Fingerprint.cpp
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
)

target_include_directories(vccProxy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vccProxy PUBLIC
    vccCore
    vccModels
    vccKernel
    yaml-cpp::yaml-cpp
)

vcc_target_options(vccProxy)

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
