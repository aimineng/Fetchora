#include "ui/FluentNavigationView.h"

#include "ui/FluentWidgets.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

// ============================================================================
//  FluentNavItem
// ============================================================================
FluentNavItem::FluentNavItem(const QChar &glyph, const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_glyph(glyph)
    , m_title(title)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(40);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void FluentNavItem::setGlyph(const QChar &glyph) { m_glyph = glyph; update(); }
void FluentNavItem::setTitle(const QString &title) { m_title = title; update(); }

void FluentNavItem::setBadge(const QString &badge)
{
    if (m_badge == badge)
        return;
    m_badge = badge;
    update();
}

void FluentNavItem::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void FluentNavItem::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    update();
}

void FluentNavItem::setHeading(bool heading)
{
    m_heading = heading;
    setCursor(heading ? Qt::ArrowCursor : Qt::PointingHandCursor);
    setEnabled(!heading);
    update();
}

QSize FluentNavItem::sizeHint() const { return QSize(200, 40); }

void FluentNavItem::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_hovered = true;
    update();
}

void FluentNavItem::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hovered = false;
    update();
}

void FluentNavItem::mouseReleaseEvent(QMouseEvent *event)
{
    QWidget::mouseReleaseEvent(event);
    if (!m_heading && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit activated();
}

void FluentNavItem::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF pill = QRectF(rect()).adjusted(4, 1, -4, -1);

    if (m_heading) {
        if (!m_collapsed) {
            p.setFont(FluentTheme::uiFont(11));
            p.setPen(t->textTertiary());
            p.drawText(QRectF(pill.left() + 14, pill.top(), pill.width(), pill.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, m_title);
        }
        return;
    }

    // ---- pill ------------------------------------------------------------
    QColor fill = Qt::transparent;
    if (m_selected)
        fill = m_hovered ? t->subtlePressed() : t->subtleHover();
    else if (m_hovered)
        fill = t->subtleHover();
    if (fill.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(pill, FluentTheme::RadiusMedium, FluentTheme::RadiusMedium);
    }

    // ---- leading accent indicator ---------------------------------------
    if (m_selected) {
        p.setBrush(t->accent());
        p.drawRoundedRect(QRectF(pill.left() + 1, pill.center().y() - 8, 3, 16), 1.5, 1.5);
    }

    // ---- glyph + title ---------------------------------------------------
    const int glyphX = m_collapsed ? int((width() - 20) / 2) : pill.left() + 16;
    p.setFont(FluentTheme::iconFont(16));
    p.setPen(m_selected ? t->accent() : t->textPrimary());
    p.drawText(QRectF(glyphX, pill.top(), 20, pill.height()), Qt::AlignCenter, QString(m_glyph));

    if (!m_collapsed) {
        QFont f = FluentTheme::uiFont(14);
        f.setWeight(m_selected ? QFont::DemiBold : QFont::Normal);
        p.setFont(f);
        p.setPen(t->textPrimary());
        const int textX = glyphX + 22 + 12;
        const int textW = width() - textX - (m_badge.isEmpty() ? 16 : 46);
        // Elide rather than hard-clip: the English captions are considerably
        // longer than the Chinese ones and a clipped word reads as a bug.
        const QFontMetrics fm(f);
        const QString label = fm.elidedText(m_title, Qt::ElideRight, qMax(0, textW));
        p.drawText(QRectF(textX, pill.top(), qMax(0, textW), pill.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
    }

    // ---- badge -----------------------------------------------------------
    if (!m_badge.isEmpty() && !m_collapsed) {
        p.setFont(FluentTheme::uiFont(11));
        const QFontMetrics fm(p.font());
        const int w = qMax(20, fm.horizontalAdvance(m_badge) + 12);
        const QRectF badge(pill.right() - w - 12, pill.center().y() - 9, w, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(m_selected ? QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 56)
                              : t->cardTertiary());
        p.drawRoundedRect(badge, 9, 9);
        p.setPen(m_selected ? t->accent() : t->textSecondary());
        p.drawText(badge, Qt::AlignCenter, m_badge);
    }
}

// ============================================================================
//  FluentNavigationView
// ============================================================================
FluentNavigationView::FluentNavigationView(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
    // Without this the pane is only as wide as its widest item's sizeHint, and
    // the captions get elided even though there is room for them.
    setFixedWidth(FluentTheme::navWidth());

    m_top = new QVBoxLayout(this);
    m_top->setContentsMargins(0, 8, 0, 8);
    m_top->setSpacing(2);
    m_top->addStretch(1);

    m_bottom = new QVBoxLayout();
    m_bottom->setContentsMargins(0, 0, 0, 8);
    m_bottom->setSpacing(2);

    // Engine status pill.
    auto *statusRow = new QWidget(this);
    statusRow->setFixedHeight(34);
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(14, 0, 12, 0);
    statusLayout->setSpacing(10);
    m_engineDot = new QLabel(statusRow);
    m_engineDot->setFixedSize(8, 8);
    m_engineText = new QLabel(statusRow);
    m_engineText->setFont(FluentTheme::uiFont(12));
    statusLayout->addWidget(m_engineDot);
    statusLayout->addWidget(m_engineText, 1);
    m_bottom->addWidget(statusRow);

    m_top->addLayout(m_bottom);

    setEngineState(false, true);
}

FluentNavItem *FluentNavigationView::addItem(const QString &key, const QChar &glyph, const QString &title)
{
    auto *item = new FluentNavItem(glyph, title, this);
    item->setCollapsed(m_collapsed);
    connect(item, &FluentNavItem::activated, this, [this, key]() { setCurrentKey(key); });
    // Insert above the bottom block (status pill + collapse toggle).
    m_top->insertWidget(m_top->count() - 2, item);
    m_items.insert(key, item);
    return item;
}

void FluentNavigationView::addHeading(const QString &text)
{
    auto *heading = new FluentNavItem(QChar(), text, this);
    heading->setHeading(true);
    heading->setFixedHeight(28);
    m_top->insertWidget(m_top->count() - 2, heading);
}

void FluentNavigationView::addSpacer(int height)
{
    auto *spacer = new QWidget(this);
    spacer->setFixedHeight(height);
    m_top->insertWidget(m_top->count() - 2, spacer);
}

void FluentNavigationView::setCurrentKey(const QString &key)
{
    if (m_currentKey == key)
        return;
    m_currentKey = key;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        it.value()->setSelected(it.key() == key);
    emit pageRequested(key);
}

void FluentNavigationView::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    setFixedWidth(collapsed ? 64 : FluentTheme::navWidth());
    for (FluentNavItem *item : std::as_const(m_items))
        item->setCollapsed(collapsed);
    m_engineText->setVisible(!collapsed);
    relayout();
}

void FluentNavigationView::setEngineState(bool ready, bool starting)
{
    m_engineReady = ready;
    m_engineStarting = starting;
    retranslate();
}

void FluentNavigationView::retranslate()
{
    const FluentTheme *t = FluentTheme::instance();
    QColor dot = m_engineReady ? t->success() : (m_engineStarting ? t->textTertiary() : t->critical());
    m_engineDot->setStyleSheet(
        QStringLiteral("background: %1; border-radius: 4px;").arg(dot.name()));
    m_engineText->setText(m_engineReady ? tr("引擎已连接")
                                        : (m_engineStarting ? tr("引擎启动中") : tr("引擎未连接")));
    m_engineText->setStyleSheet(
        QStringLiteral("color: %1;").arg(t->textSecondary().name()));
}

void FluentNavigationView::setItemTitle(const QString &key, const QString &title)
{
    if (FluentNavItem *item = m_items.value(key))
        item->setTitle(title);
}

void FluentNavigationView::setItemBadge(const QString &key, const QString &badge)
{
    if (FluentNavItem *item = m_items.value(key))
        item->setBadge(badge);
}

void FluentNavigationView::relayout()
{
    for (FluentNavItem *item : std::as_const(m_items))
        item->updateGeometry();
    updateGeometry();
    update();
}

void FluentNavigationView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), FluentTheme::instance()->backgroundAlt());
}
