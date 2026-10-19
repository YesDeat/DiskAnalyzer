// ============================================================================
//  DiskAnalyzer - 磁盘空间分析与一键清理工具
//  EN: DiskAnalyzer - disk space analyzer with one-click cleanup
//  开发环境 / Toolchain: Visual Studio 2022 (v143), Unicode, Win32 API
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 自定义消息 / Custom window messages
#define WM_SCAN_TICK   (WM_APP + 1)  // 扫描进度刷新 / scan progress refresh
#define WM_SCAN_DONE   (WM_APP + 2)  // 扫描完成 / scan finished
#define WM_CLEAN_DONE  (WM_APP + 3)  // 清理完成 / cleanup finished

#define APP_TITLE _T("DiskAnalyzer - 磁盘空间分析 / Disk Space Analyzer")
#define APP_CLASS _T("DiskAnalyzerClass")
#define TIMER_SCAN 1

typedef std::basic_string<TCHAR> tstring;

// ============================================================================
//  分类定义 / Category definitions
// ============================================================================
enum Category {
    CAT_SYSTEM = 0,   // 系统文件 / System files
    CAT_PROGRAM,      // 应用程序 / Installed programs
    CAT_GAME,         // 游戏文件 / Games
    CAT_CACHE,        // 缓存与临时文件 / Cache & temporary files
    CAT_DEV,          // 开发与构建产物 / Development & build output
    CAT_DOWNLOAD,     // 下载文件 / Downloads
    CAT_MEDIA,        // 影音图片 / Media
    CAT_DOC,          // 文档 / Documents
    CAT_RECYCLE,      // 回收站 / Recycle Bin
    CAT_OTHER,        // 其他 / Other
    CAT_COUNT
};

struct CatInfo {
    const TCHAR* name;  // 中文名 / Chinese label
    const TCHAR* en;    // 英文名 / English label
    COLORREF     color; // 图表颜色 / bar color
};

static const CatInfo g_catInfo[CAT_COUNT] = {
    { _T("系统文件"),   _T("System"),      RGB( 90, 140, 220) },
    { _T("应用程序"),   _T("Programs"),    RGB( 70, 175, 130) },
    { _T("游戏文件"),   _T("Games"),       RGB(220,  95,  60) },
    { _T("缓存/临时"),  _T("Cache / Temp"),RGB(230, 170,  40) },
    { _T("开发/构建"),  _T("Dev / Build"), RGB(140, 110, 210) },
    { _T("下载文件"),   _T("Downloads"),   RGB( 70, 185, 205) },
    { _T("影音图片"),   _T("Media"),       RGB(225, 105, 175) },
    { _T("文档"),       _T("Documents"),   RGB(160, 160, 160) },
    { _T("回收站"),     _T("Recycle Bin"), RGB(120, 120, 120) },
    { _T("其他"),       _T("Other"),       RGB(100, 105, 115) },
};

// 分类规则：路径关键字（全部小写，自上而下优先匹配）
// EN: Path keyword rules (all lowercase, first match wins)
struct PathRule { const TCHAR* pattern; Category cat; };

static const PathRule g_pathRules[] = {
    // ---- 回收站 / Recycle Bin ----
    { _T("$recycle.bin"),                      CAT_RECYCLE },

    // ---- 系统文件 / System files ----
    { _T(":\\windows\\"),                      CAT_SYSTEM },
    { _T(":\\program files\\"),                CAT_SYSTEM },
    { _T(":\\program files (x86)\\"),          CAT_SYSTEM },
    { _T(":\\programdata\\"),                  CAT_SYSTEM },
    { _T(":\\system volume information\\"),    CAT_SYSTEM },
    { _T(":\\perflogs\\"),                     CAT_SYSTEM },
    { _T(":\\$sysreset\\"),                    CAT_SYSTEM },
    { _T(":\\recovery\\"),                     CAT_SYSTEM },
    { _T(":\\users\\default"),                 CAT_SYSTEM },
    { _T(":\\hiberfil.sys"),                   CAT_SYSTEM },
    { _T(":\\pagefile.sys"),                   CAT_SYSTEM },
    { _T(":\\swapfile.sys"),                   CAT_SYSTEM },
    { _T(":\\windowsapps\\"),                  CAT_SYSTEM },
    { _T(":\\$windows.~ws"),                   CAT_SYSTEM },
    { _T(":\\$windows.~bt"),                   CAT_SYSTEM },

    // ---- 游戏文件 / Games（放在缓存规则之前，避免着色器缓存被单独统计）
    { _T("\\steamapps\\"),                     CAT_GAME },
    { _T("\\steamlibrary\\"),                  CAT_GAME },
    { _T("\\steam\\"),                         CAT_GAME },
    { _T("\\epic games\\"),                    CAT_GAME },
    { _T("\\riot games\\"),                    CAT_GAME },
    { _T("\\battle.net\\"),                    CAT_GAME },
    { _T("\\origin games\\"),                  CAT_GAME },
    { _T("\\ubisoft\\"),                       CAT_GAME },
    { _T("\\uplay\\"),                         CAT_GAME },
    { _T("\\gog galaxy\\"),                    CAT_GAME },
    { _T("\\gog games\\"),                     CAT_GAME },
    { _T("\\wegame\\"),                        CAT_GAME },
    { _T("\\xboxgames\\"),                     CAT_GAME },
    { _T("\\minecraft\\"),                     CAT_GAME },
    { _T("\\games\\"),                         CAT_GAME },

    // ---- 缓存与临时文件 / Cache & temporary files ----
    { _T("\\appdata\\local\\temp\\"),          CAT_CACHE },
    { _T("\\windows\\temp\\"),                 CAT_CACHE },
    { _T("\\inetcache\\"),                     CAT_CACHE },
    { _T("\\temporary internet files\\"),      CAT_CACHE },
    { _T("\\softwaredistribution\\download\\"),CAT_CACHE },
    { _T("\\appdata\\local\\crashdumps\\"),    CAT_CACHE },
    { _T("\\microsoft\\windows\\wer\\"),       CAT_CACHE },
    { _T("\\microsoft\\windows\\caches\\"),    CAT_CACHE },
    { _T("\\microsoft\\windows\\explorer\\"),  CAT_CACHE },
    { _T("\\microsoft\\windows\\webcache\\"),  CAT_CACHE },
    { _T("\\microsoft\\windows\\history\\"),   CAT_CACHE },
    { _T("\\pip\\cache\\"),                    CAT_CACHE },
    { _T("\\npm-cache\\"),                     CAT_CACHE },
    { _T("\\yarn\\cache\\"),                   CAT_CACHE },
    { _T("\\d3dsache\\"),                      CAT_CACHE },
    { _T("\\nvidia corporation\\"),            CAT_CACHE },
    { _T("\\appdata\\local\\amd\\"),           CAT_CACHE },
    { _T("\\windows\\logs\\"),                 CAT_CACHE },
    { _T("\\windows\\panther\\"),              CAT_CACHE },
    { _T("\\cache\\"),                         CAT_CACHE },
    { _T("\\cachedata\\"),                     CAT_CACHE },
    { _T("\\logs\\"),                          CAT_CACHE },

    // ---- 开发与构建产物 / Development & build output ----
    { _T("\\node_modules\\"),                  CAT_DEV },
    { _T("\\.git\\"),                          CAT_DEV },
    { _T("\\.vs\\"),                           CAT_DEV },
    { _T("\\.idea\\"),                         CAT_DEV },
    { _T("\\.vscode\\"),                       CAT_DEV },
    { _T("\\__pycache__\\"),                   CAT_DEV },
    { _T("\\.gradle\\"),                       CAT_DEV },
    { _T("\\.m2\\"),                           CAT_DEV },
    { _T("\\venv\\"),                          CAT_DEV },
    { _T("\\.venv\\"),                         CAT_DEV },
    { _T("\\cmake-build-"),                    CAT_DEV },
    { _T("\\obj\\"),                           CAT_DEV },
    { _T("\\bin\\debug\\"),                    CAT_DEV },
    { _T("\\bin\\release\\"),                  CAT_DEV },
    { _T("\\build\\"),                         CAT_DEV },

    // ---- 下载文件 / Downloads ----
    { _T("\\downloads\\"),                     CAT_DOWNLOAD },
};

