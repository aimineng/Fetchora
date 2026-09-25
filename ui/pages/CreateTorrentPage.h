#ifndef CREATETORRENTPAGE_H
#define CREATETORRENTPAGE_H

#include <QString>
#include <QStringList>
#include <QWidget>

class FluentButton;
class FluentProgressBar;
class InfoBar;
class TorrentUtils;

namespace Ui {
class CreateTorrentPage;
}

/**
 * CreateTorrentPage - build a .torrent from a local file or folder.
 *
 * One centred form card (source, trackers, piece size, options, action bar)
 * plus a preview card that renders whatever TorrentUtils::inspect() returns for
 * an existing .torrent file.
 *
 * Structure and layout live in CreateTorrentPage.ui; everything that depends on
 * the runtime theme (glyphs, roles, colours, the progress bar, the info bars) is
 * built here, exactly like DownloadsPage.
 */
class CreateTorrentPage : public QWidget
{
    Q_OBJECT
public:
    explicit CreateTorrentPage(TorrentUtils *torrents, QWidget *parent = nullptr);
    ~CreateTorrentPage() override;

signals:
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildForm();
    void buildPreview();
    void wireUtils();

    void pickSourceFile();
    void pickSourceFolder();
    void pickSaveTarget();
    void pickTorrentToInspect();
    void fillCommonTrackers();

    void setSource(const QString &path);
    void setSaveTarget(const QString &path);
    void updatePieceHint();
    void updateActionState();

    void startCreate();
    void showPreview(const QString &torrentPath);
    void clearPreview();
    void copyMagnet();

    void showResult(InfoBar *bar, bool success, const QString &title, const QString &message);

    QStringList trackerUrls() const;
    QStringList webSeedUrls() const;
    int pieceLength() const;
    /// Total payload of a file or of a directory tree (honours 包含隐藏文件).
    qint64 sourceSize(const QString &path) const;

    void restyle();
    void retranslate();

    Ui::CreateTorrentPage *ui = nullptr;
    TorrentUtils *m_torrents = nullptr;

    FluentButton *m_pickFileButton = nullptr;
    FluentButton *m_pickFolderButton = nullptr;
    FluentButton *m_pickSaveButton = nullptr;
    FluentButton *m_commonTrackersButton = nullptr;
    FluentButton *m_createButton = nullptr;
    FluentButton *m_inspectButton = nullptr;
    FluentButton *m_copyMagnetButton = nullptr;

    FluentProgressBar *m_progressBar = nullptr;
    InfoBar *m_banner = nullptr;         ///< result of the last create()
    InfoBar *m_previewBanner = nullptr;  ///< result of the last inspect()

    QString m_sourcePath;
    QString m_savePath;
    QString m_inspectPath;
    int m_suggestedPieceLength = 0;
};

#endif // CREATETORRENTPAGE_H
