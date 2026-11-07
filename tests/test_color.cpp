// ============================================================================
//  test_color.cpp — color/ 层的单元测试
//
//  color/ 按设计「完全不依赖 Win32」，所以这里可以直接把三个 .cpp 拉进来
//  编成控制台程序，不需要窗口、GDI+、剪贴板。测的是用户真正会碰到的语义：
//    · HSV / HSL / CMYK 与 RGB 往返是否自洽（含取整误差的上限）
//    · HEX / CSS 文本格式是否就是界面上要显示的样子
//    · 用户手输的解析（含全角字符、百分比、越界钳制、非法输入）
//    · 六种配色方案的颜色个数与关键色相偏移
//
//  不引第三方框架：CHECK 宏 + 计数，失败打印文件行号并返回非 0，CTest 据此判红。
// ============================================================================

#include "color/color.h"
#include "color/harmony.h"
#include "color/parse.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

// 失败信息里只保留 ASCII，避免控制台代码页把中文打成乱码
static std::string Ascii(const std::wstring& text) {
    std::string out;
    for (wchar_t ch : text) {
        out.push_back(ch < 128 ? static_cast<char>(ch) : '?');
    }
    return out;
}

#define CHECK(cond)                                                    \
    do {                                                               \
        if (cond) {                                                    \
            ++g_pass;                                                  \
        } else {                                                       \
            ++g_fail;                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
        }                                                              \
    } while (0)

#define CHECK_WSTR(actual, expect)                                              \
    do {                                                                        \
        const std::wstring a_ = (actual);                                       \
        const std::wstring e_ = (expect);                                       \
        if (a_ == e_) {                                                         \
            ++g_pass;                                                           \
        } else {                                                                \
            ++g_fail;                                                           \
            printf("FAIL %s:%d\n  actual: [%s]\n  expect: [%s]\n", __FILE__,     \
                   __LINE__, Ascii(a_).c_str(), Ascii(e_).c_str());             \
        }                                                                       \
    } while (0)

static bool Near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// 色相要按环比较：359° 和 1° 只差 2°
static bool HueNear(double a, double b, double tolerance = 2.0) {
    double diff = std::fabs(a - b);
    if (diff > 180.0) diff = 360.0 - diff;
    return diff <= tolerance;
}

static bool Same(const color::Rgb& rgb, int r, int g, int b) {
    return rgb.r == r && rgb.g == g && rgb.b == b;
}

// ---------------------------------------------------------------------------
// RGB ↔ HSV
// ---------------------------------------------------------------------------
static void TestHsv() {
    const struct {
        double h, s, v;
        int r, g, b;
    } cases[] = {
        {0, 1.0, 1.0, 255, 0, 0},      {60, 1.0, 1.0, 255, 255, 0},
        {120, 1.0, 1.0, 0, 255, 0},    {180, 1.0, 1.0, 0, 255, 255},
        {240, 1.0, 1.0, 0, 0, 255},    {300, 1.0, 1.0, 255, 0, 255},
        {0, 0.0, 1.0, 255, 255, 255},  {0, 0.0, 0.0, 0, 0, 0},
        {0, 1.0, 0.5, 128, 0, 0},      {210, 1.0, 1.0, 0, 128, 255},
    };
    for (const auto& c : cases) {
        const color::Rgb rgb = color::RgbFromHsv({c.h, c.s, c.v});
        CHECK(Same(rgb, c.r, c.g, c.b));
    }

    const color::Hsv red = color::HsvFromRgb({255, 0, 0});
    CHECK(HueNear(red.h, 0.0));
    CHECK(Near(red.s, 1.0, 1e-9));
    CHECK(Near(red.v, 1.0, 1e-9));

    const color::Hsv grey = color::HsvFromRgb({128, 128, 128});
    CHECK(Near(grey.s, 0.0, 1e-9));         // 灰色没有饱和度

    // 往返自洽：8 位量化会丢一点精度，色相 ±1.5°、饱和度 ±0.01 以内
    for (int hue = 0; hue < 360; hue += 15) {
        for (double s : {0.2, 0.55, 1.0}) {
            for (double v : {0.3, 0.7, 1.0}) {
                const color::Hsv src{static_cast<double>(hue), s, v};
                const color::Hsv back = color::HsvFromRgb(color::RgbFromHsv(src));
                CHECK(HueNear(back.h, src.h));
                CHECK(Near(back.s, src.s, 0.02));
                CHECK(Near(back.v, src.v, 0.02));
            }
        }
    }

    // 越界的色相绕回、饱和度/明度钳到 [0,1]
    const color::Hsv wrap = color::NormalizeHsv({-30.0, 1.5, -0.2});
    CHECK(HueNear(wrap.h, 330.0));
    CHECK(Near(wrap.s, 1.0, 1e-9));
    CHECK(Near(wrap.v, 0.0, 1e-9));
}

