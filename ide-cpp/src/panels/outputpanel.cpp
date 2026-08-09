#include "outputpanel.h"
#include "../widgets/consolepane.h"
#include "../widgets/collapsedbar.h"
#include "../widgets/headerbits.h"
#include "../models/runsession.h"
#include "../core/outclass.h"
#include "../core/commands.h"
#include "../core/paths.h"
#include "../core/hostenv.h"
#include "../theme/theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>

OutputPanel::OutputPanel(RunSession *run, QWidget *parent)
    : QWidget(parent), m_run(run) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 헤더: OUTER 라벨 + 탭들 + [+] + Clear + Detach + 접기
    auto *hdr = new QFrame(this);
    m_hdr = hdr;
    hdr->setObjectName("HeaderFrame");
    hdr->setFixedHeight(30);
    auto *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(10, 0, 8, 0);
    hl->setSpacing(2);

    // 왼쪽 묶음(제목 + 터미널 탭 + [+])은 좁아지면 양보한다 — 오른쪽 버튼이 밀려나면
    // 패널 밖으로 잘려 아예 못 누르게 된다.
    m_leftBox = new ShrinkBox(hdr);
    auto *leftLay = new QHBoxLayout(m_leftBox);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(2);

    m_titleLbl = new QLabel("OUTER", m_leftBox);
    m_titleLbl->setObjectName("PanelTitle");

    m_tabRow = new QHBoxLayout;
    m_tabRow->setContentsMargins(0, 0, 0, 0);
    m_tabRow->setSpacing(0);   // INER 탭처럼 서로 맞붙는다

    m_addBtn = new QPushButton("+", m_leftBox);
    m_addBtn->setObjectName("HeaderToolBtn");
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setToolTip("새 터미널 / REPL");

    leftLay->addWidget(m_titleLbl);
    leftLay->addSpacing(10);
    leftLay->addLayout(m_tabRow);
    leftLay->addWidget(m_addBtn);
    leftLay->addStretch(1);

    // 경로는 왼쪽을 잘라야(…\Lumina\examples) 지금 폴더가 보인다.
    m_cwdLbl = new ElideLabel(hdr, Qt::ElideLeft);
    m_cwdLbl->setAlignment(Qt::AlignCenter);
    m_cwdLbl->setEnabled(false);

    m_clearBtn = new QPushButton("Clear", hdr);
    m_detachBtn = new QPushButton(QString(QChar(0x29C9)) + " Detach", hdr);
    m_clearBtn->setObjectName("HeaderToolBtn");
    m_detachBtn->setObjectName("HeaderToolBtn");
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_detachBtn->setCursor(Qt::PointingHandCursor);

    m_collapseBtn = new QPushButton(QString(QChar(0x25BE)), hdr);   // ▾ 접기
    m_collapseBtn->setObjectName("HeaderChevronBtn");
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setToolTip("Collapse / Expand");
    m_collapseBtn->setFixedHeight(24);

    // 왼쪽 묶음과 경로는 남는 자리를 나눠 갖고, 오른쪽 세 버튼은 늘 제 크기를 지킨다.
    hl->addWidget(m_leftBox, 1);
    hl->addWidget(m_cwdLbl, 2);
    hl->addWidget(m_clearBtn, 0);
    hl->addWidget(m_detachBtn, 0);
    hl->addWidget(m_collapseBtn, 0);

    m_stack = new QStackedWidget(this);

    lay->addWidget(hdr);
    lay->addWidget(m_stack, 1);

    // 세로 띠로 접혔을 때의 모습은 세 패널이 공유한다.
    m_bar = new CollapsedBar("OUTER", this);
    m_bar->hide();
    lay->addWidget(m_bar, 1);
    connect(m_bar, &CollapsedBar::clicked, this, &OutputPanel::collapseRequested);

    connect(m_clearBtn, &QPushButton::clicked, this, &OutputPanel::clearCurrent);
    connect(m_detachBtn, &QPushButton::clicked, this, &OutputPanel::detachRequested);
    connect(m_collapseBtn, &QPushButton::clicked, this, &OutputPanel::collapseRequested);
    connect(m_addBtn, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        QAction *t = menu.addAction("New Terminal");
        QAction *r = menu.addAction("New REPL");
        QAction *sel = menu.exec(m_addBtn->mapToGlobal(QPoint(0, m_addBtn->height())));
        if (sel == t) select(addSession(false));
        else if (sel == r) select(addSession(true));
    });

    // 세션 출력 라우팅 — 채널 번호로 어느 탭인지 고른다.
    connect(m_run, &RunSession::output, this, [this](int chan, int cls, const QString &text) {
        Sess *s = byChan(chan);
        if (!s) return;
        s->pane->append(text, cls);
        if (m_runSess == s) {
            QRegularExpression re("Error: \\[line (\\d+)");
            auto m = re.match(text);
            if (m.hasMatch()) m_pendingError = m.captured(1).toInt();
        }
    });
    connect(m_run, &RunSession::finished, this, [this](int chan) {
        Sess *s = byChan(chan);
        if (!s) return;
        s->awaitingInput = false;
        syncPrompt(s);
        if (m_runSess != s) return;
        if (m_pendingError > 0) {
            emit errorLineDetected(m_pendingError);
            m_pendingError = -1;
        }
        emit runningChanged(false);
        // 끝나자마자 바로 다음 명령을 칠 수 있게 (그 터미널을 보고 있을 때만).
        if (m_cur == s && isVisible()) s->pane->focusInput();
    });
    connect(m_run, &RunSession::shellInitialized, this, [this](int chan, const QString &cleanPrompt) {
        Sess *s = byChan(chan);
        if (!s) return;
        s->pane->clear();
        s->pane->append(cleanPrompt + " ", OC_PROMPT);
        // 셸이 뜨기 전에 F5 를 눌렀다면 여기서 보낸다 — 먼저 찍으면 이 clear() 가 지운다.
        if (!s->pendingCmd.isEmpty()) {
            const QString cmd = s->pendingCmd, dir = s->pendingDir;
            s->pendingCmd.clear();
            sendRun(s, cmd, dir);
        }
    });
    connect(m_run, &RunSession::awaitingInput, this, [this](int chan) {
        Sess *s = byChan(chan);
        if (!s) return;
        // input() 대기 중엔 프로그램이 찍은 물음이 곧 프롬프트다 — 우리 건 비운다.
        s->awaitingInput = true;
        if (!s->repl) s->pane->setPrompt(QString());
        if (m_cur == s) s->pane->focusInput();
    });

    // 시작할 땐 탭 없이 빈 OUTER 로 둔다 — 터미널은 [+] 나 F5(runCode) 때 만든다.
}

