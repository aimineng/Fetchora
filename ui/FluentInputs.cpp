#include "ui/FluentInputs.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QCoreApplication>
#include <QFile>
#include <QStyle>
#include <QTextStream>
#include <QStyleOption>

namespace {

/// Vertical space reserved for the optional header/description rows.
int headerHeight(const QString &header, const QString &description)
{
    int h = 0;
    if (!header.isEmpty())
        h += 20;
    if (!description.isEmpty())
        h += 17;
    return h;
}

FluentLineEdit *asLineEdit(QWidget *w) { return qobject_cast<FluentLineEdit *>(w); }

} // namespace

// ============================================================================
//  FluentLineEdit
// ============================================================================
FluentLineEdit::FluentLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFont(FluentTheme::uiFont(14));
    setFrame(false);
    // The frame is painted by us; keep the platform style from drawing another.
    setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; }"));
}

void FluentLineEdit::setHeader(const QString &header)
{
    m_header = header;
    updatePadding();
    updateGeometry();
    update();
}

void FluentLineEdit::setDescription(const QString &description)
{
    m_description = description;
    updatePadding();
    updateGeometry();
    update();
}

void FluentLineEdit::updatePadding()
{
    /*
        The header and the description are painted by paintEvent() in the space
        above the field, but QLineEdit paints its own text centred in the WHOLE
        widget - so without this the caption and the value land on the same
        baseline. Style-sheet padding is the one hook QStyleSheetStyle honours
        when it computes the contents rect, so the field is pushed down by
        exactly the space the caption occupies.
    */
    setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none;"
                                 " padding-top: %1px; }")
                      .arg(headerHeight(m_header, m_description)));
}

void FluentLineEdit::setFieldGlyph(const QChar &glyph)
{
    m_glyph = glyph;
    const int pad = glyph.isNull() ? 10 : 34;
    setTextMargins(pad, 0, m_reveal ? 34 : 10, 0);
    update();
}

void FluentLineEdit::setMonospace(bool monospace)
{
    m_monospace = monospace;
    setFont(monospace ? FluentTheme::uiFont(12) : FluentTheme::uiFont(14));
    if (monospace) {
        QFont f = font();
        f.setFamily(FluentTheme::monoFont());
        setFont(f);
    }
}

void FluentLineEdit::setRevealButton(bool reveal)
{
    m_reveal = reveal;
    if (reveal)
        setEchoMode(QLineEdit::Password);
    setTextMargins(m_glyph.isNull() ? 10 : 34, 0, reveal ? 34 : 10, 0);
    update();
}

QSize FluentLineEdit::sizeHint() const
{
    QSize s = QLineEdit::sizeHint();
    s.setHeight(FluentTheme::controlHeight() + headerHeight(m_header, m_description));
    if (s.width() < 200)
        s.setWidth(200);
    return s;
}

QSize FluentLineEdit::minimumSizeHint() const
{
    // The header and the description are painted by us, so the widget must not
    // be allowed to shrink below the height they need. Without this a layout is
    // free to squeeze the field and the caption ends up drawn on top of it.
    QSize s = QLineEdit::minimumSizeHint();
    s.setHeight(FluentTheme::controlHeight() + headerHeight(m_header, m_description));
    return s;
}

QRect FluentLineEdit::fieldRect() const
{
    return QRect(0, height() - FluentTheme::controlHeight(), width(), FluentTheme::controlHeight());
}

QRect FluentLineEdit::glyphRect() const
{
    const QRect f = fieldRect();
    return QRect(f.left() + 9, f.top(), 20, f.height());
}

QRect FluentLineEdit::revealRect() const
{
    const QRect f = fieldRect();
    return QRect(f.right() - 30, f.top(), 24, f.height());
}