// ---------------------------------------------------------------------------
// RGB ↔ HSL / CMYK
// ---------------------------------------------------------------------------
static void TestHslCmyk() {
    const color::Hsl red = color::HslFromRgb({255, 0, 0});
    CHECK(HueNear(red.h, 0.0));
    CHECK(Near(red.s, 1.0, 1e-9));
    CHECK(Near(red.l, 0.5, 1e-9));

    const color::Hsl grey = color::HslFromRgb({128, 128, 128});
    CHECK(Near(grey.s, 0.0, 1e-9));

    CHECK(Same(color::RgbFromHsl({0, 1.0, 0.5}), 255, 0, 0));
    CHECK(Same(color::RgbFromHsl({120, 1.0, 0.5}), 0, 255, 0));
    CHECK(Same(color::RgbFromHsl({240, 1.0, 0.5}), 0, 0, 255));
    CHECK(Same(color::RgbFromHsl({0, 0.0, 1.0}), 255, 255, 255));

    const color::Cmyk black = color::CmykFromRgb({0, 0, 0});
    CHECK(Near(black.k, 1.0, 1e-9));
    CHECK(Near(black.c, 0.0, 1e-9));

    const color::Cmyk white = color::CmykFromRgb({255, 255, 255});
    CHECK(Near(white.c + white.m + white.y + white.k, 0.0, 1e-9));

    const color::Cmyk redCmyk = color::CmykFromRgb({255, 0, 0});
    CHECK(Near(redCmyk.c, 0.0, 1e-9));
    CHECK(Near(redCmyk.m, 1.0, 1e-9));
    CHECK(Near(redCmyk.y, 1.0, 1e-9));

    CHECK(Same(color::RgbFromCmyk({0, 1.0, 1.0, 0}), 255, 0, 0));
    CHECK(Same(color::RgbFromCmyk({0, 0.0, 0.0, 0}), 255, 255, 255));
}

// ---------------------------------------------------------------------------
// 格式化：界面上那几行文本就是这几个函数拼出来的
// ---------------------------------------------------------------------------
static void TestFormat() {
    CHECK_WSTR(color::ToHex({0, 0, 0}), L"#000000");
    CHECK_WSTR(color::ToHex({255, 255, 255}), L"#FFFFFF");
    CHECK_WSTR(color::ToHex({58, 131, 214}), L"#3A83D6");
    CHECK_WSTR(color::ToHex({10, 188, 222}), L"#0ABCDE");

    CHECK_WSTR(color::ToCssRgb({58, 131, 214}), L"rgb(58, 131, 214)");
    // 默认色 #3A83D6 = hsv(212, 73%, 84%) = hsl(212, 66%, 53%) = cmyk(73%, 39%, 0%, 16%)
    CHECK_WSTR(color::ToCssHsv({212, 0.729, 0.839}), L"hsv(212, 73%, 84%)");
    CHECK_WSTR(color::ToCssHsl({58, 131, 214}), L"hsl(212, 66%, 53%)");
    CHECK_WSTR(color::ToCssCmyk({58, 131, 214}), L"cmyk(73%, 39%, 0%, 16%)");

    // 色相环绕到 0 附近不能显示成 360
    CHECK_WSTR(color::ToCssHsv({359.7, 1.0, 1.0}), L"hsv(0, 100%, 100%)");
    CHECK_WSTR(color::ToCssHsv({-0.4, 1.0, 1.0}), L"hsv(0, 100%, 100%)");

    // 0xAARRGGBB 打包（GDI+ 用）
    CHECK(color::Argb({0x12, 0x34, 0x56}) == 0xFF123456u);
    CHECK(color::Argb({0x12, 0x34, 0x56}, 0x80) == 0x80123456u);

    // ToByte：四舍五入 + 钳制
    CHECK(color::ToByte(-10.0) == 0);
    CHECK(color::ToByte(300.0) == 255);
    CHECK(color::ToByte(127.4) == 127);
    CHECK(color::ToByte(127.6) == 128);
}

