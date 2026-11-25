#include "uia_client_helpers.hpp"
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct DemoProcess {
    HANDLE process = nullptr;
    ~DemoProcess() {
        if (!process) return;
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 10000);
        }
        CloseHandle(process);
    }
};

void runDemo(IUIAutomation* uia, const wchar_t* executable, const fs::path& directory,
             const std::wstring& expected, bool reopen) {
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --settings-dir \"" + directory.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info),
            "could not start the demo");
    DemoProcess demo{info.hProcess};
    CloseHandle(info.hThread);

    Search search{info.dwProcessId};
    ComPtr<IUIAutomationElement> root, field;
    for (int attempt = 0; attempt < 100 && !field; ++attempt) {
        if (!search.window) EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
        if (search.window && SUCCEEDED(uia->ElementFromHandle(search.window, root.ReleaseAndGetAddressOf())))
            field.Attach(findId(uia, root.Get(), L"general.name"));
        if (!field && WaitForSingleObject(demo.process, 100) == WAIT_OBJECT_0) break;
    }
    require(field.Get(), "name field unavailable");
    ComPtr<IUIAutomationValuePattern> value;
    require(SUCCEEDED(field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value,
            "name value pattern unavailable");
    if (reopen) {
        BSTR actual = nullptr;
        const HRESULT read = value->get_CurrentValue(&actual);
        const bool matches = actual && expected == actual;
        SysFreeString(actual);
        require(SUCCEEDED(read), "could not read the reopened name");
        require(matches, "reopened name differs from saved name");
    } else {
        require(SUCCEEDED(field->SetFocus()), "could not focus the name field");
        require(SUCCEEDED(setTextValue(value.Get(), expected.c_str())), "rename failed");
        ComPtr<IUIAutomationElement> nav;
        nav.Attach(findId(uia, root.Get(), L"app.nav"));
        require(nav && SUCCEEDED(nav->SetFocus()), "could not leave the name field");
        require(SUCCEEDED(invokeId(uia, root.Get(), L"app.save")), "Save failed");
        const auto file = directory / "settings.json";
        for (int attempt = 0; attempt < 50 && !fs::exists(file); ++attempt) Sleep(100);
        require(fs::is_regular_file(file), "Save did not create settings.json in the test directory");
    }

    ComPtr<IUIAutomationWindowPattern> window;
    require(SUCCEEDED(root->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&window))) &&
            window && SUCCEEDED(window->Close()), "could not close the window");
    DWORD exitCode = 1;
    require(WaitForSingleObject(demo.process, 10000) == WAIT_OBJECT_0 &&
            GetExitCodeProcess(demo.process, &exitCode) && exitCode == 0, "demo did not exit cleanly");
}

bool checkAppearance(IUIAutomation* uia, const wchar_t* executable, const wchar_t* appearance) {
    const auto directory = fs::temp_directory_path() / (L"isoduo smoke " + std::wstring(appearance) + L" " +
        std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(GetTickCount64()));
    require(fs::create_directory(directory), "could not create a fresh settings directory");
    bool passed = false;
    try {
        const std::wstring expected = L"APPDATA " + std::wstring(appearance);
        runDemo(uia, executable, directory, expected, false);
        runDemo(uia, executable, directory, expected, true);
        passed = true;
    } catch (const std::exception& e) {
        std::wcerr << appearance << L": " << e.what() << L'\n';
    }
    fs::remove_all(directory);
    return passed;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    bool passed = false;
    try {
        ComPtr<IUIAutomation> uia;
        require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))),
                "could not initialize UI Automation");
        bool web = checkAppearance(uia.Get(), argv[1], L"web");
        bool material = checkAppearance(uia.Get(), argv[2], L"material");
        passed = web && material;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    CoUninitialize();
    return passed ? 0 : 1;
}
