#ifndef ARIA2CLIENT_H
#define ARIA2CLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Aria2Client - a complete, typed wrapper around the aria2 JSON-RPC interface.
 *
 * Every RPC method exposed by aria2 1.x is available here.  The class owns the
 * request/response plumbing (id generation, secret token injection, error
 * mapping, queueing while aria2 is not yet reachable) and re-emits the answers
 * through a small set of signals plus a generic callback mechanism.
 */
class Aria2Client : public QObject
{
    Q_OBJECT

public:
    explicit Aria2Client(QObject *parent = nullptr);
    ~Aria2Client() override;

    void setEndpoint(const QString &host, quint16 port, const QString &secret);
    void setSecret(const QString &secret);
    QString secret() const { return m_secret; }
    quint16 port() const { return m_port; }

    /// Number of RPC calls sent since startup (used by the status bar).
    quint64 requestCount() const { return m_requestCount; }
    /// Number of failed RPC calls since startup.
    quint64 errorCount() const { return m_errorCount; }
    /// Round-trip time of the last completed call, in milliseconds.
    int lastLatency() const { return m_lastLatency; }

    /// True once `aria2.getVersion` has answered successfully.
    bool isConnected() const { return m_connected; }
    QString aria2Version() const { return m_version; }
    QJsonObject enabledFeatures() const { return m_features; }

    // ---------------------------------------------------------------- generic
    /**
     * Invoke an arbitrary aria2 RPC method.
     * @param method  e.g. "aria2.tellStatus"
     * @param params  JSON array of parameters (the secret token is prepended
     *                automatically for methods that require it).
     * @param cb      optional callback receiving (result, isError, errorText).
     *                When omitted the generic resultReady() signal is emitted.
     */
    void call(const QString &method,
              const QJsonArray &params = QJsonArray(),
              std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);

    /// Convenience overload for whole-object options.
    static QJsonObject toRpcOptions(const QVariantMap &options);

    // ------------------------------------------------------------ system calls
    void getVersion(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getGlobalStat(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getSessionInfo(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getGlobalOption(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void changeGlobalOption(const QVariantMap &options,
                            std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void shutdown(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void forceShutdown(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void saveSession(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void purgeDownloadResult(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void removeDownloadResult(const QString &gid,
                              std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void changePosition(const QString &gid, int pos, int how,
                        std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getOption(const QString &gid,
                   std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void changeOption(const QString &gid, const QVariantMap &options,
                      std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);

    // -------------------------------------------------------------- query calls
    void tellStatus(const QString &gid, const QStringList &keys = QStringList(),
                    std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void tellActive(const QStringList &keys = QStringList(),
                    std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void tellWaiting(int offset, int num, const QStringList &keys = QStringList(),
                     std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void tellStopped(int offset, int num, const QStringList &keys = QStringList(),
                     std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getUris(const QString &gid,
                 std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getFiles(const QString &gid,
                  std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getPeers(const QString &gid,
                  std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void getServers(const QString &gid,
                    std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);

    // ----------------------------------------------------------- download calls
    void addUri(const QStringList &uris, const QVariantMap &options = QVariantMap(), int position = -1,
                std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void addTorrent(const QByteArray &torrentData, const QStringList &webSeeds = QStringList(),
                    const QVariantMap &options = QVariantMap(), int position = -1,
                    std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void addMetalink(const QByteArray &metalinkData, const QVariantMap &options = QVariantMap(), int position = -1,
                     std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void remove(const QString &gid,
                std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void forceRemove(const QString &gid,
                     std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void pause(const QString &gid,
               std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void pauseAll(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void forcePause(const QString &gid,
                    std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void forcePauseAll(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void unpause(const QString &gid,
                 std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void unpauseAll(std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);

    // --------------------------------------------------------------- BT specific
    void getBtMetaInfo(const QString &gid,
                       std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void removeBtTracker(const QByteArray &infoHash, const QString &tracker,
                         std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);
    void addBtTracker(const QByteArray &infoHash, const QStringList &trackers,
                      std::function<void(const QJsonValue &, bool, const QString &)> cb = nullptr);

signals:
    /// Emitted for any call() made without a callback.
    void resultReady(const QString &method, const QJsonValue &result);
    /// Emitted for any call() that failed and had no callback.
    void callFailed(const QString &method, const QString &errorText);
    /// Connection state towards the aria2 RPC endpoint changed.
    void connectedChanged(bool connected);
    /// Human readable log line for the in-app RPC console.
    void logMessage(const QString &direction, const QString &text);

private slots:
    void onReplyFinished(QNetworkReply *reply);
    void onConnectTimer();

private:
    struct Pending {
        QString method;
        std::function<void(const QJsonValue &, bool, const QString &)> cb;
        bool silent = false;
    };

    void send(const QString &method, const QJsonArray &params, const Pending &pending);
    void flushQueue();
    QJsonArray withSecret(const QJsonArray &params) const;
    static bool methodNeedsSecret(const QString &method);
    void setConnected(bool value);

    QNetworkAccessManager *m_nam;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 6800;
    QString m_secret;

    QMap<QString, Pending> m_pending;
    QQueue<QPair<QString, QJsonArray>> m_queue;

    QTimer *m_connectTimer;
    bool m_connected = false;
    bool m_probing = false;
    QString m_version;
    QJsonObject m_features;

    quint64 m_requestCount = 0;
    quint64 m_errorCount = 0;
    int m_lastLatency = 0;
    qint64 m_seq = 0;
};

#endif // ARIA2CLIENT_H