// ---------------------------------------------------------------------------
// HEX 解析
// ---------------------------------------------------------------------------
static void TestParseHex() {
    color::Rgb rgb;

    CHECK(color::ParseHex(L"#abc", &rgb) && Same(rgb, 0xAA, 0xBB, 0xCC));
    CHECK(color::ParseHex(L"abc", &rgb) && Same(rgb, 0xAA, 0xBB, 0xCC));
    CHECK(color::ParseHex(L"#12AB34", &rgb) && Same(rgb, 0x12, 0xAB, 0x34));
    CHECK(color::ParseHex(L"0xFF8800", &rgb) && Same(rgb, 255, 0x88, 0));
    CHECK(color::ParseHex(L"0Xff8800", &rgb) && Same(rgb, 255, 0x88, 0));
    CHECK(color::ParseHex(L"   #3a83d6   ", &rgb) && Same(rgb, 58, 131, 214));  // 前后空格

    CHECK(!color::ParseHex(L"", &rgb));
    CHECK(!color::ParseHex(L"#ff88", &rgb));            // 4 位不认
    CHECK(!color::ParseHex(L"#gggggg", &rgb));          // 非十六进制字符
    CHECK(!color::ParseHex(L"rgb(1,2,3)", &rgb));       // 不是 HEX 语法
    CHECK(!color::ParseHex(L"#abc", nullptr));          // 空指针要安全返回 false
}

// ---------------------------------------------------------------------------
// 用户手输的解析（百分比 / 全角 / 越界 / 非法）
// ---------------------------------------------------------------------------
static void TestParseOthers() {
    color::Rgb rgb;
    color::Hsv hsv;
    color::Hsl hsl;
    color::Cmyk cmyk;

    CHECK(color::ParseRgb(L"rgb(58, 131, 214)", &rgb) && Same(rgb, 58, 131, 214));
    CHECK(color::ParseRgb(L"58, 131, 214", &rgb) && Same(rgb, 58, 131, 214));
    CHECK(color::ParseRgb(L"58 131 214", &rgb) && Same(rgb, 58, 131, 214));
    CHECK(color::ParseRgb(L"300, -5, 20", &rgb) && Same(rgb, 255, 0, 20));   // 钳到 0..255
    CHECK(color::ParseRgb(L"100%, 50%, 0%", &rgb) && Same(rgb, 255, 128, 0));  // 百分比按 255 折算
    CHECK(!color::ParseRgb(L"1, 2", &rgb));               // 分量个数不对
    CHECK(!color::ParseRgb(L"1, 2, abc", &rgb));          // 有非法数字

    CHECK(color::ParseHsv(L"212, 73%, 84%", &hsv));
    CHECK(HueNear(hsv.h, 212) && Near(hsv.s, 0.73, 1e-9) && Near(hsv.v, 0.84, 1e-9));
    CHECK(color::ParseHsv(L"hsv(212, 0.73, 0.84)", &hsv));   // 分数写法
    CHECK(Near(hsv.s, 0.73, 1e-9) && Near(hsv.v, 0.84, 1e-9));
    CHECK(color::ParseHsv(L"400, 100%, 100%", &hsv) && HueNear(hsv.h, 40));
    CHECK(!color::ParseHsv(L"212, 73%", &hsv));

    CHECK(color::ParseHsl(L"hsl(240, 100%, 50%)", &hsl));
    CHECK(HueNear(hsl.h, 240) && Near(hsl.s, 1.0, 1e-9) && Near(hsl.l, 0.5, 1e-9));
    CHECK(color::ParseHsl(L"240, 1, 0.5", &hsl) &&
          Near(hsl.s, 1.0, 1e-9) && Near(hsl.l, 0.5, 1e-9));

    CHECK(color::ParseCmyk(L"cmyk(73%, 39%, 0%, 16%)", &cmyk));
    CHECK(Near(cmyk.c, 0.73, 1e-9) && Near(cmyk.m, 0.39, 1e-9) &&
          Near(cmyk.y, 0.0, 1e-9) && Near(cmyk.k, 0.16, 1e-9));
    CHECK(color::ParseCmyk(L"73, 39, 0, 16", &cmyk) && Near(cmyk.c, 0.73, 1e-9));
    CHECK(!color::ParseCmyk(L"73, 39, 0", &cmyk));

    // 全角字符：中文输入法直接打出来的样子
    CHECK(color::ParseHsv(L"２１２，７３％，８４％", &hsv));
    CHECK(HueNear(hsv.h, 212) && Near(hsv.s, 0.73, 1e-9) && Near(hsv.v, 0.84, 1e-9));
    CHECK(color::ParseCmyk(L"（７３％，３９％，０％，１６％）", &cmyk));
    CHECK(Near(cmyk.c, 0.73, 1e-9) && Near(cmyk.k, 0.16, 1e-9));
}

