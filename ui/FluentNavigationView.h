#ifndef FLUENTNAVIGATIONVIEW_H
#define FLUENTNAVIGATIONVIEW_H

#include <QWidget>
#include <QHash>
#include <QList>

class FluentIcon;
class QVBoxLayout;
class QLabel;

/**
 * FluentNavItem - one row of the navigation pane.
 *
 * Draws the Fluent selection indicator: a rounded pill behind the row plus the
 * 3x16 accent bar on the leading edge of the selected item.
 */
class FluentNavItem : public QWidget
{
    Q_OBJECT
public:
    FluentNavItem(const QChar &glyph, const QString &title, QWidget *parent = nullptr);

    void setGlyph(const QChar &glyph);
    void setTitle(const QString &title);
    void setBadge(const QString &badge);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setCollapsed(bool collapsed);
    /// Non-selectable heading (e.g. "设置" group separator).
    void setHeading(bool heading);

signals:
    void activated();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    QChar m_glyph;
    QString m_title;
    QString m_badge;
    bool m_selected = false;
    bool m_collapsed = false;
    bool m_hovered = false;
    bool m_heading = false;
};

/**
 * FluentNavigationView - the WinUI NavigationView pane.
 *
 * Emits pageRequested(key) when a row is activated; the key matches the page
 * registry used by the shell ("download", "history", "settings", ...).
 */
class FluentNavigationView : public QWidget
{
    Q_OBJECT
public:
    explicit FluentNavigationView(QWidget *parent = nullptr);

    /// Adds a navigable row. Returns the item so callers can update its badge.
    FluentNavItem *addItem(const QString &key, const QChar &glyph, const QString &title);
    /// Adds a non-selectable section heading.
    void addHeading(const QString &text);
    /// Adds a visual separator.
    void addSpacer(int height = 8);

    void setCurrentKey(const QString &key);
    QString currentKey() const { return m_currentKey; }

    /// Replace the caption of an existing row (used when the UI language changes).
    void setItemTitle(const QString &key, const QString &title);
    /// Re-apply the language-dependent text of the built-in widgets.
    void retranslate();

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    /// Bottom status pill: dot + text ("引擎已连接" / "引擎启动中" / ...).
    void setEngineState(bool ready, bool starting);
    /// Small badge on the item whose key matches.
    void setItemBadge(const QString &key, const QString &badge);

protected:
    void paintEvent(QPaintEvent *event) override;

signals:
    /// The user picked a page; the key is the one passed to addItem().
    void pageRequested(const QString &key);

private:
    void relayout();

    QVBoxLayout *m_top = nullptr;
    QVBoxLayout *m_bottom = nullptr;
    QHash<QString, FluentNavItem *> m_items;
    QString m_currentKey;
    bool m_collapsed = false;

    QLabel *m_engineDot = nullptr;
    QLabel *m_engineText = nullptr;
    FluentNavItem *m_collapseItem = nullptr;
    bool m_engineReady = false;
    bool m_engineStarting = true;
};

#endif // FLUENTNAVIGATIONVIEW_H
