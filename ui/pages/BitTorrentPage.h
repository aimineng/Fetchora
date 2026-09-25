#ifndef BITTORRENTPAGE_H
#define BITTORRENTPAGE_H

#include <QHash>
#include <QList>
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
 * BitTorrentPage - the BitTorrent work surface.
 *
 * Sections, top to bottom: header + command bar, the BT statistics strip, the
 * list of BitTorrent tasks and the tracker editor.
 *
 * Structure and layout live in BitTorrentPage.ui; everything that depends on
 * the runtime theme (glyphs, roles, colours, stat cards, list rows) is built
 * here, exactly like DownloadsPage.
 */
class BitTorrentPage : public QWidget
{
    Q_OBJECT
public:
    explicit BitTorrentPage(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~BitTorrentPage() override;

signals:
    void torrentPickerRequested();
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;

private:
    /// One BitTorrent row. Declared here so the page can own them; defined in
    /// BitTorrentPage.cpp because it is pure presentation.
    class BtRow;

    void buildCommandBar();
    void buildStatCards();
    void buildTaskList();
    void buildTrackerEditor();
    void wireManager();

    void refresh();
    void refreshStats();
    void refreshTaskList();
    void refreshTrackers();

    void restyle();
    void retranslate();

    void toggleMagnetPanel();
    void submitMagnet();

    void selectRow(const QString &gid);
    void setSelectedRow(const QString &gid);
    void toggleRowPause(const QString &gid);
    void copyRowMagnet(const QString &gid);
    void openRowFolder(const QString &gid);
    void removeRow(const QString &gid);

    void addTrackerFromInput();
    void removeSelectedTracker();

    QVariantMap taskFor(const QString &gid) const;
    /// Trackers of one task; Aria2Manager publishes them as "trackers".
    static QStringList trackersOf(const QVariantMap &task);
    /// The gid currently picked in the tracker combo ("" when there is none).
    QString trackerGid() const;

    Ui::BitTorrentPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    FluentButton *m_addTorrentButton = nullptr;
    FluentButton *m_magnetButton = nullptr;
    FluentButton *m_magnetSubmitButton = nullptr;
    FluentButton *m_pauseAllButton = nullptr;
    FluentButton *m_resumeAllButton = nullptr;
    FluentButton *m_refreshButton = nullptr;
    FluentButton *m_trackerAddButton = nullptr;
    FluentButton *m_trackerRemoveButton = nullptr;

    FluentIcon *m_tasksIcon = nullptr;
    FluentIcon *m_emptyIcon = nullptr;

    QList<StatCard *> m_cards;
    QHash<QString, BtRow *> m_rows;
    QStringList m_rowOrder;   ///< gid order of the rows currently laid out
    QStringList m_comboGids;  ///< gids currently in the tracker combo
    QString m_selectedGid;    ///< highlighted row ("" when nothing is picked)
};

#endif // BITTORRENTPAGE_H