// 扩展名规则（排在路径规则之后）
// EN: Extension rules (evaluated after path rules)
struct ExtRule { const TCHAR* ext; Category cat; };

static const ExtRule g_extRules[] = {
    // 缓存/临时 / cache & temp
    { _T(".tmp"), CAT_CACHE }, { _T(".temp"), CAT_CACHE }, { _T(".log"), CAT_CACHE },
    { _T(".old"), CAT_CACHE }, { _T(".bak"), CAT_CACHE }, { _T(".dmp"), CAT_CACHE },
    { _T(".etl"), CAT_CACHE }, { _T(".crdownload"), CAT_CACHE }, { _T(".part"), CAT_CACHE },
    { _T(".partial"), CAT_CACHE }, { _T(".!ut"), CAT_CACHE },
    { _T("thumbs.db"), CAT_CACHE },

    // 下载 / downloads
    { _T(".torrent"), CAT_DOWNLOAD },

    // 游戏 / games
    { _T(".pak"), CAT_GAME }, { _T(".vpk"), CAT_GAME }, { _T(".gcf"), CAT_GAME },
    { _T(".ncf"), CAT_GAME }, { _T(".wad"), CAT_GAME }, { _T(".bsa"), CAT_GAME },
    { _T(".esm"), CAT_GAME }, { _T(".esp"), CAT_GAME }, { _T(".arc"), CAT_GAME },
    { _T(".xnb"), CAT_GAME }, { _T(".assets"), CAT_GAME }, { _T(".bundle"), CAT_GAME },
    { _T(".uasset"), CAT_GAME }, { _T(".umap"), CAT_GAME }, { _T(".upk"), CAT_GAME },
    { _T(".pk3"), CAT_GAME }, { _T(".sga"), CAT_GAME }, { _T(".scs"), CAT_GAME },
    { _T(".big"), CAT_GAME },

    // 影音图片 / media
    { _T(".mp4"), CAT_MEDIA }, { _T(".mkv"), CAT_MEDIA }, { _T(".avi"), CAT_MEDIA },
    { _T(".rmvb"), CAT_MEDIA }, { _T(".rm"), CAT_MEDIA }, { _T(".mov"), CAT_MEDIA },
    { _T(".wmv"), CAT_MEDIA }, { _T(".flv"), CAT_MEDIA }, { _T(".webm"), CAT_MEDIA },
    { _T(".m4v"), CAT_MEDIA }, { _T(".mpg"), CAT_MEDIA }, { _T(".mpeg"), CAT_MEDIA },
    { _T(".ts"), CAT_MEDIA }, { _T(".vob"), CAT_MEDIA },
    { _T(".mp3"), CAT_MEDIA }, { _T(".wav"), CAT_MEDIA }, { _T(".flac"), CAT_MEDIA },
    { _T(".aac"), CAT_MEDIA }, { _T(".m4a"), CAT_MEDIA }, { _T(".ogg"), CAT_MEDIA },
    { _T(".wma"), CAT_MEDIA }, { _T(".ape"), CAT_MEDIA },
    { _T(".jpg"), CAT_MEDIA }, { _T(".jpeg"), CAT_MEDIA }, { _T(".png"), CAT_MEDIA },
    { _T(".gif"), CAT_MEDIA }, { _T(".bmp"), CAT_MEDIA }, { _T(".webp"), CAT_MEDIA },
    { _T(".heic"), CAT_MEDIA }, { _T(".tif"), CAT_MEDIA }, { _T(".tiff"), CAT_MEDIA },
    { _T(".psd"), CAT_MEDIA }, { _T(".ai"), CAT_MEDIA }, { _T(".raw"), CAT_MEDIA },
    { _T(".svg"), CAT_MEDIA }, { _T(".ico"), CAT_MEDIA },

    // 文档 / documents
    { _T(".doc"), CAT_DOC }, { _T(".docx"), CAT_DOC }, { _T(".xls"), CAT_DOC },
    { _T(".xlsx"), CAT_DOC }, { _T(".ppt"), CAT_DOC }, { _T(".pptx"), CAT_DOC },
    { _T(".pdf"), CAT_DOC }, { _T(".txt"), CAT_DOC }, { _T(".md"), CAT_DOC },
    { _T(".csv"), CAT_DOC }, { _T(".rtf"), CAT_DOC }, { _T(".odt"), CAT_DOC },
    { _T(".epub"), CAT_DOC }, { _T(".mobi"), CAT_DOC }, { _T(".json"), CAT_DOC },
    { _T(".xml"), CAT_DOC }, { _T(".yaml"), CAT_DOC }, { _T(".yml"), CAT_DOC },
    { _T(".ini"), CAT_DOC }, { _T(".cfg"), CAT_DOC },
};