void FluentLineEdit::paintEvent(QPaintEvent *event)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ---- header / description -------------------------------------------
    int y = 0;
    if (!m_header.isEmpty()) {
        p.setFont(FluentTheme::uiFont(12));
        p.setPen(t->textSecondary());
        p.drawText(QRect(1, y, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, m_header);
        y += 20;
    }
    if (!m_description.isEmpty()) {
        p.setFont(FluentTheme::uiFont(11));
        p.setPen(t->textTertiary());
        p.drawText(QRect(1, y, width(), 16), Qt::AlignLeft | Qt::AlignVCenter, m_description);
    }

    // ---- field -----------------------------------------------------------
    const QRect f = fieldRect();
    const QRectF r = QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor fill = isEnabled() ? t->controlFill() : t->backgroundAlt();
    if (isEnabled() && m_hovered && !hasFocus())
        fill = t->controlFillHover();

    p.setPen(QPen(hasFocus() ? t->controlStroke() : t->strokeSubtle(), 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, FluentTheme::RadiusSmall, FluentTheme::RadiusSmall);

    // Accent underline: 1px normally, 2px while focused.
    const bool focus = hasFocus();
    const QColor line = focus ? t->accent() : (m_hovered ? t->textTertiary() : t->stroke());
    p.setPen(Qt::NoPen);
    p.setBrush(line);
    p.drawRoundedRect(QRectF(r.left() + 1, r.bottom() - (focus ? 1.5 : 0.5),
                             r.width() - 2, focus ? 2.0 : 1.0),
                      1, 1);

    // ---- leading glyph ---------------------------------------------------
    if (!m_glyph.isNull()) {
        p.setFont(FluentTheme::iconFont(15));
        p.setPen(focus ? t->accent() : t->textTertiary());
        p.drawText(glyphRect(), Qt::AlignCenter, QString(m_glyph));
    }

    // ---- reveal toggle ---------------------------------------------------
    if (m_reveal) {
        const bool hidden = echoMode() == QLineEdit::Password;
        p.setFont(FluentTheme::iconFont(15));
        p.setPen(m_revealHovered ? t->textPrimary() : t->textTertiary());
        p.drawText(revealRect(), Qt::AlignCenter,
                   QString(hidden ? FluentTheme::Glyph::Reveal : FluentTheme::Glyph::Hide));
    }

    // Let QLineEdit draw only the text itself.
    //
    // Our painter has to be finished first: a paint device can only be painted by
    // one QPainter at a time, so the base class's own painter would fail to start
    // and - worse - the style-sheet path inside it replaces the device's paint
    // engine, leaving our painter pointing at the old one. The focus ring below
    // then dereferenced that stale engine and faulted inside
    // QPainter::setPen(), which is the crash this comment exists for.
    QStyleOptionFrame opt;
    opt.initFrom(this);
    opt.rect = QRect(f.left() + (m_glyph.isNull() ? 0 : 0), f.top(),
                     f.width(), f.height());
    p.end();
    QLineEdit::paintEvent(event);

    // ---- focus ring ------------------------------------------------------
    if (focus && isEnabled()) {
        QPainter ring(this);
        ring.setRenderHint(QPainter::Antialiasing);
        ring.setPen(QPen(t->textPrimary(), 2));
        ring.setBrush(Qt::NoBrush);
        ring.drawRoundedRect(r.adjusted(-2, -2, 2, 2), FluentTheme::RadiusSmall + 2,
                             FluentTheme::RadiusSmall + 2);
    }
}

void FluentLineEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    update();
}

void FluentLineEdit::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    update();
}

void FluentLineEdit::enterEvent(QEnterEvent *event)
{
    QLineEdit::enterEvent(event);
    m_hovered = true;
    m_revealHovered = m_reveal && revealRect().contains(mapFromGlobal(QCursor::pos()));
    update();
}

void FluentLineEdit::leaveEvent(QEvent *event)
{
    QLineEdit::leaveEvent(event);
    m_hovered = false;
    m_revealHovered = false;
    update();
}

