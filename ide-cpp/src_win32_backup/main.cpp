/*
 * The frame: toolbar, status bar, the three panes and the keyboard.
 *
 * Detaching a pane does not copy anything - the same child window is handed to
 * another top-level frame with SetParent, and handed back when it closes.
 */
#include "lumina.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>

HWND g_main = nullptr;
HINSTANCE g_inst = nullptr;
std::vector<Tab> g_tabs;
int g_active = -1;
std::wstring g_folder;

static HWND g_toolbar = nullptr, g_status = nullptr;
static HWND g_inerFrame = nullptr, g_outerFrame = nullptr;
static HACCEL g_accel = nullptr;
static int g_sideW = 240, g_outerH = 250;
static bool g_sideVisible = true, g_panelVisible = true;
static RECT g_sashSide{}, g_sashMid{};
static enum { D_NONE, D_SIDE, D_MID } g_drag = D_NONE;
static UINT g_findMsg = 0;

// ================================================================= bars

static const wchar_t* BAR_CLASS = L"LuminaBar";

Bar* barOf(HWND bar) { return (Bar*)GetWindowLongPtrW(bar, GWLP_USERDATA); }

BarItem* barItem(HWND bar, int id) {
    Bar* b = barOf(bar);
    if (!b) return nullptr;
    for (auto& item : b->items)
        if (item.id == id) return &item;
    return nullptr;
}

void barLayout(HWND bar) {
    Bar* b = barOf(bar);
    if (!b) return;
    RECT rc;
    GetClientRect(bar, &rc);
    HDC hdc = GetDC(bar);

    int used = 0, spacers = 0;
    std::vector<int> width(b->items.size(), 0);
    for (size_t i = 0; i < b->items.size(); i++) {
        BarItem& it = b->items[i];
        if (it.hidden) continue;
        if (it.kind == BarItem::Spacer) { spacers++; continue; }
        SelectObject(hdc, it.kind == BarItem::Ghost || it.kind == BarItem::Tag ||
                                  it.kind == BarItem::Seg
                              ? g_uiSmall
                              : g_uiFont);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, it.text.c_str(), (int)it.text.size(), &sz);
        int pad = it.kind == BarItem::Btn || it.kind == BarItem::Accent ? S(28)
                : it.kind == BarItem::Label                            ? S(8)
                                                                       : S(20);
        width[i] = sz.cx + pad;
        used += width[i];
    }
    ReleaseDC(bar, hdc);

    int slack = std::max(0, (int)rc.right - used - S(12));   // 6px of air each side
    int x = S(6);
    for (size_t i = 0; i < b->items.size(); i++) {
        BarItem& it = b->items[i];
        if (it.hidden) { it.rc = {}; continue; }
        int w = it.kind == BarItem::Spacer ? (spacers ? slack / spacers : 0) : width[i];
        it.rc = {x, rc.top, x + w, rc.bottom};
        x += w;
    }
}

void barUpdate(HWND bar) {
    barLayout(bar);
    InvalidateRect(bar, nullptr, FALSE);
}

