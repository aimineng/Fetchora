#include "TorrentUtils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

// ============================================================================
//  Bencode codec
// ============================================================================
namespace {

/// Bencode a byte string: "<length>:<bytes>".
QByteArray encodeString(const QByteArray &bytes)
{
    return QByteArray::number(bytes.size()) + ':' + bytes;
}

void encodeValue(const QVariant &value, QByteArray &out);

class Decoder
{
public:
    explicit Decoder(const QByteArray &data) : m_data(data) {}

    QVariant run(int *consumed)
    {
        m_pos = 0;
        m_ok = true;
        QVariant v = parse();
        if (consumed)
            *consumed = m_pos;
        return m_ok ? v : QVariant();
    }

    bool ok() const { return m_ok; }

private:
    QVariant parse()
    {
        if (m_pos >= m_data.size()) {
            m_ok = false;
            return {};
        }
        const char c = m_data.at(m_pos);
        if (c == 'i')
            return parseInt();
        if (c == 'l')
            return parseList();
        if (c == 'd')
            return parseDict();
        if (c >= '0' && c <= '9')
            return parseString();
        m_ok = false;
        return {};
    }

    QVariant parseInt()
    {
        ++m_pos; // 'i'
        const int end = m_data.indexOf('e', m_pos);
        if (end < 0) {
            m_ok = false;
            return {};
        }
        const QByteArray num = m_data.mid(m_pos, end - m_pos);
        m_pos = end + 1;
        bool ok = false;
        const qint64 value = num.toLongLong(&ok);
        if (!ok) {
            m_ok = false;
            return {};
        }
        return value;
    }

    QVariant parseString()
    {
        const int colon = m_data.indexOf(':', m_pos);
        if (colon < 0) {
            m_ok = false;
            return {};
        }
        bool ok = false;
        const int len = m_data.mid(m_pos, colon - m_pos).toInt(&ok);
        if (!ok || len < 0 || colon + 1 + len > m_data.size()) {
            m_ok = false;
            return {};
        }
        const QByteArray str = m_data.mid(colon + 1, len);
        m_pos = colon + 1 + len;
        return str;
    }

    QVariant parseList()
    {
        ++m_pos; // 'l'
        QVariantList list;
        while (m_pos < m_data.size() && m_data.at(m_pos) != 'e') {
            const QVariant v = parse();
            if (!m_ok)
                return {};
            list.append(v);
        }
        if (m_pos >= m_data.size()) {
            m_ok = false;
            return {};
        }
        ++m_pos; // 'e'
        return list;
    }

    QVariant parseDict()
    {
        ++m_pos; // 'd'
        QVariantMap map;
        while (m_pos < m_data.size() && m_data.at(m_pos) != 'e') {
            const QVariant key = parse();
            if (!m_ok || key.typeId() != QMetaType::QByteArray) {
                m_ok = false;
                return {};
            }
            const QVariant value = parse();
            if (!m_ok)
                return {};
            map.insert(QString::fromUtf8(key.toByteArray()), value);
        }
        if (m_pos >= m_data.size()) {
            m_ok = false;
            return {};
        }
        ++m_pos; // 'e'
        return map;
    }

    const QByteArray &m_data;
    int m_pos = 0;
    bool m_ok = true;
};

void encodeValue(const QVariant &value, QByteArray &out)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        out += 'i' + QByteArray::number(value.toLongLong()) + 'e';
        break;
    case QMetaType::QByteArray:
        out += encodeString(value.toByteArray());
        break;
    case QMetaType::QString:
        out += encodeString(value.toString().toUtf8());
        break;
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        out += 'l';
        const QVariantList list = value.toList();
        for (const QVariant &v : list)
            encodeValue(v, out);
        out += 'e';
        break;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        // Sort by the UTF-8 byte representation (bencode requires byte order
        // for dictionary keys) but index the map by the original QString - a
        // UTF-8 round trip is lossy for non-ASCII keys and would drop values.
        QList<QPair<QByteArray, QString>> entries;
        entries.reserve(map.size());
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            entries.append(qMakePair(it.key().toUtf8(), it.key()));
        std::sort(entries.begin(), entries.end(),
                  [](const QPair<QByteArray, QString> &a, const QPair<QByteArray, QString> &b) {
                      return a.first < b.first;
                  });

