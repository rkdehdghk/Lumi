/* The sidebar: a tree view over the open folder, plus the file operations. */
#include "lumina.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <set>

HWND g_side = nullptr;
static HWND g_head = nullptr, g_tree = nullptr;
static std::wstring g_selected;
static bool g_selectedIsDir = false;

struct Node {
    std::wstring path;
    bool dir = false;
    bool filled = false;
};

static Node* nodeOf(HTREEITEM item) {
    if (!item) return nullptr;
    TVITEMW tv{};
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!TreeView_GetItem(g_tree, &tv)) return nullptr;
    return (Node*)tv.lParam;
}

static void fillLevel(HTREEITEM parent, const std::wstring& dir) {
    struct Entry { std::wstring name; bool dir; };
    std::vector<Entry> entries;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(joinPath(dir, L"*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L".." || name[0] == L'.') continue;
            entries.push_back({name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return lowerOf(a.name) < lowerOf(b.name);
    });

    for (const Entry& e : entries) {
        Node* node = new Node{joinPath(dir, e.name), e.dir, false};
        TVINSERTSTRUCTW ins{};
        ins.hParent = parent ? parent : TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        ins.item.pszText = (LPWSTR)e.name.c_str();
        ins.item.lParam = (LPARAM)node;
        ins.item.cChildren = e.dir ? 1 : 0;
        TreeView_InsertItem(g_tree, &ins);
    }
}

static void collectExpanded(HTREEITEM item, std::set<std::wstring>& out) {
    for (; item; item = TreeView_GetNextSibling(g_tree, item)) {
        UINT state = TreeView_GetItemState(g_tree, item, TVIS_EXPANDED);
        Node* n = nodeOf(item);
        if (n && (state & TVIS_EXPANDED)) {
            out.insert(lowerOf(n->path));
            collectExpanded(TreeView_GetChild(g_tree, item), out);
        }
    }
}

static void restoreExpanded(HTREEITEM item, const std::set<std::wstring>& want) {
    for (; item; item = TreeView_GetNextSibling(g_tree, item)) {
        Node* n = nodeOf(item);
        if (!n || !n->dir || !want.count(lowerOf(n->path))) continue;
        if (!n->filled) {
            n->filled = true;
            fillLevel(item, n->path);
        }
        TreeView_Expand(g_tree, item, TVE_EXPAND);
        restoreExpanded(TreeView_GetChild(g_tree, item), want);
    }
}

void treeRefresh() {
    if (!g_tree) return;
    std::set<std::wstring> expanded;
    collectExpanded(TreeView_GetRoot(g_tree), expanded);
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_tree);
    fillLevel(nullptr, g_folder);
    restoreExpanded(TreeView_GetRoot(g_tree), expanded);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, nullptr, TRUE);
}

void treeSetFolder(const std::wstring& folder) {
    g_folder = folder;
    conSetCwd(folder);
    g_selected.clear();
    if (!g_tree) return;
    TreeView_DeleteAllItems(g_tree);
    fillLevel(nullptr, g_folder);
    InvalidateRect(g_side, nullptr, TRUE);
}

// ---------------------------------------------------------------- file ops

/** Where New File / New Folder should create things: the selected folder, the
 *  folder holding the selected file, or the explorer root. */
static std::wstring targetDir() {
    if (g_selected.empty()) return g_folder;
    return g_selectedIsDir ? g_selected : dirName(g_selected);
}

void treeNewFile() {
    std::wstring name;
    if (!askText(GetActiveWindow(), L"File name:", L".lumi", name)) return;
    std::wstring path = joinPath(targetDir(), name);
    if (pathExists(path)) {
        errorBox(L"New File", L"'" + name + L"' already exists.");
        return;
    }
    if (!writeFileText(path, L"")) {
        errorBox(L"New File", lastErrorText());
        return;
    }
    treeRefresh();
    tabsOpen(path);
}