void FluentLineEdit::mousePressEvent(QMouseEvent *event)
{
    if (m_reveal && revealRect().contains(event->pos())) {
        setEchoMode(echoMode() == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
        update();
        return;
    }
    QLineEdit::mousePressEvent(event);
}

void FluentLineEdit::changeEvent(QEvent *event)
{
    QLineEdit::changeEvent(event);
    if (event->type() == QEvent::EnabledChange)
        update();
}

// ============================================================================
//  FluentSpinBox
// ============================================================================
FluentSpinBox::FluentSpinBox(QWidget *parent)
    : QSpinBox(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setFont(FluentTheme::uiFont(14));
    setFrame(false);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setStyleSheet(QStringLiteral("QSpinBox { background: transparent; border: none; padding-left: 9px; }"));
}

void FluentSpinBox::setHeader(const QString &header)
{
    m_header = header;
    updatePadding();
    updateGeometry();
    update();
}

void FluentSpinBox::setDescription(const QString &description)
{
    m_description = description;
    updatePadding();
    updateGeometry();
    update();
}

void FluentSpinBox::updatePadding()
{
    setStyleSheet(QStringLiteral("QSpinBox { background: transparent; border: none;"
                                 " padding-left: 9px; padding-top: %1px; }")
                      .arg(headerHeight(m_header, m_description)));
}

QSize FluentSpinBox::sizeHint() const
{
    QSize s = QSpinBox::sizeHint();
    s.setHeight(FluentTheme::controlHeight() + headerHeight(m_header, m_description));
    s.setWidth(qMax(s.width(), 120));
    return s;
}

QSize FluentSpinBox::minimumSizeHint() const
{
    QSize s = QSpinBox::minimumSizeHint();
    s.setHeight(FluentTheme::controlHeight() + headerHeight(m_header, m_description));
    return s;
}

QRect FluentSpinBox::fieldRect() const
{
    return QRect(0, height() - FluentTheme::controlHeight(), width(), FluentTheme::controlHeight());
}

QRect FluentSpinBox::upRect() const
{
    const QRect f = fieldRect();
    return QRect(f.right() - 28, f.top() + 2, 26, (f.height() - 4) / 2);
}

QRect FluentSpinBox::downRect() const
{
    const QRect f = fieldRect();
    return QRect(f.right() - 28, f.top() + f.height() / 2 + 1, 26, (f.height() - 4) / 2);
}

FluentSpinBox::Zone FluentSpinBox::zoneAt(const QPoint &pos) const
{
    if (upRect().contains(pos))
        return Up;
    if (downRect().contains(pos))
        return Down;
    return None;
}

void FluentSpinBox::resizeEvent(QResizeEvent *event)
{
    QSpinBox::resizeEvent(event);   // QSpinBox has no text margins; the sheet
                                    // reserves room for the stepper instead.
}

void FluentSpinBox::paintEvent(QPaintEvent *event)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int y = 0;
    if (!m_header.isEmpty()) {
        p.setFont(FluentTheme::uiFont(12));
        p.setPen(t->textSecondary());
        p.drawText(QRect(1, y, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, m_header);
        y += 20;
    }
    if (!m_description.isEmpty()) {
        p.setFont(FluentTheme::uiFont(11));
        p.setPen(t->textTertiary());
        p.drawText(QRect(1, y, width(), 16), Qt::AlignLeft | Qt::AlignVCenter, m_description);
    }

    const QRect f = fieldRect();
    const QRectF r = QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(hasFocus() ? t->controlStroke() : t->strokeSubtle(), 1));
    p.setBrush(isEnabled() ? t->controlFill() : t->backgroundAlt());
    p.drawRoundedRect(r, FluentTheme::RadiusSmall, FluentTheme::RadiusSmall);

    const bool focus = hasFocus();
    p.setPen(Qt::NoPen);
    p.setBrush(focus ? t->accent() : t->stroke());
    p.drawRoundedRect(QRectF(r.left() + 1, r.bottom() - (focus ? 1.5 : 0.5),
                             r.width() - 2, focus ? 2.0 : 1.0),
                      1, 1);

    // Stepper: two stacked zones, each showing a chevron.
    auto drawZone = [&](const QRect &zone, FluentSpinBox::Zone which, QChar glyph) {
        if (m_pressed == which || m_hovered == which) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pressed == which ? t->subtlePressed() : t->subtleHover());
            p.drawRoundedRect(zone, FluentTheme::RadiusSmall, FluentTheme::RadiusSmall);
        }
        p.setFont(FluentTheme::iconFont(10));
        p.setPen(isEnabled() ? t->textSecondary() : t->textDisabled());
        p.drawText(zone, Qt::AlignCenter, QString(glyph));
    };
    drawZone(upRect(), Up, FluentTheme::Glyph::ChevronUp);
    drawZone(downRect(), Down, FluentTheme::Glyph::ChevronDown);

    // Same reason as FluentLineEdit: finish our painter before the base class
    // starts its own, otherwise the focus ring below paints through an engine the
    // style sheet has already replaced.
    p.end();
    QSpinBox::paintEvent(event);

    if (focus && isEnabled()) {
        QPainter ring(this);
        ring.setRenderHint(QPainter::Antialiasing);
        ring.setPen(QPen(t->textPrimary(), 2));
        ring.setBrush(Qt::NoBrush);
        ring.drawRoundedRect(r.adjusted(-2, -2, 2, 2), FluentTheme::RadiusSmall + 2,
                             FluentTheme::RadiusSmall + 2);
    }
}

