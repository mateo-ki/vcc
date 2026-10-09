# cmake/DeployRuntime.cmake
# 自动部署 Qt 运行时插件与配置到可执行文件输出目录

if(WIN32)
    # 确定 vcpkg 根路径与 Qt 插件路径
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_VCC_VCPKG_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    elseif(DEFINED Qt6_DIR)
        get_filename_component(_VCC_VCPKG_ROOT "${Qt6_DIR}/../.." ABSOLUTE)
    else()
        set(_VCC_VCPKG_ROOT "D:/vcpkg/installed/x64-windows")
    endif()

    set(_QT_PLUGIN_DIR_DEBUG "${_VCC_VCPKG_ROOT}/debug/Qt6/plugins")
    set(_QT_PLUGIN_DIR_RELEASE "${_VCC_VCPKG_ROOT}/Qt6/plugins")

    # 所需核心插件目录
    set(_QT_PLUGIN_CATEGORIES
        platforms
        styles
        imageformats
        tls
        networkinformation
        iconengines
    )

    if(TARGET vcc)
        # 1. 复制 qt.conf 到目标输出目录
        add_custom_command(TARGET vcc POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/cmake/qt.conf"
                "$<TARGET_FILE_DIR:vcc>/qt.conf"
            COMMENT "[Deploy] Copying qt.conf to target directory"
            VERBATIM
        )

        # 2. 拷贝核心插件目录至 $<TARGET_FILE_DIR:vcc>/plugins/<category>
        foreach(_cat IN LISTS _QT_PLUGIN_CATEGORIES)
            add_custom_command(TARGET vcc POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:vcc>/plugins/${_cat}"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                    "$<$<CONFIG:Debug>:${_QT_PLUGIN_DIR_DEBUG}/${_cat}>$<$<NOT:$<CONFIG:Debug>>:${_QT_PLUGIN_DIR_RELEASE}/${_cat}>"
                    "$<TARGET_FILE_DIR:vcc>/plugins/${_cat}"
                COMMENT "[Deploy] Copying Qt plugin: ${_cat}"
                VERBATIM
            )
        endforeach()

        # 3. 额外直接拷贝 platforms 目录到可执行文件同级目录作为二次保底
        add_custom_command(TARGET vcc POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:vcc>/platforms"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "$<$<CONFIG:Debug>:${_QT_PLUGIN_DIR_DEBUG}/platforms>$<$<NOT:$<CONFIG:Debug>>:${_QT_PLUGIN_DIR_RELEASE}/platforms>"
                "$<TARGET_FILE_DIR:vcc>/platforms"
            COMMENT "[Deploy] Ensuring platforms directory alongside executable"
            VERBATIM
        )

        # 4. 提供一键打包到独立发布目录的 target: cmake --build build --target deploy
        add_custom_target(deploy
            COMMAND "${CMAKE_COMMAND}" -E echo "[Deploy] Packaging standalone distribution into $<TARGET_FILE_DIR:vcc>/dist..."
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:vcc>/dist"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:vcc>" "$<TARGET_FILE_DIR:vcc>/dist/"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/cmake/qt.conf" "$<TARGET_FILE_DIR:vcc>/dist/qt.conf"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "$<TARGET_FILE_DIR:vcc>/plugins" "$<TARGET_FILE_DIR:vcc>/dist/plugins"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "$<TARGET_FILE_DIR:vcc>/platforms" "$<TARGET_FILE_DIR:vcc>/dist/platforms"
            DEPENDS vcc
            COMMENT "Package standalone distribution into dist/"
            VERBATIM
        )
    endif()
endif()