        out += 'd';
        for (const auto &entry : entries) {
            out += QByteArray::number(entry.first.size()) + ':' + entry.first;
            encodeValue(map.value(entry.second), out);
        }
        out += 'e';
        break;
    }
    default:
        // Anything else (a QVariant holding a char* or QChar*, for instance)
        // is encoded as a bencoded string. Encoding it as a bare length prefix
        // would corrupt the stream - that bug produced .torrent files with an
        // unparseable announce-list.
        out += encodeString(value.toString().toUtf8());
        break;
    }
}

} // namespace

QByteArray Bencode::encode(const QVariant &value)
{
    QByteArray out;
    encodeValue(value, out);
    return out;
}

QVariant Bencode::decode(const QByteArray &data, int *consumed)
{
    Decoder decoder(data);
    return decoder.run(consumed);
}

bool Bencode::looksValid(const QByteArray &data)
{
    if (data.isEmpty())
        return false;
    int consumed = 0;
    Decoder decoder(data);
    const QVariant v = decoder.run(&consumed);
    return decoder.ok() && !v.isNull() && consumed > 0;
}

// ============================================================================
//  TorrentUtils
// ============================================================================

TorrentUtils::TorrentUtils(QObject *parent) : QObject(parent) {}

void TorrentUtils::setBusy(bool value)
{
    if (m_busy == value)
        return;
    m_busy = value;
    emit busyChanged();
}

int TorrentUtils::suggestPieceLength(qint64 totalBytes)
{
    const qint64 KiB = 1024;
    const qint64 MiB = 1024 * KiB;
    if (totalBytes <= 64 * MiB)
        return int(256 * KiB);
    if (totalBytes <= 256 * MiB)
        return int(512 * KiB);
    if (totalBytes <= 1024 * MiB)
        return int(1 * MiB);
    if (totalBytes <= 4 * 1024 * MiB)
        return int(2 * MiB);
    if (totalBytes <= 16 * 1024 * MiB)
        return int(4 * MiB);
    if (totalBytes <= 64 * 1024 * MiB)
        return int(8 * MiB);
    return int(16 * MiB);
}

QString TorrentUtils::formatInfoHash(const QByteArray &infoHash)
{
    return QString::fromLatin1(infoHash.toHex());
}

void TorrentUtils::collectEntries(const QString &root, bool isSingleFile, bool includeHidden,
                                  QList<Entry> &out, qint64 &total, QString &error)
{
    const QFileInfo info(root);
    if (isSingleFile) {
        Entry e;
        e.relativePath = info.fileName();
        e.length = info.size();
        out.append(e);
        total += e.length;
        return;
    }

    // Recursive directory walk, producing paths relative to the directory itself.
    QDirIterator it(root,
                    includeHidden ? (QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
                                  : (QDir::Files | QDir::NoDotAndDotDot),
                    QDirIterator::Subdirectories);
    const QString base = QDir(root).absolutePath();
    int index = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        Entry e;
        e.relativePath = QDir(base).relativeFilePath(fi.absoluteFilePath());
        e.relativePath.replace('\\', '/');
        e.length = fi.size();
        out.append(e);
        total += e.length;
        if (++index % 256 == 0)
            emit progress(0, tr("正在索引… %1 个文件").arg(index));
    }
}

