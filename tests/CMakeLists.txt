# ============================================================================
#  tests — 单元测试（CTest 驱动）
#
#  只测 color/ 这一层：它按设计「完全不依赖 Win32」，所以可以直接把三个 .cpp
#  编成控制台程序，不需要窗口 / GDI+ / 剪贴板。
#  界面行为（自绘窗口、命中测试）由 tools/ui_check.py 做冒烟测试，两者分工不同：
#    · tests/       改色彩算法、解析规则后跑，秒级、无窗口、可 CI
#    · tools/       改布局 / 交互后跑，会真的开窗口截图
#  以后要引 Catch2 / GoogleTest，替换 test_color.cpp 即可，顶层 CMake 不用动。
# ============================================================================
add_executable(test_color
    test_color.cpp
    ${CMAKE_SOURCE_DIR}/src/color/color.cpp
    ${CMAKE_SOURCE_DIR}/src/color/harmony.cpp
    ${CMAKE_SOURCE_DIR}/src/color/parse.cpp
)

target_include_directories(test_color PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(test_color PRIVATE /utf-8)
else()
    target_compile_options(test_color PRIVATE -Wall -Wextra)
endif()

add_test(NAME color COMMAND test_color)
