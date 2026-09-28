#ifndef FLUENTMAINWINDOW_H
#define FLUENTMAINWINDOW_H

#include <QMainWindow>

/**
 * FluentMainWindow - main window with native Windows 11 chrome.
 *
 * On Windows the client area is extended over the whole window (WM_NCCALCSIZE)
 * so custom widgets can draw the caption bar, while the native frame is kept
 * for the drop shadow, snapping, resize borders and Win11 rounded corners. The
 * system backdrop (Mica / Mica Alt / Acrylic) is applied through
 * DwmSetWindowAttribute when the OS supports it.
 *
 * On macOS and Linux none of that exists, so the window keeps its ordinary
 * native frame: the platform title bar is what moves, maximises and closes the
 * window, and there is no translucency (a Mica backdrop is a Windows-only
 * concept, and leaving WA_TranslucentBackground on would wash the whole UI out
 * over the desktop). The in-window FluentTitleBar is still used as a header
 * row; its own caption buttons keep working through the Qt paths below.
 *
 * Two details that are easy to get wrong and are handled here explicitly:
 *   * WM_NCHITTEST must return HTCLIENT everywhere while maximized. Returning
 *     HTCAPTION (a tempting way to "keep dragging working") makes Windows treat
 *     the whole window as a caption and swallow every click.
 *   * A frameless window keeps its icon out of Alt+Tab and the taskbar unless
 *     WM_SETICON is sent explicitly.
 */
class FluentMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /// 0 = none, 1 = Mica, 2 = Acrylic, 3 = Mica Alt
    enum Backdrop { BackdropNone = 0, BackdropMica = 1, BackdropAcrylic = 2, BackdropMicaAlt = 3 };

    explicit FluentMainWindow(QWidget *parent = nullptr);
    ~FluentMainWindow() override;

    void setBackdrop(Backdrop backdrop);
    Backdrop backdrop() const { return m_backdrop; }

    /// True when the application draws its own title bar (Windows only). False
    /// means the window has a native frame, so embedded drag areas must not
    /// start a system move.
    static bool usesCustomChrome();

    void setCaptionHeight(int height);
    int captionHeight() const { return m_captionHeight; }

    /// Re-apply backdrop + dark title bar + icon. Call after showing.
    void refreshNativeEffects();

    /// Close for real, bypassing the closeRequested() veto (used by "Quit").
    void forceClose();

    /// Start a native move/resize loop (used by drag areas in the title bar).
    void startSystemMove();
    void toggleMaximized();

    /// Keep the window background translucent so a Mica backdrop shows through.
    void setTranslucentBackground(bool translucent);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

signals:
    /// The user asked to close while "close to tray" is enabled; the owner
    /// decides whether to really quit.
    void closeRequested();

private:
    /// Push m_translucent into the widget attribute. On non-Windows the window
    /// is never translucent, so this is where that decision is enforced.
    void refreshTranslucency();

    void applyBackdrop();
    void applyDarkMode();
    void applyWindowIcon();

    Backdrop m_backdrop = BackdropMica;
    int m_captionHeight = 48;
    int m_borderWidth = 8;
    bool m_translucent = true;
    bool m_forceClose = false;
};

#endif // FLUENTMAINWINDOW_H
