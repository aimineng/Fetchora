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

    void refreshTrackers();
    void addTrackerFromInput();
    void removeSelectedTracker();
    /// Pick a list from a file or a URL, parse it and hand the trackers over.
    void importTrackers();
    void importTrackersFromFile();
    void importTrackersFromUrl();
    /// Parses what was read and merges it into the global list.
    void applyImportedTrackers(const QByteArray &data, const QString &source);

    void restyle();
    void retranslate();

    Ui::BitTorrentPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    FluentButton *m_trackerAddButton = nullptr;
    FluentButton *m_trackerRemoveButton = nullptr;
    FluentButton *m_trackerImportButton = nullptr;
    FluentButton *m_trackerResetButton = nullptr;
};

#endif // BITTORRENTPAGE_H
