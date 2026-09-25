#ifndef HISTORYPAGE_H
#define HISTORYPAGE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class Aria2Manager;
class FluentButton;
class FluentComboBox;
class FluentIcon;
class FluentLineEdit;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class StatCard;

namespace Ui {
class HistoryPage;
}

/**
 * HistoryPage - the persistent download history kept in DownloadHistory.
 *
 * The header, the statistics strip and the host layouts live in
 * HistoryPage.ui. Rows are self-contained HistoryRow widgets (no QTableWidget
 * and no QTreeWidget) created in HistoryPage.cpp, so every row can carry the
 * status pill, the elided path and its own hover actions.
 */
class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~HistoryPage() override;

signals:
    /// The user asked for a history entry to be queued again.
    void redownloadRequested(const QString &uri);

protected:
    void changeEvent(QEvent *event) override;

private:
    /// 一条历史记录行；自绘卡片，定义在 HistoryPage.cpp。
    class HistoryRow;

    // 行内按钮通过这些入口回调页面（嵌套类可以访问宿主私有成员）。
    void requestRedownload(const QString &uri);
    void copyLink(const QString &uri);
    void openEntryFolder(const QString &dir, const QString &name);
    void removeEntry(const QString &gid);

    /// 当前筛选键："all" | "complete" | "error" | "removed"。
    QString filterKey() const;

    void buildHeader();
    void buildStatCards();
    void buildList();
    void buildEmptyState();
    void wireManager();

    void refresh();
    void retranslate();
    void restyle();

    Ui::HistoryPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    FluentLineEdit *m_search = nullptr;
    FluentComboBox *m_filterCombo = nullptr;
    FluentButton *m_refreshButton = nullptr;
    QList<StatCard *> m_cards;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_rowsHost = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QHash<QString, HistoryRow *> m_rows;
    QStringList m_order;

    QWidget *m_emptyState = nullptr;
    QWidget *m_emptyPlate = nullptr;
    FluentIcon *m_emptyGlyph = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;
    FluentButton *m_emptyResetButton = nullptr;

    QString m_searchText;
    int m_filterIndex = 0;
};

#endif // HISTORYPAGE_H
