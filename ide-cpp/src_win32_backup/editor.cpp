/*
 * INER: the tab strip, the line-number gutter and the editor itself.
 *
 * The editor is a RichEdit control, so selection, undo, the clipboard and the
 * Korean IME all come from Windows.  What is written here is only the part
 * Windows does not do: colouring, the gutter, auto-indent and the tab strip.
 */
#include "lumina.h"
#include <windowsx.h>
#include <commdlg.h>
#include <richole.h>
#include <tom.h>
#include <algorithm>
#include <cstdlib>

HWND g_iner = nullptr, g_edit = nullptr;
static HWND g_tabbar = nullptr, g_gutter = nullptr;
static WNDPROC g_editProc = nullptr;
static ITextDocument* g_doc = nullptr;
static HFONT g_editFont = nullptr;
static int g_errorLine = -1;
static int g_lastLineCount = 1;
static int g_lastFirstVisible = -1;
static bool g_quiet = false;          // true while we are reformatting

// ---------------------------------------------------------------- helpers

static int lineCount() { return (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0); }
static int lineIndex(int line) { return (int)SendMessageW(g_edit, EM_LINEINDEX, line, 0); }
static int lineLen(int start) { return (int)SendMessageW(g_edit, EM_LINELENGTH, start, 0); }
static int lineOfChar(int at) { return (int)SendMessageW(g_edit, EM_EXLINEFROMCHAR, 0, at); }

static void getSel(CHARRANGE& cr) { SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM)&cr); }
static void setSel(int a, int b) {
    CHARRANGE cr{a, b};
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&cr);
}

/*
 * Colouring walks the control setting formats, which would repaint on every
 * step, fire EN_CHANGE back at us and light up the "modified" dot.  Silent
 * turns all three off for as long as it is alive.
 */
struct Silent {
    long frozen = 0;
    BOOL modified;
    CHARRANGE sel;
    bool outer;
    Silent() {
        outer = !g_quiet;
        g_quiet = true;
        modified = (BOOL)SendMessageW(g_edit, EM_GETMODIFY, 0, 0);
        getSel(sel);
        if (g_doc) g_doc->Freeze(&frozen);
    }
    ~Silent() {
        SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM)&sel);
        SendMessageW(g_edit, EM_SETMODIFY, modified, 0);
        if (g_doc) g_doc->Unfreeze(&frozen);
        if (outer) g_quiet = false;
    }
};