// ============================================================================
//  工具函数 / Helpers
// ============================================================================
static tstring ToLower(tstring s)
{
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] >= _T('A') && s[i] <= _T('Z'))
            s[i] = (TCHAR)(s[i] - _T('A') + _T('a'));
    }
    return s;
}

// 将普通路径转为支持超长路径的 \\?\ 形式
// EN: Convert a normal path to the extended-length \\?\ form
static tstring MakeLongPath(const tstring& p)
{
    if (p.size() >= 4 && p[0] == _T('\\') && p[1] == _T('\\') &&
        p[2] == _T('?') && p[3] == _T('\\'))
        return p;
    return tstring(_T("\\\\?\\")) + p;
}

static tstring FormatSize(ULONGLONG bytes)
{
    const TCHAR* units[] = { _T("B"), _T("KB"), _T("MB"), _T("GB"), _T("TB") };
    double d = (double)bytes;
    int i = 0;
    while (d >= 1024.0 && i < 4) { d /= 1024.0; i++; }

    TCHAR buf[64];
    if (i == 0) _stprintf_s(buf, _T("%llu %s"), bytes, units[i]);
    else        _stprintf_s(buf, _T("%.2f %s"), d, units[i]);
    return tstring(buf);
}

static tstring GetExt(const tstring& p)
{
    size_t dot = p.find_last_of(_T('.'));
    size_t sep = p.find_last_of(_T('\\'));
    if (dot == tstring::npos) return tstring();
    if (sep != tstring::npos && dot < sep) return tstring();
    return ToLower(p.substr(dot));
}

static tstring ExpandPath(const TCHAR* p)
{
    DWORD need = ExpandEnvironmentStrings(p, NULL, 0);
    if (need == 0) return tstring();
    std::vector<TCHAR> buf((size_t)need + 1, 0);
    ExpandEnvironmentStrings(p, &buf[0], need + 1);
    return tstring(&buf[0]);
}

static Category ClassifyPath(const tstring& fullPath)
{
    tstring p = ToLower(fullPath);

    for (int i = 0; i < _countof(g_pathRules); i++) {
        if (p.find(g_pathRules[i].pattern) != tstring::npos)
            return g_pathRules[i].cat;
    }

    tstring ext = GetExt(p);
    if (!ext.empty()) {
        for (int i = 0; i < _countof(g_extRules); i++) {
            if (ext == g_extRules[i].ext)
                return g_extRules[i].cat;
        }
    }

    return CAT_OTHER;
}

// ============================================================================
//  扫描状态 / Scan state
// ============================================================================
struct ScanState {
    std::atomic<bool>      running;
    std::atomic<bool>      stop;
    std::atomic<ULONGLONG> files;
    std::atomic<ULONGLONG> bytes;
    std::atomic<ULONGLONG> dirs;
    ULONGLONG catBytes[CAT_COUNT];
    ULONGLONG catFiles[CAT_COUNT];
    std::mutex mtx;
    tstring   current;
    tstring   root;
    DWORD     elapsedMs;
    std::vector<std::pair<tstring, ULONGLONG> > topDirs;

    void Reset()
    {
        running = false; stop = false;
        files = 0; bytes = 0; dirs = 0; elapsedMs = 0;
        ZeroMemory(catBytes, sizeof(catBytes));
        ZeroMemory(catFiles, sizeof(catFiles));
        topDirs.clear(); current.clear();
    }
};

static ScanState g_scan;
static HINSTANCE g_hInst = NULL;
static HFONT     g_hFont = NULL;
static HWND      g_hMain = NULL;
static HWND      g_hCombo = NULL;
static HWND      g_hBtnScan = NULL;
static HWND      g_hBtnStop = NULL;
static HWND      g_hProgress = NULL;
static HWND      g_hCurrent = NULL;
static HWND      g_hListCat = NULL;
static HWND      g_hListTop = NULL;
static HWND      g_hStatus = NULL;
static HWND      g_hGrpCat = NULL;
static HWND      g_hGrpTop = NULL;
static HWND      g_hGrpClean = NULL;
static HBRUSH    g_brushes[CAT_COUNT];