static void barPaint(HWND bar, HDC hdc, const RECT& rc) {
    Bar* b = barOf(bar);
    bool running = b->status && b->running;
    COLORREF bg = b->status ? (running ? RGB(0xcc, 0xa7, 0x00) : T.statusBg) : b->bg;
    COLORREF fg = b->status ? (running ? RGB(0x1e, 0x1e, 0x1e) : T.statusFg) : T.btnFg;

    HBRUSH back = CreateSolidBrush(bg);
    FillRect(hdc, &rc, back);
    DeleteObject(back);
    SetBkMode(hdc, TRANSPARENT);

    for (size_t i = 0; i < b->items.size(); i++) {
        BarItem& it = b->items[i];
        if (it.hidden || it.kind == BarItem::Spacer) continue;
        bool hot = (int)i == b->hot;
        RECT r = it.rc;
        COLORREF text = fg;
        if (b->status) {
            if (hot && it.id) {
                HBRUSH h = CreateSolidBrush(running ? RGB(0xd8, 0xb8, 0x22)
                                                    : RGB(0x4c, 0xb8, 0x60));
                FillRect(hdc, &r, h);
                DeleteObject(h);
            }
        } else if (it.kind == BarItem::Btn || it.kind == BarItem::Accent) {
            RECT box = r;
            InflateRect(&box, -S(3), -S(5));
            bool accent = it.kind == BarItem::Accent;
            HBRUSH h = CreateSolidBrush(accent ? T.accentBg : (hot ? T.btnHover : T.btnBg));
            FillRect(hdc, &box, h);
            DeleteObject(h);
            text = accent ? T.accentFg : T.btnFg;
        } else if (it.kind == BarItem::Ghost) {
            if (hot) {
                RECT box = r;
                InflateRect(&box, -S(2), -S(5));
                HBRUSH h = CreateSolidBrush(T.ghostHover);
                FillRect(hdc, &box, h);
                DeleteObject(h);
            }
            text = T.ghostFg;
        } else if (it.kind == BarItem::Seg) {
            text = it.active ? T.tabFg : T.tabFgDim;
            if (it.active) {
                RECT line{r.left + S(6), r.bottom - S(2), r.right - S(6), r.bottom};
                HBRUSH h = CreateSolidBrush(T.tabFg);
                FillRect(hdc, &line, h);
                DeleteObject(h);
            }
        } else if (it.kind == BarItem::Tag || it.kind == BarItem::Label) {
            text = T.tabFgDim;
        }
        SelectObject(hdc, it.kind == BarItem::Ghost || it.kind == BarItem::Tag ||
                                  it.kind == BarItem::Seg
                              ? g_uiSmall
                              : g_uiFont);
        SetTextColor(hdc, text);
        DrawTextW(hdc, it.text.c_str(), -1, &r,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static LRESULT CALLBACK barProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Bar* b = barOf(hwnd);
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
            HGDIOBJ old = SelectObject(mem, bmp);
            barPaint(hwnd, mem, rc);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            barLayout(hwnd);
            return 0;
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hot = -1;
            for (size_t i = 0; i < b->items.size(); i++)
                if (!b->items[i].hidden && b->items[i].id && PtInRect(&b->items[i].rc, pt))
                    hot = (int)i;
            if (hot != b->hot) {
                b->hot = hot;
                InvalidateRect(hwnd, nullptr, FALSE);
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            b->hot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (auto& it : b->items)
                if (!it.hidden && it.id && PtInRect(&it.rc, pt)) {
                    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(it.id, 0),
                                 (LPARAM)hwnd);
                    return 0;
                }
            return 0;
        }
        case WM_DESTROY:
            delete b;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND barCreate(HWND parent, int id, bool statusStyle) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = barProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = BAR_CLASS;
        RegisterClassW(&wc);
    }
    HWND bar = CreateWindowExW(0, BAR_CLASS, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                               parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    Bar* b = new Bar();
    b->status = statusStyle;
    b->bg = statusStyle ? T.statusBg : T.toolbarBg;
    SetWindowLongPtrW(bar, GWLP_USERDATA, (LONG_PTR)b);
    return bar;
}

// ================================================================= status bar

void statusPos(int line, int col) {
    BarItem* it = barItem(g_status, IDC_ST_POS);
    if (!it) return;
    it->text = L"Ln " + std::to_wstring(line) + L", Col " + std::to_wstring(col);
    barUpdate(g_status);
}

void statusFlash(const std::wstring& text) {
    BarItem* it = barItem(g_status, IDC_ST_MSG);
    if (!it) return;
    it->text = text;
    barUpdate(g_status);
    SetTimer(g_main, 1, 2000, nullptr);
}

void statusRunning(bool running) {
    Bar* b = barOf(g_status);
    if (!b || b->running == running) return;
    b->running = running;
    BarItem* it = barItem(g_status, IDC_STATUS_RUN);
    if (it) it->text = running ? L"■  Stop" : L"▷  Run";
    barUpdate(g_status);
}

// ================================================================= tabs

static int g_nextTabId = 1;
static int g_loadedId = 0;      // which tab the editor is actually showing

/** Put the active tab in the editor - unless it is already the one on screen,
 *  which is what keeps closing a background tab from reloading your work. */
static void loadActiveIntoEditor() {
    if (g_active < 0) {
        if (g_loadedId == 0) return;
        g_loadedId = 0;
        edSetText(L"");
        return;
    }
    if (g_tabs[g_active].id == g_loadedId) return;
    g_loadedId = g_tabs[g_active].id;
    edSetText(g_tabs[g_active].content);
    g_tabs[g_active].modified = false;
}

/** Park the text of the tab the editor is showing. */
static void stashActive() {
    for (Tab& tab : g_tabs) {
        if (tab.id != g_loadedId) continue;
        tab.content = edGetText();
        tab.modified = SendMessageW(g_edit, EM_GETMODIFY, 0, 0) != 0;
        return;
    }
}

void tabsChanged() {
    tabbarRefresh();
    BarItem* title = barItem(g_toolbar, IDC_TITLE);
    if (title) {
        title->text = g_active < 0 ? L"No file open"
                                   : (g_tabs[g_active].path.empty() ? L"Untitled"
                                                                    : g_tabs[g_active].path);
        barUpdate(g_toolbar);
    }
    BarItem* lang = barItem(g_status, IDC_ST_LANG);
    if (lang) {
        bool plain = g_active >= 0 && !endsWithI(g_tabs[g_active].name, L".lumi");
        lang->text = plain ? L"Plain Text" : L"Lumi";
        barUpdate(g_status);
    }
    if (g_iner) SendMessageW(g_iner, WM_SIZE, 0, 0);
}

void tabsOpen(const std::wstring& path) {
    for (size_t i = 0; i < g_tabs.size(); i++) {
        if (lowerOf(g_tabs[i].path) == lowerOf(path)) {
            tabsActivate((int)i);
            return;
        }
    }
    bool ok = false;
    std::wstring text = readFileText(path, &ok);
    if (!ok) {
        errorBox(L"Open failed", path + L"\n" + lastErrorText());
        return;
    }
    stashActive();
    Tab tab;
    tab.id = g_nextTabId++;
    tab.path = path;
    tab.name = baseName(path);
    tab.content = text;
    g_tabs.push_back(tab);
    g_active = (int)g_tabs.size() - 1;
    loadActiveIntoEditor();
    tabsChanged();
    edFocus();
}

void tabsNew() {
    stashActive();
    Tab tab;
    tab.id = g_nextTabId++;
    tab.name = L"Untitled";
    g_tabs.push_back(tab);
    g_active = (int)g_tabs.size() - 1;
    loadActiveIntoEditor();
    tabsChanged();
    edFocus();
}

void tabsActivate(int i) {
    if (i < 0 || i >= (int)g_tabs.size() || i == g_active) return;
    stashActive();
    g_active = i;
    loadActiveIntoEditor();
    tabsChanged();
    edFocus();
}

static bool confirmDiscard(const std::wstring& message) {
    return confirmBox(L"Unsaved changes", message);
}

void tabsClose(int i) {
    if (i < 0 || i >= (int)g_tabs.size()) return;
    stashActive();
    if (g_tabs[i].modified &&
        !confirmDiscard(L"Discard unsaved changes in " + g_tabs[i].name + L"?"))
        return;
    g_tabs.erase(g_tabs.begin() + i);
    if (g_tabs.empty()) g_active = -1;
    else if (i < g_active) g_active--;
    else if (i == g_active) g_active = std::min(i, (int)g_tabs.size() - 1);
    loadActiveIntoEditor();
    tabsChanged();
}

void tabsCloseOthers(int i) {
    if (i < 0 || i >= (int)g_tabs.size()) return;
    stashActive();
    bool dirty = false;
    for (size_t j = 0; j < g_tabs.size(); j++)
        if ((int)j != i && g_tabs[j].modified) dirty = true;
    if (dirty && !confirmDiscard(L"Discard unsaved changes in the other tabs?")) return;
    Tab keep = g_tabs[i];
    g_tabs.clear();
    g_tabs.push_back(keep);
    g_active = 0;
    loadActiveIntoEditor();
    tabsChanged();
}

void tabsCloseAll() {
    stashActive();
    bool dirty = false;
    for (auto& t : g_tabs)
        if (t.modified) dirty = true;
    if (dirty && !confirmDiscard(L"Discard unsaved changes in all tabs?")) return;
    g_tabs.clear();
    g_active = -1;
    loadActiveIntoEditor();
    tabsChanged();
}

bool tabsSave(int i, bool saveAs) {
    if (i < 0 || i >= (int)g_tabs.size()) return false;
    if (i == g_active) stashActive();
    Tab& tab = g_tabs[i];
    std::wstring dest = tab.path;
    if (dest.empty() || saveAs) {
        wchar_t file[MAX_PATH]{};
        std::wstring initial = tab.name.empty() ? L"Untitled.lumi" : tab.name;
        if (!endsWithI(initial, L".lumi")) initial += L".lumi";
        lstrcpynW(file, initial.c_str(), MAX_PATH);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFilter = L"Lumi files\0*.lumi\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = g_folder.c_str();
        ofn.lpstrDefExt = L"lumi";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return false;
        dest = file;
    }
    if (!writeFileText(dest, tab.content)) {
        errorBox(L"Save failed", dest + L"\n" + lastErrorText());
        return false;
    }
    tab.path = dest;
    tab.name = baseName(dest);
    tab.modified = false;
    if (i == g_active) SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    tabsChanged();
    treeRefresh();
    statusFlash(dest + L"  (saved)");
    return true;
}

void tabsPathRenamed(const std::wstring& from, const std::wstring& to) {
    std::wstring prefix = lowerOf(from) + L"\\";
    for (auto& tab : g_tabs) {
        if (tab.path.empty()) continue;
        std::wstring low = lowerOf(tab.path);
        if (low == lowerOf(from)) tab.path = to;
        else if (low.rfind(prefix, 0) == 0) tab.path = to + tab.path.substr(from.size());
        else continue;
        tab.name = baseName(tab.path);
    }
    tabsChanged();
}

void tabsPathRemoved(const std::wstring& path) {
    std::wstring prefix = lowerOf(path) + L"\\";
    bool changed = false;
    for (int i = (int)g_tabs.size() - 1; i >= 0; i--) {
        std::wstring low = lowerOf(g_tabs[i].path);
        if (g_tabs[i].path.empty() || (low != lowerOf(path) && low.rfind(prefix, 0) != 0))
            continue;
        if (i == g_active) stashActive();
        g_tabs.erase(g_tabs.begin() + i);
        if (i <= g_active) g_active--;
        changed = true;
    }
    if (!changed) return;
    g_active = std::max(g_tabs.empty() ? -1 : 0, std::min(g_active, (int)g_tabs.size() - 1));
    loadActiveIntoEditor();
    tabsChanged();
}

// ================================================================= panes

bool paneDetached(bool iner) { return (iner ? g_inerFrame : g_outerFrame) != nullptr; }

static LRESULT CALLBACK paneFrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND pane = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    bool iner = pane == g_iner;
    switch (msg) {
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (pane) MoveWindow(pane, 0, 0, rc.right, rc.bottom, TRUE);
            return 0;
        }
        case WM_SETFOCUS:
            if (pane) SetFocus(pane);
            return 0;
        case WM_COMMAND:
            runCommand(LOWORD(wp));
            return 0;
        case WM_CLOSE:
            if (pane) togglePane(iner);
            return 0;
    }
    if (g_findMsg && msg == g_findMsg) return edFindMessage(lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void togglePane(bool iner) {
    HWND& frame = iner ? g_inerFrame : g_outerFrame;
    HWND pane = iner ? g_iner : g_outer;
    if (frame) {                                   // dock it back
        HWND dying = frame;
        frame = nullptr;
        SetParent(pane, g_main);
        DestroyWindow(dying);
        ShowWindow(pane, SW_SHOW);
        frameLayout(g_main);
        tabbarRefresh();
        return;
    }
    static bool registered = false;
    if (!registered) {
        registered = true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = paneFrameProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)SendMessageW(g_main, WM_GETICON, ICON_BIG, 0);
        wc.lpszClassName = L"LuminaPaneFrame";
        RegisterClassW(&wc);
    }
    frame = CreateWindowExW(0, L"LuminaPaneFrame",
                            iner ? L"Lumina - INER" : L"Lumina - OUTER",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            S(780), S(460), nullptr, nullptr, g_inst, nullptr);
    SetWindowLongPtrW(frame, GWLP_USERDATA, (LONG_PTR)pane);
    SetParent(pane, frame);
    ShowWindow(pane, SW_SHOW);
    ShowWindow(frame, SW_SHOW);
    SendMessageW(frame, WM_SIZE, 0, 0);
    frameLayout(g_main);
    tabbarRefresh();
}