static std::wstring textRange(int a, int b) {
    if (b <= a) return L"";
    std::wstring buf((size_t)(b - a) + 2, L'\0');
    TEXTRANGEW tr{};
    tr.chrg.cpMin = a;
    tr.chrg.cpMax = b;
    tr.lpstrText = &buf[0];
    LRESULT got = SendMessageW(g_edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    buf.resize((size_t)(got > 0 ? got : 0));
    return buf;
}

static std::wstring lineText(int line) {
    int start = lineIndex(line);
    if (start < 0) return L"";
    return textRange(start, start + lineLen(start));
}

static void applyColor(int a, int b, COLORREF color, bool bold) {
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_BACKCOLOR | CFM_UNDERLINE;
    cf.crTextColor = color;
    cf.dwEffects = (bold ? CFE_BOLD : 0) | CFE_AUTOBACKCOLOR;
    setSel(a, b);
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

/* The line an error points at.  The marker that is known to show is the red
   line number the gutter draws; RichEdit is asked for a tinted background and
   a red wave underline as well, and honours them at its own discretion. */
static void applyErrorMark(int a, int b) {
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BACKCOLOR | CFM_UNDERLINETYPE | CFM_UNDERLINE;
    cf.crBackColor = T.errorBg;
    cf.dwEffects = CFE_UNDERLINE;
    cf.bUnderlineType = CFU_UNDERLINEWAVE | (5 << 4);   // 5 = red, in RichEdit's table
    setSel(a, b);
    SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// ---------------------------------------------------------------- colouring

/* ponytail: colours go through the control token by token, which is fast
   enough for the files people write in Lumi (a few thousand lines).  If a very
   large file ever stutters, colour only the visible lines instead. */
static void highlightLines(int first, int last) {
    if (!g_edit) return;
    int total = lineCount();
    first = std::max(0, first);
    last = std::min(last, total - 1);
    if (last < first) return;

    int a = lineIndex(first);
    int lastStart = lineIndex(last);
    int b = lastStart + lineLen(lastStart);
    std::wstring text = textRange(a, b);

    std::vector<Tok> toks;
    lumiTokenize(text.c_str(), (int)text.size(), toks);

    Silent silent;
    applyColor(a, b, T.editorFg, false);
    for (const Tok& t : toks) {
        if (t.kind == TK_PLAIN) continue;
        applyColor(a + t.start, a + t.start + t.len, tokColor(t.kind), tokBold(t.kind));
    }
    if (g_errorLine > 0 && g_errorLine - 1 >= first && g_errorLine - 1 <= last) {
        int s = lineIndex(g_errorLine - 1);
        applyErrorMark(s, s + std::max(lineLen(s), 1));
    }
}

static void highlightAll() { highlightLines(0, lineCount() - 1); }

// ---------------------------------------------------------------- gutter

static int charWidth() {
    HDC hdc = GetDC(g_gutter);
    HGDIOBJ old = SelectObject(hdc, g_editFont ? g_editFont : g_uiFont);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, L"0", 1, &sz);
    SelectObject(hdc, old);
    ReleaseDC(g_gutter, hdc);
    return sz.cx > 0 ? sz.cx : S(8);
}

static int gutterWidth() {
    int digits = 1;
    for (int n = g_edit ? lineCount() : 1; n >= 10; n /= 10) digits++;
    return S(8) + digits * charWidth() + S(10);
}

static void gutterSync(bool force) {
    if (!g_gutter || !g_edit) return;
    int first = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (!force && first == g_lastFirstVisible) return;
    g_lastFirstVisible = first;
    InvalidateRect(g_gutter, nullptr, FALSE);
}

static LRESULT CALLBACK gutterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg != WM_PAINT) return DefWindowProcW(hwnd, msg, wp, lp);

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(g_dark ? T.editorBg : RGB(0xf5, 0xf5, 0xf7));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(mem, g_editFont ? g_editFont : g_uiFont);

    int total = lineCount();
    int first = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    int lineH = std::max(S(g_editorFontSize) + S(2), S(10));
    for (int line = first; line < total; line++) {
        POINTL pt{};
        SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)&pt, lineIndex(line));
        if (pt.y > rc.bottom) break;
        bool bad = (line + 1) == g_errorLine;
        SetTextColor(mem, bad ? T.error : T.tabFgDim);
        RECT tr{0, pt.y, rc.right - S(10), pt.y + lineH};
        DrawTextW(mem, std::to_wstring(line + 1).c_str(), -1, &tr,
                  DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
    }

    SelectObject(mem, oldFont);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
}

// ---------------------------------------------------------------- editing aids

static void insertText(const std::wstring& s, int caretBack = 0) {
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)s.c_str());
    if (caretBack) {
        CHARRANGE cr;
        getSel(cr);
        setSel(cr.cpMin - caretBack, cr.cpMin - caretBack);
    }
}

/** Enter keeps the current indent, and opens one more level after ':'. */
static void newlineWithIndent() {
    CHARRANGE cr;
    getSel(cr);
    int line = lineOfChar(cr.cpMin);
    std::wstring text = lineText(line);
    int upto = std::max(0, std::min<int>(cr.cpMin - lineIndex(line), (int)text.size()));
    std::wstring head = text.substr(0, upto);
    std::wstring indent;
    for (wchar_t c : head) {
        if (c == L' ' || c == L'\t') indent += c;
        else break;
    }
    std::wstring trimmed = trimOf(head);
    if (!trimmed.empty() && trimmed.back() == L':') indent += L"    ";
    insertText(L"\r" + indent);
}

