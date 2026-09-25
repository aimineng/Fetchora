#include "ui/FluentButton.h"

#include <QEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>

FluentButton::FluentButton(QWidget *parent)
    : QPushButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    applyRole();
    updateGeometry();
}

FluentButton::FluentButton(const QString &text, QWidget *parent)
    : FluentButton(parent)
{
    setText(text);
}

void FluentButton::setText(const QString &text)
{
    if (QPushButton::text() == text)
        return;
    QPushButton::setText(text);
    // The caption decides how wide the button has to be; see updateGeometry().
    updateGeometry();
    update();
}

void FluentButton::setRole(Role role)
{
    if (m_role == role)
        return;
    m_role = role;
    applyRole();
    update();
}

void FluentButton::setGlyph(const QChar &glyph)
{
    if (m_glyph == glyph)
        return;
    m_glyph = glyph;
    updateGeometry();
    update();
}

void FluentButton::setIconOnly(bool iconOnly)
{
    if (m_iconOnly == iconOnly)
        return;
    m_iconOnly = iconOnly;
    updateGeometry();
    update();
}

void FluentButton::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    updateGeometry();
    update();
}

void FluentButton::setTooltipText(const QString &text)
{
    setToolTip(text);
}

void FluentButton::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    setEnabled(!loading);
    update();
}

void FluentButton::focusInEvent(QFocusEvent *event)
{
    // Qt's focus reasons are the only place this is visible, and the distinction
    // is what makes the ring match every other Windows app.
    const Qt::FocusReason reason = event->reason();
    m_focusVisible = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
        || reason == Qt::ShortcutFocusReason;
    QPushButton::focusInEvent(event);
    update();
}

void FluentButton::focusOutEvent(QFocusEvent *event)
{
    m_focusVisible = false;
    QPushButton::focusOutEvent(event);
    update();
}

void FluentButton::applyRole()
{
    // The role drives the style sheet through a dynamic property, so every
    // button in the window is restyled by a single application-wide sheet.
    static const char *names[] = {"standard", "accent", "subtle", "outline", "hyperlink", "danger"};
    setProperty("fluentRole", QString::fromLatin1(names[m_role]));
    // The sheet decides fill/stroke; make sure QPushButton's own painting is off.
    setFlat(true);
    setAttribute(Qt::WA_StyledBackground, false);
    style()->unpolish(this);
    style()->polish(this);
}

void FluentButton::updateGeometry()
{
    const int base = m_compact ? 26 : FluentTheme::controlHeight();
    if (m_iconOnly) {
        setFixedSize(base, qMax(base, 26));
        // Bypass our own setText() override: it calls back into updateGeometry().
        QPushButton::setText(QString());
        return;
    }

    setMinimumHeight(base);
    setMaximumHeight(base);
    /*
        The button paints its own glyph + label, so QPushButton::sizeHint() knows
        nothing about the space they need and the layout is free to squeeze the
        caption until it is clipped ("使用系统强调色" came out as "使用系统强调").
        Measure exactly what paintEvent() draws and refuse to go below it.
    */
    const int iconSize = m_compact ? 13 : 15;
    const int textSize = m_compact ? 12 : 14;
    const QFontMetrics tfm(FluentTheme::uiFont(textSize));
    const QFontMetrics gfm(FluentTheme::iconFont(iconSize));

    const int glyphW = m_glyph.isNull() ? 0 : gfm.horizontalAdvance(m_glyph);
    const int labelW = text().isEmpty() ? 0 : tfm.horizontalAdvance(text());
    const int gap = (glyphW > 0 && labelW > 0) ? 8 : 0;
    const int padding = m_compact ? 18 : 32;   // matches the breathing room in paintEvent

    setMinimumWidth(qMax(m_compact ? 24 : 88, glyphW + gap + labelW + padding));
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

void FluentButton::updateGlyphColor() { update(); }

void FluentButton::changeEvent(QEvent *event)
{
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::StyleChange)
        update();
    // The button paints its own text and sizes itself from the font metrics, so
    // a font change (theme ramp, user font setting) has to re-run the geometry.
    else if (event->type() == QEvent::FontChange)
        updateGeometry();
}

