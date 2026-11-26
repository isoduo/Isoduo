#include "desktop_host.hpp"
#include <algorithm>
#include <cstring>

namespace isoduo::detail {
namespace {
// Clipboard managers can hold the global clipboard briefly after a change. Bound the wait
// on the UI thread; report failure rather than waiting indefinitely if another owner stalls.
bool openClipboard(HWND window) {
    constexpr ULONGLONG timeoutMs = 250;
    const auto deadline = GetTickCount64() + timeoutMs;
    do {
        if (OpenClipboard(window)) return true;
        auto now = GetTickCount64();
        if (now >= deadline) break;
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(5, deadline - now)));
    } while (GetTickCount64() < deadline);
    return false;
}
}

namespace {
UINT richFormat() {
    static const UINT format = RegisterClipboardFormatW(L"Isoduo.RichText.1");
    return format;
}
// Read-only compatibility with clipboard owners from before the rename.
UINT legacyRichFormat() {
    static const UINT format = RegisterClipboardFormatW(L"LcarsUI.RichText.1");
    return format;
}
HGLOBAL globalText(std::u16string_view text) {
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
    if (!memory) return nullptr;
    auto data = static_cast<char16_t*>(GlobalLock(memory));
    if (!data) { GlobalFree(memory); return nullptr; }
    std::memcpy(data, text.data(), text.size() * sizeof(char16_t));
    data[text.size()] = 0;
    GlobalUnlock(memory);
    return memory;
}
HGLOBAL globalBytes(std::string_view bytes) {  // NUL-terminated: a global block may be larger than asked for
    auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size() + 1);
    if (!memory) return nullptr;
    auto data = static_cast<char*>(GlobalLock(memory));
    if (!data) { GlobalFree(memory); return nullptr; }
    std::memcpy(data, bytes.data(), bytes.size());
    data[bytes.size()] = 0;
    GlobalUnlock(memory);
    return memory;
}
}
bool WindowsClipboard::write(std::u16string_view text) { return writeRich(text, {}); }
// Windows text carries CRLF line breaks; core hands every clipboard LF. A CRLF already there stays as it is.
static std::u16string windowsLineBreaks(std::u16string_view text) {
    std::u16string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\n' && (i == 0 || text[i - 1] != u'\r')) result += u'\r';
        result += text[i];
    }
    return result;
}
// One clipboard transaction: the plain text and, for a copy from a rich field, the private payload beside it.
// EmptyClipboard drops every older format, so a payload never outlives the text it belonged to.
bool WindowsClipboard::writeRich(std::u16string_view text, std::string_view payload) {
    if (!openClipboard(window_)) return false;
    auto memory = globalText(windowsLineBreaks(text));
    auto rich = payload.empty() ? nullptr : globalBytes(payload);
    bool ok = false;
    if (memory && (payload.empty() || rich)) {
        ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (ok) memory = nullptr;  // the clipboard owns it now
        if (ok && rich) {
            if (SetClipboardData(richFormat(), rich) != nullptr) rich = nullptr;  // plain text still stands if only this fails
        }
    }
    if (memory) GlobalFree(memory);
    if (rich) GlobalFree(rich);
    CloseClipboard();
    return ok;
}
Query<std::string> WindowsClipboard::readRich() {
    if (!openClipboard(window_)) return {Status::failed, u"Cannot open the clipboard", std::nullopt};
    Query<std::string> result{Status::notSupported, u"No rich text", std::nullopt};
    const UINT format = IsClipboardFormatAvailable(richFormat()) ? richFormat() : legacyRichFormat();
    if (IsClipboardFormatAvailable(format)) {
        result = {Status::failed, u"Cannot read rich text", std::nullopt};
        if (auto memory = GetClipboardData(format)) {
            if (auto data = static_cast<const char*>(GlobalLock(memory))) {
                const auto size = GlobalSize(memory);
                std::size_t n = 0;
                while (n < size && data[n]) ++n;
                if (n < size) result = {Status::ok, {}, std::string(data, n)};
                GlobalUnlock(memory);
            }
        }
    }
    CloseClipboard();
    return result;
}
Query<std::u16string> WindowsClipboard::read() {
    if (!openClipboard(window_)) return {Status::failed, u"Cannot open the clipboard", std::nullopt};
    Query<std::u16string> result{Status::notSupported, u"Clipboard has no text", std::nullopt};
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        result = {Status::failed, u"Cannot read text from the clipboard", std::nullopt};
        if (auto memory = GetClipboardData(CF_UNICODETEXT)) {
            if (auto data = static_cast<const char16_t*>(GlobalLock(memory))) {
                auto units = GlobalSize(memory) / sizeof(char16_t);
                std::size_t n = 0;
                while (n < units && data[n]) ++n;
                if (n < units) result = {Status::ok, {}, std::u16string(data, n)};
                GlobalUnlock(memory);
            }
        }
    }
    CloseClipboard();
    return result;
}
}