QByteArray TorrentUtils::hashPieces(const QString &root, const QList<Entry> &entries,
                                    int pieceLength, QString &error)
{
    QByteArray pieces;
    pieces.reserve(int(entries.size() * 20 / 4));

    QByteArray carry;
    carry.reserve(pieceLength);
    const bool singleFile = (entries.size() == 1);

    qint64 processed = 0;
    qint64 totalBytes = 0;
    for (const Entry &e : entries)
        totalBytes += e.length;

    int lastPercent = -1;
    for (const Entry &entry : entries) {
        const QString path = singleFile ? root : QDir(root).absoluteFilePath(entry.relativePath);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = tr("无法读取 %1").arg(path);
            return {};
        }
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(4 * 1024 * 1024);
            if (chunk.isEmpty())
                break;
            carry.append(chunk);
            processed += chunk.size();

            int offset = 0;
            while (carry.size() - offset >= pieceLength) {
                pieces.append(QCryptographicHash::hash(carry.mid(offset, pieceLength),
                                                       QCryptographicHash::Sha1));
                offset += pieceLength;
            }
            if (offset > 0)
                carry.remove(0, offset);

            if (totalBytes > 0) {
                const int percent = int(processed * 100 / totalBytes);
                if (percent != lastPercent && percent % 2 == 0) {
                    lastPercent = percent;
                    emit progress(percent, tr("校验中… %1%").arg(percent));
                }
            }
        }
        file.close();
    }
    // Final (short) piece
    if (!carry.isEmpty())
        pieces.append(QCryptographicHash::hash(carry, QCryptographicHash::Sha1));

    return pieces;
}

bool TorrentUtils::create(const QString &sourcePath, const QString &savePath, const QString &trackers,
                          int pieceLength, const QString &comment, const QString &createdBy,
                          bool isPrivate, const QStringList &webSeeds, bool includeHidden)
{
    setBusy(true);
    const QFileInfo info(sourcePath);
    if (!info.exists()) {
        setBusy(false);
        emit failed(tr("源路径不存在：%1").arg(sourcePath));
        return false;
    }
    if (savePath.isEmpty()) {
        setBusy(false);
        emit failed(tr("未指定输出路径。"));
        return false;
    }

    const bool singleFile = info.isFile();

    QList<Entry> entries;
    qint64 total = 0;
    QString error;
    collectEntries(sourcePath, singleFile, includeHidden, entries, total, error);

    if (!error.isEmpty()) {
        setBusy(false);
        emit failed(error);
        return false;
    }
    if (entries.isEmpty()) {
        setBusy(false);
        emit failed(tr("没有可分享的内容：源路径中没有文件。"));
        return false;
    }

    if (pieceLength <= 0)
        pieceLength = suggestPieceLength(total);
    // BitTorrent clients expect a power of two between 16 KiB and 64 MiB.
    pieceLength = qBound(16 * 1024, pieceLength, 64 * 1024 * 1024);

    emit progress(0, tr("正在校验 %1 个文件（%2）").arg(entries.size()).arg(total));

    const QByteArray pieces = hashPieces(sourcePath, entries, pieceLength, error);
    if (!error.isEmpty()) {
        setBusy(false);
        emit failed(error);
        return false;
    }

    // ---- info dictionary ----
    QVariantMap infoDict;
    infoDict.insert(QStringLiteral("name"),
                    (singleFile ? info.fileName() : info.fileName()).toUtf8());
    infoDict.insert(QStringLiteral("piece length"), qint64(pieceLength));
    infoDict.insert(QStringLiteral("pieces"), pieces);
    if (isPrivate)
        infoDict.insert(QStringLiteral("private"), qint64(1));

    if (singleFile) {
        infoDict.insert(QStringLiteral("length"), qint64(entries.first().length));
    } else {
        QVariantList fileList;
        for (const Entry &e : entries) {
            QVariantMap fileDict;
            QVariantList pathParts;
            const QStringList parts = e.relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            for (const QString &p : parts)
                pathParts.append(p.toUtf8());
            fileDict.insert(QStringLiteral("path"), pathParts);
            fileDict.insert(QStringLiteral("length"), qint64(e.length));
            fileList.append(fileDict);
        }
        infoDict.insert(QStringLiteral("files"), fileList);
    }

    const QByteArray infoEncoded = Bencode::encode(infoDict);
    const QByteArray infoHash = QCryptographicHash::hash(infoEncoded, QCryptographicHash::Sha1);

    // ---- root dictionary ----
    QVariantMap root;
    QStringList trackerList;
    for (const QString &t : trackers.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        const QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !trackerList.contains(trimmed))
            trackerList.append(trimmed);
    }
    if (!trackerList.isEmpty()) {
        root.insert(QStringLiteral("announce"), trackerList.first().toUtf8());
        if (trackerList.size() > 1) {
            // Each tracker gets its own tier (a one-element list). The inner
            // list must be wrapped in an explicit QVariant: appending a bare
            // QVariantList would be flattened by the implicit conversion and
            // would produce an announce-list that no client can parse.
            QVariantList tiers;
            for (const QString &t : trackerList) {
                QVariantList tier;
                tier.append(t.toUtf8());
                tiers.append(QVariant(tier));
            }
            root.insert(QStringLiteral("announce-list"), QVariant(tiers));
        }
    }
    root.insert(QStringLiteral("comment"), comment.toUtf8());
    root.insert(QStringLiteral("created by"), (createdBy.isEmpty()
                                                   ? QStringLiteral("Fetchora")
                                                   : createdBy)
                                                  .toUtf8());
    root.insert(QStringLiteral("creation date"), qint64(QDateTime::currentSecsSinceEpoch()));
    root.insert(QStringLiteral("encoding"), QByteArrayLiteral("UTF-8"));
    if (!webSeeds.isEmpty()) {
        QVariantList seeds;
        for (const QString &s : webSeeds) {
            const QString trimmed = s.trimmed();
            if (!trimmed.isEmpty())
                seeds.append(trimmed.toUtf8());
        }
        if (!seeds.isEmpty())
            root.insert(QStringLiteral("url-list"), seeds);
    }
    root.insert(QStringLiteral("info"), infoDict);

    const QByteArray encoded = Bencode::encode(root);

    QDir().mkpath(QFileInfo(savePath).absolutePath());
    QFile out(savePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setBusy(false);
        emit failed(tr("无法写入 %1").arg(savePath));
        return false;
    }
    out.write(encoded);
    out.close();

    setBusy(false);
    emit progress(100, tr("种子已生成。"));
    emit finished(savePath, QString::fromLatin1(infoHash.toHex()), total);
    return true;
}

