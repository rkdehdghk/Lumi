/* Small shared helpers: encoding, files, paths, settings, fuzzy matching. */
#include "lumina.h"
#include <shlobj.h>
#include <algorithm>

int g_dpi = 96;
bool g_dark = false;
Theme T;
HFONT g_uiFont = nullptr, g_uiBold = nullptr, g_uiSmall = nullptr;
int g_editorFontSize = 16, g_termFontSize = 16;

// ---------------------------------------------------------------- encoding

/*
 * Decode child-process output.  Console programs on Windows may write in the
 * system codepage rather than UTF-8, so fall back when UTF-8 comes out broken.
 * ponytail: three tries is enough in practice; add a real codepage lookup only
 * if some tool actually needs a fourth encoding.
 */
std::wstring widen(const char* s, int len) {
    if (len <= 0) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, nullptr, 0);
    UINT cp = CP_UTF8;
    if (n <= 0) {
        cp = 949;                                   // Korean, like the old TextDecoder
        n = MultiByteToWideChar(cp, 0, s, len, nullptr, 0);
    }
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s, len, nullptr, 0);
    }
    if (n <= 0) return L"";
    std::wstring out(n, L'\0');
    MultiByteToWideChar(cp, 0, s, len, &out[0], n);
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------- files

std::wstring readFileText(const std::wstring& path, bool* ok) {
    if (ok) *ok = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::string bytes;
    if (size.QuadPart > 0 && size.QuadPart < (1LL << 28)) {
        bytes.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ReadFile(h, &bytes[0], (DWORD)bytes.size(), &got, nullptr);
        bytes.resize(got);
    }
    CloseHandle(h);
    if (ok) *ok = true;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);
    std::wstring text = widen(bytes.data(), (int)bytes.size());
    // the editor works in \r\n; normalise whatever was on disk
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r') continue;
        out += text[i];
    }
    return out;
}

bool writeFileText(const std::wstring& path, const std::wstring& text) {
    std::string bytes = narrow(text);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL okw = bytes.empty() ? TRUE
                             : WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
    CloseHandle(h);
    return okw && wrote == bytes.size();
}

// ---------------------------------------------------------------- paths

std::wstring baseName(const std::wstring& p) {
    size_t at = p.find_last_of(L"\\/");
    return at == std::wstring::npos ? p : p.substr(at + 1);
}

std::wstring dirName(const std::wstring& p) {
    size_t at = p.find_last_of(L"\\/");
    return at == std::wstring::npos ? L"" : p.substr(0, at);
}

std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (!a.empty() && (a.back() == L'\\' || a.back() == L'/')) return a + b;
    return a + L"\\" + b;
}

bool pathExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool pathIsDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring lowerOf(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) c = (wchar_t)towlower(c);
    return out;
}

bool endsWithI(const std::wstring& s, const std::wstring& suffix) {
    if (s.size() < suffix.size()) return false;
    return lowerOf(s.substr(s.size() - suffix.size())) == lowerOf(suffix);
}

std::wstring trimOf(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- fuzzy

/* VS Code-style subsequence match: "opf" finds "Open File".  Returns the
   position the match started at, or -1, so earlier matches sort first. */
int fuzzyRank(const std::wstring& query, const std::wstring& text) {
    if (query.empty()) return 0;
    std::wstring q = lowerOf(query), t = lowerOf(text);
    int first = -1;
    size_t at = 0;
    for (wchar_t c : q) {
        size_t hit = t.find(c, at);
        if (hit == std::wstring::npos) return -1;
        if (first < 0) first = (int)hit;
        at = hit + 1;
    }
    return first;
}

// ---------------------------------------------------------------- settings

static const wchar_t* iniPath() {
    static std::wstring path;
    if (path.empty()) {
        wchar_t dir[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) {
            std::wstring folder = joinPath(dir, L"Lumina");
            CreateDirectoryW(folder.c_str(), nullptr);
            path = joinPath(folder, L"lumina.ini");
        } else {
            path = L".\\lumina.ini";
        }
    }
    return path.c_str();
}

std::wstring iniGet(const wchar_t* key, const std::wstring& def) {
    wchar_t buf[512]{};
    GetPrivateProfileStringW(L"lumina", key, def.c_str(), buf, 512, iniPath());
    return buf;
}

int iniGetInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(L"lumina", key, def, iniPath());
}

