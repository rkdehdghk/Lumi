/*
 * Lumina - the IDE for the Lumi language, rebuilt on plain Win32.
 *
 * One process, no dependencies: the tabs, the buffers and the child processes
 * are ordinary globals, and a detached pane is the same child window moved to
 * another frame with SetParent - so there is no state to keep in sync.
 */
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _RICHEDIT_VER 0x0500
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <string>
#include <vector>

// ------------------------------------------------------------------ scale
extern int g_dpi;
inline int S(int px) { return MulDiv(px, g_dpi, 96); }

// ------------------------------------------------------------------ theme
struct Theme {
    COLORREF bg, editorBg, editorFg, toolbarBg, sidebarBg, sidebarFg, sidebarSel,
        selFg, listHover, tabbarBg, tabActive, tabInactive, tabFg, tabFgDim,
        error, errorBg, success, termBg, termFg, termHeader, termInfo, termInput,
        btnBg, btnHover, btnFg, ghostFg, ghostHover, accentBg, accentFg,
        line, border, focus, statusBg, statusFg, widgetBg, inputBg,
        synKeyword, synString, synNumber, synComment, synBuiltin;
};
extern Theme T;
extern bool g_dark;
void themeSet(bool dark);      // colours only (util.cpp)
void themeApply(bool dark);    // colours + repaint everything (main.cpp)

// ------------------------------------------------------------------ fonts
extern HFONT g_uiFont, g_uiBold, g_uiSmall;
extern int g_editorFontSize, g_termFontSize;
HFONT monoFont(int px);

// ------------------------------------------------------------------ util.cpp
std::wstring widen(const char* s, int len);
std::string narrow(const std::wstring& s);
std::wstring readFileText(const std::wstring& path, bool* ok);
bool writeFileText(const std::wstring& path, const std::wstring& text);
std::wstring baseName(const std::wstring& p);
std::wstring dirName(const std::wstring& p);
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
bool pathExists(const std::wstring& p);
bool pathIsDir(const std::wstring& p);
bool endsWithI(const std::wstring& s, const std::wstring& suffix);
std::wstring lowerOf(const std::wstring& s);
std::wstring trimOf(const std::wstring& s);
int fuzzyRank(const std::wstring& query, const std::wstring& text);
std::wstring iniGet(const wchar_t* key, const std::wstring& def);
int iniGetInt(const wchar_t* key, int def);
void iniSet(const wchar_t* key, const std::wstring& v);
void iniSetInt(const wchar_t* key, int v);
void errorBox(const std::wstring& title, const std::wstring& msg);
bool confirmBox(const std::wstring& title, const std::wstring& msg);
std::wstring lastErrorText();

// ------------------------------------------------------------------ lang.cpp
enum TokKind { TK_PLAIN, TK_KEYWORD, TK_STRING, TK_NUMBER, TK_COMMENT, TK_BUILTIN };
struct Tok { int start, len; TokKind kind; };
void lumiTokenize(const wchar_t* text, int len, std::vector<Tok>& out);
COLORREF tokColor(TokKind k);
bool tokBold(TokKind k);

// ------------------------------------------------------------------ bars
// One painted strip, used for the toolbar, the OUTER header and the status bar.
struct BarItem {
    int id = 0;
    std::wstring text;
    enum Kind { Btn, Accent, Ghost, Label, Tag, Seg, Spacer } kind = Ghost;
    bool active = false, hidden = false;
    RECT rc{};
};
struct Bar {
    std::vector<BarItem> items;
    bool status = false, running = false;
    COLORREF bg = 0;
    int hot = -1;
};
HWND barCreate(HWND parent, int id, bool statusStyle);
Bar* barOf(HWND bar);
void barLayout(HWND bar);
BarItem* barItem(HWND bar, int id);
void barUpdate(HWND bar);   // re-layout + repaint

// ------------------------------------------------------------------ tabs
struct Tab {
    int id = 0;             // survives index shuffling, so the editor knows
    std::wstring path;      // empty for an untitled scratch tab
    std::wstring name;
    std::wstring content;   // only meaningful while the tab is not active
    bool modified = false;
};
extern std::vector<Tab> g_tabs;
extern int g_active;
extern std::wstring g_folder;

void tabsOpen(const std::wstring& path);
void tabsNew();
void tabsActivate(int i);
void tabsClose(int i);
void tabsCloseOthers(int i);
void tabsCloseAll();
bool tabsSave(int i, bool saveAs = false);
void tabsChanged();          // tab strip + window title + status bar
void tabsPathRenamed(const std::wstring& from, const std::wstring& to);
void tabsPathRemoved(const std::wstring& path);