QVariantMap TorrentUtils::inspect(const QString &torrentPath)
{
    QVariantMap result;
    QFile file(torrentPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), tr("无法打开 %1").arg(torrentPath));
        return result;
    }
    return inspectData(file.readAll());
}

QVariantMap TorrentUtils::inspectData(const QByteArray &data)
{
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);

    if (!Bencode::looksValid(data)) {
        result.insert(QStringLiteral("error"), tr("不是有效的种子文件。"));
        return result;
    }
    const QVariant decoded = Bencode::decode(data);
    const QVariantMap root = decoded.toMap();
    const QVariantMap info = root.value(QStringLiteral("info")).toMap();
    if (info.isEmpty()) {
        result.insert(QStringLiteral("error"), tr("缺少 info 字典。"));
        return result;
    }

    const QByteArray infoEncoded = Bencode::encode(info);
    const QByteArray infoHash = QCryptographicHash::hash(infoEncoded, QCryptographicHash::Sha1);
    const QByteArray pieces = info.value(QStringLiteral("pieces")).toByteArray();

    QVariantList files;
    qint64 total = 0;
    const QString name = QString::fromUtf8(info.value(QStringLiteral("name")).toByteArray());
    const bool multi = info.contains(QStringLiteral("files"));

    if (multi) {
        const QVariantList fileList = info.value(QStringLiteral("files")).toList();
        for (const QVariant &v : fileList) {
            const QVariantMap fd = v.toMap();
            QStringList parts;
            const QVariantList pathParts = fd.value(QStringLiteral("path")).toList();
            for (const QVariant &p : pathParts)
                parts << QString::fromUtf8(p.toByteArray());
            const qint64 len = fd.value(QStringLiteral("length")).toLongLong();
            QVariantMap item;
            item.insert(QStringLiteral("path"), parts.join(QLatin1Char('/')));
            item.insert(QStringLiteral("length"), len);
            files.append(item);
            total += len;
        }
    } else {
        const qint64 len = info.value(QStringLiteral("length")).toLongLong();
        QVariantMap item;
        item.insert(QStringLiteral("path"), name);
        item.insert(QStringLiteral("length"), len);
        files.append(item);
        total = len;
    }

    QVariantList announceList;
    const QVariant al = root.value(QStringLiteral("announce-list"));
    if (al.isValid()) {
        const QVariantList tiers = al.toList();
        for (const QVariant &tier : tiers) {
            const QVariantList urls = tier.toList();
            for (const QVariant &u : urls) {
                const QString url = QString::fromUtf8(u.toByteArray());
                if (!url.isEmpty() && !announceList.contains(url))
                    announceList.append(url);
            }
        }
    }
    const QString announce = QString::fromUtf8(root.value(QStringLiteral("announce")).toByteArray());
    if (!announce.isEmpty() && !announceList.contains(announce))
        announceList.prepend(announce);

    QVariantList webSeeds;
    const QVariant wl = root.value(QStringLiteral("url-list"));
    if (wl.isValid()) {
        if (wl.typeId() == QMetaType::QVariantList) {
            const QVariantList l = wl.toList();
            for (const QVariant &v : l)
                webSeeds.append(QString::fromUtf8(v.toByteArray()));
        } else {
            webSeeds.append(QString::fromUtf8(wl.toByteArray()));
        }
    }

    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("totalLength"), total);
    result.insert(QStringLiteral("pieceLength"), info.value(QStringLiteral("piece length")).toLongLong());
    result.insert(QStringLiteral("pieceCount"), pieces.size() / 20);
    result.insert(QStringLiteral("infoHash"), QString::fromLatin1(infoHash.toHex()));
    result.insert(QStringLiteral("announce"), announce);
    result.insert(QStringLiteral("announceList"), announceList);
    result.insert(QStringLiteral("webSeeds"), webSeeds);
    result.insert(QStringLiteral("comment"), QString::fromUtf8(root.value(QStringLiteral("comment")).toByteArray()));
    result.insert(QStringLiteral("createdBy"), QString::fromUtf8(root.value(QStringLiteral("created by")).toByteArray()));
    result.insert(QStringLiteral("creationDate"), root.value(QStringLiteral("creation date")).toLongLong());
    result.insert(QStringLiteral("isPrivate"), info.value(QStringLiteral("private")).toLongLong() == 1);
    result.insert(QStringLiteral("isMultiFile"), multi);
    result.insert(QStringLiteral("files"), files);
    return result;
}

QString TorrentUtils::toMagnet(const QString &torrentPath)
{
    const QVariantMap info = inspect(torrentPath);
    if (!info.value(QStringLiteral("ok")).toBool())
        return {};

    QStringList parts;
    parts << QStringLiteral("magnet:?xt=urn:btih:") + info.value(QStringLiteral("infoHash")).toString();
    const QString name = info.value(QStringLiteral("name")).toString();
    if (!name.isEmpty())
        parts << QStringLiteral("dn=") + QString::fromUtf8(QUrl::toPercentEncoding(name));
    const QVariantList trackers = info.value(QStringLiteral("announceList")).toList();
    for (const QVariant &t : trackers) {
        const QString url = t.toString();
        if (!url.isEmpty())
            parts << QStringLiteral("tr=") + QString::fromUtf8(QUrl::toPercentEncoding(url));
    }
    return parts.join(QLatin1Char('&'));
}