void treeNewFolder() {
    std::wstring name;
    if (!askText(GetActiveWindow(), L"Folder name:", L"", name)) return;
    std::wstring path = joinPath(targetDir(), name);
    if (pathExists(path)) {
        errorBox(L"New Folder", L"'" + name + L"' already exists.");
        return;
    }
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
        errorBox(L"New Folder", lastErrorText());
        return;
    }
    treeRefresh();
}

void treeRenameSelected() {
    if (g_selected.empty()) return;
    std::wstring old = baseName(g_selected);
    std::wstring name;
    if (!askText(GetActiveWindow(), L"New name:", old, name) || name == old) return;
    std::wstring dest = joinPath(dirName(g_selected), name);
    if (pathExists(dest)) {
        errorBox(L"Rename", L"'" + name + L"' already exists.");
        return;
    }
    if (!MoveFileW(g_selected.c_str(), dest.c_str())) {
        errorBox(L"Rename", lastErrorText());
        return;
    }
    tabsPathRenamed(g_selected, dest);
    g_selected = dest;
    treeRefresh();
}

void treeDeleteSelected() {
    if (g_selected.empty()) return;
    std::wstring name = baseName(g_selected);
    if (!confirmBox(L"Delete", L"Delete '" + name + L"'?\nThis cannot be undone."))
        return;
    std::wstring from = g_selected;
    from.push_back(L'\0');                       // SHFileOperation wants a double NUL
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationW(&op) != 0) {
        errorBox(L"Delete", L"Could not delete '" + name + L"'.");
        return;
    }
    tabsPathRemoved(g_selected);
    g_selected.clear();
    treeRefresh();
}

/** Every file under `root`, for Quick Open (Ctrl+P). */
void walkDir(const std::wstring& root, std::vector<FileHit>& out) {
    static const wchar_t* SKIP[] = {L"node_modules", L"release", L"dist",
                                    L"build", L"bin", L"__pycache__"};
    struct Walk {
        static void go(const std::wstring& root, const std::wstring& cur, int depth,
                       std::vector<FileHit>& out) {
            if (depth > 8 || out.size() >= 4000) return;
            WIN32_FIND_DATAW fd{};
            HANDLE find = FindFirstFileW(joinPath(cur, L"*").c_str(), &fd);
            if (find == INVALID_HANDLE_VALUE) return;
            do {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L".." || name[0] == L'.') continue;
                std::wstring full = joinPath(cur, name);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    bool skip = false;
                    for (const wchar_t* s : SKIP)
                        if (lowerOf(name) == s) skip = true;
                    if (!skip) go(root, full, depth + 1, out);
                } else {
                    if (out.size() >= 4000) break;
                    out.push_back({name, full, full.substr(std::min(root.size() + 1,
                                                                   full.size()))});
                }
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    };
    Walk::go(root, root, 0, out);
}

// ---------------------------------------------------------------- pane

static void sideMenu(int x, int y) {
    int cmd = popupMenu(g_side, x, y,
                        {{IDC_NEWFILE, L"New File..."},
                         {IDC_NEWFOLDER, L"New Folder..."},
                         {0, nullptr},
                         {IDC_RENAME, L"Rename..."},
                         {IDC_DELETE, L"Delete"},
                         {0, nullptr},
                         {IDC_REFRESH, L"Refresh"}});
    if (cmd) runCommand(cmd);
}

