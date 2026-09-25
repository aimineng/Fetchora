#include "Aria2Client.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QVariantMap>

namespace {

/// aria2 uses kebab-case for every option name. QML/JS naturally produces
/// camelCase, so normalise here so callers can use either spelling.
QString toKebabCase(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("([a-z0-9])([A-Z])"));
    QString out = name;
    out.replace(re, QStringLiteral("\\1-\\2"));
    return out.toLower();
}

QString variantToString(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Double:
    case QMetaType::Float: {
        const double d = v.toDouble();
        if (qFuzzyCompare(d, qRound64(d)))
            return QString::number(qRound64(d));
        return QString::number(d, 'g', 12);
    }
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return QString::number(v.toLongLong());
    default:
        break;
    }
    return v.toString();
}

} // namespace

Aria2Client::Aria2Client(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_connectTimer(new QTimer(this))
{
    m_connectTimer->setInterval(4000);
    connect(m_connectTimer, &QTimer::timeout, this, &Aria2Client::onConnectTimer);
    m_connectTimer->start();
}

Aria2Client::~Aria2Client() = default;

void Aria2Client::setEndpoint(const QString &host, quint16 port, const QString &secret)
{
    const bool portChanged = (m_port != port);
    m_host = host;
    m_port = port;
    m_secret = secret;
    if (portChanged) {
        m_connected = false;
        m_pending.clear();
        m_queue.clear();
    }
}

void Aria2Client::setSecret(const QString &secret)
{
    m_secret = secret;
}

QJsonObject Aria2Client::toRpcOptions(const QVariantMap &options)
{
    QJsonObject opts;
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        const QString key = toKebabCase(it.key());
        const QVariant &val = it.value();
        switch (val.typeId()) {
        case QMetaType::QVariantList:
        case QMetaType::QStringList: {
            QJsonArray arr;
            const QVariantList list = val.toList();
            for (const QVariant &item : list)
                arr.append(variantToString(item));
            opts[key] = arr;
            break;
        }
        case QMetaType::QVariantMap: {
            // Not a valid aria2 option value - serialise for debugging instead
            // of silently dropping it.
            opts[key] = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(val.toMap()))
                                              .toJson(QJsonDocument::Compact));
            break;
        }
        default:
            opts[key] = variantToString(val);
            break;
        }
    }
    return opts;
}

bool Aria2Client::methodNeedsSecret(const QString &method)
{
    // Only aria2.<something> methods take the token; system.* never do.
    return method.startsWith(QLatin1String("aria2."));
}

QJsonArray Aria2Client::withSecret(const QJsonArray &params) const
{
    if (m_secret.isEmpty())
        return params;
    QJsonArray out;
    out.append(QStringLiteral("token:") + m_secret);
    for (const QJsonValue &v : params)
        out.append(v);
    return out;
}

void Aria2Client::call(const QString &method, const QJsonArray &params,
                       std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    Pending p;
    p.method = method;
    p.cb = std::move(cb);
    send(method, methodNeedsSecret(method) ? withSecret(params) : params, p);
}

