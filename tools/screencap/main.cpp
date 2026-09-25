/*
    screencap - development helper: grab a screen region to a PNG.

    Used while building the UI to check what the application actually renders,
    which is far more reliable than guessing from code.

    Usage: screencap <out.png> [delayMs] [x y w h]
*/
#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QDebug>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : QDir::currentPath() + "/screen.png";
    const int delay = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 1500;

    QTimer::singleShot(delay, &app, [&]() {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) {
            qWarning() << "no primary screen";
            QCoreApplication::exit(2);
            return;
        }
        QPixmap shot;
        if (argc >= 7) {
            const int x = QString::fromLocal8Bit(argv[3]).toInt();
            const int y = QString::fromLocal8Bit(argv[4]).toInt();
            const int w = QString::fromLocal8Bit(argv[5]).toInt();
            const int h = QString::fromLocal8Bit(argv[6]).toInt();
            shot = screen->grabWindow(0, x, y, w, h);
        } else {
            shot = screen->grabWindow(0);
        }
        qInfo() << "saving" << out << shot.size();
        QCoreApplication::exit(shot.save(out) ? 0 : 1);
    });
    return app.exec();
}