/** Ctrl+/ - comment or uncomment every line the selection touches. */
static void toggleComment() {
    CHARRANGE cr;
    getSel(cr);
    int firstLine = lineOfChar(cr.cpMin);
    int lastLine = lineOfChar(cr.cpMax > cr.cpMin ? cr.cpMax - 1 : cr.cpMax);
    bool allCommented = true;
    for (int l = firstLine; l <= lastLine; l++) {
        std::wstring t = trimOf(lineText(l));
        if (t.empty()) continue;
        if (t.rfind(L"//", 0) != 0) { allCommented = false; break; }
    }
    for (int l = lastLine; l >= firstLine; l--) {
        std::wstring text = lineText(l);
        if (trimOf(text).empty()) continue;
        int start = lineIndex(l);
        if (allCommented) {
            size_t at = text.find(L"//");
            int cut = (at + 2 < text.size() && text[at + 2] == L' ') ? 3 : 2;
            setSel(start + (int)at, start + (int)at + cut);
            SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
        } else {
            size_t at = text.find_first_not_of(L" \t");
            setSel(start + (int)at, start + (int)at);
            SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"// ");
        }
    }
    int end = lineIndex(lastLine);
    setSel(lineIndex(firstLine), end + lineLen(end));
    highlightLines(firstLine, lastLine);
}

static void gotoLine() {
    std::wstring answer;
    if (!askText(GetAncestor(g_edit, GA_ROOT), L"Go to line:", L"", answer)) return;
    int line = _wtoi(answer.c_str());
    if (line < 1) return;
    line = std::min(line, lineCount());
    int at = lineIndex(line - 1);
    setSel(at, at);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    SetFocus(g_edit);
    gutterSync(true);
}

// ---------------------------------------------------------------- find / replace

static FINDREPLACEW g_fr{};
static wchar_t g_findBuf[256] = L"";
static wchar_t g_replBuf[256] = L"";
static HWND g_findDlg = nullptr;

static void openFind(bool replace) {
    if (g_findDlg) { SetFocus(g_findDlg); return; }
    ZeroMemory(&g_fr, sizeof(g_fr));
    g_fr.lStructSize = sizeof(g_fr);
    g_fr.hwndOwner = GetAncestor(g_edit, GA_ROOT);
    g_fr.hInstance = g_inst;
    g_fr.Flags = FR_DOWN | FR_HIDEWHOLEWORD;
    g_fr.lpstrFindWhat = g_findBuf;
    g_fr.wFindWhatLen = 256;
    g_fr.lpstrReplaceWith = g_replBuf;
    g_fr.wReplaceWithLen = 256;
    g_findDlg = replace ? ReplaceTextW(&g_fr) : FindTextW(&g_fr);
}

static bool findNext(const wchar_t* what, DWORD flags, bool fromCaret) {
    if (!what || !*what) return false;
    CHARRANGE cr;
    getSel(cr);
    bool down = (flags & FR_DOWN) != 0;
    FINDTEXTEXW ft{};
    ft.chrg.cpMin = down ? (fromCaret ? cr.cpMin : cr.cpMax) : cr.cpMin;
    ft.chrg.cpMax = down ? -1 : 0;
    ft.lpstrText = what;
    DWORD f = (down ? FR_DOWN : 0) | ((flags & FR_MATCHCASE) ? FR_MATCHCASE : 0);
    LRESULT at = SendMessageW(g_edit, EM_FINDTEXTEXW, f, (LPARAM)&ft);
    if (at < 0 && down) {                    // wrap once, like every editor does
        ft.chrg.cpMin = 0;
        ft.chrg.cpMax = -1;
        at = SendMessageW(g_edit, EM_FINDTEXTEXW, f, (LPARAM)&ft);
    }
    if (at < 0) return false;
    setSel(ft.chrgText.cpMin, ft.chrgText.cpMax);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    gutterSync(true);
    return true;
}

