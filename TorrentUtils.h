#ifndef TORRENTUTILS_H
#define TORRENTUTILS_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

/**
 * Bencode - minimal but complete bencode codec (encode + decode).
 */
namespace Bencode {
QByteArray encode(const QVariant &value);       ///< QVariantMap / QVariantList / QByteArray / qint64
QVariant decode(const QByteArray &data, int *consumed = nullptr); ///< returns QVariantMap/List/ByteArray/qint64
bool looksValid(const QByteArray &data);
}

/**
 * TorrentUtils - create, inspect and convert .torrent files.
 *
 * Backed by a real bencode implementation, so the produced files are fully
 * standard (v1 BitTorrent, UTF-8 name encoding, private flag, web seeds,
 * multiple trackers and tracker tiers) and readable by every client.
 */
class TorrentUtils : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit TorrentUtils(QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    /**
     * Build a .torrent file.
     * @param sourcePath    file or directory to share
     * @param savePath      destination .torrent path
     * @param trackers      newline/comma separated announce URLs (first = announce)
     * @param pieceLength   bytes per piece (0 = auto: 256 KiB..16 MiB depending on size)
     * @param comment       optional comment
     * @param createdBy     optional creator string
     * @param isPrivate     set the private flag
     * @param webSeeds      HTTP/FTP mirrors (url-list)
     * @param includeHidden include dot-files while walking directories
     */
    Q_INVOKABLE bool create(const QString &sourcePath,
                            const QString &savePath,
                            const QString &trackers,
                            int pieceLength = 0,
                            const QString &comment = QString(),
                            const QString &createdBy = QString(),
                            bool isPrivate = false,
                            const QStringList &webSeeds = QStringList(),
                            bool includeHidden = false);

    /// Compute the optimal piece length for a payload of `totalBytes`.
    Q_INVOKABLE static int suggestPieceLength(qint64 totalBytes);

    /// Parse a .torrent file and return { ok, name, totalLength, pieceLength,
    /// pieces, infoHash, announce, announceList, comment, createdBy, creationDate,
    /// isPrivate, isMultiFile, files: [ {path,length} ], webSeeds }.
    Q_INVOKABLE QVariantMap inspect(const QString &torrentPath);

    /// Same as inspect() but from raw bytes.
    Q_INVOKABLE QVariantMap inspectData(const QByteArray &data);

    /// Build a magnet link from a .torrent file.
    Q_INVOKABLE QString toMagnet(const QString &torrentPath);

    /// Download metadata helper: number of pieces * 20 bytes.
    Q_INVOKABLE static QString formatInfoHash(const QByteArray &infoHash);

signals:
    void busyChanged();
    void progress(int percent, const QString &message);
    void finished(const QString &savePath, const QString &infoHash, qint64 totalLength);
    void failed(const QString &reason);

private:
    struct Entry {
        QString relativePath;
        qint64 length = 0;
    };

    void collectEntries(const QString &root, bool isSingleFile, bool includeHidden,
                        QList<Entry> &out, qint64 &total, QString &error);
    QByteArray hashPieces(const QString &root, const QList<Entry> &entries, int pieceLength,
                          QString &error);

    bool m_busy = false;
    void setBusy(bool value);
};

#endif // TORRENTUTILS_H
