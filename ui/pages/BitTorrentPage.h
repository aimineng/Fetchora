#ifndef BITTORRENTPAGE_H
#define BITTORRENTPAGE_H

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

class Aria2Manager;
class FluentButton;
class FluentIcon;
class StatCard;

namespace Ui {
class BitTorrentPage;
}

/**
 * BitTorrentPage - the tracker editor.
 *
 * This page used to be a second task list (torrents only) with its own
 * statistics, its own add-torrent/add-magnet buttons and this editor. Lists were
 * duplicated: the downloads page already shows every task, torrents included, and
 * adding a .torrent or a magnet belongs next to the other ways of adding a
 * download. What is left is the part with no other home - adding, importing and
 * removing the trackers of a torrent task.
 */
class BitTorrentPage : public QWidget
{
    Q_OBJECT

public:
    explicit BitTorrentPage(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~BitTorrentPage() override;

public slots:
    /// Re-read the torrent tasks and the tracker list of the selected one.
    void refresh();

signals:
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildTrackerEditor();
    void wireManager();

    QVariantMap taskFor(const QString &gid) const;
    /// Trackers of one task; Aria2Manager publishes them as "trackers".
    static QStringList trackersOf(const QVariantMap &task);
    /// The gid currently picked in the tracker combo ("" when there is none).
    QString trackerGid() const;

    void refreshTrackers();
    void addTrackerFromInput();
    void removeSelectedTracker();
    /// Pick a list from a file or a URL, parse it and hand the trackers over.
    void importTrackers();
    void importTrackersFromFile();
    void importTrackersFromUrl();
    /// Adds `trackers` to the selected task and reports what was used/ignored.
    void applyImportedTrackers(const QStringList &trackers, int rejected, const QString &source,
                               bool truncated);

    void restyle();
    void retranslate();

    Ui::BitTorrentPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    FluentButton *m_trackerAddButton = nullptr;
    FluentButton *m_trackerRemoveButton = nullptr;
    FluentButton *m_trackerImportButton = nullptr;

    QStringList m_comboGids;   ///< gids currently in the tracker combo
};

#endif // BITTORRENTPAGE_H
