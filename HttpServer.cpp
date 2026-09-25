#include "HttpServer.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace {

const char *kAppName = "Aria2 Downloader";
const char *kAppVersion = "0.1.4";

/// RFC 6455 handshake GUID.
const char *kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QByteArray statusText(int code)
{
    switch (code) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

QByteArray headerValue(const QByteArray &request, const QByteArray &name)
{
    const QList<QByteArray> lines = request.split('\n');
    const QByteArray lowerName = name.toLower() + ':';
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.toLower().startsWith(lowerName))
            return line.mid(lowerName.size()).trimmed();
    }
    return {};
}

/// Every aria2 option a browser extension may legitimately want to pass.
const QStringList &optionKeys()
{
    static const QStringList keys = {
        QStringLiteral("out"),            QStringLiteral("dir"),
        QStringLiteral("referer"),        QStringLiteral("user-agent"),
        QStringLiteral("header"),         QStringLiteral("cookie"),
        QStringLiteral("proxy"),          QStringLiteral("all-proxy"),
        QStringLiteral("split"),          QStringLiteral("max-connection-per-server"),
        QStringLiteral("min-split-size"), QStringLiteral("max-download-limit"),
        QStringLiteral("check-certificate"), QStringLiteral("pause"),
        QStringLiteral("checksum"),       QStringLiteral("lowest-speed-limit"),
        QStringLiteral("http-user"),      QStringLiteral("http-passwd"),
        QStringLiteral("ftp-user"),       QStringLiteral("ftp-passwd"),
        QStringLiteral("load-cookies"),   QStringLiteral("save-cookies"),
        QStringLiteral("conditional-get"), QStringLiteral("remote-time"),
        QStringLiteral("use-head"),       QStringLiteral("follow-torrent"),
        QStringLiteral("bt-metadata-only"), QStringLiteral("seed-ratio"),
        QStringLiteral("seed-time"),      QStringLiteral("select-file"),
    };
    return keys;
}

} // namespace

HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start(quint16 port)
{
    if (m_server->isListening())
        m_server->close();
    if (m_server->listen(QHostAddress::LocalHost, port)) {
        qInfo() << "Browser bridge listening on http://127.0.0.1:" << port << "(HTTP + WebSocket)";
        m_lastError.clear();
        return true;
    }
    m_lastError = m_server->errorString();
    qWarning() << "Browser bridge could not bind port" << port << ":" << m_lastError;
    return false;
}

void HttpServer::stop()
{
    for (QTcpSocket *socket : m_connections.keys())
        socket->disconnectFromHost();
    m_connections.clear();
    m_wsClients.clear();
    m_requestSockets.clear();
    if (m_server->isListening())
        m_server->close();
}

bool HttpServer::isListening() const { return m_server->isListening(); }
quint16 HttpServer::port() const { return m_server->serverPort(); }

// ============================================================================
//  Connection handling
// ============================================================================

void HttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        m_connections.insert(socket, Connection{});
        connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &HttpServer::onDisconnected);
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
            m_wsClients.removeAll(socket);
            m_connections.remove(socket);
            for (auto it = m_requestSockets.begin(); it != m_requestSockets.end();) {
                if (it.value() == socket)
                    it = m_requestSockets.erase(it);
                else
                    ++it;
            }
            socket->deleteLater();
            emit clientCountChanged(m_wsClients.size());
        });
    }
}

void HttpServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    m_connections.remove(socket);
    m_wsClients.removeAll(socket);
    for (auto it = m_requestSockets.begin(); it != m_requestSockets.end();) {
        if (it.value() == socket)
            it = m_requestSockets.erase(it);
        else
            ++it;
    }
    socket->deleteLater();
    emit clientCountChanged(m_wsClients.size());
}

void HttpServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_connections.contains(socket))
        return;

    Connection &conn = m_connections[socket];

    // ---- already a WebSocket: everything after this point is frames -------
    if (conn.upgraded) {
        conn.frameBuffer.append(socket->readAll());
        processFrames(socket, conn);
        return;
    }

    conn.buffer.append(socket->readAll());
    const int headerEnd = conn.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (conn.buffer.size() > 256 * 1024)
            respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Header too large")}});
        return;
    }

    const QByteArray headerBlock = conn.buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty())
        return;

    // A WebSocket handshake is an ordinary HTTP GET with an Upgrade header, so
    // it must be detected here; a second listener on the same port would race
    // and answer the GET with 405.
    if (headerValue(headerBlock, "Upgrade").toLower() == QLatin1String("websocket")) {
        if (tryUpgrade(socket, conn, conn.buffer))
            return;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().simplified().split(' ');
    if (requestLine.size() < 2) {
        respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Malformed request line")}});
        return;
    }
    const QByteArray method = requestLine.at(0).toUpper();
    QByteArray target = requestLine.at(1);
    const int queryPos = target.indexOf('?');
    if (queryPos >= 0)
        target = target.left(queryPos);

    int contentLength = 0;
    QByteArray origin = "browser";
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length")
            contentLength = value.toInt();
        else if (name == "origin" && !value.isEmpty())
            origin = value;
        else if (name == "user-agent" && origin == "browser")
            origin = value;
    }

    const QByteArray body = conn.buffer.mid(headerEnd + 4);
    if (body.size() < contentLength) {
        if (contentLength > 64 * 1024 * 1024)
            respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Body too large")}});
        return; // wait for the rest of the body
    }

    m_connections.remove(socket);
    handleHttp(socket, method, target, body.left(contentLength), origin);
}

// ============================================================================
//  HTTP routes
// ============================================================================

