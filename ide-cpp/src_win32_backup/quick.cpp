/*
 * The small windows: the name prompt, the context menus, and the quick input
 * behind Ctrl+P (files) and Ctrl+Shift+P (commands).
 */
#include "lumina.h"
#include <windowsx.h>
#include <algorithm>

// ---------------------------------------------------------------- modal loop

/* A popup runs its own message loop with the owner disabled, which is all
   "modal" ever meant.  It ends when the popup sets done.
   No IsDialogMessage here: it swallows Enter and Escape, which are the two
   keys these popups exist to read. */
static void modalLoop(HWND owner, HWND popup, bool* done) {
    EnableWindow(owner, FALSE);
    MSG msg;
    while (!*done && IsWindow(popup) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (IsWindow(popup)) DestroyWindow(popup);
}

// ---------------------------------------------------------------- ask text

struct AskState {
    HWND edit = nullptr;
    std::wstring title;
    std::wstring result;
    bool done = false;
    bool ok = false;
};

static WNDPROC g_askEditProc = nullptr;

static LRESULT CALLBACK askEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        AskState* st = (AskState*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
        if (wp == VK_RETURN) {
            int len = GetWindowTextLengthW(hwnd);
            std::wstring text((size_t)len + 1, L'\0');
            GetWindowTextW(hwnd, &text[0], len + 1);
            text.resize((size_t)len);
            st->result = trimOf(text);
            st->ok = !st->result.empty();
        }
        st->done = true;
        return 0;
    }
    return CallWindowProcW(g_askEditProc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK askProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AskState* st = (AskState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(T.widgetBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            HBRUSH edge = CreateSolidBrush(T.border);
            FrameRect((HDC)wp, &rc, edge);
            DeleteObject(edge);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            RECT tr{S(18), S(14), rc.right - S(18), S(34)};
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_uiFont);
            SetTextColor(hdc, T.editorFg);
            DrawTextW(hdc, st->title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            SetTextColor((HDC)wp, T.editorFg);
            SetBkColor((HDC)wp, T.inputBg);
            static HBRUSH brush = nullptr;
            if (brush) DeleteObject(brush);
            brush = CreateSolidBrush(T.inputBg);
            return (LRESULT)brush;
        }
        case WM_ACTIVATE:
            if (wp == WA_INACTIVE) st->done = true;
            return 0;
        case WM_CLOSE:
            st->done = true;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool askText(HWND owner, const std::wstring& title, const std::wstring& initial,
             std::wstring& out) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = askProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LuminaAsk";
        RegisterClassW(&wc);
    }
    if (!owner) owner = g_main;
    RECT o;
    GetWindowRect(owner, &o);
    int w = S(360), h = S(96);
    AskState st;
    st.title = title;

    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LuminaAsk", nullptr,
                                 WS_POPUP | WS_BORDER,
                                 (o.left + o.right - w) / 2, o.top + S(140), w, h,
                                 owner, nullptr, g_inst, nullptr);
    SetWindowLongPtrW(popup, GWLP_USERDATA, (LONG_PTR)&st);
    st.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                              S(16), S(40), w - S(32), S(26), popup, nullptr, g_inst,
                              nullptr);
    SendMessageW(st.edit, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    g_askEditProc = (WNDPROC)SetWindowLongPtrW(st.edit, GWLP_WNDPROC, (LONG_PTR)askEditProc);
    ShowWindow(popup, SW_SHOW);
    SetActiveWindow(popup);
    SetFocus(st.edit);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);

    modalLoop(owner, popup, &st.done);
    out = st.result;
    return st.ok;
}

// ---------------------------------------------------------------- context menu

int popupMenu(HWND owner, int x, int y, const std::vector<MenuEntry>& items) {
    HMENU menu = CreatePopupMenu();
    for (const MenuEntry& e : items) {
        if (!e.id || !e.text) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        else AppendMenuW(menu, MF_STRING, e.id, e.text);
    }
    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
                                            TPM_RIGHTBUTTON,
                                  x, y, 0, owner, nullptr);
    DestroyMenu(menu);
    return cmd;
}

// ---------------------------------------------------------------- quick input

struct QuickItem {
    std::wstring label, sub, path;
    int cmd = 0;
};

static std::vector<QuickItem> g_all, g_shown;
static int g_index = 0, g_scroll = 0;
static bool g_quickDone = false;
static HWND g_quickList = nullptr, g_quickEdit = nullptr;
static WNDPROC g_quickEditProc = nullptr;
static const int QUICK_ROWS = 12;

static int rowHeight() { return S(22); }

static void filterQuick(const std::wstring& query) {
    struct Ranked { int rank; const QuickItem* item; };
    std::vector<Ranked> hits;
    for (const QuickItem& it : g_all) {
        int rank = fuzzyRank(query, it.label + L" " + it.sub);
        if (rank >= 0) hits.push_back({rank, &it});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Ranked& a, const Ranked& b) { return a.rank < b.rank; });
    g_shown.clear();
    for (size_t i = 0; i < hits.size() && i < 200; i++) g_shown.push_back(*hits[i].item);
    g_index = 0;
    g_scroll = 0;
    InvalidateRect(g_quickList, nullptr, FALSE);
}