void Aria2Client::send(const QString &method, const QJsonArray &params, const Pending &pending)
{
    if (m_port == 0) {
        if (pending.cb)
            pending.cb(QJsonValue(), true, QStringLiteral("RPC port not configured"));
        return;
    }

    const QString id = QStringLiteral("%1.%2").arg(method, QString::number(++m_seq));

    QJsonObject request;
    request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    request[QStringLiteral("id")] = id;
    request[QStringLiteral("method")] = method;
    if (!params.isEmpty())
        request[QStringLiteral("params")] = params;

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(QStringLiteral("/jsonrpc"));

    QNetworkRequest netRequest(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    netRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    netRequest.setTransferTimeout(15000);

    QNetworkReply *reply = m_nam->post(netRequest, QJsonDocument(request).toJson(QJsonDocument::Compact));
    reply->setProperty("rpcMethod", method);
    reply->setProperty("rpcId", id);
    reply->setProperty("rpcStart", QDateTime::currentMSecsSinceEpoch());

    m_pending.insert(id, pending);
    ++m_requestCount;

    const QString pretty = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
    emit logMessage(QStringLiteral(">>"), pretty);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void Aria2Client::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    const QString method = reply->property("rpcMethod").toString();
    const QString id = reply->property("rpcId").toString();
    const qint64 start = reply->property("rpcStart").toLongLong();
    if (start > 0)
        m_lastLatency = int(QDateTime::currentMSecsSinceEpoch() - start);

    Pending pending = m_pending.take(id);

    if (reply->error() != QNetworkReply::NoError) {
        ++m_errorCount;
        const QString err = reply->errorString();
        emit logMessage(QStringLiteral("!!"), QStringLiteral("%1 -> %2").arg(method, err));
        if (method == QLatin1String("aria2.getVersion"))
            setConnected(false);
        if (pending.cb)
            pending.cb(QJsonValue(), true, err);
        else
            emit callFailed(method, err);
        return;
    }

    const QByteArray data = reply->readAll();
    emit logMessage(QStringLiteral("<<"), QString::fromUtf8(data));

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        ++m_errorCount;
        const QString err = QStringLiteral("Malformed RPC response: %1").arg(parseError.errorString());
        if (pending.cb)
            pending.cb(QJsonValue(), true, err);
        else
            emit callFailed(method, err);
        return;
    }

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        ++m_errorCount;
        const QJsonObject e = obj.value(QStringLiteral("error")).toObject();
        const QString msg = QStringLiteral("[%1] %2")
                                .arg(e.value(QStringLiteral("code")).toInt())
                                .arg(e.value(QStringLiteral("message")).toString());
        if (method == QLatin1String("aria2.getVersion"))
            setConnected(false);
        if (pending.cb)
            pending.cb(QJsonValue(), true, msg);
        else
            emit callFailed(method, msg);
        return;
    }

    const QJsonValue result = obj.value(QStringLiteral("result"));

    if (method == QLatin1String("aria2.getVersion")) {
        m_version = result.toObject().value(QStringLiteral("version")).toString();
        m_features = result.toObject().value(QStringLiteral("enabledFeatures")).toObject();
    }

    if (pending.cb)
        pending.cb(result, false, QString());
    else
        emit resultReady(method, result);

    flushQueue();
}

void Aria2Client::flushQueue()
{
    while (!m_queue.isEmpty()) {
        const auto item = m_queue.dequeue();
        Pending p;
        p.method = item.first;
        send(item.first, item.second, p);
    }
}

void Aria2Client::setConnected(bool value)
{
    if (m_connected == value)
        return;
    m_connected = value;
    emit connectedChanged(m_connected);
}

void Aria2Client::onConnectTimer()
{
    if (m_probing)
        return;
    m_probing = true;
    getVersion([this](const QJsonValue &result, bool isError, const QString &err) {
        m_probing = false;
        if (isError) {
            setConnected(false);
            Q_UNUSED(err)
            return;
        }
        m_version = result.toObject().value(QStringLiteral("version")).toString();
        m_features = result.toObject().value(QStringLiteral("enabledFeatures")).toObject();
        setConnected(true);
    });
}

// ------------------------------------------------------------------ system calls

#define ARIA2_SIMPLE(NAME, METHOD)                                                         \
    void Aria2Client::NAME(std::function<void(const QJsonValue &, bool, const QString &)> cb) \
    {                                                                                      \
        call(QStringLiteral(METHOD), QJsonArray(), std::move(cb));                         \
    }

ARIA2_SIMPLE(getGlobalStat, "aria2.getGlobalStat")
ARIA2_SIMPLE(getSessionInfo, "aria2.getSessionInfo")
ARIA2_SIMPLE(getGlobalOption, "aria2.getGlobalOption")
ARIA2_SIMPLE(shutdown, "aria2.shutdown")
ARIA2_SIMPLE(forceShutdown, "aria2.forceShutdown")
ARIA2_SIMPLE(saveSession, "aria2.saveSession")
ARIA2_SIMPLE(purgeDownloadResult, "aria2.purgeDownloadResult")
ARIA2_SIMPLE(forcePauseAll, "aria2.forcePauseAll")
ARIA2_SIMPLE(unpauseAll, "aria2.unpauseAll")
ARIA2_SIMPLE(pauseAll, "aria2.pauseAll")

#undef ARIA2_SIMPLE

void Aria2Client::getVersion(std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.getVersion"), QJsonArray(), std::move(cb));
}

void Aria2Client::changeGlobalOption(const QVariantMap &options,
                                     std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params;
    params.append(toRpcOptions(options));
    call(QStringLiteral("aria2.changeGlobalOption"), params, std::move(cb));
}

void Aria2Client::removeDownloadResult(const QString &gid,
                                       std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params{gid};
    call(QStringLiteral("aria2.removeDownloadResult"), params, std::move(cb));
}

void Aria2Client::changePosition(const QString &gid, int pos, int how,
                                 std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params{gid, pos, how};
    call(QStringLiteral("aria2.changePosition"), params, std::move(cb));
}

void Aria2Client::getOption(const QString &gid,
                            std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params{gid};
    call(QStringLiteral("aria2.getOption"), params, std::move(cb));
}

