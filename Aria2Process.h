#ifndef ARIA2PROCESS_H
#define ARIA2PROCESS_H

#include <QObject>
#include <QProcess>
#include <QStringList>

/**
 * Aria2Process - owns the lifetime of the bundled `aria2c` child process.
 *
 * It is deliberately dumb: it receives a fully-built argument list from the
 * settings layer, starts/stops the daemon, and forwards stdout/stderr so the
 * UI can show a live engine log.
 */
class Aria2Process : public QObject
{
    Q_OBJECT

public:
    explicit Aria2Process(QObject *parent = nullptr);
    ~Aria2Process() override;

    /// Absolute path of the aria2c executable that will be launched.
    void setExecutable(const QString &path);
    QString executable() const { return m_executable; }

    /// Full argument list (without argv[0]) used for the next start.
    void setArguments(const QStringList &args);
    QStringList arguments() const { return m_arguments; }

    /// Locate aria2c: next to the app first, then PATH.
    static QString locateAria2(const QString &hint = QString());

    bool isRunning() const;
    qint64 processId() const;

    /// Stop (graceful, then kill) and, if `restart`, start again.
    void restart();
    void stop();

public slots:
    void start();

signals:
    void started();
    void stopped(int exitCode);
    void failed(const QString &reason);
    void logLine(const QString &line, bool isError);

private slots:
    void onStarted();
    void onErrorOccurred(QProcess::ProcessError error);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void drainStdout();
    void drainStderr();

private:
    void emitLines(QProcess::ProcessChannel channel, bool isError);

    QProcess *m_process = nullptr;
    QString m_executable;
    QStringList m_arguments;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    bool m_intentionalStop = false;
};

#endif // ARIA2PROCESS_H