// ------------------------------------------------------------------- 탭 관리

OutputPanel::Sess *OutputPanel::byChan(int chan) const {
    for (Sess *s : m_sessions) if (s->chan == chan) return s;
    return nullptr;
}

OutputPanel::Sess *OutputPanel::addSession(bool repl) {
    auto *s = new Sess;
    s->repl = repl;
    s->chan = m_nextChan++;
    s->name = repl ? QString("REPL %1").arg(++m_replNo)
                   : QString("Terminal %1").arg(++m_termNo);

    s->pane = new ConsolePane(repl ? ConsolePane::Repl : ConsolePane::Shell, this);
    m_stack->addWidget(s->pane);

    // 한 칸 = [이름][×] 이고, 칠(배경 + 활성 상단선)은 묶음 위젯이 맡는다 — INER 탭과 같은 모양.
    s->tab = new QWidget(m_leftBox);
    s->tab->setObjectName("OuterTab");
    s->tab->setAttribute(Qt::WA_StyledBackground, true);
    auto *tl = new QHBoxLayout(s->tab);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(0);
    s->nameBtn = new QPushButton(s->name, s->tab);
    s->nameBtn->setObjectName("OuterTabName");
    s->nameBtn->setCheckable(true);
    s->nameBtn->setCursor(Qt::PointingHandCursor);
    s->closeBtn = new QPushButton(QString(QChar(0x00D7)), s->tab);
    s->closeBtn->setObjectName("OuterTabClose");
    s->closeBtn->setCursor(Qt::PointingHandCursor);
    s->closeBtn->setToolTip(s->name + " 닫기");
    tl->addWidget(s->nameBtn);
    tl->addWidget(s->closeBtn);
    m_tabRow->addWidget(s->tab);

    connect(s->nameBtn, &QPushButton::clicked, this, [this, s] { select(s); });
    connect(s->closeBtn, &QPushButton::clicked, this, [this, s] { closeSession(s); });
    connect(s->pane, &ConsolePane::lineSubmitted, this,
            [this, s](const QString &line) { submitted(s, line); });
    connect(s->pane, &ConsolePane::interruptRequested, this, [this, s] {
        m_run->stopChan(s->chan);
        s->pane->append("^C\n", OC_ERROR);
        s->replBuffer.clear();
        s->pendingCmd.clear();
        s->awaitingInput = false;
        syncPrompt(s);
        if (m_runSess == s) emit runningChanged(false);
    });

    m_sessions << s;
    if (!repl) m_run->ensureShell(s->chan, m_cwd);
    syncPrompt(s);
    updateTabButtons();
    return s;
}

