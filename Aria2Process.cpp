#include "Aria2Process.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QDebug>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

Aria2Process::Aria2Process(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());

    connect(m_process, &QProcess::started, this, &Aria2Process::onStarted);
    connect(m_process, &QProcess::errorOccurred, this, &Aria2Process::onErrorOccurred);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Aria2Process::onFinished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Aria2Process::drainStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &Aria2Process::drainStderr);

    m_executable = locateAria2();
}

Aria2Process::~Aria2Process()
{
    stop();
    closeJob();
}

#ifdef Q_OS_WIN
/**
 * Keep aria2c inside a job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
 *
 * QProcess only stops the child when *we* run our own shutdown code - a crash,
 * a task-manager kill or a debugger stop all skip it, and an orphaned aria2c
 * keeps downloading in the background. Windows closes the job handle when this
 * process ends and kills everything inside it, which covers every one of those
 * paths.
 */
void Aria2Process::adoptIntoJob()
{
    closeJob();

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return;
    }

    const qint64 pid = m_process->processId();
    HANDLE child = pid > 0 ? OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid))
                           : nullptr;
    if (!child || !AssignProcessToJobObject(job, child)) {
        // A job that owns nothing would only leak a handle. This is worth a line
        // in the log: it happens when the process already belongs to a job that
        // forbids nesting (CI runners do exactly that), and it means
        // --stop-with-process is now the only thing keeping the engine on a
        // leash.
        const DWORD error = GetLastError();
        if (child)
            CloseHandle(child);
        CloseHandle(job);
        qWarning("could not put aria2c in a job object (error %lu); relying on "
                 "--stop-with-process instead", error);
        return;
    }
    CloseHandle(child);
    m_job = job;
}

void Aria2Process::closeJob()
{
    if (!m_job)
        return;
    // Closing the last handle to the job is what terminates its members; the
    // explicit stop() above has already asked nicely.
    CloseHandle(static_cast<HANDLE>(m_job));
    m_job = nullptr;
}
#else
void Aria2Process::adoptIntoJob() {}
void Aria2Process::closeJob() {}
#endif

QString Aria2Process::locateAria2(const QString &hint)
{
    // The engine is an external program on every platform; only the file name
    // differs. Windows ships aria2c.exe, Unix (Linux/macOS) installs "aria2c".
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("aria2c.exe");
#else
    const QString name = QStringLiteral("aria2c");
#endif

    // Search order, first hit wins:
    //   1. the path configured in Settings -> RPC/引擎 (explicit user choice),
    //   2. next to the executable (the portable layout, and where build.ps1 /
    //      build.sh / CMake's POST_BUILD step drop the engine),
    //   3. the macOS bundle's Resources directory, then Contents/MacOS,
    //   4. the usual Unix install prefixes (Apple silicon Homebrew first),
    //   5. PATH.
    QStringList candidates;
    QSet<QString> seen;
    auto add = [&candidates, &seen](const QString &path) {
        if (path.isEmpty())
            return;
        const QString cleaned = QDir::cleanPath(path);
        if (seen.contains(cleaned))
            return;
        seen.insert(cleaned);
        candidates << cleaned;
    };

    add(hint);

    const QString appDir = QCoreApplication::applicationDirPath();
    add(appDir + QLatin1Char('/') + name);
    add(appDir + QStringLiteral("/bin/") + name);
    add(appDir + QStringLiteral("/../") + name);
    add(appDir + QStringLiteral("/../../") + name);
#ifdef Q_OS_MACOS
    // Inside Fetchora.app the binary is Contents/MacOS/Fetchora, so
    // "../Resources" is Contents/Resources - the documented place for helper
    // executables and data files in a bundle. "../MacOS" covers a build that put
    // the engine next to the executable instead.
    add(appDir + QStringLiteral("/../Resources/") + name);
    add(appDir + QStringLiteral("/../MacOS/") + name);
#else
    // Also look one level up from bin/ for a Unix tree layout (bin/ + lib/).
    add(appDir + QStringLiteral("/../lib/fetchora/") + name);
#endif

#ifndef Q_OS_WIN
    add(QStringLiteral("/opt/homebrew/bin/") + name); // Apple silicon Homebrew
    add(QStringLiteral("/usr/local/bin/") + name);    // Intel Homebrew / /usr/local
    add(QStringLiteral("/usr/bin/") + name);          // every distribution
#else
    add(QStringLiteral("C:/Program Files/aria2/") + name);
#endif

    for (const QString &c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isFile())
            return QDir::cleanPath(fi.absoluteFilePath());
    }

    // Fall back to PATH lookup. QStandardPaths::findExecutable() already knows
    // the platform's executable suffix, so the bare name is correct here.
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("aria2c"));
    if (!inPath.isEmpty())
        return inPath;

    // Nothing found: return the "next to the executable" path so the caller can
    // report a path the user can act on.
    return QDir::cleanPath(appDir + QLatin1Char('/') + name);
}

