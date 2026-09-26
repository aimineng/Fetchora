#ifndef BITTORRENTPAGE_H
#define BITTORRENTPAGE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class Aria2Manager;
class FluentButton;
class QLabel;
class QTableWidget;

namespace Ui {
class BitTorrentPage;
}

/**
 * BitTorrentPage - the tracker manager.
 *
 * Two lists: the trackers handed to every BitTorrent task, and the blacklist that
 * is filtered out of them. The first is fed from subscription sources (the two
 * published collections, or any URL / local file), which are fetched on sync and
 * merged; the second is whatever the user put in it plus the entries of any
 * blacklist source. Health is read from aria2's own announce log rather than by
 * probing third-party servers from this machine.
 *
 * A single task's trackers are edited in the task details, not here.
 */
class BitTorrentPage : public QWidget
{
    Q_OBJECT

public:
    explicit BitTorrentPage(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~BitTorrentPage() override;

    /// Which of the two lists the page is showing.
    enum Tab { Effective = 0, Blacklist };
    /// Opens the page on one of them (also what the tab buttons do).
    void setTab(Tab tab);

public slots:
    /// Re-reads the settings, the table and the health column.
    void refresh();
    /// Fetches every subscription source and pushes the result to the engine.
    void sync();

signals:
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildTabs();
    void buildSources();
    void buildTable();
    void wireManager();

    void rebuildChips();
    void rebuildTable();
    void applyEffectiveList();
    void addFromInput();
    void removeSelectedRow();
    /// Downloads one source and stores what it parsed to.
    void fetchSource(const QString &id);

    QStringList sources() const;
    void setSources(const QStringList &list);
    QStringList blacklist() const;
    void setBlacklist(const QStringList &list);

    void restyle();
    void retranslate();

    Ui::BitTorrentPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    Tab m_tab = Effective;

    FluentButton *m_effectiveTab = nullptr;
    FluentButton *m_blacklistTab = nullptr;
    FluentButton *m_removeButton = nullptr;
    FluentButton *m_syncButton = nullptr;

    QWidget *m_chipsHost = nullptr;      ///< the row of source chips
    class QComboBox *m_sourceEdit = nullptr;
    FluentButton *m_sourceAdd = nullptr;
    QLabel *m_countLabel = nullptr;

    /// What each source returned: source id -> tracker URLs.
    QHash<QString, QStringList> m_fetched;
    /// Sources that failed, so the table can say so instead of showing nothing.
    QHash<QString, QString> m_fetchErrors;
    bool m_syncing = false;
    int m_pendingFetches = 0;
};

#endif // BITTORRENTPAGE_H
