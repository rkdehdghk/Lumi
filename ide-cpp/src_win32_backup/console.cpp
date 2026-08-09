/*
 * OUTER: the terminal, the Lumi REPL, and the child processes behind them.
 *
 * Each child gets one pipe for its output and one for its input.  A reader
 * thread turns bytes into text and posts it to the window; everything that
 * touches state happens on the UI thread, so no locks are needed.
 */
#include "lumina.h"
#include <windowsx.h>
#include <algorithm>

HWND g_outer = nullptr;
static HWND g_head = nullptr, g_out[2] = {nullptr, nullptr}, g_cmd = nullptr;
static WNDPROC g_cmdProc = nullptr;
static HFONT g_termFont = nullptr;

enum { CH_LUMI = 0, CH_SHELL = 1, CH_REPL = 2 };
enum TermMode { M_IDLE, M_LUMI, M_SHELL };

static int g_panel = P_TERM;
static TermMode g_termMode = M_IDLE;
static std::wstring g_termCwd;
static bool g_termAwaitingInput = false;
static bool g_replBusy = false, g_replAwaitingInput = false;
static std::vector<std::wstring> g_replBuffer;
static std::vector<std::wstring> g_history[2];
static int g_historyAt[2] = {-1, -1};
static std::wstring g_scratch;
static int g_pendingErrorLine = -1;

/* lumi.exe marks two things in its output with a record-separator line:
   RS+"INPUT" (input() is waiting for a line) and RS+"DONE" (this run ended). */
static const wchar_t RS = L'\x1e';
static const wchar_t* MARK_INPUT = L"\x1e" L"INPUT\n";
static const wchar_t* MARK_DONE = L"\x1e" L"DONE\n";

// ---------------------------------------------------------------- children

struct Child {
    HANDLE proc = nullptr, job = nullptr, stdinW = nullptr;
    std::wstring pending;      // text held back for a marker split across reads
    bool running = false;
};
static Child g_child[3];

struct ReaderArg { HANDLE read; int which; };

/** How many bytes of `s` end on a complete UTF-8 character. */
static size_t completeUtf8(const char* s, size_t n) {
    size_t back = 0;
    while (back < 3 && back < n) {
        unsigned char c = (unsigned char)s[n - 1 - back];
        if ((c & 0xC0) == 0x80) { back++; continue; }        // continuation byte
        size_t need = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3
                    : (c & 0xF8) == 0xF0 ? 4 : 1;
        return need > back + 1 ? n - back - 1 : n;
    }
    return n;
}

static DWORD WINAPI readerThread(LPVOID param) {
    ReaderArg* arg = (ReaderArg*)param;
    std::string carry;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(arg->read, buf, sizeof(buf), &got, nullptr) && got > 0) {
        carry.append(buf, got);
        size_t take = completeUtf8(carry.data(), carry.size());
        if (take == 0) continue;
        std::wstring* text = new std::wstring(widen(carry.data(), (int)take));
        carry.erase(0, take);
        PostMessageW(g_main, WM_PROC_OUT, arg->which, (LPARAM)text);
    }
    if (!carry.empty())
        PostMessageW(g_main, WM_PROC_OUT, arg->which,
                     (LPARAM) new std::wstring(widen(carry.data(), (int)carry.size())));
    CloseHandle(arg->read);
    PostMessageW(g_main, WM_PROC_DONE, arg->which, 0);
    delete arg;
    return 0;
}

