#include "ui/FluentWidgets.h"

#include "ui/FluentButton.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

// ============================================================================
//  FluentIcon
// ============================================================================
FluentIcon::FluentIcon(QWidget *parent)
    : QLabel(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    apply();
}

FluentIcon::FluentIcon(const QChar &glyph, int size, QWidget *parent)
    : FluentIcon(parent)
{
    m_glyph = glyph;
    m_size = size;
    apply();
}

void FluentIcon::setGlyph(const QChar &glyph)
{
    if (m_glyph == glyph)
        return;
    m_glyph = glyph;
    apply();
}

void FluentIcon::setIconSize(int pixelSize)
{
    if (m_size == pixelSize)
        return;
    m_size = pixelSize;
    apply();
}

void FluentIcon::setIconColor(const QColor &color)
{
    m_followTheme = false;
    if (m_color == color)
        return;
    m_color = color;
    apply();
}

void FluentIcon::usePrimaryColor()
{
    m_followTheme = true;
    apply();
}

void FluentIcon::useSecondaryColor()
{
    m_followTheme = false;
    m_color = FluentTheme::instance()->textSecondary();
    apply();
}

void FluentIcon::useTertiaryColor()
{
    m_followTheme = false;
    m_color = FluentTheme::instance()->textTertiary();
    apply();
}

void FluentIcon::useAccentColor()
{
    m_followTheme = false;
    m_color = FluentTheme::instance()->accent();
    apply();
}

void FluentIcon::changeEvent(QEvent *event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        apply();
}

void FluentIcon::apply()
{
    // Guards against re-entrancy: setStyleSheet() below emits StyleChange,
    // which lands back in changeEvent() -> apply(). Without this guard the
    // widget recurses until the stack overflows and the process dies without
    // any diagnostic output at all.
    if (m_applying)
        return;
    m_applying = true;

    setFont(FluentTheme::iconFont(m_size));
    setFixedSize(m_size + 6, m_size + 6);
    setText(m_glyph.isNull() ? QString() : QString(m_glyph));

    const QColor c = m_followTheme ? FluentTheme::instance()->textPrimary() : m_color;
    /*
        The font-family MUST be declared here, not only via setFont().

        Once a widget is covered by a style sheet, Qt resolves its font through
        the sheet, and a `QWidget { font-family: ... }` rule in the application
        sheet wins over setFont(). The icon font would then be replaced by the UI
        font, which has no glyphs in the private use area - the icon silently
        renders as nothing. Widgets that paint the glyph themselves (FluentButton,
        FluentNavItem, the title bar) are unaffected, which is why this only ever
        showed up on FluentIcon.
    */
    setStyleSheet(QStringLiteral("QLabel { font-family: \"%1\"; font-size: %2px;"
                                 " color: %3; background: transparent; border: none; }")
                      .arg(FluentTheme::iconFont())
                      .arg(m_size)
                      .arg(c.name(QColor::HexRgb)));

    m_applying = false;
}

// ============================================================================
//  FluentCard
// ============================================================================
FluentCard::FluentCard(QWidget *parent)
    : QFrame(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(FluentTheme::spacingL(), FluentTheme::spacingL(),
                               FluentTheme::spacingL(), FluentTheme::spacingL());
    m_body->setSpacing(FluentTheme::spacingM());
}

QVBoxLayout *FluentCard::body() { return m_body; }

void FluentCard::setVariant(Variant variant)
{
    if (m_variant == variant)
        return;
    m_variant = variant;
    update();
}

void FluentCard::setInteractive(bool interactive)
{
    if (m_interactive == interactive)
        return;
    m_interactive = interactive;
    setAttribute(Qt::WA_Hover, interactive);
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void FluentCard::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void FluentCard::setAccentTint(const QColor &color)
{
    m_tint = color;
    update();
}

void FluentCard::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    if (!m_interactive)
        return;
    m_hovered = true;
    update();
}

void FluentCard::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    m_hovered = false;
    update();
}

