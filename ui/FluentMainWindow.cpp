#include "ui/FluentMainWindow.h"

#include "ui/FluentTheme.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QPainter>
#include <QShowEvent>
#include <QWindow>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#  include <dwmapi.h>
#  include <windowsx.h>
#  include <string>

#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#  ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#    define DWMWA_WINDOW_CORNER_PREFERENCE 33
#  endif
#  ifndef DWMWA_SYSTEMBACKDROP_TYPE
#    define DWMWA_SYSTEMBACKDROP_TYPE 38
#  endif
#  ifndef DWMWCP_ROUND
#    define DWMWCP_ROUND 2
#  endif
#  ifndef DWMSBT_AUTO
#    define DWMSBT_AUTO 0
#    define DWMSBT_NONE 1
#    define DWMSBT_MAINWINDOW 2
#    define DWMSBT_TRANSIENTWINDOW 3
#    define DWMSBT_TABBEDWINDOW 4
#  endif

namespace {
using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

DwmSetWindowAttributeFn dwmSetAttribute()
{
    static DwmSetWindowAttributeFn fn = []() -> DwmSetWindowAttributeFn {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (!dwm)
            return nullptr;
        return reinterpret_cast<DwmSetWindowAttributeFn>(
            GetProcAddress(dwm, "DwmSetWindowAttribute"));
    }();
    return fn;
}
} // namespace
#endif

namespace {

/// Whether the window is drawn without a native frame. Windows only: there we
/// cover the frame with our own caption bar and keep the native resize borders
/// through WM_NCHITTEST. macOS and Linux get a normal window with a real title
/// bar, so the window can always be moved, maximised and closed by the OS even
/// if the custom chrome misbehaves.
#ifdef Q_OS_WIN
constexpr bool kCustomChrome = true;
#else
constexpr bool kCustomChrome = false;
#endif

} // namespace

bool FluentMainWindow::usesCustomChrome()
{
    return kCustomChrome;
}

FluentMainWindow::FluentMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Frameless + translucent only where we can actually implement it. Asking
    // for Qt::FramelessWindowHint on X11/Wayland or AppKit would leave the user
    // with no title bar at all: moving and closing the window would depend
    // entirely on the in-window chrome.
    if (kCustomChrome) {
        setWindowFlag(Qt::FramelessWindowHint, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    } else {
        setWindowFlag(Qt::FramelessWindowHint, false);
        setAttribute(Qt::WA_TranslucentBackground, false);
    }
    setMinimumSize(940, 620);
    setAutoFillBackground(false);
}

FluentMainWindow::~FluentMainWindow() = default;

void FluentMainWindow::setBackdrop(Backdrop backdrop)
{
    if (m_backdrop == backdrop)
        return;
    m_backdrop = backdrop;
    // A backdrop is what makes the window translucent: with BackdropNone there
    // is nothing behind it to show through, and leaving WA_TranslucentBackground
    // on would composite the whole UI over the desktop and wash every colour
    // out (the page surface ends up lighter than the cards on top of it).
    m_translucent = (backdrop != BackdropNone);
    refreshTranslucency();
    applyBackdrop();
    update();
}

// The requested translucency is only honoured where a backdrop can be painted
// behind the window. Everywhere else the window is opaque: there is no Mica off
// Windows, and an opaque surface is what keeps the design tokens readable.
void FluentMainWindow::refreshTranslucency()
{
    setAttribute(Qt::WA_TranslucentBackground, m_translucent && kCustomChrome);
}

void FluentMainWindow::setCaptionHeight(int height)
{
    m_captionHeight = qBound(24, height, 96);
}

void FluentMainWindow::setTranslucentBackground(bool translucent)
{
    if (m_translucent == translucent)
        return;
    m_translucent = translucent;
    refreshTranslucency();
    applyBackdrop();
}

// ---------------------------------------------------------------- natives
void FluentMainWindow::applyBackdrop()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    DwmSetWindowAttributeFn set = dwmSetAttribute();
    if (!set)
        return;

    int type = DWMSBT_AUTO;
    switch (m_backdrop) {
    case BackdropNone: type = DWMSBT_NONE; break;
    case BackdropAcrylic: type = DWMSBT_TRANSIENTWINDOW; break;
    case BackdropMicaAlt: type = DWMSBT_TABBEDWINDOW; break;
    case BackdropMica:
    default: type = DWMSBT_MAINWINDOW; break;
    }
    set(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));

    const DWORD corner = DWMWCP_ROUND;
    set(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
#else
    Q_UNUSED(m_backdrop)
#endif
}

void FluentMainWindow::applyDarkMode()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    BOOL dark = FluentTheme::instance()->isDark() ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
#endif
}