// ------------------------------------------------------------------ editor.cpp
extern HWND g_iner, g_edit;
HWND inerCreate(HWND parent);
void edSetText(const std::wstring& text);
std::wstring edGetText();
void edSetErrorLine(int line);        // 1-based, -1 clears
void edFocus();
void edSetFontSize(int px);
void edZoom(int delta);
void edTheme();
void edTrigger(int cmd);              // IDC_FIND / IDC_REPLACE / ...
bool edFindMessage(LPARAM lp);        // FINDMSGSTRING from the frame
void tabbarRefresh();

// ------------------------------------------------------------------ console.cpp
enum Panel { P_TERM = 0, P_REPL = 1 };
// what a piece of console output is, rather than what colour it is today: the
// theme can change under it, and then every chunk is coloured again.
enum OutClass { OC_TEXT, OC_ERROR, OC_SUCCESS, OC_INFO, OC_USERIN, OC_PROMPT };
extern HWND g_outer;
HWND outerCreate(HWND parent);
void outAppend(int panel, const std::wstring& text, int cls);
void outClear(int panel);
void panelSelect(int panel);
int panelCurrent();
void runCode();
void stopRun();
bool runBusy();
void conSubmitLine(const std::wstring& text);
void conSetCwd(const std::wstring& folder);
void conTheme();
void conSetFontSize(int px);
void conZoom(int delta);
void conFocus();
void conProcOut(WPARAM which, LPARAM payload);
void conProcDone(WPARAM which);
void conKillAll();
std::wstring interpreterPath();

// ------------------------------------------------------------------ explorer.cpp
extern HWND g_side;
HWND sideCreate(HWND parent);
void treeRefresh();
void treeSetFolder(const std::wstring& folder);
void treeNewFile();
void treeNewFolder();
void treeRenameSelected();
void treeDeleteSelected();
void sideTheme();
struct FileHit { std::wstring name, path, rel; };
void walkDir(const std::wstring& root, std::vector<FileHit>& out);

// ------------------------------------------------------------------ quick.cpp
bool askText(HWND owner, const std::wstring& title, const std::wstring& initial,
             std::wstring& out);
void quickFiles();
void quickCommands();
struct MenuEntry { int id; const wchar_t* text; };   // id 0 = separator
int popupMenu(HWND owner, int x, int y, const std::vector<MenuEntry>& items);

// ------------------------------------------------------------------ frame
extern HWND g_main;
extern HINSTANCE g_inst;
void statusPos(int line, int col);
void statusFlash(const std::wstring& text);
void statusRunning(bool running);
void toggleSidebar();
void togglePanel();
void togglePane(bool iner);          // detach / dock
bool paneDetached(bool iner);
void frameLayout(HWND frame);
void runCommand(int id);             // one entry point for menus, keys, palette

// ------------------------------------------------------------------ messages
#define WM_PROC_OUT  (WM_APP + 1)    // wParam = which child, lParam = std::wstring*
#define WM_PROC_DONE (WM_APP + 2)    // wParam = which child

// ------------------------------------------------------------------ commands
enum {
    IDC_NEW = 100, IDC_OPEN, IDC_SAVE, IDC_SAVEAS, IDC_RUN, IDC_STOP, IDC_CLEAR,
    IDC_NEWFILE, IDC_NEWFOLDER, IDC_NEWTAB, IDC_OPENFILE, IDC_OPENFOLDER,
    IDC_DETACH_INER, IDC_DETACH_OUTER, IDC_SEG_TERM, IDC_SEG_REPL, IDC_REFRESH,
    IDC_THEME, IDC_THEME_LIGHT, IDC_THEME_DARK, IDC_STATUS_RUN, IDC_TITLE,
    IDC_ST_MSG, IDC_ST_POS, IDC_ST_INDENT, IDC_ST_LANG, IDC_TAG,
    IDC_QUICK_FILES, IDC_QUICK_CMDS, IDC_TOGGLE_SIDE, IDC_TOGGLE_PANEL,
    IDC_CLOSETAB, IDC_CLOSEOTHERS, IDC_CLOSEALL, IDC_NEXTTAB, IDC_PREVTAB,
    IDC_FIND, IDC_REPLACE, IDC_GOTOLINE, IDC_COMMENT, IDC_FORMAT,
    IDC_ZOOMIN, IDC_ZOOMOUT, IDC_ZOOMRESET, IDC_RENAME, IDC_DELETE,
    IDC_TREE, IDC_EDIT, IDC_OUT_TERM, IDC_OUT_REPL, IDC_CMD, IDC_TABBAR
};
