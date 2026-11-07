#include "color/parse.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// A number read out of user input, remembering whether it carried a '%'.
struct Number {
    double value = 0.0;
    bool percent = false;
    bool valid = false;
};

std::wstring Trim(const std::wstring& text) {
    const wchar_t* spaces = L" \t\r\n";
    const size_t first = text.find_first_not_of(spaces);
    if (first == std::wstring::npos) {
        return std::wstring();
    }
    const size_t last = text.find_last_not_of(spaces);
    return text.substr(first, last - first + 1);
}

// Full-width characters are what a Chinese IME produces by default, so they
// are folded to their ASCII counterparts before anything else happens.
std::wstring NormalizeWide(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch >= 0xFF10 && ch <= 0xFF19) {          // full-width digits
            result.push_back(static_cast<wchar_t>(L'0' + (ch - 0xFF10)));
        } else if (ch == 0xFF0E) {                   // ．
            result.push_back(L'.');
        } else if (ch == 0xFF0C) {                   // ，
            result.push_back(L',');
        } else if (ch == 0xFF05) {                   // ％
            result.push_back(L'%');
        } else if (ch == 0xFF0D) {                   // －
            result.push_back(L'-');
        } else if (ch == 0xFF08) {                   // （
            result.push_back(L'(');
        } else if (ch == 0xFF09) {                   // ）
            result.push_back(L')');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

// Removes an "rgb(...)"-style wrapper and returns whatever is inside.
std::wstring StripCall(const std::wstring& text) {
    const size_t open = text.find(L'(');
    if (open == std::wstring::npos) {
        return text;
    }
    const size_t close = text.rfind(L')');
    if (close == std::wstring::npos || close < open) {
        return text;
    }
    return text.substr(open + 1, close - open - 1);
}

// Splits on commas and whitespace.
std::vector<std::wstring> SplitTokens(const std::wstring& text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == L',' || ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' ||
            ch == L'\t' || ch == 0x3000 /* ideographic space */) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

Number ParseNumber(const std::wstring& token) {
    Number number;
    std::wstring text = Trim(token);
    if (text.empty()) {
        return number;
    }
    if (text.back() == L'%') {
        number.percent = true;
        text.pop_back();
        text = Trim(text);
    }
    if (text.empty()) {
        return number;
    }
    try {
        size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) {
            return number;
        }
        number.value = value;
        number.valid = true;
    } catch (...) {
        // Any malformed input simply stays invalid.
    }
    return number;
}

int HexDigit(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return static_cast<int>(ch - L'0');
    if (ch >= L'a' && ch <= L'f') return static_cast<int>(ch - L'a') + 10;
    if (ch >= L'A' && ch <= L'F') return static_cast<int>(ch - L'A') + 10;
    return -1;
}

// Reads a saturation/value-like component. "73%" and "73" mean 73%, while a
// value written as a plain fraction ("0.73") is taken at face value.
double RatioValue(const Number& number) {
    if (number.percent || number.value > 1.0) {
        return number.value / 100.0;
    }
    return number.value;
}

}  // namespace

namespace color {

bool ParseHex(const std::wstring& text, Rgb* out) {
    if (!out) {
        return false;
    }

    std::wstring body = Trim(NormalizeWide(text));
    if (body.empty()) {
        return false;
    }
    if (body[0] == L'#') {
        body.erase(0, 1);
    } else if (body.size() > 2 && body[0] == L'0' && (body[1] == L'x' || body[1] == L'X')) {
        body.erase(0, 2);
    }
    body = Trim(body);

    if (body.size() != 3 && body.size() != 6) {
        return false;
    }

    unsigned components[3] = {0, 0, 0};
    for (size_t i = 0; i < body.size(); ++i) {
        const int digit = HexDigit(body[i]);
        if (digit < 0) {
            return false;
        }
        if (body.size() == 3) {
            components[i] = static_cast<unsigned>(digit * 17);   // #abc -> #aabbcc
        } else if (i % 2 == 0) {
            components[i / 2] = static_cast<unsigned>(digit) << 4;
        } else {
            components[i / 2] |= static_cast<unsigned>(digit);
        }
    }

    out->r = static_cast<uint8_t>(components[0]);
    out->g = static_cast<uint8_t>(components[1]);
    out->b = static_cast<uint8_t>(components[2]);
    return true;
}

bool ParseRgb(const std::wstring& text, Rgb* out) {
    if (!out) {
        return false;
    }

    const auto tokens = SplitTokens(StripCall(NormalizeWide(text)));
    if (tokens.size() != 3) {
        return false;
    }

    double values[3] = {0.0, 0.0, 0.0};
    for (size_t i = 0; i < 3; ++i) {
        const Number number = ParseNumber(tokens[i]);
        if (!number.valid) {
            return false;
        }
        values[i] = number.percent ? number.value / 100.0 * 255.0 : number.value;
    }

    out->r = ToByte(values[0]);
    out->g = ToByte(values[1]);
    out->b = ToByte(values[2]);
    return true;
}

bool ParseHsv(const std::wstring& text, Hsv* out) {
    if (!out) {
        return false;
    }

    const auto tokens = SplitTokens(StripCall(NormalizeWide(text)));
    if (tokens.size() != 3) {
        return false;
    }

    Number numbers[3];
    for (size_t i = 0; i < 3; ++i) {
        numbers[i] = ParseNumber(tokens[i]);
        if (!numbers[i].valid) {
            return false;
        }
    }

    out->h = numbers[0].value;
    out->s = RatioValue(numbers[1]);
    out->v = RatioValue(numbers[2]);
    *out = NormalizeHsv(*out);
    return true;
}

bool ParseHsl(const std::wstring& text, Hsl* out) {
    if (!out) {
        return false;
    }

    const auto tokens = SplitTokens(StripCall(NormalizeWide(text)));
    if (tokens.size() != 3) {
        return false;
    }

    Number numbers[3];
    for (size_t i = 0; i < 3; ++i) {
        numbers[i] = ParseNumber(tokens[i]);
        if (!numbers[i].valid) {
            return false;
        }
    }

    out->h = numbers[0].value;
    out->s = RatioValue(numbers[1]);
    out->l = RatioValue(numbers[2]);
    out->h = std::fmod(std::max(0.0, out->h), 360.0);
    out->s = std::min(1.0, std::max(0.0, out->s));
    out->l = std::min(1.0, std::max(0.0, out->l));
    return true;
}

bool ParseCmyk(const std::wstring& text, Cmyk* out) {
    if (!out) {
        return false;
    }

    const auto tokens = SplitTokens(StripCall(NormalizeWide(text)));
    if (tokens.size() != 4) {
        return false;
    }

    Number numbers[4];
    for (size_t i = 0; i < 4; ++i) {
        numbers[i] = ParseNumber(tokens[i]);
        if (!numbers[i].valid) {
            return false;
        }
    }

    out->c = std::min(1.0, std::max(0.0, RatioValue(numbers[0])));
    out->m = std::min(1.0, std::max(0.0, RatioValue(numbers[1])));
    out->y = std::min(1.0, std::max(0.0, RatioValue(numbers[2])));
    out->k = std::min(1.0, std::max(0.0, RatioValue(numbers[3])));
    return true;
}

}  // namespace color
