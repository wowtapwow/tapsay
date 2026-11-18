// ============================================================
// TapSay.cpp  (手机当麦克风话筒+鼠标 + 键盘 + 双指滚动/右键/捏合缩放)
//           + 轻点话筒图标：给电脑发全局 Win+H，打开 Windows 自带的“语音输入”直接说话转文字
//           + 三指轻点 Ctrl 锁定切换 / 三指上下滑动 Win+D 显示桌面
//           + 三指左滑 前一个窗口 / 三指右滑 后一个窗口
//           + 左下角退格键删除长按持续删除 / 切换应用图标 / 回车按钮
//           + 长按滚动 + 单实例 + 浮动小图标 + 可拖动
//           + 左右键菜单 + 命令行 t N / m 居中 / c 居中 / h 顶部居中 / r 居右 + IP 自动刷新
//           + 触摸板提示 + 横屏按钮单排 + 首次触摸自动全屏
//           + 首次启动自动居中 + 中英文语言自适应
//           + 电脑剪贴板(文本/文件,含复制/剪切) → 同步给手机（手机上按上传文件按钮和新建文本按钮也能同步给电脑剪贴板）
//           + 屏幕右侧出现鼠标滚轮和鼠标滚轮中键浮动岛可挪移岛任意位置，也可贴右边屏幕边缘隐藏剩下细边可点它打开
//           + 屏幕左右边缘单指上滑/下滑 = 持续滚动鼠标滚轮向上/向下
//           + 按住剪贴板图标左/右滑动 = 光标持续逐字前/后移动
//           + 按住剪贴板与下方按钮之间的分隔线任意位置左/右滑动 = 光标持续逐字前/后移动
//           + 屏幕左右边缘各 22px 区域内，单指上滑 → 滚轮持续向上，单指下滑 → 滚轮持续向下，滑动过程中改变方向滑则滚轮反方向滚动
//           + 剪贴板与下方按钮之间那条分隔线任意位置以及剪贴板图标按住左滑光标持续逐字往前，按住右滑光标持续逐字往后，滑动过程中改变方向则反方向移动光标
//           + TapSay对比另一个TapVox的优点就是只有http扫码后操作更简单
// ============================================================
// cl /nologo /EHsc /O2 /MT /utf-8 /D_UNICODE /DUNICODE TapSay.cpp TapSay.res /link /SUBSYSTEM:WINDOWS /out:TapSay.exe
// ============================================================

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shlobj.h>

// ---------- UIAutomation 前置依赖 ----------
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
// 【新增】截图给手机看：PNG 编码用 GDI+
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "Gdiplus.lib")

#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Uiautomationcore.lib")
#pragma comment(lib, "Advapi32.lib")

// ============================================================
// 全局
// ============================================================
static HWND        g_hwnd     = NULL;
static HWND        g_miniHwnd = NULL;
// 本程序窗口之外最后一个“真正的应用”前台窗口（收起二维码时把前台还给它）
static HWND        g_lastForeground = NULL;
static HINSTANCE   g_hInst    = NULL;
static HANDLE      g_mutex    = NULL;
static int   g_port     = 8868;
static int         g_dpi      = 96;

// 【新增】GDI+ 截图编码运行环境（Win+H 转完文字后拍输入框给手机看）
static ULONG_PTR   g_gdiplusTok = 0;
static struct GdiPlusInitGuard {
    GdiPlusInitGuard() {
        Gdiplus::GdiplusStartupInput in;
        if (Gdiplus::GdiplusStartup(&g_gdiplusTok, &in, NULL) != Gdiplus::Ok)
            g_gdiplusTok = 0;
    }
    ~GdiPlusInitGuard() {
        if (g_gdiplusTok) { Gdiplus::GdiplusShutdown(g_gdiplusTok); g_gdiplusTok = 0; }
    }
} g_gdiplusGuard;

// ---------- 多语言 ----------
enum Lang { LANG_ZH = 0, LANG_EN = 1 };
static Lang g_lang = LANG_ZH;

static Lang DetectSystemLang() {
    LANGID lid = GetUserDefaultUILanguage();
    if ((lid & 0xFF) == 0x04) return LANG_ZH;
    return LANG_EN;
}

struct StrTable {
    const wchar_t* trayTip;
    const wchar_t* menuShowQr;
    const wchar_t* menuExit;
    const wchar_t* menuCenter;
    const wchar_t* menuRight;
    const wchar_t* menuTop;      // 二维码居上（等同 h 命令）
    const wchar_t* menuRefresh;
    const wchar_t* menuHide;
    const wchar_t* menuCopyLink;   // 复制链接
    const wchar_t* winTitle;
    const wchar_t* qrTitle;
    const wchar_t* qrHint;
    const wchar_t* failTitle;
    const wchar_t* failMsg;
    const wchar_t* clipZipName;   // 【新增】“全部打包”时的 zip 文件名
};

static const StrTable kStrZh = {
    L"手机秒变鼠标键盘触摸板 - 点击显示二维码",
    L"显示二维码", L"退出",
    L"二维码居中", L"二维码居右", L"二维码居上", L"刷新并重新对齐", L"隐藏窗口", L"复制链接",
    L"手机扫码控制",
    L"手机扫码控制电脑",
    L"手机需与电脑处于同一局域网",
    L"启动失败",
    L"无法启动本地服务，端口可能被占用。\n请关闭占用 8868 端口的程序后重试。",
    L"电脑剪贴板.zip"
};

static const StrTable kStrEn = {
    L"Phone as Mouse Touchpad - Click to show QR",
    L"Show QR Code", L"Exit",
    L"Center QR", L"Align QR Right", L"Align QR Top", L"Refresh & Realign", L"Hide Window", L"Copy Link",
    L"Phone Remote Mouse",
    L"Phone scans QR as keyboard & mouse",
    L"Phone must be on the same LAN as PC",
    L"Startup Failed",
    L"Cannot start local server. Port may be in use.\nPlease free port 8868 and retry.",
    L"PC-Clipboard.zip"
};

static const StrTable& S() { return g_lang == LANG_ZH ? kStrZh : kStrEn; }

#define WM_TRAYICON           (WM_APP + 1)
#define WM_FIRST_CLIENT       (WM_APP + 2)
#define WM_SHOW_QR_FROM_OTHER (WM_APP + 3)
#define WM_SET_SHOW_QR_TIMER  (WM_APP + 4)
#define WM_SHOW_QR_CENTER     (WM_APP + 5)
#define WM_SHOW_QR_RIGHT      (WM_APP + 6)
#define WM_SHOW_QR_TOP        (WM_APP + 7)   // 【新增】h 参数：顶部居中

#define TIMER_QR_MENU 1001
#define TIMER_SHOW_QR 1002
#define TIMER_QR_REPOS 1003   // h 参数：显示后再校正一次位置（PE 兜底）

static NOTIFYICONDATAW g_nid;
static bool            g_tray_visible      = false;
static bool            g_auto_hide_done    = false;
static bool            g_has_shell         = false;
static volatile LONG   g_first_client_seen = 0;

static int  g_startupDelaySec = 0;
static int  g_startupPosition = 0;

#define MINI_W        48
#define MINI_H        48
#define MINI_MARGIN   16

static bool  g_miniDragging  = false;
static bool  g_miniMoved     = false;
static POINT g_miniDragStart = {0, 0};
static RECT  g_miniStartRect = {0, 0, 0, 0};

static bool  g_qrDragArmed = false;
static bool  g_qrDragging  = false;
static POINT g_qrDragStart = {0, 0};
static RECT  g_qrStartRect = {0, 0, 0, 0};

static void HideQrToTray();   // 定义在托盘部分，供浮动小图标 / 主窗口共用

// --- Ctrl 锁定状态 ---
static volatile LONG g_ctrlDown = 0;

// ============================================================
// 【新增】剪贴板相关全局
// ============================================================
static SRWLOCK                  g_clipLock = SRWLOCK_INIT;
static std::wstring             g_clipText;          // 文本内容
static std::vector<std::wstring> g_clipFiles;        // 文件/文件夹路径列表
static bool                     g_clipIsCut = false; // 是否剪切
static volatile LONG            g_clipVersion = 0;   // 版本号

// 【新增】手机上传到电脑的文件（用于拼成电脑的“文件剪贴板”）
static SRWLOCK                   g_uploadLock = SRWLOCK_INIT;
static std::wstring              g_uploadDir;        // 落地目录 %TEMP%\PhoneMouseUpload
static std::vector<std::wstring> g_uploadList;       // 本轮上传的文件路径

bool QrEncode(const std::string& text, int ec_level,
              std::vector<std::vector<int> >& out);

static std::vector<std::vector<int>> g_qr;
static std::wstring                  g_url;

// ============================================================
// 工具
// ============================================================
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (len <= 0) return std::wstring();
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  NULL, 0, NULL, NULL);
    if (len <= 0) return std::string();
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        &s[0], len, NULL, NULL);
    return s;
}
static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static std::string UrlUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = HexVal(s[i + 1]), lo = HexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) { out += (char)((hi << 4) | lo); i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}

// JSON 字符串转义（将宽字符转为 UTF-8 后再转义）
static std::string JsonEscapeW(const std::wstring& w) {
    std::string s = WideToUtf8(w);
    std::string out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8]; sprintf(buf, "\\u%04x", c); out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

// ============================================================
// 虚拟网卡识别
// ============================================================
static bool ContainsAnyW(const wchar_t* str,
                         const wchar_t* const* kws, int n) {
    if (!str) return false;
    for (int i = 0; i < n; ++i)
        if (wcsstr(str, kws[i])) return true;
    return false;
}
static bool IsVirtualAdapter(const wchar_t* desc, const wchar_t* name) {
    static const wchar_t* kws[] = {
        L"VMware", L"VirtualBox", L"Hyper-V", L"Virtual",
        L"TAP-",   L"TAP ",     L"TUN",     L"VPN",
        L"Loopback", L"WSL",    L"Docker",  L"Pseudo",
        L"Npcap",  L"WinPcap",  L"OpenVPN", L"WireGuard",
        L"Tailscale", L"ZeroTier", L"Hamachi", L"SoftEther",
        L"Microsoft KM-TEST", L"WAN Miniport",
        L"Kernel Debug", L"Bluetooth",
        L"Wi-Fi Direct", L"WFD"
    };
    int n = sizeof(kws) / sizeof(kws[0]);
    return ContainsAnyW(desc, kws, n) || ContainsAnyW(name, kws, n);
}

// ============================================================
// 局域网 IP 探测
// ============================================================
struct IpCandidate {
    std::string ip;
    int         score;
    bool operator<(const IpCandidate& o) const { return score > o.score; }
};

static std::vector<std::string> GetLocalIpList(std::string& preferred) {
    std::vector<std::string> ips;
    preferred.clear();

    std::vector<IpCandidate> candidates;
    std::map<std::string, bool> seen;

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    ULONG bufLen = 16384;
    std::vector<BYTE> buf(bufLen);
    ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL,
                                     (IP_ADAPTER_ADDRESSES*)&buf[0], &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufLen);
        ret = GetAdaptersAddresses(AF_INET, flags, NULL,
                                   (IP_ADAPTER_ADDRESSES*)&buf[0], &bufLen);
    }

    if (ret == NO_ERROR) {
        IP_ADAPTER_ADDRESSES* pAddr = (IP_ADAPTER_ADDRESSES*)&buf[0];
        while (pAddr) {
            if (pAddr->OperStatus == IfOperStatusUp) {
                bool hasGateway = (pAddr->FirstGatewayAddress != NULL);
                DWORD ifType     = pAddr->IfType;
                bool  isWireless = (ifType == 71);
                bool  isEthernet = (ifType == 6);
                bool  isVirtual  = IsVirtualAdapter(pAddr->Description,
                                                    pAddr->FriendlyName);

                int score = 0;
                if      (isWireless && !isVirtual) score = 4000;
                else if (!isVirtual)               score = 3500;
                else if (isEthernet && !isVirtual) score = 3000;
                else if (isEthernet && isVirtual)  score = 2000;
                else if (isWireless && isVirtual)  score = 1000;
                else                               score =  500;

                if (hasGateway) score += 100;

                IP_ADAPTER_UNICAST_ADDRESS* pUni = pAddr->FirstUnicastAddress;
                while (pUni) {
                    if (pUni->Address.lpSockaddr &&
                        pUni->Address.lpSockaddr->sa_family == AF_INET) {
                        sockaddr_in* sa = (sockaddr_in*)pUni->Address.lpSockaddr;
                        char tmp[64] = {0};
                        strcpy(tmp, inet_ntoa(sa->sin_addr));
                        std::string ip = tmp;

                        if (ip != "0.0.0.0" &&
                            ip != "127.0.0.1" &&
                            ip.compare(0, 7, "169.254") != 0 &&
                            !seen[ip]) {

                            int s = score;
                            if (ip.compare(0, 8, "192.168.") == 0) s += 30;
                            else if (ip.compare(0, 3, "10.") == 0) s += 25;
                            else if (ip.compare(0, 4, "172.") == 0) {
                                int second = atoi(ip.c_str() + 4);
                                if (second >= 16 && second <= 31) s += 20;
                            }

                            IpCandidate c;
                            c.ip    = ip;
                            c.score = s;
                            candidates.push_back(c);
                            seen[ip] = true;
                        }
                    }
                    pUni = pUni->Next;
                }
            }
            pAddr = pAddr->Next;
        }
    }

    std::sort(candidates.begin(), candidates.end());
    for (size_t i = 0; i < candidates.size(); ++i)
        ips.push_back(candidates[i].ip);

    if (ips.empty()) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            sockaddr_in to;
            memset(&to, 0, sizeof(to));
            to.sin_family = AF_INET;
            to.sin_port = htons(53);
            to.sin_addr.s_addr = inet_addr("8.8.8.8");
            if (connect(s, (sockaddr*)&to, sizeof(to)) == 0) {
                sockaddr_in name;
                int nl = sizeof(name);
                if (getsockname(s, (sockaddr*)&name, &nl) == 0) {
                    char tmp2[64] = {0};
                    strcpy(tmp2, inet_ntoa(name.sin_addr));
                    if (strcmp(tmp2, "127.0.0.1") != 0 &&
                        strncmp(tmp2, "169.254", 7) != 0) {
                        ips.push_back(tmp2);
                    }
                }
            }
            closesocket(s);
        }
    }

    if (ips.empty()) ips.push_back("127.0.0.1");
    preferred = ips[0];
    return ips;
}

static bool RefreshQr() {
    std::string preferred;
    std::vector<std::string> allIps = GetLocalIpList(preferred);

    if (preferred.empty() || preferred == "127.0.0.1") {
        for (size_t i = 0; i < allIps.size(); ++i) {
            if (allIps[i] != "127.0.0.1") { preferred = allIps[i]; break; }
        }
    }
    if (preferred.empty()) preferred = "127.0.0.1";

    char urlbuf[256];
    sprintf(urlbuf, "http://%s:%d", preferred.c_str(), g_port);
    std::wstring newUrl = Utf8ToWide(urlbuf);

    std::vector<std::vector<int>> newQr;
    if (!QrEncode(urlbuf, 4, newQr)) return false;

    g_url = newUrl;
    g_qr  = newQr;
    return true;
}

// ============================================================
// 【新增】剪贴板读取
// ============================================================
static void ReadClipboardNow() {
    if (!g_hwnd) return;

    bool opened = false;
    for (int i = 0; i < 5; ++i) {
        if (OpenClipboard(g_hwnd)) { opened = true; break; }
        Sleep(20);
    }
    if (!opened) return;

    std::wstring text;
    std::vector<std::wstring> files;
    bool isCut = false;

    // --- 文件（CF_HDROP） ---
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP);
        if (hDrop) {
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; ++i) {
                UINT need = DragQueryFileW(hDrop, i, NULL, 0);
                if (need == 0) continue;
                std::vector<wchar_t> buf(need + 1, L'\0');
                if (DragQueryFileW(hDrop, i, &buf[0], need + 1) > 0) {
                    files.push_back(std::wstring(&buf[0], need));
                }
            }
        }
    }

    // --- 文本（CF_UNICODETEXT） ---
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            wchar_t* p = (wchar_t*)GlobalLock(h);
            if (p) {
                text = p;
                GlobalUnlock(h);
            }
        }
    }

    // --- 判断复制 / 剪切（Preferred DropEffect） ---
    UINT prefFmt = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (prefFmt && IsClipboardFormatAvailable(prefFmt)) {
        HANDLE h = GetClipboardData(prefFmt);
        if (h) {
            DWORD* p = (DWORD*)GlobalLock(h);
            if (p) {
                if (((*p) & DROPEFFECT_MOVE) != 0) isCut = true;
                GlobalUnlock(h);
            }
        }
    }

    CloseClipboard();

    // --- 存储 ---
    AcquireSRWLockExclusive(&g_clipLock);
    g_clipText  = text;
    g_clipFiles = files;
    g_clipIsCut = isCut;
    InterlockedIncrement(&g_clipVersion);
    ReleaseSRWLockExclusive(&g_clipLock);
}

// ============================================================
// 命令行
// ============================================================
static wchar_t ToLowerW(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return (wchar_t)(c - L'A' + L'a');
    return c;
}
static void ParseCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a.empty()) continue;
        while (!a.empty() && (a[0] == L'-' || a[0] == L'/')) a.erase(0, 1);
        if (a.empty()) continue;
        wchar_t c = ToLowerW(a[0]);
        if ((c == L'm' || c == L'c') && a.size() == 1) {
            // 居中：m 与 c 完全等价（同一分支，保证行为一模一样）
            g_startupPosition = 1;
        } else if (c == L'h' && a.size() == 1) {
            g_startupPosition = 3;                  // h：二维码顶部居中
        } else if (c == L'r' && a.size() == 1) {
            g_startupPosition = 2;
        } else if (c == L't') {
            int sec = 0;
            if (a.size() > 1) sec = _wtoi(a.c_str() + 1);
            else if (i + 1 < argc) {
                sec = _wtoi(argv[i + 1]);
                if (sec > 0) ++i;
            }
            if (sec > 0 && sec <= 3600) g_startupDelaySec = sec;
        } else if (c == L'p') {
            // -p 端口：默认 8868 被占用时可以换一个
            int p = 0;
            if (a.size() > 1) p = _wtoi(a.c_str() + 1);
            else if (i + 1 < argc) {
                p = _wtoi(argv[i + 1]);
                if (p > 0) ++i;
            }
            if (p >= 1 && p <= 65535) g_port = p;
        }
    }
    LocalFree(argv);
}

// ============================================================
// 迷你图标
// ============================================================
static void ShowQrNearAnchor();

static LRESULT CALLBACK MiniProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            HBRUSH bg  = CreateSolidBrush(RGB(37, 99, 235));
            HPEN   pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ oldBr  = SelectObject(hdc, bg);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            RoundRect(hdc, 1, 1, w - 1, h - 1, 14, 14);
            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPen);
            DeleteObject(bg);
            DeleteObject(pen);
            HICON ico = LoadIconW(g_hInst, L"MAINICON");
            if (!ico) ico = LoadIconW(NULL, IDI_APPLICATION);
            if (ico) {
                int isz = 30;
                DrawIconEx(hdc, (w-isz)/2, (h-isz)/2, ico, isz, isz, 0, NULL, DI_NORMAL);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            g_miniDragging = true;
            g_miniMoved    = false;
            GetCursorPos(&g_miniDragStart);
            GetWindowRect(hwnd, &g_miniStartRect);
            SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_miniDragging) {
                POINT pt;
                GetCursorPos(&pt);
                int dx = pt.x - g_miniDragStart.x;
                int dy = pt.y - g_miniDragStart.y;
                if (!g_miniMoved && (abs(dx) > 3 || abs(dy) > 3)) g_miniMoved = true;
                if (g_miniMoved) {
                    SetWindowPos(hwnd, HWND_TOPMOST,
                                 g_miniStartRect.left + dx,
                                 g_miniStartRect.top  + dy,
                                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g_miniDragging) {
                g_miniDragging = false;
                ReleaseCapture();
                if (!g_miniMoved) {
                    if (IsWindowVisible(g_hwnd)) HideQrToTray();
                    else ShowQrNearAnchor();
                } else {
                    if (g_hwnd && IsWindowVisible(g_hwnd)) ShowQrNearAnchor();
                }
            }
            return 0;
        }
        case WM_CAPTURECHANGED: {
            g_miniDragging = false;
            return 0;
        }
        case WM_RBUTTONUP: {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, S().menuShowQr);
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 2, S().menuExit);
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int cmd = (int)TrackPopupMenu(hMenu,
                TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            if (cmd == 1) ShowQrNearAnchor();
            else if (cmd == 2 && g_hwnd) DestroyWindow(g_hwnd);
            return 0;
        }
        case WM_DESTROY:
            if (g_miniHwnd == hwnd) g_miniHwnd = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void CreateMiniIcon() {
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = MiniProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_HAND);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"PhoneMouseMiniIcon";
        wc.hIcon         = LoadIconW(g_hInst, L"MAINICON");
        if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        wc.hIconSm       = wc.hIcon;
        RegisterClassExW(&wc);
        s_registered = true;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    g_miniHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"PhoneMouseMiniIcon", L"",
        WS_POPUP,
        sw - MINI_W - MINI_MARGIN, sh - MINI_H - MINI_MARGIN,
        MINI_W, MINI_H, NULL, NULL, g_hInst, NULL);
}

// ============================================================
// 系统托盘
// ============================================================
static bool ShowTrayIcon() {
    if (g_tray_visible || !g_hwnd) return g_tray_visible;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(g_hInst, L"MAINICON");
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, S().trayTip, _TRUNCATE);
    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) g_tray_visible = true;
    return g_tray_visible;
}
static void HideTrayIcon() {
    if (!g_tray_visible) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_tray_visible = false;
}
static bool DetectShell() {
    if (FindWindowW(L"Shell_TrayWnd", NULL)) return true;
    if (FindWindowW(L"Shell_SecondaryTrayWnd", NULL)) return true;
    return false;
}
static bool TryGetTrayIconRect(RECT& rc) {
    NOTIFYICONIDENTIFIER nii;
    memset(&nii, 0, sizeof(nii));
    nii.cbSize = sizeof(nii);
    nii.hWnd   = g_hwnd;
    nii.uID    = 1;
    return SUCCEEDED(Shell_NotifyIconGetRect(&nii, &rc));
}

// ============================================================
// 隐藏到托盘 / 复制链接
// ============================================================
static bool SetClipboardTextW(const std::wstring& text);   // 定义见下方剪贴板部分

// 【新增】这个前台窗口是不是“真正的应用窗口”（不是桌面/任务栏/开始菜单）
static bool IsUsableForegroundWindow(HWND h) {
    if (!h || !IsWindow(h) || !IsWindowVisible(h)) return false;
    wchar_t cls[128] = {0};
    GetClassNameW(h, cls, 127);
    if (_wcsicmp(cls, L"Progman") == 0) return false;
    if (_wcsicmp(cls, L"WorkerW") == 0) return false;
    if (_wcsicmp(cls, L"Shell_TrayWnd") == 0) return false;
    if (_wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0) return false;
    if (_wcsicmp(cls, L"Windows.UI.Core.CoreWindow") == 0) return false;
    return true;
}

// 隐藏二维码窗口并收进系统托盘：
//   · 二维码窗口本身是 WS_EX_TOOLWINDOW，隐藏后任务栏不会留下项目；
//   · 托盘图标不存在时先补一个（例如资源管理器比本程序晚启动）；
//   · 托盘图标在的时候，浮动小图标也一起收起，只留托盘图标运行；
//   · 托盘实在创建不成功则保留浮动小图标，否则程序会没有任何入口。
//   · 如果二维码正当前台，收起后把前台还给原来的窗口，
//     这样用户原本停着的窗口能继续接收输入。
static void HideQrToTray() {
    if (!g_hwnd) return;

    HWND fgBefore = GetForegroundWindow();
    bool qrWasForeground = (fgBefore == g_hwnd || fgBefore == g_miniHwnd);
    HWND restore = g_lastForeground;
    bool canRestore = (qrWasForeground && restore &&
                       restore != g_hwnd && restore != g_miniHwnd &&
                       IsUsableForegroundWindow(restore));

    // 【关键】先把前台还给原来的窗口，本程序此刻还是前台进程，系统才会允许切换；
    // 如果先隐藏二维码，前台会掉到浮动小图标/桌面上，再想切回去就晚了。
    if (canRestore) SetForegroundWindow(restore);

    if (!g_tray_visible) ShowTrayIcon();

    if (IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_HIDE);

    // 再兜一层：极端情况下窗口样式被改掉时，强制补回 WS_EX_TOOLWINDOW，
    // 保证任务栏里不会留下这个窗口的项目。
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) == 0) {
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex | WS_EX_TOOLWINDOW);
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    if (g_tray_visible && g_miniHwnd && IsWindowVisible(g_miniHwnd))
        ShowWindow(g_miniHwnd, SW_HIDE);

    if (canRestore) SetForegroundWindow(restore);
}

// 把二维码下方显示的访问链接复制到电脑剪贴板
static void CopyQrLink() {
    if (g_url.empty() && !RefreshQr()) return;
    SetClipboardTextW(g_url);
}

// ============================================================
// 定位
// ============================================================
static void ShowQrNearAnchor() {
    if (!g_hwnd) return;

    RECT rcWnd;
    GetWindowRect(g_hwnd, &rcWnd);
    const int qrW = rcWnd.right - rcWnd.left;
    const int qrH = rcWnd.bottom - rcWnd.top;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = 0, y = 0;
    bool placed = false;

    if (g_miniHwnd && IsWindowVisible(g_miniHwnd)) {
        RECT rcMini;
        GetWindowRect(g_miniHwnd, &rcMini);
        int cxMini = (rcMini.left + rcMini.right) / 2;
        x = cxMini - qrW / 2;
        y = rcMini.top - qrH - 8;
        if (y < 8) y = rcMini.bottom + 8;
        placed = true;
    }
    if (!placed && g_has_shell && g_tray_visible) {
        for (int i = 0; i < 6; ++i) {
            RECT rcTray = {0};
            if (TryGetTrayIconRect(rcTray)) {
                int cxTray = (rcTray.left + rcTray.right) / 2;
                x = cxTray - qrW / 2;
                if (rcTray.top > sh / 2) {
                    y = rcTray.top - qrH - 8;
                    if (y < 8) y = rcTray.bottom + 8;
                } else {
                    y = rcTray.bottom + 8;
                    if (y + qrH > sh - 8) y = rcTray.top - qrH - 8;
                }
                placed = true;
                break;
            }
            Sleep(200);
        }
    }
    if (!placed) {
        RECT wa = {0, 0, sw, sh};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        x = wa.right  - qrW - 12;
        y = wa.bottom - qrH - 12;
    }
    if (x + qrW > sw - 8) x = sw - qrW - 8;
    if (x < 8)            x = 8;
    if (y + qrH > sh - 8) y = sh - qrH - 8;
    if (y < 8)            y = 8;

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, qrW, qrH,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

static void MoveQrToCenter() {
    if (!g_hwnd) return;
    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_hwnd, HWND_TOPMOST, (sw - w) / 2, (sh - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

static void MoveQrToRight() {
    if (!g_hwnd) return;
    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_hwnd, HWND_TOPMOST, sw - w - 20, (sh - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

// 【修正】h 参数：二维码居于屏幕上方顶部的中间
//   旧写法拿 SPI_GETWORKAREA 的顶部 + GetSystemMetrics 直接算 y：
//   在 WinPE / 精简外壳里，工作区经常是 (0,0,0,0) 或者顶部跑到屏幕外，
//   屏幕尺寸也可能还没初始化，算出来的 y 会掉到屏幕中间甚至屏幕外，
//   于是“顶部居中”看起来完全没生效。现在改为：
//     · 以窗口所在显示器的**物理矩形**为准（多显示器也对）；
//     · 工作区顶部只有确实落在屏幕上方 1/4 内才采用（避开顶部任务栏），
//       PE 里的离谱值直接忽略；
//     · 尺寸拿不到时用兜底值，坐标一律夹在屏幕内；
//     · 显示后 150ms / 550ms 再各校正一次，应付 PE 里“桌面晚初始化”
//       或精简外壳把窗口位置又拉回去的情况。
static void PlaceQrTopCenter() {
    if (!g_hwnd) return;

    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0) w = 340;      // 窗口还没完成布局时的兜底尺寸
    if (h <= 0) h = 430;

    int monLeft = 0, monTop = 0, monW = 0, monH = 0;
    bool gotMon = false;
    HMONITOR hMon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    if (hMon) {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            monLeft = mi.rcMonitor.left;
            monTop  = mi.rcMonitor.top;
            monW    = mi.rcMonitor.right  - mi.rcMonitor.left;
            monH    = mi.rcMonitor.bottom - mi.rcMonitor.top;
            gotMon  = true;
            int waTop = mi.rcWork.top;
            if (waTop >= mi.rcMonitor.top && waTop < mi.rcMonitor.top + monH / 4)
                monTop = waTop;          // 正常桌面：避开顶部任务栏
        }
    }
    if (!gotMon || monW <= 0 || monH <= 0) {
        monW = GetSystemMetrics(SM_CXSCREEN);
        monH = GetSystemMetrics(SM_CYSCREEN);
        monLeft = 0;
        monTop  = 0;
    }
    if (monW <= 0) monW = 1024;   // 极端情况兜底，绝不算出负坐标
    if (monH <= 0) monH = 768;

    int x = monLeft + (monW - w) / 2;
    int y = monTop + 8;
    if (y + h > monTop + monH) y = monTop;   // 屏幕太矮：贴住顶部，不掉到中间
    if (y < monTop) y = monTop;

    // 注意：这里不带 SWP_SHOWWINDOW —— 定时器校正时不能把已隐藏的窗口弹出来
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

static int g_qrReposLeft = 0;   // 显示后还需要再校正几次

static void MoveQrToTopCenter(bool armRetry = true) {
    if (!g_hwnd) return;
    PlaceQrTopCenter();
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    if (armRetry) {
        g_qrReposLeft = 2;
        SetTimer(g_hwnd, TIMER_QR_REPOS, 150, NULL);
    }
}

static void ShowStartupQr() {
    if (!g_hwnd) return;
    RefreshQr();
    InvalidateRect(g_hwnd, NULL, TRUE);
    if      (g_startupPosition == 2) MoveQrToRight();
    else if (g_startupPosition == 3) MoveQrToTopCenter();
    else                             MoveQrToCenter();
}

static void ShowQrWithRefresh() {
    if (!g_hwnd) return;
    RefreshQr();
    InvalidateRect(g_hwnd, NULL, TRUE);
    ShowQrNearAnchor();
}

static void ShowQrContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1001, S().menuCenter);
    AppendMenuW(hMenu, MF_STRING, 1002, S().menuRight);
    AppendMenuW(hMenu, MF_STRING, 1006, S().menuTop);      // 新增：二维码居上（等同 h 命令）
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1003, S().menuRefresh);
    AppendMenuW(hMenu, MF_STRING, 1007, S().menuCopyLink);
    AppendMenuW(hMenu, MF_STRING, 1004, S().menuHide);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1005, S().menuExit);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = (int)TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    switch (cmd) {
        case 1001: MoveQrToCenter();         break;
        case 1002: MoveQrToRight();          break;
        case 1006: MoveQrToTopCenter();      break;   // 与 h 命令完全同一条路径
        case 1003: ShowQrWithRefresh();      break;
        case 1007: CopyQrLink();             break;
        case 1004: HideQrToTray();           break;
        case 1005: DestroyWindow(hwnd);      break;
    }
}

// ============================================================
// QR 编码器
// ============================================================
namespace {

const int kCapacity[40][4] = {
    {19,16,13,9},{34,28,22,16},{55,44,34,26},{80,64,48,36},
    {108,86,62,46},{136,108,76,60},{156,124,88,66},{194,154,110,86},
    {232,182,132,100},{274,216,154,122},{324,254,180,140},{370,290,206,158},
    {428,334,244,180},{461,365,261,197},{523,415,295,223},{589,453,325,253},
    {647,507,367,283},{721,563,397,313},{795,627,445,341},{861,669,485,385},
    {932,714,512,406},{1006,782,568,442},{1094,860,614,464},{1174,914,664,514},
    {1276,1000,718,538},{1370,1062,754,596},{1468,1128,808,628},{1531,1193,871,661},
    {1631,1267,911,701},{1735,1373,985,745},{1843,1455,1033,793},{1955,1541,1115,845},
    {2071,1631,1171,901},{2191,1725,1231,961},{2306,1812,1286,986},{2434,1914,1354,1054},
    {2566,1992,1426,1096},{2702,2102,1502,1142},{2812,2216,1582,1222},{2956,2334,1666,1276},
};

const int kEcBlocks[40][4][6] = {
{{1,26,19},{1,26,16},{1,26,13},{1,26,9}},
{{1,44,34},{1,44,28},{1,44,22},{1,44,16}},
{{1,70,55},{1,70,44},{2,35,17},{2,35,13}},
{{1,100,80},{2,50,32},{2,50,24},{4,25,9}},
{{1,134,108},{2,67,43},{2,33,15,2,34,16},{2,33,11,2,34,12}},
{{2,86,68},{4,43,27},{4,43,19},{4,43,15}},
{{2,98,78},{4,49,31},{2,32,14,4,33,15},{4,39,13,1,40,14}},
{{2,121,97},{2,60,38,2,61,39},{4,40,18,2,41,19},{4,40,14,2,41,15}},
{{2,146,116},{3,58,36,2,59,37},{4,36,16,4,37,17},{4,36,12,4,37,13}},
{{2,86,68,2,87,69},{4,69,43,1,70,44},{6,43,19,2,44,20},{6,43,15,2,44,16}},
{{4,101,81},{1,80,50,4,81,51},{4,50,22,4,51,23},{3,36,12,8,37,13}},
{{2,116,92,2,117,93},{6,58,36,2,59,37},{4,46,20,6,47,21},{7,42,14,4,43,15}},
{{4,133,107},{8,59,37,1,60,38},{8,44,20,4,45,21},{12,33,11,4,34,12}},
{{3,145,115,1,146,116},{4,64,40,5,65,41},{11,36,16,5,37,17},{11,36,12,5,37,13}},
{{5,109,87,1,110,88},{5,65,41,5,66,42},{5,54,24,7,55,25},{11,36,12,7,37,13}},
{{5,122,98,1,123,99},{7,73,45,3,74,46},{15,43,19,2,44,20},{3,45,15,13,46,16}},
{{1,135,107,5,136,108},{10,74,46,1,75,47},{1,50,22,15,51,23},{2,42,14,17,43,15}},
{{5,150,120,1,151,121},{9,69,43,4,70,44},{17,50,22,1,51,23},{2,42,14,19,43,15}},
{{3,141,113,4,142,114},{3,70,44,11,71,45},{17,47,21,4,48,22},{9,39,13,16,40,14}},
{{3,135,107,5,136,108},{3,67,41,13,68,42},{15,54,24,5,55,25},{15,43,15,10,44,16}},
{{4,144,116,4,145,117},{17,68,42},{17,50,22,6,51,23},{19,46,16,6,47,17}},
{{2,139,111,7,140,112},{17,74,46},{7,54,24,16,55,25},{34,37,13}},
{{4,151,121,5,152,122},{4,75,47,14,76,48},{11,54,24,14,55,25},{16,45,15,14,46,16}},
{{6,147,117,4,148,118},{6,73,45,14,74,46},{11,54,24,16,55,25},{30,46,16,2,47,17}},
{{8,132,106,4,133,107},{8,75,47,13,76,48},{7,54,24,22,55,25},{22,45,15,13,46,16}},
{{10,142,114,2,143,115},{19,74,46,4,75,47},{28,50,22,6,51,23},{33,46,16,4,47,17}},
{{8,152,122,4,153,123},{22,73,45,3,74,46},{8,53,23,26,54,24},{12,45,15,28,46,16}},
{{3,147,117,10,148,118},{3,73,45,23,74,46},{4,54,24,31,55,25},{11,45,15,31,46,16}},
{{7,146,116,7,147,117},{21,73,45,7,74,46},{1,53,23,37,54,24},{19,45,15,26,46,16}},
{{5,145,115,10,146,116},{19,75,47,10,76,48},{15,54,24,25,55,25},{23,45,15,25,46,16}},
{{13,145,115,3,146,116},{2,74,46,29,75,47},{42,54,24,1,55,25},{23,45,15,28,46,16}},
{{17,145,115},{10,74,46,23,75,47},{10,54,24,35,55,25},{19,45,15,35,46,16}},
{{17,145,115,1,146,116},{14,74,46,21,75,47},{29,54,24,19,55,25},{11,45,15,46,46,16}},
{{13,145,115,6,146,116},{14,74,46,23,75,47},{44,54,24,7,55,25},{59,46,16,1,47,17}},
{{12,151,121,7,152,122},{12,75,47,26,76,48},{39,54,24,14,55,25},{22,45,15,41,46,16}},
{{6,151,121,14,152,122},{6,75,47,34,76,48},{46,54,24,10,55,25},{2,45,15,64,46,16}},
{{17,152,122,4,153,123},{29,74,46,14,75,47},{49,54,24,10,55,25},{24,45,15,46,46,16}},
{{4,152,122,18,153,123},{13,74,46,32,75,47},{48,54,24,14,55,25},{42,45,15,32,46,16}},
{{20,147,117,4,148,118},{40,75,47,7,76,48},{43,54,24,22,55,25},{10,45,15,67,46,16}},
{{19,148,118,6,149,119},{18,75,47,31,76,48},{34,54,24,34,55,25},{20,45,15,61,46,16}},
};

const int kRemainder[40] = {0,7,7,7,7,7,0,0,0,0,0,0,0,3,3,3,3,3,3,3,4,4,4,4,4,4,4,3,3,3,3,3,3,3,0,0,0,0,0,0};

int g_alphaInt[256];
int g_intAlpha[256];
struct GfInit {
    GfInit() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            g_alphaInt[i] = x; g_intAlpha[x] = i;
            x <<= 1; if (x & 0x100) x ^= 0x11D;
        }
        g_alphaInt[255] = 0; g_intAlpha[0] = 256;
    }
};
GfInit g_gfInit;

const int kGp7[]  = {21,102,238,149,146,229,87,0};
const int kGp10[] = {45,32,94,64,70,118,61,46,67,251,0};
const int kGp13[] = {78,140,206,218,130,104,106,100,86,100,176,152,74,0};
const int kGp15[] = {105,99,5,124,140,237,58,58,51,37,202,91,61,183,8,0};
const int kGp16[] = {120,225,194,182,169,147,191,91,3,76,161,102,109,107,104,120,0};
const int kGp17[] = {136,163,243,39,150,99,24,147,214,206,123,239,43,78,206,139,43,0};
const int kGp18[] = {153,96,98,5,179,252,148,152,187,79,170,118,97,184,94,158,234,215,0};
const int kGp20[] = {190,188,212,212,164,156,239,83,225,221,180,202,187,26,163,61,50,79,60,17,0};
const int kGp22[] = {231,165,105,160,134,219,80,98,172,8,74,200,53,221,109,14,230,93,242,247,171,210,0};
const int kGp24[] = {21,227,96,87,232,117,0,111,218,228,226,192,152,169,180,159,126,251,117,211,48,135,121,229,0};
const int kGp26[] = {70,218,145,153,227,48,102,13,142,245,21,161,53,165,28,111,201,145,17,118,182,103,2,158,125,173,0};
const int kGp28[] = {123,9,37,242,119,212,195,42,87,245,43,21,201,232,27,205,147,195,190,110,180,108,234,224,104,200,223,168,0};
const int kGp30[] = {180,192,40,238,216,251,37,156,130,224,193,226,173,42,125,222,96,239,86,110,48,50,182,179,31,216,152,145,173,41,0};

int GpLen(int n) {
    switch (n) {
        case 7: return 8; case 10: return 11; case 13: return 14; case 15: return 16;
        case 16: return 17; case 17: return 18; case 18: return 19; case 20: return 21;
        case 22: return 23; case 24: return 25; case 26: return 27; case 28: return 29;
        case 30: return 31;
    }
    return 0;
}
const int* GpPtr(int n) {
    switch (n) {
        case 7: return kGp7; case 10: return kGp10; case 13: return kGp13; case 15: return kGp15;
        case 16: return kGp16; case 17: return kGp17; case 18: return kGp18; case 20: return kGp20;
        case 22: return kGp22; case 24: return kGp24; case 26: return kGp26; case 28: return kGp28;
        case 30: return kGp30;
    }
    return NULL;
}
std::string Binary(int x, int digits) {
    char h[32]; sprintf(h, "%x", x);
    std::string s;
    for (size_t i = 0; h[i]; ++i) {
        int v = 0; char c = h[i];
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        s += (v & 8) ? '1' : '0';
        s += (v & 4) ? '1' : '0';
        s += (v & 2) ? '1' : '0';
        s += (v & 1) ? '1' : '0';
    }
    size_t p = s.find_first_not_of('0');
    s = (p == std::string::npos) ? std::string() : s.substr(p);
    while ((int)s.size() < digits) s = "0" + s;
    return s;
}
int GetMode(const std::string& str) {
    if (str.empty()) return 4;
    bool numeric = true;
    for (size_t i = 0; i < str.size(); ++i)
        if (str[i] < '0' || str[i] > '9') { numeric = false; break; }
    if (numeric) return 1;
    static const char* alnum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*./:+-";
    bool al = true;
    for (size_t i = 0; i < str.size(); ++i)
        if (!strchr(alnum, str[i])) { al = false; break; }
    if (al) return 2;
    return 4;
}
void GetVersionEcLevel(int len, int mode, int requested, int& out_version, int& out_ec) {
    int local_mode = mode;
    if (mode == 4) local_mode = 3;
    else if (mode == 8) local_mode = 4;
    const int tab[3][4] = {{10,9,8,8},{12,11,16,10},{14,13,16,12}};
    int minversion = 99, maxec = requested;
    int minlv = 1, maxlv = 4;
    if (requested >= 1 && requested <= 4) { minlv = requested; maxlv = requested; }
    for (int ec = minlv; ec <= maxlv; ++ec) {
        for (int version = 1; version <= 40; ++version) {
            int bits = kCapacity[version - 1][ec - 1] * 8 - 4;
            int digits;
            if (version < 10) digits = tab[0][local_mode - 1];
            else if (version < 27) digits = tab[1][local_mode - 1];
            else digits = tab[2][local_mode - 1];
            int modebits = bits - digits;
            int c;
            if (local_mode == 1) c = (int)floor(modebits * 3.0 / 10.0);
            else if (local_mode == 2) c = (int)floor(modebits * 2.0 / 11.0);
            else if (local_mode == 3) c = (int)floor(modebits * 1.0 / 8.0);
            else c = (int)floor(modebits * 1.0 / 13.0);
            if (c >= len) {
                if (version <= minversion) { minversion = version; maxec = ec; }
                break;
            }
        }
    }
    out_version = minversion; out_ec = maxec;
}
std::string GetLength(const std::string& str, int version, int mode) {
    int i = mode;
    if (mode == 4) i = 3;
    else if (mode == 8) i = 4;
    const int tab[3][4] = {{10,9,8,8},{12,11,16,10},{14,13,16,12}};
    int digits;
    if (version < 10) digits = tab[0][i - 1];
    else if (version < 27) digits = tab[1][i - 1];
    else digits = tab[2][i - 1];
    return Binary((int)str.size(), digits);
}
const int kAsciiTbl[96] = {
    36,-1,-1,-1,37,38,-1,-1,-1,-1,39,40,-1,41,42,43,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,44,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};
std::string EncodeNumeric(const std::string& str) {
    std::string out;
    for (size_t i = 0; i < str.size(); i += 3) {
        std::string a = str.substr(i, 3);
        int v = atoi(a.c_str());
        out += Binary(v, (int)a.size() * 3 + 1);
    }
    return out;
}
std::string EncodeAscii(const std::string& str) {
    std::string out;
    for (size_t i = 0; i < str.size(); i += 2) {
        std::string a = str.substr(i, 2);
        if (a.size() == 2) {
            int b1 = kAsciiTbl[(unsigned char)a[0] - 32];
            int b2 = kAsciiTbl[(unsigned char)a[1] - 32];
            out += Binary(b1 * 45 + b2, 11);
        } else {
            int v = kAsciiTbl[(unsigned char)a[0] - 32];
            out += Binary(v, 6);
        }
    }
    return out;
}
std::string EncodeBinary(const std::string& str) {
    std::string out;
    for (size_t i = 0; i < str.size(); ++i) out += Binary((unsigned char)str[i], 8);
    return out;
}
std::string EncodeData(const std::string& str, int mode) {
    if (mode == 1) return EncodeNumeric(str);
    if (mode == 2) return EncodeAscii(str);
    return EncodeBinary(str);
}
std::string AddPadData(int version, int ec, const std::string& data) {
    int cpty = kCapacity[version - 1][ec - 1] * 8;
    std::string buffer = data;
    int len = (int)buffer.size();
    int count_to_pad = std::min(4, cpty - len);
    if (count_to_pad > 0) { buffer += std::string(count_to_pad, '0'); len += count_to_pad; }
    if (len % 8 != 0) {
        int missing = 8 - len % 8;
        buffer += std::string(missing, '0'); len += missing;
    }
    int remaining_bytes = (cpty - len) / 8;
    for (int i = 1; i <= remaining_bytes; ++i)
        buffer += (i % 2 == 1) ? "11101100" : "00010001";
    return buffer;
}
std::vector<int> ConvertBitstringToBytes(const std::string& data) {
    std::vector<int> msg;
    int n = (int)data.size() / 8;
    for (int i = 0; i < n; ++i) {
        int v = 0;
        for (int b = 0; b < 8; ++b) v = (v << 1) | (data[i * 8 + b] - '0');
        msg.push_back(v);
    }
    return msg;
}
std::vector<int> CalculateErrorCorrection(const std::vector<int>& mp, int num_ec) {
    int len_message = (int)mp.size();
    int highest_exponent = len_message + num_ec - 1;
    std::vector<int> mp_int(highest_exponent + 2, 0);
    std::vector<int> gp_alpha(highest_exponent + 1, 0);
    for (int i = 1; i <= len_message; ++i)
        mp_int[highest_exponent - i + 1] = mp[i - 1];
    for (int i = 1; i <= highest_exponent - len_message; ++i) mp_int[i] = 0;
    mp_int[0] = 0;
    const int* gp = GpPtr(num_ec);
    int gplen = GpLen(num_ec);
    while (highest_exponent >= num_ec) {
        for (int i = 0; i <= highest_exponent; ++i) gp_alpha[i] = 0;
        for (int i = 0; i < gplen; ++i)
            gp_alpha[highest_exponent - num_ec + i] = gp[i];
        int exp = g_intAlpha[mp_int[highest_exponent]];
        for (int i = highest_exponent; i >= highest_exponent - num_ec; --i) {
            if (exp != 256) gp_alpha[i] = (gp_alpha[i] + exp) % 255;
            else gp_alpha[i] = 256;
        }
        for (int i = highest_exponent - num_ec - 1; i >= 0; --i) gp_alpha[i] = 256;
        for (int i = highest_exponent; i >= 0; --i) {
            int ai = (gp_alpha[i] == 256) ? 0 : g_alphaInt[gp_alpha[i]];
            mp_int[i] = ai ^ mp_int[i];
        }
        for (int i = highest_exponent; i >= num_ec; --i) {
            if (mp_int[i] == 0) highest_exponent = i - 1;
            else break;
        }
        if (highest_exponent < num_ec) break;
    }
    std::vector<int> ret;
    for (int i = highest_exponent; i >= 0; --i) ret.push_back(mp_int[i]);
    return ret;
}
std::string ArrangeCodewords(int version, int ec, const std::string& data) {
    std::vector<std::string> datablocks, final_ecblocks;
    int pos = 0;
    const int* blk = kEcBlocks[version - 1][ec - 1];
    for (int g = 0; g < 2; ++g) {
        int count = blk[g * 3 + 0];
        int total = blk[g * 3 + 1];
        int dataCW = blk[g * 3 + 2];
        if (count == 0) continue;
        int size_ec = total - dataCW;
        for (int k = 0; k < count; ++k) {
            std::string datablock = data.substr(pos * 8, dataCW * 8);
            std::vector<int> mp = ConvertBitstringToBytes(datablock);
            std::vector<int> ecv = CalculateErrorCorrection(mp, size_ec);
            std::string ecstr;
            for (size_t x = 0; x < ecv.size(); ++x) ecstr += Binary(ecv[x], 8);
            datablocks.push_back(datablock);
            final_ecblocks.push_back(ecstr);
            pos += dataCW;
        }
    }
    std::string arranged;
    int maxBlockLen = 0;
    for (size_t i = 0; i < datablocks.size(); ++i)
        maxBlockLen = std::max(maxBlockLen, (int)datablocks[i].size());
    for (int p = 0; p < maxBlockLen; p += 8)
        for (size_t i = 0; i < datablocks.size(); ++i)
            if (p < (int)datablocks[i].size())
                arranged += datablocks[i].substr(p, 8);
    maxBlockLen = 0;
    for (size_t i = 0; i < final_ecblocks.size(); ++i)
        maxBlockLen = std::max(maxBlockLen, (int)final_ecblocks[i].size());
    for (int p = 0; p < maxBlockLen; p += 8)
        for (size_t i = 0; i < final_ecblocks.size(); ++i)
            if (p < (int)final_ecblocks[i].size())
                arranged += final_ecblocks[i].substr(p, 8);
    return arranged;
}

const int kAlignmentPattern[41][8] = {
    {0},{0},{6,18},{6,22},{6,26},{6,30},{6,34},
    {6,22,38},{6,24,42},{6,26,46},{6,28,50},{6,30,54},{6,32,58},{6,34,62},
    {6,26,46,66},{6,26,48,70},{6,26,50,74},{6,30,54,78},{6,30,56,82},{6,30,58,86},{6,34,62,90},
    {6,28,50,72,94},{6,26,50,74,98},{6,30,54,78,102},{6,28,54,80,106},{6,32,58,84,110},{6,30,58,86,114},{6,34,62,90,118},
    {6,26,50,74,98,122},{6,30,54,78,102,126},{6,26,52,78,104,130},{6,30,56,82,108,134},{6,34,60,86,112,138},{6,30,58,86,114,142},{6,34,62,90,118,146},
    {6,30,54,78,102,126,150},{6,24,50,76,102,128,154},{6,28,54,80,106,132,158},{6,32,58,84,110,136,162},{6,26,54,82,110,138,166},{6,30,58,86,114,142,170}
};

const char* kTypeInfo[4][8] = {
{"111011111000100","111001011110011","111110110101010","111100010011101","110011000101111","110001100011000","110110001000001","110100101110110"},
{"101010000010010","101000100100101","101111001111100","101101101001011","100010111111001","100000011001110","100111110010111","100101010100000"},
{"011010101011111","011000001101000","011111100110001","011101000000110","010010010110100","010000110000011","010111011011010","010101111101101"},
{"001011010001001","001001110111110","001110011100111","001100111010000","000011101100010","000001001010101","000110100001100","000100000111011"}
};

const char* kVersionInfo[34] = {
"001010010011111000","001111011010000100","100110010101100100","110010110010010100",
"011011111101110100","010001101110001100","111000100001101100","101100000110011100","000101001001111100",
"000111101101000010","101110100010100010","111010000101010010","010011001010110010","011001011001001010",
"110000010110101010","100100110001011010","001101111110111010","001000110111000100","100001111000100100",
"110101011111010110","011100010000110110","010110000011001110","111111001100101110","101011101011011110",
"000010100100111110","101010111001000001","000011110110100001","010111010001010001","111110011110110001",
"110100001101001001","011101000010101001","001001100101011001","100000101010111001","100101100011000101"
};

typedef std::vector<std::vector<int> > Matrix;

void AddPositionDetectionPatterns(Matrix& m) {
    int size = (int)m.size() - 1;
    for (int i = 1; i <= 8; ++i)
        for (int j = 1; j <= 8; ++j) {
            m[i][j] = -2; m[size - 8 + i][j] = -2; m[i][size - 8 + j] = -2;
        }
    for (int i = 1; i <= 7; ++i) {
        m[1][i] = 2; m[7][i] = 2; m[i][1] = 2; m[i][7] = 2;
        m[size][i] = 2; m[size - 6][i] = 2; m[size - i + 1][1] = 2; m[size - i + 1][7] = 2;
        m[1][size - i + 1] = 2; m[7][size - i + 1] = 2; m[i][size - 6] = 2; m[i][size] = 2;
    }
    for (int i = 1; i <= 3; ++i)
        for (int j = 1; j <= 3; ++j) {
            m[2 + j][i + 2] = 2;
            m[size - j - 1][i + 2] = 2;
            m[2 + j][size - i - 1] = 2;
        }
}
void AddTimingPattern(Matrix& m) {
    int size = (int)m.size() - 1;
    int line = 7;
    for (int i = 9; i <= size - 8; ++i) {
        m[i][line] = (i % 2 == 0) ? -2 : 2;
        m[line][i] = (i % 2 == 0) ? -2 : 2;
    }
}
void FillMatrixPosition(Matrix& m, const std::string& bitstr, int x, int y) {
    m[x][y] = (bitstr == "1") ? 2 : -2;
}
void AddVersionInformation(Matrix& m, int version) {
    if (version < 7) return;
    int size = (int)m.size() - 1;
    const char* bitstring = kVersionInfo[version - 7];
    int start_x = size - 10, start_y = 1;
    for (int i = 0; bitstring[i]; ++i) {
        std::string bit(1, bitstring[i]);
        FillMatrixPosition(m, bit, start_x + (i % 3), start_y + (i / 3));
    }
    start_x = 1; start_y = size - 10;
    for (int i = 0; bitstring[i]; ++i) {
        std::string bit(1, bitstring[i]);
        FillMatrixPosition(m, bit, start_x + (i / 3), start_y + (i % 3));
    }
}
void AddAlignmentPattern(Matrix& m) {
    int size = (int)m.size() - 1;
    int version = (size - 17) / 4;
    const int* ap = kAlignmentPattern[version];
    int n = 0;
    while (n < 8 && ap[n] != 0) n++;
    for (int x = 0; x < n; ++x)
        for (int y = 0; y < n; ++y) {
            if ((x == 0 && y == 0) || (x == n - 1 && y == 0) || (x == 0 && y == n - 1)) continue;
            int pos_x = ap[x] + 1, pos_y = ap[y] + 1;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    m[pos_x + dx][pos_y + dy] = (std::max(abs(dx), abs(dy)) % 2 == 0) ? 2 : -2;
        }
}
void AddTypeInfoToMatrix(Matrix& m, int ec, int mask) {
    const char* ec_mask_type = kTypeInfo[ec - 1][mask];
    int size = (int)m.size() - 1;
    for (int i = 1; i <= 7; ++i) FillMatrixPosition(m, std::string(1, ec_mask_type[i-1]), 9, size - i + 1);
    for (int i = 8; i <= 9; ++i) FillMatrixPosition(m, std::string(1, ec_mask_type[i-1]), 9, 17 - i);
    for (int i = 10; i <= 15; ++i) FillMatrixPosition(m, std::string(1, ec_mask_type[i-1]), 9, 16 - i);
    for (int i = 1; i <= 6; ++i) FillMatrixPosition(m, std::string(1, ec_mask_type[i-1]), i, 9);
    FillMatrixPosition(m, std::string(1, ec_mask_type[6]), 8, 9);
    for (int i = 8; i <= 15; ++i) FillMatrixPosition(m, std::string(1, ec_mask_type[i-1]), size - 15 + i, 9);
}
Matrix PrepareMatrixWithMask(int version, int ec, int mask) {
    int size = version * 4 + 17;
    Matrix m(size + 1, std::vector<int>(size + 1, 0));
    AddPositionDetectionPatterns(m);
    AddTimingPattern(m);
    AddVersionInformation(m, version);
    m[9][size - 7] = 2;
    AddAlignmentPattern(m);
    AddTypeInfoToMatrix(m, ec, mask);
    return m;
}
bool MaskFunc(int mask, int x, int y) {
    switch (mask) {
        case 0: return ((y + x) % 2) == 0;
        case 1: return (y % 2) == 0;
        case 2: return (x % 3) == 0;
        case 3: return ((y + x) % 3) == 0;
        case 4: return ((y % 4 - 1.5) * (x % 6 - 2.5)) > 0;
        case 5: return ((y * x) % 2 + (y * x) % 3) == 0;
        case 6: return (((y * x) % 3 + y * x) % 2) == 0;
        case 7: return (((y * x) % 3 + y + x) % 2) == 0;
        default: return false;
    }
}
int GetPixelWithMask(int mask, int x, int y, int dataBit) {
    bool invert = MaskFunc(mask, x - 1, y - 1);
    bool res = ((dataBit == 0) == invert);
    return res ? 1 : -1;
}
void AddDataToMatrix(Matrix& m, const std::string& data, int mask) {
    int size = (int)m.size() - 1;
    int ptr = 1, x = size, y = size;
    int x_dir = -1, y_dir = -1;
    while (true) {
        if (m[x][y] == 0) {
            int bit = (ptr - 1 < (int)data.size()) ? (data[ptr - 1] - '0') : 0;
            m[x][y] = GetPixelWithMask(mask, x, y, bit);
            ptr++;
            if (ptr > (int)data.size() || x < 0) return;
        }
        x = x + x_dir;
        if (x_dir == 1) {
            y = y + y_dir;
            if (y < 1 || y > size) {
                x = x - 2;
                if (x == 7) x = 6;
                y = (y_dir == -1) ? 1 : size;
                y_dir = -y_dir;
            }
        }
        x_dir = -x_dir;
    }
}
int CalculatePenalty(const Matrix& m) {
    int p1 = 0, p2 = 0, p3 = 0;
    int size = (int)m.size() - 1;
    int dark = 0;
    bool last_bit_blank, is_blank;
    for (int x = 1; x <= size; ++x) {
        int run = 0; last_bit_blank = false;
        for (int y = 1; y <= size; ++y) {
            if (m[x][y] > 0) { dark++; is_blank = false; } else is_blank = true;
            if (y == 1) run = 1;
            else if (last_bit_blank == is_blank) run++;
            else { if (run >= 5) p1 += run - 2; run = 1; }
            last_bit_blank = is_blank;
        }
        if (run >= 5) p1 += run - 2;
    }
    for (int y = 1; y <= size; ++y) {
        int run = 0; last_bit_blank = false;
        for (int x = 1; x <= size; ++x) {
            is_blank = (m[x][y] < 0);
            if (x == 1) run = 1;
            else if (last_bit_blank == is_blank) run++;
            else { if (run >= 5) p1 += run - 2; run = 1; }
            last_bit_blank = is_blank;
        }
        if (run >= 5) p1 += run - 2;
    }
    for (int x = 1; x <= size; ++x)
        for (int y = 1; y <= size; ++y) {
            if ((y < size - 1) && (x < size - 1) && (
                (m[x][y] < 0 && m[x+1][y] < 0 && m[x][y+1] < 0 && m[x+1][y+1] < 0) ||
                (m[x][y] > 0 && m[x+1][y] > 0 && m[x][y+1] > 0 && m[x+1][y+1] > 0)))
                p2 += 3;
            if (y + 6 < size &&
                m[x][y] > 0 && m[x][y+1] < 0 && m[x][y+2] > 0 && m[x][y+3] > 0 &&
                m[x][y+4] > 0 && m[x][y+5] < 0 && m[x][y+6] > 0 &&
                ((y + 10 < size && m[x][y+7] < 0 && m[x][y+8] < 0 && m[x][y+9] < 0 && m[x][y+10] < 0) ||
                 (y - 4 >= 1 && m[x][y-1] < 0 && m[x][y-2] < 0 && m[x][y-3] < 0 && m[x][y-4] < 0)))
                p3 += 40;
            if (x + 6 <= size &&
                m[x][y] > 0 && m[x+1][y] < 0 && m[x+2][y] > 0 && m[x+3][y] > 0 &&
                m[x+4][y] > 0 && m[x+5][y] < 0 && m[x+6][y] > 0 &&
                ((x + 10 <= size && m[x+7][y] < 0 && m[x+8][y] < 0 && m[x+9][y] < 0 && m[x+10][y] < 0) ||
                 (x - 4 >= 1 && m[x-1][y] < 0 && m[x-2][y] < 0 && m[x-3][y] < 0 && m[x-4][y] < 0)))
                p3 += 40;
        }
    double r = (double)dark / (double)(size * size);
    int p4 = (int)floor(fabs(r * 100.0 - 50.0)) * 2;
    return p1 + p2 + p3 + p4;
}
Matrix GetMatrixAndPenalty(int version, int ec, const std::string& data, int mask, int& penalty) {
    Matrix m = PrepareMatrixWithMask(version, ec, mask);
    AddDataToMatrix(m, data, mask);
    penalty = CalculatePenalty(m);
    return m;
}
Matrix GetMatrixWithLowestPenalty(int version, int ec, const std::string& data) {
    int min_penalty = 0;
    Matrix best = GetMatrixAndPenalty(version, ec, data, 0, min_penalty);
    for (int i = 1; i <= 7; ++i) {
        int penalty = 0;
        Matrix m = GetMatrixAndPenalty(version, ec, data, i, penalty);
        if (penalty < min_penalty) { best = m; min_penalty = penalty; }
    }
    return best;
}

} // namespace

bool QrEncode(const std::string& text, int ec_level, std::vector<std::vector<int> >& out) {
    int mode = GetMode(text);
    int version = 0, ec = 0;
    GetVersionEcLevel((int)text.size(), mode, ec_level, version, ec);
    if (version > 40 || version < 1) return false;
    std::string data_raw = Binary(mode, 4);
    data_raw += GetLength(text, version, mode);
    data_raw += EncodeData(text, mode);
    data_raw = AddPadData(version, ec, data_raw);
    std::string arranged = ArrangeCodewords(version, ec, data_raw);
    if (arranged.size() % 8 != 0) return false;
    arranged += std::string(kRemainder[version - 1], '0');
    Matrix m = GetMatrixWithLowestPenalty(version, ec, arranged);
    int size = version * 4 + 17;
    out.assign(size, std::vector<int>(size, 0));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            out[y][x] = (m[x + 1][y + 1] > 0) ? 1 : 0;
    return true;
}

// ============================================================
// 手机端 HTML
// ============================================================
static const char* kHtmlTemplate = R"HTML(<!DOCTYPE html>
<html lang="{{LANG}}" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,minimum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="theme-color" content="#000000">
<title>{{TITLE}}</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;-webkit-user-select:none;user-select:none;margin:0;padding:0}
/* 【修复】上面那条 * 规则把 user-select 全关了，输入框也跟着被关掉，
   于是 iOS 长按输入框不会弹“粘贴”菜单 —— 这里给所有输入控件放开，
   并恢复长按呼出菜单（-webkit-touch-callout）与手势 */
input,textarea{
  -webkit-user-select:text;user-select:text;
  -webkit-touch-callout:default;
  touch-action:auto;
}

:root, html[data-theme="dark"]{
  --bg-body:#0f0f0f;
  --bg-pad-1:#1c1c1c;
  --bg-pad-2:#252525;
  --bg-pad-border:#000;
  --bg-controls:#000;
  --bg-btn:#2a2a2a;
  --btn-fg:#ffffff;
  --fg-text:#e5e7eb;
  --pad-word:#e5e7eb;
  --pad-hint:#9ca3af;
  --status-idle:#4b5563;
  --theme-btn-bg:#2a2a2a;
  --theme-btn-fg:#e5e7eb;
  --theme-btn-border:#3a3a3a;
  --panel-bg:#1c1c1c;
  --panel-border:rgba(255,255,255,0.10);
  --panel-sub:rgba(255,255,255,0.05);
  color-scheme: dark;
}
html[data-theme="light"]{
  --bg-body:#f5f5f7;
  --bg-pad-1:#ffffff;
  --bg-pad-2:#eef0f3;
  --bg-pad-border:#d0d4da;
  --bg-controls:#e8eaee;
  --bg-btn:#ffffff;
  --btn-fg:#1f2937;
  --fg-text:#1f2937;
  --pad-word:#c8ccd2;
  --pad-hint:#b5bac1;
  --status-idle:#6b7280;
  --theme-btn-bg:#ffffff;
  --theme-btn-fg:#1f2937;
  --theme-btn-border:#c8ccd2;
  --panel-bg:#ffffff;
  --panel-border:rgba(0,0,0,0.10);
  --panel-sub:rgba(0,0,0,0.04);
  color-scheme: light;
}

html,body{width:100%;height:100%;overflow:hidden;background:var(--bg-body);font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif;overscroll-behavior:none;touch-action:none;color:var(--fg-text)}
body{display:flex;flex-direction:column;padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)}
#pad{flex:1;background:linear-gradient(180deg,var(--bg-pad-1) 0%,var(--bg-pad-2) 100%);position:relative;overflow:hidden;touch-action:none;border-bottom:2px solid var(--bg-pad-border)}
/* 【改动】竖屏：触摸板提醒字与下方提示字累计上移 58px（20 + 18 + 20） */
#pad::before{content:'{{PAD_BEFORE}}';position:absolute;top:calc(50% - 58px);left:50%;transform:translate(-50%,-50%);color:var(--pad-word);font-size:29px;font-weight:700;letter-spacing:6px;pointer-events:none}
#pad::after{
  content:'{{HINT_1}}\A{{HINT_2}}\A{{HINT_3}}\A{{HINT_4}}\A{{HINT_5}}';
  white-space:pre;
  /* 【改动】竖屏：提示字累计上移 58px（原 calc(50% + 40px)，先 -20 再 -18 再 -20） */
  position:absolute;top:calc(50% - 18px);left:50%;transform:translateX(-50%);
  color:var(--pad-hint);font-size:17px;letter-spacing:1px;line-height:1.9;
  text-align:center;pointer-events:none;
}
#status{position:absolute;top:12px;left:50%;transform:translateX(-50%);color:var(--status-idle);font-size:12px;transition:color .15s;font-weight:600;pointer-events:none}
#status.active{color:#22c55e}
#status.drag{color:#f59e0b}
#status.kb{color:#38bdf8}
#status.scroll{color:#a855f7}
#status.rmenu{color:#ef4444}
#status.desktop{color:#facc15}
#status.pill{
  padding:4px 12px;
  border-radius:12px;
  background:var(--bg-pad-1);
  box-shadow:0 1px 3px rgba(0,0,0,.15);
  white-space:nowrap;
  font-weight:700;
}

#themeBtn{
  position:fixed;
  top:calc(env(safe-area-inset-top,0px) + 8px);
  right:calc(env(safe-area-inset-right,0px) + 8px);
  width:38px;height:38px;
  border-radius:10px;
  border:1px solid var(--theme-btn-border);
  background:var(--theme-btn-bg);
  color:var(--theme-btn-fg);
  font-size:20px;
  display:flex;align-items:center;justify-content:center;
  cursor:pointer;
  z-index:9998;
  user-select:none;-webkit-user-select:none;
  touch-action:manipulation;
  transition:background .15s,border-color .15s,transform .1s;
  box-shadow:0 1px 4px rgba(0,0,0,.15);
}
#themeBtn:active{transform:scale(.92)}

#ctrlBtn{
  position:fixed;
  top:calc(env(safe-area-inset-top,0px) + 8px);
  left:calc(env(safe-area-inset-left,0px) + 8px);
  padding:5px 12px;
  background:#4b5563;
  color:#fff;
  font-size:13px;
  font-weight:700;
  letter-spacing:2px;
  border:1px solid transparent;
  border-radius:6px;
  z-index:9999;
  cursor:pointer;
  user-select:none;
  -webkit-user-select:none;
  touch-action:manipulation;
  transition:background .15s,box-shadow .15s,transform .1s;
}
#ctrlBtn:active{ transform:scale(.94); }
#ctrlBtn.on{
  background:#0ea5e9;
  animation:ctrlPulse 1.5s ease-in-out infinite;
}

/* 【新增】剪贴板按钮（触摸板底部居中，位于下方左右键按钮上方） */
#clipBtn{
  position:absolute;
  bottom:2px;
  left:50%;
  transform:translateX(-50%);
  padding:7px 18px;
  background:#4b5563;
  color:#fff;
  font-size:18px;
  font-weight:700;
  line-height:1;
  border:1px solid transparent;
  border-radius:10px;
  z-index:9999;
  cursor:pointer;
  user-select:none;
  -webkit-user-select:none;
  touch-action:manipulation;
  transition:background .15s,transform .1s;
  box-shadow:0 2px 8px rgba(0,0,0,.25);
}
#clipBtn:active{ transform:translateX(-50%) scale(.94); }
#clipBtn.on{ background:#0ea5e9; }

#selectAllBadge{
  position:fixed;
  top:calc(env(safe-area-inset-top,0px) + 8px);
  left:50%;
  transform:translateX(-50%);
  max-width:88vw;
  padding:5px 12px;
  background:#dc2626;
  color:#fff;
  font-size:13px;
  font-weight:700;
  letter-spacing:2px;
  line-height:1.4;
  text-align:center;
  white-space:normal;
  border-radius:6px;
  z-index:9999;
  pointer-events:none;
  user-select:none;
  -webkit-user-select:none;
  animation:ctrlPulse 1.5s ease-in-out infinite;
}
@keyframes ctrlPulse{
  0%,100%{ box-shadow:0 0 0 2px rgba(14,165,233,.5), 0 2px 8px rgba(0,0,0,.5); }
  50%    { box-shadow:0 0 0 5px rgba(14,165,233,.2), 0 2px 14px rgba(0,0,0,.6); }
}

.controls{display:grid;grid-template-columns:repeat(6,1fr);gap:8px;padding:10px;background:var(--bg-controls)}
.controls button{
  display:flex;align-items:center;justify-content:center;
  padding:18px 8px;border:none;border-radius:12px;
  color:var(--btn-fg);background:var(--bg-btn);touch-action:manipulation;cursor:pointer;
  line-height:1;
  transition:transform .06s;
}
.controls button:active{transform:scale(.94)}
.ico{
  display:flex;align-items:center;justify-content:center;
  line-height:1;font-size:34px;font-weight:600;
}
.ico svg{width:28px;height:40px;display:block}
.ico.txt{font-size:22px;font-weight:800;letter-spacing:1px}
/* 回车按钮不再写 ENT
   用矢量描边绘制，圆头圆角，任意分辨率都清晰；
   整版放大 +1px：两条路径坐标整体放大 13.5%，描边 2 -> 3，图标 36px -> 41px；
   回车符号整体下移 2px：position:relative + top:2px */
#btnE .ico.ret svg{width:41px;height:41px;position:relative;top:2px}
#btnI .ico svg{width:26px;height:26px}

#btnL{order:1;grid-column:span 3}
#btnR{order:2;grid-column:span 3}
#btnU{order:3;grid-column:span 2}
#btnI{order:4;grid-column:span 2}
#btnN{order:5;grid-column:span 2}
#btnA{order:6;grid-column:span 2}
#btnK{order:7;grid-column:span 2}
#btnE{order:8;grid-column:span 2}

.bl{color:#fff;background:#2563eb}.bl:active{background:#1d4ed8}
.rd{color:#fff;background:#dc2626}.rd:active{background:#b91c1c}
.gr{color:#fff;background:#16a34a}.gr:active{background:#15803d}
.gr.holding{background:#059669;box-shadow:0 0 0 3px rgba(16,163,74,.4) inset}
.kb{color:#fff;background:#0ea5e9}.kb:active{background:#0284c7}
.kb.on{background:#0369a1;box-shadow:0 0 0 2px #38bdf8 inset}
.en{color:#fff;background:#7c3aed}.en:active{background:#6d28d9}
.or{color:#fff;background:#ea580c}.or:active{background:#c2410c}

#kbdInput{position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;border:0;padding:0;margin:0;outline:none;background:transparent;color:transparent;caret-color:transparent;font-size:16px;z-index:-1;pointer-events:none;}

/* 悬浮话筒按钮：位于鼠标左键上方，可拖动；
   点一下给电脑发全局 Win+H，打开 Windows 自带的“语音输入” */
#micBtn{
  position:fixed;
  left:0;top:0;
  width:46px;height:46px;
  padding:0;
  border-radius:50%;
  border:1px solid var(--theme-btn-border);
  background:var(--theme-btn-bg);
  color:var(--theme-btn-fg);
  display:flex;align-items:center;justify-content:center;
  cursor:pointer;
  z-index:9997;
  opacity:0;
  touch-action:none;
  user-select:none;-webkit-user-select:none;
  box-shadow:0 2px 10px rgba(0,0,0,.35);
  transition:background .15s,border-color .15s,color .15s,transform .1s;
}
#micBtn svg{width:24px;height:24px;display:block;pointer-events:none}
#micBtn:active{transform:scale(.94)}
#micBtn.on{
  background:#dc2626;border-color:#dc2626;color:#fff;
  animation:micPulse 1.4s ease-in-out infinite;
}
/* 【新增】Win+H 转完文字后拍到的“输入框”截图：只看不改，铺满整块触摸板
   · 双指捏合手感跟着缩放；左滑/上滑贴到左边屏幕边缘消隐成一条几像素的细条；
   · 从左边屏幕边缘往右点/滑，细条收回原位重新显示。 */
#micShot{
  position:absolute;inset:6px;
  z-index:9995;
  display:none;
  align-items:center;justify-content:center;
  pointer-events:none;
  transition:transform .22s ease,opacity .22s ease;
}
#micShot.show{ display:flex; }
/* 已贴到左边屏幕边缘：整块滑出屏幕，只在左边留一条细条 */
#micShot.docked{
  transform:translateX(calc(-100% - 8px));
  opacity:0;
  transition:transform .24s ease,opacity .24s ease;
}
/* 贴边后左边屏幕边缘的呼出热区（显示截图时才出现） */
#micShotEdge{
  position:fixed;
  left:0;top:0;bottom:0;
  width:14px;
  z-index:9996;
  display:none;
  background:transparent;
  touch-action:none;
  -webkit-tap-highlight-color:transparent;
}
#micShotEdge.on{ display:block; }
#micShot img{
  display:block;
  /* 【改动】不再限制在触摸板大小以内：放大后可以铺到触摸板外面去 */
  flex:0 0 auto;
  max-width:none;max-height:none;
  border-radius:8px;
  pointer-events:none;
}
@keyframes micPulse{
  0%,100%{ box-shadow:0 0 0 3px rgba(220,38,38,.45), 0 2px 10px rgba(0,0,0,.35); }
  50%    { box-shadow:0 0 0 8px rgba(220,38,38,.10), 0 2px 10px rgba(0,0,0,.35); }
}
/* 【新增】剪贴板弹窗 */
#clipPanel{
  position:fixed;inset:0;
  background:rgba(0,0,0,0.6);
  z-index:10000;
  display:none;
  align-items:center;justify-content:center;
  padding:20px;
  -webkit-backdrop-filter:blur(4px);backdrop-filter:blur(4px);
}
#clipPanel.show{ display:flex; }
.clip-dialog{
  background:var(--panel-bg);
  color:var(--fg-text);
  border-radius:14px;
  width:min(560px, 94vw);
  max-height:82vh;
  display:flex;flex-direction:column;
  overflow:hidden;
  box-shadow:0 12px 48px rgba(0,0,0,0.5);
  border:1px solid var(--panel-border);
}
.clip-title-bar{
  padding:14px 16px;
  font-size:16px;font-weight:700;
  display:flex;justify-content:space-between;align-items:center;
  border-bottom:1px solid var(--panel-border);
  gap:8px;
}
.clip-title-bar .clip-title-txt{ display:flex;align-items:center;gap:8px; }
#clipCloseBtn{
  background:none;border:none;
  color:var(--fg-text);
  font-size:26px;line-height:1;
  cursor:pointer;padding:0 6px;border-radius:6px;
}
#clipCloseBtn:active{ background:var(--panel-sub); }
#clipBody{
  padding:16px;
  overflow:auto;
  flex:1;
  font-size:14px;line-height:1.5;
  -webkit-overflow-scrolling:touch;
  touch-action:pan-y;
}
.clip-text-area{
  background:var(--panel-sub);
  padding:12px;
  border-radius:8px;
  white-space:pre-wrap;word-break:break-word;
  max-height:44vh;overflow:auto;
  font-family:ui-monospace,Menlo,Consolas,monospace;
  font-size:13px;margin-bottom:12px;
  -webkit-user-select:text;user-select:text;
  -webkit-overflow-scrolling:touch;
  touch-action:pan-y;
}
.clip-file-list{ list-style:none;padding:0;margin:0 0 12px 0; }
.clip-file-list li{
  padding:10px 12px;
  background:var(--panel-sub);
  border-radius:8px;
  margin-bottom:8px;
  display:flex;justify-content:space-between;align-items:center;
  gap:8px;
}
.clip-file-list .fname{
  flex:1;min-width:0;
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
  font-size:13px;
}
.clip-file-list .fsize{
  font-size:12px;opacity:0.6;white-space:nowrap;
}
.clip-file-list a.dl,
.clip-file-list span.dl{
  color:#0ea5e9;text-decoration:none;font-weight:700;
  padding:4px 10px;border-radius:6px;
  background:rgba(14,165,233,0.15);
  white-space:nowrap;font-size:12px;
}
/* 下载按钮改成 span + JS 触发：iOS 上 a[download] 的默认行为不可靠，
   必须由脚本在用户手势中直接发起下载，否则会被静默拦截 */
.clip-file-list span.dl{
  cursor:pointer;
  -webkit-user-select:none;user-select:none;
  -webkit-tap-highlight-color:transparent;
}
/* 【新增】0 字节文件的“复制名称”按钮（绿色，与蓝色“下载”区分） */
.clip-file-list span.cp{
  color:#16a34a;font-weight:700;
  padding:4px 10px;border-radius:6px;
  background:rgba(22,163,74,0.15);
  white-space:nowrap;font-size:12px;
  cursor:pointer;
  -webkit-user-select:none;user-select:none;
  -webkit-tap-highlight-color:transparent;
  transition:background .15s,color .15s;
}
.clip-file-list span.cp:active{ background:rgba(22,163,74,0.32); }
/* 复制成功 / 失败：短暂变成实心块，给用户明确反馈 */
.clip-file-list span.cp.ok{ color:#fff;background:#16a34a; }
.clip-file-list span.cp.err{ color:#fff;background:#dc2626; }
.clip-copy-pop{
  margin:0 0 12px 0;padding:12px;
  border-radius:10px;
  background:rgba(22,163,74,0.10);
  border:1px solid rgba(22,163,74,0.40);
}
.clip-copy-pop input.ccp-input{
  display:block;width:100%;box-sizing:border-box;
  font-size:16px;              /* >=16px：iOS 聚焦输入框时不会放大页面 */
  padding:10px 12px;border-radius:8px;
  border:1px solid var(--panel-border);
  background:var(--panel-bg);color:var(--fg-text);
  -webkit-user-select:text;user-select:text;
  -webkit-touch-callout:default;
  touch-action:auto;
  -webkit-tap-highlight-color:transparent;
}
.clip-copy-pop .ccp-hint{
  font-size:12px;line-height:1.5;margin-top:8px;
  color:var(--fg-text);opacity:0.75;
}
.clip-copy-pop .clip-action{ margin-top:8px; }
.clip-action{
  display:block;width:100%;
  padding:12px;border:none;border-radius:8px;
  background:#2563eb;color:#fff;
  font-size:15px;font-weight:700;
  cursor:pointer;margin-top:4px;
}
.clip-action:active{ background:#1d4ed8; }
.clip-action.ok{ background:#16a34a; }
.clip-action.ghost{
  background:var(--panel-sub);color:var(--fg-text);
  border:1px solid var(--panel-border);
}
/* 【新增】剪贴板底部工具条：上传手机文件 / 新建文本 */
.clip-tools{
  display:flex;gap:8px;
  padding:10px 16px 14px;
  border-top:1px solid var(--panel-border);
  background:var(--panel-bg);
}
.clip-tool-btn{
  flex:1;
  padding:11px 8px;
  border-radius:9px;
  border:1px solid var(--panel-border);
  background:var(--panel-sub);
  color:var(--fg-text);
  font-size:14px;font-weight:700;
  line-height:1.2;
  cursor:pointer;
  -webkit-tap-highlight-color:transparent;
  -webkit-user-select:none;user-select:none;
  transition:background .15s,transform .1s;
}
.clip-tool-btn:active{ background:rgba(37,99,235,.18);transform:scale(.98); }
.clip-upload-status{
  padding:0 16px 8px;
  font-size:12px;line-height:1.5;
  color:#16a34a;
  background:var(--panel-bg);
}
.clip-upload-status.err{ color:#dc2626; }
/* 【新增】新建文本的输入区 */
.clip-compose{ display:none; padding:12px 16px 16px; border-top:1px solid var(--panel-border); background:var(--panel-bg); }
.clip-compose.show{ display:block; }
/* 【改动】新建文本：点按钮时现场创建的独立窗口，强制贴在最顶部、压在最上层 */
#textWin{
  position:fixed;left:0;top:0;
  width:100%;
  z-index:2147483647;            /* 顶到最大层级：永远压在剪贴板面板(10000)等所有层之上 */
  display:none;
}
#textWin.show{ display:block; }
#textWin .text-win-box{
  background:var(--panel-bg);
  color:var(--fg-text);
  border:1px solid var(--panel-border);
  border-top:none;
  border-radius:0 0 14px 14px;
  box-shadow:0 12px 48px rgba(0,0,0,.5);
  overflow:hidden;
}
/* 搬进窗口的输入区：铺满窗口、去掉原来的分隔线 */
#textWin .clip-compose{
  display:block;
  padding:10px 14px 12px;
  border-top:none;
  background:var(--panel-bg);
}
.clip-compose textarea{
  display:block;width:100%;box-sizing:border-box;
  min-height:100px;
  padding:10px 12px;
  font-size:16px;line-height:1.5;
  border-radius:8px;
  border:1px solid var(--panel-border);
  background:var(--panel-sub);color:var(--fg-text);
  resize:vertical;
  /* 允许长按粘贴 / 选中文字（否则被全局 user-select:none 关掉） */
  -webkit-user-select:text;user-select:text;
  -webkit-touch-callout:default;
  touch-action:auto;
}
/* iOS 上 display:none 的 file input 有时点不开选择器，
   改成“渲染出来但挪到屏幕外 + 透明”的写法最稳 */
.clip-file-input{
  position:fixed;
  left:-9999px;
  top:0;
  width:1px;
  height:1px;
  opacity:0;
  border:0;
  padding:0;
  margin:0;
}
.clip-badge{
  display:inline-block;padding:2px 8px;border-radius:4px;
  background:rgba(148,163,184,0.25);
  font-size:11px;font-weight:600;letter-spacing:1px;
}
.clip-badge.cut{ background:#dc2626;color:#fff; }
.clip-empty{
  text-align:center;padding:30px 20px;
  opacity:0.6;font-size:14px;
}
.clip-loading{
  text-align:center;padding:26px 20px;
  opacity:0.6;font-size:14px;
}
.clip-hint{
  margin:0 0 12px 0;padding:10px 12px;border-radius:8px;
  background:rgba(14,165,233,0.14);
  border:1px solid rgba(14,165,233,0.38);
  font-size:12px;line-height:1.55;
  word-break:break-word;
}

/* 【新增】复刻 ROVE 霸屏状态最右边屏幕的滚轮条：
   滚轮向上 / 鼠标中键 / 滚轮向下 三个按钮。
   尺寸、颜色、圆角、描边、模糊、阴影与 ROVE 一致；
   按下高亮 rgba(255,255,255,.24) 也一致。
   【改动】透明度和最早一致（opacity:.25）；
   初始位置整体上移 5px；按住浮动岛拖动可自由挪到任意位置
   （包括下方回车键等按键区域）；
   拖到右侧屏幕边缘松手会贴边消隐，只留 6px 边条，
   点/滑右侧屏幕边缘线即可呼出。 */
#immScroll{
  position:absolute;
  right:0;
  bottom:5px;              /* 【改动】初始位置比原来上移 5px */
  z-index:94;
  display:flex;
  flex-direction:column;
  padding:3px;
  border-radius:26px;
  background:rgba(128,128,128,.72);
  border:1px solid rgba(255,255,255,.3);
  backdrop-filter:blur(10px);
  -webkit-backdrop-filter:blur(10px);
  box-shadow:0 8px 26px rgba(0,0,0,.4);
  touch-action:none;
  -webkit-user-select:none;
  user-select:none;
  -webkit-touch-callout:none;
  opacity:.25;             /* 【改动】恢复最早的半透明度 */
  transition:transform .18s ease;
  will-change:transform;
}
#immScroll.dragging{ cursor:grabbing; }
/* 【新增】右侧屏幕边缘呼出热区：贴边消隐后，点/滑这里把浮动岛呼出来
   （高度/位置由脚本对齐到那条 6px 边条上，不挡其它区域） */
#immEdgeTrigger{
  position:fixed;
  right:0;
  width:14px;
  z-index:95;
  display:none;
  background:transparent;
  touch-action:none;
  -webkit-tap-highlight-color:transparent;
}
#immScroll button{
  width:46px;height:46px;
  display:flex;align-items:center;justify-content:center;
  border-radius:21px;
  border:0;background:none;padding:0;
  color:#f4f7fb;
  font:inherit;
  touch-action:none;
  -webkit-tap-highlight-color:transparent;
  -webkit-user-select:none;user-select:none;
  -webkit-touch-callout:none;
}
#immScroll button:active{background:rgba(255,255,255,.24)}
#immScroll svg{width:23px;height:23px;display:block;pointer-events:none}

@media (orientation: landscape){
  /* 【改动】横屏保持原位（不受竖屏上移 20px 影响） */
  #pad::before{ top:50%; font-size:23px; letter-spacing:4px; color:var(--pad-word); }
  #pad::after{
    content:'{{LANDSCAPE_HINT}}';
    white-space:normal;
    top:auto;
    bottom:8px;
    left:50%;
    transform:translateX(-50%);
    width:92%;
    font-size:11px;
    line-height:1.3;
    letter-spacing:0;
  }
  #status{ top:6px; font-size:11px; }

  #selectAllBadge{
    font-size:11px;
    padding:4px 10px;
    letter-spacing:1px;
  }

  #themeBtn{ width:32px;height:32px;font-size:16px;border-radius:8px; }
  #clipBtn{ bottom:22px; padding:5px 14px; font-size:15px; border-radius:8px; }

  .controls{
    grid-template-columns:repeat(8,1fr);
    gap:4px;
    padding:4px 6px;
    padding-bottom:calc(4px + env(safe-area-inset-bottom));
  }
  .controls button{
    padding:6px 2px;
    border-radius:8px;
    min-height:0;
  }
  .ico{ font-size:22px; }
  .ico svg{ width:18px; height:26px; }
  .ico.txt{ font-size:14px; letter-spacing:0; }
  #btnE .ico.ret svg{ width:25px; height:25px; }
  #btnI .ico svg{ width:18px; height:18px; }

  #btnL,#btnR,#btnU,#btnN,#btnA,#btnK,#btnE,#btnI{ grid-column:auto; }

  #btnU{order:1}
  #btnL{order:2}
  #btnA{order:3}
  #btnK{order:4}
  #btnE{order:5}
  #btnI{order:6}
  #btnR{order:7}
  #btnN{order:8}

  .clip-text-area{ max-height:36vh; }
}
</style>
</head>
<body>

<button id="ctrlBtn" aria-label="Toggle Ctrl">CTRL</button>
<button id="themeBtn" aria-label="Toggle theme">☀</button>

<div id="pad">
  <div id="status">{{JS_READY}}</div>
  <button id="clipBtn" aria-label="PC Clipboard">📋</button>
  <!-- 【新增】复刻 ROVE 霸屏最右边的滚轮条：紧贴触摸板右侧边缘、右键按钮上方；
       按住浮动岛拖动可自由挪位，拖到右侧边缘会贴边消隐 -->
  <div class="immScroll" id="immScroll">
    <button data-scroll="up" aria-label="滚轮持续向上"><svg viewBox="0 0 24 24" fill="none"><path d="M6 15l6-6 6 6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
    <button data-mid="1" aria-label="鼠标中键"><svg viewBox="0 0 24 24" fill="none"><rect x="7" y="2.7" width="10" height="18.6" rx="5" stroke="currentColor" stroke-width="1.7"/><path d="M12 6.4v4.2" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/></svg></button>
    <button data-scroll="down" aria-label="滚轮持续向下"><svg viewBox="0 0 24 24" fill="none"><path d="M6 9l6 6 6-6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
  </div>
  <!-- 【新增】贴边消隐后，右侧屏幕边缘的呼出热区 -->
  <div id="immEdgeTrigger" aria-hidden="true"></div>
</div>
<!-- 【新增】“输入框”截图贴到左边屏幕边缘后，左边屏幕边缘的呼出热区 -->
<div id="micShotEdge" aria-hidden="true"></div>

<div class="controls">
  <button class="bl" id="btnL">
    <span class="ico">
      <svg viewBox="0 0 24 34" aria-hidden="true">
        <rect x="1.5" y="1.5" width="21" height="31" rx="8" ry="8" fill="none" stroke="currentColor" stroke-width="2"/>
        <path d="M12 1.5 V15" stroke="currentColor" stroke-width="2" fill="none"/>
        <path d="M1.5 15 H22.5" stroke="currentColor" stroke-width="2" fill="none"/>
        <text x="6.8" y="12" text-anchor="middle" font-size="10" font-weight="700" font-family="Arial,Helvetica,sans-serif" fill="currentColor">L</text>
      </svg>
    </span>
  </button>
  <button class="rd" id="btnR">
    <span class="ico">
      <svg viewBox="0 0 24 34" aria-hidden="true">
        <rect x="1.5" y="1.5" width="21" height="31" rx="8" ry="8" fill="none" stroke="currentColor" stroke-width="2"/>
        <path d="M12 1.5 V15" stroke="currentColor" stroke-width="2" fill="none"/>
        <path d="M1.5 15 H22.5" stroke="currentColor" stroke-width="2" fill="none"/>
        <text x="17.2" y="12" text-anchor="middle" font-size="10" font-weight="700" font-family="Arial,Helvetica,sans-serif" fill="currentColor">R</text>
      </svg>
    </span>
  </button>
  <button class="gr" id="btnU"><span class="ico">↑</span></button>
  <button class="or" id="btnI">
    <span class="ico">
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <defs>
          <mask id="swAppMask" maskUnits="userSpaceOnUse" x="0" y="0" width="24" height="24">
            <rect x="0" y="0" width="24" height="24" fill="#fff"/>
            <rect x="8" y="9" width="15.4" height="13.4" rx="4" fill="#000"/>
          </mask>
        </defs>
        <g mask="url(#swAppMask)">
          <rect x="2.6" y="3.6" width="11.4" height="9.4" rx="2" fill="none" stroke="currentColor" stroke-width="2"/>
        </g>
        <rect x="10" y="11" width="11.4" height="9.4" rx="2" fill="none" stroke="currentColor" stroke-width="2"/>
      </svg>
    </span>
  </button>
  <button class="gr" id="btnN"><span class="ico">↓</span></button>
  <button class="or" id="btnA"><span class="ico">⌫</span></button>
  <button class="kb" id="btnK"><span class="ico">⌨</span></button>
  <!-- 原来的 ENT 文字换成回车符号：
       ← 左向箭头 + 右端向上弯的竖钩，圆头圆角；
       整版放大 +1px：坐标放大 13.5%、描边 3 -->
  <button class="en" id="btnE" aria-label="{{JS_ENTER}}">
    <span class="ico ret">
      <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
        <path d="M18.75 4.78 V11.25 A2.95 2.95 0 0 1 15.8 14.2 H4.11" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
        <path d="M8.31 9.77 L4.11 14.2 L8.31 18.63" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
      </svg>
    </span>
  </button>
</div>

<!-- 【新增】剪贴板弹窗 -->
<div id="clipPanel">
  <div class="clip-dialog">
    <div class="clip-title-bar">
      <span class="clip-title-txt" id="clipTitleText">{{JS_CLIP_TITLE}}</span>
      <button id="clipCloseBtn" aria-label="Close">×</button>
    </div>
    <div id="clipBody"></div>
    <!-- 【新增】新建文本的输入区（默认收起） -->
    <div class="clip-compose" id="clipCompose">
      <textarea id="clipComposeInput" placeholder="{{JS_CLIP_NEWTEXT_PH}}"></textarea>
      <button class="clip-action" id="clipComposeSend">{{JS_CLIP_SEND}}</button>
      <button class="clip-action ghost" id="clipComposeCancel">{{JS_CLIP_CANCEL}}</button>
    </div>
    <div class="clip-upload-status" id="clipUploadStatus"></div>
    <!-- 【新增】底部工具条：把手机里的文件/照片/视频上传到电脑剪贴板 + 新建文本 -->
    <div class="clip-tools">
      <button class="clip-tool-btn" id="clipUploadBtn">⬆ {{JS_CLIP_UPLOAD}}</button>
      <button class="clip-tool-btn" id="clipTextBtn">✎ {{JS_CLIP_NEWTEXT}}</button>
    </div>
    <input type="file" id="clipFileInput" class="clip-file-input" multiple>
  </div>
</div>

<input id="kbdInput" type="text" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false" inputmode="text">
<!-- 【新增】悬浮话筒（鼠标左键上方，可拖动） -->
<button id="micBtn" type="button" aria-label="Voice input" title="Win+H">
  <svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <rect x="9" y="2" width="6" height="12" rx="3"/>
    <path d="M5 11a7 7 0 0 0 14 0"/>
    <path d="M12 18v3"/>
    <path d="M8.5 21h7"/>
  </svg>
</button>
<script>
(function(){
'use strict';

var L = {
  ready:      "{{JS_READY}}",
  ctrlLocked: "{{JS_CTRL_LOCKED}}",
  ctrlRel:    "{{JS_CTRL_RELEASED}}",
  drag:       "{{JS_DRAG}}",
  release:    "{{JS_RELEASE}}",
  single:     "{{JS_SINGLE}}",
  dbl:        "{{JS_DOUBLE}}",
  right:      "{{JS_RIGHT}}",
  scrollUp:   "{{JS_SCROLL_UP}}",
  scrollDown: "{{JS_SCROLL_DOWN}}",
  two:        "{{JS_TWO}}",
  three:      "{{JS_THREE}}",
  pinch:      "{{JS_PINCH}}",
  desktop:    "{{JS_DESKTOP}}",
  prevWin:    "{{JS_PREV_WIN}}",
  nextWin:    "{{JS_NEXT_WIN}}",
  kbOpen:     "{{JS_KB_OPEN}}",
  kbClose:    "{{JS_KB_CLOSE}}",
  micWinH:    "{{JS_MIC_WINH}}",
  micShot:    "{{JS_MIC_SHOT}}",
  backspace:  "{{JS_BACKSPACE}}",
  enter:      "{{JS_ENTER}}",
  tab:        "{{JS_TAB}}",
  esc:        "{{JS_ESC}}",
  arrow:      "{{JS_ARROW}}",
  selectAll:  "{{JS_SELECT_ALL}}",
  selected:   "{{JS_SELECTED_BADGE}}",
  switchApp:  "{{JS_SWITCH_APP}}",
  // 【新增】剪贴板相关
  clipTitle:        "{{JS_CLIP_TITLE}}",
  clipEmpty:        "{{JS_CLIP_EMPTY}}",
  clipCut:          "{{JS_CLIP_CUT}}",
  clipCopyLabel:    "{{JS_CLIP_COPY_LABEL}}",
  clipCopyToPhone:  "{{JS_CLIP_COPY_TO_PHONE}}",
  clipCopied:       "{{JS_CLIP_COPIED}}",
  clipDownload:     "{{JS_CLIP_DOWNLOAD}}",
  clipDownloadAll:  "{{JS_CLIP_DOWNLOAD_ALL}}",
  clipLoading:      "{{JS_CLIP_LOADING}}",
  clipError:        "{{JS_CLIP_ERROR}}",
  clipNoPermission: "{{JS_CLIP_NO_PERM}}",
  // 文件夹 → ZIP 打包下载
  clipZip:          "{{JS_CLIP_ZIP}}",
  clipZipAll:       "{{JS_CLIP_ZIP_ALL}}",
  clipZipTip:       "{{JS_CLIP_ZIP_TIP}}",
  clipZipTooBig:    "{{JS_CLIP_ZIP_TOO_BIG}}",
  clipItemWord:     "{{JS_CLIP_ITEM_WORD}}",
  clipZipFileName:  "{{JS_CLIP_ZIP_FILE}}",
  // 0 字节文件：复制文件名前缀
  clipCopyName:     "{{JS_CLIP_COPY_NAME}}",
  clipCopyFail:     "{{JS_CLIP_COPY_FAIL}}",
  clipCopyManual:   "{{JS_CLIP_COPY_MANUAL}}",
  clipCopyManualClose: "{{JS_CLIP_COPY_MANUAL_CLOSE}}",
  // 手机 → 电脑：上传文件 / 新建文本
  clipUpload:       "{{JS_CLIP_UPLOAD}}",
  clipNewText:      "{{JS_CLIP_NEWTEXT}}",
  clipNewTextPh:    "{{JS_CLIP_NEWTEXT_PH}}",
  clipSend:         "{{JS_CLIP_SEND}}",
  clipCancel:       "{{JS_CLIP_CANCEL}}",
  clipUploading:    "{{JS_CLIP_UPLOADING}}",
  clipUploadDone:   "{{JS_CLIP_UPLOAD_DONE}}",
  clipUploadFail:   "{{JS_CLIP_UPLOAD_FAIL}}",
  clipUploadNoNet:  "{{JS_CLIP_UPLOAD_NONET}}",
  clipSent:         "{{JS_CLIP_SENT}}",
  clipPasteFilled:  "{{JS_CLIP_PASTE_FILLED}}",
  ctrl:       "CTRL"
};

var THEME_KEY = 'pm_theme';
var themeBtn = document.getElementById('themeBtn');

function applyTheme(t){
  document.documentElement.setAttribute('data-theme', t);
  themeBtn.textContent = (t === 'dark') ? '☀' : '☾';
  var meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', t === 'dark' ? '#000000' : '#f5f5f7');
}
function detectInitialTheme(){
  try{
    var saved = localStorage.getItem(THEME_KEY);
    if (saved === 'dark' || saved === 'light') return saved;
  }catch(e){}
  try{
    if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches)
      return 'light';
  }catch(e){}
  return 'dark';
}
function toggleTheme(){
  var cur = document.documentElement.getAttribute('data-theme') || 'dark';
  var next = (cur === 'dark') ? 'light' : 'dark';
  applyTheme(next);
  try{ localStorage.setItem(THEME_KEY, next); }catch(e){}
}
applyTheme(detectInitialTheme());

var themeGuard = 0;
themeBtn.addEventListener('touchstart', function(e){
  e.preventDefault(); e.stopPropagation();
  themeGuard = Date.now();
  toggleTheme();
}, {passive:false});
themeBtn.addEventListener('click', function(e){
  if(Date.now() - themeGuard < 800) return;
  e.preventDefault();
  toggleTheme();
});

var __autoFSTried = false;
function tryAutoFullscreen(){
  if(__autoFSTried) return;
  __autoFSTried = true;
  try{
    var el = document.documentElement;
    var opt = { navigationUI: 'hide' };
    if(el.requestFullscreen){
      var p = el.requestFullscreen(opt);
      if(p && p.catch) p.catch(function(){});
    } else if(el.webkitRequestFullscreen){
      el.webkitRequestFullscreen();
    } else if(el.webkitRequestFullScreen){
      el.webkitRequestFullScreen();
    } else if(el.mozRequestFullScreen){
      el.mozRequestFullScreen();
    } else if(el.msRequestFullscreen){
      el.msRequestFullscreen();
    }
  }catch(err){}
}
document.addEventListener('touchstart', function(){ tryAutoFullscreen(); }, {capture:true, passive:true, once:false});
document.addEventListener('mousedown', function(){ tryAutoFullscreen(); }, {capture:true, once:false});
document.addEventListener('touchend', function(){
  if(!document.fullscreenElement && !document.webkitFullscreenElement &&
     !document.mozFullScreenElement && !document.msFullscreenElement){
    __autoFSTried = false;
  }
}, {passive:true});

var ctrlLocked = false;
var ctrlBtnEl = document.getElementById('ctrlBtn');

function setCtrlBadge(on){
  ctrlLocked = !!on;
  if(on) ctrlBtnEl.classList.add('on');
  else   ctrlBtnEl.classList.remove('on');
}

var selectAllBadgeEl = null;
var selectAllBadgeTimer = null;
function flashBadge(text){
  if(selectAllBadgeTimer){ clearTimeout(selectAllBadgeTimer); selectAllBadgeTimer = null; }
  if(selectAllBadgeEl){
    if(selectAllBadgeEl.parentNode) selectAllBadgeEl.parentNode.removeChild(selectAllBadgeEl);
    selectAllBadgeEl = null;
  }
  selectAllBadgeEl = document.createElement('div');
  selectAllBadgeEl.id = 'selectAllBadge';
  selectAllBadgeEl.textContent = text;
  document.body.appendChild(selectAllBadgeEl);
  selectAllBadgeTimer = setTimeout(function(){
    selectAllBadgeTimer = null;
    if(selectAllBadgeEl){
      if(selectAllBadgeEl.parentNode) selectAllBadgeEl.parentNode.removeChild(selectAllBadgeEl);
      selectAllBadgeEl = null;
    }
  }, 1500);
}
function flashSelectAllBadge(){ flashBadge(L.selected); }
function flashSwitchAppBadge(){ flashBadge(L.switchApp); }

try{ fetch('/keyboard/special?k=ctrl_up', {cache:'no-store', keepalive:true}).catch(function(){}); }catch(e){}
function releaseCtrlOnUnload(){
  if(!ctrlLocked) return;
  ctrlLocked = false;
  try{ navigator.sendBeacon('/keyboard/special?k=ctrl_up'); }catch(e){}
  try{ fetch('/keyboard/special?k=ctrl_up', {cache:'no-store', keepalive:true}).catch(function(){}); }catch(e){}
}
window.addEventListener('pagehide', releaseCtrlOnUnload);
window.addEventListener('beforeunload', releaseCtrlOnUnload);

var pad=document.getElementById('pad'),statusEl=document.getElementById('status');
var SENS=2.2;
var pdx=0,pdy=0,sending=0;
var lx=0,ly=0,sx=0,sy=0,moved=0,st=0,lt=0;
var timer=null;
var dragTimer=null,inDragMode=false;
var MOVE_CANCEL_PX=12;

var twoFingerActive=false,twoFingerUsed=false,lastTwoY=0,scrollAccum=0;
var SCROLL_SENS=3,SCROLL_STEP=120;
var twoFingerMoved=false;
var twoFingerStartX=0,twoFingerStartY=0,twoFingerStartTime=0;
var TWO_FINGER_MOVE_PX=12;
var TWO_FINGER_TAP_MS=300;

var twoFingerPinching=false;
var twoFingerStartDist=0;
var lastPinchDist=0;
var PINCH_THRESHOLD=18;
var PINCH_STEP=22;
var twoFingerLastDir=0;

var threeFingerActive=false;
var threeFingerUsed=false;
var threeFingerMoved=false;
var threeFingerDir=0;
var threeFingerStartX=0,threeFingerStartY=0;
var threeFingerStartTime=0;
var THREE_FINGER_MOVE_PX=30;
var THREE_FINGER_TAP_MS=450;

/* 【新增】屏幕左右边缘单指上/下滑：持续滚动滚轮
   · 只在触摸板左右边缘 22px 内生效，触摸板中间的单指移动/点击/长按拖拽完全不变；
   · 单指上滑 = 滚轮向上，单指下滑 = 滚轮向下；
   · 越过 10px 后立刻滚一格，之后每 80ms 持续滚一格；
   · 滑动过程中改变方向（反向超过 10px）立刻改成反方向滚动滚轮；
   · 松手、取消、失焦或切到后台立即停止。 */
var EDGE_SCROLL_W=22;
var EDGE_SCROLL_TRIGGER_PX=10;
var EDGE_SCROLL_REVERSE_PX=10;
var EDGE_SCROLL_INTERVAL_MS=80;
var edgeScrollActive=false,edgeScrollDir=0,edgeScrollStartY=0,edgeScrollAnchorY=0,edgeScrollTimer=null;

function edgeScrollHit(t){
  var r=pad.getBoundingClientRect();
  return t.clientX<=r.left+EDGE_SCROLL_W || t.clientX>=r.right-EDGE_SCROLL_W;
}
function edgeScrollFire(){
  if(!edgeScrollDir) return;
  get('/mouse/scroll?delta='+(edgeScrollDir>0?120:-120));
}
function edgeScrollShow(){
  show(edgeScrollDir>0?L.scrollUp:L.scrollDown,1,0,0,1);
}
function edgeScrollBegin(t){
  edgeScrollActive=true;
  edgeScrollDir=0;
  edgeScrollStartY=t.clientY;
  edgeScrollAnchorY=t.clientY;
  cancelLongPress();
}
function edgeScrollStop(){
  if(edgeScrollTimer){ clearInterval(edgeScrollTimer); edgeScrollTimer=null; }
  edgeScrollActive=false;
  edgeScrollDir=0;
}
function edgeScrollUpdate(t){
  var y=t.clientY;
  if(!edgeScrollDir){
    var dy=y-edgeScrollStartY;
    if(Math.abs(dy)<EDGE_SCROLL_TRIGGER_PX) return;
    edgeScrollDir=dy<0?1:-1;         // 手指上滑 → 滚轮向上；手指下滑 → 滚轮向下
    edgeScrollAnchorY=y;
    edgeScrollFire();
    edgeScrollShow();
  } else {
    var step=y-edgeScrollAnchorY;
    if(Math.abs(step)<EDGE_SCROLL_REVERSE_PX) return;
    edgeScrollAnchorY=y;
    var dir=step<0?1:-1;
    if(dir!==edgeScrollDir){
      edgeScrollDir=dir;             // 滑动中改变方向：立刻反方向滚一格
      edgeScrollFire();
      edgeScrollShow();
    }
  }
  if(!edgeScrollTimer){
    edgeScrollTimer=setInterval(edgeScrollFire,EDGE_SCROLL_INTERVAL_MS);
  }
}
window.addEventListener('blur',edgeScrollStop);
document.addEventListener('visibilitychange',function(){
  if(document.visibilityState!=='visible') edgeScrollStop();
});

function distBetween(t0,t1){
  var dx=t0.clientX-t1.clientX;
  var dy=t0.clientY-t1.clientY;
  return Math.sqrt(dx*dx+dy*dy);
}

function show(t,ok,isDrag,isKb,isScroll,isRmenu,isDesktop,pill){
  statusEl.textContent=t;
  statusEl.classList.toggle('active',!!ok);
  statusEl.classList.toggle('drag',!!isDrag);
  statusEl.classList.toggle('kb',!!isKb);
  statusEl.classList.toggle('scroll',!!isScroll);
  statusEl.classList.toggle('rmenu',!!isRmenu);
  statusEl.classList.toggle('desktop',!!isDesktop);
  statusEl.classList.toggle('pill',!!pill);
  if(timer)clearTimeout(timer);
  timer=setTimeout(function(){
    statusEl.textContent=L.ready;
    statusEl.classList.remove('active');
    statusEl.classList.remove('drag');
    statusEl.classList.remove('kb');
    statusEl.classList.remove('scroll');
    statusEl.classList.remove('rmenu');
    statusEl.classList.remove('desktop');
    statusEl.classList.remove('pill');
  },900);
}
function get(u){return fetch(u,{cache:'no-store',keepalive:true}).catch(function(){});}
/* 【新增】单独包一层：只给截图那几处用，不去包装全局 fetch，别的手势逻辑一点不受影响 */
var __micShotFetch = (function(){
  var f = (typeof window.fetch === 'function') ? window.fetch.bind(window) : null;
  return function(u, o){
    if (f) return f(u, o);
    if (window.fetch) return window.fetch(u, o);
    return Promise.reject(new Error('No fetch'));
  };
})();
function flush(){
  if(sending||(!pdx&&!pdy))return;
  var dx=pdx,dy=pdy;pdx=0;pdy=0;sending=1;
  fetch('/mouse/move?dx='+dx+'&dy='+dy,{cache:'no-store'})
    .catch(function(){})
    .then(function(){sending=0;if(pdx||pdy)flush();});
}
function cancelLongPress(){if(dragTimer){clearTimeout(dragTimer);dragTimer=null;}}
function cancelDrag(){cancelLongPress();if(inDragMode){inDragMode=false;get('/mouse/click?btn=left_up');}}

function toggleCtrlLock(){
  var next = !ctrlLocked;
  get('/keyboard/special?k=' + (next ? 'ctrl_down' : 'ctrl_up'));
  setCtrlBadge(next);
  if(next) show(L.ctrlLocked,1,0,1,0,0,0,1);
  else     show(L.ctrlRel,   1,0,1,0,0,0,1);
}

var ctrlBtnGuard = 0;
ctrlBtnEl.addEventListener('touchstart', function(e){
  e.preventDefault(); e.stopPropagation();
  ctrlBtnGuard = Date.now();
  toggleCtrlLock();
}, {passive:false});
ctrlBtnEl.addEventListener('click', function(e){
  if(Date.now() - ctrlBtnGuard < 800) return;
  e.preventDefault();
  toggleCtrlLock();
});

/* ============================================================
 * 【新增】剪贴板面板
 * ============================================================ */
var clipBtnEl   = document.getElementById('clipBtn');
var clipPanel   = document.getElementById('clipPanel');
var clipBody    = document.getElementById('clipBody');
var clipCloseEl = document.getElementById('clipCloseBtn');
var clipTitleEl = document.getElementById('clipTitleText');

function escHtml(s){
  return String(s).replace(/[&<>"']/g, function(c){
    return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];
  });
}
function fmtSize(n){
  if (!n && n !== 0) return '';
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n/1024).toFixed(1) + ' KB';
  if (n < 1073741824) return (n/1048576).toFixed(1) + ' MB';
  return (n/1073741824).toFixed(2) + ' GB';
}
function fallbackCopy(text, cb){
  try{
    var ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.top = '0'; ta.style.left = '0';
    ta.style.opacity = '0';
    ta.style.fontSize = '16px';
    document.body.appendChild(ta);
    ta.focus(); ta.select();
    try{ document.execCommand('copy'); }catch(e){}
    document.body.removeChild(ta);
  }catch(e){}
  if (cb) cb();
}

/* ============================================================
 * 【修复】0 字节文件：复制文件名前缀到手机剪贴板
 * ------------------------------------------------------------
 *  上一版没复制成功的原因：用的是一个 1px×1px、还带 readonly 的
 *  隐藏 textarea。iOS 上这种“退化尺寸 + 只读”的框往往选不中任何
 *  文字，execCommand('copy') 却照样返回 true —— 于是按钮显示
 *  “已复制”，剪贴板里其实什么都没有。
 *
 *  这一版的做法：
 *   1) 用正常尺寸、可编辑（inputmode=none 防键盘）的 textarea，
 *      focus → select → setSelectionRange，落到用户手势里同步执行；
 *   2) 复制后**回读 selectionStart/End 校验真的选中了全文**，
 *      选中长度为 0 一律判定为失败（不再假成功）；
 *   3) 选中失败再走 contenteditable + Range 的老 iOS 方案；
 *   4) 全部失败 → 在面板里弹出一个真正可长按拷贝的输入框兜底。
 * ============================================================ */
function filePrefix(name){
  var s = String(name == null ? '' : name);
  var dot = s.lastIndexOf('.');
  /* dot > 0：隐藏文件（.gitignore 之类）保留原名，"archive.tar.gz" 只去掉 .gz */
  if (dot > 0) {
    var stem = s.substring(0, dot);
    if (stem.length > 0) return stem;
  }
  return s;
}

/* 方案一：正常尺寸的 textarea（这是 iOS/Android 上最通用的写法） */
function copyByTextarea(text){
  var ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('inputmode', 'none');   /* iOS 上不出键盘 */
  ta.setAttribute('autocomplete', 'off');
  ta.setAttribute('autocorrect', 'off');
  ta.setAttribute('autocapitalize', 'off');
  ta.setAttribute('spellcheck', 'false');
  ta.style.position = 'fixed';
  ta.style.top = '0';
  ta.style.left = '0';
  ta.style.width = '20em';
  ta.style.height = '2em';
  ta.style.padding = '0';
  ta.style.margin = '0';
  ta.style.border = '0';
  ta.style.outline = 'none';
  ta.style.background = 'transparent';
  ta.style.color = 'transparent';
  ta.style.fontSize = '16px';   /* >=16px，iOS 聚焦时不会放大页面 */
  ta.style.opacity = '0.01';    /* 不能用 0 / display:none，否则选不中 */
  ta.style.zIndex = '2147483647';
  document.body.appendChild(ta);
  var ok = false, selected = 0;
  try{
    try{ ta.focus({ preventScroll: true }); }catch(e){ ta.focus(); }
    try{ ta.select(); }catch(e){}
    try{ ta.setSelectionRange(0, text.length); }catch(e){}
    try{ selected = ta.selectionEnd - ta.selectionStart; }catch(e){ selected = 0; }
    try{ ok = document.execCommand('copy'); }catch(e){ ok = false; }
  }catch(e){ ok = false; }
  try{ document.body.removeChild(ta); }catch(e){}
  /* 关键校验：没选中任何文字就当失败，绝不假报成功 */
  return ok && selected === text.length && text.length > 0;
}

/* 方案二：contenteditable + Range（老 iOS Safari 的经典写法） */
function copyByEditable(text){
  var d = document.createElement('div');
  d.contentEditable = 'true';
  d.style.position = 'fixed';
  d.style.top = '0';
  d.style.left = '0';
  d.style.width = '20em';
  d.style.height = '2em';
  d.style.overflow = 'hidden';
  d.style.whiteSpace = 'pre';
  d.style.fontSize = '16px';
  d.style.opacity = '0.01';
  d.style.zIndex = '2147483647';
  d.textContent = text;
  document.body.appendChild(d);
  var ok = false, selected = 0;
  try{
    try{ d.focus({ preventScroll: true }); }catch(e){ d.focus(); }
    var range = document.createRange();
    range.selectNodeContents(d);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
    selected = String(sel).length;
    try{ ok = document.execCommand('copy'); }catch(e){ ok = false; }
  }catch(e){ ok = false; }
  try{ window.getSelection().removeAllRanges(); }catch(e){}
  try{ document.body.removeChild(d); }catch(e){}
  return ok && selected === text.length && text.length > 0;
}

function copyToPhone(text, cb){
  var done = function(ok){ if (cb) cb(!!ok); };
  var ok = false;
  /* 1) 点击手势里同步执行（http 局域网页面唯一可靠的路径） */
  try{ ok = copyByTextarea(text); }catch(e){ ok = false; }
  if (ok) { done(true); return; }
  /* 2) contenteditable + Range 兜底 */
  try{ ok = copyByEditable(text); }catch(e){ ok = false; }
  if (ok) { done(true); return; }
  /* 3) 异步 Clipboard API（localhost 才存在） */
  try{
    if (navigator.clipboard && navigator.clipboard.writeText){
      navigator.clipboard.writeText(text)
        .then(function(){ done(true); })
        .catch(function(){ done(false); });
      return;
    }
  }catch(e){}
  done(false);
}

/* 自动复制被系统拦下时的手动兜底：给一个真正可长按拷贝的输入框 */
function showCopyFallback(text){
  var old = document.getElementById('clipCopyPop');
  if (old && old.parentNode) old.parentNode.removeChild(old);

  var pop = document.createElement('div');
  pop.id = 'clipCopyPop';
  pop.className = 'clip-copy-pop';
  pop.innerHTML =
    '<input class="ccp-input" id="ccpInput" type="text" readonly autocomplete="off" spellcheck="false">' +
    '<div class="ccp-hint">' + escHtml(L.clipCopyManual) + '</div>' +
    '<button class="clip-action" id="ccpCloseBtn">' + escHtml(L.clipCopyManualClose) + '</button>';

  var list = document.getElementById('clipFileList');
  if (list && list.parentNode) list.parentNode.insertBefore(pop, list);
  else clipBody.appendChild(pop);

  var inp = document.getElementById('ccpInput');
  if (inp){
    inp.value = text;
    try{ inp.focus({ preventScroll: true }); }catch(e){}
    try{ inp.select(); }catch(e){}
    try{ inp.setSelectionRange(0, text.length); }catch(e){}
  }
  var btn = document.getElementById('ccpCloseBtn');
  if (btn){
    btn.addEventListener('click', function(e){
      e.preventDefault();
      e.stopPropagation();
      if (pop.parentNode) pop.parentNode.removeChild(pop);
    });
  }
}

/* ============================================================
 * 【修复】下载：0 字节空文件在 iOS Safari 上永远转圈
 * ------------------------------------------------------------
 *  现象: 复制一个 0 字节的文件到电脑剪贴板 → 手机上点“下载”，
 *        只得到一个 0 字节的空文件，下载列表里一直转圈。
 *  原因: 服务端对空文件只能回 “Content-Length: 0” 的响应，
 *        iOS 下载管理器把这种响应当成“没有内容”，既不落盘也
 *        不结束，于是永远停在“0 字节 / 下载中”。
 *  方案: 0 字节的文件根本不需要联网 —— 在手机上用 Blob 直接
 *        造出来；整个过程同步执行，不会丢掉用户手势（先 fetch
 *        再下载就会因为异步丢手势而被 Safari 静默拦截）。
 *        不支持 Blob 下载的旧系统，回落到服务端下载，服务端对
 *        空文件改用 chunked 传输，避开 “Content-Length: 0”。
 *        非空文件仍走服务端直传，且同样在脚本里直接发起。
 * ============================================================ */

function isIOS(){
  var ua = navigator.userAgent || '';
  if (/iPad|iPhone|iPod/.test(ua)) return true;
  /* iPadOS 13+ 默认把自己伪装成 macOS */
  return (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
}

/* 在手机上直接生成 0 字节空文件，完全不经过服务器 */
function downloadEmptyBlob(filename){
  try{
    if (!window.Blob || !window.URL || !URL.createObjectURL) return false;
    var blob = new Blob([new Uint8Array(0)], {type:'application/octet-stream'});
    var url  = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.setAttribute('download', filename || 'download');
    a.style.display = 'none';
    document.body.appendChild(a);
    try { a.click(); } catch(e){}
    setTimeout(function(){
      try { document.body.removeChild(a); } catch(e){}
      try { URL.revokeObjectURL(url); } catch(e){}
    }, 5000);
    return true;
  }catch(e){
    return false;
  }
}

/* 服务端直传：在用户手势里同步触发，iOS 才会真正交给下载管理器 */
function nativeDownload(url, filename){
  var a = document.createElement('a');
  a.href = url;
  a.setAttribute('download', filename || 'download');
  a.style.display = 'none';
  document.body.appendChild(a);
  try { a.click(); } catch(e){}
  setTimeout(function(){
    try { document.body.removeChild(a); } catch(e){}
  }, 1500);
}

/* size === 0 走手机本地生成，其余走服务端直传 */
function downloadClipFile(url, filename, size){
  var fname = filename || 'download';
  if (size === 0 && downloadEmptyBlob(fname)) return;
  nativeDownload(url, fname);
}

/* 【新增】电脑剪贴板内容实时刷新 ------------------------------------------
   面板打开期间每隔 CLIP_POLL_MS 拉一次 /clipboard/version（只是一个数字，
   很便宜）；版本号变了才去拉完整的 /clipboard/info 重绘。
   于是电脑上复制 / 剪切任何东西，手机这个窗口里的内容会自动跟着变，
   不用关掉重开；版本号没变就不碰 DOM，不会打断用户正在点的按钮。 */
var clipPollTimer = null;
var clipShownVer   = -1;     /* 当前面板里已经画出来的剪贴板版本号 */
var CLIP_POLL_MS   = 600;

/* 兜底复制框（长按拷贝）弹着的时候先别重绘，否则会把那行文字刷掉 */
function clipPanelBusy(){
  return !!document.getElementById('clipCopyPop');
}

function scheduleClipPoll(){
  if (clipPollTimer){ clearTimeout(clipPollTimer); clipPollTimer = null; }
  clipPollTimer = setTimeout(pollClipPanel, CLIP_POLL_MS);
}

/* 上一轮回来之后才排下一轮，慢网 / 断网都不会把请求堆起来 */
function pollClipPanel(){
  clipPollTimer = null;
  if (!clipPanel.classList.contains('show')) return;
  fetch('/clipboard/version', {cache:'no-store'})
    .then(function(r){ return r.json(); })
    .then(function(v){
      if (!v || v.version === clipShownVer) return;
      return fetch('/clipboard/info', {cache:'no-store'})
        .then(function(r){ return r.json(); })
        .then(function(info){
          if (!info || !clipPanel.classList.contains('show')) return;
          if (info.version === clipShownVer) return;
          if (clipPanelBusy()) return;
          renderClipInfo(info);
        });
    })
    .catch(function(){})
    .then(function(){
      if (!document.hidden && clipPanel.classList.contains('show')) scheduleClipPoll();
    });
}

function startClipPoll(){
  stopClipPoll();
  scheduleClipPoll();
}

function stopClipPoll(){
  if (clipPollTimer){ clearTimeout(clipPollTimer); clipPollTimer = null; }
}

/* 手机锁屏 / 切到后台时停掉轮询，回到页面接着刷 */
document.addEventListener('visibilitychange', function(){
  if (document.hidden) stopClipPoll();
  else if (clipPanel.classList.contains('show')) startClipPoll();
});

function openClipPanel(){
  clipPanel.classList.add('show');
  clipBtnEl.classList.add('on');
  clipShownVer = -1;
  clipBody.innerHTML = '<div class="clip-loading">' + escHtml(L.clipLoading) + '</div>';
  fetch('/clipboard/info', {cache:'no-store'})
    .then(function(r){ return r.json(); })
    .then(function(info){ renderClipInfo(info); })
    .catch(function(){
      clipBody.innerHTML = '<div class="clip-empty" style="color:#ef4444">' + escHtml(L.clipError) + '</div>';
    });
  startClipPoll();
}
function closeClipPanel(){
  stopClipPoll();
  clipPanel.classList.remove('show');
  clipBtnEl.classList.remove('on');
  if (textWin) hideClipCompose();   /* 【改动】面板关掉时，新建文本窗口一起收掉 */
}

/* 重新拉一次电脑剪贴板状态（上传完 / 发送完文字后用） */
function refreshClipPanel(){
  if (!clipPanel.classList.contains('show')) return;
  fetch('/clipboard/info', {cache:'no-store'})
    .then(function(r){ return r.json(); })
    .then(function(info){ renderClipInfo(info); })
    .catch(function(){});
}

/* ============================================================
 * 【新增】手机 → 电脑：上传文件/照片/视频到电脑剪贴板
 * ------------------------------------------------------------
 *  走 POST /clipboard/upload?name=<文件名>&new=1|0，请求体就是文件原字节。
 *  电脑端把文件落到临时目录，然后把这些文件设置成电脑的
 *  「文件剪贴板」（CF_HDROP），电脑上直接 Ctrl+V 就能粘贴到
 *  资源管理器 / 微信 / 邮件等地方。
 *  用 XHR 而不是 fetch：只有 XHR 能拿到上传进度（视频文件很大）。
 * ============================================================ */
var clipUploadBtn  = document.getElementById('clipUploadBtn');
var clipTextBtn    = document.getElementById('clipTextBtn');
var clipFileInput  = document.getElementById('clipFileInput');
var clipStatusEl   = document.getElementById('clipUploadStatus');
var clipCompose    = document.getElementById('clipCompose');
var clipComposeIn  = document.getElementById('clipComposeInput');
var clipComposeSendEl   = document.getElementById('clipComposeSend');
var clipComposeCancelEl = document.getElementById('clipComposeCancel');

function setClipStatus(text, isErr){
  if (!clipStatusEl) return;
  clipStatusEl.textContent = text || '';
  if (isErr) clipStatusEl.classList.add('err');
  else clipStatusEl.classList.remove('err');
}

var clipUploadBatch = 0;

function uploadOneFile(file, isFirst, onProgress, onDone){
  var url = '/clipboard/upload?name=' + encodeURIComponent(file.name || 'file') +
            (isFirst ? '&new=1' : '');
  var finished = false;
  var finish = function(ok){
    if (finished) return;
    finished = true;
    onDone(ok);
  };
  try{
    var xhr = new XMLHttpRequest();
    xhr.open('POST', url, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = function(e){
      if (e.lengthComputable && onProgress) {
        onProgress(Math.round(e.loaded * 100 / e.total));
      }
    };
    xhr.onload  = function(){ finish(xhr.status === 200); };
    xhr.onerror = function(){ finish(false); };
    xhr.ontimeout = function(){ finish(false); };
    xhr.onabort = function(){ finish(false); };
    xhr.send(file);
  }catch(e){
    finish(false);
  }
}

function uploadFiles(fileList){
  var arr = [];
  for (var i = 0; i < fileList.length; ++i) arr.push(fileList[i]);
  if (!arr.length) return;
  clipUploadBatch++;
  var total = arr.length, done = 0, failed = 0;
  var step = function(){
    if (done >= total){
      if (failed >= total) {
        setClipStatus(L.clipUploadNoNet, true);
      } else {
        var msg = L.clipUploadDone.replace('{n}', String(total - failed));
        if (failed > 0) msg += '（' + failed + ' ' + L.clipUploadFail + '）';
        setClipStatus(msg, false);
      }
      refreshClipPanel();
      setTimeout(function(){ setClipStatus('', false); }, 5000);
      return;
    }
    var f = arr[done];
    var human = f.name + '（' + fmtSize(f.size) + '）';
    setClipStatus(L.clipUploading + ' ' + (done + 1) + '/' + total + '  ' + human, false);
    uploadOneFile(f, done === 0,
      function(pct){
        setClipStatus(L.clipUploading + ' ' + (done + 1) + '/' + total +
                      '  ' + pct + '%  ' + human, false);
      },
      function(ok){
        if (!ok) failed++;
        done++;
        step();
      });
  };
  step();
}

if (clipUploadBtn && clipFileInput){
  clipUploadBtn.addEventListener('click', function(e){
    e.preventDefault();
    e.stopPropagation();
    /* iOS/Android 都要求由用户手势直接触发 input，不能异步再点 */
    try{ clipFileInput.click(); }catch(err){}
  });
  clipFileInput.addEventListener('change', function(){
    if (clipFileInput.files && clipFileInput.files.length) {
      uploadFiles(clipFileInput.files);
    }
    /* 清空，方便再次选同一个文件 */
    try{ clipFileInput.value = ''; }catch(err){}
  });
}

/* ============================================================
 * 【新增】新建文本 → 直接写进电脑剪贴板
 * ============================================================ */

/* 【新增】打开新建文本时，如果手机剪贴板里已经有文本，直接填进框里。
 *   本页是局域网 http 地址，iOS Safari / 微信里 navigator.clipboard 不存在，
 *   【改动】这种情况下不再弹“手机浏览器不允许……”的提示，静默保持空框。 */
var clipComposeAutoText = '';   // 记录上一次自动填入的内容，便于覆盖

function readPhoneClipboard(cb){
  try{
    if (navigator.clipboard && navigator.clipboard.readText){
      navigator.clipboard.readText()
        .then(function(t){ cb(typeof t === 'string' ? t : null); })
        .catch(function(){ cb(null); });
      return;
    }
  }catch(e){}
  cb(null);
}

/* 老接口兜底：极少数安卓 WebView 仍允许把剪贴板内容直接粘进当前聚焦的编辑框。
   成功时文字已经在框里，不需要我们再去读。 */
function tryLegacyPasteInsert(){
  try{
    if (!document.execCommand) return false;
    var ok = false;
    try{ ok = document.execCommand('paste'); }catch(e){ return false; }
    if (!ok) return false;
    var v = clipComposeIn ? clipComposeIn.value : '';
    if (v){
      clipComposeAutoText = v;
      return true;
    }
  }catch(e){}
  return false;
}

/* ============================================================
 * 【改动】新建文本：点按钮时“新创建一个窗口”，并强制这个窗口马上出现、贴在最顶部
 * ------------------------------------------------------------
 *  不再借用剪贴板面板里的那块位置，而是现场建一个独立的 #textWin 窗口：
 *    · 出现顺序：先同步“创建 + 显示 + 置顶”，再在同一个点击手势里聚焦（第一帧就可见），
 *      读剪贴板 / 粘贴推迟到画过一帧之后 —— 需要粘贴的那一次也不会挡住窗口出现；
 *    · 层级：z-index:2147483647，比剪贴板面板(10000)更高，永远压在最上层；
 *    · 位置：固定在屏幕最顶部（有输入法时按 visualViewport 贴可见区顶部），
 *      输入法弹出 / 收起 / 视口滚动 / 旋转都立刻重贴一次，绝不被键盘盖住；
 *    · 窗口里的输入区沿用原来的 #clipCompose（自动填入手机剪贴板文本、
 *      发送到电脑剪贴板、取消等逻辑全部复用，不重复实现）。
 * ============================================================ */
var textWin    = null;      /* 窗口本身（第一次点“新建文本”时创建） */
var textWinBox = null;      /* 窗口里承载输入区的容器 */

function positionTextWin(){
  if (!textWin) return;
  var vv = window.visualViewport;
  if (vv){
    textWin.style.top   = vv.offsetTop  + 'px';
    textWin.style.left  = vv.offsetLeft + 'px';
    textWin.style.width = vv.width + 'px';
  } else {
    textWin.style.top   = '0px';        /* 老浏览器：直接贴屏幕最顶上 */
    textWin.style.left  = '0px';
    textWin.style.width = '100%';
  }
}

/* 点“新建文本”：创建窗口（只建一次，之后复用）→ 把输入区搬进去 → 强制置顶 → 聚焦 */
function openTextWin(){
  if (!textWin){
    textWin = document.createElement('div');
    textWin.id = 'textWin';
    textWinBox = document.createElement('div');
    textWinBox.className = 'text-win-box';
    textWin.appendChild(textWinBox);
    document.body.appendChild(textWin);
  }
  if (clipCompose && clipCompose.parentNode !== textWinBox){
    textWinBox.appendChild(clipCompose);      /* 输入区搬进新窗口 */
  }

  /* ① 先“出现 + 置顶”：全部同步，同一个点击事件里就画出来，不等任何异步操作 */
  textWin.classList.add('show');
  positionTextWin();
  try{ void textWin.offsetHeight; }catch(e){}  /* 逼浏览器立刻布局，保证这一帧就可见 */

  /* ② 还在点击手势里，赶紧聚焦，键盘才会自动弹（拖到异步里 iOS 就不弹了） */
  showClipCompose();
  positionTextWin();

  /* ③ 下一帧 / 双 rAF 再钉一次，防止输入法动画或浏览器滚动把窗口带偏 */
  setTimeout(positionTextWin, 0);
  if (window.requestAnimationFrame){
    requestAnimationFrame(function(){
      positionTextWin();
      requestAnimationFrame(positionTextWin);
    });
  }

  /* ④ 读剪贴板 / 老接口粘贴放到“画过一帧之后”再执行：它可能同步阻塞、还会弹权限框，
        绝不能挡在窗口出现之前 —— 有内容要粘贴的情况也一样，窗口先出现且置顶，再粘贴 */
  var doPrefill = function(){
    prefillClipCompose();
    positionTextWin();                       /* 粘贴/填入之后再钉一次 */
  };
  if (window.requestAnimationFrame){
    requestAnimationFrame(function(){ setTimeout(doPrefill, 0); });
  } else {
    setTimeout(doPrefill, 32);
  }

  setTimeout(positionTextWin, 250);           /* 输入法弹出动画结束后再兜一次底 */
}

/* 输入法弹出 / 收起、视口滚动、屏幕旋转：窗口开着就立刻重贴到顶部 */
if (window.visualViewport){
  window.visualViewport.addEventListener('resize', positionTextWin);
  window.visualViewport.addEventListener('scroll', positionTextWin);
}
window.addEventListener('resize', positionTextWin);
window.addEventListener('orientationchange', positionTextWin);

/* 【改动】显示输入区 + 聚焦：只有同步动作（不碰剪贴板），保证窗口先出来、键盘马上弹 */
function showClipCompose(){
  if (!clipCompose) return;
  clipCompose.classList.add('show');
  setClipStatus('', false);
  focusComposeInput();
}
/* 聚焦输入框：preventScroll 防止浏览器为了“把输入框滚进视野”而挪动窗口，
   老写法会把已经贴顶的窗口往上带，看起来像“窗口没出现 / 跑到上面去了” */
function focusComposeInput(){
  try{ clipComposeIn.focus({preventScroll:true}); }
  catch(e){ try{ clipComposeIn.focus(); }catch(e2){} }
}
/* 【改动】粘贴 / 自动填入单独一个函数，由 openTextWin 推迟到窗口出现之后再调：
   老 WebView 里 document.execCommand('paste') 是同步阻塞、还会弹权限框，
   以前它卡在“窗口出现”之前，所以看起来窗口出现得慢、位置也不对。 */
function prefillClipCompose(){
  readPhoneClipboard(function(text){
    if (text === null || typeof text === 'undefined'){
      /* 再给老接口一次机会（成功的话文字已经直接进了框） */
      if (tryLegacyPasteInsert()){
        setClipStatus(L.clipPasteFilled, false);
        positionTextWin();                       /* 粘贴完再钉一次，窗口不会跑位 */
        setTimeout(function(){ setClipStatus('', false); }, 4000);
        return;
      }
      /* 【改动】浏览器不允许网页读剪贴板时不再弹任何提示，安静返回 */
      positionTextWin();
      return;
    }
    if (!text) return;                       // 剪贴板里没有文本，保持空框
    var cur = clipComposeIn ? clipComposeIn.value : '';
    /* 只在“还没打字”或“上次就是自动填的”时覆盖，避免冲掉用户正在写的内容 */
    if (cur === '' || cur === clipComposeAutoText){
      clipComposeIn.value = text;
      clipComposeAutoText = text;
      try{
        focusComposeInput();
        clipComposeIn.setSelectionRange(text.length, text.length);
      }catch(e){}
      setClipStatus(L.clipPasteFilled, false);
      positionTextWin();                       /* 填完再钉一次 */
      setTimeout(function(){ setClipStatus('', false); }, 4000);
    }
  });
}
function hideClipCompose(){
  if (!clipCompose) return;
  clipCompose.classList.remove('show');
  if (textWin) textWin.classList.remove('show');   /* 【改动】收起输入区 = 关掉这个独立窗口 */
}
function sendClipText(){
  var t = clipComposeIn ? clipComposeIn.value : '';
  if (!t) { try{ clipComposeIn.focus(); }catch(e){} return; }
  setClipStatus(L.clipSend + '…', false);
  fetch('/clipboard/settext', {
    method: 'POST',
    headers: {'Content-Type': 'text/plain; charset=utf-8'},
    body: t
  })
    .then(function(r){ return r.ok; })
    .then(function(ok){
      if (!ok) { setClipStatus(L.clipUploadNoNet, true); return; }
      clipComposeIn.value = '';
      clipComposeAutoText = '';
      hideClipCompose();
      setClipStatus(L.clipSent, false);
      refreshClipPanel();
      setTimeout(function(){ setClipStatus('', false); }, 4000);
    })
    .catch(function(){ setClipStatus(L.clipUploadNoNet, true); });
}

if (clipTextBtn){
  clipTextBtn.addEventListener('click', function(e){
    e.preventDefault(); e.stopPropagation();
    openTextWin();          /* 【改动】每次点都新开窗口，并强制贴到屏幕最顶部 */
  });
}
if (clipComposeSendEl){
  clipComposeSendEl.addEventListener('click', function(e){
    e.preventDefault(); e.stopPropagation();
    sendClipText();
  });
}
if (clipComposeCancelEl){
  clipComposeCancelEl.addEventListener('click', function(e){
    e.preventDefault(); e.stopPropagation();
    hideClipCompose();
  });
}

function renderClipInfo(info){
  /* 记下已经画出来的版本号，轮询据此判断“电脑剪贴板是不是真的变了” */
  if (info && typeof info.version === 'number') clipShownVer = info.version;
  clipTitleEl.textContent = L.clipTitle;
  if (!info || info.type === 'empty'){
    clipBody.innerHTML = '<div class="clip-empty">' + escHtml(L.clipEmpty) + '</div>';
    return;
  }
  var badgeHtml = info.isCut
    ? '<span class="clip-badge cut">' + escHtml(L.clipCut) + '</span>'
    : '<span class="clip-badge">' + escHtml(L.clipCopyLabel) + '</span>';
  clipTitleEl.innerHTML = '<span>' + escHtml(L.clipTitle) + '</span>' + badgeHtml;

  if (info.type === 'text'){
    clipBody.innerHTML =
      '<div class="clip-text-area" id="clipTextBody"></div>' +
      '<button class="clip-action" id="clipCopyBtn">' + escHtml(L.clipCopyToPhone) + '</button>';

    fetch('/clipboard/text', {cache:'no-store'})
      .then(function(r){ return r.text(); })
      .then(function(t){
        var box = document.getElementById('clipTextBody');
        if (box) box.textContent = t;
        var btn = document.getElementById('clipCopyBtn');
        if (!btn) return;
        var orig = L.clipCopyToPhone;
        btn.addEventListener('click', function(){
          var done = function(){
            btn.classList.add('ok');
            btn.textContent = L.clipCopied;
            setTimeout(function(){
              btn.classList.remove('ok');
              btn.textContent = orig;
            }, 1400);
          };
          if (navigator.clipboard && navigator.clipboard.writeText){
            navigator.clipboard.writeText(t).then(done).catch(function(){ fallbackCopy(t, done); });
          } else {
            fallbackCopy(t, done);
          }
        });
      })
      .catch(function(){
        var box = document.getElementById('clipTextBody');
        if (box) box.textContent = L.clipError;
      });
    return;
  }

  if (info.type === 'files'){
    var filesArr = info.files || [];
    var hasFile = false, hasDir = false, hasTooBig = false;
    var html = '<ul class="clip-file-list" id="clipFileList">';
    for (var i = 0; i < filesArr.length; ++i){
      var f = filesArr[i];
      var fsz = (typeof f.size === 'number') ? f.size : -1;
      var rowBtn = '';
      if (f.dir){
        /* 【新增】文件夹：电脑端会实时打成 zip，手机点一下就能拿到整个文件夹 */
        hasDir = true;
        if (f.tooBig){
          hasTooBig = true;
        } else {
          rowBtn = '<span class="dl" data-idx="' + i + '" data-zip="1">' +
                   escHtml(L.clipZip) + '</span>';
        }
      } else {
        hasFile = true;
        if (fsz === 0){
          /* 【新增】0 字节文件：不再给“下载”（它早就在手机上生成过了），
             改成“复制名称”，点一下就把文件名前缀复制到手机剪贴板 */
          rowBtn = '<span class="cp" data-copy="' + i + '">' +
                   escHtml(L.clipCopyName) + '</span>';
        } else {
          rowBtn = '<span class="dl" data-idx="' + i + '">' + escHtml(L.clipDownload) + '</span>';
        }
      }
      var sizeStr;
      if (f.dir){
        var cnt = (typeof f.count === 'number' && f.count > 0)
                ? (f.count + ' ' + L.clipItemWord + ' · ') : '';
        sizeStr = '📁 ' + cnt + (fsz > 0 ? fmtSize(fsz) : '');
      } else {
        sizeStr = fmtSize(f.size);
      }
      html += '<li>' +
        '<span class="fname" title="' + escHtml(f.name) + '">' + escHtml(f.name) + '</span>' +
        '<span class="fsize">' + escHtml(sizeStr) + '</span>' +
        rowBtn +
        '</li>';
    }
    html += '</ul>';
    if (hasTooBig){
      html += '<div class="clip-hint">' + escHtml(L.clipZipTooBig) + '</div>';
    } else if (hasDir){
      html += '<div class="clip-hint">' + escHtml(L.clipZipTip) + '</div>';
    }
    /* 打包按钮只在“确实能打包”时出现：太大时点了也只会拿到 413 */
    if (hasDir && !hasTooBig){
      html += '<button class="clip-action" id="clipDlZipAllBtn">' + escHtml(L.clipZipAll) + '</button>';
    } else if (hasFile){
      html += '<button class="clip-action" id="clipDlAllBtn">' + escHtml(L.clipDownloadAll) + '</button>';
    }
    clipBody.innerHTML = html;

    /* 列表用事件委托：一次绑定，任何一行下载都走同一条可靠路径 */
    var fileList = document.getElementById('clipFileList');
    if (fileList){
      fileList.addEventListener('click', function(ev){
        var t = ev.target;
        while (t && t !== fileList &&
               !(t.classList && (t.classList.contains('dl') || t.classList.contains('cp'))))
          t = t.parentNode;
        if (!t || t === fileList) return;
        ev.preventDefault();

        /* 0 字节文件：复制文件名前缀（去掉最后一个扩展名） */
        if (t.getAttribute('data-copy') !== null){
          var ci = parseInt(t.getAttribute('data-copy'), 10);
          if (isNaN(ci)) return;
          var cf   = filesArr[ci] || {};
          var stem = filePrefix(cf.name);
          var cell = t;
          copyToPhone(stem, function(ok){
            if (ok){
              cell.classList.remove('err');
              cell.classList.add('ok');
              cell.textContent = L.clipCopied;
              setTimeout(function(){
                cell.classList.remove('ok');
                cell.textContent = L.clipCopyName;
              }, 1300);
            } else {
              /* 系统拦住了自动复制：弹出一个真正能长按拷贝的输入框 */
              cell.classList.remove('ok');
              cell.classList.remove('err');
              cell.textContent = L.clipCopyName;
              showCopyFallback(stem);
            }
          });
          return;
        }

        var idx = parseInt(t.getAttribute('data-idx'), 10);
        if (isNaN(idx)) return;
        var file = filesArr[idx] || {};
        /* 文件夹走 zip：绝不能再用 size===0 的本地空文件分支 */
        if (t.getAttribute('data-zip') === '1'){
          nativeDownload('/clipboard/file?i=' + idx, (file.name || 'folder') + '.zip');
          return;
        }
        var fsz  = (typeof file.size === 'number') ? file.size : -1;
        downloadClipFile('/clipboard/file?i=' + idx, file.name || 'download', fsz);
      });
    }

    var dlZipAll = document.getElementById('clipDlZipAllBtn');
    if (dlZipAll){
      /* 一次请求、一个压缩包：比连续触发多个下载在 iOS 上可靠得多 */
      dlZipAll.addEventListener('click', function(){
        nativeDownload('/clipboard/all', L.clipZipFileName);
      });
    }

    var dlAll = document.getElementById('clipDlAllBtn');
    if (dlAll){
      dlAll.addEventListener('click', function(){
        var delay = 0;
        for (var j = 0; j < filesArr.length; ++j){
          var f2 = filesArr[j];
          if (f2.dir) continue;
          var fsz2 = (typeof f2.size === 'number') ? f2.size : -1;
          (function(name, idx, size, wait){
            setTimeout(function(){
              downloadClipFile('/clipboard/file?i=' + idx, name, size);
            }, wait);
          })(f2.name || 'download', j, fsz2, delay);
          delay += 350;
        }
      });
    }
    return;
  }

  clipBody.innerHTML = '<div class="clip-empty">' + escHtml(L.clipError) + '</div>';
}

/* 【新增】按住剪贴板图标，或按住触摸板底部那条分隔线，左右滑动：
   光标逐字前/后移动（低灵敏度：手指移动一段距离才移一个字）
   · 轻点剪贴板图标仍然打开电脑剪贴板面板，和原来一样；
   · 按住后先横向移动 26px 才算开始，之后每横向移动 20px 光标移一个字；
   · 向左 = 光标逐字往前（VK_LEFT），向右 = 光标逐字往后（VK_RIGHT）；
   · 滑动过程中改变方向（反向超过 10px），立刻改成朝相反方向逐字移动；
   · 松手、取消、失焦或切后台立即停止；滑动结束不会误开剪贴板面板。 */
var CURSOR_DRAG_TRIGGER_PX=26;   /* 起步死区：按住后移动这么多像素才开始 */
var CURSOR_DRAG_STEP_PX=20;      /* 灵敏度：每移动这么多像素光标才移一个字 */
var CURSOR_REVERSE_PX=10;        /* 反向超过这个距离才切换方向，避免手指抖动 */
var cursorKeyDir=0;

function cursorKeyTap(dir){
  cursorKeyDir=dir;
  get('/keyboard/special?k='+(dir<0?'left':'right'));
  show(dir<0?'←':'→',1,0,0,0,0,0,1);
}
function cursorKeyStop(){
  cursorKeyDir=0;
}

/* ---------- 剪贴板图标 ---------- */
var clipTouchActive=false,clipSliding=false,clipTouchStartX=0,clipLastX=0,clipAccum=0,clipAccumDir=0;
function clipCursorUpdate(x){
  if(!clipSliding){
    if(Math.abs(x-clipTouchStartX)<CURSOR_DRAG_TRIGGER_PX){ clipLastX=x; return; }
    clipSliding=true;
    clipLastX=x;
    clipAccum=0;
    clipAccumDir=(x<clipTouchStartX)?-1:1;
    cursorKeyTap(clipAccumDir);   /* 起步先移一个字，之后按距离逐字移动 */
    return;
  }
  var dx=x-clipLastX;
  clipLastX=x;
  if(!dx) return;
  var dir=dx<0?-1:1;
  if(dir!==clipAccumDir){         /* 改变方向：立即改成相反方向逐字移动 */
    clipAccumDir=dir;
    clipAccum=CURSOR_DRAG_STEP_PX-CURSOR_REVERSE_PX;
  }
  clipAccum+=Math.abs(dx);
  while(clipAccum>=CURSOR_DRAG_STEP_PX){
    clipAccum-=CURSOR_DRAG_STEP_PX;
    cursorKeyTap(dir);
  }
}

/* ---------- 剪贴板与下方按钮之间的分隔线（触摸板底边） ---------- */
var LINE_CURSOR_HIT_PX=14;
var lineCursorActive=false,lineCursorSliding=false,lineCursorStartX=0,lineCursorLastX=0,lineAccum=0,lineAccumDir=0;
function lineCursorHit(t){
  var r=pad.getBoundingClientRect();
  return t.clientY>=r.bottom-LINE_CURSOR_HIT_PX;
}
function lineCursorBegin(x){
  lineCursorActive=true;
  lineCursorSliding=false;
  lineCursorStartX=x;
  lineCursorLastX=x;
  lineAccum=0;
  lineAccumDir=0;
  cursorKeyStop();
}
function lineCursorUpdate(x){
  if(!lineCursorSliding){
    if(Math.abs(x-lineCursorStartX)<CURSOR_DRAG_TRIGGER_PX){ lineCursorLastX=x; return; }
    lineCursorSliding=true;
    lineCursorLastX=x;
    lineAccum=0;
    lineAccumDir=(x<lineCursorStartX)?-1:1;
    cursorKeyTap(lineAccumDir);   /* 起步先移一个字，之后按距离逐字移动 */
    return;
  }
  var dx=x-lineCursorLastX;
  lineCursorLastX=x;
  if(!dx) return;
  var dir=dx<0?-1:1;
  if(dir!==lineAccumDir){         /* 改变方向：立即改成相反方向逐字移动 */
    lineAccumDir=dir;
    lineAccum=CURSOR_DRAG_STEP_PX-CURSOR_REVERSE_PX;
  }
  lineAccum+=Math.abs(dx);
  while(lineAccum>=CURSOR_DRAG_STEP_PX){
    lineAccum-=CURSOR_DRAG_STEP_PX;
    cursorKeyTap(dir);
  }
}
function lineCursorEnd(){
  lineCursorActive=false;
  lineCursorSliding=false;
  cursorKeyStop();
}
window.addEventListener('blur',function(){
  cursorKeyStop();
  clipTouchActive=false;
  clipSliding=false;
  lineCursorActive=false;
  lineCursorSliding=false;
});
document.addEventListener('visibilitychange',function(){
  if(document.visibilityState!=='visible'){
    cursorKeyStop();
    clipTouchActive=false;
    clipSliding=false;
    lineCursorActive=false;
    lineCursorSliding=false;
  }
});

var clipGuard = 0;
clipBtnEl.addEventListener('touchstart', function(e){
  e.preventDefault(); e.stopPropagation();
  clipGuard = Date.now();
  clipTouchActive = true;
  clipSliding = false;
  cursorKeyStop();
  var t = e.touches[0];
  clipTouchStartX = t ? t.clientX : 0;
}, {passive:false});
clipBtnEl.addEventListener('touchmove', function(e){
  e.preventDefault(); e.stopPropagation();
  if(!clipTouchActive || e.touches.length !== 1) return;
  clipCursorUpdate(e.touches[0].clientX);
}, {passive:false});
clipBtnEl.addEventListener('touchend', function(e){
  e.preventDefault(); e.stopPropagation();
  if(!clipTouchActive) return;
  clipTouchActive = false;
  var wasSliding = clipSliding;
  clipSliding = false;
  cursorKeyStop();
  clipGuard = Date.now();
  if(!wasSliding) openClipPanel();
}, {passive:false});
clipBtnEl.addEventListener('touchcancel', function(e){
  e.preventDefault(); e.stopPropagation();
  clipTouchActive = false;
  clipSliding = false;
  cursorKeyStop();
  clipGuard = Date.now();
}, {passive:false});
clipBtnEl.addEventListener('click', function(e){
  if (Date.now() - clipGuard < 800) return;
  e.preventDefault();
  openClipPanel();
});
clipCloseEl.addEventListener('click', function(e){
  e.preventDefault(); e.stopPropagation();
  closeClipPanel();
});
clipCloseEl.addEventListener('touchstart', function(e){
  e.preventDefault(); e.stopPropagation();
  closeClipPanel();
}, {passive:false});
clipPanel.addEventListener('click', function(e){
  if (e.target === clipPanel) closeClipPanel();
});

/* ============================================================ */

function avgXY(touches){
  var sumX=0,sumY=0,n=touches.length;
  for(var i=0;i<n;i++){ sumX+=touches[i].clientX; sumY+=touches[i].clientY; }
  return {x:sumX/n, y:sumY/n};
}

function startTwoFinger(e){
  twoFingerActive=true;twoFingerUsed=true;cancelDrag();
  var t0=e.touches[0],t1=e.touches[1];
  lastTwoY=(t0.clientY+t1.clientY)/2;
  scrollAccum=0;twoFingerMoved=false;
  twoFingerStartX=(t0.clientX+t1.clientX)/2;
  twoFingerStartY=(t0.clientY+t1.clientY)/2;
  twoFingerStartTime=Date.now();
  twoFingerPinching=false;
  twoFingerStartDist=distBetween(t0,t1);
  lastPinchDist=twoFingerStartDist;
  twoFingerLastDir=0;
  show(L.two,1,0,0,0);
}
function startThreeFinger(e){
  threeFingerActive=true;threeFingerUsed=true;threeFingerMoved=false;threeFingerDir=0;
  threeFingerStartTime=Date.now();cancelDrag();
  var c=avgXY(e.touches);
  threeFingerStartX=c.x;threeFingerStartY=c.y;
  show(L.three,1,0,0,0);
}

pad.addEventListener('touchstart',function(e){
  e.preventDefault();
  var n=e.touches.length;
  if(n>=2){ if(lineCursorActive) lineCursorEnd(); edgeScrollStop(); }
  if(n>=4) return;
  if(n===3){ if(!threeFingerActive) startThreeFinger(e); return; }
  if(n===2){ if(threeFingerUsed) return; if(!twoFingerActive) startTwoFinger(e); return; }
  if(n===1){
    if(twoFingerUsed||threeFingerUsed) return;
    var t=e.touches[0];
    if(lineCursorHit(t)){ lineCursorBegin(t.clientX); return; }
    if(edgeScrollHit(t)){ edgeScrollBegin(t); return; }
    lx=sx=t.clientX;ly=sy=t.clientY;
    moved=0;st=Date.now();inDragMode=false;
    cancelLongPress();
    dragTimer=setTimeout(function(){
      dragTimer=null;
      if(!inDragMode && !twoFingerActive && !threeFingerActive){
        inDragMode=true;get('/mouse/click?btn=left_down');show(L.drag,1,1);
      }
    },350);
  }
},{passive:false});

pad.addEventListener('touchmove',function(e){
  e.preventDefault();
  var n=e.touches.length;

  if(n>=2){ if(lineCursorActive) lineCursorEnd(); edgeScrollStop(); }
  if(n>=4) return;

  if(n===3){
    if(!threeFingerActive) startThreeFinger(e);
    var c3=avgXY(e.touches);
    var ddx=c3.x-threeFingerStartX;
    var ddy=c3.y-threeFingerStartY;
    if(!threeFingerMoved &&
       (Math.abs(ddx)>THREE_FINGER_MOVE_PX || Math.abs(ddy)>THREE_FINGER_MOVE_PX)){
      threeFingerMoved = true;
    }
    if(Math.abs(ddy)>THREE_FINGER_MOVE_PX &&
       Math.abs(ddy)>Math.abs(ddx)*1.2){
      var newDir = ddy > 0 ? 1 : -1;
      if(newDir !== threeFingerDir){
        threeFingerDir = newDir;
        get('/keyboard/special?k=showdesktop');
        show(L.desktop,1,0,0,0,0,1);
      }
    } else if(Math.abs(ddx)>THREE_FINGER_MOVE_PX &&
              Math.abs(ddx)>Math.abs(ddy)*1.2){
      /* 三指左滑 → 前一个窗口 (Alt+Shift+Tab)；三指右滑 → 后一个窗口 (Alt+Tab) */
      var newDirH = ddx > 0 ? 2 : -2;
      if(newDirH !== threeFingerDir){
        threeFingerDir = newDirH;
        if(newDirH > 0){
          get('/keyboard/special?k=nextwindow');
          show(L.nextWin,1,0,0,0,0,1);
        } else {
          get('/keyboard/special?k=prevwindow');
          show(L.prevWin,1,0,0,0,0,1);
        }
      }
    }
    return;
  }

  if(n===2){
    if(threeFingerUsed) return;
    if(!twoFingerActive) startTwoFinger(e);
    var t0=e.touches[0],t1=e.touches[1];
    var y=(t0.clientY+t1.clientY)/2;
    var x=(t0.clientX+t1.clientX)/2;
    var dy2=y-lastTwoY;lastTwoY=y;

    var curDist=distBetween(t0,t1);

    if(!twoFingerMoved && !twoFingerPinching){
      if(Math.abs(curDist-twoFingerStartDist)>PINCH_THRESHOLD){
        twoFingerPinching=true;
        lastPinchDist=curDist;
      }
    }

    if(twoFingerPinching){
      if(Math.abs(curDist-lastPinchDist)>=PINCH_STEP){
        var dir = curDist > lastPinchDist ? 120 : -120;
        get('/mouse/zoom?delta='+dir);
        lastPinchDist = curDist;
        show(L.pinch,1,0,0,0);
      }
      return;
    }

    if(!twoFingerMoved){
      if(Math.abs(x-twoFingerStartX)>TWO_FINGER_MOVE_PX ||
         Math.abs(y-twoFingerStartY)>TWO_FINGER_MOVE_PX){
        twoFingerMoved=true;
      }
    }
    scrollAccum += (-dy2)*SCROLL_SENS;
    while(Math.abs(scrollAccum)>=SCROLL_STEP){
      var s=scrollAccum>0?SCROLL_STEP:-SCROLL_STEP;
      get('/mouse/scroll?delta='+s);
      scrollAccum-=s;
    }
    if(twoFingerMoved && dy2 !== 0){
      var dirSign = dy2 > 0 ? 1 : -1;
      if(dirSign !== twoFingerLastDir){
        twoFingerLastDir = dirSign;
        show(dirSign > 0 ? L.scrollDown : L.scrollUp, 1, 0, 0, 1);
      }
    }
    return;
  }

  if(n===1){
    if(lineCursorActive){ lineCursorUpdate(e.touches[0].clientX); return; }
    if(edgeScrollActive){ edgeScrollUpdate(e.touches[0]); return; }
    if(twoFingerUsed||threeFingerUsed) return;
    var t=e.touches[0];
    var dx=(t.clientX-lx)*SENS,dy=(t.clientY-ly)*SENS;
    lx=t.clientX;ly=t.clientY;
    if(!moved){
      if(Math.abs(t.clientX-sx)>MOVE_CANCEL_PX||Math.abs(t.clientY-sy)>MOVE_CANCEL_PX){
        moved=1;cancelLongPress();
      }
    }
    pdx+=Math.round(dx);pdy+=Math.round(dy);
    flush();
  }
},{passive:false});

pad.addEventListener('touchend',function(e){
  e.preventDefault();
  if(e.touches.length!==0) return;

  var isTap2 = twoFingerUsed && !twoFingerMoved && !twoFingerPinching &&
               (Date.now()-twoFingerStartTime)<=TWO_FINGER_TAP_MS;
  var isTap3 = threeFingerUsed && !threeFingerMoved &&
               (Date.now()-threeFingerStartTime)<=THREE_FINGER_TAP_MS;

  var wasThree = threeFingerUsed;
  var wasTwo  = twoFingerUsed;
  var wasDrag = inDragMode;
  var wasEdge = edgeScrollActive;
  var wasLine = lineCursorActive;

  if(wasLine) lineCursorEnd();
  edgeScrollStop();
  threeFingerActive=false; threeFingerUsed=false; threeFingerMoved=false; threeFingerDir=0;
  twoFingerActive=false; twoFingerUsed=false; twoFingerMoved=false; scrollAccum=0;
  twoFingerPinching=false; twoFingerStartDist=0; lastPinchDist=0;
  inDragMode=false;

  if(wasLine) return;
  if(wasEdge) return;
  if(wasThree){ if(isTap3) toggleCtrlLock(); return; }
  if(wasTwo){
    if(isTap2){ get('/mouse/click?btn=right'); show(L.right,1,0,0,0,1); }
    return;
  }

  cancelLongPress();
  if(wasDrag){ get('/mouse/click?btn=left_up'); show(L.release,1,1); return; }
  if(moved||Date.now()-st>400) return;
  var n=Date.now();
  if(n-lt<280){get('/mouse/click?btn=double');show(L.dbl,1);lt=0;}
  else{get('/mouse/click?btn=left');show(L.single,1);lt=n;}
},{passive:false});

pad.addEventListener('touchcancel',function(e){
  cancelLongPress();
  if(lineCursorActive) lineCursorEnd();
  edgeScrollStop();
  if(inDragMode){inDragMode=false;get('/mouse/click?btn=left_up');}
  twoFingerActive=false;twoFingerUsed=false;twoFingerMoved=false;scrollAccum=0;
  twoFingerPinching=false; twoFingerStartDist=0; lastPinchDist=0;
  threeFingerActive=false;threeFingerUsed=false;threeFingerMoved=false;threeFingerDir=0;
  moved=1;
});

function bind(id,u,label,cb){
  var b=document.getElementById(id);var t=0;
  b.addEventListener('touchstart',function(e){
    e.preventDefault();e.stopPropagation();
    t=Date.now();get(u);
    if(label)show(label,1);
    if(cb)cb();
  },{passive:false});
  b.addEventListener('click',function(){
    if(Date.now()-t<600)return;
    get(u);if(label)show(label,1);
    if(cb)cb();
  });
}
/* 左下角往前删除键（⌫）：
   · 按一下：给电脑发一次退格；
   · 长按：一直往前连续退格。 */
function bindBackspace(id, label){
  var b = document.getElementById(id);
  var holdDelay = null, repeatTimer = null, lastTouch = 0, lastPress = 0;

  function stopHold(){
    if (holdDelay){ clearTimeout(holdDelay); holdDelay = null; }
    if (repeatTimer){ clearInterval(repeatTimer); repeatTimer = null; }
    b.classList.remove('holding');
  }
  function pressOnce(){
    lastPress = Date.now();
    if (label) show(label, 1);
    get('/keyboard/special?k=backspace');
  }
  function startHold(){
    b.classList.add('holding');
    // 先删一次，长按再连续删
    pressOnce();
    holdDelay = setTimeout(function(){
      holdDelay = null;
      repeatTimer = setInterval(function(){
        get('/keyboard/special?k=backspace');
      }, 70);
    }, 400);
  }

  b.addEventListener('touchstart', function(e){
    e.preventDefault(); e.stopPropagation();
    lastTouch = Date.now();
    startHold();
  }, {passive:false});
  b.addEventListener('touchend', function(e){ e.preventDefault(); e.stopPropagation(); stopHold(); }, {passive:false});
  b.addEventListener('touchcancel', function(){ stopHold(); }, {passive:false});
  b.addEventListener('touchmove', function(e){
    var rect = b.getBoundingClientRect();
    var t = e.touches[0];
    if (t && (t.clientX < rect.left || t.clientX > rect.right || t.clientY < rect.top || t.clientY > rect.bottom)) stopHold();
  }, {passive:false});
  b.addEventListener('mousedown', function(){ if (Date.now() - lastTouch < 800) return; startHold(); });
  b.addEventListener('mouseup',   function(){ if (Date.now() - lastTouch < 800) return; stopHold(); });
  b.addEventListener('mouseleave',function(){ if (Date.now() - lastTouch < 800) return; stopHold(); });
  b.addEventListener('click', function(e){
    if (Date.now() - Math.max(lastTouch, lastPress) < 800) return;
    e.preventDefault();
    pressOnce();
  });
}

bind('btnL','/mouse/click?btn=left','');
bind('btnR','/mouse/click?btn=right','');
bindBackspace('btnA', L.backspace);
bind('btnI','/keyboard/special?k=switchapp','',flashSwitchAppBadge);
bind('btnE','/keyboard/special?k=enter','');

function bindHoldScroll(id, delta, label){
  var b = document.getElementById(id);
  var holdDelay = null, repeatTimer = null, lastTouch = 0;
  function doScrollOnce(){ get('/mouse/scroll?delta=' + delta); }
  function startHold(){
    doScrollOnce(); b.classList.add('holding');
    if(label) show(label, 1);
    holdDelay = setTimeout(function(){
      holdDelay = null;
      repeatTimer = setInterval(function(){ doScrollOnce(); }, 55);
    }, 350);
  }
  function stopHold(){
    if(holdDelay){ clearTimeout(holdDelay); holdDelay = null; }
    if(repeatTimer){ clearInterval(repeatTimer); repeatTimer = null; }
    b.classList.remove('holding');
  }
  b.addEventListener('touchstart', function(e){ e.preventDefault(); e.stopPropagation(); lastTouch = Date.now(); startHold(); }, {passive:false});
  b.addEventListener('touchend', function(e){ e.preventDefault(); e.stopPropagation(); stopHold(); }, {passive:false});
  b.addEventListener('touchcancel', function(e){ stopHold(); }, {passive:false});
  b.addEventListener('touchmove', function(e){
    var rect = b.getBoundingClientRect();
    var t = e.touches[0];
    if(t && (t.clientX < rect.left || t.clientX > rect.right || t.clientY < rect.top || t.clientY > rect.bottom)){ stopHold(); }
  }, {passive:false});
  b.addEventListener('mousedown', function(e){ if(Date.now() - lastTouch < 800) return; startHold(); });
  b.addEventListener('mouseup', function(e){ if(Date.now() - lastTouch < 800) return; stopHold(); });
  b.addEventListener('mouseleave', function(e){ if(Date.now() - lastTouch < 800) return; stopHold(); });
}
bindHoldScroll('btnU',  120, L.scrollUp);
bindHoldScroll('btnN', -120, L.scrollDown);

/* 【新增】复刻 ROVE 霸屏最右侧滚轮条的行为：
   · 滚轮上/下：按下立即滚一格，之后每 80ms 连续滚；松手/取消/失焦/切后台立刻停；
   · 鼠标中键：按一下 = 中键按下 + 抬起；
   · 轻微震动反馈与 ROVE 一致（滚轮 5ms，中键 12ms）。
   【改动】透明度恢复最早的半透明（opacity:.25），初始位置上移 5px；
   · 不再显示顶部小横条，按住浮动岛拖动即可自由挪位；
   · 浮动岛挂到 body 上，可拖到回车键等下方按键区域，不再被触摸板裁掉；
   · 拖到右侧屏幕边缘松手后自动贴边消隐，只留 6px 边条；
   · 点/滑右侧屏幕边缘线呼出，呼出后 3 秒无操作再次消隐；
   · 把它拖离右边后恢复为平时一直显示。 */
(function bindImmScroll(){
  var island = document.getElementById('immScroll');
  if(!island) return;
  var pad = document.getElementById('pad');
  var edge = document.getElementById('immEdgeTrigger');
  var EDGE_PX  = 6;      // 贴边消隐后留在屏幕上的边条宽度
  var DOCK_PX  = 8;      // 离右边缘多近算“拖到边缘”
  var SHOW_MS  = 3000;   // 呼出后无操作自动消隐的时间
  var MOVE_PX  = 8;      // 按住后移动多少像素开始拖动
  var timer = null;      // 滚轮连发
  var hideTimer = null;  // 呼出后的自动消隐
  var docked = false;    // 已贴到右边缘（贴边模式）
  var hidden = false;    // 当前正缩在屏幕外，只留边条
  var press = null;      // 当前按住/拖动状态
  var positioned = false;

  island.addEventListener('contextmenu', function(e){ e.preventDefault(); });
  island.addEventListener('dragstart', function(e){ e.preventDefault(); });
  /* 滚轮条放在触摸板内部：拦掉触摸事件，避免同时触发触摸板的移动/点击手势 */
  ['touchstart','touchmove','touchend','touchcancel'].forEach(function(t){
    island.addEventListener(t, function(e){ e.preventDefault(); e.stopPropagation(); }, {passive:false});
  });

  function stop(){ if(timer){ clearInterval(timer); timer = null; } }
  function fire(delta){ get('/mouse/scroll?delta=' + delta); }

  function clamp(v, lo, hi){ return v < lo ? lo : (v > hi ? hi : v); }
  function num(v, d){ var n = parseFloat(v); return isNaN(n) ? d : n; }
  function vw(){ return window.innerWidth  || document.documentElement.clientWidth  || 0; }
  function vh(){ return window.innerHeight || document.documentElement.clientHeight || 0; }
  function maxX(){ return Math.max(0, vw() - island.offsetWidth); }
  function maxY(){ return Math.max(0, vh() - island.offsetHeight); }
  function left(){ return num(island.style.left, 0); }
  function top(){  return num(island.style.top, 0); }

  function setPos(x, y){
    island.style.left = clamp(x, 0, maxX()) + 'px';
    island.style.top  = clamp(y, 0, maxY()) + 'px';
  }

  /* 先按 CSS 的 right:0 / bottom:5px 定位，再把浮动岛挂到 body 上改成 fixed 定位，
     这样它既能保持原来的初始位置，又能拖到下方回车键等按键区域而不被裁剪 */
  function initPos(){
    if(positioned || !pad || !pad.clientHeight) return;
    var ir = island.getBoundingClientRect();
    var x = vw() - island.offsetWidth;   // 贴右侧屏幕边缘
    var y = ir.top;                      // 保持原来比底部高 5px 的位置
    if(island.parentNode !== document.body) document.body.appendChild(island);
    if(edge && edge.parentNode !== document.body) document.body.appendChild(edge);
    island.style.position = 'fixed';
    island.style.right = 'auto';
    island.style.bottom = 'auto';
    island.style.left = clamp(x, 0, maxX()) + 'px';
    island.style.top  = clamp(y, 0, maxY()) + 'px';
    positioned = true;
  }

  function clearHideTimer(){
    if(hideTimer){ clearTimeout(hideTimer); hideTimer = null; }
  }
  function scheduleHide(){
    clearHideTimer();
    if(!docked) return;
    hideTimer = setTimeout(function(){
      hideTimer = null;
      if(!docked || press) return;
      hidden = true;
      updateTransform();
      showEdge(true);
    }, SHOW_MS);
  }
  function updateTransform(){
    island.style.transform = (docked && hidden)
      ? 'translateX(' + Math.max(0, island.offsetWidth - EDGE_PX) + 'px)'
      : 'none';
  }
  function showEdge(on){
    if(!edge) return;
    edge.style.display = on ? 'block' : 'none';
    if(on){
      var t = top();
      edge.style.top = clamp(t, 0, Math.max(0, vh() - 20)) + 'px';
      edge.style.height = Math.min(island.offsetHeight, Math.max(20, vh() - t)) + 'px';
    }
  }

  /* 贴到右边缘并消隐，只留一条 6px 的边线 */
  function dockToEdge(){
    docked = true;
    hidden = true;
    clearHideTimer();
    island.classList.add('imm-docked');
    island.style.left = maxX() + 'px';
    island.style.top  = clamp(top(), 0, maxY()) + 'px';
    updateTransform();
    showEdge(true);
  }

  /* 呼出：从右边滑回来，过一会儿没操作再自动消隐 */
  function reveal(){
    if(!docked) return;
    clearHideTimer();
    hidden = false;
    updateTransform();
    showEdge(false);
    scheduleHide();
  }

  /* 拖离右边：退出贴边模式，恢复平时一直显示 */
  function undock(){
    docked = false;
    hidden = false;
    clearHideTimer();
    island.classList.remove('imm-docked');
    island.style.transform = 'none';
    showEdge(false);
  }

  /* ---------- 自由拖动：按住浮动岛任意位置，移动超过 8px 就开始拖 ---------- */
  function startPress(e){
    if(hidden) return;
    if(e.pointerType === 'mouse' && e.button !== 0) return;
    if(press) return;
    var t = e.target;
    press = {
      id: e.pointerId,
      sx: e.clientX,
      sy: e.clientY,
      ox: left(),
      oy: top(),
      moved: false,
      mid: !!(t && t.closest && t.closest('[data-mid]'))
    };
    if(docked) clearHideTimer();
    /* 先由浮动岛接住指针；按在滚轮按钮上时按钮自己会再接管，行为保持不变 */
    try{ island.setPointerCapture(e.pointerId); }catch(_){}
  }
  function movePress(e){
    if(!press || e.pointerId !== press.id) return;
    var dx = e.clientX - press.sx;
    var dy = e.clientY - press.sy;
    if(!press.moved && Math.abs(dx) + Math.abs(dy) > MOVE_PX){
      press.moved = true;
      stop();                       // 拖动时立刻停掉滚轮连发
      if(docked) undock();
      clearHideTimer();
      island.classList.add('dragging');
    }
    if(!press.moved) return;
    e.preventDefault();
    setPos(press.ox + dx, press.oy + dy);
  }
  function endPress(e, cancelled){
    if(!press) return;
    if(e && e.pointerId !== undefined && e.pointerId !== press.id) return;
    var moved = press.moved;
    var mid   = press.mid;
    var id    = press.id;
    var ox    = press.ox;
    var oy    = press.oy;
    press = null;
    island.classList.remove('dragging');
    stop();
    try{ island.releasePointerCapture(id); }catch(_){}
    if(cancelled){
      if(moved){ setPos(ox, oy); undock(); }
      if(docked && !hidden) scheduleHide();
      return;
    }
    if(moved){
      /* 拖到右边缘松手：贴边消隐；拖到别处：保持当前位置一直显示 */
      if(left() >= maxX() - DOCK_PX) dockToEdge();
      else undock();
    } else if(mid){
      get('/mouse/click?btn=middle');
      try{ navigator.vibrate && navigator.vibrate(12); }catch(_){}
    }
    if(docked && !hidden) scheduleHide();
  }

  island.addEventListener('pointerdown', startPress, true);
  island.addEventListener('pointermove', movePress, {passive:false});
  island.addEventListener('pointerup', function(e){ endPress(e, false); });
  island.addEventListener('pointercancel', function(e){ endPress(e, true); });

  /* 点/滑右侧屏幕边缘线：呼出浮动岛 */
  if(edge){
    edge.addEventListener('pointerdown', function(e){
      e.preventDefault();
      e.stopPropagation();
      reveal();
    }, {passive:false});
  }

  function onResize(){
    if(!positioned){ initPos(); return; }
    if(docked){
      island.style.left = maxX() + 'px';
      island.style.top  = clamp(top(), 0, maxY()) + 'px';
      updateTransform();
      if(hidden) showEdge(true);
    } else {
      setPos(left(), top());
    }
  }
  window.addEventListener('resize', onResize);
  window.addEventListener('orientationchange', function(){
    setTimeout(onResize, 120);
  });
  window.addEventListener('load', initPos);

  /* 先按 CSS 的 right:0 / bottom:5px 定位，再换成固定的屏幕坐标 */
  initPos();
  if(window.requestAnimationFrame){
    requestAnimationFrame(function(){ requestAnimationFrame(initPos); });
  }

  /* ---------- 滚轮上/下：按住连续滚 ---------- */
  island.querySelectorAll('[data-scroll]').forEach(function(btn){
    var dir = btn.getAttribute('data-scroll') === 'up' ? 1 : -1;
    btn.addEventListener('contextmenu', function(e){ e.preventDefault(); });
    btn.addEventListener('pointerdown', function(e){
      if(e.pointerType === 'mouse' && e.button !== 0) return;
      e.preventDefault(); e.stopPropagation();
      try{ btn.setPointerCapture(e.pointerId); }catch(_){}
      stop(); fire(dir * 120);
      timer = setInterval(function(){ fire(dir * 120); }, 80);
      try{ navigator.vibrate && navigator.vibrate(5); }catch(_){}
    }, {passive:false});
    function end(e){ stop(); try{ btn.releasePointerCapture(e.pointerId); }catch(_){} }
    btn.addEventListener('pointerup', end);
    btn.addEventListener('pointercancel', end);
    btn.addEventListener('lostpointercapture', stop);
  });

  document.addEventListener('pointerup', stop, true);
  document.addEventListener('pointercancel', stop, true);
  window.addEventListener('blur', function(){ stop(); endPress(null, true); });
  document.addEventListener('visibilitychange', function(){
    if(document.visibilityState !== 'visible'){
      stop();
      endPress(null, true);
    }
  });
})();

var kbInput = document.getElementById('kbdInput');
var composing = false;
var btnK = document.getElementById('btnK');

var kbOpen = false;
var kbGuard = 0;

function setKbBtnState(on){
  kbOpen = !!on;
  if(on) btnK.classList.add('on');
  else   btnK.classList.remove('on');
}
function openKeyboard(){
  try{ kbInput.focus({preventScroll:true}); }
  catch(err){ try{ kbInput.focus(); }catch(e2){} }
  try{ kbInput.click(); }catch(err){}
  setKbBtnState(true);
  show(L.kbOpen,1,0,1,0,0,0,1);
}
function closeKeyboard(){
  try{ kbInput.blur(); }catch(err){}
  setKbBtnState(false);
  show(L.kbClose,1,0,1,0,0,0,1);
}
function toggleKeyboard(){
  var active = (document.activeElement === kbInput);
  if(kbOpen && active) closeKeyboard();
  else                 openKeyboard();
}

btnK.addEventListener('touchstart', function(e){
  e.preventDefault(); e.stopPropagation();
  kbGuard = Date.now();
  toggleKeyboard();
}, {passive:false});
btnK.addEventListener('click', function(e){
  if(Date.now() - kbGuard < 800) return;
  e.preventDefault();
  toggleKeyboard();
});

function flushKbText(){
  var v = kbInput.value; if(!v) return;
  for(var i=0;i<v.length;i++){
    var ch = v.charAt(i);
    var code = v.charCodeAt(i);
    if(code>=0xD800 && code<=0xDBFF && i+1<v.length){ ch = v.substring(i,i+2); i++; }
    get('/keyboard/char?c='+encodeURIComponent(ch));
  }
  kbInput.value = '';
}
kbInput.addEventListener('compositionstart', function(){ composing = true; });
kbInput.addEventListener('compositionend', function(){ composing = false; flushKbText(); });
kbInput.addEventListener('input', function(){ if(!composing) flushKbText(); });

kbInput.addEventListener('focus', function(){
  setKbBtnState(true);
  kbGuard = 0;
});
kbInput.addEventListener('blur', function(){
  if(Date.now() - kbGuard > 300){
    setKbBtnState(false);
  }
});

kbInput.addEventListener('keydown', function(e){
  if(composing) return;
  if(e.key==='Backspace'){ e.preventDefault(); get('/keyboard/special?k=backspace'); show(L.backspace,1,0,1); }
  else if(e.key==='Enter'){ e.preventDefault(); get('/keyboard/special?k=enter'); show(L.enter,1,0,1); }
  else if(e.key==='Tab'){ e.preventDefault(); get('/keyboard/special?k=tab'); show(L.tab,1,0,1); }
  else if(e.key==='Escape'){ e.preventDefault(); get('/keyboard/special?k=escape'); show(L.esc,1,0,1); }
  else if(e.key==='ArrowLeft'||e.key==='ArrowRight'||e.key==='ArrowUp'||e.key==='ArrowDown'){
    e.preventDefault();
    var m={ArrowLeft:'left',ArrowRight:'right',ArrowUp:'up',ArrowDown:'down'};
    get('/keyboard/special?k='+m[e.key]);
    show(L.arrow,1,0,1);
  }
});

/* ============================================================
 * 悬浮话筒
 *   · 默认停在鼠标左键正上方，可任意拖动（位置会记住）
 *   · 点一下给电脑发一次全局 Win+H：打开 Windows 自带的“语音输入”，
 *     直接说话就实时转成文字打进电脑当前输入框
 * ============================================================ */
var micBtnEl  = document.getElementById('micBtn');
var micAnchor = document.getElementById('btnL');
var MIC_POS_KEY = 'pm_mic_pos';
var micUserSet = false;
var micDragging = false, micMoved = false;
var micStartX = 0, micStartY = 0, micStartLeft = 0, micStartTop = 0;
var micGuard = 0, micLastTouch = 0;

function micPlace(left, top){
  var w = micBtnEl.offsetWidth  || 46;
  var h = micBtnEl.offsetHeight || 46;
  var vw = window.innerWidth  || 360;
  var vh = window.innerHeight || 640;
  if (left < 4) left = 4;
  if (top  < 4) top  = 4;
  if (left > vw - w - 4) left = vw - w - 4;
  if (top  > vh - h - 4) top  = vh - h - 4;
  micBtnEl.style.left = left + 'px';
  micBtnEl.style.top  = top  + 'px';
  micBtnEl.style.opacity = '1';
}
function micDefaultPos(){
  if (!micAnchor) return;
  var r = micAnchor.getBoundingClientRect();
  var w = micBtnEl.offsetWidth  || 46;
  var h = micBtnEl.offsetHeight || 46;
  micPlace(r.left + r.width / 2 - w / 2 - 9, r.top - h - 17);
}
(function initMicPos(){
  var saved = null;
  try{ saved = JSON.parse(localStorage.getItem(MIC_POS_KEY) || 'null'); }catch(err){ saved = null; }
  if (saved && typeof saved.left === 'number' && typeof saved.top === 'number'){
    micUserSet = true;
    micPlace(saved.left, saved.top);
  } else {
    micDefaultPos();
  }
})();

/* 话筒红/白 = 电脑语音输入是否正在收音；定时和电脑同步 */
var micOn = false;
var micPollTimer = 0;
var micTapAt = 0;
var micOffCount = 0;

function micSetOn(on){
  micOn = !!on;
  if (micOn) micBtnEl.classList.add('on');
  else micBtnEl.classList.remove('on');
}

function micPoll(){
  micPollTimer = 0;
  fetch('/voice/state', {cache:'no-store'})
    .then(function(r){ return r.json(); })
    .then(function(j){
      if (j && j.known && typeof j.listening !== 'undefined' &&
          Date.now() - micTapAt > 1200){
        if (j.listening){
          micOffCount = 0;
          micSetOn(true);
        } else if (++micOffCount >= 2){
          micSetOn(false);
        }
      }
      micPollTimer = setTimeout(micPoll, 1200);
    })
    .catch(function(){ micPollTimer = setTimeout(micPoll, 2000); });
}

function micTap(){
  /* 点白话筒 = 给电脑发 Win+H 打开语音输入（电脑端只做这一件事，保证一定发出去），
     同时手机话筒变红闪烁；再点红话筒 = 让电脑收起面板，话筒恢复白色。 */
  micTapAt = Date.now();
  micOffCount = 0;
  micShotCancel();
  if (micOn){
    micSetOn(false);
    get('/keyboard/special?k=winhclose');
    return;
  }
  micSetOn(true);
  get('/keyboard/special?k=winh');
  flashBadge(L.micWinH);
  /* 【新增】转完文字后拍一张电脑上“输入框”的图，铺在触摸板上给手机看 */
  micShotStart();
}

/* ============================================================
 * 【新增】Win+H 转完文字 → 拍一张电脑上“输入框”的图给手机看
 *   · 收音状态（/voice/state）由 收音 → 没收音 = 这段话说完了；
 *   · 再等 1.6 秒让电脑把最后几个字打完，然后截一张图；
 *   · 图铺在触摸板上，不挡任何按钮；
 *   · 双指捏合缩放；左滑 / 上滑贴到左边屏幕边缘消隐成一条细条，
 *     从左边屏幕边缘往右点一下 / 滑一下，细条收回原位重新显示；
 *   · 没等到文字（比如只是试了试麦克风）就不弹图，屏幕上啥也不留。
 * ============================================================ */
var micShotEl = null, micShotImg = null, micShotTimer = 0;
var micShotHeard = false, micShotSent = false;
var micShotDocked = false;          // 已贴到左边屏幕边缘消隐
var micShotPendingDock = false;     // 图还在解码时就已滑走：解码完再消隐
var micShotScale = 1;               // 当前缩放倍数（1 = 刚好和手机屏幕一样宽）
var micShotOffX = 0, micShotOffY = 0;   // 双指平移后的偏移（相对居中位置）
var MIC_SHOT_SCALE_MAX = 8;         // 双指捏合最多放大到 8 倍（远超触摸板大小，铺到屏幕外面）
var MIC_SHOT_MOVE_PX = 10;          // 单指移动超过这么多算滑动（不是点一下）
var micShotEdgeEl = null;           // 左边屏幕边缘的呼出热区

function micShotEdgeInit(){
  if (micShotEdgeEl || !document.body) return;
  micShotEdgeEl = document.getElementById('micShotEdge');
  if (!micShotEdgeEl){
    micShotEdgeEl = document.createElement('div');
    micShotEdgeEl.id = 'micShotEdge';
    micShotEdgeEl.setAttribute('aria-hidden', 'true');
    document.body.appendChild(micShotEdgeEl);
  }
  var open = function(e){
    micShotReveal();          // 从左边屏幕边缘收回截图
    if (e && e.cancelable) e.preventDefault();
    if (e && e.stopPropagation) e.stopPropagation();
  };
  micShotEdgeEl.addEventListener('touchstart', open, {passive:false});
  micShotEdgeEl.addEventListener('pointerdown', open, {passive:false});
  micShotEdgeEl.addEventListener('click', open, {passive:false});
}
function micShotEdgeShow(){
  if (!micShotEdgeEl) return;
  micShotEdgeEl.classList.toggle('on', micShotDocked && micShotScale > 0);
}
/* 正在显示的截图（没贴边、也没关掉）——缩放/捏合只认它 */
function micShotVisible(){
  return !!(micShotEl && micShotScale > 0 && !micShotDocked &&
            micShotEl.classList.contains('show'));
}
function micShotCancel(){
  if (micShotTimer){ clearTimeout(micShotTimer); micShotTimer = 0; }
  micShotHeard = false;
  micShotSent = false;
}
function micShotDock(on){
  micShotPendingDock = !!on;
  if (!micShotEl) return;
  micShotDocked = !!on;
  micShotEl.classList.toggle('docked', micShotDocked);
  micShotEdgeShow();
}
/* 从左边屏幕边缘点/滑一下：细条收回原位，重新显示截图 */
function micShotReveal(){
  if (!micShotEl || !micShotDocked) return;
  micShotResetPos();            // 收回时回到正面
  micShotDock(false);
}
/* 贴到左边屏幕边缘时不占地方：整块滑出去不见，只在左边留呼出细条 */
function micShotHide(){
  micShotDock(true);
}
/* 双指捏合缩放：k 是相对当前尺寸的倍数
   · 可以放大越过触摸板的范围（一路铺到手机屏幕外面去）；
   · 缩小最小到“左右宽度 = 手机屏幕左右宽度”，再小就不缩了。 */
function micShotZoom(k){
  if (!micShotImg || !k || !isFinite(k)) return;
  micShotScale = Math.min(MIC_SHOT_SCALE_MAX, Math.max(micShotMinScale(), micShotScale * k));
  micShotApply();
}
/* 最小缩放倍数：把图缩到和手机屏幕一样宽，就到底了 */
function micShotMinScale(){
  return MIC_SHOT_BASE_W > 0 ? Math.min(1, micShotViewW() / MIC_SHOT_BASE_W) : 1;
}
function micShotViewW(){
  return Math.max(1, window.innerWidth ||
                     document.documentElement.clientWidth || 320);
}
function micShotApply(){
  if (!micShotEl || !micShotImg || !micShotBaseW) return;
  /* 手指别抖过头：已经到底再缩就不动 */
  if (micShotScale < micShotMinScale()) micShotScale = micShotMinScale();
  micShotImg.style.width  = Math.round(micShotBaseW * micShotScale) + 'px';
  micShotImg.style.height = Math.round(micShotBaseH * micShotScale) + 'px';
  micShotApplyPos();
  micShotEdgeShow();
}
/* 【新增】双指平移：给截图加一个偏移量（相对“居中铺在触摸板里”的位置） */
function micShotApplyPos(){
  if (!micShotImg) return;
  micShotImg.style.transform = (micShotOffX || micShotOffY)
    ? 'translate(' + Math.round(micShotOffX) + 'px,' + Math.round(micShotOffY) + 'px)'
    : '';
}
/* 挪回正面：新截图、或从左边屏幕边缘收回时都回到中间 */
function micShotResetPos(){
  micShotOffX = 0;
  micShotOffY = 0;
  if (micShotImg) micShotImg.style.transform = '';
}
function micShotShow(blob){
  if (!micShotEl){
    micShotEl = document.createElement('div');
    micShotEl.id = 'micShot';
    micShotEdgeInit();          // 热区要挂在 body 上，别挂进触摸板
    micShotImg = document.createElement('img');
    micShotImg.alt = '';
    micShotImg.addEventListener('error', function(){ micShotHide(); });
    /* 图真正解码出来以后再定尺寸：就按“整张刚好放进触摸板”铺，之后捏合在此基础上缩放 */
    micShotImg.addEventListener('load', function(){
      micShotReady(0);
      micShotDock(!!micShotPendingDock);   // 用户可能在图解码前就已经滑走了
    });
    micShotEl.appendChild(micShotImg);
    /* 图放在触摸板里面：拖动 / 滑动照常能用，按钮也都在上层 */
    (pad || document.body).appendChild(micShotEl);
  }
  micShotEdgeInit();
  micShotScale = 1;
  micShotDock(false);
  micShotResetPos();            // 新图从正中间开始
  micShotImg.src = URL.createObjectURL(blob);
  micShotEl.classList.add('show');
  /* 有些浏览器先把图放进缓存、load 不一定每次都有：每次显示都量一遍 */
  micShotReady(0);
  setTimeout(function(){ micShotReady(1); }, 60);
}
/* 图解码完成后按触摸板大小定 1 倍尺寸（重试几次，量到为止） */
function micShotReady(tryN){
  if (!micShotImg) return;
  if (micShotImg.naturalWidth > 0){ fitMicShotImg(); micShotApply(); return; }
  if (tryN < 6) setTimeout(function(){ micShotReady(tryN + 1); }, 80);
}
var MIC_SHOT_BASE_W = 0, MIC_SHOT_BASE_H = 0;   // 手机屏幕宽度 / 按比例算出的高
var micShotBaseW = 0, micShotBaseH = 0;   // 1 倍（左右刚好和手机屏幕一样宽）时的尺寸
function fitMicShotImg(){
  /* 【改动】1 倍 = 左右宽度正好等于手机屏幕左右宽度（不是限制在触摸板里）；
     高度按比例；放大/缩小都以这个尺寸为基准，放大后可越过触摸板范围 */
  if (!micShotEl || !micShotImg) return;
  var nw = micShotImg.naturalWidth  || 0;
  var nh = micShotImg.naturalHeight || 0;
  if (!nw || !nh) return;
  MIC_SHOT_BASE_W = micShotViewW();
  MIC_SHOT_BASE_H = Math.max(1, Math.round(MIC_SHOT_BASE_W * nh / nw));
  micShotBaseW = MIC_SHOT_BASE_W;
  micShotBaseH = MIC_SHOT_BASE_H;
  micShotImg.style.width  = micShotBaseW + 'px';
  micShotImg.style.height = micShotBaseH + 'px';
}

/* ============================================================
 * 截图上的手势（只认截图自己的手指）：
 *   双指捏合 → 放大 / 缩小；
 *   双指滑动 → 挪移截图的位置（手指往哪滑，图就往哪走）；
 *   单指左滑 / 上滑 → 贴到左边屏幕边缘消隐（与上一版一致）；
 *   单指下滑 → 刷新成电脑此刻的实时截图（缩放、位置都保留）；
 *   其余触摸照常交给触摸板（移动 / 点击 / 滚动 / 捏合缩放电脑……）
 * ============================================================ */
(function micShotGestures(){
  var g = null;       // 当前手势

  function twoDist(e){
    var a = e.touches[0], b = e.touches[1];
    var dx = a.clientX - b.clientX, dy = a.clientY - b.clientY;
    return Math.sqrt(dx * dx + dy * dy);
  }
  function onePoint(t){ return t || null; }

  function start(e){
    if (!micShotVisible()) return;
    var n = e.touches ? e.touches.length : 1;
    var t0 = e.touches ? e.touches[0] : e;
    g = {
      pinching: n >= 2,
      dist:     n >= 2 ? twoDist(e) : 0,
      sx:       t0.clientX,
      sy:       t0.clientY,
      ox:       micShotOffX,          // 开始时截图已有的偏移，双指平移在它基础上累加
      oy:       micShotOffY,
      moved:    false,
      lastPinchAt: 0
    };
    if (n >= 2){
      /* 双指：中点的移动量就是平移量 */
      var c0 = twoCenter2(e);
      g.cx = c0.x;
      g.cy = c0.y;
    }
  }
  function twoCenter2(e){
    var a = e.touches[0], b = e.touches[1];
    return { x: (a.clientX + b.clientX) / 2, y: (a.clientY + b.clientY) / 2 };
  }
  function move(e){
    if (!g || !micShotVisible()) return;
    var list = (e.touches && e.touches.length) ? e.touches : [e];
    if (!(list.length >= 2 || e.touches)){
      /* 拿不到多指信息（鼠标 / 老浏览器）：不拦，交给触摸板 */
      return;
    }
    var n = e.touches.length;
    if (n >= 2){
      var d = twoDist(e);
      if (!g.pinching){
        g.pinching = true;
        g.dist = d;
        g.moved = true;
        var cc = twoCenter2(e);
        g.cx = cc.x;
        g.cy = cc.y;
      } else {
        if (g.dist > 0 && d > 0){
          var k = d / g.dist;
          g.dist = d;
          if (k !== 1){
            micShotZoom(k);      // 实时跟手
            g.lastPinchAt = Date.now();
          }
        }
        /* 【新增】双指滑动 = 挪移截图位置：两指中点往哪走，图就往哪走 */
        if (g.cx !== undefined){
          var cc2 = twoCenter2(e);
          var mdx = cc2.x - g.cx;
          var mdy = cc2.y - g.cy;
          if (Math.abs(mdx) > MIC_SHOT_MOVE_PX || Math.abs(mdy) > MIC_SHOT_MOVE_PX)
            g.moved = true;
          g.dx = mdx;
          g.dy = mdy;
          micShotOffX = g.ox + mdx;
          micShotOffY = g.oy + mdy;
          micShotApplyPos();
        }
      }
    } else if (g.pinching){
      /* 捏合过程中抬起一指：继续保持捏合状态，免得手指一松就变成拖动 */
      g.dist = 0;
    } else {
      var t = e.touches[0];
      var dx = t.clientX - g.sx;
      var dy = t.clientY - g.sy;
      if (Math.abs(dx) > MIC_SHOT_MOVE_PX || Math.abs(dy) > MIC_SHOT_MOVE_PX)
        g.moved = true;
    }
    if (e.cancelable) e.preventDefault();
    e.stopPropagation();
  }
  function end(e){
    if (!g) return;
    if (e && e.touches && e.touches.length > 0) return;   // 还有手指没抬
    var gg = g;
    g = null;
    if (e && e.type === 'touchcancel') return;
    if (!micShotVisible()) return;
    if (gg.pinching){
      /* 捏合/双指平移结束：不算点一下，也不贴边 */
      if (gg.lastPinchAt && Date.now() - gg.lastPinchAt < 400){
        micShotApply();
        micShotEdgeShow();
      }
      return;
    }
    if (!gg.moved) return;          // 只是点了一下图，什么都不做
    var dx = (e ? (e.changedTouches && e.changedTouches[0] ? e.changedTouches[0].clientX : e.clientX) : gg.sx) - gg.sx;
    var dy = (e ? (e.changedTouches && e.changedTouches[0] ? e.changedTouches[0].clientY : e.clientY) : gg.sy) - gg.sy;
    /* 单指：左滑 或 上滑 → 贴到左边屏幕边缘消隐（和原来一样） */
    var wantLeft  = dx <= -MIC_SHOT_MOVE_PX * 3 && Math.abs(dx) >= Math.abs(dy);
    var wantUp    = dy <= -MIC_SHOT_MOVE_PX * 3 && Math.abs(dy) >  Math.abs(dx);
    /* 【新增】单指：下滑 → 刷新成电脑的实时截图（缩放/位置不动） */
    var wantDown  = dy >=  MIC_SHOT_MOVE_PX * 3 && Math.abs(dy) >  Math.abs(dx);
    if (wantDown){ micShotRefresh(); return; }
    if (wantLeft || wantUp) micShotDock(true);
  }

  ['touchstart','touchmove','touchend','touchcancel'].forEach(function(type){
    pad.addEventListener(type, function(e){
      if (type === 'touchstart') start(e);
      else if (type === 'touchmove') move(e);
      else end(e);
    }, {passive:false, capture:true});
  });

  /* 鼠标：滚轮缩放 + 按住左键往左上拖贴边（手机上也走这里，不影响触摸） */
  pad.addEventListener('wheel', function(e){
    if (!micShotVisible()) return;
    micShotZoom(e.deltaY < 0 ? 1.12 : (1 / 1.12));
    e.preventDefault();
  }, {passive:false});
  window.addEventListener('resize', function(){
    if (!micShotEl || !micShotScale) return;
    fitMicShotImg();
    micShotApply();
  });
})();
function micShotSend(){
  if (micShotTimer){ clearTimeout(micShotTimer); micShotTimer = 0; }
  if (!micShotHeard || micShotSent) return false;
  micShotSent = true;
  fetch('/screen/input', {cache:'no-store'})
    .then(function(r){
      if (!r.ok) return null;
      return r.blob();
    })
    .then(function(b){
      if (b && b.size > 0) micShotShow(b);
    })
    .catch(function(){});
  return true;
}

/* ============================================================
 * 【新增】单指下滑 → 重新向电脑要一张实时截图，换掉现在这张
 *   · 缩放倍数、挪移位置、贴边状态全部保留，只换图；
 *   · 一次只允许一个刷新请求在飞，连滑不会堆一串；
 *   · 失败了就保持原图不动。
 * ============================================================ */
var micShotRefreshing = false;
function micShotRefresh(){
  if (!micShotEl || !micShotImg || micShotRefreshing) return false;
  micShotRefreshing = true;
  micShotEl.classList.add('refreshing');
  __micShotFetch('/screen/input', {cache:'no-store'})
    .then(function(r){
      if (!r.ok) return null;
      return r.blob();
    })
    .then(function(b){
      /* 换图，不改尺寸、不改位置：缩放倍数和挪移都保持原样 */
      if (b && b.size > 0 && micShotImg) micShotImg.src = URL.createObjectURL(b);
    })
    .catch(function(){})
    .then(function(){
      micShotRefreshing = false;
      if (micShotEl) micShotEl.classList.remove('refreshing');
    });
  return true;
}
function micShotStart(){
  micShotCancel();
  micShotSent = false;
  /* 看收音状态：先看见收音，再等收音停下来；没看见收音就等用户直接收起面板 */
  var t0 = Date.now();
  var offSeen = 0;
  var poll = function(){
    if (!micShotStart.gen) micShotStart.gen = 0;
    var gen = ++micShotStart.gen;
    micShotTimer = 0;
    fetch('/voice/state', {cache:'no-store'})
      .then(function(r){ return r.json(); })
      .then(function(j){
        if (gen !== micShotStart.gen || micShotSent) return false;
        var known = !!(j && j.known);
        if (known && j.listening){ micShotHeard = true; offSeen = 0; }
        else if (known && micShotHeard){
          if (++offSeen >= 2){
            /* 说完停下：等电脑把最后几个字打进输入框，再截图 */
            micShotTimer = setTimeout(function(){ micShotTimer = 0; micShotSend(); }, 1600);
            return false;
          }
        }
        if (Date.now() - t0 > 90000) return false;    // 90 秒还没说完就不拍了
        micShotTimer = setTimeout(poll, 700);
        return true;
      })
      .catch(function(){
        if (gen === micShotStart.gen)
          micShotTimer = setTimeout(poll, 1500);
      });
  };
  micShotTimer = setTimeout(poll, 1200);
}
micShotStart.gen = 0;

function micDown(e){
  var t = e.touches ? e.touches[0] : e;
  micDragging = true;
  micMoved = false;
  micStartX = t.clientX;
  micStartY = t.clientY;
  micStartLeft = parseFloat(micBtnEl.style.left) || 0;
  micStartTop  = parseFloat(micBtnEl.style.top)  || 0;
  if (e.cancelable) e.preventDefault();
}
function micMove(e){
  if (!micDragging) return;
  var t = e.touches ? e.touches[0] : e;
  if (!t) return;
  var dx = t.clientX - micStartX;
  var dy = t.clientY - micStartY;
  if (!micMoved && (Math.abs(dx) > 6 || Math.abs(dy) > 6)) micMoved = true;
  if (micMoved){
    micPlace(micStartLeft + dx, micStartTop + dy);
    if (e.cancelable) e.preventDefault();
  }
}
function micUp(e){
  if (!micDragging) return;
  micDragging = false;
  if (micMoved){
    try{
      localStorage.setItem(MIC_POS_KEY, JSON.stringify({
        left: parseFloat(micBtnEl.style.left) || 0,
        top:  parseFloat(micBtnEl.style.top)  || 0
      }));
    }catch(err){}
    micUserSet = true;
  } else {
    micGuard = Date.now();
    micTap();
  }
  if (e.cancelable) e.preventDefault();
}

micBtnEl.addEventListener('touchstart', function(e){
  micLastTouch = Date.now();
  micDown(e);
}, {passive:false});
micBtnEl.addEventListener('touchmove', micMove, {passive:false});
micBtnEl.addEventListener('touchend',   micUp,  {passive:false});
micBtnEl.addEventListener('touchcancel',function(e){
  if (!micDragging) return;
  micDragging = false;
  micMoved = false;
}, {passive:false});
micBtnEl.addEventListener('mousedown', function(e){
  if (Date.now() - micLastTouch < 800) return;
  micDown(e);
});
document.addEventListener('mousemove', function(e){
  if (Date.now() - micLastTouch < 800) return;
  micMove(e);
});
document.addEventListener('mouseup', function(e){
  if (Date.now() - micLastTouch < 800) return;
  micUp(e);
});
micBtnEl.addEventListener('click', function(e){
  e.preventDefault();
  if (Date.now() - micGuard < 800) return;
  if (micMoved){ micMoved = false; return; }
  micTap();
});
window.addEventListener('resize', function(){
  if (!micUserSet) micDefaultPos();
});
window.addEventListener('orientationchange', function(){
  setTimeout(function(){ if (!micUserSet) micDefaultPos(); }, 300);
});

/* 定时和电脑同步“是否正在收音”，红话筒状态以电脑为准 */
micPollTimer = setTimeout(micPoll, 800);
document.addEventListener('visibilitychange', function(){
  if (document.visibilityState === 'visible'){
    if (!micPollTimer) micPollTimer = setTimeout(micPoll, 300);
  } else if (micPollTimer){
    clearTimeout(micPollTimer);
    micPollTimer = 0;
  }
});
window.addEventListener('pagehide', function(){
  if (micPollTimer){ clearTimeout(micPollTimer); micPollTimer = 0; }
});

document.addEventListener('dblclick', function(e){ e.preventDefault(); }, {passive:false});
document.addEventListener('gesturestart', function(e){ e.preventDefault(); });
window.addEventListener('scroll', function(){ window.scrollTo(0,0); }, {passive:true});
})();
</script>
</body>
</html>
)HTML";

// 根据当前语言构建最终 HTML 页面
static std::string BuildHtmlPage() {
    std::string html = kHtmlTemplate;

    auto replaceAll = [&](const std::string& key, const std::string& val) {
        std::string k = std::string("{{") + key + "}}";
        size_t pos = 0;
        while ((pos = html.find(k, pos)) != std::string::npos) {
            html.replace(pos, k.size(), val);
            pos += val.size();
        }
    };
    auto repW = [&](const char* key, const wchar_t* w) {
        replaceAll(key, WideToUtf8(w));
    };
    auto repA = [&](const char* key, const char* s) {
        replaceAll(key, s ? std::string(s) : std::string());
    };

    if (g_lang == LANG_ZH) {
        repA("LANG", "zh-CN");
        repW("TITLE",      L"手机扫码秒变鼠标键盘触摸板");
        repA("PAD_BEFORE", "触 摸 板");
        repA("HINT_1", "单指 移动 / 点击 / 长按拖拽");
        repA("HINT_2", "双指 滑动滚轮 / 轻点右键 / 捏合缩放");
        repA("HINT_3", "三指 轻点 Ctrl 锁定");
        repA("HINT_4", "三指 上下滑 显示桌面");
        repA("HINT_5", "三指 左右滑 前后窗口");
        repA("LANDSCAPE_HINT", "单指移动/点击 · 双指滚轮/右键/捏合缩放 · 三指Ctrl/桌面/左右切窗口 · ⌫退格 · 切换应用 · 回车");
        repW("JS_READY",         L"就绪");
        repW("JS_CTRL_LOCKED",   L"CTRL 已锁定");
        repW("JS_CTRL_RELEASED", L"CTRL 已释放");
        repW("JS_DRAG",          L"拖拽中");
        repW("JS_RELEASE",       L"释放");
        repW("JS_SINGLE",        L"单击");
        repW("JS_DOUBLE",        L"双击");
        repW("JS_RIGHT",         L"右键");
        repW("JS_SCROLL_UP",     L"滚上");
        repW("JS_SCROLL_DOWN",   L"滚下");
        repW("JS_TWO",           L"双指操作");
        repW("JS_THREE",         L"三指操作");
        repW("JS_PINCH",         L"缩放");
        repW("JS_DESKTOP",       L"显示桌面");
        repW("JS_PREV_WIN",      L"前一个窗口");
        repW("JS_NEXT_WIN",      L"后一个窗口");
        repW("JS_KB_OPEN",       L"键盘已开");
        repW("JS_KB_CLOSE",      L"键盘已关");
        repW("JS_MIC_WINH",       L"请说话会转为文字");
        repW("JS_MIC_SHOT",       L"已转成文字，发一张输入框的图给你看");
        repW("JS_BACKSPACE",     L"退格");
        repW("JS_ENTER",         L"回车");
        repW("JS_TAB",           L"Tab");
        repW("JS_ESC",           L"Esc");
        repW("JS_ARROW",         L"方向键");
        repW("JS_SELECT_ALL",    L"全选");
        repW("JS_CTRL",          L"CTRL");
        repW("JS_SELECTED_BADGE",L"全选中");
        repW("JS_SWITCH_APP",    L"切换应用");
        // 【新增】剪贴板
        repW("JS_CLIP_TITLE",        L"电脑剪贴板");
        repW("JS_CLIP_EMPTY",        L"电脑剪贴板为空");
        repW("JS_CLIP_CUT",          L"剪切");
        repW("JS_CLIP_COPY_LABEL",   L"复制");
        repW("JS_CLIP_COPY_TO_PHONE",L"复制到手机");
        repW("JS_CLIP_COPIED",       L"✓ 已复制");
        repW("JS_CLIP_DOWNLOAD",     L"下载");
        repW("JS_CLIP_DOWNLOAD_ALL", L"全部下载");
        repW("JS_CLIP_LOADING",      L"读取中…");
        repW("JS_CLIP_ERROR",        L"读取失败");
        repW("JS_CLIP_NO_PERM",      L"无法访问剪贴板");
        // 文件夹 → ZIP 打包下载
        repW("JS_CLIP_ZIP",          L"打包");
        repW("JS_CLIP_ZIP_ALL",      L"全部打包成一个 ZIP 下载");
        repW("JS_CLIP_ZIP_TIP",      L"文件夹会在电脑上自动打包成 ZIP；iPhone 下载后到“文件”里长按 ZIP → 解压缩，即可得到整个文件夹。");
        repW("JS_CLIP_ZIP_TOO_BIG",  L"文件夹太大（超过 4GB 或 5 万个文件），无法一次打包，请分开复制。");
        repW("JS_CLIP_ITEM_WORD",    L"项");
        repW("JS_CLIP_ZIP_FILE",     L"电脑剪贴板.zip");
        // 0 字节文件：复制文件名前缀
        repW("JS_CLIP_COPY_NAME",    L"复制名称");
        repW("JS_CLIP_COPY_FAIL",    L"复制失败");
        repW("JS_CLIP_COPY_MANUAL",  L"长按上面的文字，选“拷贝”即可复制。");
        repW("JS_CLIP_COPY_MANUAL_CLOSE", L"关闭");
        // 手机 → 电脑：上传文件 / 新建文本
        repW("JS_CLIP_UPLOAD",       L"上传文件");
        repW("JS_CLIP_NEWTEXT",      L"新建文本");
        repW("JS_CLIP_NEWTEXT_PH",   L"输入要发送到电脑的文字…");
        repW("JS_CLIP_SEND",         L"发送到电脑");
        repW("JS_CLIP_CANCEL",       L"取消");
        repW("JS_CLIP_UPLOADING",    L"上传中");
        repW("JS_CLIP_UPLOAD_DONE",  L"已上传 {n} 个文件，已放进电脑剪贴板");
        repW("JS_CLIP_UPLOAD_FAIL",  L"个失败");
        repW("JS_CLIP_UPLOAD_NONET", L"上传失败，请检查手机与电脑是否在同一 WiFi");
        repW("JS_CLIP_SENT",         L"已发送到电脑剪贴板");
        repW("JS_CLIP_PASTE_FILLED", L"已填入手机剪贴板里的文字");
    } else {
        repA("LANG", "en");
        repW("TITLE",      L"Phone as Mouse Touchpad");
        repA("PAD_BEFORE", "TOUCHPAD");
        repA("HINT_1", "1 finger  move / click / long-press drag");
        repA("HINT_2", "2 fingers  scroll / tap right-click / pinch zoom");
        repA("HINT_3", "3 fingers  tap to toggle Ctrl");
        repA("HINT_4", "3 fingers  swipe up/down show desktop");
        repA("HINT_5", "3 fingers  swipe left/right prev/next window");
        repA("LANDSCAPE_HINT", "1 finger move/click · 2 fingers scroll/right-click/pinch · 3 fingers Ctrl/Desktop/switch window · ⌫ backspace · switch app · Enter");
        repW("JS_READY",         L"Ready");
        repW("JS_CTRL_LOCKED",   L"CTRL Locked");
        repW("JS_CTRL_RELEASED", L"CTRL Released");
        repW("JS_DRAG",          L"Dragging");
        repW("JS_RELEASE",       L"Release");
        repW("JS_SINGLE",        L"Click");
        repW("JS_DOUBLE",        L"Double");
        repW("JS_RIGHT",         L"Right Click");
        repW("JS_SCROLL_UP",     L"Scroll Up");
        repW("JS_SCROLL_DOWN",   L"Scroll Down");
        repW("JS_TWO",           L"2 Fingers");
        repW("JS_THREE",         L"3 Fingers");
        repW("JS_PINCH",         L"Zoom");
        repW("JS_DESKTOP",       L"Show Desktop");
        repW("JS_PREV_WIN",      L"Previous Window");
        repW("JS_NEXT_WIN",      L"Next Window");
        repW("JS_KB_OPEN",       L"Keyboard On");
        repW("JS_KB_CLOSE",      L"Keyboard Off");
        repW("JS_MIC_WINH",       L"Speak and it will be turned into text");
        repW("JS_MIC_SHOT",       L"Text done - here is the input box on your PC");
        repW("JS_BACKSPACE",     L"Backspace");
        repW("JS_ENTER",         L"Enter");
        repW("JS_TAB",           L"Tab");
        repW("JS_ESC",           L"Esc");
        repW("JS_ARROW",         L"Arrow");
        repW("JS_SELECT_ALL",    L"Select All");
        repW("JS_CTRL",          L"CTRL");
        repW("JS_SELECTED_BADGE",L"Selected");
        repW("JS_SWITCH_APP",    L"Switch App");
        // 【新增】剪贴板
        repW("JS_CLIP_TITLE",        L"PC Clipboard");
        repW("JS_CLIP_EMPTY",        L"PC clipboard is empty");
        repW("JS_CLIP_CUT",          L"Cut");
        repW("JS_CLIP_COPY_LABEL",   L"Copy");
        repW("JS_CLIP_COPY_TO_PHONE",L"Copy to Phone");
        repW("JS_CLIP_COPIED",       L"✓ Copied");
        repW("JS_CLIP_DOWNLOAD",     L"Download");
        repW("JS_CLIP_DOWNLOAD_ALL", L"Download All");
        repW("JS_CLIP_LOADING",      L"Loading…");
        repW("JS_CLIP_ERROR",        L"Load failed");
        repW("JS_CLIP_NO_PERM",      L"Cannot access clipboard");
        // Folders → ZIP download
        repW("JS_CLIP_ZIP",          L"ZIP");
        repW("JS_CLIP_ZIP_ALL",      L"Download all as one ZIP");
        repW("JS_CLIP_ZIP_TIP",      L"Folders are packed into a ZIP on the PC. On iPhone: open Files, long-press the ZIP, then Uncompress.");
        repW("JS_CLIP_ZIP_TOO_BIG",  L"Folder too large to pack (over 4GB or 50,000 items). Please copy it in parts.");
        repW("JS_CLIP_ITEM_WORD",    L"items");
        repW("JS_CLIP_ZIP_FILE",     L"PC-Clipboard.zip");
        // 0-byte files: copy the name prefix
        repW("JS_CLIP_COPY_NAME",    L"Copy name");
        repW("JS_CLIP_COPY_FAIL",    L"Copy failed");
        repW("JS_CLIP_COPY_MANUAL",  L"Long-press the text above and choose Copy.");
        repW("JS_CLIP_COPY_MANUAL_CLOSE", L"Close");
        // Phone → PC: upload files / new text
        repW("JS_CLIP_UPLOAD",       L"Upload");
        repW("JS_CLIP_NEWTEXT",      L"New text");
        repW("JS_CLIP_NEWTEXT_PH",   L"Type text to send to the PC...");
        repW("JS_CLIP_SEND",         L"Send to PC");
        repW("JS_CLIP_CANCEL",       L"Cancel");
        repW("JS_CLIP_UPLOADING",    L"Uploading");
        repW("JS_CLIP_UPLOAD_DONE",  L"Uploaded {n} file(s) to the PC clipboard");
        repW("JS_CLIP_UPLOAD_FAIL",  L"failed");
        repW("JS_CLIP_UPLOAD_NONET", L"Upload failed - check that the phone and PC are on the same WiFi");
        repW("JS_CLIP_SENT",         L"Sent to the PC clipboard");
        repW("JS_CLIP_PASTE_FILLED", L"Filled from the phone clipboard");
    }
    return html;
}

// ============================================================
// 鼠标 / 键盘模拟
// ============================================================
static void SendMouse(DWORD flags, LONG dx, LONG dy, DWORD data) {
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dx = dx; in.mi.dy = dy;
    in.mi.mouseData = data; in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(INPUT));
}
static void MouseMoveRel(int dx, int dy) { SendMouse(MOUSEEVENTF_MOVE, (LONG)dx, (LONG)dy, 0); }
static void MouseClickBtn(DWORD downFlag, DWORD upFlag) {
    SendMouse(downFlag, 0, 0, 0);
    SendMouse(upFlag,   0, 0, 0);
}
static void MouseDoubleClick() {
    MouseClickBtn(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
    Sleep(60);
    MouseClickBtn(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
}
static void MouseWheel(int delta) { SendMouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)delta); }
static void SendUnicodeChar(wchar_t ch) {
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type       = INPUT_KEYBOARD;
    in[0].ki.wScan   = (WORD)ch;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}
static void SendVkKey(WORD vk) {
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type       = INPUT_KEYBOARD;
    in[0].ki.wVk     = vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// 记住“本程序窗口出现之前/之外”最后一个前台窗口。
// 二维码窗口是置顶的，有时会抢到前台；收起时把前台还给用户原本的窗口。
static HWINEVENTHOOK g_fgHook = NULL;
static void CALLBACK ForegroundEventProc(HWINEVENTHOOK, DWORD event,
                                         HWND hwnd, LONG idObject,
                                         LONG idChild, DWORD, DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND &&
        idObject == OBJID_WINDOW && idChild == CHILDID_SELF &&
        hwnd && hwnd != g_hwnd && hwnd != g_miniHwnd &&
        IsUsableForegroundWindow(hwnd)) {
        g_lastForeground = hwnd;
    }
}
static void InstallForegroundHook() {
    if (g_fgHook) return;
    HWND fg = GetForegroundWindow();
    if (fg && fg != g_hwnd && fg != g_miniHwnd) g_lastForeground = fg;
    g_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                               NULL, ForegroundEventProc, 0, 0,
                               WINEVENT_OUTOFCONTEXT);
}
static void UninstallForegroundHook() {
    if (g_fgHook) {
        UnhookWinEvent(g_fgHook);
        g_fgHook = NULL;
    }
}

// ============================================================
// ListView 全选 / 反选
// ============================================================
static HWND FindChildWindowByClassRecursive(HWND hParent, const wchar_t* className) {
    if (!hParent || !IsWindow(hParent)) return NULL;
    HWND hChild = NULL;
    while ((hChild = FindWindowExW(hParent, hChild, NULL, NULL)) != NULL) {
        wchar_t buf[256] = {0};
        GetClassNameW(hChild, buf, 256);
        if (_wcsicmp(buf, className) == 0) return hChild;
        HWND hDeep = FindChildWindowByClassRecursive(hChild, className);
        if (hDeep) return hDeep;
    }
    return NULL;
}

static HWND FindListViewAroundWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return NULL;
    wchar_t cls[256] = {0};

    GetClassNameW(hWnd, cls, 256);
    if (_wcsicmp(cls, L"SysListView32") == 0 && IsWindowVisible(hWnd)) {
        return hWnd;
    }
    HWND hLV = FindChildWindowByClassRecursive(hWnd, L"SysListView32");
    if (hLV && IsWindowVisible(hLV)) return hLV;

    HWND hParent = GetParent(hWnd);
    while (hParent) {
        GetClassNameW(hParent, cls, 256);
        if (_wcsicmp(cls, L"SysListView32") == 0 && IsWindowVisible(hParent)) {
            return hParent;
        }
        hLV = FindChildWindowByClassRecursive(hParent, L"SysListView32");
        if (hLV && IsWindowVisible(hLV)) return hLV;
        hParent = GetParent(hParent);
    }
    return NULL;
}

static HWND FindTargetListView() {
    POINT pt;
    if (GetCursorPos(&pt)) {
        HWND hUnder = WindowFromPoint(pt);
        if (hUnder) {
            HWND hLV = FindListViewAroundWindow(hUnder);
            if (hLV) return hLV;
        }
    }
    HWND hForeground = GetForegroundWindow();
    if (hForeground) {
        HWND hLV = FindListViewAroundWindow(hForeground);
        if (hLV) return hLV;
    }
    return NULL;
}

static bool RemoteSetItemState(HWND hLV, int itemIndex, UINT stateMask, UINT state) {
    if (!hLV || !IsWindow(hLV)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hLV, &pid);
    if (!pid) return false;
    HANDLE hProcess = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) return false;

    LVITEMW lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask      = LVIF_STATE;
    lvi.stateMask = stateMask;
    lvi.state     = state;
    lvi.iItem     = itemIndex;
    lvi.iSubItem  = 0;

    LPVOID pRemote = VirtualAllocEx(hProcess, NULL, sizeof(LVITEMW),
                                    MEM_COMMIT, PAGE_READWRITE);
    if (!pRemote) {
        CloseHandle(hProcess);
        return false;
    }
    BOOL ok = WriteProcessMemory(hProcess, pRemote, &lvi, sizeof(LVITEMW), NULL);
    if (ok) {
        SendMessageW(hLV, LVM_SETITEMSTATE, (WPARAM)itemIndex, (LPARAM)pRemote);
    }
    VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return ok != FALSE;
}

static bool ListViewSelectAllItems(HWND hLV) {
    if (!hLV || !IsWindow(hLV)) return false;
    int count = (int)SendMessageW(hLV, LVM_GETITEMCOUNT, 0, 0);
    if (count <= 0) return false;
    bool any = false;
    for (int i = 0; i < count; ++i) {
        if (RemoteSetItemState(hLV, i, LVIS_SELECTED, LVIS_SELECTED)) {
            SendMessageW(hLV, LVM_UPDATE, (WPARAM)i, 0);
            any = true;
        }
    }
    if (any) {
        SendMessageW(hLV, LVM_ENSUREVISIBLE, 0, FALSE);
        InvalidateRect(hLV, NULL, TRUE);
    }
    return any;
}

static bool RemoteGetItemSelected(HWND hLV, int itemIndex, bool& selected) {
    if (!hLV || !IsWindow(hLV)) return false;
    LRESULT state = SendMessageW(hLV, LVM_GETITEMSTATE,
                                 (WPARAM)itemIndex, (LPARAM)LVIS_SELECTED);
    selected = (state & LVIS_SELECTED) != 0;
    return true;
}

static bool ListViewInvertSelection(HWND hLV) {
    if (!hLV || !IsWindow(hLV)) return false;
    int count = (int)SendMessageW(hLV, LVM_GETITEMCOUNT, 0, 0);
    if (count <= 0) return false;

    bool any = false;
    for (int i = 0; i < count; ++i) {
        bool sel = false;
        if (RemoteGetItemSelected(hLV, i, sel)) {
            UINT newState = sel ? 0 : LVIS_SELECTED;
            if (RemoteSetItemState(hLV, i, LVIS_SELECTED, newState)) {
                SendMessageW(hLV, LVM_UPDATE, (WPARAM)i, 0);
                any = true;
            }
        }
    }
    if (any) {
        SendMessageW(hLV, LVM_ENSUREVISIBLE, 0, FALSE);
        InvalidateRect(hLV, NULL, TRUE);
    }
    return any;
}

// ============================================================
// UI Automation 公共辅助
// ============================================================
static IUIAutomationElement* FindListContainerFromPoint(IUIAutomation* pAuto,
                                                        IUIAutomationElement* pAt,
                                                        IUIAutomationTreeWalker* pWalker) {
    if (!pAuto || !pAt || !pWalker) return NULL;
    IUIAutomationElement* pCur = pAt;
    pCur->AddRef();
    while (pCur) {
        CONTROLTYPEID ct = 0;
        pCur->get_CurrentControlType(&ct);
        if (ct == UIA_ListControlTypeId || ct == UIA_TreeControlTypeId) {
            return pCur;
        }
        IUIAutomationElement* pParent = NULL;
        HRESULT hr = pWalker->GetParentElement(pCur, &pParent);
        pCur->Release();
        pCur = (SUCCEEDED(hr) && pParent) ? pParent : NULL;
    }
    return NULL;
}

static IUIAutomationCondition* MakeItemCondition(IUIAutomation* pAuto) {
    if (!pAuto) return NULL;
    VARIANT v; VariantInit(&v);
    v.vt = VT_I4;

    v.lVal = UIA_ListItemControlTypeId;
    IUIAutomationCondition* pC1 = NULL;
    pAuto->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &pC1);

    v.lVal = UIA_TreeItemControlTypeId;
    IUIAutomationCondition* pC2 = NULL;
    pAuto->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &pC2);

    IUIAutomationCondition* pAny = NULL;
    if (pC1 && pC2) pAuto->CreateOrCondition(pC1, pC2, &pAny);

    if (pC1) pC1->Release();
    if (pC2) pC2->Release();
    return pAny;
}

static bool InvertSelectionViaUIA(POINT pt) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    IUIAutomation* pAuto = NULL;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL,
                                  CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation,
                                  (void**)&pAuto);
    if (FAILED(hr) || !pAuto) return false;

    bool ok = false;

    IUIAutomationElement* pAt = NULL;
    hr = pAuto->ElementFromPoint(pt, &pAt);
    if (FAILED(hr) || !pAt) { pAuto->Release(); return false; }

    IUIAutomationTreeWalker* pWalker = NULL;
    pAuto->get_ControlViewWalker(&pWalker);

    IUIAutomationElement* pList = FindListContainerFromPoint(pAuto, pAt, pWalker);
    if (pWalker) pWalker->Release();
    pAt->Release();

    if (pList) {
        IUIAutomationCondition* pAny = MakeItemCondition(pAuto);
        if (pAny) {
            IUIAutomationElementArray* pItems = NULL;
            hr = pList->FindAll(TreeScope_Children, pAny, &pItems);
            if (SUCCEEDED(hr) && pItems) {
                int cnt = 0;
                pItems->get_Length(&cnt);
                for (int i = 0; i < cnt; ++i) {
                    IUIAutomationElement* pItem = NULL;
                    if (FAILED(pItems->GetElement(i, &pItem)) || !pItem) continue;

                    IUIAutomationSelectionItemPattern* pSel = NULL;
                    hr = pItem->GetCurrentPatternAs(
                             UIA_SelectionItemPatternId,
                             IID_IUIAutomationSelectionItemPattern,
                             (void**)&pSel);
                    if (SUCCEEDED(hr) && pSel) {
                        BOOL isSel = FALSE;
                        pSel->get_CurrentIsSelected(&isSel);
                        if (isSel) pSel->RemoveFromSelection();
                        else       pSel->AddToSelection();
                        pSel->Release();
                        ok = true;
                    }
                    pItem->Release();
                }
                pItems->Release();
            }
            pAny->Release();
        }
        pList->Release();
    }

    pAuto->Release();
    return ok;
}

static bool SelectAllViaUIA(POINT pt) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    IUIAutomation* pAuto = NULL;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL,
                                  CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation,
                                  (void**)&pAuto);
    if (FAILED(hr) || !pAuto) return false;

    bool ok = false;

    IUIAutomationElement* pAt = NULL;
    hr = pAuto->ElementFromPoint(pt, &pAt);
    if (FAILED(hr) || !pAt) { pAuto->Release(); return false; }

    IUIAutomationTreeWalker* pWalker = NULL;
    pAuto->get_ControlViewWalker(&pWalker);

    IUIAutomationElement* pList = FindListContainerFromPoint(pAuto, pAt, pWalker);
    if (pWalker) pWalker->Release();
    pAt->Release();

    if (pList) {
        IUIAutomationCondition* pAny = MakeItemCondition(pAuto);
        if (pAny) {
            IUIAutomationElementArray* pItems = NULL;
            hr = pList->FindAll(TreeScope_Children, pAny, &pItems);
            if (SUCCEEDED(hr) && pItems) {
                int cnt = 0;
                pItems->get_Length(&cnt);
                for (int i = 0; i < cnt; ++i) {
                    IUIAutomationElement* pItem = NULL;
                    if (FAILED(pItems->GetElement(i, &pItem)) || !pItem) continue;

                    IUIAutomationSelectionItemPattern* pSel = NULL;
                    hr = pItem->GetCurrentPatternAs(
                             UIA_SelectionItemPatternId,
                             IID_IUIAutomationSelectionItemPattern,
                             (void**)&pSel);
                    if (SUCCEEDED(hr) && pSel) {
                        pSel->AddToSelection();
                        pSel->Release();
                        ok = true;
                    }
                    pItem->Release();
                }
                pItems->Release();
            }
            pAny->Release();
        }
        pList->Release();
    }

    pAuto->Release();
    return ok;
}

static void SendInvertSelection() {
    HWND hLV = FindTargetListView();
    if (hLV && ListViewInvertSelection(hLV)) return;

    POINT pt;
    if (GetCursorPos(&pt)) {
        if (InvertSelectionViaUIA(pt)) return;
    }
}

static void SendCtrlA() {
    bool ctrlWasLocked = (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1);
    INPUT in[4];
    memset(in, 0, sizeof(in));
    int n = 0;
    if (!ctrlWasLocked) {
        in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_CONTROL; n++;
    }
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = 'A'; n++;
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = 'A';
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    if (!ctrlWasLocked) {
        in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    }
    SendInput(n, in, sizeof(INPUT));
    if (ctrlWasLocked) InterlockedExchange(&g_ctrlDown, 1);
}

static void SendSelectAll() {
    HWND hLV = FindTargetListView();
    if (hLV && ListViewSelectAllItems(hLV)) return;

    POINT pt;
    if (GetCursorPos(&pt)) {
        if (SelectAllViaUIA(pt)) return;
    }

    HWND hFore = GetForegroundWindow();
    bool needSwitch = (hFore == NULL || hFore == g_hwnd || hFore == g_miniHwnd);
    if (needSwitch) {
        POINT pt2;
        if (GetCursorPos(&pt2)) {
            HWND hUnder = WindowFromPoint(pt2);
            if (hUnder && hUnder != g_hwnd && hUnder != g_miniHwnd) {
                HWND hRoot = GetAncestor(hUnder, GA_ROOT);
                if (hRoot && hRoot != g_hwnd && hRoot != g_miniHwnd) {
                    SetForegroundWindow(hRoot);
                    Sleep(60);
                }
            }
        }
    }
    SendCtrlA();
}

static void SendZoom(int delta) {
    bool ctrlWasLocked = (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1);
    INPUT in[3];
    memset(in, 0, sizeof(in));
    int n = 0;
    if (!ctrlWasLocked) {
        in[n].type    = INPUT_KEYBOARD;
        in[n].ki.wVk  = VK_CONTROL;
        n++;
    }
    in[n].type         = INPUT_MOUSE;
    in[n].mi.mouseData = (DWORD)delta;
    in[n].mi.dwFlags   = MOUSEEVENTF_WHEEL;
    n++;
    if (!ctrlWasLocked) {
        in[n].type       = INPUT_KEYBOARD;
        in[n].ki.wVk     = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        n++;
    }
    SendInput(n, in, sizeof(INPUT));
    if (ctrlWasLocked) InterlockedExchange(&g_ctrlDown, 1);
}

static void SendWinD() {
    bool ctrlWasLocked = (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1);
    INPUT in[6];
    memset(in, 0, sizeof(in));
    int n = 0;
    if (ctrlWasLocked) {
        in[n].type       = INPUT_KEYBOARD;
        in[n].ki.wVk     = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        n++;
    }
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = VK_LWIN; n++;
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = 'D';     n++;
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = 'D';
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = VK_LWIN;
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    if (ctrlWasLocked) {
        in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = VK_CONTROL; n++;
    }
    SendInput(n, in, sizeof(INPUT));
    if (ctrlWasLocked) InterlockedExchange(&g_ctrlDown, 1);
}

// 【新增】点话筒：发送全局 Win+H，打开 Windows 自带的“语音输入”，
//   打开后直接对着手机/电脑说话，系统就把说的话实时转成文字打进电脑当前输入框。
//   与 SendWinD 一样：Ctrl 处于锁定状态时先临时抬起再按回，
//   否则 Ctrl+Win+H 不是系统语音输入的热键。
static void SendWinH() {
    bool ctrlWasLocked = (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1);
    INPUT in[6];
    memset(in, 0, sizeof(in));
    int n = 0;
    if (ctrlWasLocked) {
        in[n].type       = INPUT_KEYBOARD;
        in[n].ki.wVk     = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        n++;
    }
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = VK_LWIN; n++;
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = 'H';     n++;
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = 'H';
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = VK_LWIN;
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    if (ctrlWasLocked) {
        in[n].type   = INPUT_KEYBOARD; in[n].ki.wVk = VK_CONTROL; n++;
    }
    SendInput(n, in, sizeof(INPUT));
    if (ctrlWasLocked) InterlockedExchange(&g_ctrlDown, 1);
}

// ============================================================
// 话筒按钮：分“打开 / 关闭”两个动作
// ------------------------------------------------------------
// · 白话筒（winh）  ：只发 Win+H，绝不先做任何关闭动作，保证一定调起语音输入；
// · 红话筒（winhclose）：用 UIA 点语音输入面板自带的 CloseButton 收起；
//   点不到（UIA 不可用）再补发一次 Win+H，交给系统自己切换。
// ============================================================
static volatile LONG g_voiceKnown = 0;   // 是否亲眼见过语音输入面板（供状态同步用）
static bool ProcessIsTextInputHost(DWORD pid) {
    if (!pid) return false;
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return false;
    wchar_t path[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(hp, 0, path, &size) != FALSE;
    CloseHandle(hp);
    if (!ok) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"TextInputHost.exe") == 0;
}
// 语音输入收音时会一直占用麦克风采集：查默认录音设备的音频会话，
// 看 TextInputHost 是否有 Active 的采集会话。停止收音后会话会变 Inactive。
static bool MicCapturedByTextInputHost() {
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool captured = false;
    IMMDeviceEnumerator* pEnum = NULL;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
                                   CLSCTX_ALL, IID_PPV_ARGS(&pEnum))) && pEnum) {
        IMMDevice* pDev = NULL;
        if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eCapture, eConsole, &pDev)) && pDev) {
            IAudioSessionManager2* pMgr = NULL;
            if (SUCCEEDED(pDev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                                         NULL, (void**)&pMgr)) && pMgr) {
                IAudioSessionEnumerator* pSessions = NULL;
                if (SUCCEEDED(pMgr->GetSessionEnumerator(&pSessions)) && pSessions) {
                    int n = 0;
                    pSessions->GetCount(&n);
                    for (int i = 0; i < n && !captured; ++i) {
                        IAudioSessionControl* pCtl = NULL;
                        if (FAILED(pSessions->GetSession(i, &pCtl)) || !pCtl) continue;
                        IAudioSessionControl2* pCtl2 = NULL;
                        if (SUCCEEDED(pCtl->QueryInterface(
                                __uuidof(IAudioSessionControl2), (void**)&pCtl2)) && pCtl2) {
                            DWORD pid = 0;
                            AudioSessionState st = AudioSessionStateInactive;
                            pCtl2->GetProcessId(&pid);
                            pCtl2->GetState(&st);
                            if (st == AudioSessionStateActive &&
                                ProcessIsTextInputHost(pid))
                                captured = true;
                            pCtl2->Release();
                        }
                        pCtl->Release();
                    }
                    pSessions->Release();
                }
                pMgr->Release();
            }
            pDev->Release();
        }
        pEnum->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return captured;
}
static bool IsVoiceCloseElement(IUIAutomationElement* el) {
    if (!el) return false;
    BSTR b = NULL;
    bool hit = false;
    if (SUCCEEDED(el->get_CurrentAutomationId(&b)) && b) {
        if (_wcsicmp(b, L"CloseButton") == 0 ||
            _wcsicmp(b, L"CloseButtonV1") == 0)
            hit = true;
        SysFreeString(b);
        b = NULL;
    }
    if (!hit && SUCCEEDED(el->get_CurrentName(&b)) && b) {
        if (_wcsicmp(b, L"Close") == 0 ||
            _wcsicmp(b, L"关闭") == 0 ||
            _wcsicmp(b, L"关闭语音输入") == 0 ||
            _wcsicmp(b, L"关闭听写") == 0 ||
            _wcsicmp(b, L"Close voice typing") == 0 ||
            _wcsicmp(b, L"Close dictation") == 0 ||
            _wcsicmp(b, L"Dismiss") == 0)
            hit = true;
        SysFreeString(b);
    }
    return hit;
}
static bool WindowHasVoiceCloseButton(HWND hwnd, bool invokeIt) {
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool found = false;
    IUIAutomation* pAuto = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, NULL,
                                   CLSCTX_INPROC_SERVER,
                                   IID_IUIAutomation, (void**)&pAuto)) && pAuto) {
        IUIAutomationElement* pRoot = NULL;
        if (SUCCEEDED(pAuto->ElementFromHandle(hwnd, &pRoot)) && pRoot) {
            IUIAutomationElementArray* pArr = NULL;
            if (SUCCEEDED(pRoot->FindAll(TreeScope_Descendants, NULL, &pArr)) && pArr) {
                int n = 0;
                pArr->get_Length(&n);
                for (int i = 0; i < n && !found; ++i) {
                    IUIAutomationElement* pEl = NULL;
                    if (FAILED(pArr->GetElement(i, &pEl)) || !pEl) continue;
                    if (IsVoiceCloseElement(pEl)) {
                        found = true;
                        if (invokeIt) {
                            IUIAutomationInvokePattern* pInv = NULL;
                            if (SUCCEEDED(pEl->GetCurrentPattern(
                                    UIA_InvokePatternId, (IUnknown**)&pInv)) && pInv) {
                                pInv->Invoke();
                                pInv->Release();
                            }
                        }
                    }
                    pEl->Release();
                }
                pArr->Release();
            }
            pRoot->Release();
        }
        pAuto->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return found;
}
static BOOL CALLBACK VoiceHostWindowProc(HWND h, LPARAM lp) {
    if (*reinterpret_cast<bool*>(lp)) return FALSE;
    if (!IsWindowVisible(h)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    if (!ProcessIsTextInputHost(pid)) return TRUE;
    if (WindowHasVoiceCloseButton(h, true)) {
        *reinterpret_cast<bool*>(lp) = true;
        return FALSE;
    }
    return TRUE;
}
static BOOL CALLBACK VoiceHostProbeProc(HWND h, LPARAM lp) {
    if (*reinterpret_cast<bool*>(lp)) return FALSE;
    if (!IsWindowVisible(h)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    if (!ProcessIsTextInputHost(pid)) return TRUE;
    if (WindowHasVoiceCloseButton(h, false)) {
        *reinterpret_cast<bool*>(lp) = true;
        return FALSE;
    }
    return TRUE;
}
static bool CloseVoiceTypingByUia() {
    bool invoked = false;
    EnumWindows(VoiceHostWindowProc, reinterpret_cast<LPARAM>(&invoked));
    if (!invoked) return false;
    Sleep(350);                      // 等收起动画
    bool still = false;
    EnumWindows(VoiceHostProbeProc, reinterpret_cast<LPARAM>(&still));
    return !still;                   // 关闭按钮没了 = 面板已经收起
}
static void CloseVoiceTyping() {
    if (CloseVoiceTypingByUia()) return;   // 点面板自带的关闭按钮
    SendWinH();                            // 兜底：交给系统切换
}

// ------------------------------------------------------------
// 【新增】语音输入面板所在的 TextInputHost 顶层窗口
//   截图时把它排除掉：截的是“电脑上正在输入文字的那个窗口”，
//   而不是浮在上面的语音输入小面板。
// ------------------------------------------------------------
static BOOL CALLBACK VoiceHostTopWindowProc(HWND h, LPARAM lp) {
    std::vector<HWND>* out = reinterpret_cast<std::vector<HWND>*>(lp);
    if (!out || !IsWindowVisible(h) || IsIconic(h)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    if (!ProcessIsTextInputHost(pid)) return TRUE;
    RECT rc = {0, 0, 0, 0};
    if (!GetWindowRect(h, &rc)) return TRUE;
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return TRUE;
    out->push_back(h);
    return TRUE;          // 全部收下，后面按“最小”挑面板
}
static bool IsVoiceHostWindow(HWND h) {
    DWORD pid = 0;
    if (!h || !IsWindow(h)) return false;
    GetWindowThreadProcessId(h, &pid);
    return pid != 0 && pid != GetCurrentProcessId() && ProcessIsTextInputHost(pid);
}

// ------------------------------------------------------------
// 【新增】截一张“正在输入文字的窗口”的图，编码成 PNG
//   · 优先取：前台窗口（语音输入面板一律跳过）→ 鼠标下窗口 → 最后一个前台窗口；
//   · 直接截屏幕对应区域（会把语音输入小面板一起拍进去，正是要的效果）；
//     目标窗口贴边或整段被遮挡导致截空时，改用 PrintWindow 单独渲染这个窗口；
//   · 目标窗口比屏幕大（最大化/贴边）时按比例缩到屏幕以内。
// ------------------------------------------------------------
// ------------------------------------------------------------
// 【新增】截图 → PNG（Win+H 转完文字后拍一张“输入框”的图给手机看）
//   · 抓屏范围 = 目标窗口 ∪ 语音输入面板，再夹到屏幕以内，
//     所以浮在输入框旁边的语音输入小面板也会被拍进去；
//   · 目标窗口还在屏幕上：直接截真实屏幕内容；
//     贴边/整块被挡住：让窗口自己渲染一份（PrintWindow）；
//   · 输出缩略 2 倍（长边最多 1600），手机一张普通 PNG 就能看，传个几秒就完；
//   · 截图就是窗口原样：不画红框、不加文字提示。
// ------------------------------------------------------------
struct ShotCtx {
    HDC     sdc;
    HDC     mdc;
    HBITMAP bmp;
    HGDIOBJ old;
    void*   bits;
    int     width, height;
    ShotCtx() : sdc(NULL), mdc(NULL), bmp(NULL), old(NULL),
                bits(NULL), width(0), height(0) {}
};
static void ShotCleanup(ShotCtx& c) {
    if (c.mdc) {
        if (c.old) SelectObject(c.mdc, c.old);
        DeleteDC(c.mdc);
    }
    if (c.bmp) DeleteObject(c.bmp);
    if (c.sdc) ReleaseDC(NULL, c.sdc);
    c.sdc = NULL; c.mdc = NULL; c.bmp = NULL;
    c.old = NULL; c.bits = NULL; c.width = c.height = 0;
}
// 一张 32 位自上而下 DIB，屏幕大小：之后每次截图只往里拷屏幕的对应区域
static bool ShotBegin(ShotCtx& c) {
    c.sdc = GetDC(NULL);
    if (!c.sdc) { ShotCleanup(c); return false; }
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0 || sh <= 0) { ShotCleanup(c); return false; }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sw;
    bi.bmiHeader.biHeight      = -sh;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // 用系统自己的内存建 DIB section：GDI+ 的 Bitmap(aPixelData) 直接读这块像素
    c.mdc = CreateCompatibleDC(c.sdc);
    c.bmp = CreateDIBSection(c.sdc, &bi, DIB_RGB_COLORS, &c.bits, NULL, 0);
    if (!c.mdc || !c.bmp || !c.bits) { ShotCleanup(c); return false; }
    c.old = SelectObject(c.mdc, c.bmp);
    c.width  = sw;
    c.height = sh;
    return true;
}
// 缩略后编码成 PNG：src 是 c.mdc 里实际收到的 (srcW × srcH) 像素
static bool ShotEncodePng(ShotCtx& c, int srcW, int srcH, std::string& png) {
    png.clear();
    if (srcW <= 0 || srcH <= 0 || srcW > c.width || srcH > c.height) return false;

    // 缩略 2 倍（长边最多 1600）：手机上一张普通 PNG 就能看清，传输也就一两秒
    int outW = (srcW + 1) / 2;
    int outH = (srcH + 1) / 2;
    const int kMaxSide = 1600;
    if (outW > kMaxSide || outH > kMaxSide) {
        double k = (double)kMaxSide / std::max(outW, outH);
        outW = std::max(1, (int)(outW * k));
        outH = std::max(1, (int)(outH * k));
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = outW;
    bi.bmiHeader.biHeight      = -outH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* outBits = NULL;
    HBITMAP dib = CreateDIBSection(c.sdc, &bi, DIB_RGB_COLORS, &outBits, NULL, 0);
    if (!dib || !outBits) { if (dib) DeleteObject(dib); return false; }

    bool ok = false;
    HDC odc = CreateCompatibleDC(c.sdc);
    if (odc) {
        HGDIOBJ o = SelectObject(odc, dib);
        /* 一次 HALFTONE 缩到目标尺寸；截图就是窗口原样，不加任何框和字 */
        SetStretchBltMode(odc, HALFTONE);
        SetBrushOrgEx(odc, 0, 0, NULL);
        StretchBlt(odc, 0, 0, outW, outH, c.mdc, 0, 0, srcW, srcH, SRCCOPY);
        SelectObject(odc, o);
        DeleteDC(odc);
        ok = true;
    }

    if (ok) {
        Gdiplus::Bitmap gdib(outW, outH, outW * 4, PixelFormat32bppRGB,
                             (BYTE*)outBits);
        if (gdib.GetLastStatus() == Gdiplus::Ok) {
            IStream* stream = NULL;
            if (CreateStreamOnHGlobal(NULL, TRUE, &stream) == S_OK && stream) {
                UINT nCodec = 0, csz = 0;
                bool saved = false;
                if (Gdiplus::GetImageEncodersSize(&nCodec, &csz) == Gdiplus::Ok && csz) {
                    std::vector<BYTE> cbuf(csz);
                    Gdiplus::ImageCodecInfo* codec = (Gdiplus::ImageCodecInfo*)cbuf.data();
                    if (Gdiplus::GetImageEncoders(nCodec, csz, codec) == Gdiplus::Ok) {
                        for (UINT i = 0; i < nCodec && !saved; ++i) {
                            if (wcscmp(codec[i].MimeType, L"image/png") != 0) continue;
                            saved = (gdib.Save(stream, &codec[i].Clsid, NULL) ==
                                     Gdiplus::Ok);
                        }
                    }
                }
                if (saved) {
                    LARGE_INTEGER zero;
                    zero.QuadPart = 0;
                    stream->Seek(zero, STREAM_SEEK_SET, NULL);
                    STATSTG st;
                    memset(&st, 0, sizeof(st));
                    if (stream->Stat(&st, STATFLAG_NONAME) == S_OK &&
                        st.cbSize.QuadPart > 0 && st.cbSize.QuadPart < (1LL << 30)) {
                        png.resize((size_t)st.cbSize.QuadPart);
                        ULONG got = 0;
                        if (stream->Read(&png[0], (ULONG)png.size(), &got) != S_OK)
                            got = 0;
                        png.resize((size_t)got);
                        if (png.empty()) ok = false;
                    } else {
                        ok = false;
                    }
                } else {
                    ok = false;
                }
                stream->Release();
            } else {
                ok = false;
            }
        } else {
            ok = false;
        }
    }
    DeleteObject(dib);
    if (!ok) png.clear();
    return ok;
}
// 截目标窗口（屏幕外的语音输入小面板会尽量补上）
static bool CaptureWindowPng(HWND hwnd, const RECT* voice, std::string& png) {
    png.clear();
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    RECT wr = {0, 0, 0, 0};
    if (!GetWindowRect(hwnd, &wr)) return false;
    long long ww = (long long)wr.right - wr.left;
    long long wh = (long long)wr.bottom - wr.top;
    if (ww <= 0 || wh <= 0 || ww > 16384 || wh > 16384) return false;

    int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (sw <= 0 || sh <= 0) {
        sx = sy = 0;
        sw = GetSystemMetrics(SM_CXSCREEN);
        sh = GetSystemMetrics(SM_CYSCREEN);
    }

    // 只拍“窗口 ∪ 语音面板”，夹到屏幕以内
    RECT r = wr;
    if (voice) {
        if (voice->left   < r.left)   r.left   = voice->left;
        if (voice->top    < r.top)    r.top    = voice->top;
        if (voice->right  > r.right)  r.right  = voice->right;
        if (voice->bottom > r.bottom) r.bottom = voice->bottom;
    }
    if (r.left   < sx) r.left   = sx;
    if (r.top    < sy) r.top    = sy;
    if (r.right  > sx + sw) r.right  = sx + sw;
    if (r.bottom > sy + sh) r.bottom = sy + sh;
    long long iw = (long long)r.right - r.left;
    long long ih = (long long)r.bottom - r.top;
    if (iw <= 0 || ih <= 0) return false;

    bool onScreen = (iw * 2 >= ww && ih * 2 >= wh);
    int wantW = onScreen ? (int)std::min<long long>(iw, ww)
                         : (int)std::min<long long>(ww, sw);
    int wantH = onScreen ? (int)std::min<long long>(ih, wh)
                         : (int)std::min<long long>(wh, sh);
    if (wantW <= 0 || wantH <= 0) return false;

    ShotCtx c;
    if (!ShotBegin(c)) return false;
    bool got = false;
    if (onScreen) {
        // 窗口大部分还在屏幕上：截真实屏幕内容（抓屏范围已经并上了语音面板）
        got = BitBlt(c.mdc, 0, 0, wantW, wantH, c.sdc, r.left, r.top, SRCCOPY) != FALSE;
    } else {
        // 窗口基本不在屏幕上（贴边 / 被挡住）：让窗口自己渲染一份
        HDC windowDc = GetWindowDC(hwnd);
        if (windowDc) {
            got = PrintWindow(hwnd, c.mdc, 2 /* PW_RENDERFULLCONTENT */) != FALSE;
            ReleaseDC(hwnd, windowDc);
        }
    }
    bool ok = got && ShotEncodePng(c, wantW, wantH, png);
    ShotCleanup(c);
    return ok;
}
// 挑“正在输入文字”的窗口：前台 → 鼠标下 → 最后一个前台（语音输入面板一律跳过）
static HWND PickInputWindow() {
    HWND fg = GetForegroundWindow();
    if (fg && !IsVoiceHostWindow(fg) && IsUsableForegroundWindow(fg) &&
        !IsIconic(fg)) {
        wchar_t cls[128] = {0};
        GetClassNameW(fg, cls, 127);
        if (wcscmp(cls, L"Progman") != 0 && wcscmp(cls, L"WorkerW") != 0)
            return fg;
    }
    POINT pt;
    if (GetCursorPos(&pt)) {
        HWND h = WindowFromPoint(pt);
        for (int i = 0; i < 8 && h; ++i) {
            HWND root = GetAncestor(h, GA_ROOT);
            if (!root || root == h) break;
            h = root;
        }
        if (h && !IsVoiceHostWindow(h) && IsUsableForegroundWindow(h) &&
            !IsIconic(h))
            return h;
    }
    HWND last = g_lastForeground;
    if (last && !IsVoiceHostWindow(last) && IsUsableForegroundWindow(last))
        return last;
    return NULL;
}
// 语音输入面板所在的屏幕区域（找不到就返回 false）
static bool VoiceHostRect(RECT& out) {
    std::vector<HWND> wins;
    EnumWindows(VoiceHostTopWindowProc, reinterpret_cast<LPARAM>(&wins));
    long long best = 0;
    bool got = false;
    for (size_t i = 0; i < wins.size(); ++i) {
        RECT rc = {0, 0, 0, 0};
        if (!GetWindowRect(wins[i], &rc)) continue;
        long long a = ((long long)rc.right - rc.left) *
                      ((long long)rc.bottom - rc.top);
        if (a <= 0) continue;
        if (!got || a < best) {          // 面板是最小的那个 TextInputHost 窗口
            best = a;
            out  = rc;
            got  = true;
        }
    }
    return got;
}
// 拍一张“输入框”的图（PNG 字节）
static bool CaptureInputWindowPng(std::string& png) {
    png.clear();
    RECT voice = {0, 0, 0, 0};
    bool hasVoice = VoiceHostRect(voice);
    HWND h = PickInputWindow();
    if (h && CaptureWindowPng(h, hasVoice ? &voice : NULL, png)) return true;
    h = g_lastForeground;
    if (h && !IsVoiceHostWindow(h) && CaptureWindowPng(h, hasVoice ? &voice : NULL, png))
        return true;
    if (hasVoice) {
        // 实在挑不到输入窗口：至少让用户亲眼看到语音输入框里转出来的字
        std::string fallback;
        if (CaptureWindowPng(GetForegroundWindow(), NULL, fallback)) {
            png.swap(fallback);
            return true;
        }
    }
    return false;
}



// ------------------------------------------------------------
// 语音输入是否正在收音：手机上的红/白话筒靠它和电脑同步。
// 面板在 TextInputHost 的 XAML 里叫 FloatyPanel / DictationFloaty，
// 里面有一个 MicrophoneButton，它的 Toggle 状态就是“收音 / 暂停”。
// 找不到面板 = 没在收音。
// ------------------------------------------------------------
static IUIAutomationElement* FindByAutomationId(IUIAutomation* pAuto,
        IUIAutomationElement* pRoot, const wchar_t* id) {
    if (!pAuto || !pRoot || !id) return NULL;
    IUIAutomationElement* pEl = NULL;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(id);
    IUIAutomationCondition* pCond = NULL;
    if (SUCCEEDED(pAuto->CreatePropertyCondition(
            UIA_AutomationIdPropertyId, v, &pCond)) && pCond) {
        pRoot->FindFirst(TreeScope_Descendants, pCond, &pEl);
        pCond->Release();
    }
    VariantClear(&v);
    return pEl;      // 调用方负责 Release
}
static IUIAutomationElement* FindByNameProp(IUIAutomation* pAuto,
        IUIAutomationElement* pRoot, const wchar_t* name) {
    if (!pAuto || !pRoot || !name) return NULL;
    IUIAutomationElement* pEl = NULL;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(name);
    IUIAutomationCondition* pCond = NULL;
    if (SUCCEEDED(pAuto->CreatePropertyCondition(
            UIA_NamePropertyId, v, &pCond)) && pCond) {
        pRoot->FindFirst(TreeScope_Descendants, pCond, &pEl);
        pCond->Release();
    }
    VariantClear(&v);
    return pEl;
}
// 只认屏幕上真实可见的元素：面板收起后 XAML 常把元素留在树里，
// 这时 IsOffscreen 为真 / 尺寸为 0，必须排除，否则会一直判成“在收音”。
static bool ElementOnScreen(IUIAutomationElement* el) {
    if (!el) return false;
    BOOL off = FALSE;
    if (SUCCEEDED(el->get_CurrentIsOffscreen(&off)) && off) return false;
    RECT rc = {0, 0, 0, 0};
    if (SUCCEEDED(el->get_CurrentBoundingRectangle(&rc))) {
        if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return false;
    }
    return true;
}
static bool WindowVoiceState(IUIAutomation* pAuto, HWND hwnd, bool& found) {
    found = false;
    IUIAutomationElement* pRoot = NULL;
    if (FAILED(pAuto->ElementFromHandle(hwnd, &pRoot)) || !pRoot) return false;
    bool listening = false;
    IUIAutomationElement* pPanel = FindByAutomationId(pAuto, pRoot, L"FloatyPanel");
    if (!pPanel) pPanel = FindByAutomationId(pAuto, pRoot, L"DictationFloaty");
    if (pPanel && !ElementOnScreen(pPanel)) {
        pPanel->Release();
        pPanel = NULL;
    }
    if (pPanel) {
        found = true;
        listening = true;
        IUIAutomationElement* pMic = FindByAutomationId(pAuto, pPanel, L"MicrophoneButton");
        if (pMic && !ElementOnScreen(pMic)) {
            pMic->Release();
            pMic = NULL;
        }
        if (pMic) {
            IUIAutomationTogglePattern* pTog = NULL;
            if (SUCCEEDED(pMic->GetCurrentPattern(UIA_TogglePatternId,
                                                  (IUnknown**)&pTog)) && pTog) {
                ToggleState st = ToggleState_Indeterminate;
                if (SUCCEEDED(pTog->get_CurrentToggleState(&st)))
                    listening = (st != ToggleState_Off);
                pTog->Release();
            }
            pMic->Release();
        }
        pPanel->Release();
    } else {
        IUIAutomationElement* pClose =
            FindByAutomationId(pAuto, pRoot, L"CloseButton");
        if (pClose && !ElementOnScreen(pClose)) {
            pClose->Release();
            pClose = NULL;
        }
        if (!pClose) {
            static const wchar_t* kCloseNames[] = {
                L"关闭", L"Close", L"关闭语音输入", L"关闭听写", L"Dismiss"
            };
            for (int i = 0; i < 5 && !pClose; ++i) {
                pClose = FindByNameProp(pAuto, pRoot, kCloseNames[i]);
                if (pClose && !ElementOnScreen(pClose)) {
                    pClose->Release();
                    pClose = NULL;
                }
            }
        }
        if (pClose) {
            found = true;
            listening = true;
            pClose->Release();
        }
    }
    pRoot->Release();
    return listening;
}
struct VoiceListenCtx {
    IUIAutomation* pAuto;
    bool detected;
    bool listening;
};
static BOOL CALLBACK VoiceListenProc(HWND h, LPARAM lp) {
    VoiceListenCtx* c = reinterpret_cast<VoiceListenCtx*>(lp);
    if (!c || c->detected) return FALSE;
    if (!IsWindowVisible(h)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    if (!ProcessIsTextInputHost(pid)) return TRUE;
    bool found = false;
    bool listening = WindowVoiceState(c->pAuto, h, found);
    if (found) {
        c->detected = true;
        c->listening = listening;
        return FALSE;
    }
    return TRUE;
}
static bool VoiceTypingListeningNow() {
    bool detected = false;
    bool listening = false;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IUIAutomation* pAuto = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, NULL,
                                   CLSCTX_INPROC_SERVER,
                                   IID_IUIAutomation, (void**)&pAuto)) && pAuto) {
        VoiceListenCtx ctx;
        ctx.pAuto = pAuto;
        ctx.detected = false;
        ctx.listening = false;
        EnumWindows(VoiceListenProc, reinterpret_cast<LPARAM>(&ctx));
        detected = ctx.detected;
        listening = ctx.listening;
        pAuto->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();

    // 音频采集是更直接的“正在收音”信号：和 UIA 任一为真就算收音。
    // 停止收音后采集会话变 Inactive，配合上面的 UIA 就能判成“已关闭”。
    bool captured = MicCapturedByTextInputHost();
    if (captured) {
        detected = true;
        listening = true;
    }
    if (detected) InterlockedExchange(&g_voiceKnown, 1);
    return detected ? listening : false;
}
static bool VoiceTypingKnown() {
    return InterlockedCompareExchange(&g_voiceKnown, 0, 0) == 1;
}

// 【新增】三指左/右滑：直接切换窗口
//   backward = true  → Alt+Shift+Tab（前一个窗口）
//   backward = false → Alt+Tab      （后一个窗口）
//   与 SendWinD 一样：如果 Ctrl 处于锁定状态，先临时抬起再按回，
//   否则 Ctrl+Alt+Tab 会变成“任务视图”而不是普通窗口切换。
static void SendAltTab(bool backward) {
    bool ctrlWasLocked = (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1);
    INPUT in[8];
    memset(in, 0, sizeof(in));
    int n = 0;
    if (ctrlWasLocked) {
        in[n].type       = INPUT_KEYBOARD;
        in[n].ki.wVk     = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        n++;
    }
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_MENU; n++;   // Alt 按下
    if (backward) {
        in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_SHIFT; n++;  // Shift 按下
    }
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_TAB; n++;        // Tab 按下
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_TAB;
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;                        // Tab 抬起
    if (backward) {
        in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_SHIFT;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;                    // Shift 抬起
    }
    in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_MENU;
    in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;                        // Alt 抬起
    if (ctrlWasLocked) {
        in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_CONTROL; n++; // 恢复 Ctrl 锁定
    }
    SendInput(n, in, sizeof(INPUT));
    if (ctrlWasLocked) InterlockedExchange(&g_ctrlDown, 1);
}
static void SendAltTabPrev() { SendAltTab(true);  }   // 前一个窗口
static void SendAltTabNext() { SendAltTab(false); }   // 后一个窗口

// ============================================================
// 【改动】切换应用（循环切换「所有」窗口）
//   · 候选 = 所有可见、无主、非工具、有标题的顶层窗口（不含桌面/任务栏/自己），
//     包含已经最小化到任务栏的窗口，EnumWindows 天然按 Z 序从上到下返回；
//   · 每一轮在里面找当前前台窗口的位置，然后往后挪一格并激活；
//     目标窗口如果是最小化的，先还原再置前；
//   · 2.5 秒内连续点击沿用同一份快照，所以能一圈一圈按顺序循环（转一圈包含最小化窗口）；
//   · 激活用 AttachThreadInput + 轻敲 ALT 解除前台锁定，失败再退回合成 Alt+Tab。
// ============================================================
namespace {

BOOL CALLBACK CollectSwitchWindowsProc(HWND hwnd, LPARAM param) {
    std::vector<HWND>* out = reinterpret_cast<std::vector<HWND>*>(param);
    if (!out) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;          // 跳过对话框 / 工具窗
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) != 0) return TRUE;               // 跳过工具窗口
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;            // 跳过无标题窗口
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
        wcscmp(cls, L"Shell_TrayWnd") == 0 ||
        wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
        return TRUE;                                             // 跳过桌面 / 任务栏
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;               // 跳过 Tap 自己
    out->push_back(hwnd);
    return TRUE;
}

std::vector<HWND> CollectSwitchTopLevel() {
    std::vector<HWND> result;
    EnumWindows(CollectSwitchWindowsProc, reinterpret_cast<LPARAM>(&result));
    return result;
}

// 「切换应用」的候选列表
// 【改动】现在不再排除最小化窗口：任务栏上最小化的窗口也要参与循环切换，
//   所以候选列表就是全部可切换窗口（顺序仍是 EnumWindows 的 Z 序）。
std::vector<HWND> CollectSwitchCandidates() {
    return CollectSwitchTopLevel();
}

// 把窗口带到前台。SetForegroundWindow 受前台锁定限制，这里用 AttachThreadInput +
// 轻敲 ALT 解除限制；仍失败则退回合成 Alt+Tab，保证能切过去。
void ActivateSwitchWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    // 【改动】最小化窗口（任务栏上的窗口）要先还原，并且等它真的退出最小化状态：
    //   ShowWindowAsync 是异步的，紧接着的 SetForegroundWindow 会被系统忽略，
    //   结果就是“点了没反应、窗口还留在任务栏”，所以这里补一个短等待（最多 120ms）。
    if (IsIconic(hwnd)) {
        ShowWindowAsync(hwnd, SW_RESTORE);
        for (int i = 0; i < 12 && IsIconic(hwnd); ++i) Sleep(10);
    }

    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD myThread = GetCurrentThreadId();
    bool attached = false;
    if (fgThread && fgThread != myThread) {
        attached = AttachThreadInput(myThread, fgThread, TRUE) != 0;
    }
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (attached) AttachThreadInput(myThread, fgThread, FALSE);
    if (GetForegroundWindow() == hwnd) return;

    // 兜底：合成一次 Alt+Tab
    INPUT in[4];
    memset(in, 0, sizeof(in));
    for (int i = 0; i < 4; ++i) in[i].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_MENU;
    in[1].ki.wVk = VK_TAB;
    in[2].ki.wVk = VK_TAB;
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_MENU;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

// 循环切换用的窗口快照：连续点击在同一个快照里往后走，2.5 秒没有点击就重新取。
std::vector<HWND> g_switchCycle;
int                g_switchPos = -1;
ULONGLONG          g_switchAt  = 0;

void SwitchWindow(bool previous) {
    const ULONGLONG now = GetTickCount64();
    bool rebuild = g_switchCycle.empty() || now - g_switchAt > 2500;
    if (!rebuild) {
        for (size_t i = 0; i < g_switchCycle.size(); ++i) {
            if (!g_switchCycle[i] || !IsWindow(g_switchCycle[i])) { rebuild = true; break; }
        }
    }
    if (rebuild) {
        g_switchCycle = CollectSwitchCandidates();
        g_switchPos = -1;
        if (g_switchCycle.empty()) return;
        HWND fg = GetForegroundWindow();
        for (size_t i = 0; i < g_switchCycle.size(); ++i) {
            if (g_switchCycle[i] == fg) { g_switchPos = (int)i; break; }
        }
        if (g_switchPos < 0) g_switchPos = 0;
    }
    const int n = (int)g_switchCycle.size();
    if (n <= 0) return;
    const int step = previous ? -1 : 1;
    g_switchPos = ((g_switchPos + step) % n + n) % n;
    g_switchAt = now;
    ActivateSwitchWindow(g_switchCycle[g_switchPos]);
}

}  // namespace

// 手机端「切换应用」图标：Ctrl 锁定时先临时抬起，
// 否则合成的 Alt 会变成 Ctrl+Alt+Tab（任务视图），切不到普通窗口。
static void SendSwitchApp() {
    bool ctrlWasLocked = (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1);
    if (ctrlWasLocked) {
        INPUT up;
        memset(&up, 0, sizeof(up));
        up.type       = INPUT_KEYBOARD;
        up.ki.wVk     = VK_CONTROL;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &up, sizeof(INPUT));
    }
    SwitchWindow(false);
    if (ctrlWasLocked) {
        INPUT down;
        memset(&down, 0, sizeof(down));
        down.type   = INPUT_KEYBOARD;
        down.ki.wVk = VK_CONTROL;
        SendInput(1, &down, sizeof(INPUT));
        InterlockedExchange(&g_ctrlDown, 1);
    }
}

static void CtrlDown() {
    if (InterlockedCompareExchange(&g_ctrlDown, 1, 0) == 0) {
        INPUT in;
        memset(&in, 0, sizeof(in));
        in.type    = INPUT_KEYBOARD;
        in.ki.wVk  = VK_CONTROL;
        SendInput(1, &in, sizeof(INPUT));
    }
}
static void CtrlUp() {
    if (InterlockedCompareExchange(&g_ctrlDown, 0, 1) == 1) {
        INPUT in;
        memset(&in, 0, sizeof(in));
        in.type       = INPUT_KEYBOARD;
        in.ki.wVk     = VK_CONTROL;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }
}

// ============================================================
// 收发封装（纯 HTTP）
// ============================================================
static bool SendAll(SOCKET s, const void* data, size_t len) {
    const char* p = (const char*)data;
    size_t sent = 0;
    while (sent < len) {
        int n = send(s, p + sent, (int)(len - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}
static int ConnRecv(SOCKET s, char* buf, int len, int flags = 0) {
    return recv(s, buf, len, flags);
}
static void ConnShutdown(SOCKET s) {
    shutdown(s, SD_SEND);
}
static void ConnClose(SOCKET s) {
    closesocket(s);
}

// ============================================================
// HTTP 服务器
// ============================================================
static SOCKET        g_srv    = INVALID_SOCKET;
static volatile LONG g_stop   = 0;
static HANDLE        g_thread = NULL;
static void SendResponse(SOCKET s, const char* status,
                         const std::string& body, const char* ctype) {
    char hdr[512];
    sprintf(hdr,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        status, ctype, (unsigned)body.size());
    SendAll(s, hdr, strlen(hdr));
    if (!body.empty()) SendAll(s, body.data(), body.size());
}
static void ParseQuery(const std::string& q,
                       std::map<std::string, std::string>& out) {
    size_t i = 0;
    while (i < q.size()) {
        size_t amp = q.find('&', i);
        if (amp == std::string::npos) amp = q.size();
        std::string pair = q.substr(i, amp - i);
        size_t eq = pair.find('=');
        if (eq != std::string::npos)
            out[UrlUnescape(pair.substr(0, eq))] = UrlUnescape(pair.substr(eq + 1));
        else
            out[UrlUnescape(pair)] = "";
        i = amp + 1;
    }
}

// ============================================================
// 【新增】读取请求头字段（大小写不敏感）
// ============================================================
static std::string GetHeaderValue(const std::string& req, const char* name) {
    size_t nameLen = strlen(name);
    size_t pos = 0;
    while (pos < req.size()) {
        size_t lineEnd = req.find("\r\n", pos);
        if (lineEnd == std::string::npos) lineEnd = req.size();
        std::string ln = req.substr(pos, lineEnd - pos);
        if (ln.size() > nameLen && ln[nameLen] == ':') {
            bool match = true;
            for (size_t i = 0; i < nameLen; ++i) {
                char a = ln[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) {
                size_t v = nameLen + 1;
                while (v < ln.size() && (ln[v] == ' ' || ln[v] == '\t')) ++v;
                return ln.substr(v);
            }
        }
        if (lineEnd == req.size()) break;
        pos = lineEnd + 2;
    }
    return std::string();
}

// ============================================================
// 【新增】文件夹 → ZIP 实时打包
// ------------------------------------------------------------
//  问题: 电脑剪贴板里是一个文件夹时，手机网页只能看到一个 📁，
//        点不了、也拿不到里面的东西 —— 浏览器（尤其 iOS Safari）
//        根本没有“下载一个文件夹”这种能力。
//  方案: 由电脑端（服务端）把文件夹连同所有子目录实时打成
//        单个 .zip 再发给手机，手机一次点击就能拿到全部内容，
//        在 iPhone 上用“文件”App 解压缩即可还原整个文件夹。
//  要点:
//    1) 只用 STORE（不压缩）：边读边发、不吃 CPU，内容与磁盘
//       完全一致（照片/视频/压缩包本来也压不动）。
//    2) 先枚举一遍算出 zip 的精确字节数，才能给出 Content-Length
//       —— iOS 的下载管理器只认精确长度，长度不对就一直转圈。
//    3) 条目用 UTF-8 文件名 + 语言编码标志位(bit 11)，中文名不乱码。
//    4) 超过 4GB / 5 万项（标准 zip 字段上限）时明确报错，不做半截。
// ============================================================
#define ZIP_MAX_BYTES    0xFFFF0000LL   // 单个文件与整包都不超过 ~4GB
#define ZIP_MAX_ENTRIES  50000

static unsigned int g_crcTab[256];
static bool         g_crcTabReady = false;

static void Crc32Init() {
    for (unsigned int i = 0; i < 256; ++i) {
        unsigned int c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crcTab[i] = c;
    }
    g_crcTabReady = true;
}
// 传入上一次的返回值即可连续计算；返回值为最终 CRC32（已取反）
static unsigned int Crc32Update(unsigned int crc, const void* data, size_t len) {
    if (!g_crcTabReady) Crc32Init();
    const unsigned char* p = (const unsigned char*)data;
    crc = ~crc;
    for (size_t i = 0; i < len; ++i)
        crc = g_crcTab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void ZipPut16(std::string& b, unsigned int v) {
    b += (char)(v & 0xFF);
    b += (char)((v >> 8) & 0xFF);
}
static void ZipPut32(std::string& b, unsigned int v) {
    b += (char)(v & 0xFF);
    b += (char)((v >> 8) & 0xFF);
    b += (char)((v >> 16) & 0xFF);
    b += (char)((v >> 24) & 0xFF);
}

struct ZipEntry {
    std::wstring diskPath;   // 磁盘完整路径（目录条目用不到）
    std::string  arcName;    // zip 内部路径（UTF-8，'/' 分隔，目录以 '/' 结尾）
    long long    size;       // 文件字节数（目录为 0）
    bool         isDir;
    WORD         dosDate;
    WORD         dosTime;
};

static void ZipDosTime(const std::wstring& p, WORD& d, WORD& t) {
    d = 0x0021; t = 0;                       // 兜底：1980-01-01
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return;
    FILETIME local;
    if (!FileTimeToLocalFileTime(&fad.ftLastWriteTime, &local)) return;
    FileTimeToDosDateTime(&local, &d, &t);
}

// 递归收集一个磁盘路径（文件或文件夹）下的全部条目
static void ZipCollect(const std::wstring& diskPath, const std::string& arcBase,
                       std::vector<ZipEntry>& out, long long& total,
                       bool& overflow, int depth) {
    if (overflow) return;
    if ((int)out.size() >= ZIP_MAX_ENTRIES || depth > 64) { overflow = true; return; }

    DWORD attr = GetFileAttributesW(diskPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    // 符号链接 / 联结点：跳过，避免绕圈或越出文件夹
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return;

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        ZipEntry de;
        de.diskPath = diskPath;
        de.arcName  = arcBase + "/";
        de.size     = 0;
        de.isDir    = true;
        ZipDosTime(diskPath, de.dosDate, de.dosTime);
        out.push_back(de);

        std::wstring pattern = diskPath;
        if (!pattern.empty() && pattern[pattern.size() - 1] != L'\\') pattern += L'\\';
        pattern += L'*';
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;   // 没权限读：留一个空目录条目
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            if (overflow) break;
            if ((int)out.size() >= ZIP_MAX_ENTRIES) { overflow = true; break; }

            std::wstring child = diskPath;
            if (!child.empty() && child[child.size() - 1] != L'\\') child += L'\\';
            child += fd.cFileName;
            std::string childArc = arcBase + "/" + WideToUtf8(fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ZipCollect(child, childArc, out, total, overflow, depth + 1);
                continue;
            }
            // 枚举阶段就试开一次：打不开的文件直接跳过，
            // 否则发到一半发现读不了，Content-Length 就对不上了
            HANDLE h = CreateFileW(child.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) continue;
            LARGE_INTEGER li; li.QuadPart = 0;
            GetFileSizeEx(h, &li);
            CloseHandle(h);
            if (li.QuadPart < 0 || li.QuadPart > ZIP_MAX_BYTES) { overflow = true; break; }

            ZipEntry fe;
            fe.diskPath = child;
            fe.arcName  = childArc;
            fe.size     = (long long)li.QuadPart;
            fe.isDir    = false;
            ZipDosTime(child, fe.dosDate, fe.dosTime);
            out.push_back(fe);
            total += fe.size;
            if (total > ZIP_MAX_BYTES) { overflow = true; break; }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        return;
    }

    // 顶层就是一个普通文件
    HANDLE h = CreateFileW(diskPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER li; li.QuadPart = 0;
    GetFileSizeEx(h, &li);
    CloseHandle(h);
    if (li.QuadPart < 0 || li.QuadPart > ZIP_MAX_BYTES) { overflow = true; return; }

    ZipEntry fe;
    fe.diskPath = diskPath;
    fe.arcName  = arcBase;
    fe.size     = (long long)li.QuadPart;
    fe.isDir    = false;
    ZipDosTime(diskPath, fe.dosDate, fe.dosTime);
    out.push_back(fe);
    total += fe.size;
    if (total > ZIP_MAX_BYTES) overflow = true;
}

static bool ZipEntryLess(const ZipEntry& a, const ZipEntry& b) {
    return a.arcName < b.arcName;
}

// 把若干磁盘路径（文件或文件夹）收集成一张 zip 条目表
static bool ZipBuildList(const std::vector<std::wstring>& roots,
                         std::vector<ZipEntry>& out, long long& total,
                         bool& overflow) {
    total = 0;
    overflow = false;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (overflow) break;
        const std::wstring& p = roots[i];
        const wchar_t* np = wcsrchr(p.c_str(), L'\\');
        if (!np) np = wcsrchr(p.c_str(), L'/');
        np = np ? np + 1 : p.c_str();
        if (!np || !*np) continue;
        ZipCollect(p, WideToUtf8(np), out, total, overflow, 0);
    }
    std::sort(out.begin(), out.end(), ZipEntryLess);
    return !out.empty() && !overflow;
}

// zip 的精确总字节数（STORE + 数据描述符，全部可预知）
static long long ZipArchiveSize(const std::vector<ZipEntry>& list) {
    long long n = 22;   // EOCD
    for (size_t i = 0; i < list.size(); ++i) {
        n += 30 + (long long)list[i].arcName.size() +
             (list[i].isDir ? 0 : list[i].size) + 16;   // 本地头 + 数据 + 描述符
        n += 46 + (long long)list[i].arcName.size();    // 中央目录项
    }
    return n;
}

// RFC5987 文件名编码（中文名等可正确显示），与文件下载保持一致的规则
static void BuildAttachmentNames(const std::string& utf8Name,
                                 std::string& simpleName, std::string& enc) {
    for (size_t i = 0; i < utf8Name.size(); ++i) {
        unsigned char c = (unsigned char)utf8Name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '~') {
            enc += (char)c;
        } else {
            char b[8]; sprintf(b, "%%%02X", c); enc += b;
        }
    }
    for (size_t i = 0; i < utf8Name.size(); ++i) {
        unsigned char c = (unsigned char)utf8Name[i];
        if (c < 0x20 || c == 0x7F || c >= 0x80 || c == '"' || c == '\\')
            simpleName += '_';
        else
            simpleName += (char)c;
    }
    if (simpleName.empty()) simpleName = "download.zip";
}

// 实时打 zip 并发给客户端
static void SendZipResponse(SOCKET cli, const std::vector<std::wstring>& roots,
                            const std::string& zipNameUtf8, const std::string& method) {
    std::vector<ZipEntry> list;
    long long total = 0;
    bool overflow = false;
    ZipBuildList(roots, list, total, overflow);

    if (overflow) {
        SendResponse(cli, "413 Payload Too Large",
                     "Too large to zip (limit 4GB / 50000 items)", "text/plain");
        return;
    }
    if (list.empty()) {
        SendResponse(cli, "404 Not Found", "Nothing to zip", "text/plain");
        return;
    }
    long long archiveSize = ZipArchiveSize(list);
    if (archiveSize > ZIP_MAX_BYTES) {
        SendResponse(cli, "413 Payload Too Large", "Archive too large", "text/plain");
        return;
    }

    std::string simpleName, enc;
    BuildAttachmentNames(zipNameUtf8.empty() ? std::string("download.zip") : zipNameUtf8,
                         simpleName, enc);

    char hdr[2048];
    _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/zip\r\n"
        "Content-Length: %lld\r\n"
        "Content-Disposition: attachment; filename=\"%s\"; filename*=UTF-8''%s\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        archiveSize, simpleName.c_str(), enc.c_str());
    if (method == "HEAD") {
        SendAll(cli, hdr, strlen(hdr));
        ConnShutdown(cli);
        return;
    }
    if (!SendAll(cli, hdr, strlen(hdr))) return;

    std::string central;
    central.reserve((size_t)std::min<long long>(archiveSize, 8 << 20));
    char buf[16384];
    long long offset = 0;

    for (size_t i = 0; i < list.size(); ++i) {
        const ZipEntry& e = list[i];
        long long dataSize = e.isDir ? 0 : e.size;

        std::string lh;
        ZipPut32(lh, 0x04034B50);
        ZipPut16(lh, 20);            // 需要的解压版本 2.0
        ZipPut16(lh, 0x0808);        // bit3 数据描述符 + bit11 UTF-8 文件名
        ZipPut16(lh, 0);             // 压缩方法：0 = STORE
        ZipPut16(lh, e.dosTime);
        ZipPut16(lh, e.dosDate);
        ZipPut32(lh, 0);             // crc 放在数据描述符里
        ZipPut32(lh, 0);             // 压缩后大小
        ZipPut32(lh, 0);             // 原始大小
        ZipPut16(lh, (unsigned int)e.arcName.size());
        ZipPut16(lh, 0);             // extra
        lh += e.arcName;
        if (!SendAll(cli, lh.data(), lh.size())) return;

        unsigned int crc = 0;
        long long written = 0;
        if (dataSize > 0) {
            HANDLE h = CreateFileW(e.diskPath.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                long long remaining = dataSize;
                DWORD rd;
                while (remaining > 0 &&
                       ReadFile(h, buf,
                                (DWORD)std::min<long long>((long long)sizeof(buf), remaining),
                                &rd, NULL) && rd > 0) {
                    if (!SendAll(cli, buf, rd)) { CloseHandle(h); return; }
                    crc = Crc32Update(crc, buf, rd);
                    remaining -= rd;
                    written += rd;
                }
                CloseHandle(h);
            }
            // 文件在枚举之后被改小/占用了：补 0 凑满承诺的字节数，
            // 这样 Content-Length 一分不差、zip 结构依然完整
            if (written < dataSize) {
                memset(buf, 0, sizeof(buf));
                long long pad = dataSize - written;
                while (pad > 0) {
                    DWORD n = (DWORD)std::min<long long>((long long)sizeof(buf), pad);
                    if (!SendAll(cli, buf, n)) return;
                    crc = Crc32Update(crc, buf, n);
                    pad -= n;
                }
            }
        }

        std::string dd;
        ZipPut32(dd, 0x08074B50);            // 数据描述符签名
        ZipPut32(dd, crc);
        ZipPut32(dd, (unsigned int)dataSize);
        ZipPut32(dd, (unsigned int)dataSize);
        if (!SendAll(cli, dd.data(), dd.size())) return;

        ZipPut32(central, 0x02014B50);
        ZipPut16(central, 20);               // 制作版本
        ZipPut16(central, 20);               // 需要的解压版本
        ZipPut16(central, 0x0808);
        ZipPut16(central, 0);
        ZipPut16(central, e.dosTime);
        ZipPut16(central, e.dosDate);
        ZipPut32(central, crc);
        ZipPut32(central, (unsigned int)dataSize);
        ZipPut32(central, (unsigned int)dataSize);
        ZipPut16(central, (unsigned int)e.arcName.size());
        ZipPut16(central, 0);                // extra
        ZipPut16(central, 0);                // 注释
        ZipPut16(central, 0);                // 起始磁盘号
        ZipPut16(central, 0);                // 内部属性
        ZipPut32(central, e.isDir ? 0x10u : 0x20u);   // 外部属性：目录/归档位
        ZipPut32(central, (unsigned int)offset);
        central += e.arcName;

        offset += (long long)lh.size() + dataSize + 16;
    }

    if (!SendAll(cli, central.data(), central.size())) return;

    std::string eocd;
    ZipPut32(eocd, 0x06054B50);
    ZipPut16(eocd, 0);
    ZipPut16(eocd, 0);
    ZipPut16(eocd, (unsigned int)list.size());
    ZipPut16(eocd, (unsigned int)list.size());
    ZipPut32(eocd, (unsigned int)central.size());
    ZipPut32(eocd, (unsigned int)offset);
    ZipPut16(eocd, 0);
    SendAll(cli, eocd.data(), eocd.size());

    // 与文件下载一样：主动半关闭，让浏览器收到干净的结束信号
    ConnShutdown(cli);
}

// 文件夹统计（大小 / 文件数），供手机端显示与判断能否打包
static bool ZipDirStats(const std::wstring& dir, long long& total,
                        long long& fileCount, bool& tooBig) {
    std::vector<std::wstring> roots;
    roots.push_back(dir);
    std::vector<ZipEntry> list;
    bool overflow = false;
    ZipBuildList(roots, list, total, overflow);
    tooBig = overflow;
    fileCount = 0;
    for (size_t i = 0; i < list.size(); ++i)
        if (!list[i].isDir) ++fileCount;
    return true;
}

// ============================================================
// 【新增】手机 → 电脑：上传文件 / 新建文本 写进电脑剪贴板
// ------------------------------------------------------------
//  · POST /clipboard/upload?name=<文件名>&new=1|0
//      请求体就是文件原始字节；先落到 %TEMP%\PhoneMouseUpload，
//      再把这一批文件设置成电脑的“文件剪贴板”(CF_HDROP)，
//      电脑上 Ctrl+V 即可粘贴到资源管理器 / 微信 / 邮件等。
//      new=1 表示这是本次多选的第一批，先清空上一批。
//  · POST /clipboard/settext
//      请求体是 UTF-8 文本，直接设置成电脑的文本剪贴板。
// ============================================================
#define MAX_UPLOAD_BYTES 0x80000000LL      // 单个文件上限 2GB
#define MAX_SETTEXT_BYTES (4*1024*1024LL)  // 新建文本上限 4MB

static std::wstring UploadDir() {
    if (!g_uploadDir.empty()) return g_uploadDir;
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    std::wstring d = (n > 0 && n < MAX_PATH)
                   ? std::wstring(tmp)
                   : std::wstring(L"C:\\Windows\\Temp\\");
    if (d.empty() || d[d.size() - 1] != L'\\') d += L'\\';
    d += L"PhoneMouseUpload";
    CreateDirectoryW(d.c_str(), NULL);
    g_uploadDir = d;
    return d;
}

// 去掉路径分隔符等非法字符，防止越写到别的目录
static std::wstring SanitizeUploadName(const std::wstring& in) {
    std::wstring out;
    for (size_t i = 0; i < in.size() && out.size() < 120; ++i) {
        wchar_t c = in[i];
        if (c < 32) continue;
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"'  || c == L'<' || c == L'>' || c == L'|') continue;
        out += c;
    }
    while (!out.empty() && (out[0] == L'.' || out[0] == L' ')) out.erase(0, 1);
    while (!out.empty() && out[out.size() - 1] == L' ')      out.erase(out.size() - 1);
    if (out.empty()) out = L"file";
    return out;
}

static std::wstring UniqueUploadPath(const std::wstring& dir, const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    std::wstring base = name, ext;
    if (dot != std::wstring::npos && dot > 0) {
        base = name.substr(0, dot);
        ext  = name.substr(dot);
    }
    for (int i = 0; i < 5000; ++i) {
        std::wstring cand;
        if (i == 0) {
            cand = dir + L"\\" + name;
        } else {
            wchar_t idx[32];
            _snwprintf_s(idx, 32, _TRUNCATE, L" (%d)", i);
            cand = dir + L"\\" + base + idx + ext;
        }
        if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) return cand;
    }
    return dir + L"\\" + name;
}

// 剪贴板可能被别的程序占着，多试几次
static bool OpenClipboardRetry() {
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(g_hwnd)) return true;
        Sleep(20);
    }
    return false;
}

// 把一组文件设置成电脑的“文件剪贴板”
static bool SetClipboardFiles(const std::vector<std::wstring>& files) {
    if (files.empty()) return false;

    size_t total = sizeof(DROPFILES);
    for (size_t i = 0; i < files.size(); ++i)
        total += (files[i].size() + 1) * sizeof(wchar_t);
    total += sizeof(wchar_t);   // 末尾双 0

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
    if (!hMem) return false;

    DROPFILES* df = (DROPFILES*)GlobalLock(hMem);
    if (!df) { GlobalFree(hMem); return false; }
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    wchar_t* p = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
    for (size_t i = 0; i < files.size(); ++i) {
        const std::wstring& f = files[i];
        memcpy(p, f.c_str(), f.size() * sizeof(wchar_t));
        p += f.size();
        *p++ = 0;
    }
    *p = 0;
    GlobalUnlock(hMem);

    if (!OpenClipboardRetry()) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    bool ok = (SetClipboardData(CF_HDROP, hMem) != NULL);
    if (!ok) GlobalFree(hMem);

    // 标成“复制”，避免资源管理器把这批文件当成剪切
    UINT prefFmt = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (prefFmt) {
        HGLOBAL hEff = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (hEff) {
            DWORD* pd = (DWORD*)GlobalLock(hEff);
            if (pd) {
                *pd = DROPEFFECT_COPY;
                GlobalUnlock(hEff);
                if (!SetClipboardData(prefFmt, hEff)) GlobalFree(hEff);
            } else {
                GlobalFree(hEff);
            }
        }
    }
    CloseClipboard();
    return ok;
}

// 把一段文本设置成电脑的文本剪贴板
static bool SetClipboardTextW(const std::wstring& text) {
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) return false;
    void* p = GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); return false; }
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(hMem);

    if (!OpenClipboardRetry()) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    bool ok = (SetClipboardData(CF_UNICODETEXT, hMem) != NULL);
    if (!ok) GlobalFree(hMem);
    CloseClipboard();
    return ok;
}

static void SendJson(SOCKET cli, const std::string& json) {
    SendResponse(cli, "200 OK", json, "application/json; charset=utf-8");
}

// 收完整个请求体（含已经读进 req 的那部分）
static bool ReadRequestBody(SOCKET cli, const std::string& req, size_t bodyStart,
                            long long contentLen, std::string& body) {
    body = req.substr(bodyStart);
    if ((long long)body.size() > contentLen) body.resize((size_t)contentLen);
    while ((long long)body.size() < contentLen) {
        char b[16384];
        int n = ConnRecv(cli, b,
                     (int)std::min<long long>((long long)sizeof(b),
                                              contentLen - (long long)body.size()), 0);
        if (n <= 0) return false;
        body.append(b, n);
    }
    return true;
}

// POST /clipboard/upload?name=...&new=1|0
static void HandleClipboardUpload(SOCKET cli, const std::string& req,
                                  std::map<std::string, std::string>& params) {
    size_t hdrEnd = req.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) {
        SendResponse(cli, "400 Bad Request", "Bad request", "text/plain");
        return;
    }
    hdrEnd += 4;

    std::string clStr = GetHeaderValue(req, "Content-Length");
    long long contentLen = clStr.empty() ? -1 : _strtoi64(clStr.c_str(), NULL, 10);
    if (contentLen < 0) {
        SendResponse(cli, "411 Length Required", "Content-Length required", "text/plain");
        return;
    }
    if (contentLen > MAX_UPLOAD_BYTES) {
        SendResponse(cli, "413 Payload Too Large", "File too large", "text/plain");
        return;
    }

    // 大文件上传期间放宽接收超时，免得被 3 秒的读超时掐断
    DWORD rcvTv = 30000;
    setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTv, sizeof(rcvTv));

    std::string nameUtf8 = params.count("name") ? params["name"] : std::string();
    std::wstring name = SanitizeUploadName(Utf8ToWide(nameUtf8));
    std::wstring dir  = UploadDir();
    std::wstring full = UniqueUploadPath(dir, name);

    HANDLE hFile = CreateFileW(full.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        SendResponse(cli, "500 Internal Server Error", "Cannot save file", "text/plain");
        return;
    }

    long long written = 0;
    bool ioOk = true;
    std::string rest = req.substr(hdrEnd);
    if (!rest.empty() && contentLen > 0) {
        DWORD take = (DWORD)std::min<long long>((long long)rest.size(), contentLen);
        DWORD w = 0;
        if (!WriteFile(hFile, rest.data(), take, &w, NULL) || w != take) ioOk = false;
        else written = (long long)w;
    }
    char buf[16384];
    while (ioOk && written < contentLen) {
        int n = ConnRecv(cli, buf,
                     (int)std::min<long long>((long long)sizeof(buf), contentLen - written), 0);
        if (n <= 0) { ioOk = false; break; }
        DWORD w = 0;
        if (!WriteFile(hFile, buf, (DWORD)n, &w, NULL) || (int)w != n) { ioOk = false; break; }
        written += (long long)w;
    }
    CloseHandle(hFile);

    if (!ioOk || written != contentLen) {
        DeleteFileW(full.c_str());
        SendResponse(cli, "400 Bad Request", "Upload incomplete", "text/plain");
        return;
    }

    bool isNew = (params.count("new") && params["new"] == "1");
    AcquireSRWLockExclusive(&g_uploadLock);
    if (isNew) g_uploadList.clear();
    g_uploadList.push_back(full);
    std::vector<std::wstring> listCopy = g_uploadList;
    ReleaseSRWLockExclusive(&g_uploadLock);

    bool clipOk = SetClipboardFiles(listCopy);

    // 立刻同步给页面：不等 WM_CLIPBOARDUPDATE 的回读，
    // 否则手机刷新列表时可能还读到旧内容
    AcquireSRWLockExclusive(&g_clipLock);
    g_clipFiles = listCopy;
    g_clipText.clear();
    g_clipIsCut = false;
    InterlockedIncrement(&g_clipVersion);
    ReleaseSRWLockExclusive(&g_clipLock);

    char cnt[32];
    sprintf(cnt, "%d", (int)listCopy.size());
    std::string json = "{\"ok\":true,\"name\":\"" + JsonEscapeW(name) + "\",\"count\":";
    json += cnt;
    json += ",\"clipboard\":";
    json += (clipOk ? "true" : "false");
    json += "}";
    SendJson(cli, json);
}

// POST /clipboard/settext
static void HandleClipboardSetText(SOCKET cli, const std::string& req) {
    size_t hdrEnd = req.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) {
        SendResponse(cli, "400 Bad Request", "Bad request", "text/plain");
        return;
    }
    hdrEnd += 4;

    std::string clStr = GetHeaderValue(req, "Content-Length");
    long long contentLen = clStr.empty() ? 0 : _strtoi64(clStr.c_str(), NULL, 10);
    if (contentLen < 0 || contentLen > MAX_SETTEXT_BYTES) {
        SendResponse(cli, "413 Payload Too Large", "Text too long", "text/plain");
        return;
    }

    std::string body;
    if (!ReadRequestBody(cli, req, hdrEnd, contentLen, body)) {
        SendResponse(cli, "400 Bad Request", "Upload incomplete", "text/plain");
        return;
    }

    std::wstring w = Utf8ToWide(body);
    bool ok = SetClipboardTextW(w);
    if (ok) {
        AcquireSRWLockExclusive(&g_clipLock);
        g_clipText  = w;
        g_clipFiles.clear();
        g_clipIsCut = false;
        InterlockedIncrement(&g_clipVersion);
        ReleaseSRWLockExclusive(&g_clipLock);
    }
    SendJson(cli, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ============================================================
// 【新增】文件下载响应
//   - 支持 HEAD 探测与 Range 分段（iOS Safari 下载前常先发 Range/HEAD）
//   - 始终携带精确 Content-Length + Accept-Ranges，便于 Safari 判定“下载完成”
//   - 响应发送完成后 shutdown(SD_SEND)，让浏览器收到干净的结束信号，
//     避免出现“进度条卡住 / 0 字节未完成(.download)”的情况
// ============================================================
static void SendFileResponse(SOCKET cli, const std::wstring& filePath,
                             const std::string& method,
                             const std::string& rangeHeader) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        SendResponse(cli, "404 Not Found", "File not found", "text/plain");
        return;
    }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hFile, &sz)) {
        CloseHandle(hFile);
        SendResponse(cli, "500 Internal Server Error", "Size error", "text/plain");
        return;
    }
    long long fileSize = (long long)sz.QuadPart;

    const wchar_t* namePtr = wcsrchr(filePath.c_str(), L'\\');
    if (!namePtr) namePtr = wcsrchr(filePath.c_str(), L'/');
    namePtr = namePtr ? namePtr + 1 : filePath.c_str();
    std::string utf8Name = WideToUtf8(namePtr);

    // RFC5987：filename* 使用百分号编码（中文名等可正确显示）
    std::string enc;
    for (size_t i = 0; i < utf8Name.size(); ++i) {
        unsigned char c = (unsigned char)utf8Name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '~') {
            enc += (char)c;
        } else {
            char b[8]; sprintf(b, "%%%02X", c); enc += b;
        }
    }
    // filename="..." 只保留 ASCII 可见字符，避免非 ASCII 字节让 Safari 解析异常
    std::string simpleName;
    for (size_t i = 0; i < utf8Name.size(); ++i) {
        unsigned char c = (unsigned char)utf8Name[i];
        if (c < 0x20 || c == 0x7F || c >= 0x80 || c == '"' || c == '\\') {
            simpleName += '_';
        } else {
            simpleName += (char)c;
        }
    }
    if (simpleName.empty()) simpleName = "download";

    // 解析 Range: bytes=start-end（Safari 下载前常先发 Range 探测）
    //   - 只认第一个区间：多区间按规范要回 multipart/byteranges，
    //     Safari 反而处理不好；回整份文件最稳
    //   - "bytes=0-" / "bytes=-500" 这类省略写法按 RFC7233 补齐
    long long start = 0, end = fileSize - 1;
    bool partial     = false;
    bool rangeParsed = false;   // Range 写对了、但区间落在文件外 → 416
    if (fileSize > 0 && !rangeHeader.empty()) {
        std::string spec = rangeHeader;
        while (!spec.empty() && (spec[0] == ' ' || spec[0] == '\t')) spec.erase(0, 1);
        if (spec.compare(0, 6, "bytes=") == 0) {
            spec = spec.substr(6);
            while (!spec.empty() && (spec[0] == ' ' || spec[0] == '\t')) spec.erase(0, 1);
            while (!spec.empty() &&
                   (spec[spec.size() - 1] == ' ' || spec[spec.size() - 1] == '\t'))
                spec.erase(spec.size() - 1);
            size_t comma = spec.find(',');
            if (comma != std::string::npos) spec = spec.substr(0, comma);
            size_t dash = spec.find('-');
            if (dash != std::string::npos) {
                std::string a = spec.substr(0, dash);
                std::string b = spec.substr(dash + 1);
                if (a.empty() && !b.empty()) {
                    // 后缀区间：最后 N 个字节
                    long long n = _strtoi64(b.c_str(), NULL, 10);
                    if (n > 0) {
                        rangeParsed = true;
                        if (n > fileSize) n = fileSize;
                        start = fileSize - n;
                        end   = fileSize - 1;
                        partial = true;
                    }
                } else if (!a.empty()) {
                    long long s = _strtoi64(a.c_str(), NULL, 10);
                    if (s < 0) s = 0;
                    rangeParsed = true;
                    long long e = fileSize - 1;
                    if (!b.empty()) {
                        e = _strtoi64(b.c_str(), NULL, 10);
                        if (e >= fileSize) e = fileSize - 1;
                    }
                    if (s < fileSize && s <= e) {
                        start = s;
                        end   = e;
                        partial = true;
                    }
                }
            }
        }
    }

    // 区间完全落在文件外：必须明确回 416，否则浏览器会一直等数据
    if (rangeParsed && !partial) {
        char h416[512];
        _snprintf_s(h416, sizeof(h416), _TRUNCATE,
            "HTTP/1.1 416 Range Not Satisfiable\r\n"
            "Content-Range: bytes */%lld\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 0\r\n"
            "Accept-Ranges: bytes\r\n"
            "Connection: close\r\n\r\n",
            fileSize);
        SendAll(cli, h416, strlen(h416));
        CloseHandle(hFile);
        ConnShutdown(cli);
        return;
    }

    long long bodyLen = (fileSize > 0 && partial) ? (end - start + 1) : fileSize;

    char hdr[2048];
    if (partial) {
        _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %lld\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Content-Disposition: attachment; filename=\"%s\"; filename*=UTF-8''%s\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            bodyLen, start, end, fileSize, simpleName.c_str(), enc.c_str());
    } else {
        _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %lld\r\n"
            "Content-Disposition: attachment; filename=\"%s\"; filename*=UTF-8''%s\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            bodyLen, simpleName.c_str(), enc.c_str());
    }

    // HEAD 探测只需要响应头
    if (method == "HEAD") {
        SendAll(cli, hdr, strlen(hdr));
        CloseHandle(hFile);
        ConnShutdown(cli);
        return;
    }

    /* ------------------------------------------------------------
     * 0 字节空文件的坑（iOS Safari “0 字节一直转圈”）:
     *   空文件只能回 “Content-Length: 0”，iOS 的下载管理器把它
     *   当成“没有内容”，既不落盘也不结束，于是永远转圈。
     *   改用 chunked 传输：长度未知的流式响应里放一个终止 chunk，
     *   下载管理器会正常走完流程，文件本身依然是 0 字节。
     *   （新版手机端已在本地用 Blob 直接生成空文件，这里是
     *     旧系统 / 直接访问该链接时的兜底通道。）
     *   注意：这个分支必须排在下面那次 SendAll 之前。早先的写法把
     *   带头部的那句放在分支前面，结果同一个连接先回一个
     *   “Content-Length: 0”，再回一个 chunked —— 浏览器只认第一个，
     *   于是又回到了一直转圈的老样子。
     * ------------------------------------------------------------ */
    if (fileSize == 0) {
        char hdr0[2048];
        _snprintf_s(hdr0, sizeof(hdr0), _TRUNCATE,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Content-Disposition: attachment; filename=\"%s\"; filename*=UTF-8''%s\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            simpleName.c_str(), enc.c_str());
        if (SendAll(cli, hdr0, strlen(hdr0))) SendAll(cli, "0\r\n\r\n", 5);
        CloseHandle(hFile);
        ConnShutdown(cli);
        return;
    }

    if (!SendAll(cli, hdr, strlen(hdr))) { CloseHandle(hFile); return; }

    if (partial) {
        LARGE_INTEGER li; li.QuadPart = start;
        SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
    }
    char buf[16384];
    DWORD read;
    long long remaining = bodyLen;
    while (remaining > 0 &&
           ReadFile(hFile, buf,
                    (DWORD)std::min<long long>((long long)sizeof(buf), remaining),
                    &read, NULL) && read > 0) {
        if (!SendAll(cli, buf, read)) break;
        remaining -= (long long)read;
    }
    CloseHandle(hFile);

    // 主动半关闭：把 FIN 及时送达浏览器，确保其判定下载“已完成”
    ConnShutdown(cli);
}

static void HandleClient(SOCKET cli) {
    // 收请求头要快（控制指令是高频短请求），发大文件要慢一点：
    // 手机 WiFi 抖动时 3 秒的发送超时会把长下载直接掐断。
    DWORD rcvTv = 3000;
    DWORD sndTv = 60000;
    setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTv, sizeof(rcvTv));
    setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sndTv, sizeof(sndTv));

    std::string req;
    char buf[1024];
    bool reqComplete = false;
    while (req.size() < 65536) {
        int n = ConnRecv(cli, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, n);
        if (req.find("\r\n\r\n") != std::string::npos) { reqComplete = true; break; }
    }
    // 请求头没收全就不要硬着头皮解析：以前会拿半截请求去查
    // Range/UA，判成一个没有 Range 的普通请求，浏览器只能干等。
    if (req.empty() || !reqComplete) {
        if (!req.empty())
            SendResponse(cli, "408 Request Timeout", "Incomplete request", "text/plain");
        ConnClose(cli);
        return;
    }
    if (InterlockedCompareExchange(&g_first_client_seen, 1, 0) == 0) {
        if (g_hwnd) PostMessageW(g_hwnd, WM_FIRST_CLIENT, 0, 0);
    }

    size_t lineEnd = req.find("\r\n");
    if (lineEnd == std::string::npos) { ConnClose(cli); return; }
    std::string line = req.substr(0, lineEnd);
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) { ConnClose(cli); return; }
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) { ConnClose(cli); return; }
    std::string method = line.substr(0, sp1);          // GET / HEAD
    std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);

    std::string path = target, query;
    size_t qp = target.find('?');
    if (qp != std::string::npos) {
        path  = target.substr(0, qp);
        query = target.substr(qp + 1);
    }
    std::map<std::string, std::string> params;
    ParseQuery(query, params);

    if (path == "/" || path == "/index.html") {
        // 【修改】手机扫码打开页面 / 网页点刷新（每次加载页面）都自动收起到托盘图标运行，
        //        不再只收一次：只要二维码窗口正显示着，收到页面请求就收起来。
        if (g_hwnd) PostMessageW(g_hwnd, WM_FIRST_CLIENT, 0, 0);
        std::string page = BuildHtmlPage();
        SendResponse(cli, "200 OK", page, "text/html; charset=utf-8");
    }
    else if (path == "/mouse/move") {
        int dx = params.count("dx") ? atoi(params["dx"].c_str()) : 0;
        int dy = params.count("dy") ? atoi(params["dy"].c_str()) : 0;
        if (dx || dy) MouseMoveRel(dx, dy);
        SendResponse(cli, "200 OK", "OK", "text/plain");
    }
    else if (path == "/mouse/click") {
        std::string b = params.count("btn") ? params["btn"] : "left";
        if      (b == "right")     MouseClickBtn(MOUSEEVENTF_RIGHTDOWN,  MOUSEEVENTF_RIGHTUP);
        else if (b == "middle")    MouseClickBtn(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);
        else if (b == "double")    MouseDoubleClick();
        else if (b == "left_down") SendMouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
        else if (b == "left_up")   SendMouse(MOUSEEVENTF_LEFTUP,   0, 0, 0);
        else                       MouseClickBtn(MOUSEEVENTF_LEFTDOWN,   MOUSEEVENTF_LEFTUP);
        SendResponse(cli, "200 OK", "OK", "text/plain");
    }
    else if (path == "/mouse/scroll") {
        int d = params.count("delta") ? atoi(params["delta"].c_str()) : 0;
        if (d) MouseWheel(d);
        SendResponse(cli, "200 OK", "OK", "text/plain");
    }
    else if (path == "/mouse/zoom") {
        int d = params.count("delta") ? atoi(params["delta"].c_str()) : 0;
        if (d) SendZoom(d);
        SendResponse(cli, "200 OK", "OK", "text/plain");
    }
    else if (path == "/keyboard/char") {
        std::string c = params.count("c") ? params["c"] : "";
        if (!c.empty()) {
            std::wstring w = Utf8ToWide(c);
            for (size_t i = 0; i < w.size(); ++i) SendUnicodeChar(w[i]);
        }
        SendResponse(cli, "200 OK", "OK", "text/plain");
    }
    else if (path == "/keyboard/special") {
        std::string k = params.count("k") ? params["k"] : "";
        if      (k == "backspace")       SendVkKey(VK_BACK);
        else if (k == "enter")           SendVkKey(VK_RETURN);
        else if (k == "tab")             SendVkKey(VK_TAB);
        else if (k == "escape")          SendVkKey(VK_ESCAPE);
        else if (k == "delete")          SendVkKey(VK_DELETE);
        else if (k == "space")           SendVkKey(VK_SPACE);
        else if (k == "left")            SendVkKey(VK_LEFT);
        else if (k == "right")           SendVkKey(VK_RIGHT);
        else if (k == "up")              SendVkKey(VK_UP);
        else if (k == "down")            SendVkKey(VK_DOWN);
        else if (k == "home")            SendVkKey(VK_HOME);
        else if (k == "end")             SendVkKey(VK_END);
        else if (k == "showdesktop")     SendWinD();
        else if (k == "winh")            SendWinH();          // 白话筒：无条件发 Win+H 打开
        else if (k == "winhclose")       CloseVoiceTyping();  // 红话筒：收起语音输入面板
        else if (k == "prevwindow")      SendAltTabPrev();   // 三指左滑：前一个窗口
        else if (k == "nextwindow")      SendAltTabNext();   // 三指右滑：后一个窗口
        else if (k == "selectall")       SendSelectAll();
        else if (k == "invertselect")    SendInvertSelection();
        else if (k == "switchapp")       SendSwitchApp();     // 切换应用：循环切换所有窗口
        else if (k == "ctrl_down")       CtrlDown();
        else if (k == "ctrl_up")         CtrlUp();
        SendResponse(cli, "200 OK", "OK", "text/plain");
    }
    // 【新增】电脑语音输入是否正在收音：手机上的红/白话筒靠它同步
    else if (path == "/voice/state") {
        bool on = VoiceTypingListeningNow();
        bool known = on || VoiceTypingKnown();
        char b[64];
        sprintf(b, "{\"listening\":%d,\"known\":%d}", on ? 1 : 0, known ? 1 : 0);
        SendJson(cli, b);
    }
    // ============================================================
    // 【新增】剪贴板相关接口
    // ============================================================
    else if (path == "/clipboard/version") {
        // 【新增】只回一个版本号：手机端靠它做实时轮询，
        // 比轮询 /clipboard/info 便宜得多（那一个要递归统计文件夹大小）。
        LONG ver = 0;
        AcquireSRWLockShared(&g_clipLock);
        ver = g_clipVersion;
        ReleaseSRWLockShared(&g_clipLock);
        char vbuf[64];
        sprintf(vbuf, "{\"version\":%ld}", ver);
        SendResponse(cli, "200 OK", vbuf, "application/json; charset=utf-8");
    }
    else if (path == "/clipboard/info") {
        std::wstring textCopy;
        std::vector<std::wstring> filesCopy;
        bool isCutCopy = false;
        LONG ver = 0;
        AcquireSRWLockShared(&g_clipLock);
        textCopy  = g_clipText;
        filesCopy = g_clipFiles;
        isCutCopy = g_clipIsCut;
        ver       = g_clipVersion;
        ReleaseSRWLockShared(&g_clipLock);

        std::string json = "{\"version\":";
        char vbuf[32]; sprintf(vbuf, "%ld", ver);
        json += vbuf;
        json += ",\"isCut\":";
        json += (isCutCopy ? "true" : "false");

        if (!filesCopy.empty()) {
            json += ",\"type\":\"files\",\"files\":[";
            for (size_t i = 0; i < filesCopy.size(); ++i) {
                if (i) json += ",";
                const std::wstring& fp = filesCopy[i];
                const wchar_t* np = wcsrchr(fp.c_str(), L'\\');
                if (!np) np = wcsrchr(fp.c_str(), L'/');
                np = np ? np + 1 : fp.c_str();
                std::wstring name(np);

                DWORD attr = GetFileAttributesW(fp.c_str());
                bool isDir = (attr != INVALID_FILE_ATTRIBUTES) &&
                             (attr & FILE_ATTRIBUTE_DIRECTORY);

                long long size = 0;
                long long count = 0;
                bool tooBig = false;
                if (isDir) {
                    // 【新增】文件夹：递归统计总大小与文件数，
                    // 手机端据此显示“📁 12 项 · 3.4 MB”，并决定能否打包
                    ZipDirStats(fp, size, count, tooBig);
                } else if (attr != INVALID_FILE_ATTRIBUTES) {
                    HANDLE h = CreateFileW(fp.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (h != INVALID_HANDLE_VALUE) {
                        LARGE_INTEGER li;
                        if (GetFileSizeEx(h, &li)) size = li.QuadPart;
                        CloseHandle(h);
                    }
                }
                char szbuf[32];
                sprintf(szbuf, "%lld", size);
                json += "{\"name\":\"" + JsonEscapeW(name) + "\"";
                json += ",\"size\":" + std::string(szbuf);
                json += ",\"dir\":";
                json += (isDir ? "true" : "false");
                if (isDir) {
                    char cbuf[32];
                    sprintf(cbuf, "%lld", count);
                    json += ",\"count\":" + std::string(cbuf);
                    json += ",\"tooBig\":";
                    json += (tooBig ? "true" : "false");
                }
                json += "}";
            }
            json += "]";
        } else if (!textCopy.empty()) {
            json += ",\"type\":\"text\"";
        } else {
            json += ",\"type\":\"empty\"";
        }
        json += "}";
        SendResponse(cli, "200 OK", json, "application/json; charset=utf-8");
    }
    else if (path == "/clipboard/text") {
        std::wstring textCopy;
        AcquireSRWLockShared(&g_clipLock);
        textCopy = g_clipText;
        ReleaseSRWLockShared(&g_clipLock);
        SendResponse(cli, "200 OK", WideToUtf8(textCopy), "text/plain; charset=utf-8");
    }
    else if (path == "/clipboard/file") {
        int idx = params.count("i") ? atoi(params["i"].c_str()) : -1;
        std::wstring pathCopy;
        bool ok = false;
        AcquireSRWLockShared(&g_clipLock);
        if (idx >= 0 && (size_t)idx < g_clipFiles.size()) {
            pathCopy = g_clipFiles[idx];
            ok = true;
        }
        ReleaseSRWLockShared(&g_clipLock);

        if (!ok) {
            SendResponse(cli, "404 Not Found", "Not found", "text/plain");
        } else {
            DWORD attr = GetFileAttributesW(pathCopy.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) {
                SendResponse(cli, "404 Not Found", "Not found", "text/plain");
            } else if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                // 【修复】文件夹：现场打包成 zip 再发。
                // 以前这里直接回 404 “Not a file”，所以手机上点不了文件夹、
                // 也拿不到里面的任何东西。
                const wchar_t* np = wcsrchr(pathCopy.c_str(), L'\\');
                if (!np) np = wcsrchr(pathCopy.c_str(), L'/');
                np = np ? np + 1 : pathCopy.c_str();
                std::string zipName = WideToUtf8(np);
                zipName += ".zip";
                std::vector<std::wstring> roots;
                roots.push_back(pathCopy);
                SendZipResponse(cli, roots, zipName, method);
            } else {
                SendFileResponse(cli, pathCopy, method, GetHeaderValue(req, "Range"));
            }
        }
    }
    else if (path == "/clipboard/all") {
        // 剪贴板里的文件 + 文件夹一次性打成一个 zip：
        // 手机上“多个下载”很容易被 Safari 拦掉，一次一个包最稳
        std::vector<std::wstring> filesCopy;
        AcquireSRWLockShared(&g_clipLock);
        filesCopy = g_clipFiles;
        ReleaseSRWLockShared(&g_clipLock);

        if (filesCopy.empty()) {
            SendResponse(cli, "404 Not Found", "Clipboard has no files", "text/plain");
        } else {
            SendZipResponse(cli, filesCopy, WideToUtf8(S().clipZipName), method);
        }
    }
    // ============================================================
    // 【新增】手机 → 电脑：上传文件 / 新建文本
    // ============================================================
    else if (path == "/clipboard/upload" && method == "POST") {
        HandleClipboardUpload(cli, req, params);
    }
    else if (path == "/clipboard/settext" && method == "POST") {
        HandleClipboardSetText(cli, req);
    }
    // ============================================================
    // 【新增】Win+H 转完文字后截一张“输入框”的图给手机看
    // ============================================================
    else if (path == "/screen/input") {
        std::string png;
        if (!CaptureInputWindowPng(png)) {
            SendResponse(cli, "503 Service Unavailable", "capture failed",
                         "text/plain");
        } else {
            char hdr[256];
            sprintf(hdr,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/png\r\n"
                "Content-Length: %u\r\n"
                "Cache-Control: no-store, no-cache, must-revalidate\r\n"
                "Pragma: no-cache\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n",
                (unsigned)png.size());
            SendAll(cli, hdr, strlen(hdr));
            if (method != "HEAD") SendAll(cli, png.data(), png.size());
            ConnShutdown(cli);   // 图片给全了及时半关闭，手机那边好判断“收完了”
        }
    }
    else {
        SendResponse(cli, "404 Not Found", "Not Found", "text/plain");
    }
    ConnClose(cli);
}

static DWORD WINAPI ServerThread(LPVOID) {
    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_srv, &fds);
        timeval tv = { 0, 100000 };
        int r = select(0, &fds, NULL, NULL, &tv);
        if (r <= 0) continue;
        if (FD_ISSET(g_srv, &fds)) {
            SOCKET cli = accept(g_srv, NULL, NULL);
            if (cli != INVALID_SOCKET) HandleClient(cli);
        }
    }
    if (g_srv != INVALID_SOCKET) closesocket(g_srv);
    return 0;
}
static bool StartMouseServer(int port) {
    g_srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_srv == INVALID_SOCKET) return false;
    int opt = 1;
    setsockopt(g_srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_srv, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(g_srv); g_srv = INVALID_SOCKET; return false;
    }
    if (listen(g_srv, SOMAXCONN) != 0) {
        closesocket(g_srv); g_srv = INVALID_SOCKET; return false;
    }
    InterlockedExchange(&g_stop, 0);
    g_thread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);
    return g_thread != NULL;
}
static void StopMouseServer() {
    InterlockedExchange(&g_stop, 1);
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    g_srv = INVALID_SOCKET;
}

// ============================================================
// 二维码大窗
// ============================================================
static HFONT g_fontTitle = NULL;
static HFONT g_fontUrl   = NULL;
static HFONT g_fontHint  = NULL;

static void PaintContent(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    const int pad     = MulDiv(14, g_dpi, 96);
    const int titleH  = MulDiv(30, g_dpi, 96);
    const int urlH    = MulDiv(38, g_dpi, 96);
    const int hintH   = MulDiv(20, g_dpi, 96);
    int availW = rc.right  - pad * 2;
    int availH = rc.bottom - titleH - urlH - hintH - pad;

    int n = (int)g_qr.size();
    if (n > 0) {
        int side = (availW < availH) ? availW : availH;
        int px = side / n;
        if (px < 1) px = 1;
        int total = px * n;
        int ox = (rc.right - total) / 2;
        // 【修改】标题与二维码之间的空白缩小五分之四（原约 44px → 约 9px）；
        //        配合下方窗口高度 430→395，整体变紧凑，二维码尺寸不变。
        int urlTopY = rc.bottom - urlH - hintH;            // 蓝色 http 行上边界
        int gapTop  = MulDiv(9, g_dpi, 96);                // 标题与二维码之间的空白（原留白的 1/5）
        int oy      = titleH + gapTop;                     // 从上往下排：标题 → 小空白 → 二维码
        if (oy + total > urlTopY - MulDiv(6, g_dpi, 96))   // 兜底：给蓝色行上方至少留 6px
            oy = urlTopY - MulDiv(6, g_dpi, 96) - total;
        if (oy < titleH) oy = titleH;
        HBRUSH bb = CreateSolidBrush(RGB(0, 0, 0));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (g_qr[y][x]) {
                    RECT r;
                    r.left = ox + x * px; r.top = oy + y * px;
                    r.right = r.left + px; r.bottom = r.top + px;
                    FillRect(hdc, &r, bb);
                }
        DeleteObject(bb);
    }
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontTitle);
    SetTextColor(hdc, RGB(33, 33, 33));
    RECT titleRc = {8, 4, rc.right - 8, titleH};
    DrawTextW(hdc, S().qrTitle, -1, &titleRc,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, g_fontUrl);
    SetTextColor(hdc, RGB(37, 99, 235));
    RECT urlRc = {8, rc.bottom - urlH - hintH, rc.right - 8, rc.bottom - hintH};
    DrawTextW(hdc, g_url.c_str(), -1, &urlRc,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(hdc, g_fontHint);
    SetTextColor(hdc, RGB(150, 150, 150));
    RECT hintRc = {8, rc.bottom - hintH, rc.right - 8, rc.bottom - 2};
    DrawTextW(hdc, S().qrHint, -1, &hintRc,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldFont);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: PaintContent(hwnd); return 0;

        // ============================================================
        // 【新增】剪贴板更新
        // ============================================================
        case WM_CLIPBOARDUPDATE:
            ReadClipboardNow();
            return 0;

        case WM_LBUTTONDOWN:
            g_qrDragArmed = true;
            g_qrDragging  = false;
            GetCursorPos(&g_qrDragStart);
            GetWindowRect(hwnd, &g_qrStartRect);
            return 0;

        case WM_MOUSEMOVE: {
            if (g_qrDragArmed && !g_qrDragging) {
                POINT pt; GetCursorPos(&pt);
                if (abs(pt.x - g_qrDragStart.x) > 4 ||
                    abs(pt.y - g_qrDragStart.y) > 4) {
                    g_qrDragging = true;
                    SetCapture(hwnd);
                }
            }
            if (g_qrDragging) {
                POINT pt; GetCursorPos(&pt);
                int dx = pt.x - g_qrDragStart.x;
                int dy = pt.y - g_qrDragStart.y;
                SetWindowPos(hwnd, HWND_TOPMOST,
                             g_qrStartRect.left + dx,
                             g_qrStartRect.top  + dy,
                             0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_qrDragging) {
                g_qrDragging = false;
                ReleaseCapture();
                g_qrDragArmed = false;
            } else if (g_qrDragArmed) {
                SetTimer(hwnd, TIMER_QR_MENU, GetDoubleClickTime(), NULL);
                g_qrDragArmed = false;
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            KillTimer(hwnd, TIMER_QR_MENU);
            g_qrDragArmed = false;
            g_qrDragging  = false;
            ShowQrWithRefresh();
            return 0;

        case WM_RBUTTONUP:
            ShowQrContextMenu(hwnd);
            return 0;

        case WM_TIMER:
            if (wp == TIMER_SHOW_QR) {
                KillTimer(hwnd, TIMER_SHOW_QR);
                ShowStartupQr();
                return 0;
            }
            if (wp == TIMER_QR_MENU) {
                KillTimer(hwnd, TIMER_QR_MENU);
                ShowQrContextMenu(hwnd);
                return 0;
            }
            if (wp == TIMER_QR_REPOS) {
                // h 参数：显示后 150ms / 550ms 各校正一次位置。
                // 只挪位置，不抢焦点，也不会把已隐藏的窗口弹出来。
                KillTimer(hwnd, TIMER_QR_REPOS);
                if (IsWindowVisible(g_hwnd)) PlaceQrTopCenter();
                if (g_qrReposLeft > 0) {
                    --g_qrReposLeft;
                    if (g_qrReposLeft > 0)
                        SetTimer(hwnd, TIMER_QR_REPOS, 400, NULL);
                }
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            g_qrDragging  = false;
            g_qrDragArmed = false;
            return 0;

        case WM_DISPLAYCHANGE:
            // 分辨率/显示器变了（PE 里显示驱动晚加载、切换分辨率很常见）：
            // h 模式下重新贴回“屏幕顶部的中间”，否则窗口会停在老分辨率的位置
            if (g_startupPosition == 3 && g_hwnd && IsWindowVisible(g_hwnd))
                PlaceQrTopCenter();
            return 0;

        case WM_CLOSE:
            KillTimer(hwnd, TIMER_QR_MENU);
            KillTimer(hwnd, TIMER_SHOW_QR);
            g_qrDragging  = false;
            g_qrDragArmed = false;
            HideQrToTray();
            return 0;

        case WM_FIRST_CLIENT:
            // 【修改】每次扫码打开页面 / 网页点刷新都收起到托盘，不再只隐藏第一次
            g_auto_hide_done = true;
            HideQrToTray();
            return 0;

        case WM_SHOW_QR_FROM_OTHER:
            ShowQrWithRefresh();
            return 0;

        case WM_SET_SHOW_QR_TIMER: {
            KillTimer(hwnd, TIMER_SHOW_QR);
            UINT sec = (UINT)wp;
            if (sec > 0 && sec <= 3600) {
                SetTimer(hwnd, TIMER_SHOW_QR, sec * 1000, NULL);
            }
            return 0;
        }

        case WM_SHOW_QR_CENTER:
            MoveQrToCenter();
            return 0;

        case WM_SHOW_QR_RIGHT:
            MoveQrToRight();
            return 0;

        case WM_SHOW_QR_TOP:
            MoveQrToTopCenter();
            return 0;

        case WM_TRAYICON:
            if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
                if (IsWindowVisible(hwnd)) HideQrToTray();
                else ShowQrWithRefresh();
            } else if (lp == WM_RBUTTONUP) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, S().menuShowQr);
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 3, S().menuCopyLink);
                AppendMenuW(hMenu, MF_STRING, 2, S().menuExit);
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                int cmd = (int)TrackPopupMenu(
                    hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                    pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                if (cmd == 1)      ShowQrWithRefresh();
                else if (cmd == 3) CopyQrLink();
                else if (cmd == 2) DestroyWindow(hwnd);
            }
            return 0;

        case WM_DESTROY:
            // 【新增】取消剪贴板监听
            RemoveClipboardFormatListener(hwnd);
            UninstallForegroundHook();
            KillTimer(hwnd, TIMER_QR_MENU);
            KillTimer(hwnd, TIMER_SHOW_QR);
            KillTimer(hwnd, TIMER_QR_REPOS);
            CtrlUp();
            HideTrayIcon();
            StopMouseServer();
            if (g_miniHwnd) { DestroyWindow(g_miniHwnd); g_miniHwnd = NULL; }
            if (g_mutex) { CloseHandle(g_mutex); g_mutex = NULL; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// 入口
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDPIAware();

    // 获取系统 DPI（96 = 100%）
    g_dpi = 96;
    {
        HDC hdcScreen = GetDC(NULL);
        if (hdcScreen) {
            g_dpi = GetDeviceCaps(hdcScreen, LOGPIXELSX);
            ReleaseDC(NULL, hdcScreen);
            if (g_dpi <= 0) g_dpi = 96;
        }
    }

    g_lang = DetectSystemLang();   // 语言自适应
    ParseCommandLine();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    g_mutex = CreateMutexW(NULL, FALSE, L"Local\\PhoneMouse_SingleInstance");
    if (g_mutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = NULL;
        for (int i = 0; i < 30 && !existing; ++i) {
            existing = FindWindowW(L"PhoneRemoteMouseWnd", NULL);
            if (!existing) Sleep(100);
        }
        if (existing) {
            if (g_startupDelaySec > 0) {
                PostMessageW(existing, WM_SET_SHOW_QR_TIMER,
                             (WPARAM)g_startupDelaySec, 0);
            } else if (g_startupPosition == 1) {
                PostMessageW(existing, WM_SHOW_QR_CENTER, 0, 0);
            } else if (g_startupPosition == 2) {
                PostMessageW(existing, WM_SHOW_QR_RIGHT, 0, 0);
            } else if (g_startupPosition == 3) {
                PostMessageW(existing, WM_SHOW_QR_TOP, 0, 0);
            } else {
                PostMessageW(existing, WM_SHOW_QR_FROM_OTHER, 0, 0);
            }
            ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
        }
        CloseHandle(g_mutex);
        CoUninitialize();
        return 0;
    }

    g_hInst = hInst;

    // 【新增】尽早装前台钩子：这时还没显示二维码/浮动小图标，
    // 能准确记住用户原本停留的窗口，收起二维码时把前台还给它。
    InstallForegroundHook();

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (!StartMouseServer(g_port)) {
        MessageBoxW(NULL, S().failMsg, S().failTitle, MB_OK | MB_ICONERROR);
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    RefreshQr();

    g_has_shell = DetectShell();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"PhoneRemoteMouseWnd";
    wc.hIcon         = LoadIconW(hInst, L"MAINICON");
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    g_fontTitle = CreateFontW(-MulDiv(18, g_dpi, 96), 0, 0, 0, FW_BOLD, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei");
    g_fontUrl   = CreateFontW(-MulDiv(16, g_dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei");
    g_fontHint  = CreateFontW(-MulDiv(12, g_dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei");

    int qrW = MulDiv(340, g_dpi, 96);
    int qrH = MulDiv(395, g_dpi, 96);

    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"PhoneRemoteMouseWnd", S().winTitle,
        WS_POPUP | WS_BORDER,
        0, 0, qrW, qrH,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) {
        StopMouseServer();
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    // ============================================================
    // 【新增】注册剪贴板监听 + 首次读取
    // ============================================================
    AddClipboardFormatListener(g_hwnd);
    ReadClipboardNow();

    if (g_has_shell) ShowTrayIcon();

    bool useMini = !g_tray_visible;
    if (useMini) {
        CreateMiniIcon();
        if (g_miniHwnd) ShowWindow(g_miniHwnd, SW_SHOW);
    }

    // 前台窗口跟踪钩子已在 wWinMain 开头装好

    if (g_startupDelaySec > 0) {
        SetTimer(g_hwnd, TIMER_SHOW_QR, (UINT)g_startupDelaySec * 1000, NULL);
    } else {
        if (useMini && g_miniHwnd) Sleep(100);
        else Sleep(600);
        ShowStartupQr();
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopMouseServer();
    WSACleanup();
    CoUninitialize();
    return 0;
}
