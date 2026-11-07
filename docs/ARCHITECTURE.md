# Palette 架构说明

> 面向要改这份代码的人（包括几个月后的我）。讲清楚：东西在哪、谁调谁、
> 颜色怎么从一次拖动走到界面上的每一块色块，以及那些**踩过一次就不想再踩**的坑。

---

## 1. 它是什么

一个 Win32 + GDI+ 的取色 / 调色 / 配色小工具：拖动色相与饱和度/明度取色，
右侧给出 HEX / RGB / HSV / HSL / CMYK 五行数值（可直接手写修改），下面一排六种配色方案。

界面**整窗自绘**（没有任何布局控件），唯一的原生控件是一个复用的 `EDIT`，
叠在"正在编辑的那一行数值"上面。

技术栈：C++17 + Win32 API + GDI+ + Win32 资源（图标/版本信息）。无第三方 GUI 库。

---

## 2. 四层结构与依赖方向

```
┌────────────────────────────────────────────────────────────────┐
│  src/main.cpp    WinMain：DPI 感知 → GDI+ 启动 → 建窗 → 消息循环 │
└───────────────────────────┬────────────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────────────┐
│  src/app/        Application：窗口过程、命中测试、拖拽、编辑框、  │
│                  复制/提示、状态（hsv_ 是唯一权威）              │
└───────────────────────────┬────────────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────────────┐
│  src/ui/         theme（配色/字体）· layout（几何）· render（绘制原语）│
└───────────────────────────┬────────────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────────────┐
│  src/platform/   dpi（Per-Monitor V2 / 缩放）· clipboard（写文本）│
└───────────────────────────┬────────────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────────────┐
│  src/color/      color（RGB/HSV/HSL/CMYK 互转 + 格式化）·         │
│                  harmony（六种配色）· parse（解析用户输入）       │
│                  ★ 零 Win32 依赖，可单独复用，也就是 tests/ 测的那层 │
└────────────────────────────────────────────────────────────────┘
```

依赖只能往下。**`color/` 里不许出现 `windows.h`** —— 这条是硬约定：
它保证色彩数学能在 `tests/` 里直接编成控制台程序跑，不用开窗口。

---

## 3. 目录职责

| 路径 | 放什么 |
|---|---|
| `src/main.cpp` | 只做启动：`EnableDpiAwareness()` → `GdiplusStartup` → `Application::Create/Run` → `GdiplusShutdown` |
| `src/app/app.h` `.cpp` | `app::Application`：窗口过程、`HitTest`、拖拽状态机、`Draw*Card` 各卡片绘制、原生 `EDIT` 的创建/提交/取消、复制与 toast |
| `src/app/app.rc` `resource.h` | 图标 + `VS_VERSION_INFO`；资源 ID |
| `src/ui/theme.*` | 设计令牌：所有颜色常量与字体（DPI 变化时重建字体） |
| `src/ui/layout.*` | `ui::BuildLayout(clientW, clientH, scale)`：**唯一的几何来源**，输出所有矩形（物理像素） |
| `src/ui/render.*` | 绘制原语：圆角矩形、渐变、描边、文本、胶囊按钮 |
| `src/ui/gdiplus_include.h` | 统一封装 GDI+ 头（MSVC / MinGW 差异 + `min/max` 之类） |
| `src/platform/dpi.*` | Per-Monitor V2 声明、DPI 缩放、按 DPI 调整窗口尺寸 |
| `src/platform/clipboard.*` | 往剪贴板写文本 |
| `src/color/color.*` | RGB↔HSV/HSL/CMYK、`NormalizeHsv`、`Argb`、`ToHex/ToCss*`、`ToByte` |
| `src/color/harmony.*` | 六种配色方案（互补/邻近/三角/分裂互补/四方/单色），固定返回 5 个颜色 |
| `src/color/parse.*` | 解析用户手输：HEX / rgb() / hsv() / hsl() / cmyk()，含全角字符折叠 |
| `assets/palette.ico` | 唯一构建输入（由 `tools/make_icon.py` 生成） |
| `tools/` | 开发期脚本：图标生成、界面冒烟测试、整窗截图 |
| `tests/` | `color/` 的单元测试（CTest） |
| `docs/` | 本文件 |
| `bin\` | 成品输出目录（`Palette.exe`），整个目录拷走即用 |
| `build/` | CMake 生成的中间文件（不入库） |

---

## 4. 颜色是怎么流动的

```
用户拖动 / 滚轮 / 手输
        │
        ▼
app::Application（命中测试 → DragMode::Sv | Hue | Rgb | ValueField）
        │
        ├─► color::Hsv  hsv_       ★ 唯一的权威编辑状态
        │        │
        │        ├─► RgbFromHsv      → 预览色块 / 取色器标记 / HEX·RGB 行
        │        ├─► HslFromRgb      → HSL 行
        │        ├─► CmykFromRgb     → CMYK 行
        │        └─► HarmonyPalette  → 六个配色芯片 + 五块配色色块
        │
        └─► color::Parse*  → 手输数值 → 折回 hsv_（非法输入不改色，弹提示）