// ============================================================================
//  扫描引擎 / Scan engine
// ============================================================================
static ULONGLONG ScanDirectory(const tstring& dir, int depth)
{
    if (g_scan.stop) return 0;

    WIN32_FIND_DATA fd;
    tstring search = dir;
    if (!search.empty() && search.back() != _T('\\')) search += _T('\\');
    search += _T('*');

    HANDLE hFind = FindFirstFile(MakeLongPath(search).c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    {
        std::lock_guard<std::mutex> lock(g_scan.mtx);
        g_scan.current = dir;
    }

    ULONGLONG total = 0;
    do {
        if (g_scan.stop) break;

        // 跳过 . 和 ..
        // EN: skip "." and ".."
        if (fd.cFileName[0] == _T('.') &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == _T('.') && fd.cFileName[2] == 0)))
            continue;

        // 跳过重解析点（符号链接 / 挂载点），避免循环
        // EN: skip reparse points to avoid cycles
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        tstring full = dir;
        if (full.back() != _T('\\')) full += _T('\\');
        full += fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            g_scan.dirs++;
            total += ScanDirectory(full, depth + 1);
        }
        else {
            ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            total += size;
            g_scan.files++;
            g_scan.bytes += size;

            Category c = ClassifyPath(full);
            std::lock_guard<std::mutex> lock(g_scan.mtx);
            g_scan.catBytes[c] += size;
            g_scan.catFiles[c]++;
        }
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);

    // 记录前两层的目录体积，用于排行榜
    // EN: record the size of first two directory levels for the ranking
    if (depth > 0 && depth <= 2)
        g_scan.topDirs.push_back(std::make_pair(dir, total));

    return total;
}

static void ScanWorker(tstring root)
{
    g_scan.Reset();
    g_scan.running = true;
    g_scan.root = root;

    DWORD t0 = GetTickCount();
    ScanDirectory(root, 0);
    g_scan.elapsedMs = GetTickCount() - t0;

    g_scan.running = false;
    if (g_hMain) PostMessage(g_hMain, WM_SCAN_DONE, 0, 0);
}

// ============================================================================
//  清理引擎 / Cleanup engine
// ============================================================================
struct CleanStat {
    ULONGLONG freed;
    DWORD     deletedFiles;
    DWORD     failedFiles;
    DWORD     deletedDirs;
};

static CleanStat         g_clean;
static std::atomic<bool> g_cleanRunning;

// 递归删除目录下的所有文件与空目录，被占用的会自动跳过
// EN: Recursively delete files and empty dirs; files in use are skipped
static void DeleteRecursive(const tstring& dir, CleanStat& st)
{
    WIN32_FIND_DATA fd;
    tstring search = dir;
    if (!search.empty() && search.back() != _T('\\')) search += _T('\\');
    search += _T('*');

    HANDLE h = FindFirstFile(MakeLongPath(search).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == _T('.') &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == _T('.') && fd.cFileName[2] == 0)))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        tstring full = dir;
        if (full.back() != _T('\\')) full += _T('\\');
        full += fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteRecursive(full, st);
            if (RemoveDirectory(MakeLongPath(full).c_str())) st.deletedDirs++;
        }
        else {
            ULONGLONG sz = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (DeleteFile(MakeLongPath(full).c_str())) { st.freed += sz; st.deletedFiles++; }
            else st.failedFiles++;
        }
    } while (FindNextFile(h, &fd));

    FindClose(h);
}

// 常规临时文件清理目标
// EN: Regular temp cleanup targets
static const TCHAR* g_cleanTargets[] = {
    _T("%TEMP%"),
    _T("%WINDIR%\\Temp"),
    _T("%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache"),
    _T("%LOCALAPPDATA%\\CrashDumps"),
    _T("%LOCALAPPDATA%\\Microsoft\\Windows\\WER"),
    _T("%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer"),
};

// Windows 更新缓存 / Windows Update cache
static const TCHAR* g_updateTargets[] = {
    _T("%WINDIR%\\SoftwareDistribution\\Download"),
};

static void CleanWorker(const std::vector<tstring>& targets)
{
    g_clean.freed = 0;
    g_clean.deletedFiles = 0;
    g_clean.failedFiles = 0;
    g_clean.deletedDirs = 0;
    g_cleanRunning = true;

    for (size_t i = 0; i < targets.size(); i++)
        DeleteRecursive(targets[i], g_clean);

    g_cleanRunning = false;
    if (g_hMain) PostMessage(g_hMain, WM_CLEAN_DONE, 0, 0);
}