void OutputPanel::closeSession(Sess *s) {
    m_run->stopChan(s->chan);
    m_sessions.removeOne(s);
    if (m_cur == s) m_cur = nullptr;
    if (m_runSess == s) { m_runSess = nullptr; emit runningChanged(false); }
    m_stack->removeWidget(s->pane);
    s->pane->deleteLater();
    s->tab->deleteLater();
    delete s;
    // 다 닫으면 시작할 때와 같은 빈 OUTER 로 돌아간다.
    if (!m_cur && !m_sessions.isEmpty()) select(m_sessions.first());
    updateTabButtons();
}

void OutputPanel::select(Sess *s) {
    if (!s) return;
    m_cur = s;
    m_stack->setCurrentWidget(s->pane);
    if (s->repl) m_run->ensureRepl(s->chan, m_cwd);
    else         m_run->ensureShell(s->chan, m_cwd);
    updateTabButtons();
    s->pane->focusInput();
}

void OutputPanel::updateTabButtons() {
    for (Sess *s : m_sessions) {
        s->nameBtn->setChecked(s == m_cur);
        // 동적 속성은 스타일을 다시 물려야(unpolish/polish) 화면에 반영된다.
        s->tab->setProperty("active", s == m_cur);
        s->tab->style()->unpolish(s->tab);
        s->tab->style()->polish(s->tab);
    }
}

// ------------------------------------------------------------------- 입출력

void OutputPanel::syncPrompt(Sess *s) {
    if (s->repl) {
        s->pane->setPrompt(s->replBuffer.isEmpty() ? "lumi> " : "...   ");
    } else {
        s->pane->setPrompt(QString());
    }
}

void OutputPanel::submitted(Sess *s, const QString &line) {
    if (s->awaitingInput) {
        m_run->writeStdin(s->chan, (line + "\n").toUtf8());
        s->awaitingInput = false;
        syncPrompt(s);
        return;
    }
    if (s->repl) replSubmit(s, line);
    else        runShellCommand(s, line);
}

// REPL 한 줄. `:` 로 끝나면 블록 수집을 시작하고 빈 줄에서 통째로 보낸다.
void OutputPanel::replSubmit(Sess *s, const QString &raw) {
    const QString stripped = raw.trimmed();

    if (!s->replBuffer.isEmpty()) {
        if (stripped.isEmpty()) {
            QString block = s->replBuffer.join("\n");
            s->replBuffer.clear();
            syncPrompt(s);
            m_run->ensureRepl(s->chan, m_cwd);
            m_run->replRun(s->chan, block);
        } else {
            s->replBuffer << raw;
        }
        return;
    }

    const QString low = stripped.toLower();
    if (low == "clear" || low == "cls") { s->pane->clear(); return; }
    if (low == "reset") {
        m_run->resetRepl(s->chan);
        s->replBuffer.clear();
        syncPrompt(s);
        s->pane->append("(shell reset - all variables and functions cleared)\n", OC_INFO);
        return;
    }
    if (stripped.isEmpty()) return;
    if (stripped.endsWith(':')) {
        s->replBuffer << raw;
        syncPrompt(s);
        return;
    }
    m_run->ensureRepl(s->chan, m_cwd);
    m_run->replRun(s->chan, raw);
}

void OutputPanel::runShellCommand(Sess *s, const QString &rawLine) {
    m_run->ensureShell(s->chan, m_cwd);
    QString cmd = rawLine;
    const QString eol = hostLineEnd();
    if (!cmd.endsWith(eol)) {
        if (cmd.endsWith("\n")) cmd.chop(1);
        cmd += eol;
    }
    m_run->writeStdin(s->chan, cmd.toUtf8());
    syncPrompt(s);
}