void FluentSpinBox::mouseMoveEvent(QMouseEvent *event)
{
    const Zone z = zoneAt(event->pos());
    if (z != m_hovered) {
        m_hovered = z;
        update();
    }
    QSpinBox::mouseMoveEvent(event);
}

void FluentSpinBox::mousePressEvent(QMouseEvent *event)
{
    const Zone z = zoneAt(event->pos());
    if (z != None) {
        m_pressed = z;
        update();
        return; // swallow: the stepper is ours, not the line edit's
    }
    QSpinBox::mousePressEvent(event);
}

void FluentSpinBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed != None) {
        const Zone z = zoneAt(event->pos());
        if (z == m_pressed) {
            if (z == Up)
                stepUp();
            else
                stepDown();
        }
        m_pressed = None;
        update();
        return;
    }
    QSpinBox::mouseReleaseEvent(event);
}

void FluentSpinBox::leaveEvent(QEvent *event)
{
    m_hovered = None;
    update();
    QSpinBox::leaveEvent(event);
}

// ============================================================================
//  FluentSwitch
// ============================================================================
FluentSwitch::FluentSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);

    auto *anim = new QPropertyAnimation(this, "position", this);
    anim->setDuration(160);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(this, &QAbstractButton::toggled, this, [this, anim](bool on) {
        anim->stop();
        anim->setStartValue(m_position);
        anim->setEndValue(on ? 1.0 : 0.0);
        anim->start();
    });
}

void FluentSwitch::setHeader(const QString &header) { setText(header); }
void FluentSwitch::setDescription(const QString &) {}

void FluentSwitch::setPosition(qreal position)
{
    m_position = position;
    update();
}

QSize FluentSwitch::sizeHint() const { return QSize(40, 20); }
QSize FluentSwitch::minimumSizeHint() const { return QSize(40, 20); }