void FluentCard::mouseReleaseEvent(QMouseEvent *event)
{
    QFrame::mouseReleaseEvent(event);
    if (m_interactive && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
}

void FluentCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const FluentTheme *t = FluentTheme::instance();
    const QColor tint = m_tint.isValid() ? m_tint : t->accent();

    QColor fill;
    QColor border;
    switch (m_variant) {
    case Layer:
        fill = t->layer();
        border = t->strokeSubtle();
        break;
    case Secondary:
        fill = t->cardSecondary();
        border = t->strokeSubtle();
        break;
    case Accent:
        fill = QColor(tint.red(), tint.green(), tint.blue(), t->isDark() ? 41 : 26);
        border = QColor(tint.red(), tint.green(), tint.blue(), 115);
        break;
    case Outline:
        fill = Qt::transparent;
        border = t->stroke();
        break;
    case Card:
    default:
        fill = m_hovered ? t->cardSecondary() : t->card();
        border = t->strokeSubtle();
        break;
    }
    if (m_selected)
        border = tint;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, FluentTheme::RadiusLarge, FluentTheme::RadiusLarge);
}

// ============================================================================
//  InfoBar
// ============================================================================
InfoBar::InfoBar(QWidget *parent)
    : QFrame(parent)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(FluentTheme::spacingM(), FluentTheme::spacingM(),
                            FluentTheme::spacingM(), FluentTheme::spacingM());
    row->setSpacing(FluentTheme::spacingM());

    m_glyph = new FluentIcon(this);
    m_glyph->setIconSize(18);
    row->addWidget(m_glyph, 0, Qt::AlignTop);

    auto *text = new QVBoxLayout();
    text->setSpacing(3);
    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    QFont tf = m_title->font();
    tf.setWeight(QFont::DemiBold);
    m_title->setFont(tf);
    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setProperty("fluentRole", "caption");
    text->addWidget(m_title);
    text->addWidget(m_message);
    row->addLayout(text, 1);

    m_action = new FluentButton(this);
    m_action->setRole(FluentButton::Subtle);
    m_action->setCompact(true);
    m_action->hide();
    connect(m_action, &QPushButton::clicked, this, &InfoBar::actionTriggered);
    row->addWidget(m_action, 0, Qt::AlignTop);

    m_close = new FluentButton(this);
    m_close->setGlyph(FluentTheme::Glyph::Close);
    m_close->setIconOnly(true);
    m_close->setRole(FluentButton::Subtle);
    m_close->setCompact(true);
    connect(m_close, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });
    row->addWidget(m_close, 0, Qt::AlignTop);

    restyle();
    hide();
}

void InfoBar::setSeverity(Severity severity)
{
    if (m_severity == severity)
        return;
    m_severity = severity;
    restyle();
}

void InfoBar::setTitle(const QString &title)
{
    m_title->setText(title);
    m_title->setVisible(!title.isEmpty());
}

void InfoBar::setMessage(const QString &message)
{
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void InfoBar::setActionText(const QString &text)
{
    m_action->setText(text);
    m_action->setVisible(!text.isEmpty());
}

void InfoBar::setClosable(bool closable) { m_close->setVisible(closable); }

void InfoBar::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    QColor tint;
    QColor bg;
    QChar glyph;
    switch (m_severity) {
    case Success: tint = t->success(); bg = t->successBg(); glyph = FluentTheme::Glyph::Success; break;
    case Warning: tint = t->caution(); bg = t->cautionBg(); glyph = FluentTheme::Glyph::Warning; break;
    case Error:   tint = t->critical(); bg = t->criticalBg(); glyph = FluentTheme::Glyph::Error; break;
    case Info:
    default:      tint = t->info(); bg = t->infoBg(); glyph = FluentTheme::Glyph::Info; break;
    }
    m_glyph->setGlyph(glyph);
    m_glyph->setIconColor(tint);
    // Scope to this class so the rule does not cascade onto the icon child and
    // strip the colour that draws the glyph.
    setStyleSheet(QStringLiteral("InfoBar { background: %1; border: 1px solid %2; border-radius: %3px; }")
                      .arg(bg.name(QColor::HexRgb),
                           QColor(tint.red(), tint.green(), tint.blue(), 90).name(QColor::HexArgb))
                      .arg(FluentTheme::RadiusLarge));
}

