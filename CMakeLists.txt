cmake_minimum_required(VERSION 3.16)

project(Palette VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    # 资源编译器（图标 + 版本信息）。RC 语言要显式打开，
    # 否则 .rc 文件不会进 target，图标和版本号就都丢了。
    enable_language(RC)
    # app.rc 里用 "palette.ico" 引用图标，文件在 assets\ 下，
    # 所以要把 assets 加进资源编译器的头文件搜索路径。
    if(MSVC)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I${CMAKE_CURRENT_SOURCE_DIR}/src/app /I${CMAKE_CURRENT_SOURCE_DIR}/assets")
    else()
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/src/app -I${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()
endif()

add_executable(Palette WIN32
    src/main.cpp
    src/app/app.cpp
    src/app/app.h
    src/app/app.rc
    src/app/resource.h
    src/color/color.cpp
    src/color/color.h
    src/color/harmony.cpp
    src/color/harmony.h
    src/color/parse.cpp
    src/color/parse.h
    src/platform/clipboard.cpp
    src/platform/clipboard.h
    src/platform/dpi.cpp
    src/platform/dpi.h
    src/ui/gdiplus_include.h
    src/ui/layout.cpp
    src/ui/layout.h
    src/ui/render.cpp
    src/ui/render.h
    src/ui/theme.cpp
    src/ui/theme.h
)

target_include_directories(Palette PRIVATE src)
target_compile_definitions(Palette PRIVATE UNICODE _UNICODE NOMINMAX
                                           _WIN32_WINNT=0x0601 WINVER=0x0601)
target_link_libraries(Palette PRIVATE user32 gdi32 gdiplus comctl32)

if(MSVC)
    target_compile_options(Palette PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(Palette PRIVATE -Wall -Wextra)
    if(MINGW)
        # Produce a self-contained executable that does not require GCC DLLs.
        # -mwindows 必须显式给：少了它 exe 会按 CONSOLE 子系统链接，
        # 双击时弹出一个黑命令行窗口，关掉那个窗口进程就跟着退出。
        target_link_options(Palette PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
    endif()
endif()

# 产物统一输出到 bin\：根目录保持纯工程（源码 / 构建脚本 / 文档），
# 编译中间文件仍然全在 build\。全工程只有 bin\ 这一份 Palette.exe。
# 以前 build\ 和根目录各躺一个 exe，改完图标分不清哪个是新的
# （Windows 又是按路径缓存图标，两处都显示旧的，很难判断）。
set_target_properties(Palette PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    # 多配置生成器（MSVC）默认还会再套一层 Release\；显式钉死，
    # 别再造出 bin\Release\Palette.exe 这种第二个副本。
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 图标是资源编译器的输入，改了 .ico 必须重新编资源 —— 用 OBJECT_DEPENDS
# 把 .ico 挂进依赖图，免得改了图标忘了重跑 windres。
if(WIN32)
    set_property(SOURCE src/app/app.rc APPEND PROPERTY
                 OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/palette.ico)
endif()

# ---------------------------------------------------------------------------
#  测试（CTest）：只覆盖 color/ 这个纯逻辑层（不依赖 Win32）
#  跑法： ctest --test-dir build --output-on-failure
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

