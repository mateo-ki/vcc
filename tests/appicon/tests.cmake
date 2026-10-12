# 图标自查工具：不是回归测试，是"把 SVG 渲成 PNG 让我自己看图"的回路
# （本机内嵌浏览器面板经常开不了，截图这条路不可靠）。
qt_add_executable(vccIconRender appicon/IconRenderTests.cpp)
target_include_directories(vccIconRender PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vccIconRender PRIVATE Qt6::Core Qt6::Gui Qt6::Svg GTest::gtest)
vcc_target_options(vccIconRender)