// ============================================================================
//  StatCard
// ============================================================================
StatCard::StatCard(QWidget *parent)
    : QFrame(parent)
    , m_tint(FluentTheme::instance()->accent())
{
    setMinimumHeight(92);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(FluentTheme::spacingL(), FluentTheme::spacingM(),
                            FluentTheme::spacingL(), FluentTheme::spacingM());
    row->setSpacing(14);

    m_glyph = new FluentIcon(this);
    m_glyph->setIconSize(18);
    m_glyph->setFixedSize(38, 38);
    row->addWidget(m_glyph, 0, Qt::AlignVCenter);

    auto *col = new QVBoxLayout();
    col->setSpacing(1);
    m_value = new QLabel(this);
    m_value->setProperty("fluentRole", "stat");
    m_label = new QLabel(this);
    m_label->setProperty("fluentRole", "caption");
    m_secondary = new QLabel(this);
    m_secondary->setProperty("fluentRole", "statHint");
    col->addWidget(m_value);
    col->addWidget(m_label);
    col->addWidget(m_secondary);
    row->addLayout(col, 1);

    m_bar = new FluentProgressBar(this);
    m_bar->setBarHeight(3);
    m_bar->hide();

    restyle();
}

void StatCard::setGlyph(const QChar &glyph) { m_glyph->setGlyph(glyph); }
void StatCard::setLabel(const QString &label) { m_label->setText(label); }
void StatCard::setValue(const QString &value) { m_value->setText(value); }
void StatCard::setSecondary(const QString &secondary)
{
    m_secondary->setText(secondary);
    m_secondary->setVisible(!secondary.isEmpty());
}
void StatCard::setTint(const QColor &tint)
{
    m_tint = tint;
    restyle();
}

void StatCard::setProgress(double progress)
{
    m_progress = progress;
    m_bar->setValue(qBound(0.0, progress, 100.0));
    m_bar->setVisible(progress >= 0);
}

void StatCard::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    // Scope the plate rule to this class only. An unscoped rule would cascade
    // onto the FluentIcon child and replace its colour-only sheet, which makes
    // the glyph vanish (the child paints its glyph with QPainter, but the
    // inherited sheet also recolours/re-lays-out the label).
    m_glyph->setIconColor(m_tint);
    m_glyph->setStyleSheet(
        QStringLiteral("QLabel { background: %1; border-radius: %2px; color: %3; }")
            .arg(QColor(m_tint.red(), m_tint.green(), m_tint.blue(), t->isDark() ? 56 : 36)
                     .name(QColor::HexArgb))
            .arg(FluentTheme::RadiusMedium)
            .arg(m_tint.name(QColor::HexRgb)));
    m_bar->setBarColor(m_tint);
    setStyleSheet(QStringLiteral("StatCard { background: %1; border: 1px solid %2; border-radius: %3px; }")
                      .arg(t->card().name(QColor::HexRgb), t->strokeSubtle().name(QColor::HexRgb))
                      .arg(FluentTheme::RadiusLarge));
}

// ============================================================================
//  FluentProgressBar
// ============================================================================
FluentProgressBar::FluentProgressBar(QWidget *parent)
    : QWidget(parent)
    , m_color(FluentTheme::instance()->accent())
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_barHeight);
}

void FluentProgressBar::setValue(double value)
{
    value = qBound(0.0, value, 100.0);
    if (qFuzzyCompare(m_value, value))
        return;
    m_value = value;
    update();
}

void FluentProgressBar::setIndeterminate(bool indeterminate)
{
    if (m_indeterminate == indeterminate)
        return;
    m_indeterminate = indeterminate;
    updateTimer();
    update();
}

void FluentProgressBar::setBarColor(const QColor &color)
{
    m_color = color;
    update();
}

void FluentProgressBar::setShowSegments(bool show)
{
    m_segments = show;
    update();
}

void FluentProgressBar::setBarHeight(int height)
{
    m_barHeight = height;
    setFixedHeight(height);
    update();
}

void FluentProgressBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateTimer();
}

void FluentProgressBar::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateTimer();
}

void FluentProgressBar::updateTimer()
{
    const bool want = m_indeterminate && isVisible();
    if (want && m_timerId == 0) {
        m_timerId = startTimer(33);
    } else if (!want && m_timerId != 0) {
        killTimer(m_timerId);
        m_timerId = 0;
    }
}

void FluentProgressBar::timerEvent(QTimerEvent *event)
{
    if (event->timerId() != m_timerId) {
        QWidget::timerEvent(event);
        return;
    }
    m_phase = (m_phase + 2) % 140;
    update();
}