// ============================================================================
//  界面 / UI
// ============================================================================
static HWND CreateLabel(HWND parent, const TCHAR* text, int id, int x, int y, int w, int height)
{
    HWND h = CreateWindowEx(0, WC_STATIC, text, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        x, y, w, height, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (g_hFont) SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return h;
}

static HWND CreateButton(HWND parent, const TCHAR* text, int id, int x, int y, int w, int height)
{
    HWND h = CreateWindowEx(0, WC_BUTTON, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, height, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (g_hFont) SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return h;
}

static void AddColumn(HWND hList, int index, const TCHAR* text, int width, int fmt)
{
    LVCOLUMN col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    col.iSubItem = index;
    col.pszText = (LPTSTR)text;
    col.cx = width;
    col.fmt = fmt;
    ListView_InsertColumn(hList, index, &col);
}

static void InitList(HWND hList)
{
    ListView_SetExtendedListViewStyle(hList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
}

static void FillCategoryList()
{
    ListView_DeleteAllItems(g_hListCat);

    ULONGLONG total = g_scan.bytes.load();
    if (total == 0) total = 1;

    int order[CAT_COUNT];
    for (int i = 0; i < CAT_COUNT; i++) order[i] = i;
    std::sort(order, order + CAT_COUNT,
        [](int a, int b) { return g_scan.catBytes[a] > g_scan.catBytes[b]; });

    for (int k = 0; k < CAT_COUNT; k++) {
        int c = order[k];
        if (g_scan.catFiles[c] == 0) continue;

        double pct = (double)g_scan.catBytes[c] * 100.0 / (double)total;

        LVITEM li;
        ZeroMemory(&li, sizeof(li));
        li.mask = LVIF_TEXT | LVIF_PARAM;
        li.iItem = ListView_GetItemCount(g_hListCat);
        // 高位存分类索引，低位存百分比*10
        // EN: high word = category index, low word = percent * 10
        li.lParam = (LPARAM)(((c & 0xFFFF) << 16) | ((int)(pct * 10) & 0xFFFF));

        TCHAR name[128];
        _stprintf_s(name, _T("%s  %s"), g_catInfo[c].name, g_catInfo[c].en);
        li.pszText = name;

        int idx = ListView_InsertItem(g_hListCat, &li);

        TCHAR size[64], ratio[64], cnt[64];
        _stprintf_s(size,  _T("%s"), FormatSize(g_scan.catBytes[c]).c_str());
        _stprintf_s(ratio, _T("%.1f%%"), pct);
        _stprintf_s(cnt,   _T("%llu"), (ULONGLONG)g_scan.catFiles[c]);

        ListView_SetItemText(g_hListCat, idx, 1, size);
        ListView_SetItemText(g_hListCat, idx, 2, ratio);
        ListView_SetItemText(g_hListCat, idx, 3, cnt);
    }
}

static void FillTopDirList()
{
    ListView_DeleteAllItems(g_hListTop);

    std::vector<std::pair<tstring, ULONGLONG> >& v = g_scan.topDirs;
    std::sort(v.begin(), v.end(),
        [](const std::pair<tstring, ULONGLONG>& a,
           const std::pair<tstring, ULONGLONG>& b) { return a.second > b.second; });

    ULONGLONG total = g_scan.bytes.load();
    if (total == 0) total = 1;
    size_t n = (std::min)((size_t)300, v.size());

    for (size_t i = 0; i < n; i++) {
        if (v[i].second == 0) continue;

        tstring shown = v[i].first;
        // 去掉 \\?\ 前缀便于阅读 / strip the \\?\ prefix for readability
        if (shown.size() > 4 && shown[0] == _T('\\') && shown[1] == _T('\\') &&
            shown[2] == _T('?') && shown[3] == _T('\\'))
            shown = shown.substr(4);

        LVITEM li;
        ZeroMemory(&li, sizeof(li));
        li.mask = LVIF_TEXT;
        li.iItem = (int)ListView_GetItemCount(g_hListTop);
        li.pszText = (LPTSTR)shown.c_str();
        int idx = ListView_InsertItem(g_hListTop, &li);

        TCHAR size[64], ratio[64];
        _stprintf_s(size,  _T("%s"), FormatSize(v[i].second).c_str());
        _stprintf_s(ratio, _T("%.1f%%"), (double)v[i].second * 100.0 / (double)total);

        ListView_SetItemText(g_hListTop, idx, 1, size);
        ListView_SetItemText(g_hListTop, idx, 2, ratio);
    }
}

static void LayoutControls(int cx, int cy)
{
    int topH = 86;
    int cleanH = 152;
    int midH = cy - topH - cleanH;
    if (midH < 80) midH = 80;

    int leftW = (int)((double)(cx - 36) * 0.58);
    int rightX = 12 + leftW + 12;
    int rightW = cx - 12 - rightX;

    MoveWindow(g_hProgress, 12, 42, cx - 24, 16, TRUE);
    MoveWindow(g_hCurrent,  12, 62, cx - 24, 18, TRUE);

    MoveWindow(g_hGrpCat, 12, topH, leftW, midH, TRUE);
    MoveWindow(g_hListCat, 22, topH + 22, leftW - 20, midH - 32, TRUE);

    MoveWindow(g_hGrpTop, rightX, topH, rightW, midH, TRUE);
    MoveWindow(g_hListTop, rightX + 10, topH + 22, rightW - 20, midH - 32, TRUE);

    int cleanY = cy - cleanH;
    MoveWindow(g_hGrpClean, 12, cleanY, cx - 24, cleanH - 8, TRUE);

    // 第一行按钮 / first row
    int y1 = cleanY + 26;
    int bw = 150, gap = 10;
    MoveWindow(GetDlgItem(g_hMain, IDC_BTN_CLEAN_TEMP),    22,            y1, bw, 28, TRUE);
    MoveWindow(GetDlgItem(g_hMain, IDC_BTN_CLEAN_UPDATE),  22 + bw + gap, y1, bw, 28, TRUE);
    MoveWindow(GetDlgItem(g_hMain, IDC_BTN_CLEAN_RECYCLE), 22 + (bw + gap) * 2, y1, bw, 28, TRUE);

    // 第二行按钮 / second row
    int y2 = y1 + 38;
    MoveWindow(GetDlgItem(g_hMain, IDC_BTN_OPEN_CLEANMGR), 22,            y2, bw, 28, TRUE);
    MoveWindow(GetDlgItem(g_hMain, IDC_BTN_OPEN_SENSE),   22 + bw + gap, y2, bw, 28, TRUE);
    MoveWindow(GetDlgItem(g_hMain, IDC_BTN_EXPORT),       22 + (bw + gap) * 2, y2, bw, 28, TRUE);
}

static bool IsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID pSid = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pSid))
        return false;

    if (!CheckTokenMembership(NULL, pSid, &isAdmin)) {
        FreeSid(pSid);
        return false;
    }

    FreeSid(pSid);
    return isAdmin == TRUE;
}

// 以管理员权限重启自身 / restart itself with elevation
static void Elevate()
{
    TCHAR path[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, path, MAX_PATH);

    SHELLEXECUTEINFO sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = _T("runas");
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteEx(&sei)) {
        MessageBox(NULL, _T("提权失败，请右键以管理员身份运行！\nEN: Failed to elevate, please run as administrator."),
            APP_TITLE, MB_ICONWARNING);
    }
    exit(0);
}

