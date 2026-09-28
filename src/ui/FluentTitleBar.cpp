#include "ui/FluentTitleBar.h"

#include "ui/FluentInputs.h"
#include "ui/FluentMainWindow.h"
#include "ui/FluentWidgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWindow>

namespace {
constexpr int kButtonWidth = 46;
constexpr int kTileSize = 24;
} // namespace

FluentTitleBar::FluentTitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(FluentTheme::captionHeight());
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);

    m_appTile = new QLabel(this);
    m_appTile->setFixedSize(kTileSize, kTileSize);
    m_appTile->setAlignment(Qt::AlignCenter);
    m_appTile->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_title = new QLabel(QStringLiteral("Fetchora"), this);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont tf = FluentTheme::uiFont(14);
    tf.setWeight(QFont::DemiBold);
    m_title->setFont(tf);

    m_subtitle = new QLabel(this);
    m_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_subtitle->setFont(FluentTheme::uiFont(12));

    m_primaryBadge = new QLabel(this);
    m_primaryBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_primaryBadge->setFont(FluentTheme::uiFont(11));
    m_primaryBadge->setAlignment(Qt::AlignCenter);
    m_primaryBadge->hide();

    m_secondaryBadge = new QLabel(this);
    m_secondaryBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_secondaryBadge->setFont(FluentTheme::uiFont(11));
    m_secondaryBadge->setAlignment(Qt::AlignCenter);
    m_secondaryBadge->hide();

    m_search = new QLineEdit(this);
    // Names the scope: this box filters the download list, while the history and
    // settings pages each have their own search for their own page.
    m_search->setPlaceholderText(tr("搜索下载任务"));
    m_search->setToolTip(tr("在下载任务里按文件名或链接搜索；历史与设置页各有自己的搜索框"));
    m_search->setFont(FluentTheme::uiFont(12));
    m_search->setFixedHeight(30);
    m_search->setTextMargins(30, 0, 10, 0);
    m_search->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: %3px;"
        "            color: %4; padding: 0; }"
        "QLineEdit:focus { border: 1px solid %5; background: %1; }")
        .arg(FluentTheme::instance()->controlFill().name(),
             FluentTheme::instance()->strokeSubtle().name())
        .arg(FluentTheme::RadiusSmall)
        .arg(FluentTheme::instance()->textPrimary().name(),
             FluentTheme::instance()->controlStroke().name()));
    connect(m_search, &QLineEdit::textChanged, this, &FluentTitleBar::searchEdited);

    m_searchGlyph = new FluentIcon(FluentTheme::Glyph::Search, 14, this);
    m_searchGlyph->useTertiaryColor();

    // The search box owns its own mouse events; the bar drags everywhere else.
    m_search->installEventFilter(this);

    updateWindowButtons();

    // Theme changes re-polish the application sheet, but a widget's own sheet
    // wins over it - so the colours baked in above have to be replayed.
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &FluentTitleBar::restyle);
}

void FluentTitleBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_search->setPlaceholderText(tr("搜索下载任务"));
        m_search->setToolTip(tr("在下载任务里按文件名或链接搜索；历史与设置页各有自己的搜索框"));
    }
}

void FluentTitleBar::setTitle(const QString &title)
{
    m_title->setText(title);
    layoutChildren();
    update();
}

void FluentTitleBar::setSubtitle(const QString &subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
    layoutChildren();
}

void FluentTitleBar::setPrimaryBadge(const QString &text, bool accent)
{
    m_primaryAccent = accent;
    m_primaryBadge->setText(text);
    m_primaryBadge->setVisible(!text.isEmpty());
    const FluentTheme *t = FluentTheme::instance();
    m_primaryBadge->setStyleSheet(
        QStringLiteral("QLabel { background: %1; color: %2; border-radius: 10px; padding: 0 8px; }")
            .arg(accent ? QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 51).name(QColor::HexArgb)
                        : t->cardTertiary().name(),
                 accent ? t->accent().name() : t->textSecondary().name()));
    layoutChildren();
}