void Aria2Client::changeOption(const QString &gid, const QVariantMap &options,
                               std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params{gid, toRpcOptions(options)};
    call(QStringLiteral("aria2.changeOption"), params, std::move(cb));
}

// ------------------------------------------------------------------- query calls

namespace {
QJsonArray withKeys(const QJsonArray &base, const QStringList &keys)
{
    QJsonArray out = base;
    if (!keys.isEmpty()) {
        QJsonArray k;
        for (const QString &s : keys)
            k.append(s);
        out.append(k);
    }
    return out;
}
} // namespace

void Aria2Client::tellStatus(const QString &gid, const QStringList &keys,
                             std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.tellStatus"), withKeys(QJsonArray{gid}, keys), std::move(cb));
}

void Aria2Client::tellActive(const QStringList &keys,
                             std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.tellActive"), withKeys(QJsonArray{}, keys), std::move(cb));
}

void Aria2Client::tellWaiting(int offset, int num, const QStringList &keys,
                              std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.tellWaiting"), withKeys(QJsonArray{offset, num}, keys), std::move(cb));
}

void Aria2Client::tellStopped(int offset, int num, const QStringList &keys,
                              std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.tellStopped"), withKeys(QJsonArray{offset, num}, keys), std::move(cb));
}

void Aria2Client::getUris(const QString &gid,
                          std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.getUris"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::getFiles(const QString &gid,
                           std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.getFiles"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::getPeers(const QString &gid,
                           std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.getPeers"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::getServers(const QString &gid,
                             std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.getServers"), QJsonArray{gid}, std::move(cb));
}

// ---------------------------------------------------------------- download calls

void Aria2Client::addUri(const QStringList &uris, const QVariantMap &options, int position,
                         std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray uriArray;
    for (const QString &u : uris)
        uriArray.append(u);

    QJsonArray params;
    params.append(uriArray);
    if (!options.isEmpty())
        params.append(toRpcOptions(options));
    if (position >= 0) {
        if (options.isEmpty())
            params.append(QJsonObject());
        params.append(position);
    }
    call(QStringLiteral("aria2.addUri"), params, std::move(cb));
}

void Aria2Client::addTorrent(const QByteArray &torrentData, const QStringList &webSeeds,
                             const QVariantMap &options, int position,
                             std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    // aria2.addTorrent(torrent, uris?, options?, position?)
    // Positional parameters cannot be skipped, so fill the gaps explicitly.
    QJsonArray params;
    params.append(QString::fromLatin1(torrentData.toBase64()));

    if (!webSeeds.isEmpty() || !options.isEmpty() || position >= 0) {
        QJsonArray seeds;
        for (const QString &s : webSeeds)
            seeds.append(s);
        params.append(seeds);
    }
    if (!options.isEmpty() || position >= 0)
        params.append(toRpcOptions(options));
    if (position >= 0)
        params.append(position);

    call(QStringLiteral("aria2.addTorrent"), params, std::move(cb));
}

void Aria2Client::addMetalink(const QByteArray &metalinkData, const QVariantMap &options, int position,
                              std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params;
    params.append(QString::fromLatin1(metalinkData.toBase64()));
    if (!options.isEmpty() || position >= 0)
        params.append(toRpcOptions(options));
    if (position >= 0)
        params.append(position);
    call(QStringLiteral("aria2.addMetalink"), params, std::move(cb));
}

void Aria2Client::remove(const QString &gid,
                         std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.remove"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::forceRemove(const QString &gid,
                              std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.forceRemove"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::pause(const QString &gid,
                        std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.pause"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::forcePause(const QString &gid,
                             std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.forcePause"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::unpause(const QString &gid,
                          std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.unpause"), QJsonArray{gid}, std::move(cb));
}

// ------------------------------------------------------------------ BT specific

void Aria2Client::getBtMetaInfo(const QString &gid,
                                std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    call(QStringLiteral("aria2.getBtMetaInfo"), QJsonArray{gid}, std::move(cb));
}

void Aria2Client::removeBtTracker(const QByteArray &infoHash, const QString &tracker,
                                  std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray params{QString::fromLatin1(infoHash.toBase64()), tracker};
    call(QStringLiteral("aria2.removeBtTracker"), params, std::move(cb));
}

void Aria2Client::addBtTracker(const QByteArray &infoHash, const QStringList &trackers,
                               std::function<void(const QJsonValue &, bool, const QString &)> cb)
{
    QJsonArray list;
    for (const QString &t : trackers)
        list.append(t);
    QJsonArray params{QString::fromLatin1(infoHash.toBase64()), list};
    call(QStringLiteral("aria2.addBtTracker"), params, std::move(cb));
}
