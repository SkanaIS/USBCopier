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
#include <unordered_set>
#include <algorithm>
#include "resource.h"
#pragma comment(lib, "shlwapi.lib")
using namespace std;

// ��Ϣ������
const int WM_USER_SHELLICON = WM_USER + 1;
const int IDM_EXIT = 1001; // �˳��˵�
const int IDM_AUTOSTART = 1002; // �������˵�
const int IDM_CONFIG = 1003; // ���ò˵�
const int IDM_ABOUT = 1004; // ���ڲ˵�

// ȫ�ֱ���
NOTIFYICONDATA nid;
HINSTANCE hInst;
mutex logMutex;
HMENU hMenu = nullptr;
unordered_set<wstring> whitelistExtensions;
bool enableWhitelist = false;

// ���ַ���ת��ΪСд
wstring ToLower(const wstring& str)
{
    wstring lowerStr = str;
    transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::towlower);
    return lowerStr;
}

// ��ȡ��ǰʱ����ַ���
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

// ת������
string WStringToUTF8(const wstring& wstr)
{
    wstring_convert<codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(wstr);
}

// ��¼��־
void LogMessage(const wstring& message)
{
    wstring logPath = L"D:\\save\\log.txt";
    ofstream logFile(logPath, ios::app);
    if (logFile.is_open()) {
        logFile << WStringToUTF8(GetFormattedCurrentTime() + L" - " + message);
        logFile.close();
    }
}

// ����Ĭ�������ļ�
void CreateDefaultConfig()
{
    wstring configPath = L"D:\\save\\config.txt";
    wofstream configFile(configPath);
    if (configFile.is_open()) {
        configFile << L"enableWhitelist=true\n";
        configFile << L"whitelist=doc,docx,pptx\n";
        configFile.close();
    }
}

// ��ȡ�����ļ�
void ReadConfig()
{
    wstring configPath = L"D:\\save\\config.txt";
    if (!PathFileExistsW(configPath.c_str())) {
        CreateDefaultConfig();
    }

    wifstream configFile(configPath);
    if (configFile.is_open()) {
        wstring line;
        while (getline(configFile, line)) {
            line = ToLower(line);
            if (line.find(L"enablewhitelist=true") != wstring::npos) {
                enableWhitelist = true;
            }
            else if (line.find(L"whitelist=") != wstring::npos) {
                size_t pos = line.find(L"=");
                if (pos != wstring::npos) {
                    wstring extensions = line.substr(pos + 1);
                    wstringstream ss(extensions);
                    wstring ext;
                    while (getline(ss, ext, L',')) {
                        whitelistExtensions.insert(ToLower(ext));
                    }
                }
            }
        }
        configFile.close();
    }
}

// ����ļ��Ƿ��ڰ�������
bool IsFileInWhitelist(const wstring& fileName)
{
    if (!enableWhitelist) return true;
    size_t pos = fileName.find_last_of(L".");
    if (pos != wstring::npos) {
        wstring ext = ToLower(fileName.substr(pos + 1));
        return whitelistExtensions.find(ext) != whitelistExtensions.end();
    }
    return false;
}

//������
//��ȡ��ǰ��ִ���ļ�·��
wstring GetExePath()
{
    wchar_t path[MAX_PATH];//·��
    GetModuleFileNameW(nullptr, path, MAX_PATH);//��ȡ·��
    return path;//����·��
}

// ����ע���������
void SetAutoStart() {
    HKEY hKey = nullptr;
    wstring path = GetExePath(); // ��ȡ��ǰ��ִ���ļ�·��
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

// ȡ��ע���������
void CancelAutoStart()
{
    HKEY hKey = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey);
    RegDeleteValueW(hKey, L"USBCopier");
    RegCloseKey(hKey);
}

// �ݹ鿽��Ŀ¼
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
            if (IsFileInWhitelist(findData.cFileName) && !PathFileExistsW(dstPath.c_str())) {
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

// ���U�̲���
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

            // �����
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
                CheckMenuItem(hMenu, IDM_AUTOSTART, MF_BYCOMMAND | MF_CHECKED);
            }
            else {
                CheckMenuItem(hMenu, IDM_AUTOSTART, MF_BYCOMMAND | MF_UNCHECKED);
            }

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT) {
            DestroyWindow(hWnd);
        }
        if (LOWORD(wParam) == IDM_AUTOSTART) {
            UINT state = GetMenuState(hMenu, IDM_AUTOSTART, MF_BYCOMMAND);
            if (state & MF_CHECKED) {
                MessageBoxW(hWnd, L"USBCopier no longer starts with Windows.", L"USBCopier", MB_OK);
                CancelAutoStart();
                CheckMenuItem(hMenu, IDM_AUTOSTART, MF_BYCOMMAND | MF_UNCHECKED);
            }
            else {
                MessageBoxW(hWnd, L"USBCopier now starts with Windows.", L"USBCopier", MB_OK);
                SetAutoStart();
                CheckMenuItem(hMenu, IDM_AUTOSTART, MF_BYCOMMAND | MF_CHECKED);
            }
        }
		if (LOWORD(wParam) == IDM_CONFIG) {
			// �������ļ�
			ShellExecuteW(hWnd, L"open", L"D:\\save\\config.txt", nullptr, nullptr, SW_SHOWNORMAL);
		}
		if (LOWORD(wParam) == IDM_ABOUT) {
			MessageBoxW(hWnd, L"USBCopier is developed by Kazea. Thank you for using it.\n\nIn accordance with the principles of free software, you are free to use, modify, and distribute this program under the CC BY 4.0 license.", L"About USBCopier", MB_OK|MB_ICONINFORMATION);
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
    AppendMenu(hMenu, MF_STRING, IDM_AUTOSTART, TEXT("�� Windows ����"));
	AppendMenu(hMenu, MF_STRING, IDM_CONFIG, TEXT("���� USBCopier"));
	AppendMenu(hMenu, MF_STRING, IDM_ABOUT, TEXT("���� USBCopier"));
    AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"�˳� USBCopier");
}

// ���
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hInst = hInstance;
    ReadConfig(); // ��ȡ�����ļ�
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