void toggleSidebar() {
    g_sideVisible = !g_sideVisible;
    ShowWindow(g_side, g_sideVisible ? SW_SHOW : SW_HIDE);
    frameLayout(g_main);
}

void togglePanel() {
    if (g_outerFrame) return;
    g_panelVisible = !g_panelVisible;
    ShowWindow(g_outer, g_panelVisible ? SW_SHOW : SW_HIDE);
    frameLayout(g_main);
}

// ================================================================= layout

void frameLayout(HWND frame) {
    if (frame != g_main) return;
    RECT rc;
    GetClientRect(frame, &rc);
    int toolH = S(34), statusH = S(22), sash = S(4);
    MoveWindow(g_toolbar, 0, 0, rc.right, toolH, TRUE);
    MoveWindow(g_status, 0, rc.bottom - statusH, rc.right, statusH, TRUE);

    int top = toolH, bottom = rc.bottom - statusH, left = 0;
    g_sashSide = g_sashMid = RECT{};
    if (g_sideVisible) {
        MoveWindow(g_side, 0, top, S(g_sideW), bottom - top, TRUE);
        left = S(g_sideW);
        g_sashSide = {left, top, left + sash, bottom};
        left += sash;
    }

    bool showIner = !g_inerFrame;
    bool showOuter = !g_outerFrame && g_panelVisible;
    int width = std::max(0, (int)rc.right - left);
    if (showIner && showOuter) {
        int outerH = std::min(std::max(S(90), S(g_outerH)), (bottom - top) - S(120));
        int split = bottom - outerH;
        MoveWindow(g_iner, left, top, width, split - top, TRUE);
        g_sashMid = {left, split, rc.right, split + sash};
        MoveWindow(g_outer, left, split + sash, width, bottom - split - sash, TRUE);
    } else if (showIner) {
        MoveWindow(g_iner, left, top, width, bottom - top, TRUE);
    } else if (showOuter) {
        MoveWindow(g_outer, left, top, width, bottom - top, TRUE);
    }
}