static void moveQuick(int delta) {
    if (g_shown.empty()) return;
    g_index = (g_index + delta + (int)g_shown.size()) % (int)g_shown.size();
    if (g_index < g_scroll) g_scroll = g_index;
    if (g_index >= g_scroll + QUICK_ROWS) g_scroll = g_index - QUICK_ROWS + 1;
    InvalidateRect(g_quickList, nullptr, FALSE);
}

static QuickItem g_chosen;
static bool g_hasChoice = false;

static void chooseQuick(int at) {
    if (at < 0 || at >= (int)g_shown.size()) return;
    g_chosen = g_shown[at];
    g_hasChoice = true;
    g_quickDone = true;
}

static LRESULT CALLBACK quickListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            HBRUSH bg = CreateSolidBrush(T.widgetBg);
            FillRect(mem, &rc, bg);
            DeleteObject(bg);
            SetBkMode(mem, TRANSPARENT);

            if (g_shown.empty()) {
                SelectObject(mem, g_uiFont);
                SetTextColor(mem, T.tabFgDim);
                RECT tr{S(12), 0, rc.right, rowHeight()};
                DrawTextW(mem, L"No matching results", -1, &tr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            int h = rowHeight();
            for (int i = 0; i < QUICK_ROWS; i++) {
                int at = g_scroll + i;
                if (at >= (int)g_shown.size()) break;
                RECT r{0, i * h, rc.right, (i + 1) * h};
                if (at == g_index) {
                    HBRUSH sel = CreateSolidBrush(T.sidebarSel);
                    FillRect(mem, &r, sel);
                    DeleteObject(sel);
                }
                SelectObject(mem, g_uiFont);
                SetTextColor(mem, at == g_index ? T.selFg : T.editorFg);
                RECT lr{S(12), r.top, rc.right - S(12), r.bottom};
                DrawTextW(mem, g_shown[at].label.c_str(), -1, &lr,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (!g_shown[at].sub.empty()) {
                    SIZE sz{};
                    GetTextExtentPoint32W(mem, g_shown[at].label.c_str(),
                                          (int)g_shown[at].label.size(), &sz);
                    SelectObject(mem, g_uiSmall);
                    SetTextColor(mem, T.tabFgDim);
                    RECT sr{S(12) + sz.cx + S(10), r.top, rc.right - S(12), r.bottom};
                    DrawTextW(mem, g_shown[at].sub.c_str(), -1, &sr,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                }
            }
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            chooseQuick(g_scroll + GET_Y_LPARAM(lp) / rowHeight());
            return 0;
        case WM_MOUSEWHEEL: {
            g_scroll = std::max(0, std::min((int)g_shown.size() - QUICK_ROWS,
                                            g_scroll - (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 3 : -3)));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK quickEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        switch (wp) {
            case VK_ESCAPE: g_quickDone = true; return 0;
            case VK_RETURN: chooseQuick(g_index); return 0;
            case VK_DOWN: moveQuick(+1); return 0;
            case VK_UP: moveQuick(-1); return 0;
        }
    }
    LRESULT r = CallWindowProcW(g_quickEditProc, hwnd, msg, wp, lp);
    if (msg == WM_CHAR || msg == WM_KEYUP || msg == WM_PASTE) {
        int len = GetWindowTextLengthW(hwnd);
        std::wstring text((size_t)len + 1, L'\0');
        GetWindowTextW(hwnd, &text[0], len + 1);
        text.resize((size_t)len);
        static std::wstring last = L"\x01";
        if (text != last) {
            last = text;
            filterQuick(trimOf(text));
        }
    }
    return r;
}

static LRESULT CALLBACK quickProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(T.widgetBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            HBRUSH edge = CreateSolidBrush(T.border);
            FrameRect((HDC)wp, &rc, edge);
            DeleteObject(edge);
            return 1;
        }
        case WM_CTLCOLOREDIT: {
            SetTextColor((HDC)wp, T.editorFg);
            SetBkColor((HDC)wp, T.inputBg);
            static HBRUSH brush = nullptr;
            if (brush) DeleteObject(brush);
            brush = CreateSolidBrush(T.inputBg);
            return (LRESULT)brush;
        }
        case WM_ACTIVATE:
            if (wp == WA_INACTIVE) g_quickDone = true;
            return 0;
        case WM_CLOSE:
            g_quickDone = true;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void openQuick(const std::wstring& placeholder, std::vector<QuickItem> items) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = quickProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LuminaQuick";
        RegisterClassW(&wc);
        wc.lpfnWndProc = quickListProc;
        wc.lpszClassName = L"LuminaQuickList";
        RegisterClassW(&wc);
    }
    HWND owner = GetActiveWindow();
    if (!owner) owner = g_main;
    RECT o;
    GetWindowRect(owner, &o);
    int w = S(600), h = S(38) + QUICK_ROWS * rowHeight();
    g_all = std::move(items);
    g_index = g_scroll = 0;
    g_quickDone = false;
    g_hasChoice = false;

    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LuminaQuick", nullptr,
                                 WS_POPUP | WS_BORDER, (o.left + o.right - w) / 2,
                                 o.top + S(60), w, h, owner, nullptr, g_inst, nullptr);
    g_quickEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  S(6), S(6), w - S(12), S(26), popup, nullptr, g_inst,
                                  nullptr);
    SendMessageW(g_quickEdit, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    SendMessageW(g_quickEdit, EM_SETCUEBANNER, TRUE, (LPARAM)placeholder.c_str());
    g_quickEditProc =
        (WNDPROC)SetWindowLongPtrW(g_quickEdit, GWLP_WNDPROC, (LONG_PTR)quickEditProc);
    g_quickList = CreateWindowExW(0, L"LuminaQuickList", nullptr, WS_CHILD | WS_VISIBLE,
                                  0, S(38), w, QUICK_ROWS * rowHeight(), popup, nullptr,
                                  g_inst, nullptr);
    filterQuick(L"");
    ShowWindow(popup, SW_SHOW);
    SetActiveWindow(popup);
    SetFocus(g_quickEdit);

    modalLoop(owner, popup, &g_quickDone);
    g_quickList = g_quickEdit = nullptr;
    if (g_hasChoice) {
        if (g_chosen.cmd) runCommand(g_chosen.cmd);
        else if (!g_chosen.path.empty()) tabsOpen(g_chosen.path);
    }
    g_all.clear();
    g_shown.clear();
}

