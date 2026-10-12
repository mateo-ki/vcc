# 节点卡片自绘改造的验收：像素对照旧样式表卡 + 卡片子树零样式表 + 单卡不被拉高。
set(vccNodeCardTests_SRCS guiNodeCard/NodeCardTests.cpp)

qt_add_executable(vccNodeCardTests ${vccNodeCardTests_SRCS})
target_include_directories(vccNodeCardTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccNodeCardTests PRIVATE
    vccGui vccApp vccDb vccProxy vccKernel vccParser vccCore vccModels Qt6::Core
    Qt6::Gui Qt6::Widgets GTest::gtest)
vcc_target_options(vccNodeCardTests)
add_test(NAME vccNodeCardTests COMMAND vccNodeCardTests)
set_tests_properties(vccNodeCardTests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/vcpkg/installed/x64-windows/debug/Qt6/plugins")