// ---------------------------------------------------------------------------
// 配色方案
// ---------------------------------------------------------------------------
static void TestHarmony() {
    const color::Hsv base{210, 0.8, 0.9};

    for (int i = 0; i < color::kHarmonyTypeCount; ++i) {
        const auto type = static_cast<color::HarmonyType>(i);
        const auto palette = color::HarmonyPalette(base, type);
        CHECK(palette.size() == 5);                       // 界面固定 5 个芯片
        CHECK(color::HarmonyName(type) != nullptr);
        CHECK(std::wstring(color::HarmonyName(type)).size() > 0);
    }

    // 互补：第 2 个是 base + 180°
    {
        const auto p = color::HarmonyPalette(base, color::HarmonyType::Complementary);
        CHECK(HueNear(color::HsvFromRgb(p[0]).h, 210));
        CHECK(HueNear(color::HsvFromRgb(p[1]).h, 30, 3));
    }
    // 邻近：±40 / ±20
    {
        const auto p = color::HarmonyPalette(base, color::HarmonyType::Analogous);
        CHECK(HueNear(color::HsvFromRgb(p[0]).h, 170, 3));
        CHECK(HueNear(color::HsvFromRgb(p[2]).h, 210, 3));
        CHECK(HueNear(color::HsvFromRgb(p[4]).h, 250, 3));
    }
    // 三角：+120 / +240
    {
        const auto p = color::HarmonyPalette(base, color::HarmonyType::Triadic);
        CHECK(HueNear(color::HsvFromRgb(p[1]).h, 330, 3));
        CHECK(HueNear(color::HsvFromRgb(p[2]).h, 90, 3));
    }
    // 四方：+90 / +180 / +270
    {
        const auto p = color::HarmonyPalette(base, color::HarmonyType::Tetradic);
        CHECK(HueNear(color::HsvFromRgb(p[1]).h, 300, 3));
        CHECK(HueNear(color::HsvFromRgb(p[2]).h, 30, 3));
        CHECK(HueNear(color::HsvFromRgb(p[3]).h, 120, 3));
    }
    // 单色调：色相不变，明度递减
    {
        const auto p = color::HarmonyPalette(base, color::HarmonyType::Monochromatic);
        const double v0 = color::HsvFromRgb(p[0]).v;
        const double v4 = color::HsvFromRgb(p[4]).v;
        CHECK(v0 > v4);
        for (size_t i = 1; i < p.size(); ++i) {
            CHECK(color::HsvFromRgb(p[i]).v <= color::HsvFromRgb(p[i - 1]).v + 0.02);
        }
    }
}

int main() {
    TestHsv();
    TestHslCmyk();
    TestFormat();
    TestParseHex();
    TestParseOthers();
    TestHarmony();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
