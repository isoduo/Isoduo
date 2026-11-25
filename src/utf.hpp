#pragma once
#include <codecvt>
#include <locale>
#include <string>
#include <string_view>
// C++20 still provides codecvt for portable UTF-8/UTF-16 conversion.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
namespace isoduo::detail {
inline std::u16string utf16FromUtf8(std::string_view value) {
    if (value.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conversion;
    return conversion.from_bytes(value.data(), value.data() + value.size());
}
inline std::string utf8FromUtf16(std::u16string_view value) {
    if (value.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conversion;
    return conversion.to_bytes(value.data(), value.data() + value.size());
}
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