void FluentTitleBar::setSecondaryBadge(const QString &text)
{
    m_secondaryBadge->setText(text);
    m_secondaryBadge->setVisible(!text.isEmpty());
    const FluentTheme *t = FluentTheme::instance();
    m_secondaryBadge->setStyleSheet(
        QStringLiteral("QLabel { background: %1; color: %2; border-radius: 10px; padding: 0 8px; }")
            .arg(t->cardTertiary().name(), t->textSecondary().name()));
    layoutChildren();
}

void FluentTitleBar::setSearchVisible(bool visible)
{
    m_search->setVisible(visible);
    m_searchGlyph->setVisible(visible);
    layoutChildren();
}

QString FluentTitleBar::searchText() const { return m_search->text(); }

void FluentTitleBar::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    m_search->setStyleSheet(
        QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; border-radius: %3px;"
                       "            color: %4; padding: 0; }"
                       "QLineEdit:focus { border: 1px solid %5; background: %1; }")
            .arg(t->controlFill().name(), t->strokeSubtle().name())
            .arg(FluentTheme::RadiusSmall)
            .arg(t->textPrimary().name(), t->controlStroke().name()));

    // setPrimaryBadge()/setSecondaryBadge() re-derive their colours, so simply
    // replaying the current text refreshes them.
    if (!m_primaryBadge->text().isEmpty())
        setPrimaryBadge(m_primaryBadge->text(), m_primaryAccent);
    if (!m_secondaryBadge->text().isEmpty())
        setSecondaryBadge(m_secondaryBadge->text());

    updateWindowButtons();
    update();
}

void FluentTitleBar::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

bool FluentTitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::FocusIn) {
        m_search->setStyleSheet(
            QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; border-radius: %3px;"
                           "            color: %4; padding: 0; }")
                .arg(FluentTheme::instance()->controlFill().name(),
                     FluentTheme::instance()->controlStroke().name())
                .arg(FluentTheme::RadiusSmall)
                .arg(FluentTheme::instance()->textPrimary().name()));
    } else if (watched == m_search && event->type() == QEvent::FocusOut) {
        m_search->setStyleSheet(
            QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; border-radius: %3px;"
                           "            color: %4; padding: 0; }")
                .arg(FluentTheme::instance()->controlFill().name(),
                     FluentTheme::instance()->strokeSubtle().name())
                .arg(FluentTheme::RadiusSmall)
                .arg(FluentTheme::instance()->textPrimary().name()));
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------- geometry
QRect FluentTitleBar::buttonRect(Button which) const
{
    switch (which) {
    case Minimize: return QRect(width() - kButtonWidth * 3, 0, kButtonWidth, height());
    case Maximize: return QRect(width() - kButtonWidth * 2, 0, kButtonWidth, height());
    case Close:    return QRect(width() - kButtonWidth * 1, 0, kButtonWidth, height());
    default:       return QRect();
    }
}

FluentTitleBar::Button FluentTitleBar::buttonAt(const QPoint &pos) const
{
    if (buttonRect(Minimize).contains(pos)) return Minimize;
    if (buttonRect(Maximize).contains(pos)) return Maximize;
    if (buttonRect(Close).contains(pos)) return Close;
    return None;
}

void FluentTitleBar::layoutChildren()
{
    int x = 18;
    const int cy = height() / 2;

    m_appTile->move(x, cy - kTileSize / 2);
    x += kTileSize + 12;

    m_title->adjustSize();
    m_title->move(x, cy - m_title->height() / 2);
    x += m_title->width() + 10;

    if (m_subtitle->isVisible()) {
        m_subtitle->adjustSize();
        m_subtitle->move(x, cy - m_subtitle->height() / 2);
        x += m_subtitle->width() + 10;
    }
    if (m_primaryBadge->isVisible()) {
        m_primaryBadge->setFixedHeight(20);
        m_primaryBadge->adjustSize();
        m_primaryBadge->move(x, cy - 10);
        x += m_primaryBadge->width() + 6;
    }
    if (m_secondaryBadge->isVisible()) {
        m_secondaryBadge->setFixedHeight(20);
        m_secondaryBadge->adjustSize();
        m_secondaryBadge->move(x, cy - 10);
    }

    // Search sits in the middle, between the badges and the caption buttons.
    const int searchW = qBound(200, width() / 4, 340);
    m_search->setGeometry((width() - searchW) / 2, cy - 15, searchW, 30);
    m_searchGlyph->move(m_search->x() + 6, cy - 15 + (30 - m_searchGlyph->height()) / 2);
}

void FluentTitleBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void FluentTitleBar::updateWindowButtons()
{
    update();
}

// ------------------------------------------------------------------ input
void FluentTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const Button b = buttonAt(event->pos());
    if (b != None) {
        m_pressed = b;
        update();
        return;
    }
    // Drag the window from anywhere else on the bar. Only when the application
    // owns the chrome: with a native frame (macOS/Linux) the window manager
    // already drags the window from the title bar area, and starting a second
    // move loop from inside a widget would fight it.
    if (FluentMainWindow::usesCustomChrome()) {
        if (auto *win = qobject_cast<FluentMainWindow *>(window())) {
            m_dragging = true;
            win->startSystemMove();
            m_dragging = false;
        }
    }
}

void FluentTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    const Button b = buttonAt(event->pos());
    if (b != m_hovered) {
        m_hovered = b;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void FluentTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    const Button b = buttonAt(event->pos());
    if (m_pressed != None && b == m_pressed) {
        switch (b) {
        case Minimize: emit minimizeRequested(); break;
        case Maximize: emit maximizeRequested(); break;
        case Close:    emit closeRequested(); break;
        default: break;
        }
    }
    m_pressed = None;
    update();
    QWidget::mouseReleaseEvent(event);
}

void FluentTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (buttonAt(event->pos()) == None && event->button() == Qt::LeftButton)
        emit maximizeRequested();
    QWidget::mouseDoubleClickEvent(event);
}

void FluentTitleBar::leaveEvent(QEvent *event)
{
    m_hovered = None;
    m_pressed = None;
    update();
    QWidget::leaveEvent(event);
}

// ----------------------------------------------------------------- painting
void FluentTitleBar::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ---- app tile --------------------------------------------------------
    const QRect tile = m_appTile->geometry();
    QLinearGradient g(tile.topLeft(), tile.bottomLeft());
    g.setColorAt(0.0, t->accentLight1());
    g.setColorAt(1.0, t->accentDark1());
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(tile, FluentTheme::RadiusSmall, FluentTheme::RadiusSmall);
    p.setFont(FluentTheme::iconFont(13));
    p.setPen(t->onAccent());
    p.drawText(tile, Qt::AlignCenter, QString(FluentTheme::Glyph::Download));

    // ---- captions --------------------------------------------------------
    p.setFont(m_title->font());
    p.setPen(t->textPrimary());
    p.drawText(m_title->geometry(), Qt::AlignLeft | Qt::AlignVCenter, m_title->text());

    if (m_subtitle->isVisible()) {
        p.setFont(m_subtitle->font());
        p.setPen(t->textTertiary());
        p.drawText(m_subtitle->geometry(), Qt::AlignLeft | Qt::AlignVCenter, m_subtitle->text());
    }

    // ---- caption buttons -------------------------------------------------
    const bool maximized = window() && window()->isMaximized();
    struct { Button id; QChar glyph; } buttons[] = {
        {Minimize, FluentTheme::Glyph::Minimize},
        {Maximize, maximized ? FluentTheme::Glyph::Restore : FluentTheme::Glyph::Maximize},
        {Close,    FluentTheme::Glyph::Close},
    };

    for (const auto &b : buttons) {
        const QRect r = buttonRect(b.id);
        const bool hovered = (m_hovered == b.id);
        const bool pressed = (m_pressed == b.id);
        if (hovered || pressed) {
            const QColor fill = (b.id == Close)
                                    ? (pressed ? QColor(0xA5, 0x22, 0x16) : QColor(0xC4, 0x2B, 0x1C))
                                    : (pressed ? t->subtlePressed() : t->subtleHover());
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRect(r);
        }
        p.setFont(FluentTheme::iconFont(11));
        p.setPen((b.id == Close && hovered) ? QColor(Qt::white) : t->textPrimary());
        p.drawText(r, Qt::AlignCenter, QString(b.glyph));
    }
}