```

**为什么以 HSV 为准**：颜色变成黑 / 白 / 灰时 RGB 里已经没有色相信息，
HSV 能把色相留住 —— 否则拖到最暗端，色相条会莫名其妙跳回红色（0°）。

**RGB 滑条是第二条入口**：它直接改单个通道，再经 `HsvFromRgb` 折回 `hsv_`。
所以拖 RGB 滑条时，取色器方块、色相标记、五行数值、配色色块全都同步跟着变。
（早期只改了 RGB 数字不回收状态，画面不动的 bug 就是这么来的。）

---

## 5. 布局与 DPI

`ui::BuildLayout(clientWidth, clientHeight, scale)` 收客户区的**物理像素**和 DPI 缩放，
返回所有控件矩形的物理像素坐标。里面所有尺寸先按 **96 DPI 的逻辑像素**写死，最后统一乘 `scale`，
所以任何 DPI 下比例一致。

- `Application::S(v)` 是同样的换算（逻辑 → 物理），绘制时用它。
- 窗口最小尺寸 **940×640（逻辑像素）**，低于这个值布局会塌，用 `WM_GETMINMAXINFO` 锁住。
- DPI 变化（拖到另一块屏幕）会重建字体（`theme_`）并按新缩放重算布局。

---

## 6. 自绘 + 一行原生编辑框

整窗自己画，但"改数值"这一件事交给原生 `EDIT`：

- 只创建一个编辑框（`editor_`），复用给 5 行；`ShowEditorFor(row)` 时把它移到那一行上面
  （`SyncEditorRect()` 跟着布局走）。
- 子类化用 `SetWindowSubclass`，并在 `WM_GETDLGCODE` 返回 `DLGC_WANTALLKEYS`，
  否则回车 / Esc / Tab 会被默认处理吞掉。
- 背景色用 `WM_CTLCOLOREDIT` 设成和行底色一致 —— 注意**控件句柄在 `lParam`**，`wParam` 是 HDC。
- 父窗口吞掉了 `WM_LBUTTONDOWN`（没调 `DefWindowProc`），系统不会移走焦点，
  所以编辑框收不到 `EN_KILLFOCUS`。**"点别处提交"必须自己实现**，不能指望焦点消息。
- 编辑框打开的那一行固定画常态底色 `row`（不是 `rowHover`），否则编辑框背景和行底色会差一圈。

---

## 7. 命中测试与拖拽

`HitTest(x, y)` 把坐标映射成 `HitKind`（`Sv` / `Hue` / `RgbSlider` / `ValueField` /
`ValueCopy` / `Chip` / `Swatch` / `Recent`）+ 索引；`WM_LBUTTONDOWN` 据此进入
`DragMode`（`Sv` / `Hue` / `Rgb`），`WM_MOUSEMOVE` 期间只做对应的更新，`WM_LBUTTONUP` 收尾。

- 行上**点数值区** → 打开编辑框；**点行的其它位置** → 复制该行数值（`ValueCopy`）。
- 配色芯片点一下切方案；配色色块 / 最近使用点一下应用该色。
- 滚轮：色相微调、RGB 单通道微调（按住 Ctrl 步长 10）。

---

## 8. 构建、测试、运行

```bat
build.bat                  :: 配置 + 编译，产物落在 bin\Palette.exe
```

脚本自动挑工具链：PATH 里有 MSVC 就用 MSVC，否则用 MinGW-w64（在 `PATH` 上，或在 `.build.local.bat` 指定）；
CMake 优先 PATH，找不到就用 Python 环境里那份。

```bat
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure      :: 单元测试（color/ 层）
```

两类测试分工明确：

| | 测什么 | 怎么跑 | 开销 |
|---|---|---|---|
| `tests/` | `color/` 的色彩数学、解析规则、配色方案 | `ctest` | 秒级、不开窗口、可 CI |
| `tools/ui_check.py` | 自绘界面：定位数值行、点复制读剪贴板、写数值改色、拖滑条、最小尺寸不塌 | `python tools\ui_check.py` | 会真的开窗口截图（24 项断言） |

`tools/shoot_window.py` 是另一件事：抓带标题栏的整窗截图，并核对 `WM_GETICON` 的图标句柄。

---

## 9. 版本号与图标

- 版本号两处同步：`src/app/app.rc` 的 `VS_VERSION_INFO`（`1,0,0,0`）与
  `CMakeLists.txt` 的 `project(Palette VERSION 1.0.0.0)`。改完重编，在「属性 → 详细信息」核对。
- 图标由 `tools/make_icon.py` 生成（10 档尺寸，每档单独渲染）；`CMakeLists.txt` 用
  `OBJECT_DEPENDS` 把 `.ico` 挂进资源编译器的依赖图，改了图标重编就会自动重跑 `windres`。

---

## 10. 改动前值得先看一眼的坑

（完整清单在 README 的「踩过的坑」一节，这里只列最容易被新代码踩到的）

1. **GDI+ `ResetClip()` 会丢调用方的裁剪区** —— 画渐变时用 `GraphicsState` 保存/恢复。
2. **一个 `.ico` 只挂一个资源 ID**：拆成大/小两组会出现两个 `RT_GROUP_ICON`，资源管理器只认 ID 最小的那组。
3. **只设窗口类图标不够**：任务栏和 Alt+Tab 走 `WM_GETICON`，要显式 `WM_SETICON`。
4. **画渐变要"贴"到控件位置**，不能拉伸铺满画布再靠遮罩裁（否则只显示渐变中段，色相环的红/品红直接消失）。
5. **`GetProcAddress` 转函数指针用 `memcpy`**，直接 `reinterpret_cast` 会触发 `-Wcast-function-type`。
6. **任何按行底色定位的脚本都要认两种底色**（`row` / `rowHover`），否则悬停那行会被跳过、行号整体错位。