static LRESULT CALLBACK sideProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int head = S(28), name = S(22);
            MoveWindow(g_head, 0, 0, rc.right, head, TRUE);
            MoveWindow(g_tree, 0, head + name, rc.right,
                       std::max(0L, rc.bottom - head - name), TRUE);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(T.sidebarBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            RECT nr{S(12), S(28), rc.right - S(6), S(50)};
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_uiBold);
            SetTextColor(hdc, T.sidebarFg);
            std::wstring name = baseName(g_folder);
            for (auto& c : name) c = (wchar_t)towupper(c);
            DrawTextW(hdc, name.c_str(), -1, &nr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND:
            runCommand(LOWORD(wp));
            return 0;
        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)lp;
            if (nm->hwndFrom != g_tree) break;
            switch (nm->code) {
                case TVN_ITEMEXPANDINGW: {
                    NMTREEVIEWW* tv = (NMTREEVIEWW*)lp;
                    Node* n = nodeOf(tv->itemNew.hItem);
                    if (n && n->dir && !n->filled) {
                        n->filled = true;
                        fillLevel(tv->itemNew.hItem, n->path);
                    }
                    return 0;
                }
                case TVN_SELCHANGEDW: {
                    NMTREEVIEWW* tv = (NMTREEVIEWW*)lp;
                    Node* n = nodeOf(tv->itemNew.hItem);
                    if (n) {
                        g_selected = n->path;
                        g_selectedIsDir = n->dir;
                    }
                    return 0;
                }
                case TVN_DELETEITEMW: {
                    NMTREEVIEWW* tv = (NMTREEVIEWW*)lp;
                    delete (Node*)tv->itemOld.lParam;
                    return 0;
                }
                case TVN_KEYDOWN: {
                    NMTVKEYDOWN* kd = (NMTVKEYDOWN*)lp;
                    if (kd->wVKey == VK_F2) treeRenameSelected();
                    else if (kd->wVKey == VK_DELETE) treeDeleteSelected();
                    else if (kd->wVKey == VK_RETURN && !g_selected.empty() &&
                             !g_selectedIsDir)
                        tabsOpen(g_selected);
                    return 0;
                }
                case NM_CLICK: {
                    TVHITTESTINFO ht{};
                    GetCursorPos(&ht.pt);
                    ScreenToClient(g_tree, &ht.pt);
                    HTREEITEM item = TreeView_HitTest(g_tree, &ht);
                    if (!item || !(ht.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT))) return 0;
                    Node* n = nodeOf(item);
                    if (!n) return 0;
                    TreeView_SelectItem(g_tree, item);
                    g_selected = n->path;
                    g_selectedIsDir = n->dir;
                    if (n->dir) TreeView_Expand(g_tree, item, TVE_TOGGLE);
                    else tabsOpen(n->path);
                    return 0;
                }
                case NM_RCLICK: {
                    TVHITTESTINFO ht{};
                    POINT screen;
                    GetCursorPos(&screen);
                    ht.pt = screen;
                    ScreenToClient(g_tree, &ht.pt);
                    HTREEITEM item = TreeView_HitTest(g_tree, &ht);
                    if (item) {
                        TreeView_SelectItem(g_tree, item);
                        Node* n = nodeOf(item);
                        if (n) {
                            g_selected = n->path;
                            g_selectedIsDir = n->dir;
                        }
                    } else {
                        g_selected.clear();
                        g_selectedIsDir = false;
                    }
                    sideMenu(screen.x, screen.y);
                    return 1;
                }
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND sideCreate(HWND parent) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = sideProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LuminaSide";
    RegisterClassW(&wc);

    g_side = CreateWindowExW(0, L"LuminaSide", nullptr,
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10,
                             parent, nullptr, g_inst, nullptr);
    g_head = barCreate(g_side, 0, false);
    Bar* bar = barOf(g_head);
    bar->bg = T.sidebarBg;
    bar->items = {
        {IDC_TAG, L"EXPLORER", BarItem::Tag},
        {0, L"", BarItem::Spacer},
        {IDC_REFRESH, L"Refresh", BarItem::Ghost},
    };
    g_tree = CreateWindowExW(0, WC_TREEVIEWW, nullptr,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS |
                                 TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
                                 TVS_FULLROWSELECT,
                             0, 0, 10, 10, g_side, (HMENU)IDC_TREE, g_inst, nullptr);
    SendMessageW(g_tree, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    sideTheme();
    return g_side;
}

void sideTheme() {
    if (!g_tree) return;
    TreeView_SetBkColor(g_tree, T.sidebarBg);
    TreeView_SetTextColor(g_tree, T.sidebarFg);
    TreeView_SetLineColor(g_tree, T.tabFgDim);
    barOf(g_head)->bg = T.sidebarBg;
    InvalidateRect(g_side, nullptr, TRUE);
    InvalidateRect(g_tree, nullptr, TRUE);
}
