#include "Log.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <share.h>

namespace
{
    std::FILE* g_file      = nullptr;
    bool       g_noLogFile = false;  // already reported once; never try again
    std::mutex g_mutex;

    // The crash handler's own handle on the same file, opened after g_file has truncated it.
    //
    // This used to be g_mutex with try_to_lock, and that silently threw the whole crash report away:
    // the game is multi-threaded (render, input, the SkyCraft reader), so a fault on the render thread
    // while any of them happened to be inside WriteLocked lost every line of the report. That is not a
    // rare race to shrug at - it is exactly the crash we are chasing, and the run came back with a log
    // that stopped four seconds before the fault and nothing in between, so we could not even tell which
    // hook had killed the game. A lost crash line is the one failure this file cannot have.
    //
    // FILE_APPEND_DATA is what removes the need for any lock: every WriteFile on it goes to the current
    // end of the file as one step, so two threads appending cannot interleave or overwrite, and a thread
    // that re-enters WriteRaw from inside its own WriteFile cannot deadlock. Nothing here takes
    // g_mutex, so it cannot lose the report to another thread either, and it touches no CRT, no heap
    // and no std::string - a fault inside the handler must not become the second fault.
    HANDLE g_crashFile = INVALID_HANDLE_VALUE;

    // Caller holds g_mutex.
    void WriteLocked(const char* line)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::fprintf(g_file, "[%02d:%02d:%02d.%03d] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
        std::fflush(g_file);
    }
}

void mclog::Init()
{
    // Under the mutex and only once: crashlog's handler and the detours can call Write() from any
    // thread, and an unguarded g_file would let one of them read the pointer while fopen was still
    // writing it - or, worse, hand a half-initialised FILE* to fprintf.
    std::lock_guard lock(g_mutex);
    if (g_file || g_noLogFile)
        return;

    if (const char* value = std::getenv("MAXCRAFT_VERBOSE"))
        verbose.store(*value && *value != '0', std::memory_order_relaxed);

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"MaxCraft.log");
    // Truncate once, then keep the file open in append mode. Append, not "w": the crash handler writes
    // through its own handle (g_crashFile below), and a "w" stream writes at its own position - the
    // first ordinary line after a handled fault would land on top of the crash report.
    if (FILE* truncate = _wfsopen(path, L"w", _SH_DENYNO))
        std::fclose(truncate);
    g_file = _wfsopen(path, L"a", _SH_DENYNO);  // readable while the game runs
    // Opened after the "w" above, so the truncate has already happened and both handles only append
    // from here on. FILE_SHARE_WRITE because g_file holds the file open for writing at the same time;
    // FILE_FLAG_WRITE_THROUGH so a line reaches the disk before the process dies with it.
    g_crashFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (!g_file) {
        // A game folder we may not write to (Program Files, or a read-only install) leaves every line
        // of this session invisible, so say so on the one channel that is still open and then stop
        // trying: a failed open per line would be a syscall storm on the game thread, and the mod has
        // to keep running either way. Windows Error Reporting's answer is "no faulting module".
        wchar_t reason[128]{};
        swprintf_s(reason, L"MaxCraft: MaxCraft.log could not be opened next to maxpayne2.exe (error %lu)\n", GetLastError());
        OutputDebugStringW(reason);
        g_noLogFile = true;
    }
}

void mclog::Write(const std::string& line)
{
    std::lock_guard lock(g_mutex);
    if (!g_file)
        return;
    WriteLocked(line.c_str());
}

void mclog::WriteRaw(const char* line)
{
    // No lock, no CRT, no heap: the crash handler's path. See g_crashFile for why the mutex had to go.
    // try_to_lock used to guard this and it cost us a whole crash report: the game runs several threads
    // that log, and losing the lock to any of them dropped every line, which is how the run we needed
    // ended with a log that just stopped.
    if (g_crashFile == INVALID_HANDLE_VALUE || !line)
        return;

    // "[hh:mm:ss.mmm] " + the line + '\n', in one fixed buffer and one WriteFile, so a report can never
    // land in two pieces. std::snprintf would be shorter but it takes the CRT lock, and a second fault
    // inside a vectored exception handler is fatal - the timestamp is the one thing worth spelling out.
    // 640, not 512: crashlog's Reporter fills up to kLineBytes-1 = 511 characters before it stops, and
    // a crash report that loses its last few characters at exactly the wrong line is the failure this
    // whole path exists to prevent.
    char   out[640]{};
    DWORD  at = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    auto two = [&](DWORD value) {
        out[at++] = static_cast<char>('0' + value / 10);
        out[at++] = static_cast<char>('0' + value % 10);
    };
    out[at++] = '[';
    two(t.wHour);
    out[at++] = ':';
    two(t.wMinute);
    out[at++] = ':';
    two(t.wSecond);
    out[at++] = '.';
    for (int place = 100; place >= 1; place /= 10)
        out[at++] = static_cast<char>('0' + t.wMilliseconds / place % 10);
    out[at++] = ']';
    out[at++] = ' ';

    const DWORD room = static_cast<DWORD>(sizeof(out)) - at - 1;  // keeps room for the '\n'
    DWORD       copy = 0;
    while (line[copy] && copy < room)
        ++copy;
    for (DWORD i = 0; i < copy; ++i)
        out[at + i] = line[i];
    at += copy;
    out[at++] = '\n';

    DWORD written = 0;
    WriteFile(g_crashFile, out, at, &written, nullptr);
}

std::string mclog::Utf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int   bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}

bool mclog::Verbose()
{
    return verbose.load(std::memory_order_relaxed);
}
