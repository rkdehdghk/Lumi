#include "runsession.h"
#include "streammarkers.h"
#include "core/outclass.h"
#include "core/paths.h"
#include "core/hostenv.h"
#include <QStringDecoder>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QProcessEnvironment>
#if !defined(Q_OS_WIN)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  include <sys/types.h>
#  include <unistd.h>
#endif

static constexpr char RS = '\x1e';

// QProcess::kill 은 셸만 죽이고 그 안에서 돌던 lumi 는 계속 돈다.
// Stop/Ctrl+C 가 정말 멈추려면 트리째 죽여야 한다 — 어떻게 죽이는지는 hostenv 가 안다.
static void killTree(QProcess *p) { hostKillTree(p); }

RunSession::RunSession(QObject *parent) : QObject(parent) {}

void RunSession::spawn(int chan, const QString &program, const QStringList &args,
                       const QString &cwd) {
    Channel &c = m_ch[chan];
    if (c.proc) {
        // 시그널을 먼저 끊는다: 안 끊으면 kill 로 나오는 finished 가 방금 세팅한
        // 새 상태를 도로 지운다.
        c.proc->disconnect(this);
        killTree(c.proc);
        c.proc->waitForFinished(2000);
        c.proc->deleteLater();
        c.proc = nullptr;
    }
    c.pending.clear();
    c.rawPending.clear();

    auto *p = new QProcess(this);
    c.proc = p;
#if !defined(Q_OS_WIN)
    // 자식을 제 무리(process group)의 우두머리로 세운다 — hostKillTree 가
    // 손자까지 한 번에 죽일 수 있는 것은 이것 덕분이다.
    p->setChildProcessModifier([] { setsid(); });
#endif
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->setWorkingDirectory(cwd);
    // 셸 프롬프트를 우리가 아는 모양으로 못박는다 (POSIX 에서만 할 일이 있다)
    const QStringList extra = hostShellEnv();
    if (!extra.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &kv : extra) {
            const int eq = kv.indexOf('=');
            if (eq > 0) env.insert(kv.left(eq), kv.mid(eq + 1));
        }
        p->setProcessEnvironment(env);
    }
    p->setProgram(program);
    p->setArguments(args);

    connect(p, &QProcess::readyReadStandardOutput, this, [this, chan] { onData(chan); });
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, chan, p](int, QProcess::ExitStatus) {
        if (p->bytesAvailable()) onData(chan);
        Channel &ch = m_ch[chan];
        // 프로세스가 끝났으니 붙일 다음 조각이 없다 — 남은 꼬리는 지금 흘려보낸다.
        if (!ch.rawPending.isEmpty()) {
            handleMarkers(chan, decode(ch.rawPending));
            ch.rawPending.clear();
        }
        ch.busy = false;
        if (ch.proc == p) { ch.proc = nullptr; p->deleteLater(); }
        emit finished(chan);
    });
    connect(p, &QProcess::errorOccurred, this, [this, chan, p](QProcess::ProcessError) {
        if (m_ch[chan].proc != p) return;
        m_ch[chan].busy = false;
        emit output(chan, OC_ERROR, "Process error: " + p->errorString() + "\n");
        emit finished(chan);
    });

    c.cwd = cwd;
    p->start();

    c.busy = true;
}

void RunSession::ensureShell(int chan, const QString &cwd) {
    if (m_ch[chan].proc && m_ch[chan].proc->state() != QProcess::NotRunning) return;
    QString targetCwd = (cwd.isEmpty() || !QFileInfo(cwd).isDir()) ? QDir::homePath() : cwd;
    m_ch[chan].initializing = true;
    m_ch[chan].pending.clear();
    spawn(chan, hostShellProgram(), hostShellArgs(), targetCwd);
    m_ch[chan].busy = false;
}

void RunSession::cdShell(int chan, const QString &cwd) {
    auto it = m_ch.find(chan);
    // Starting 도 받아 준다: 시작 직후의 셸에 QProcess 는 쓰기를 버퍼에 담아 두었다가
    // 프로세스가 뜨면 흘려보낸다. Running 만 받으면 창을 켜자마자 폴더를 여는 흐름에서
    // 이 cd 가 통째로 사라져 터미널이 홈 폴더에 머물렀다.
    if (it == m_ch.end() || !it->proc || it->proc->state() == QProcess::NotRunning) return;
    QString targetCwd = (cwd.isEmpty() || !QFileInfo(cwd).isDir()) ? QDir::homePath() : cwd;
    QString curP = QDir::cleanPath(QDir(it->cwd).absolutePath());
    QString newP = QDir::cleanPath(QDir(targetCwd).absolutePath());
    if (!curP.isEmpty() && curP == newP) return;
    writeStdin(chan, (hostCdCommand(targetCwd) + hostLineEnd()).toUtf8());
    it->cwd = targetCwd;
}

void RunSession::ensureRepl(int chan, const QString &baseDir) {
    if (m_ch[chan].proc) return;
    spawn(chan, interpreterPath(), {"--repl", baseDir}, baseDir);
    m_ch[chan].busy = false;
}

void RunSession::replRun(int chan, const QString &code) {
    if (!m_ch[chan].proc) return;
    m_ch[chan].proc->write((code + "\n" + markEot()).toUtf8());
}