void FluentMainWindow::applyWindowIcon()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;

    // The icon is already embedded by version.rc, so read it back instead of
    // shipping the image a second time in source form.
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const std::wstring native = exe.toStdWString();

    HICON large = nullptr;
    HICON small = nullptr;
    const UINT n = ExtractIconExW(native.c_str(), 0, &large, &small, 1);
    if (n == 0 || (!large && !small)) {
        large = LoadIconW(nullptr, IDI_APPLICATION);
        small = large;
    }
    if (large) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(large));
    }
    if (small) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(small));
    }
#endif
}

void FluentMainWindow::refreshNativeEffects()
{
    applyDarkMode();
    applyBackdrop();
    applyWindowIcon();
}

void FluentMainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;

    // Keep the native frame (shadow, snapping, animation) but let our content
    // cover the whole client area.
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    // Ask DWM to keep the frame but drop the non-client painting.
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

    refreshNativeEffects();
#endif
}

void FluentMainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange || event->type() == QEvent::ActivationChange)
        refreshNativeEffects();
}

void FluentMainWindow::forceClose()
{
    m_forceClose = true;
    close();
}

void FluentMainWindow::closeEvent(QCloseEvent *event)
{
    if (m_forceClose) {
        event->accept();
        return;
    }
    // Let the owner decide (minimise to tray vs. really quit). It calls
    // forceClose() when the window should actually go away.
    emit closeRequested();
    event->ignore();
}

void FluentMainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QPainter p(this);
    if (m_translucent) {
        // A translucent wash so a Mica backdrop tints rather than washes out the
        // content drawn on top of it.
        p.fillRect(rect(), FluentTheme::instance()->acrylicTint());
    } else {
        // No backdrop: paint the real surface colour, otherwise the frameless
        // window would have no background at all (autoFillBackground is off).
        p.fillRect(rect(), FluentTheme::instance()->background());
    }
}

void FluentMainWindow::startSystemMove()
{
#ifdef Q_OS_WIN
    if (isMaximized()) {
        // Dragging a maximized window restores it first, like WinUI does.
        const QPoint global = QCursor::pos();
        const qreal ratio = qreal(global.x() - x()) / qreal(qMax(1, width()));
        showNormal();
        move(int(global.x() - ratio * width()), int(global.y() - m_captionHeight / 2));
    }
    ReleaseCapture();
    SendMessageW(reinterpret_cast<HWND>(winId()), WM_NCLBUTTONDOWN, HTCAPTION, 0);
#else
    // Qt's own move loop. windowHandle() is null until the widget has a
    // QWindow, which is why the null check is here - the title bar only calls
    // this from a mouse event, but a programmatic call must not crash either.
    if (QWindow *handle = windowHandle())
        handle->startSystemMove();
#endif
}

void FluentMainWindow::toggleMaximized()
{
    // No native call needed on any platform: Qt maps showMaximized() onto the
    // right window manager operation, including macOS's zoom button behaviour.
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

bool FluentMainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        switch (msg->message) {
        case WM_NCCALCSIZE:
            if (!msg->wParam)
                break;
            // Client area covers the frame; resize edges are handled in
            // WM_NCHITTEST so Windows still gives us the native borders.
            *result = 0;
            return true;

        case WM_NCHITTEST: {
            const POINT pt{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
            RECT rc;
            GetWindowRect(msg->hwnd, &rc);
            const int x = pt.x - rc.left;
            const int y = pt.y - rc.top;
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;

            if (isMaximized()) {
                // CRITICAL: everything is client area while maximized.
                // Returning HTCAPTION here makes Windows swallow every click
                // into a non-client drag loop and the whole UI stops
                // responding. Moving is done by the title bar via
                // startSystemMove() instead.
                *result = HTCLIENT;
                return true;
            }

            const int b = m_borderWidth;
            if (x < b && y < b) { *result = HTTOPLEFT; return true; }
            if (x > w - b && y < b) { *result = HTTOPRIGHT; return true; }
            if (x < b && y > h - b) { *result = HTBOTTOMLEFT; return true; }
            if (x > w - b && y > h - b) { *result = HTBOTTOMRIGHT; return true; }
            if (y < b) { *result = HTTOP; return true; }
            if (y > h - b) { *result = HTBOTTOM; return true; }
            if (x < b) { *result = HTLEFT; return true; }
            if (x > w - b) { *result = HTRIGHT; return true; }
            *result = HTCLIENT;
            return true;
        }

        case WM_GETMINMAXINFO: {
            auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
            mmi->ptMinTrackSize.x = LONG(minimumWidth());
            mmi->ptMinTrackSize.y = LONG(minimumHeight());
            MONITORINFO mi{};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                // Maximize into the work area, not over the taskbar.
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                mmi->ptMaxTrackSize = {mmi->ptMaxSize.x, mmi->ptMaxSize.y};
            }
            *result = 0;
            return true;
        }
        default:
            break;
        }
    }
#else
    // Not Windows: there is no Win32 message to intercept. Qt hands us only
    // "generic" platform messages (XCB/Wayland/AppKit), which the base class
    // already deals with - so return false immediately and let it do so.
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