/** Ctrl+P - every file under the open folder. */
void quickFiles() {
    std::vector<FileHit> files;
    walkDir(g_folder, files);
    std::vector<QuickItem> items;
    items.reserve(files.size());
    for (const FileHit& f : files) {
        QuickItem it;
        it.label = f.name;
        it.path = f.path;
        it.sub = dirName(f.rel);
        items.push_back(it);
    }
    openQuick(L"Go to file...", std::move(items));
}

/** Ctrl+Shift+P - everything the app can do, with its shortcut. */
void quickCommands() {
    const struct { const wchar_t* label; const wchar_t* sub; int cmd; } list[] = {
        {L"File: New File...", L"", IDC_NEWFILE},
        {L"File: New Folder...", L"", IDC_NEWFOLDER},
        {L"File: New Untitled Tab", L"Ctrl+N", IDC_NEWTAB},
        {L"File: Open File...", L"Ctrl+O", IDC_OPENFILE},
        {L"File: Open Folder...", L"", IDC_OPENFOLDER},
        {L"File: Save", L"Ctrl+S", IDC_SAVE},
        {L"File: Save As...", L"Ctrl+Shift+S", IDC_SAVEAS},
        {L"File: Go to File...", L"Ctrl+P", IDC_QUICK_FILES},
        {L"Run: Run Lumi File", L"F5", IDC_RUN},
        {L"Run: Stop", L"Ctrl+C", IDC_STOP},
        {L"View: Toggle Explorer", L"Ctrl+B", IDC_TOGGLE_SIDE},
        {L"View: Toggle Panel", L"Ctrl+`", IDC_TOGGLE_PANEL},
        {L"View: Light Theme", L"", IDC_THEME_LIGHT},
        {L"View: Dark Theme", L"", IDC_THEME_DARK},
        {L"View: Show Terminal", L"", IDC_SEG_TERM},
        {L"View: Show Lumi REPL", L"", IDC_SEG_REPL},
        {L"View: Clear Panel", L"", IDC_CLEAR},
        {L"View: Detach INER (editor)", L"", IDC_DETACH_INER},
        {L"View: Detach OUTER (console)", L"", IDC_DETACH_OUTER},
        {L"View: Reset Zoom", L"Ctrl+0", IDC_ZOOMRESET},
        {L"View: Zoom In", L"Ctrl+=", IDC_ZOOMIN},
        {L"View: Zoom Out", L"Ctrl+-", IDC_ZOOMOUT},
        {L"Explorer: Refresh", L"", IDC_REFRESH},
        {L"Explorer: Rename...", L"F2", IDC_RENAME},
        {L"Explorer: Delete", L"Del", IDC_DELETE},
        {L"Editor: Find", L"Ctrl+F", IDC_FIND},
        {L"Editor: Replace", L"Ctrl+H", IDC_REPLACE},
        {L"Editor: Go to Line...", L"Ctrl+G", IDC_GOTOLINE},
        {L"Editor: Toggle Line Comment", L"Ctrl+/", IDC_COMMENT},
    };
    std::vector<QuickItem> items;
    for (const auto& e : list) {
        QuickItem it;
        it.label = e.label;
        it.sub = e.sub;
        it.cmd = e.cmd;
        items.push_back(it);
    }
    openQuick(L"Type a command...", std::move(items));
}