static bool spawnChild(int which, const std::wstring& cmdline, const std::wstring& cwd) {
    Child& c = g_child[which];
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outR = nullptr, outW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&inR, &inW, &sa, 0)) return false;
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = outW;
    PROCESS_INFORMATION pi{};
    std::wstring line = cmdline;
    line.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, &line[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(outW);
    CloseHandle(inR);
    if (!ok) {
        CloseHandle(outR);
        CloseHandle(inW);
        return false;
    }

    // one job per child, so Stop takes the whole process tree with it
    c.job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl{};
    jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(c.job, JobObjectExtendedLimitInformation, &jl, sizeof(jl));
    AssignProcessToJobObject(c.job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    c.proc = pi.hProcess;
    c.stdinW = inW;
    c.running = true;
    c.pending.clear();

    ReaderArg* arg = new ReaderArg{outR, which};
    CloseHandle(CreateThread(nullptr, 0, readerThread, arg, 0, nullptr));
    return true;
}

static void childWrite(int which, const std::wstring& text) {
    Child& c = g_child[which];
    if (!c.stdinW) return;
    std::string bytes = narrow(text);
    DWORD wrote = 0;
    WriteFile(c.stdinW, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
}

static void childKill(int which) {
    Child& c = g_child[which];
    if (c.running && c.job) TerminateJobObject(c.job, 1);
}

static void childClose(int which) {
    Child& c = g_child[which];
    if (c.stdinW) CloseHandle(c.stdinW);
    if (c.proc) CloseHandle(c.proc);
    if (c.job) CloseHandle(c.job);
    c.stdinW = c.proc = c.job = nullptr;
    c.running = false;
}

void conKillAll() {
    for (int i = 0; i < 3; i++) childKill(i);
}

// ---------------------------------------------------------------- output view

static void scrollBottom(HWND out) {
    SendMessageW(out, WM_VSCROLL, SB_BOTTOM, 0);
}

struct Chunk { int len; int cls; };
static std::vector<Chunk> g_chunks[2];

static COLORREF classColor(int cls) {
    switch (cls) {
        case OC_ERROR: return T.error;
        case OC_SUCCESS:
        case OC_PROMPT: return T.success;
        case OC_INFO: return T.termInfo;
        case OC_USERIN: return T.termInput;
        default: return T.termFg;
    }
}

static int outLength(HWND out) {
    GETTEXTLENGTHEX gl{};
    gl.flags = GTL_NUMCHARS | GTL_PRECISE;
    gl.codepage = 1200;
    return (int)SendMessageW(out, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
}

static void colorRange(HWND out, int a, int b, int cls) {
    CHARRANGE at{a, b};
    SendMessageW(out, EM_EXSETSEL, 0, (LPARAM)&at);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.crTextColor = classColor(cls);
    cf.dwEffects = cls == OC_PROMPT ? CFE_BOLD : 0;
    SendMessageW(out, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

void outAppend(int panel, const std::wstring& text, int cls) {
    if (text.empty()) return;
    HWND out = g_out[panel];
    CHARRANGE keep;
    SendMessageW(out, EM_EXGETSEL, 0, (LPARAM)&keep);
    bool userSelecting = keep.cpMin != keep.cpMax;

    int before = outLength(out);
    colorRange(out, before, before, cls);
    SendMessageW(out, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    int after = outLength(out);
    g_chunks[panel].push_back({after - before, cls});

    // ponytail: keep the last ~400k characters of scrollback; raise if someone
    // ever needs to scroll further back than that.
    if (after > 400000) {
        int cut = after - 300000;
        CHARRANGE range{0, cut};
        SendMessageW(out, EM_EXSETSEL, 0, (LPARAM)&range);
        SendMessageW(out, EM_REPLACESEL, FALSE, (LPARAM)L"");
        auto& chunks = g_chunks[panel];
        while (cut > 0 && !chunks.empty()) {
            if (chunks.front().len <= cut) {
                cut -= chunks.front().len;
                chunks.erase(chunks.begin());
            } else {
                chunks.front().len -= cut;
                cut = 0;
            }
        }
    }
    if (userSelecting) SendMessageW(out, EM_EXSETSEL, 0, (LPARAM)&keep);
    else if (panel == g_panel) scrollBottom(out);
}

/** Re-colour everything already printed - the theme just changed under it. */
static void outRecolor(int panel) {
    HWND out = g_out[panel];
    if (g_chunks[panel].empty()) return;
    CHARRANGE keep;
    POINT scroll{};
    SendMessageW(out, EM_EXGETSEL, 0, (LPARAM)&keep);
    SendMessageW(out, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(out, WM_SETREDRAW, FALSE, 0);
    int at = 0;
    for (const Chunk& c : g_chunks[panel]) {
        colorRange(out, at, at + c.len, c.cls);
        at += c.len;
    }
    SendMessageW(out, EM_EXSETSEL, 0, (LPARAM)&keep);
    SendMessageW(out, WM_SETREDRAW, TRUE, 0);
    // the scroll position must go back last: moving the selection with drawing
    // off leaves the view wherever the last format landed
    SendMessageW(out, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    InvalidateRect(out, nullptr, TRUE);
}

void outClear(int panel) {
    SetWindowTextW(g_out[panel], L"");
    g_chunks[panel].clear();
}

int panelCurrent() { return g_panel; }

// ---------------------------------------------------------------- prompt

static std::wstring promptText() {
    if (g_panel == P_TERM)
        return g_termMode == M_IDLE ? g_termCwd + L"> " : L"";
    if (g_replAwaitingInput) return L"";
    return g_replBuffer.empty() ? L"lumi> " : L"...   ";
}

static bool inputBusy() {
    if (g_panel == P_TERM) return g_termMode == M_LUMI && !g_termAwaitingInput;
    return g_replBusy && !g_replAwaitingInput;
}

static void layoutConsole();

static void refreshPrompt() {
    EnableWindow(g_cmd, !inputBusy());
    BarItem* stop = barItem(g_head, IDC_STOP);
    if (stop) stop->hidden = (g_panel != P_TERM || g_termMode == M_IDLE);
    barUpdate(g_head);
    layoutConsole();
    InvalidateRect(g_outer, nullptr, FALSE);
    statusRunning(g_termMode != M_IDLE);
    // give the caret back after the input was re-enabled, but never steal it
    // from the editor
    HWND focus = GetFocus();
    if (IsWindowEnabled(g_cmd) && focus && (focus == g_outer || IsChild(g_outer, focus)))
        SetFocus(g_cmd);
}

void panelSelect(int panel) {
    g_panel = panel;
    ShowWindow(g_out[P_TERM], panel == P_TERM ? SW_SHOW : SW_HIDE);
    ShowWindow(g_out[P_REPL], panel == P_REPL ? SW_SHOW : SW_HIDE);
    BarItem* t = barItem(g_head, IDC_SEG_TERM);
    BarItem* r = barItem(g_head, IDC_SEG_REPL);
    if (t) t->active = panel == P_TERM;
    if (r) r->active = panel == P_REPL;
    refreshPrompt();
    scrollBottom(g_out[panel]);
}

// ---------------------------------------------------------------- markers

/** Pull the markers out of `raw` and return the plain text the user sees. */
static std::wstring handleMarkers(int which, const std::wstring& rawIn,
                                  bool* sawInput, bool* sawDone) {
    Child& c = g_child[which];
    std::wstring raw = c.pending + rawIn;
    c.pending.clear();
    std::wstring out;
    size_t i = 0;
    while (i < raw.size()) {
        size_t at = raw.find(RS, i);
        if (at == std::wstring::npos) { out += raw.substr(i); break; }
        out += raw.substr(i, at - i);
        if (raw.compare(at, wcslen(MARK_INPUT), MARK_INPUT) == 0) {
            *sawInput = true;
            i = at + wcslen(MARK_INPUT);
        } else if (raw.compare(at, wcslen(MARK_DONE), MARK_DONE) == 0) {
            *sawDone = true;
            i = at + wcslen(MARK_DONE);
        } else if (raw.size() - at < 7) {
            c.pending = raw.substr(at);       // a marker split across two reads
            break;
        } else {
            out += RS;
            i = at + 1;
        }
    }
    return out;
}

static int errorLineOf(const std::wstring& text) {
    size_t at = text.find(L"Error: [line ");
    if (at == std::wstring::npos) return -1;
    return _wtoi(text.c_str() + at + 13);
}

// ---------------------------------------------------------------- running Lumi

std::wstring interpreterPath() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = dirName(exe);
    const wchar_t* tries[] = {L"lumi.exe", L"..\\c-interpreter\\bin\\lumi.exe",
                              L"..\\..\\c-interpreter\\bin\\lumi.exe",
                              L"..\\..\\..\\c-interpreter\\bin\\lumi.exe"};
    for (const wchar_t* rel : tries) {
        std::wstring p = joinPath(dir, rel);
        wchar_t full[MAX_PATH]{};
        if (GetFullPathNameW(p.c_str(), MAX_PATH, full, nullptr) && pathExists(full))
            return full;
    }
    return joinPath(dir, L"lumi.exe");
}

bool runBusy() { return g_termMode != M_IDLE; }

void runCode() {
    if (g_termMode != M_IDLE) return;
    if (g_active < 0) return;
    panelSelect(P_TERM);
    edSetErrorLine(-1);
    g_pendingErrorLine = -1;

    const Tab& tab = g_tabs[g_active];
    std::wstring name = tab.path.empty() ? L"Untitled" : baseName(tab.path);
    outAppend(P_TERM, L"> run " + name + L"\n", OC_TEXT);

    // The editor may hold unsaved edits, so run exactly what is on screen:
    // write it to a scratch file and tell lumi.exe where 'bring' should look.
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    g_scratch = joinPath(tmp, L"lumina-run-" + std::to_wstring(GetCurrentProcessId()) +
                                  L".lumi");
    std::wstring baseDir = tab.path.empty() ? g_folder : dirName(tab.path);
    if (!writeFileText(g_scratch, edGetText())) {
        outAppend(P_TERM, L"Cannot write the scratch file: " + lastErrorText() + L"\n",
                  OC_ERROR);
        return;
    }

    std::wstring cmd = L"\"" + interpreterPath() + L"\" --ide \"" + g_scratch + L"\" \"" +
                       baseDir + L"\"";
    if (!spawnChild(CH_LUMI, cmd, baseDir)) {
        outAppend(P_TERM, L"Cannot start the Lumi interpreter: " + lastErrorText() + L"\n",
                  OC_ERROR);
        return;
    }
    g_termMode = M_LUMI;
    g_termAwaitingInput = false;
    refreshPrompt();
}

void stopRun() {
    if (g_termMode == M_IDLE) return;
    outAppend(P_TERM, L"^C\n", OC_ERROR);
    childKill(g_termMode == M_LUMI ? CH_LUMI : CH_SHELL);
}

// ---------------------------------------------------------------- OS shell

static void runShellCommand(const std::wstring& raw) {
    std::wstring cmd = trimOf(raw);
    outAppend(P_TERM, g_termCwd + L"> " + raw + L"\n", OC_PROMPT);
    if (cmd.empty()) { refreshPrompt(); return; }
    std::wstring low = lowerOf(cmd);

    if (low == L"cls" || low == L"clear") {
        outClear(P_TERM);
        return;
    }
    if (low == L"exit") {
        outAppend(P_TERM, L"(the Lumina terminal stays open)\n", OC_INFO);
        return;
    }
    if (cmd == L"cd" || low.rfind(L"cd ", 0) == 0) {
        std::wstring arg = trimOf(cmd.substr(2));
        if (!arg.empty() && arg.front() == L'"' && arg.back() == L'"')
            arg = arg.substr(1, arg.size() - 2);
        if (arg.empty()) {
            outAppend(P_TERM, g_termCwd + L"\n", OC_TEXT);
        } else {
            std::wstring target = joinPath(g_termCwd, arg);
            wchar_t full[MAX_PATH]{};
            if (arg.size() > 1 && arg[1] == L':') lstrcpynW(full, arg.c_str(), MAX_PATH);
            else GetFullPathNameW(target.c_str(), MAX_PATH, full, nullptr);
            if (pathIsDir(full)) g_termCwd = full;
            else
                outAppend(P_TERM,
                          L"The system cannot find the path specified: " + arg + L"\n",
                          OC_ERROR);
        }
        refreshPrompt();
        return;
    }
    if (cmd.size() == 2 && cmd[1] == L':' && iswalpha(cmd[0])) {
        std::wstring drive = lowerOf(cmd);
        drive[0] = (wchar_t)towupper(drive[0]);
        drive += L"\\";
        if (pathIsDir(drive)) g_termCwd = drive;
        else outAppend(P_TERM, L"The system cannot find the drive " + cmd + L"\n",
                       OC_ERROR);
        refreshPrompt();
        return;
    }

    if (!spawnChild(CH_SHELL, L"cmd.exe /d /s /c " + cmd, g_termCwd)) {
        outAppend(P_TERM, lastErrorText() + L"\n", OC_ERROR);
        return;
    }
    g_termMode = M_SHELL;
    refreshPrompt();
}

// ---------------------------------------------------------------- Lumi REPL

static bool ensureRepl() {
    if (g_child[CH_REPL].running) return true;
    std::wstring base = (g_active >= 0 && !g_tabs[g_active].path.empty())
                            ? dirName(g_tabs[g_active].path)
                            : g_folder;
    std::wstring cmd = L"\"" + interpreterPath() + L"\" --repl \"" + base + L"\"";
    if (!spawnChild(CH_REPL, cmd, base)) {
        outAppend(P_REPL, L"Cannot start the Lumi interpreter: " + lastErrorText() + L"\n",
                  OC_ERROR);
        return false;
    }
    return true;
}

static void replRun(const std::wstring& code) {
    if (!ensureRepl()) return;
    g_replBusy = true;
    refreshPrompt();
    childWrite(CH_REPL, code + L"\n\x1e" L"EOT\n");
}

/** One line typed at the REPL prompt (block collection included). */
static void replSubmit(const std::wstring& raw) {
    std::wstring stripped = trimOf(raw);
    bool inBlock = !g_replBuffer.empty();
    outAppend(P_REPL, (inBlock ? L"...   " : L"lumi> ") + raw + L"\n", OC_USERIN);

    if (inBlock) {
        if (stripped.empty()) {
            std::wstring block;
            for (size_t i = 0; i < g_replBuffer.size(); i++)
                block += (i ? L"\n" : L"") + g_replBuffer[i];
            g_replBuffer.clear();
            replRun(block);
        } else {
            g_replBuffer.push_back(raw);
            refreshPrompt();
        }
        return;
    }

    std::wstring low = lowerOf(stripped);
    if (low == L"clear" || low == L"cls") {
        outClear(P_REPL);
        return;
    }
    if (low == L"reset") {
        childKill(CH_REPL);
        g_replBuffer.clear();
        outAppend(P_REPL, L"(shell reset - all variables and functions cleared)\n",
                  OC_INFO);
        refreshPrompt();
        return;
    }
    if (stripped.empty()) return;
    if (stripped.back() == L':') {
        g_replBuffer.push_back(raw);
        refreshPrompt();
        return;
    }
    replRun(raw);
}

// ---------------------------------------------------------------- input line

void conSubmitLine(const std::wstring& text) {
    if (g_panel == P_TERM) {
        if (g_termMode == M_LUMI) {
            if (!g_termAwaitingInput) return;
            outAppend(P_TERM, text + L"\n", OC_USERIN);
            g_termAwaitingInput = false;
            refreshPrompt();
            childWrite(CH_LUMI, text + L"\n");
            return;
        }
        if (g_termMode == M_SHELL) {
            outAppend(P_TERM, text + L"\n", OC_USERIN);
            childWrite(CH_SHELL, text + L"\n");
            return;
        }
        runShellCommand(text);
        return;
    }
    if (g_replAwaitingInput) {
        outAppend(P_REPL, text + L"\n", OC_USERIN);
        g_replAwaitingInput = false;
        refreshPrompt();
        childWrite(CH_REPL, text + L"\n");
        return;
    }
    if (g_replBusy) return;
    replSubmit(text);
}

static void browseHistory(int delta) {
    auto& list = g_history[g_panel];
    if (list.empty()) return;
    int at = g_historyAt[g_panel];
    if (at < 0) at = (int)list.size();
    at += delta;
    if (at >= (int)list.size()) {
        g_historyAt[g_panel] = -1;
        SetWindowTextW(g_cmd, L"");
        return;
    }
    at = std::max(0, at);
    g_historyAt[g_panel] = at;
    SetWindowTextW(g_cmd, list[at].c_str());
    SendMessageW(g_cmd, EM_SETSEL, list[at].size(), list[at].size());
}

static LRESULT CALLBACK cmdSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            int len = GetWindowTextLengthW(hwnd);
            std::wstring text((size_t)len + 1, L'\0');
            GetWindowTextW(hwnd, &text[0], len + 1);
            text.resize((size_t)len);
            SetWindowTextW(hwnd, L"");
            g_historyAt[g_panel] = -1;
            if (!trimOf(text).empty()) g_history[g_panel].push_back(text);
            conSubmitLine(text);
            return 0;
        }
        if (wp == VK_UP) { browseHistory(-1); return 0; }
        if (wp == VK_DOWN) { browseHistory(1); return 0; }
        if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            DWORD a = 0, b = 0;
            SendMessageW(hwnd, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
            if (a == b) { stopRun(); return 0; }
        }
    }
    if (msg == WM_MOUSEWHEEL && (GetKeyState(VK_CONTROL) & 0x8000)) {
        conZoom(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? +1 : -1);
        return 0;
    }
    return CallWindowProcW(g_cmdProc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- pane

static int promptWidth() {
    HDC hdc = GetDC(g_outer);
    HGDIOBJ old = SelectObject(hdc, g_termFont);
    std::wstring p = promptText();
    SIZE sz{};
    GetTextExtentPoint32W(hdc, p.c_str(), (int)p.size(), &sz);
    SelectObject(hdc, old);
    ReleaseDC(g_outer, hdc);
    return sz.cx;
}

static void layoutConsole() {
    if (!g_outer || !g_cmd) return;
    RECT rc;
    GetClientRect(g_outer, &rc);
    int head = S(30), pad = S(8);
    int lineH = S(g_termFontSize) + S(6);
    int top = head + pad;
    int bottom = rc.bottom - pad;
    int outH = std::max(0, bottom - top - lineH);
    for (int i = 0; i < 2; i++)
        MoveWindow(g_out[i], pad, top, std::max(0L, rc.right - 2 * pad), outH, TRUE);
    int px = pad + promptWidth();
    MoveWindow(g_cmd, px, top + outH, std::max(0L, rc.right - pad - px), lineH, TRUE);
}

static LRESULT CALLBACK outerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            MoveWindow(g_head, 0, 0, rc.right, S(30), TRUE);
            layoutConsole();
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.top = S(30);
            HBRUSH b = CreateSolidBrush(T.termBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rcCmd;
            GetWindowRect(g_cmd, &rcCmd);
            MapWindowPoints(nullptr, hwnd, (POINT*)&rcCmd, 2);
            RECT pr{S(8), rcCmd.top, rcCmd.left, rcCmd.bottom};
            HBRUSH b = CreateSolidBrush(T.termBg);
            FillRect(hdc, &pr, b);
            DeleteObject(b);
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_termFont);
            SetTextColor(hdc, g_panel == P_REPL ? T.synBuiltin : T.success);
            DrawTextW(hdc, promptText().c_str(), -1, &pr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (IsWindowEnabled(g_cmd)) SetFocus(g_cmd);
            return 0;
        case WM_SETFOCUS:
            if (IsWindowEnabled(g_cmd)) SetFocus(g_cmd);
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, IsWindowEnabled(g_cmd) ? T.termFg : T.tabFgDim);
            SetBkColor(hdc, T.termBg);
            static HBRUSH brush = nullptr;
            if (brush) DeleteObject(brush);
            brush = CreateSolidBrush(T.termBg);
            return (LRESULT)brush;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SEG_TERM: panelSelect(P_TERM); return 0;
                case IDC_SEG_REPL: panelSelect(P_REPL); return 0;
                case IDC_CLEAR:
                    outClear(g_panel);
                    if (g_panel == P_REPL) { g_replBuffer.clear(); refreshPrompt(); }
                    return 0;
                case IDC_STOP: stopRun(); return 0;
                case IDC_DETACH_OUTER: togglePane(false); return 0;
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                conZoom(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? +1 : -1);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND outerCreate(HWND parent) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = outerProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.lpszClassName = L"LuminaOuter";
        RegisterClassW(&wc);
    }
    g_outer = CreateWindowExW(0, L"LuminaOuter", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, 10, 10, parent, nullptr, g_inst, nullptr);
    g_head = barCreate(g_outer, 0, false);
    Bar* bar = barOf(g_head);
    bar->bg = T.termHeader;
    bar->items = {
        {IDC_TAG, L"OUTER", BarItem::Tag},
        {IDC_SEG_TERM, L"TERMINAL", BarItem::Seg, true},
        {IDC_SEG_REPL, L"REPL", BarItem::Seg},
        {0, L"", BarItem::Spacer},
        {IDC_STOP, L"Stop", BarItem::Ghost, false, true},
        {IDC_DETACH_OUTER, L"⧉ Detach", BarItem::Ghost},
        {IDC_CLEAR, L"Clear", BarItem::Ghost},
    };

    for (int i = 0; i < 2; i++) {
        g_out[i] = CreateWindowExW(0, MSFTEDIT_CLASS, nullptr,
                                   WS_CHILD | (i == 0 ? WS_VISIBLE : 0) | WS_VSCROLL |
                                       ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                       ES_NOHIDESEL,
                                   0, 0, 10, 10, g_outer,
                                   (HMENU)(INT_PTR)(IDC_OUT_TERM + i), g_inst, nullptr);
        SendMessageW(g_out[i], EM_SETEVENTMASK, 0, 0);
        SendMessageW(g_out[i], EM_EXLIMITTEXT, 0, 0x7FFFFF00);
        SendMessageW(g_out[i], EM_SETTARGETDEVICE, 0, 0);   // wrap output to the pane
    }
    g_cmd = CreateWindowExW(0, L"EDIT", nullptr,
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            0, 0, 10, 10, g_outer, (HMENU)IDC_CMD, g_inst, nullptr);
    g_cmdProc = (WNDPROC)SetWindowLongPtrW(g_cmd, GWLP_WNDPROC, (LONG_PTR)cmdSubclass);

    g_termCwd = g_folder;
    conSetFontSize(g_termFontSize);
    conTheme();
    return g_outer;
}

void conSetFontSize(int px) {
    g_termFontSize = std::max(8, std::min(48, px));
    iniSetInt(L"termFontSize", g_termFontSize);
    if (g_termFont) DeleteObject(g_termFont);
    g_termFont = monoFont(g_termFontSize);
    SendMessageW(g_cmd, WM_SETFONT, (WPARAM)g_termFont, TRUE);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE;
    cf.yHeight = g_termFontSize * 15;
    lstrcpynW(cf.szFaceName, L"Consolas", 32);
    for (int i = 0; i < 2; i++)
        SendMessageW(g_out[i], EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    layoutConsole();
    InvalidateRect(g_outer, nullptr, TRUE);
}

void conZoom(int delta) { conSetFontSize(g_termFontSize + delta); }

void conTheme() {
    if (!g_outer) return;
    barOf(g_head)->bg = T.termHeader;
    for (int i = 0; i < 2; i++) {
        SendMessageW(g_out[i], EM_SETBKGNDCOLOR, 0, (LPARAM)T.termBg);
        outRecolor(i);
    }
    InvalidateRect(g_outer, nullptr, TRUE);
    barUpdate(g_head);
}

void conFocus() {
    if (g_cmd && IsWindowEnabled(g_cmd)) SetFocus(g_cmd);
}

void conSetCwd(const std::wstring& folder) {
    if (g_termMode != M_IDLE) return;      // a running command owns the prompt
    g_termCwd = folder;
    if (g_outer) refreshPrompt();
}

// ---------------------------------------------------------------- child events

void conProcOut(WPARAM which, LPARAM payload) {
    std::wstring* raw = (std::wstring*)payload;
    int panel = (which == CH_REPL) ? P_REPL : P_TERM;
    bool sawInput = false, sawDone = false;
    std::wstring text = handleMarkers((int)which, *raw, &sawInput, &sawDone);
    bool isError = raw->find(L"Error:") != std::wstring::npos;
    int line = errorLineOf(*raw);
    if (line > 0 && which == CH_LUMI) g_pendingErrorLine = line;
    delete raw;

    if (!text.empty())
        outAppend(panel, text, isError ? OC_ERROR : OC_TEXT);

    if (which == CH_LUMI && sawInput) {
        g_termAwaitingInput = true;
        refreshPrompt();
    }
    if (which == CH_REPL) {
        if (sawInput) { g_replAwaitingInput = true; refreshPrompt(); }
        if (sawDone) {
            g_replBusy = false;
            g_replAwaitingInput = false;
            refreshPrompt();
        }
    }
}

void conProcDone(WPARAM which) {
    DWORD code = 1;
    if (g_child[which].proc) GetExitCodeProcess(g_child[which].proc, &code);
    childClose((int)which);

    if (which == CH_LUMI) {
        if (code == 0) outAppend(P_TERM, L"\nDone.\n", OC_SUCCESS);
        if (g_pendingErrorLine > 0) {
            edSetErrorLine(g_pendingErrorLine);
            g_pendingErrorLine = -1;
        }
        g_termMode = M_IDLE;
        g_termAwaitingInput = false;
        if (!g_scratch.empty()) DeleteFileW(g_scratch.c_str());
        refreshPrompt();
    } else if (which == CH_SHELL) {
        g_termMode = M_IDLE;
        refreshPrompt();
    } else {
        g_replBusy = false;
        g_replAwaitingInput = false;
        refreshPrompt();
    }
}