void FluentButton::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    const bool on = isEnabled();
    const bool hovered = underMouse() && on;
    const bool pressed = isDown() && on;

    // ---------------------------------------------------------------- colours
    QColor fill;
    QColor stroke;
    QColor fg;
    switch (m_role) {
    case Accent:
        fill = !on ? QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 90)
                   : (pressed ? t->accentDark1() : (hovered ? t->accentLight1() : t->accent()));
        stroke = QColor(255, 255, 255, 26);
        fg = on ? t->onAccent() : t->textDisabled();
        break;
    case Subtle:
    case Hyperlink:
        fill = pressed ? t->subtlePressed() : (hovered ? t->subtleHover() : Qt::transparent);
        stroke = Qt::transparent;
        fg = !on ? t->textDisabled()
                 : (m_role == Hyperlink ? t->accent() : t->textPrimary());
        break;
    case Outline:
        fill = pressed ? t->controlFillPressed() : (hovered ? t->controlFillHover() : Qt::transparent);
        stroke = hovered ? t->controlStroke() : t->stroke();
        fg = on ? t->textPrimary() : t->textDisabled();
        break;
    case Danger:
        fill = pressed ? QColor(0xA5, 0x22, 0x16) : (hovered ? QColor(0xD9, 0x3B, 0x2B) : QColor(0xC4, 0x2B, 0x1C));
        stroke = Qt::transparent;
        fg = QColor(Qt::white);
        break;
    case Standard:
    default:
        fill = !on ? t->controlFill()
                   : (pressed ? t->controlFillPressed() : (hovered ? t->controlFillHover() : t->controlFill()));
        stroke = t->strokeSubtle();
        fg = on ? t->textPrimary() : t->textDisabled();
        break;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ------------------------------------------------------------- background
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int radius = m_compact ? FluentTheme::RadiusSmall : FluentTheme::RadiusMedium;
    p.setPen(stroke.alpha() == 0 ? Qt::NoPen : QPen(stroke, 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, radius, radius);

    // ------------------------------------------------------------- focus ring
    // Keyboard focus only: Windows and macOS draw a focus visual when the user
    // *tabs* to a control, not after a mouse click, and a ring that follows every
    // click on a navigation row reads as a rendering artefact.
    //
    // It is also drawn *inside* the button's own rect. It used to be expanded 2px
    // outward, which a rail row's 2px margins then clipped: all that survived of
    // the ring were two arcs at the right-hand corners.
    if (m_focusVisible && hasFocus() && on && focusPolicy() != Qt::NoFocus) {
        p.setPen(QPen(t->textPrimary(), 2));
        p.setBrush(Qt::NoBrush);
        const int ringRadius = qMax(2, radius - 1);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), ringRadius, ringRadius);
    }

    // ----------------------------------------------------------------- glyph
    const int iconSize = m_compact ? 13 : 15;
    const int textSize = m_compact ? 12 : 14;
    QFont textFont = FluentTheme::uiFont(textSize);
    QFont glyphFont = FluentTheme::iconFont(iconSize);

    const QString label = text();
    const bool hasGlyph = !m_glyph.isNull() && !m_iconOnly;

    QFontMetrics tfm(textFont);
    QFontMetrics gfm(glyphFont);
    const int glyphW = hasGlyph ? gfm.horizontalAdvance(m_glyph) : 0;
    const int labelW = label.isEmpty() || m_iconOnly ? 0 : tfm.horizontalAdvance(label);
    const int gap = (glyphW > 0 && labelW > 0) ? 8 : 0;
    const int contentW = glyphW + gap + labelW;
    const int startX = (width() - contentW) / 2;

    if (m_iconOnly && !m_glyph.isNull()) {
        p.setFont(glyphFont);
        p.setPen(fg);
        p.drawText(rect(), Qt::AlignCenter, QString(m_glyph));
    } else {
        int x = startX;
        const int baseline = (height() + tfm.capHeight()) / 2;
        if (hasGlyph) {
            p.setFont(glyphFont);
            p.setPen(fg);
            const int gy = (height() + gfm.capHeight()) / 2;
            p.drawText(x, gy, QString(m_glyph));
            x += glyphW + gap;
        }
        if (labelW > 0) {
            p.setFont(textFont);
            p.setPen(fg);
            p.drawText(x, baseline, label);
        }
    }
}
