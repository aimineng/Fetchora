#ifndef FLUENTTITLEBAR_H
#define FLUENTTITLEBAR_H

#include <QWidget>

class QLabel;
class QLineEdit;
class FluentIcon;

/**
 * FluentTitleBar - the WinUI caption bar.
 *
 * Layout: [app tile] [title] [status badges] ---- [centred search] ---- [window buttons]
 *
 * Dragging is implemented with FluentMainWindow::startSystemMove() rather than
 * by returning HTCAPTION from WM_NCHITTEST: doing the latter on a maximized
 * window makes Windows swallow every click.
 */
class FluentTitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit FluentTitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setSubtitle(const QString &subtitle);
    /// Small pill badges next to the title (speed, active count, ...).
    void setPrimaryBadge(const QString &text, bool accent = false);
    void setSecondaryBadge(const QString &text);

    void setSearchVisible(bool visible);
    QString searchText() const;
    void focusSearch();

    /// Re-apply the colours baked into the search field and the badges. Wired to
    /// FluentTheme::changed (rather than to QEvent::StyleChange) so the
    /// setStyleSheet() calls inside cannot re-enter and recurse.
    void restyle();

signals:
    void searchEdited(const QString &text);
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();
    /// Emitted when the window state changes so the caption button can update.
    void windowStateChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum Button { None, Minimize, Maximize, Close };

    QRect buttonRect(Button which) const;
    Button buttonAt(const QPoint &pos) const;
    void layoutChildren();
    void updateWindowButtons();

    QLabel *m_appTile = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_primaryBadge = nullptr;
    QLabel *m_secondaryBadge = nullptr;
    QLineEdit *m_search = nullptr;
    FluentIcon *m_searchGlyph = nullptr;

    Button m_hovered = None;
    Button m_pressed = None;
    bool m_dragging = false;
    bool m_primaryAccent = false;
    QPoint m_dragOrigin;
};

#endif // FLUENTTITLEBAR_H
