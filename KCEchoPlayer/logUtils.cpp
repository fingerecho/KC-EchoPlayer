#include "logUtils.h"
#include "logUtils.h"
#include <mutex>
#include <io.h>
#include <Windows.h>
#include <fstream>

void WriteLog(const std::wstring& msg)
{
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    auto pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) dir = dir.substr(0, pos);
    std::wstring logPath = dir + L"\\app-basic.log";

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, logPath.c_str(), L"ab, ccs=UTF-16LE") == 0 && fp) {
        // 写 UTF-16 LE BOM（仅首次文件为空时）
        DWORD size = 0;
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(fp)));
        LARGE_INTEGER li;
        li.QuadPart = 0;
        if (SetFilePointerEx(h, li, &li, FILE_END) && li.QuadPart == 0) {
            const wchar_t bom = L'\xFEFF';
            WriteFile(h, &bom, sizeof(wchar_t), &size, nullptr);
        }
        DWORD written = 0;
        WriteFile(h, msg.c_str(), static_cast<DWORD>(msg.size() * sizeof(wchar_t)), &written, nullptr);
        wchar_t newline = L'\n';
        WriteFile(h, &newline, sizeof(wchar_t), &written, nullptr);
        fclose(fp);
    }

    OutputDebugStringW(msg.c_str());
}