bool edFindMessage(LPARAM lp) {
    FINDREPLACEW* fr = (FINDREPLACEW*)lp;
    if (fr->Flags & FR_DIALOGTERM) { g_findDlg = nullptr; return true; }
    if (fr->Flags & FR_FINDNEXT) {
        if (!findNext(fr->lpstrFindWhat, fr->Flags, false))
            statusFlash(std::wstring(L"Not found: ") + fr->lpstrFindWhat);
        return true;
    }
    if (fr->Flags & FR_REPLACE) {
        CHARRANGE cr;
        getSel(cr);
        std::wstring sel = textRange(cr.cpMin, cr.cpMax);
        if (_wcsicmp(sel.c_str(), fr->lpstrFindWhat) == 0)
            SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)fr->lpstrReplaceWith);
        findNext(fr->lpstrFindWhat, fr->Flags, true);
        return true;
    }
    if (fr->Flags & FR_REPLACEALL) {
        setSel(0, 0);
        int n = 0;
        while (findNext(fr->lpstrFindWhat, fr->Flags | FR_DOWN, true)) {
            SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)fr->lpstrReplaceWith);
            if (++n > 100000) break;
        }
        highlightAll();
        statusFlash(std::to_wstring(n) + L" replaced");
        return true;
    }
    return true;
}

// ---------------------------------------------------------------- editor proc

static LRESULT CALLBACK editSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
                insertText(L"    ");
                gutterSync(true);
                return 0;
            }
            break;
        case WM_CHAR: {
            wchar_t c = (wchar_t)wp;
            if (c == L'\t') return 0;                    // handled in WM_KEYDOWN
            if (c == L'\r') {
                newlineWithIndent();
                gutterSync(true);
                return 0;
            }
            CHARRANGE cr;
            getSel(cr);
            bool caret = cr.cpMin == cr.cpMax;
            if (caret && (c == L')' || c == L']' || c == L'}' || c == L'"') &&
                textRange(cr.cpMin, cr.cpMin + 1) == std::wstring(1, c)) {
                setSel(cr.cpMin + 1, cr.cpMin + 1);      // type over the auto-inserted one
                return 0;
            }
            if (caret && (c == L'(' || c == L'[' || c == L'{' || c == L'"')) {
                const wchar_t* pair = c == L'(' ? L"()" : c == L'[' ? L"[]"
                                    : c == L'{' ? L"{}" : L"\"\"";
                insertText(pair, 1);
                return 0;
            }
            break;
        }
        case WM_MOUSEWHEEL:
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                edZoom(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? +1 : -1);
                return 0;
            }
            break;
    }
    LRESULT r = CallWindowProcW(g_editProc, hwnd, msg, wp, lp);
    switch (msg) {
        case WM_VSCROLL:
        case WM_KEYDOWN:
        case WM_LBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_SIZE:
            gutterSync(msg == WM_SIZE);
            break;
    }
    return r;
}

// ---------------------------------------------------------------- tab strip

struct TabRect { RECT rc, close; };
static std::vector<TabRect> g_tabRects;
static RECT g_detachRect{};
static int g_tabScroll = 0;

