#ifndef FLUENTTASKLIST_H
#define FLUENTTASKLIST_H

#include <QFrame>
#include <QHash>
#include <QList>
#include <QVariantMap>
#include <QWidget>

class FluentButton;
class FluentIcon;
class FluentProgressBar;
class QLabel;
class QVBoxLayout;
class QScrollArea;

/**
 * FluentTaskCard - one download row.
 *
 * Layout: [state plate] [name + meta + progress] [speed / eta] [actions]
 *
 * The row is a hit target (click selects, double click acts) and the action
 * buttons appear on hover. Everything is repainted in place when the poll
 * updates the task, so the list never flickers.
 */
class FluentTaskCard : public QFrame
{
    Q_OBJECT
public:
    explicit FluentTaskCard(const QString &gid, QWidget *parent = nullptr);

    QString gid() const { return m_gid; }
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    /// Merge a task map from Aria2Manager::tasks(); only repaints what changed.
    void updateTask(const QVariantMap &task);
    const QVariantMap &task() const { return m_task; }

    /// Suggested height for the current content.
    int preferredHeight() const;

    /// Re-apply the internal layout; called on resize and after content changes.
    /// NOTE: must stay *before* the signals: section - moc keeps parsing in
    /// "signals" mode until it meets an explicit access specifier, so a method
    /// declared after it would be moc'd as a signal (and moc would emit an empty
    /// definition of it, which then collides at link time).
    void relayout();

signals:
    void clicked(const QString &gid);
    void doubleClicked(const QString &gid);
    void pauseRequested(const QString &gid);
    void resumeRequested(const QString &gid);
    void openRequested(const QString &gid);
    void folderRequested(const QString &gid);
    void removeRequested(const QString &gid);
    void copyLinkRequested(const QString &gid);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildActions();
    QString metaLine() const;
    QChar plateGlyph() const;
    QColor statusTint() const;

    QString m_gid;
    QVariantMap m_task;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_actionsVisible = false;

    FluentIcon *m_stateGlyph = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_meta = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_eta = nullptr;
    QLabel *m_percent = nullptr;
    FluentProgressBar *m_progress = nullptr;

    QWidget *m_actionBar = nullptr;
    FluentButton *m_pauseButton = nullptr;
    FluentButton *m_retryButton = nullptr;
    FluentButton *m_openButton = nullptr;
    FluentButton *m_folderButton = nullptr;
    FluentButton *m_copyButton = nullptr;
    FluentButton *m_removeButton = nullptr;
};

/**
 * FluentTaskList - scrollable list of FluentTaskCard.
 *
 * Keeps one card per gid and updates it in place: rebuilding the list on every
 * poll (once a second) would reset scroll position and flicker.
 */
class FluentTaskList : public QWidget
{
    Q_OBJECT
public:
    explicit FluentTaskList(QWidget *parent = nullptr);

    /// Replace the model. Preserves scroll position and card identity.
    void setTasks(const QVariantList &tasks);
    void setSelectedGid(const QString &gid);
    QString selectedGid() const { return m_selectedGid; }

    void setEmptyStateText(const QString &title, const QString &hint);
    int cardCount() const { return m_cards.size(); }

signals:
    void selectionChanged(const QString &gid);
    void pauseRequested(const QString &gid);
    void resumeRequested(const QString &gid);
    void openRequested(const QString &gid);
    void folderRequested(const QString &gid);
    void removeRequested(const QString &gid);
    void copyLinkRequested(const QString &gid);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildEmptyState();
    /// Re-applies the colours baked into the empty-state tile.
    void restyleEmptyState();

    QScrollArea *m_scroll = nullptr;
    QWidget *m_container = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_emptyState = nullptr;
    FluentIcon *m_emptyIcon = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyHint = nullptr;

    QHash<QString, FluentTaskCard *> m_cards;
    QStringList m_order;
    QString m_selectedGid;
    QString m_emptyTitleText;
    QString m_emptyHintText;
};

#endif // FLUENTTASKLIST_H