void iniSet(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"lumina", key, v.c_str(), iniPath());
}

void iniSetInt(const wchar_t* key, int v) {
    iniSet(key, std::to_wstring(v));
}

// ---------------------------------------------------------------- dialogs

void errorBox(const std::wstring& title, const std::wstring& msg) {
    MessageBoxW(GetActiveWindow(), msg.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

bool confirmBox(const std::wstring& title, const std::wstring& msg) {
    return MessageBoxW(GetActiveWindow(), msg.c_str(), title.c_str(),
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

std::wstring lastErrorText() {
    DWORD e = GetLastError();
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring out = buf ? buf : L"unknown error";
    if (buf) LocalFree(buf);
    return trimOf(out);
}

// ---------------------------------------------------------------- theme

static COLORREF hx(unsigned v) {
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void themeSet(bool dark) {
    g_dark = dark;
    if (!dark) {
        T = {hx(0xffffff), hx(0xffffff), hx(0x1e1e1e), hx(0xeef0f3), hx(0xf3f3f6),
             hx(0x2a2a2a), hx(0xd3e3ff), hx(0x000000), hx(0xe8e8ee), hx(0xdfe1e6),
             hx(0xffffff), hx(0xeceef2), hx(0x3a3a3a), hx(0x8a8a92), hx(0xd11a2a),
             hx(0xfde2e4), hx(0x1a7f37), hx(0xfbfbfd), hx(0x1e1e1e), hx(0xe7e9ee),
             hx(0x8a8a92), hx(0x0b61d1), hx(0xe4e6eb), hx(0xd5d8de), hx(0x1e1e1e),
             hx(0x5a5a63), hx(0xe2e2e8), hx(0x2ea043), hx(0xffffff), hx(0xd9d9de),
             hx(0xc8c8d0), hx(0x0b61d1), hx(0x2ea043), hx(0xffffff), hx(0xffffff),
             hx(0xffffff), hx(0x0033b3), hx(0x067d17), hx(0x1750eb), hx(0x9aa0a6),
             hx(0x7b1fa2)};
    } else {
        T = {hx(0x1e1e1e), hx(0x1e1e1e), hx(0xd4d4d4), hx(0x3c3c3c), hx(0x252526),
             hx(0xcccccc), hx(0x04395e), hx(0xffffff), hx(0x2a2d2e), hx(0x252526),
             hx(0x1e1e1e), hx(0x2d2d2d), hx(0xffffff), hx(0x969696), hx(0xf14c4c),
             hx(0x5a1d1d), hx(0x89d185), hx(0x1e1e1e), hx(0xcccccc), hx(0x252526),
             hx(0x969696), hx(0x4fc1ff), hx(0x3a3d41), hx(0x45494e), hx(0xcccccc),
             hx(0xcccccc), hx(0x3f3f46), hx(0x0e639c), hx(0xffffff), hx(0x2b2b2b),
             hx(0x454545), hx(0x007fd4), hx(0x007acc), hx(0xffffff), hx(0x252526),
             hx(0x3c3c3c), hx(0x6a9bf4), hx(0x5fbf6b), hx(0x7ba7f7), hx(0x8a9199),
             hx(0xc586e0)};
    }
    iniSet(L"theme", dark ? L"dark" : L"light");
}

HFONT monoFont(int px) {
    LOGFONTW lf{};
    lf.lfHeight = -S(px);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, L"Consolas", 32);
    return CreateFontIndirectW(&lf);
}