void FluentSwitch::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal w = 40, h = 20;
    const QRectF track((width() - w) / 2.0, (height() - h) / 2.0, w, h);
    const bool on = isChecked();
    const bool hovered = underMouse();

    // Track.
    QColor trackColor;
    if (!isEnabled())
        trackColor = t->isDark() ? QColor(0x2A, 0x2A, 0x2A) : QColor(0xE8, 0xE8, 0xE8);
    else if (on)
        trackColor = isDown() ? t->accentDark2() : (hovered ? t->accentLight1() : t->accent());
    else
        trackColor = isDown() ? t->controlFillPressed()
                              : (hovered ? t->controlFillHover() : t->controlFill());

    p.setPen(on || !isEnabled() ? Qt::NoPen : QPen(t->controlStroke(), 1));
    p.setBrush(trackColor);
    p.drawRoundedRect(track, h / 2.0, h / 2.0);

    // Knob.
    const qreal knob = 12;
    const qreal x = track.left() + 4 + m_position * (w - knob - 8);
    const QRectF knobRect(x, track.top() + (h - knob) / 2.0, knob, knob);
    QColor knobColor;
    if (!isEnabled())
        knobColor = t->isDark() ? QColor(0x5D, 0x5D, 0x5D) : QColor(0xB0, 0xB0, 0xB0);
    else if (on)
        knobColor = t->onAccent();
    else
        knobColor = (hovered || isDown()) ? t->textPrimary() : t->textSecondary();
    p.setPen(Qt::NoPen);
    p.setBrush(knobColor);
    p.drawEllipse(knobRect);

    if (hasFocus() && isEnabled()) {
        p.setPen(QPen(t->textPrimary(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track.adjusted(-3, -3, 3, 3), h / 2.0 + 3, h / 2.0 + 3);
    }
}

void FluentSwitch::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void FluentSwitch::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

// ============================================================================
//  FluentCheckBox
// ============================================================================
FluentCheckBox::FluentCheckBox(QWidget *parent)
    : QCheckBox(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFont(FluentTheme::uiFont(14));
    // We paint the indicator; stop the style from drawing its own.
    setStyleSheet(QStringLiteral(
        "QCheckBox { spacing: 10px; }"
        "QCheckBox::indicator { width: 0px; height: 0px; }"));
}

void FluentCheckBox::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal box = 19;
    const QRectF ind(1, (height() - box) / 2.0, box, box);
    const bool on = isChecked();
    const bool partial = checkState() == Qt::PartiallyChecked;

    QColor fill;
    QColor border;
    if (!isEnabled()) {
        fill = t->isDark() ? QColor(0x23, 0x23, 0x23) : QColor(0xF0, 0xF0, 0xF0);
        border = t->strokeSubtle();
    } else if (on || partial) {
        fill = isDown() ? t->accentDark2() : (m_hovered ? t->accentLight1() : t->accent());
        border = Qt::transparent;
    } else {
        fill = isDown() ? t->controlFillPressed()
                        : (m_hovered ? t->controlFillHover() : t->controlFill());
        border = t->controlStroke();
    }

    p.setPen(border.alpha() == 0 ? Qt::NoPen : QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(ind, FluentTheme::RadiusSmall, FluentTheme::RadiusSmall);

    if (on || partial) {
        p.setFont(FluentTheme::iconFont(12));
        p.setPen(t->onAccent());
        p.drawText(ind, Qt::AlignCenter,
                   QString(partial ? FluentTheme::Glyph::Indeterminate : FluentTheme::Glyph::Check));
    }

    if (hasFocus() && isEnabled()) {
        p.setPen(QPen(t->textPrimary(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(ind.adjusted(-3, -3, 3, 3), FluentTheme::RadiusSmall + 3,
                          FluentTheme::RadiusSmall + 3);
    }

    // Text.
    const QString label = text();
    if (!label.isEmpty()) {
        p.setFont(font());
        p.setPen(isEnabled() ? t->textPrimary() : t->textDisabled());
        const QRect textRect(int(box) + 10, 0, width() - int(box) - 10, height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void FluentCheckBox::enterEvent(QEnterEvent *event)
{
    QCheckBox::enterEvent(event);
    m_hovered = true;
    update();
}

void FluentCheckBox::leaveEvent(QEvent *event)
{
    QCheckBox::leaveEvent(event);
    m_hovered = false;
    update();
}

// ============================================================================
//  FluentRadioButton
// ============================================================================
FluentRadioButton::FluentRadioButton(QWidget *parent)
    : QRadioButton(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFont(FluentTheme::uiFont(14));
    setStyleSheet(QStringLiteral(
        "QRadioButton { spacing: 10px; }"
        "QRadioButton::indicator { width: 0px; height: 0px; }"));
}

void FluentRadioButton::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal d = 19;
    const QRectF ind(1, (height() - d) / 2.0, d, d);
    const bool on = isChecked();

    QColor fill;
    QColor border;
    if (!isEnabled()) {
        fill = t->isDark() ? QColor(0x23, 0x23, 0x23) : QColor(0xF0, 0xF0, 0xF0);
        border = t->strokeSubtle();
    } else if (on) {
        fill = isDown() ? t->accentDark2() : (m_hovered ? t->accentLight1() : t->accent());
        border = Qt::transparent;
    } else {
        fill = isDown() ? t->controlFillPressed()
                        : (m_hovered ? t->controlFillHover() : t->controlFill());
        border = t->controlStroke();
    }

    p.setPen(border.alpha() == 0 ? Qt::NoPen : QPen(border, 1));
    p.setBrush(fill);
    p.drawEllipse(ind);

    if (on) {
        const qreal inner = 8;
        p.setPen(Qt::NoPen);
        p.setBrush(t->onAccent());
        p.drawEllipse(ind.center(), inner / 2.0, inner / 2.0);
    }

    if (hasFocus() && isEnabled()) {
        p.setPen(QPen(t->textPrimary(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ind.adjusted(-3, -3, 3, 3));
    }

    const QString label = text();
    if (!label.isEmpty()) {
        p.setFont(font());
        p.setPen(isEnabled() ? t->textPrimary() : t->textDisabled());
        p.drawText(QRect(int(d) + 10, 0, width() - int(d) - 10, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void FluentRadioButton::enterEvent(QEnterEvent *event)
{
    QRadioButton::enterEvent(event);
    m_hovered = true;
    update();
}

void FluentRadioButton::leaveEvent(QEvent *event)
{
    QRadioButton::leaveEvent(event);
    m_hovered = false;
    update();
}

// ============================================================================
//  FluentSlider
// ============================================================================
FluentSlider::FluentSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    setAttribute(Qt::WA_Hover, true);
    setMinimumHeight(28);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(QStringLiteral("QSlider { background: transparent; }"));
}

void FluentSlider::setShowValue(bool show)
{
    m_showValue = show;
    update();
}

void FluentSlider::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int reserve = m_showValue ? 56 : 0;
    const int trackW = width() - reserve - 12;
    const qreal cy = height() / 2.0;
    const qreal thumbD = isSliderDown() ? 14 : 20;
    const qreal trackH = 4;

    // Track.
    const QRectF track(6, cy - trackH / 2.0, trackW, trackH);
    p.setPen(Qt::NoPen);
    p.setBrush(t->trackFill());
    p.drawRoundedRect(track, trackH / 2.0, trackH / 2.0);

    // Filled portion.
    const qreal span = qMax(1, maximum() - minimum());
    const qreal frac = qBound(0.0, qreal(value() - minimum()) / span, 1.0);
    const qreal fillW = track.width() * frac;
    if (fillW > 0) {
        p.setBrush(isEnabled() ? t->accent() : t->textDisabled());
        p.drawRoundedRect(QRectF(track.left(), track.top(), fillW, trackH),
                          trackH / 2.0, trackH / 2.0);
    }

    // Thumb.
    const qreal thumbX = track.left() + fillW;
    const QRectF thumb(thumbX - thumbD / 2.0, cy - thumbD / 2.0, thumbD, thumbD);
    p.setBrush(isEnabled() ? (isSliderDown() ? t->accentDark1() : t->accent()) : t->textDisabled());
    p.drawEllipse(thumb);
    if (!isSliderDown()) {
        const qreal inner = 10;
        p.setBrush(t->onAccent());
        p.drawEllipse(thumb.center(), inner / 2.0, inner / 2.0);
    }

    if (m_showValue) {
        p.setFont(FluentTheme::uiFont(14));
        p.setPen(t->textPrimary());
        p.drawText(QRect(width() - reserve, 0, reserve - 6, height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(value()));
    }
}

void FluentSlider::enterEvent(QEnterEvent *event)
{
    QSlider::enterEvent(event);
    m_hovered = true;
    update();
}

void FluentSlider::leaveEvent(QEvent *event)
{
    QSlider::leaveEvent(event);
    m_hovered = false;
    update();
}

// ============================================================================
//  FluentComboBox
// ============================================================================
FluentComboBox::FluentComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFont(FluentTheme::uiFont(14));

    // The popup is a styled list so the flyout matches the rest of the UI.
    auto *view = new QListView(this);
    view->setUniformItemSizes(true);
    setView(view);
}

void FluentComboBox::setHeader(const QString &header)
{
    m_header = header;
    updatePadding();
    updateGeometry();
    update();
}

void FluentComboBox::updatePadding()
{
    setStyleSheet(QStringLiteral("QComboBox { background: transparent; border: none;"
                                 " padding-top: %1px; }")
                      .arg(headerHeight(m_header, QString())));
}

void FluentComboBox::setCompact(bool compact)
{
    m_compact = compact;
    updateGeometry();
    update();
}

QSize FluentComboBox::sizeHint() const
{
    QSize s = QComboBox::sizeHint();
    const int ctrl = m_compact ? 26 : FluentTheme::controlHeight();
    s.setHeight(ctrl + headerHeight(m_header, QString()));
    s.setWidth(qMax(s.width(), 120));
    return s;
}

QSize FluentComboBox::minimumSizeHint() const
{
    const int ctrl = m_compact ? 26 : FluentTheme::controlHeight();
    QSize s = QComboBox::minimumSizeHint();
    s.setHeight(ctrl + headerHeight(m_header, QString()));
    return s;
}

void FluentComboBox::showPopup()
{
    QComboBox::showPopup();
    if (QWidget *popup = view()->window()) {
        popup->setStyleSheet(FluentTheme::instance()->applicationStyleSheet());
    }
}

void FluentComboBox::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int y = 0;
    if (!m_header.isEmpty()) {
        p.setFont(FluentTheme::uiFont(12));
        p.setPen(t->textSecondary());
        p.drawText(QRect(1, y, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, m_header);
        y += 20;
    }

    const int ctrl = m_compact ? 26 : FluentTheme::controlHeight();
    const QRect f(0, height() - ctrl, width(), ctrl);
    const QRectF r = QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor fill = isEnabled() ? t->controlFill() : t->backgroundAlt();
    if (isEnabled()) {
        if (view()->isVisible())
            fill = t->controlFillPressed();
        else if (m_hovered)
            fill = t->controlFillHover();
    }
    p.setPen(QPen(view()->isVisible() ? t->controlStroke() : t->strokeSubtle(), 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, FluentTheme::RadiusSmall, FluentTheme::RadiusSmall);

    // Current text.
    const QString label = currentText();
    p.setFont(FluentTheme::uiFont(m_compact ? 12 : 14));
    p.setPen(isEnabled() ? t->textPrimary() : t->textDisabled());
    p.drawText(QRect(f.left() + 10, f.top(), f.width() - 40, f.height()),
               Qt::AlignLeft | Qt::AlignVCenter, label);

    // Chevron.
    p.setFont(FluentTheme::iconFont(11));
    p.setPen(isEnabled() ? t->textSecondary() : t->textDisabled());
    p.drawText(QRect(f.right() - 24, f.top(), 20, f.height()), Qt::AlignCenter,
               QString(FluentTheme::Glyph::ChevronDown));
}

void FluentComboBox::enterEvent(QEnterEvent *event)
{
    QComboBox::enterEvent(event);
    m_hovered = true;
    update();
}

void FluentComboBox::leaveEvent(QEvent *event)
{
    QComboBox::leaveEvent(event);
    m_hovered = false;
    update();
}