void FluentProgressBar::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal h = height();
    const qreal radius = h / 2.0;
    const QRectF track(0, 0, width(), h);

    QPainterPath clip;
    clip.addRoundedRect(track, radius, radius);
    p.setClipPath(clip);

    p.fillRect(track, t->trackFill());

    if (m_indeterminate) {
        // A soft sweep, the standard ProgressBar busy indicator.
        const qreal sweepWidth = width() * 0.35;
        const qreal x = (width() + sweepWidth) * (m_phase / 140.0) - sweepWidth;
        QLinearGradient g(x, 0, x + sweepWidth, 0);
        QColor c = m_color;
        c.setAlpha(0);
        QColor mid = m_color;
        g.setColorAt(0.0, c);
        g.setColorAt(0.5, mid);
        g.setColorAt(1.0, c);
        p.fillRect(QRectF(x, 0, sweepWidth, h), g);
    } else {
        const qreal w = track.width() * (m_value / 100.0);
        if (w > 0)
            p.fillRect(QRectF(0, 0, w, h), m_color);

        if (m_segments && w > 0) {
            // Thin gaps every 10% so a multi-file torrent reads as pieces.
            p.setPen(QPen(t->background(), 1.5));
            for (int i = 1; i < 10; ++i) {
                const qreal x = track.width() * i / 10.0;
                p.drawLine(QPointF(x, 0), QPointF(x, h));
            }
        }
    }
}

// ============================================================================
//  ToastHost
// ============================================================================
namespace {

class Toast : public QFrame
{
public:
    Toast(const QString &text, ToastHost::Severity severity, QWidget *parent)
        : QFrame(parent)
    {
        const FluentTheme *t = FluentTheme::instance();
        QColor tint;
        QChar glyph;
        switch (severity) {
        case ToastHost::Success: tint = t->success(); glyph = FluentTheme::Glyph::Success; break;
        case ToastHost::Warning: tint = t->caution(); glyph = FluentTheme::Glyph::Warning; break;
        case ToastHost::Error:   tint = t->critical(); glyph = FluentTheme::Glyph::Error; break;
        default:                 tint = t->info(); glyph = FluentTheme::Glyph::Info; break;
        }

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(15, 12, 8, 12);
        row->setSpacing(10);

        auto *icon = new FluentIcon(glyph, 16, this);
        icon->setObjectName(QStringLiteral("toastGlyph"));
        icon->setIconColor(tint);
        row->addWidget(icon, 0, Qt::AlignTop);

        auto *label = new QLabel(text, this);
        label->setWordWrap(true);
        label->setMaximumWidth(320);
        label->setProperty("fluentRole", "caption");
        row->addWidget(label, 1);

        setStyleSheet(QStringLiteral(
                          "Toast { background: %1; border: 1px solid %2; border-radius: %3px; }")
                          .arg(t->isDark() ? t->cardTertiary().name() : t->card().name(),
                               QColor(tint.red(), tint.green(), tint.blue(), 130).name(QColor::HexArgb))
                          .arg(FluentTheme::RadiusLarge));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
};

} // namespace

ToastHost::ToastHost(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_stack = new QVBoxLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setSpacing(10);
    m_stack->addStretch(1);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // The host is not in its parent's layout, so it has to track the parent's
    // geometry itself or the stack would stay at a stale position after a
    // window resize.
    if (QWidget *p = parentWidget())
        p->installEventFilter(this);
}

bool ToastHost::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void ToastHost::push(const QString &text, Severity severity, int durationMs)
{
    if (text.isEmpty() || !parentWidget())
        return;

    auto *toast = new Toast(text, severity, this);
    toast->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    // Insert above the trailing stretch so toasts stack downwards from the top
    // of the host, and the host itself stays bottom-right anchored.
    m_stack->insertWidget(m_stack->count() - 1, toast, 0, Qt::AlignRight);
    toast->show();

    // Keep at most m_maxVisible toasts alive.
    while (m_stack->count() - 1 > m_maxVisible) {
        QLayoutItem *item = m_stack->itemAt(0);
        if (!item || !item->widget())
            break;
        QWidget *oldest = item->widget();
        m_stack->removeWidget(oldest);
        oldest->deleteLater();
    }

    QTimer::singleShot(durationMs, toast, [this, toast]() { removeToast(toast); });
    relayout();
}

void ToastHost::removeToast(QWidget *toast)
{
    if (!toast)
        return;
    m_stack->removeWidget(toast);
    toast->deleteLater();
    relayout();
}

void ToastHost::relayout()
{
    if (QWidget *p = parentWidget()) {
        // Occupy the bottom-right corner, leaving room for the margins.
        const int w = qMin(400, qMax(260, p->width() - 36));
        setGeometry(p->width() - w - 18, 18, w, qMax(0, p->height() - 36));
        raise();
    }
}