// --------------------------------------------------------------------- 런

// 런은 "지금 보고 있는 터미널"에서 돈다. 고른 탭이 REPL 이면 아무 터미널이나,
// 그것도 없으면 새로 하나 연다.
void OutputPanel::runCode(const QString &fileName, const QString &baseDir) {
    Sess *s = (m_cur && !m_cur->repl) ? m_cur : nullptr;
    if (!s) for (Sess *t : m_sessions) if (!t->repl) { s = t; break; }
    if (!s) s = addSession(false);
    select(s);
    m_runSess = s;
    m_pendingError = -1;
    s->awaitingInput = false;

    m_run->ensureShell(s->chan, baseDir);
    const QString cmdLine = interpreterCommand() + " --ide \"" + fileName + "\"";
    // 방금 뜬 셸이면 첫 프롬프트를 기다린다 — shellInitialized 가 화면을 비우기 때문에
    // 지금 찍으면 명령줄이 사라진다.
    if (m_run->starting(s->chan)) { s->pendingCmd = cmdLine; s->pendingDir = baseDir; }
    else sendRun(s, cmdLine, baseDir);
    emit runningChanged(true);
}

void OutputPanel::sendRun(Sess *s, const QString &cmdLine, const QString &baseDir) {
    m_run->cdShell(s->chan, baseDir);
    // cmd 는 /q(echo off)라 우리가 밀어 넣은 명령을 되찍지 않는다 — 직접 찍어 준다.
    // 안 찍으면 프로그램 출력이 프롬프트("C:\...>") 뒤에 그대로 붙어 버린다.
    s->pane->append(cmdLine + "\n", OC_USERIN);
    m_run->writeStdin(s->chan, (cmdLine + hostLineEnd()).toUtf8());
    syncPrompt(s);
}

// Stop 은 F5 로 돌린 프로그램만 멈춘다. 예전엔 모든 채널을 죽여서 옆 터미널과
// REPL 세션(그 안의 변수·함수까지)이 같이 날아갔다.
void OutputPanel::stopRun() {
    Sess *s = m_runSess;
    if (!s) return;
    m_run->stopChan(s->chan);
    s->awaitingInput = false;
    s->replBuffer.clear();
    s->pendingCmd.clear();
    s->pane->append("^C\n", OC_ERROR);
    syncPrompt(s);
    emit runningChanged(false);
}

bool OutputPanel::busy() const { return m_run->isBusy(); }

void OutputPanel::clearCurrent() { if (m_cur) m_cur->pane->clear(); }

void OutputPanel::setCwd(const QString &c) {
    m_cwd = c;
    // 자르는 일은 ElideLabel 이 제 폭을 보고 알아서 한다 (창을 늘리면 다시 길어진다).
    m_cwdLbl->setFullText(QDir::toNativeSeparators(c));
    m_cwdLbl->setToolTip(QDir::toNativeSeparators(c));
    for (Sess *s : m_sessions) {
        if (!s->repl) m_run->cdShell(s->chan, m_cwd);
        syncPrompt(s);
    }
}

void OutputPanel::refreshTheme() {
    for (Sess *s : m_sessions) s->pane->refreshTheme();
    update();
}

void OutputPanel::focusInput() { if (m_cur) m_cur->pane->focusInput(); }

void OutputPanel::setDetached(bool on) {
    m_detachBtn->setText(QString(QChar(0x29C9)) + (on ? " Dock" : " Detach"));
}

// 세로 띠로 접히면 공통 막대(CollapsedBar)만 보이고, 세로로 접히면 헤더 줄만 남는다.
void OutputPanel::setCollapsed(bool on, bool horizontal) {
    const bool strip = on && horizontal;
    m_stack->setVisible(!on);
    m_hdr->setVisible(!strip);
    m_bar->setVisible(strip);
    // 화살표는 "누르면 갈 방향"을 가리킨다 — 옆에 붙은 패널이면 좌우(◂▸), 위아래면 상하(▴▾).
    m_collapseBtn->setText(QString(QChar(horizontal ? (on ? 0x25B8 : 0x25C2)
                                                    : (on ? 0x25B4 : 0x25BE))));
}