void HttpServer::handleHttp(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
                            const QByteArray &body, const QByteArray &origin)
{
    const QString originStr = QString::fromUtf8(origin);

    if (method == "OPTIONS") {
        respondRaw(socket, 204, "text/plain", QByteArray());
        return;
    }

    if (method == "GET" && (path == "/ping" || path == "/")) {
        respond(socket, 200, QJsonObject{
                                {QStringLiteral("ok"), true},
                                {QStringLiteral("app"), QString::fromLatin1(kAppName)},
                                {QStringLiteral("version"), QString::fromLatin1(kAppVersion)},
                                {QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                            });
        return;
    }

    if (method == "GET" && path == "/status") {
        respond(socket, 200, QJsonObject{{QStringLiteral("ok"), true}});
        emit statusRequested(QString());
        return;
    }

    if (method != "POST") {
        respond(socket, 405, {{QStringLiteral("error"), QStringLiteral("Use POST for this endpoint")}});
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Fall back to form-encoded bodies so a plain <form> post also works.
        const QUrlQuery form(QString::fromUtf8(body));
        if (form.hasQueryItem(QStringLiteral("url"))) {
            emit downloadRequested(form.queryItemValue(QStringLiteral("url")), originStr, QVariantMap());
            respond(socket, 200, {{QStringLiteral("ok"), true}});
            return;
        }
        respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Invalid JSON body")}});
        return;
    }

    const QJsonObject obj = doc.object();
    const QVariantMap options = extractOptions(obj);

    if (path == "/download" || path == "/add") {
        QStringList urls;
        if (obj.value(QStringLiteral("urls")).isArray()) {
            for (const QJsonValue &v : obj.value(QStringLiteral("urls")).toArray()) {
                const QString u = v.toString().trimmed();
                if (!u.isEmpty())
                    urls << u;
            }
        }
        const QString single = obj.value(QStringLiteral("url")).toString().trimmed();
        if (!single.isEmpty())
            urls.prepend(single);

        if (urls.isEmpty()) {
            respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Missing 'url' field")}});
            return;
        }
        if (urls.size() == 1)
            emit downloadRequested(urls.first(), originStr, options);
        else
            emit downloadBatchRequested(urls, originStr, options);

        respond(socket, 200, QJsonObject{
                                {QStringLiteral("ok"), true},
                                {QStringLiteral("count"), urls.size()},
                            });
        return;
    }

    if (path == "/torrent") {
        const QString b64 = obj.value(QStringLiteral("torrent")).toString();
        if (b64.isEmpty()) {
            respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Missing 'torrent' (base64)")}});
            return;
        }
        emit torrentRequested(QByteArray::fromBase64(b64.toLatin1()), originStr);
        respond(socket, 200, {{QStringLiteral("ok"), true}});
        return;
    }

    if (path == "/magnet") {
        const QString magnet = obj.value(QStringLiteral("magnet")).toString().trimmed();
        if (!magnet.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
            respond(socket, 400, {{QStringLiteral("error"), QStringLiteral("Not a magnet link")}});
            return;
        }
        emit magnetRequested(magnet, originStr);
        respond(socket, 200, {{QStringLiteral("ok"), true}});
        return;
    }

    if (path == "/pause" || path == "/unpause") {
        const QString gid = obj.value(QStringLiteral("gid")).toString();
        if (path == "/pause")
            emit pauseRequested(gid);
        else
            emit unpauseRequested(gid);
        respond(socket, 200, {{QStringLiteral("ok"), true}});
        return;
    }

    respond(socket, 404, {{QStringLiteral("error"), QStringLiteral("Unknown endpoint")}});
}

QVariantMap HttpServer::extractOptions(const QJsonObject &obj)
{
    QVariantMap options;
    QVariantList headers;

    // Options may arrive either at the top level (`{url, dir, out}`) or nested
    // under an `options` object (`{url, options:{dir, out}}`). The extension
    // uses the nested form, so both have to be understood.
    const QJsonObject nested = obj.value(QStringLiteral("options")).toObject();

    for (const QString &key : optionKeys()) {
        QJsonValue v;
        if (nested.contains(key))
            v = nested.value(key);
        else if (obj.contains(key))
            v = obj.value(key);
        else
            continue;

        if (key == QStringLiteral("header")) {
            if (v.isArray()) {
                for (const QJsonValue &h : v.toArray())
                    headers << h.toString();
            } else if (!v.toString().isEmpty()) {
                headers << v.toString();
            }
            continue;
        }
        if (key == QStringLiteral("cookie") && !v.toString().isEmpty()) {
            headers << QStringLiteral("Cookie: ") + v.toString();
            continue;
        }
        // An empty "out" means "let aria2 decide", so do not forward it.
        if (key == QStringLiteral("out") && v.toString().isEmpty())
            continue;
        options.insert(key, v.toVariant());
    }
    if (!headers.isEmpty())
        options.insert(QStringLiteral("header"), headers);

    // "filename" is the convenience alias the extension uses.
    if (!options.contains(QStringLiteral("out"))) {
        const QString fname = nested.contains(QStringLiteral("filename"))
                                  ? nested.value(QStringLiteral("filename")).toString()
                                  : obj.value(QStringLiteral("filename")).toString();
        if (!fname.isEmpty())
            options.insert(QStringLiteral("out"), fname.split(QLatin1Char('/')).last());
    }
    return options;
}

void HttpServer::respond(QTcpSocket *socket, int status, const QJsonObject &payload)
{
    respondRaw(socket, status, "application/json", QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void HttpServer::respondRaw(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body)
{
    if (!socket)
        return;
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
    if (socket->state() == QAbstractSocket::UnconnectedState)
        socket->deleteLater();
    else
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
}

// ============================================================================
//  WebSocket (RFC 6455), text frames only
// ============================================================================

QByteArray HttpServer::webSocketAccept(const QByteArray &clientKey)
{
    return QCryptographicHash::hash(clientKey.trimmed() + kWebSocketGuid, QCryptographicHash::Sha1).toBase64();
}

bool HttpServer::tryUpgrade(QTcpSocket *socket, Connection &conn, const QByteArray &request)
{
    const QByteArray key = headerValue(request, "Sec-WebSocket-Key");
    if (key.isEmpty()) {
        respondRaw(socket, 400, "text/plain", "Missing Sec-WebSocket-Key");
        return false;
    }

    QByteArray response;
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + webSocketAccept(key) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n\r\n";
    socket->write(response);
    socket->flush();

    const int headerEnd = request.indexOf("\r\n\r\n");
    QByteArray leftover;
    if (headerEnd >= 0 && headerEnd + 4 < request.size())
        leftover = request.mid(headerEnd + 4);

    conn.upgraded = true;
    conn.buffer.clear();
    conn.frameBuffer = leftover;
    if (!m_wsClients.contains(socket))
        m_wsClients.append(socket);
    emit clientCountChanged(m_wsClients.size());

    if (!leftover.isEmpty())
        processFrames(socket, conn);
    return true;
}

QByteArray HttpServer::encodeFrame(const QByteArray &payload, quint8 opcode)
{
    QByteArray frame;
    frame.append(char(0x80 | opcode)); // FIN + opcode

    const int size = payload.size();
    if (size < 126) {
        frame.append(char(size));
    } else if (size <= 0xFFFF) {
        frame.append(char(126));
        frame.append(char((size >> 8) & 0xFF));
        frame.append(char(size & 0xFF));
    } else {
        frame.append(char(127));
        for (int i = 7; i >= 0; --i)
            frame.append(char((quint64(size) >> (8 * i)) & 0xFF));
    }
    frame.append(payload);
    return frame;
}

void HttpServer::decodeFrames(QByteArray &buffer, QList<QPair<quint8, QByteArray>> &out)
{
    while (buffer.size() >= 2) {
        const quint8 b0 = quint8(buffer.at(0));
        const quint8 b1 = quint8(buffer.at(1));
        const quint8 opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;
        quint64 length = b1 & 0x7F;

        int offset = 2;
        if (length == 126) {
            if (buffer.size() < offset + 2)
                return;
            length = (quint64(quint8(buffer.at(2))) << 8) | quint8(buffer.at(3));
            offset += 2;
        } else if (length == 127) {
            if (buffer.size() < offset + 8)
                return;
            length = 0;
            for (int i = 0; i < 8; ++i)
                length = (length << 8) | quint8(buffer.at(offset + i));
            offset += 8;
        }

        QByteArray mask;
        if (masked) {
            if (buffer.size() < offset + 4)
                return;
            mask = buffer.mid(offset, 4);
            offset += 4;
        }

        if (quint64(buffer.size()) < quint64(offset) + length)
            return; // wait for the rest

        QByteArray payload = buffer.mid(offset, int(length));
        buffer.remove(0, offset + int(length));

        if (masked && mask.size() == 4) {
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = char(quint8(payload.at(i)) ^ quint8(mask.at(i % 4)));
        }
        out.append(qMakePair(opcode, payload));
    }
}

void HttpServer::processFrames(QTcpSocket *socket, Connection &conn)
{
    QList<QPair<quint8, QByteArray>> frames;
    decodeFrames(conn.frameBuffer, frames);

    for (const auto &frame : frames) {
        const quint8 opcode = frame.first;
        const QByteArray payload = frame.second;

        if (opcode == 0x8) { // close
            sendFrame(socket, QByteArray(), 0x8);
            socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x9) { // ping -> pong
            sendFrame(socket, payload, 0xA);
            continue;
        }
        if (opcode != 0x1 && opcode != 0x2)
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (doc.isObject())
            handleSocketCommand(socket, doc.object());
    }
}

void HttpServer::handleSocketCommand(QTcpSocket *socket, const QJsonObject &command)
{
    const QString id = command.value(QStringLiteral("id")).toString();
    const QString cmd = command.value(QStringLiteral("cmd")).toString().toLower();
    const QVariantMap options = command.value(QStringLiteral("options")).toObject().toVariantMap();

    if (!id.isEmpty() && id != QLatin1String("null"))
        m_requestSockets.insert(id, socket);

    if (cmd == QLatin1String("ping")) {
        wsReply(id, QJsonObject{
                        {QStringLiteral("app"), QString::fromLatin1(kAppName)},
                        {QStringLiteral("version"), QString::fromLatin1(kAppVersion)},
                    });
        return;
    }

    if (cmd == QLatin1String("status")) {
        emit statusRequested(id);
        return;
    }

    if (cmd == QLatin1String("add")) {
        QStringList urls;
        if (command.value(QStringLiteral("urls")).isArray()) {
            for (const QJsonValue &v : command.value(QStringLiteral("urls")).toArray()) {
                const QString u = v.toString().trimmed();
                if (!u.isEmpty())
                    urls << u;
            }
        }
        const QString single = command.value(QStringLiteral("url")).toString().trimmed();
        if (!single.isEmpty())
            urls.prepend(single);

        if (urls.isEmpty()) {
            wsReplyError(id, QStringLiteral("No url given"));
            return;
        }
        if (urls.size() == 1)
            emit downloadRequested(urls.first(), QStringLiteral("extension"), options);
        else
            emit downloadBatchRequested(urls, QStringLiteral("extension"), options);
        wsReply(id, QJsonObject{{QStringLiteral("count"), urls.size()}});
        return;
    }

    if (cmd == QLatin1String("torrent")) {
        const QByteArray data = QByteArray::fromBase64(
            command.value(QStringLiteral("torrent")).toString().toLatin1());
        if (data.isEmpty()) {
            wsReplyError(id, QStringLiteral("Empty torrent payload"));
            return;
        }
        emit torrentRequested(data, QStringLiteral("extension"));
        wsReply(id, QJsonObject{});
        return;
    }

    if (cmd == QLatin1String("magnet")) {
        const QString magnet = command.value(QStringLiteral("magnet")).toString().trimmed();
        if (!magnet.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
            wsReplyError(id, QStringLiteral("Not a magnet link"));
            return;
        }
        emit magnetRequested(magnet, QStringLiteral("extension"));
        wsReply(id, QJsonObject{});
        return;
    }

    if (cmd == QLatin1String("pause") || cmd == QLatin1String("unpause")) {
        const QString gid = command.value(QStringLiteral("gid")).toString();
        if (cmd == QLatin1String("pause"))
            emit pauseRequested(gid);
        else
            emit unpauseRequested(gid);
        wsReply(id, QJsonObject{});
        return;
    }

    wsReplyError(id, QStringLiteral("Unknown command: %1").arg(cmd));
}

QTcpSocket *HttpServer::socketForId(const QString &id)
{
    if (!id.isEmpty()) {
        QTcpSocket *socket = m_requestSockets.take(id);
        if (socket && m_wsClients.contains(socket))
            return socket;
    }
    if (m_wsClients.size() == 1)
        return m_wsClients.first();
    return nullptr;
}

void HttpServer::replyOrBroadcast(const QString &id, const QJsonObject &body)
{
    const QByteArray encoded = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (QTcpSocket *target = socketForId(id)) {
        sendFrame(target, encoded, 0x1);
        return;
    }
    for (QTcpSocket *socket : m_wsClients)
        sendFrame(socket, encoded, 0x1);
}

void HttpServer::wsReply(const QString &id, const QJsonObject &payload)
{
    QJsonObject body = payload;
    if (!id.isEmpty())
        body.insert(QStringLiteral("id"), id);
    body.insert(QStringLiteral("ok"), true);
    replyOrBroadcast(id, body);
}

void HttpServer::wsReplyError(const QString &id, const QString &message)
{
    QJsonObject body{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message},
    };
    if (!id.isEmpty())
        body.insert(QStringLiteral("id"), id);
    replyOrBroadcast(id, body);
}

void HttpServer::sendFrame(QTcpSocket *socket, const QByteArray &payload, quint8 opcode)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    socket->write(encodeFrame(payload, opcode));
    socket->flush();
}

void HttpServer::wsBroadcast(const QJsonObject &event)
{
    const QByteArray encoded = QJsonDocument(event).toJson(QJsonDocument::Compact);
    for (QTcpSocket *socket : m_wsClients)
        sendFrame(socket, encoded, 0x1);
}