static void layoutTabs(HWND hwnd, int pass = 0) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC hdc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(hdc, g_uiFont);

    int tagW = S(46), detachW = S(74);
    g_detachRect = {rc.right - detachW, rc.top, rc.right, rc.bottom};

    g_tabRects.assign(g_tabs.size(), TabRect{});
    int x = tagW - g_tabScroll;
    for (size_t i = 0; i < g_tabs.size(); i++) {
        std::wstring label = (g_tabs[i].modified ? L"● " : L"") + g_tabs[i].name;
        SIZE sz{};
        GetTextExtentPoint32W(hdc, label.c_str(), (int)label.size(), &sz);
        int w = sz.cx + S(44);
        g_tabRects[i].rc = {x, rc.top, x + w, rc.bottom};
        g_tabRects[i].close = {x + w - S(24), rc.top + (rc.bottom - S(18)) / 2,
                               x + w - S(6), rc.top + (rc.bottom + S(18)) / 2};
        x += w;
    }
    SelectObject(hdc, old);
    ReleaseDC(hwnd, hdc);

    // keep the active tab in view (one correction pass is always enough)
    if (pass == 0 && g_active >= 0 && g_active < (int)g_tabRects.size()) {
        int limit = rc.right - detachW;
        RECT r = g_tabRects[g_active].rc;
        int shift = 0;
        if (r.right > limit) shift = r.right - limit;
        else if (r.left < tagW) shift = r.left - tagW;
        if (shift) {
            g_tabScroll = std::max(0, g_tabScroll + shift);
            layoutTabs(hwnd, 1);
        }
    }
}