// ================================================================= commands

static std::wstring pickFile() {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"Lumi files\0*.lumi\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = g_folder.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? file : L"";
}

static std::wstring pickFolder() {
    wchar_t out[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = GetActiveWindow();
    bi.lpszTitle = L"Open folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (!idl) return L"";
    SHGetPathFromIDListW(idl, out);
    CoTaskMemFree(idl);
    return out;
}

static bool focusInConsole() {
    HWND focus = GetFocus();
    return g_outer && focus && (focus == g_outer || IsChild(g_outer, focus));
}

void runCommand(int id) {
    switch (id) {
        case IDC_NEW: {
            BarItem* it = barItem(g_toolbar, IDC_NEW);
            POINT pt{it ? it->rc.left : 0, it ? it->rc.bottom : 0};
            ClientToScreen(g_toolbar, &pt);
            int cmd = popupMenu(g_main, pt.x, pt.y,
                                {{IDC_NEWFILE, L"New File..."},
                                 {IDC_NEWFOLDER, L"New Folder..."},
                                 {0, nullptr},
                                 {IDC_NEWTAB, L"New Tab (scratch)"}});
            if (cmd) runCommand(cmd);
            return;
        }
        case IDC_OPEN: {
            BarItem* it = barItem(g_toolbar, IDC_OPEN);
            POINT pt{it ? it->rc.left : 0, it ? it->rc.bottom : 0};
            ClientToScreen(g_toolbar, &pt);
            int cmd = popupMenu(g_main, pt.x, pt.y,
                                {{IDC_OPENFILE, L"Open File..."},
                                 {IDC_OPENFOLDER, L"Open Folder..."}});
            if (cmd) runCommand(cmd);
            return;
        }
        case IDC_NEWFILE: treeNewFile(); return;
        case IDC_NEWFOLDER: treeNewFolder(); return;
        case IDC_NEWTAB: tabsNew(); return;
        case IDC_OPENFILE: {
            std::wstring p = pickFile();
            if (!p.empty()) tabsOpen(p);
            return;
        }
        case IDC_OPENFOLDER: {
            std::wstring p = pickFolder();
            if (!p.empty()) treeSetFolder(p);
            return;
        }
        case IDC_SAVE: tabsSave(g_active); return;
        case IDC_SAVEAS: tabsSave(g_active, true); return;
        case IDC_RUN: runCode(); return;
        case IDC_STOP: stopRun(); return;
        case IDC_STATUS_RUN: runBusy() ? stopRun() : runCode(); return;
        case IDC_CLEAR: outClear(panelCurrent()); return;
        case IDC_REFRESH: treeRefresh(); return;
        case IDC_THEME: themeApply(!g_dark); return;
        case IDC_THEME_LIGHT: if (g_dark) themeApply(false); return;
        case IDC_THEME_DARK: if (!g_dark) themeApply(true); return;
        case IDC_DETACH_INER: togglePane(true); return;
        case IDC_DETACH_OUTER: togglePane(false); return;
        case IDC_SEG_TERM: panelSelect(P_TERM); return;
        case IDC_SEG_REPL: panelSelect(P_REPL); return;
        case IDC_TOGGLE_SIDE: toggleSidebar(); return;
        case IDC_TOGGLE_PANEL: togglePanel(); return;
        case IDC_QUICK_FILES: quickFiles(); return;
        case IDC_QUICK_CMDS: quickCommands(); return;
        case IDC_CLOSETAB: tabsClose(g_active); return;
        case IDC_CLOSEOTHERS: tabsCloseOthers(g_active); return;
        case IDC_CLOSEALL: tabsCloseAll(); return;
        case IDC_NEXTTAB:
            if (g_tabs.size() > 1) tabsActivate((g_active + 1) % (int)g_tabs.size());
            return;
        case IDC_PREVTAB:
            if (g_tabs.size() > 1)
                tabsActivate((g_active - 1 + (int)g_tabs.size()) % (int)g_tabs.size());
            return;
        case IDC_FIND:
        case IDC_REPLACE:
        case IDC_GOTOLINE:
        case IDC_COMMENT:
            edTrigger(id);
            return;
        case IDC_ZOOMIN:
            focusInConsole() ? conZoom(+1) : edZoom(+1);
            return;
        case IDC_ZOOMOUT:
            focusInConsole() ? conZoom(-1) : edZoom(-1);
            return;
        case IDC_ZOOMRESET:
            edSetFontSize(16);
            conSetFontSize(16);
            return;
        case IDC_RENAME: treeRenameSelected(); return;
        case IDC_DELETE: treeDeleteSelected(); return;
    }
}

// ================================================================= frame proc

static LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            frameLayout(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = S(640);
            mm->ptMinTrackSize.y = S(420);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(T.line);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (PtInRect(&g_sashSide, pt)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return 1; }
            if (PtInRect(&g_sashMid, pt)) { SetCursor(LoadCursorW(nullptr, IDC_SIZENS)); return 1; }
            break;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&g_sashSide, pt)) g_drag = D_SIDE;
            else if (PtInRect(&g_sashMid, pt)) g_drag = D_MID;
            if (g_drag != D_NONE) SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_drag == D_NONE) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (g_drag == D_SIDE)
                g_sideW = std::max(150, std::min(600, MulDiv(pt.x, 96, g_dpi)));
            else
                g_outerH = std::max(90, MulDiv(rc.bottom - S(22) - pt.y, 96, g_dpi));
            frameLayout(hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_drag != D_NONE) {
                g_drag = D_NONE;
                ReleaseCapture();
            }
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                KillTimer(hwnd, 1);
                BarItem* it = barItem(g_status, IDC_ST_MSG);
                if (it) { it->text = L""; barUpdate(g_status); }
            }
            return 0;
        case WM_COMMAND:
            runCommand(LOWORD(wp));
            return 0;
        case WM_PROC_OUT:
            conProcOut(wp, lp);
            return 0;
        case WM_PROC_DONE:
            conProcDone(wp);
            return 0;
        case WM_CLOSE: {
            stashActive();
            bool dirty = false;
            for (auto& tab : g_tabs)
                if (tab.modified) dirty = true;
            if (dirty &&
                !confirmBox(L"Unsaved changes", L"Discard unsaved changes and quit?"))
                return 0;
            conKillAll();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    if (g_findMsg && msg == g_findMsg) return edFindMessage(lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ================================================================= theme

void themeApply(bool dark) {
    themeSet(dark);
    edTheme();
    conTheme();
    sideTheme();
    Bar* tb = barOf(g_toolbar);
    if (tb) tb->bg = T.toolbarBg;
    BarItem* th = barItem(g_status, IDC_THEME);
    if (th) th->text = g_dark ? L"◑  Dark" : L"◑  Light";
    barUpdate(g_toolbar);
    barUpdate(g_status);
    // every window, children included - half the app repainting is worse than
    // none of it
    UINT how = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW;
    RedrawWindow(g_main, nullptr, nullptr, how);
    if (g_inerFrame) RedrawWindow(g_inerFrame, nullptr, nullptr, how);
    if (g_outerFrame) RedrawWindow(g_outerFrame, nullptr, nullptr, how);
}

// ================================================================= boot

static std::wstring defaultFolder() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = dirName(exe);
    if (pathExists(joinPath(dir, L"lumi.exe"))) return dirName(dir);   // packaged
    std::wstring interp = interpreterPath();
    std::wstring bin = dirName(interp);                                 // ...\bin
    std::wstring project = dirName(dirName(bin));                       // repo root
    return pathIsDir(project) ? project : dir;
}

static void createFonts() {
    LOGFONTW lf{};
    lf.lfHeight = -S(13);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, L"Segoe UI", 32);
    g_uiFont = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    g_uiBold = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_NORMAL;
    lf.lfHeight = -S(11);
    g_uiSmall = CreateFontIndirectW(&lf);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_inst = inst;

    // per-monitor DPI when the OS has it, system DPI otherwise
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setCtx = (BOOL(WINAPI*)(HANDLE))GetProcAddress(user32,
                                                        "SetProcessDpiAwarenessContext");
    if (setCtx) setCtx((HANDLE)-4);          // PER_MONITOR_AWARE_V2
    else SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);

    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    LoadLibraryW(L"Msftedit.dll");
    g_findMsg = RegisterWindowMessageW(FINDMSGSTRINGW);
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");

    themeSet(iniGet(L"theme", L"light") == L"dark");
    g_editorFontSize = iniGetInt(L"editorFontSize", 16);
    g_termFontSize = iniGetInt(L"termFontSize", 16);
    createFonts();
    g_folder = defaultFolder();

    WNDCLASSW wc{};
    wc.lpfnWndProc = mainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LuminaMain";
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                                 LR_DEFAULTSIZE | LR_SHARED);
    RegisterClassW(&wc);

    g_main = CreateWindowExW(0, L"LuminaMain", L"Lumina", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, S(1180), S(780), nullptr,
                             nullptr, inst, nullptr);

    g_toolbar = barCreate(g_main, 0, false);
    barOf(g_toolbar)->items = {
        {IDC_NEW, L"New  ▾", BarItem::Btn},
        {IDC_OPEN, L"Open  ▾", BarItem::Btn},
        {IDC_SAVE, L"Save", BarItem::Btn},
        {IDC_RUN, L"Run   F5", BarItem::Accent},
        {0, L"", BarItem::Spacer},
        {IDC_TITLE, L"Untitled", BarItem::Label},
    };
    g_status = barCreate(g_main, 0, true);
    barOf(g_status)->items = {
        {IDC_STATUS_RUN, L"▷  Run", BarItem::Label},
        {IDC_ST_MSG, L"", BarItem::Label},
        {0, L"", BarItem::Spacer},
        {IDC_THEME, g_dark ? L"◑  Dark" : L"◑  Light", BarItem::Label},
        {IDC_ST_POS, L"Ln 1, Col 1", BarItem::Label},
        {IDC_ST_INDENT, L"Spaces: 4", BarItem::Label},
        {IDC_ST_LANG, L"Lumi", BarItem::Label},
    };

    sideCreate(g_main);
    inerCreate(g_main);
    outerCreate(g_main);

    Tab first;
    first.id = g_nextTabId++;
    first.name = L"Untitled";
    first.content = L"print(\"hello, Lumi!\")\n";
    g_tabs.push_back(first);
    g_active = 0;
    loadActiveIntoEditor();
    tabsChanged();
    treeSetFolder(g_folder);
    panelSelect(P_TERM);

    ACCEL keys[] = {
        {FVIRTKEY, VK_F5, IDC_RUN},
        {FVIRTKEY | FCONTROL, 'S', IDC_SAVE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', IDC_SAVEAS},
        {FVIRTKEY | FCONTROL, 'O', IDC_OPENFILE},
        {FVIRTKEY | FCONTROL, 'N', IDC_NEWTAB},
        {FVIRTKEY | FCONTROL, 'W', IDC_CLOSETAB},
        {FVIRTKEY | FCONTROL, 'P', IDC_QUICK_FILES},
        {FVIRTKEY | FCONTROL | FSHIFT, 'P', IDC_QUICK_CMDS},
        {FVIRTKEY | FCONTROL, 'B', IDC_TOGGLE_SIDE},
        {FVIRTKEY | FCONTROL, VK_OEM_3, IDC_TOGGLE_PANEL},
        {FVIRTKEY | FCONTROL, VK_TAB, IDC_NEXTTAB},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, IDC_PREVTAB},
        {FVIRTKEY | FCONTROL, 'F', IDC_FIND},
        {FVIRTKEY | FCONTROL, 'H', IDC_REPLACE},
        {FVIRTKEY | FCONTROL, 'G', IDC_GOTOLINE},
        {FVIRTKEY | FCONTROL, VK_OEM_2, IDC_COMMENT},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, IDC_ZOOMIN},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, IDC_ZOOMOUT},
        {FVIRTKEY | FCONTROL, '0', IDC_ZOOMRESET},
    };
    g_accel = CreateAcceleratorTableW(keys, (int)(sizeof(keys) / sizeof(keys[0])));

    ShowWindow(g_main, SW_SHOW);
    frameLayout(g_main);
    edFocus();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        HWND top = GetAncestor(msg.hwnd, GA_ROOT);
        if (!IsWindow(top)) top = g_main;
        if (TranslateAcceleratorW(top, g_accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    conKillAll();
    OleUninitialize();
    return 0;
}