void Aria2Process::setExecutable(const QString &path)
{
    m_executable = path;
}

void Aria2Process::setArguments(const QStringList &args)
{
    m_arguments = args;
}

bool Aria2Process::isRunning() const
{
    return m_process->state() == QProcess::Running;
}

qint64 Aria2Process::processId() const
{
    return m_process->processId();
}

void Aria2Process::start()
{
    if (m_process->state() != QProcess::NotRunning) {
        emit logLine(QStringLiteral("Engine is already running."), false);
        return;
    }
    if (m_executable.isEmpty() || !QFileInfo::exists(m_executable)) {
        emit failed(tr("未找到 aria2c，请将其放在程序目录下或加入 PATH。"));
        return;
    }

    // Belt and braces for "the engine must not outlive the app":
    //
    //   * --stop-with-process makes aria2c watch *our* pid and exit by itself
    //     when we are gone. It is the only mechanism that also covers the paths
    //     where nothing of ours runs any more - a crash, a task-manager kill, a
    //     debugger stop - and it works on every platform. aria2 shuts down
    //     cleanly through it, so the session file is written on the way out.
    //   * the job object below covers the same ground on Windows and does not
    //     depend on the engine cooperating.
    //
    // Neither is enough on its own: AssignProcessToJobObject fails when the
    // process already belongs to a job that forbids nesting (which is how CI
    // runners and some launchers run programs), and a user-supplied aria2c may
    // be an old build without the option.
    QStringList args = m_arguments;
    args << QStringLiteral("--stop-with-process=%1").arg(QCoreApplication::applicationPid());

    m_intentionalStop = false;
    emit logLine(QStringLiteral("Launching: %1 %2").arg(m_executable, args.join(QLatin1Char(' '))), false);
    m_process->start(m_executable, args);
}

void Aria2Process::stop()
{
    if (m_process->state() == QProcess::NotRunning) {
        closeJob();
        return;
    }
    m_intentionalStop = true;
#ifdef Q_OS_WIN
    // aria2c is a console program with no window: QProcess::terminate() posts a
    // WM_CLOSE that nobody will ever receive, so waiting for it only cost four
    // seconds on every settings change and every exit. The session was already
    // saved over RPC by the caller.
    m_process->kill();
    m_process->waitForFinished(2000);
#else
    m_process->terminate();
    if (!m_process->waitForFinished(4000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
#endif
    closeJob();
}

void Aria2Process::restart()
{
    stop();
    start();
}

void Aria2Process::onStarted()
{
    adoptIntoJob();
    emit logLine(QStringLiteral("aria2c started (pid %1)").arg(m_process->processId()), false);
    emit started();
}

void Aria2Process::onErrorOccurred(QProcess::ProcessError error)
{
    QString reason;
    switch (error) {
    case QProcess::FailedToStart:
        reason = tr("启动 aria2c 失败（%1）。").arg(m_executable);
        break;
    case QProcess::Crashed:
        reason = tr("aria2c 已崩溃。");
        break;
    case QProcess::Timedout:
        reason = tr("aria2c 未及时响应。");
        break;
    case QProcess::WriteError:
        reason = tr("无法向 aria2c 写入数据。");
        break;
    case QProcess::ReadError:
        reason = tr("无法从 aria2c 读取数据。");
        break;
    default:
        reason = tr("未知的 aria2c 进程错误。");
        break;
    }
    if (!m_intentionalStop)
        emit failed(reason);
    else
        emit logLine(reason, true);
}

void Aria2Process::onFinished(int exitCode, QProcess::ExitStatus status)
{
    drainStdout();
    drainStderr();
    if (status == QProcess::CrashExit && !m_intentionalStop)
        emit logLine(QStringLiteral("aria2c terminated unexpectedly (exit %1).").arg(exitCode), true);
    else
        emit logLine(QStringLiteral("aria2c stopped (exit %1).").arg(exitCode), false);
    emit stopped(exitCode);
    m_intentionalStop = false;
}

void Aria2Process::drainStdout()
{
    emitLines(QProcess::StandardOutput, false);
}

void Aria2Process::drainStderr()
{
    emitLines(QProcess::StandardError, true);
}

void Aria2Process::emitLines(QProcess::ProcessChannel channel, bool isError)
{
    QByteArray &buffer = isError ? m_stderrBuffer : m_stdoutBuffer;
    buffer.append(isError ? m_process->readAllStandardError() : m_process->readAllStandardOutput());

    int idx;
    while ((idx = buffer.indexOf('\n')) >= 0) {
        QByteArray line = buffer.left(idx);
        buffer.remove(0, idx + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!line.isEmpty())
            emit logLine(QString::fromUtf8(line), isError);
    }
    // Keep a runaway partial line from growing without bound.
    if (buffer.size() > 64 * 1024)
        buffer.clear();
    Q_UNUSED(channel)
}