bool RunSession::isBusy() const {
    for (const Channel &c : m_ch) if (c.busy) return true;
    return false;
}

bool RunSession::isBusy(int chan) const {
    auto it = m_ch.constFind(chan);
    return it != m_ch.constEnd() && it->busy;
}

bool RunSession::alive(int chan) const {
    auto it = m_ch.constFind(chan);
    return it != m_ch.constEnd() && it->proc != nullptr;
}

bool RunSession::starting(int chan) const {
    auto it = m_ch.constFind(chan);
    return it != m_ch.constEnd() && it->initializing;
}

// 탭 하나(채널 하나)만 중단. 탭을 닫을 때도, Stop 을 눌렀을 때도 이걸 쓴다.
void RunSession::stopChan(int chan) {
    auto it = m_ch.find(chan);
    if (it == m_ch.end()) return;
    Channel &c = *it;
    if (!c.proc || c.proc->state() == QProcess::NotRunning) return;
    c.proc->disconnect(this);
    killTree(c.proc);
    c.proc->waitForFinished(500);
    c.proc->deleteLater();
    c.proc = nullptr;
    c.busy = false;
    c.pending.clear();
    c.rawPending.clear();
    emit finished(chan);
}

void RunSession::resetRepl(int chan) {
    auto it = m_ch.find(chan);
    if (it == m_ch.end() || !it->proc) return;
    it->proc->disconnect(this);
    killTree(it->proc);
    it->proc->waitForFinished(2000);
    it->proc->deleteLater();
    it->proc = nullptr;
    it->busy = false;
    it->pending.clear();
    it->rawPending.clear();
}

void RunSession::stopAll() {
    for (Channel &c : m_ch)
        if (c.proc) { killTree(c.proc); c.proc->waitForFinished(1000); }
}

void RunSession::writeStdin(int chan, const QByteArray &data) {
    if (m_ch[chan].proc) m_ch[chan].proc->write(data);
}

QString RunSession::decode(const QByteArray &b) {
    // 인코딩 폴백: UTF-8 → CP949 → 시스템
    {
        QStringDecoder dec(QStringConverter::Utf8);
        QString s = dec(b);
        if (!dec.hasError()) return s;
    }
    {
        QStringDecoder dec("windows-949");   // CP949 Korean
        if (dec.isValid()) {
            QString s = dec(b);
            if (!dec.hasError()) return s;
        }
    }
    return QString::fromLocal8Bit(b);
}

void RunSession::handleMarkers(int chan, const QString &text) {
    // 마커 처리: \x1eINPUT / \x1eDONE / \x1eEOT. 읽기 경계에서 잘린 마커는 보관.
    QString combined = m_ch[chan].pending + text;
    m_ch[chan].pending.clear();

    QString display;
    int i = 0;
    const int len = combined.size();
    bool sawInput = false;
    while (i < len) {
        int at = combined.indexOf(QChar(RS), i);
        if (at < 0) { display += QStringView(combined).mid(i); break; }
        display += QStringView(combined).mid(i, at - i);

        const int n = matchMarker(combined, at);
        if (n < 0) { m_ch[chan].pending = combined.mid(at); break; }   // 잘렸다 — 더 기다린다
        if (n == 0) { display += QChar(RS); i = at + 1; continue; }    // 마커가 아니다

        const QStringView mark = QStringView(combined).mid(at, n);
        i = at + n;
        if (mark == QStringView(markInput())) {
            sawInput = true;
        } else {                          // DONE / EOT — 한 번 실행이 끝났다
            m_ch[chan].busy = false;
            emit finished(chan);
        }
    }
    if (!display.isEmpty()) {
        const int cls = display.contains("Error:") ? OC_ERROR : OC_TEXT;
        // cmd 가 명령마다 다시 찍는 프롬프트("C:\...>")도 첫 줄과 같은 색이어야 한다.
        static const QRegularExpression tailPrompt(hostPromptTailPattern());
        auto pm = tailPrompt.match(display);
        if (pm.hasMatch()) {
            const QString body = display.left(pm.capturedStart());
            if (!body.isEmpty()) emit output(chan, cls, body);
            emit output(chan, OC_PROMPT, pm.captured(0).trimmed() + " ");
        } else {
            emit output(chan, cls, display);
        }
    }
    if (sawInput) emit awaitingInput(chan);   // 출력을 먼저 뿌리고 나서 입력 대기
}

void RunSession::onData(int chan) {
    if (!m_ch[chan].proc) return;
    Channel &c = m_ch[chan];
    QByteArray raw = c.rawPending + c.proc->readAllStandardOutput();
    c.rawPending = takeIncompleteUtf8(&raw);
    if (raw.isEmpty()) return;
    QString text = decode(raw);

    if (c.initializing) {
        c.pending += text;
        static const QRegularExpression promptRegex(hostPromptInitPattern());
        QRegularExpressionMatch match = promptRegex.match(c.pending.trimmed());
        if (match.hasMatch()) {
            QString cleanPrompt = match.captured(0).trimmed();
            cleanPrompt.remove(QRegularExpression(R"(\x1b\[[0-9;]*[a-zA-Z])"));
            c.pending.clear();
            c.initializing = false;
            emit shellInitialized(chan, cleanPrompt);
        }
        return;
    }

    handleMarkers(chan, text);
}