// ============================================================================
//  窗口过程 / Window procedure
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CreateLabel(hWnd, _T("驱动器 / Drive:"), 0, 12, 13, 82, 18);

        g_hCombo = CreateWindowEx(0, WC_COMBOBOX, _T(""),
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            96, 8, 190, 200, hWnd, (HMENU)(INT_PTR)IDC_COMBO_DRIVE, g_hInst, NULL);
        if (g_hFont) SendMessage(g_hCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnScan = CreateButton(hWnd, _T("开始扫描 Scan"), IDC_BTN_SCAN, 292, 8, 120, 26);
        g_hBtnStop = CreateButton(hWnd, _T("停止 Stop"),     IDC_BTN_STOP, 418, 8, 80,  26);
        EnableWindow(g_hBtnStop, FALSE);

        g_hProgress = CreateWindowEx(0, PROGRESS_CLASS, _T(""),
            WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
            12, 42, 900, 16, hWnd, (HMENU)(INT_PTR)IDC_PROGRESS, g_hInst, NULL);

        g_hCurrent = CreateLabel(hWnd, _T("就绪 / Ready"), 0, 12, 62, 900, 18);

        g_hGrpCat = CreateWindowEx(0, WC_BUTTON, _T("分类汇总 / Breakdown"),
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 10, 10, hWnd, NULL, g_hInst, NULL);
        if (g_hFont) SendMessage(g_hGrpCat, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hGrpTop = CreateWindowEx(0, WC_BUTTON, _T("占用最大的目录 / Largest folders"),
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 10, 10, hWnd, NULL, g_hInst, NULL);
        if (g_hFont) SendMessage(g_hGrpTop, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hListCat = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, _T(""),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            22, 108, 520, 300, hWnd, (HMENU)(INT_PTR)IDC_LIST_CATEGORY, g_hInst, NULL);
        if (g_hFont) SendMessage(g_hListCat, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        InitList(g_hListCat);
        AddColumn(g_hListCat, 0, _T("分类 Category"), 190, LVCFMT_LEFT);
        AddColumn(g_hListCat, 1, _T("大小 Size"),     95,  LVCFMT_RIGHT);
        AddColumn(g_hListCat, 2, _T("占比 Ratio"),  130,  LVCFMT_LEFT);
        AddColumn(g_hListCat, 3, _T("文件数 Files"), 85,  LVCFMT_RIGHT);

        g_hListTop = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, _T(""),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            560, 108, 380, 300, hWnd, (HMENU)(INT_PTR)IDC_LIST_TOPDIR, g_hInst, NULL);
        if (g_hFont) SendMessage(g_hListTop, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        InitList(g_hListTop);
        AddColumn(g_hListTop, 0, _T("目录 Folder"), 230, LVCFMT_LEFT);
        AddColumn(g_hListTop, 1, _T("大小 Size"),   80,  LVCFMT_RIGHT);
        AddColumn(g_hListTop, 2, _T("占比 Ratio"), 60,  LVCFMT_RIGHT);

        g_hGrpClean = CreateWindowEx(0, WC_BUTTON, _T("一键清理 / One-click cleanup"),
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 10, 10, hWnd, NULL, g_hInst, NULL);
        if (g_hFont) SendMessage(g_hGrpClean, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        CreateButton(hWnd, _T("清理临时文件 Clean temp"),  IDC_BTN_CLEAN_TEMP,    22, 500, 150, 28);
        CreateButton(hWnd, _T("清理更新缓存 Update cache"), IDC_BTN_CLEAN_UPDATE, 182, 500, 150, 28);
        CreateButton(hWnd, _T("清空回收站 Recycle bin"),   IDC_BTN_CLEAN_RECYCLE, 342, 500, 150, 28);
        CreateButton(hWnd, _T("磁盘清理 cleanmgr"),       IDC_BTN_OPEN_CLEANMGR, 22, 538, 150, 28);
        CreateButton(hWnd, _T("存储感知 Storage Sense"),  IDC_BTN_OPEN_SENSE,   182, 538, 150, 28);
        CreateButton(hWnd, _T("导出报告 Export report"),  IDC_BTN_EXPORT,        342, 538, 150, 28);

        // 填充盘符列表 / fill the drive list
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(mask & (1 << i))) continue;

            TCHAR root[8];
            _stprintf_s(root, _T("%c:\\"), _T('A') + i);
            if (GetDriveType(root) != DRIVE_FIXED) continue;

            ULARGE_INTEGER total, free;
            TCHAR item[160];
            if (GetDiskFreeSpaceEx(root, &free, &total, NULL)) {
                _stprintf_s(item, _T("%s   可用 %s / 共 %s   [free %s of %s]"),
                    root,
                    FormatSize(free.QuadPart).c_str(), FormatSize(total.QuadPart).c_str(),
                    FormatSize(free.QuadPart).c_str(), FormatSize(total.QuadPart).c_str());
            }
            else {
                _stprintf_s(item, _T("%s"), root);
            }

            int idx = (int)SendMessage(g_hCombo, CB_ADDSTRING, 0, (LPARAM)item);
            SendMessage(g_hCombo, CB_SETITEMDATA, idx, (LPARAM)i);
        }
        SendMessage(g_hCombo, CB_SETCURSEL, 0, 0);
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_BTN_SCAN:
        {
            if (g_scan.running || g_cleanRunning) break;

            int sel = (int)SendMessage(g_hCombo, CB_GETCURSEL, 0, 0);
            if (sel < 0) break;

            int letter = (int)SendMessage(g_hCombo, CB_GETITEMDATA, sel, 0);
            tstring root = tstring(_T("\\\\?\\")) + (TCHAR)(_T('A') + letter) + _T(":\\");

            ListView_DeleteAllItems(g_hListCat);
            ListView_DeleteAllItems(g_hListTop);

            EnableWindow(g_hBtnScan, FALSE);
            EnableWindow(g_hBtnStop, TRUE);
            EnableWindow(g_hCombo, FALSE);

            SendMessage(g_hProgress, PBM_SETMARQUEE, TRUE, 0);
            SetTimer(hWnd, TIMER_SCAN, 150, NULL);

            std::thread(ScanWorker, root).detach();
            break;
        }

        case IDC_BTN_STOP:
        {
            g_scan.stop = true;
            EnableWindow(g_hBtnStop, FALSE);
            break;
        }

        case IDC_BTN_CLEAN_TEMP:
        {
            if (g_scan.running || g_cleanRunning) break;
            if (MessageBox(hWnd,
                _T("将删除以下位置的临时文件：\n")
                _T("  %TEMP%\n  %WINDIR%\\Temp\n  INetCache\n  CrashDumps\n  WER\n  Explorer 缩略图缓存\n\n")
                _T("EN: Delete temporary files from %TEMP%, Windows\\Temp, INetCache, ")
                _T("CrashDumps, WER and the Explorer thumbnail cache.\n\n继续 Continue?"),
                _T("确认 / Confirm"), MB_ICONQUESTION | MB_YESNO) != IDYES)
                break;

            std::vector<tstring> targets;
            for (int i = 0; i < _countof(g_cleanTargets); i++)
                targets.push_back(ExpandPath(g_cleanTargets[i]));

            g_cleanRunning = true;
            SendMessage(g_hProgress, PBM_SETMARQUEE, TRUE, 0);
            SetWindowText(g_hStatus, _T("正在清理临时文件... / Cleaning temporary files..."));
            std::thread(CleanWorker, targets).detach();
            break;
        }

        case IDC_BTN_CLEAN_UPDATE:
        {
            if (g_scan.running || g_cleanRunning) break;
            if (MessageBox(hWnd,
                _T("将删除 Windows 更新下载缓存：\n  %WINDIR%\\SoftwareDistribution\\Download\n\n")
                _T("EN: Delete the Windows Update download cache.\n\n继续 Continue?"),
                _T("确认 / Confirm"), MB_ICONQUESTION | MB_YESNO) != IDYES)
                break;

            std::vector<tstring> targets;
            for (int i = 0; i < _countof(g_updateTargets); i++)
                targets.push_back(ExpandPath(g_updateTargets[i]));

            g_cleanRunning = true;
            SendMessage(g_hProgress, PBM_SETMARQUEE, TRUE, 0);
            SetWindowText(g_hStatus, _T("正在清理更新缓存... / Cleaning update cache..."));
            std::thread(CleanWorker, targets).detach();
            break;
        }

        case IDC_BTN_CLEAN_RECYCLE:
        {
            if (g_scan.running || g_cleanRunning) break;
            if (MessageBox(hWnd, _T("确定清空回收站？\nEN: Empty the Recycle Bin?"),
                _T("确认 / Confirm"), MB_ICONQUESTION | MB_YESNO) != IDYES)
                break;

            SHEmptyRecycleBin(hWnd, NULL,
                SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
            SetWindowText(g_hStatus, _T("回收站已清空 / Recycle bin emptied"));
            break;
        }

        case IDC_BTN_OPEN_CLEANMGR:
        {
            // 启动系统磁盘清理；进界面后记得点「清理系统文件」按钮
            // EN: launch cleanmgr; remember to click "Clean up system files"
            ShellExecute(hWnd, _T("runas"), _T("cleanmgr.exe"), NULL, NULL, SW_SHOWNORMAL);
            break;
        }

        case IDC_BTN_OPEN_SENSE:
        {
            ShellExecute(hWnd, _T("open"), _T("ms-settings:storagesense"), NULL, NULL, SW_SHOWNORMAL);
            break;
        }

        case IDC_BTN_EXPORT:
        {
            TCHAR desk[MAX_PATH] = { 0 };
            if (!SUCCEEDED(SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk))) break;

            TCHAR file[MAX_PATH];
            _stprintf_s(file, _T("%s\\DiskAnalyzer_Report.txt"), desk);

            FILE* fp = _tfopen(file, _T("w"));
            if (!fp) {
                MessageBox(hWnd, _T("导出失败 / Export failed"), APP_TITLE, MB_ICONERROR);
                break;
            }

            _ftprintf(fp, _T("DiskAnalyzer 磁盘空间分析报告 / Disk space report\n"));
            _ftprintf(fp, _T("生成时间 / Generated: %s\n"), _tctime(NULL) ? _tctime(NULL) : _T(""));
            _ftprintf(fp, _T("-------------------------------------------------\n\n"));
            _ftprintf(fp, _T("[分类汇总 / Breakdown]\n"));

            ULONGLONG total = g_scan.bytes.load();
    if (total == 0) total = 1;
            int order[CAT_COUNT];
            for (int i = 0; i < CAT_COUNT; i++) order[i] = i;
            std::sort(order, order + CAT_COUNT,
                [](int a, int b) { return g_scan.catBytes[a] > g_scan.catBytes[b]; });

            for (int k = 0; k < CAT_COUNT; k++) {
                int c = order[k];
                if (g_scan.catFiles[c] == 0) continue;
                _ftprintf(fp, _T("%-12s %-14s %10s  %5.1f%%  %llu files\n"),
                    g_catInfo[c].name, g_catInfo[c].en,
                    FormatSize(g_scan.catBytes[c]).c_str(),
                    (double)g_scan.catBytes[c] * 100.0 / (double)total,
                    (ULONGLONG)g_scan.catFiles[c]);
            }

            _ftprintf(fp, _T("\n[占用最大的目录 / Largest folders]\n"));
            std::vector<std::pair<tstring, ULONGLONG> >& v = g_scan.topDirs;
            std::sort(v.begin(), v.end(),
                [](const std::pair<tstring, ULONGLONG>& a,
                   const std::pair<tstring, ULONGLONG>& b) { return a.second > b.second; });

            size_t n = (std::min)((size_t)100, v.size());
            for (size_t i = 0; i < n; i++) {
                tstring shown = v[i].first;
                if (shown.size() > 4 && shown[0] == _T('\\') && shown[1] == _T('\\') &&
                    shown[2] == _T('?') && shown[3] == _T('\\'))
                    shown = shown.substr(4);
                _ftprintf(fp, _T("%10s   %s\n"), FormatSize(v[i].second).c_str(), shown.c_str());
            }

            fclose(fp);

            TCHAR msg[MAX_PATH + 64];
            _stprintf_s(msg, _T("已导出到桌面 / Exported to:\n%s"), file);
            MessageBox(hWnd, msg, APP_TITLE, MB_ICONINFORMATION);
            break;
        }
        }
        break;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_SCAN && g_scan.running) {
            TCHAR txt[1024];
            tstring cur;
            {
                std::lock_guard<std::mutex> lock(g_scan.mtx);
                cur = g_scan.current;
            }
            if (cur.size() > 4 && cur[0] == _T('\\') && cur[1] == _T('\\') &&
                cur[2] == _T('?') && cur[3] == _T('\\'))
                cur = cur.substr(4);

            _stprintf_s(txt, _T("已扫描 %llu 个文件 / %llu 个目录，共 %s"),
                (ULONGLONG)g_scan.files, (ULONGLONG)g_scan.dirs,
                FormatSize(g_scan.bytes).c_str());
            SetWindowText(g_hStatus, txt);

            if (cur.size() > 90) cur = cur.substr(cur.size() - 90);
            SetWindowText(g_hCurrent, cur.c_str());
        }
        break;
    }

    case WM_SCAN_DONE:
    {
        KillTimer(hWnd, TIMER_SCAN);
        SendMessage(g_hProgress, PBM_SETMARQUEE, FALSE, 0);
        EnableWindow(g_hBtnScan, TRUE);
        EnableWindow(g_hBtnStop, FALSE);
        EnableWindow(g_hCombo, TRUE);

        FillCategoryList();
        FillTopDirList();

        TCHAR txt[512];
        _stprintf_s(txt,
            _T("扫描完成 / Done: %llu 个文件 files, %llu 个目录 dirs, 共 total %s, 耗时 time %.1fs"),
            (ULONGLONG)g_scan.files, (ULONGLONG)g_scan.dirs,
            FormatSize(g_scan.bytes).c_str(), g_scan.elapsedMs / 1000.0);
        SetWindowText(g_hStatus, txt);
        SetWindowText(g_hCurrent, _T("完成 / Finished"));
        break;
    }

    case WM_CLEAN_DONE:
    {
        SendMessage(g_hProgress, PBM_SETMARQUEE, FALSE, 0);
        TCHAR txt[512];
        _stprintf_s(txt,
            _T("清理完成 / Cleanup done: 释放 freed %s, 删除 %lu 个文件 files, ")
            _T("%lu 个失败 skipped (被占用 in use), 移除 %lu 个空目录 empty dirs"),
            FormatSize(g_clean.freed).c_str(),
            (unsigned long)g_clean.deletedFiles,
            (unsigned long)g_clean.failedFiles,
            (unsigned long)g_clean.deletedDirs);
        SetWindowText(g_hStatus, txt);
        break;
    }

    case WM_NOTIFY:
    {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_LIST_CATEGORY && nm->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;

            case CDDS_ITEMPREPAINT:
                return CDRF_NOTIFYSUBITEMDRAW;

            case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
            {
                // 只在「占比」列画进度条 / only draw the bar in the Ratio column
                // EN: only the "Ratio" column gets a custom bar
                if (cd->iSubItem != 2) return CDRF_DODEFAULT;

                int catIdx = (int)((cd->nmcd.lItemlParam >> 16) & 0xFFFF);
                int pct10  = (int)(cd->nmcd.lItemlParam & 0xFFFF);

                HDC  hdc = cd->nmcd.hdc;
                RECT rc  = cd->nmcd.rc;

                FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

                RECT bar = rc;
                InflateRect(&bar, -1, -4);
                RECT fill = bar;
                fill.right = fill.left + (bar.right - bar.left) * pct10 / 1000;

                HBRUSH br = CreateSolidBrush(g_catInfo[catIdx].color);
                FillRect(hdc, &fill, br);
                DeleteObject(br);
                FrameRect(hdc, &bar, (HBRUSH)GetStockObject(GRAY_BRUSH));

                TCHAR txt[32];
                _stprintf_s(txt, _T("%.1f%%"), pct10 / 10.0);
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                return CDRF_SKIPDEFAULT;
            }
            }
        }
        break;
    }

    case WM_SIZE:
    {
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
    {
        for (int i = 0; i < CAT_COUNT; i++) {
            if (g_brushes[i]) { DeleteObject(g_brushes[i]); g_brushes[i] = NULL; }
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================================
//  入口 / Entry point
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // 需要管理员权限才能读取系统目录 / admin rights are needed for system dirs
    // EN: administrator privileges are required to read protected system directories
    if (!IsAdmin()) Elevate();

    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc;
    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    NONCLIENTMETRICS ncm;
    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_hFont = CreateFontIndirect(&ncm.lfMessageFont);

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = APP_CLASS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassEx(&wc)) return 1;

    g_hMain = CreateWindowEx(0, APP_CLASS, APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 700,
        NULL, NULL, hInstance, NULL);

    if (!g_hMain) return 1;

    // 状态栏文本 / status label
    g_hStatus = CreateLabel(g_hMain, _T("选择驱动器后点击「开始扫描」/ Select a drive then click Scan"),
        IDC_TEXT_STATUS, 470, 13, 500, 18);

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
    return (int)msg.wParam;
}
