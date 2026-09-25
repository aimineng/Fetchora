#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QPair>

/**
 * HttpServer - the single localhost endpoint the browser extension talks to.
 *
 * One TCP listener serves both transports, which is required because a WebSocket
 * connection starts life as an ordinary HTTP GET carrying an `Upgrade` header.
 *
 *   HTTP routes (all JSON, all CORS enabled)
 *     OPTIONS *                 preflight
 *     GET  /ping                { ok, app, version }
 *     GET  /status              { ok }
 *     POST /download            { url | urls[], options }
 *     POST /add                 alias of /download
 *     POST /torrent             { torrent: <base64> }
 *     POST /magnet              { magnet: "magnet:?xt=..." }
 *     POST /pause | /unpause    { gid } (omit gid for all tasks)
 *
 *   WebSocket  ws://127.0.0.1:<port>/ws   (RFC 6455, text frames)
 *     -> { "id": "ext-1", "cmd": "add|torrent|magnet|pause|unpause|ping|status", ... }
 *     <- { "id": "ext-1", "ok": true, ... }
 *     <- { "event": "task-completed", ... }        (unsolicited push)
 */
class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() override;

    bool start(quint16 port = 8899);
    void stop();
    bool isListening() const;
    quint16 port() const;
    QString lastError() const { return m_lastError; }

    int webSocketClientCount() const { return m_wsClients.size(); }

    /// Reply to a WebSocket request, echoing its id.
    void wsReply(const QString &id, const QJsonObject &payload);
    void wsReplyError(const QString &id, const QString &message);
    /// Push an unsolicited event to every connected extension.
    void wsBroadcast(const QJsonObject &event);

signals:
    void downloadRequested(const QString &url, const QString &origin, const QVariantMap &options);
    void downloadBatchRequested(const QStringList &urls, const QString &origin, const QVariantMap &options);
    void torrentRequested(const QByteArray &torrentData, const QString &origin);
    void magnetRequested(const QString &magnet, const QString &origin);
    void pauseRequested(const QString &gid);
    void unpauseRequested(const QString &gid);
    /// The extension asked for live statistics; answer with wsReply().
    void statusRequested(const QString &requestId);
    void clientCountChanged(int count);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    struct Connection {
        QByteArray buffer;
        bool upgraded = false;   ///< true once the socket became a WebSocket
        QByteArray frameBuffer;
    };

    // -- HTTP ---------------------------------------------------------------
    void handleHttp(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
                    const QByteArray &body, const QByteArray &origin);
    void respond(QTcpSocket *socket, int status, const QJsonObject &payload);
    void respondRaw(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body);
    static QVariantMap extractOptions(const QJsonObject &obj);

    // -- WebSocket ----------------------------------------------------------
    bool tryUpgrade(QTcpSocket *socket, Connection &conn, const QByteArray &request);
    void processFrames(QTcpSocket *socket, Connection &conn);
    void handleSocketCommand(QTcpSocket *socket, const QJsonObject &command);
    void sendFrame(QTcpSocket *socket, const QByteArray &payload, quint8 opcode);
    QTcpSocket *socketForId(const QString &id);
    void replyOrBroadcast(const QString &id, const QJsonObject &body);
    static QByteArray encodeFrame(const QByteArray &payload, quint8 opcode);
    static void decodeFrames(QByteArray &buffer, QList<QPair<quint8, QByteArray>> &out);
    static QByteArray webSocketAccept(const QByteArray &clientKey);

    QTcpServer *m_server;
    QHash<QTcpSocket *, Connection> m_connections;
    QList<QTcpSocket *> m_wsClients;
    QHash<QString, QTcpSocket *> m_requestSockets;
    QString m_lastError;
};

#endif // HTTPSERVER_H