static LRESULT CALLBACK tabbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            layoutTabs(hwnd);
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);

            HBRUSH bg = CreateSolidBrush(T.tabbarBg);
            FillRect(mem, &rc, bg);
            DeleteObject(bg);
            SetBkMode(mem, TRANSPARENT);

            SelectObject(mem, g_uiSmall);
            SetTextColor(mem, T.tabFgDim);
            RECT tag{S(10), rc.top, S(46), rc.bottom};
            DrawTextW(mem, L"INER", -1, &tag, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            for (size_t i = 0; i < g_tabs.size(); i++) {
                RECT r = g_tabRects[i].rc;
                if (r.right < S(46) || r.left > g_detachRect.left) continue;
                bool on = (int)i == g_active;
                HBRUSH b = CreateSolidBrush(on ? T.tabActive : T.tabInactive);
                FillRect(mem, &r, b);
                DeleteObject(b);
                if (on) {
                    RECT top{r.left, r.top, r.right, r.top + S(2)};
                    HBRUSH acc = CreateSolidBrush(T.statusBg);
                    FillRect(mem, &top, acc);
                    DeleteObject(acc);
                }
                RECT edge{r.right - 1, r.top, r.right, r.bottom};
                HBRUSH ln = CreateSolidBrush(T.line);
                FillRect(mem, &edge, ln);
                DeleteObject(ln);

                SelectObject(mem, g_uiFont);
                SetTextColor(mem, on ? T.tabFg : T.tabFgDim);
                RECT tr{r.left + S(10), r.top, r.right - S(26), r.bottom};
                std::wstring label = (g_tabs[i].modified ? L"● " : L"") + g_tabs[i].name;
                DrawTextW(mem, label.c_str(), -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT cl = g_tabRects[i].close;
                DrawTextW(mem, L"×", -1, &cl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            SelectObject(mem, g_uiSmall);
            SetTextColor(mem, T.ghostFg);
            DrawTextW(mem, paneDetached(true) ? L"⧉ Dock" : L"⧉ Detach", -1,
                      &g_detachRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (msg == WM_LBUTTONDOWN && PtInRect(&g_detachRect, pt)) {
                togglePane(true);
                return 0;
            }
            for (size_t i = 0; i < g_tabRects.size(); i++) {
                if (!PtInRect(&g_tabRects[i].rc, pt)) continue;
                if (msg == WM_MBUTTONDOWN) { tabsClose((int)i); return 0; }
                if (msg == WM_RBUTTONDOWN) {
                    POINT screen = pt;
                    ClientToScreen(hwnd, &screen);
                    int cmd = popupMenu(hwnd, screen.x, screen.y,
                                        {{IDC_CLOSETAB, L"Close"},
                                         {IDC_CLOSEOTHERS, L"Close Others"},
                                         {IDC_CLOSEALL, L"Close All"}});
                    if (cmd == IDC_CLOSETAB) tabsClose((int)i);
                    else if (cmd == IDC_CLOSEOTHERS) tabsCloseOthers((int)i);
                    else if (cmd == IDC_CLOSEALL) tabsCloseAll();
                    return 0;
                }
                if (PtInRect(&g_tabRects[i].close, pt)) tabsClose((int)i);
                else tabsActivate((int)i);
                return 0;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void tabbarRefresh() {
    if (g_tabbar) InvalidateRect(g_tabbar, nullptr, FALSE);
}

// ---------------------------------------------------------------- pane

static void inerLayout(HWND hwnd) {
    if (!g_edit) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int bar = S(35);
    MoveWindow(g_tabbar, 0, 0, rc.right, bar, TRUE);
    int gw = gutterWidth();
    bool empty = g_tabs.empty();
    ShowWindow(g_gutter, empty ? SW_HIDE : SW_SHOW);
    ShowWindow(g_edit, empty ? SW_HIDE : SW_SHOW);
    if (!empty) {
        MoveWindow(g_gutter, 0, bar, gw, rc.bottom - bar, TRUE);
        MoveWindow(g_edit, gw, bar, std::max(0L, rc.right - gw), rc.bottom - bar, TRUE);
    }
    gutterSync(true);
}

static LRESULT CALLBACK inerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            inerLayout(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(T.editorBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_tabs.empty()) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                rc.top += S(35);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, T.tabFgDim);
                SelectObject(hdc, g_uiFont);
                DrawTextW(hdc, L"No file open", -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETFOCUS:
            if (!g_tabs.empty()) SetFocus(g_edit);
            return 0;
        case WM_COMMAND:
            if ((HWND)lp == g_edit && HIWORD(wp) == EN_CHANGE && !g_quiet) {
                int total = lineCount();
                CHARRANGE cr;
                getSel(cr);
                int line = lineOfChar(cr.cpMin);
                if (abs(total - g_lastLineCount) > 1) highlightAll();
                else highlightLines(line - 1, line + 1);
                g_lastLineCount = total;
                if (g_errorLine > 0) edSetErrorLine(-1);
                bool mod = SendMessageW(g_edit, EM_GETMODIFY, 0, 0) != 0;
                if (g_active >= 0 && g_tabs[g_active].modified != mod) {
                    g_tabs[g_active].modified = mod;
                    tabsChanged();
                }
                gutterSync(true);
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)lp;
            if (nm->hwndFrom == g_edit && nm->code == EN_SELCHANGE) {
                SELCHANGE* sc = (SELCHANGE*)lp;
                int line = lineOfChar(sc->chrg.cpMin);
                statusPos(line + 1, sc->chrg.cpMin - lineIndex(line) + 1);
                gutterSync(false);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- public

HWND inerCreate(HWND parent) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = inerProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LuminaIner";
        RegisterClassW(&wc);
        wc.lpfnWndProc = tabbarProc;
        wc.lpszClassName = L"LuminaTabs";
        RegisterClassW(&wc);
        wc.lpfnWndProc = gutterProc;
        wc.lpszClassName = L"LuminaGutter";
        RegisterClassW(&wc);
    }
    g_iner = CreateWindowExW(0, L"LuminaIner", nullptr,
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                             0, 0, 10, 10, parent, nullptr, g_inst, nullptr);
    g_tabbar = CreateWindowExW(0, L"LuminaTabs", nullptr, WS_CHILD | WS_VISIBLE,
                               0, 0, 10, 10, g_iner, (HMENU)IDC_TABBAR, g_inst, nullptr);
    g_gutter = CreateWindowExW(0, L"LuminaGutter", nullptr, WS_CHILD | WS_VISIBLE,
                               0, 0, 10, 10, g_iner, nullptr, g_inst, nullptr);
    g_edit = CreateWindowExW(0, MSFTEDIT_CLASS, nullptr,
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                 ES_NOHIDESEL | ES_WANTRETURN,
                             0, 0, 10, 10, g_iner, (HMENU)IDC_EDIT, g_inst, nullptr);
    g_editProc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)editSubclass);
    SendMessageW(g_edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL);
    SendMessageW(g_edit, EM_EXLIMITTEXT, 0, 0x7FFFFF00);
    SendMessageW(g_edit, EM_SETUNDOLIMIT, 400, 0);
    SendMessageW(g_edit, EM_SETTARGETDEVICE, 0, 1);   // no word wrap: 1 line = 1 line

    IRichEditOle* ole = nullptr;
    if (SendMessageW(g_edit, EM_GETOLEINTERFACE, 0, (LPARAM)&ole) && ole) {
        ole->QueryInterface(__uuidof(ITextDocument), (void**)&g_doc);
        ole->Release();
    }

    edSetFontSize(g_editorFontSize);
    edTheme();
    return g_iner;
}

void edSetText(const std::wstring& text) {
    bool outer = !g_quiet;
    g_quiet = true;
    SETTEXTEX st{};
    st.flags = ST_DEFAULT;
    st.codepage = 1200;
    SendMessageW(g_edit, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)text.c_str());
    g_errorLine = -1;
    g_lastLineCount = lineCount();
    highlightAll();
    setSel(0, 0);
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    SendMessageW(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
    if (outer) g_quiet = false;
    if (g_iner) inerLayout(g_iner);
    statusPos(1, 1);
}

std::wstring edGetText() {
    GETTEXTLENGTHEX gl{};
    gl.flags = GTL_DEFAULT;
    gl.codepage = 1200;
    LRESULT len = SendMessageW(g_edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    if (len <= 0) return L"";
    std::wstring buf((size_t)len + 2, L'\0');
    GETTEXTEX gt{};
    gt.cb = (DWORD)((len + 2) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    LRESULT got = SendMessageW(g_edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&buf[0]);
    buf.resize((size_t)(got > 0 ? got : 0));
    for (auto& c : buf) if (c == L'\r') c = L'\n';
    return buf;
}

void edSetErrorLine(int line) {
    int old = g_errorLine;
    g_errorLine = -1;
    if (old > 0 && old <= lineCount()) highlightLines(old - 1, old - 1);
    g_errorLine = (line > 0 && line <= lineCount()) ? line : -1;
    if (g_errorLine > 0) {
        highlightLines(g_errorLine - 1, g_errorLine - 1);
        int at = lineIndex(g_errorLine - 1);
        setSel(at, at);
        SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    }
    // nothing about the text changed, only its colours, so ask for the repaint
    InvalidateRect(g_edit, nullptr, FALSE);
    gutterSync(true);
}

void edFocus() { if (g_edit && !g_tabs.empty()) SetFocus(g_edit); }

void edSetFontSize(int px) {
    g_editorFontSize = std::max(8, std::min(48, px));
    iniSetInt(L"editorFontSize", g_editorFontSize);
    if (g_edit) {
        Silent silent;
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE;
        cf.yHeight = g_editorFontSize * 15;      // px -> twips at the usual 96dpi
        lstrcpynW(cf.szFaceName, L"Consolas", 32);
        SendMessageW(g_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    if (g_editFont) DeleteObject(g_editFont);
    g_editFont = monoFont(g_editorFontSize);
    if (g_iner) inerLayout(g_iner);
}

void edZoom(int delta) { edSetFontSize(g_editorFontSize + delta); }

void edTheme() {
    if (!g_edit) return;
    SendMessageW(g_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)T.editorBg);
    highlightAll();
    InvalidateRect(g_edit, nullptr, FALSE);
    InvalidateRect(g_iner, nullptr, TRUE);
}

void edTrigger(int cmd) {
    switch (cmd) {
        case IDC_FIND: openFind(false); break;
        case IDC_REPLACE: openFind(true); break;
        case IDC_GOTOLINE: gotoLine(); break;
        case IDC_COMMENT: toggleComment(); break;
    }
}
