#ifndef DOWNLOADSPAGE_H
#define DOWNLOADSPAGE_H

#include <QWidget>

#include "ui/FluentTaskList.h"

class Aria2Manager;
class FluentButton;
class InfoBar;
class StatCard;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace Ui {
class DownloadsPage;
}

/**
 * DownloadsPage - the task list with command bar, statistics strip, status
 * filters and an optional details pane.
 *
 * Structure and layout live in DownloadsPage.ui; everything that depends on
 * the runtime theme (glyphs, roles, colours, the stat cards) is built here.
 */
class DownloadsPage : public QWidget
{
    Q_OBJECT
public:
    explicit DownloadsPage(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~DownloadsPage() override;

    /// Restrict the page to one status bucket ("all", "waiting", ...).
    void setStatusFilter(const QString &filter);
    QString statusFilter() const { return m_filter; }

    void setShowFilterBar(bool show);
    void setEmptyStateText(const QString &title, const QString &hint);

    /// Selects a task and asks the owner to show its details.
    void selectTask(const QString &gid);
    QString selectedGid() const { return m_list->selectedGid(); }

signals:
    void newTaskRequested();
    void torrentPickerRequested();
    void selectionChanged(const QString &gid);
    /// The command bar's details button was pressed; the shell toggles the pane.
    void detailsToggleRequested();
    void toast(const QString &message, bool isError);

protected:
    void changeEvent(QEvent *event) override;

private:
    /// The tasks this page shows: its own status filter + the global search text.
    QVariantList visibleTasks() const;
    void buildCommandBar();
    void buildStatCards();
    void buildFilterChips();
    void wireManager();
    void refresh();
    void restyle();

    Ui::DownloadsPage *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;

    FluentButton *m_newButton = nullptr;
    FluentButton *m_torrentButton = nullptr;
    FluentButton *m_pauseAllButton = nullptr;
    FluentButton *m_resumeAllButton = nullptr;
    FluentButton *m_refreshButton = nullptr;
    FluentButton *m_clearButton = nullptr;
    FluentButton *m_detailsButton = nullptr;

    QList<StatCard *> m_cards;
    QList<FluentButton *> m_chips;
    QComboBox *m_sortCombo = nullptr;
    FluentTaskList *m_list = nullptr;
    InfoBar *m_banner = nullptr;

    QString m_filter = QStringLiteral("all");
    QString m_emptyTitle;
    QString m_emptyHint;
};

#endif // DOWNLOADSPAGE_H
