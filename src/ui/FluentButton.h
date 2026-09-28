#ifndef FLUENTBUTTON_H
#define FLUENTBUTTON_H

#include <QPushButton>

#include "ui/FluentTheme.h"

/**
 * FluentButton - WinUI 3 button.
 *
 * Roles: Accent | Standard | Subtle | Outline | Hyperlink | Danger
 *
 * The visual states come from the application style sheet (so a theme switch
 * restyles every button at once); this class only carries the glyph, the icon
 * size and the geometry rules.
 */
class FluentButton : public QPushButton
{
    Q_OBJECT
public:
    enum Role { Accent, Standard, Subtle, Outline, Hyperlink, Danger };

    explicit FluentButton(QWidget *parent = nullptr);
    FluentButton(const QString &text, QWidget *parent = nullptr);

    /// Overridden so a caption change re-measures the button (see updateGeometry()).
    void setText(const QString &text);

    void setRole(Role role);
    Role role() const { return m_role; }

    /// Leading Segoe Fluent Icons glyph. Pass a null QChar to clear it.
    void setGlyph(const QChar &glyph);
    QChar glyph() const { return m_glyph; }

    /// Square, text-less button (used for command-bar and row actions).
    void setIconOnly(bool iconOnly);
    bool isIconOnly() const { return m_iconOnly; }

    /// Compact buttons are used inside list rows and command bars.
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }

    void setTooltipText(const QString &text);

    /// Swaps in a busy indicator; the text stays but the button is disabled.
    void setLoading(bool loading);

protected:
    void changeEvent(QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyRole();
    void updateGeometry();
    void updateGlyphColor();

    Role m_role = Standard;
    QChar m_glyph;
    bool m_iconOnly = false;
    bool m_compact = false;
    bool m_loading = false;
    /// True while the focus ring should be painted: keyboard focus only, see
    /// focusInEvent().
    bool m_focusVisible = false;
};

#endif // FLUENTBUTTON_H
