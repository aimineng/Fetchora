#ifndef NEWTASKDIALOG_H
#define NEWTASKDIALOG_H

#include <QDialog>
#include <QString>
#include <QVariantMap>

class Aria2Manager;
class FluentButton;
class FluentCheckBox;
class FluentLineEdit;
class FluentSpinBox;
class SettingsManager;

namespace Ui {
class NewTaskDialog;
}

/**
 * NewTaskDialog - the "new download" sheet.
 *
 * Accepts one link per line (HTTP/HTTPS/FTP, magnet:, ed2k:, .torrent paths) and
 * exposes the per-task aria2 options that matter in practice: save directory,
 * split count, connection limits, speed limits, Referer / User-Agent, proxy and
 * "add paused".
 *
 * Structure and layout live in NewTaskDialog.ui. Everything the .ui format
 * cannot express - FluentButton glyphs and roles, the fields inside the
 * collapsible advanced grid - is built here and inserted into the layouts the
 * .ui file created, exactly like DownloadsPage fills ui->commandLayout.
 *
 * The dialog is a normal framed window even though the shell is frameless, and
 * it inherits the application style sheet that main.cpp sets on QApplication.
 *
 * Usage:
 *     NewTaskDialog dialog(&aria2, &window);
 *     dialog.exec();
 */
class NewTaskDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewTaskDialog(Aria2Manager *aria2, QWidget *parent = nullptr);
    ~NewTaskDialog() override;

protected:
    void changeEvent(QEvent *event) override;

private:
    /**
     * The values the sheet opened with. A field is sent to aria2 only when the
     * user moved it away from one of these, so an untouched sheet never
     * overrides the global configuration. The numbers are snapshotted *after*
     * clamping into the spin box ranges, because a global value outside the
     * range (say split = 32) must not be rewritten as "changed".
     */
    struct Defaults
    {
        int maxConcurrentDownloads = 0;
        int split = 0;
        int maxConnectionPerServer = 0;
        int maxDownloadLimitKb = 0;
        int maxUploadLimitKb = 0;
        QString referer;
        QString userAgent;
        QString allProxy;
    };

    void resolveSettings();
    void buildHeader();
    void buildUrlBox();
    void buildSaveRow();
    void buildAdvancedSection();
    void buildFooter();

    void loadFromSettings();
    void wireSettings();

    void retranslate();
    void restyle();
    /// Non-resizable 640 wide sheet: 560 tall while the advanced section is
    /// closed, taller while it is open so nothing is ever squeezed.
    void updateCompactSize();
    void updateSubmitState();

    QString normalizedText() const;
    QVariantMap buildOptions(bool startPaused) const;
    void submit(bool startPaused);

    void pasteFromClipboard();
    void pickTorrentFile();
    void browseSaveDir();
    void toggleAdvanced();

    void persistInt(const QString &key, int value);
    void persistString(const QString &key, const QString &value);
    void persistLimit(const QString &key, int kbPerSecond);

    Ui::NewTaskDialog *ui = nullptr;
    Aria2Manager *m_aria2 = nullptr;
    SettingsManager *m_settings = nullptr;
    Defaults m_defaults;

    FluentButton *m_closeButton = nullptr;
    FluentButton *m_pasteButton = nullptr;
    FluentButton *m_torrentButton = nullptr;
    FluentButton *m_browseButton = nullptr;
    FluentButton *m_advancedButton = nullptr;
    FluentButton *m_cancelButton = nullptr;
    FluentButton *m_laterButton = nullptr;
    FluentButton *m_submitButton = nullptr;

    FluentLineEdit *m_saveDirEdit = nullptr;
    FluentSpinBox *m_maxConcurrentSpin = nullptr;
    FluentSpinBox *m_splitSpin = nullptr;
    FluentSpinBox *m_perServerSpin = nullptr;
    FluentSpinBox *m_downloadLimitSpin = nullptr;
    FluentSpinBox *m_uploadLimitSpin = nullptr;
    FluentLineEdit *m_refererEdit = nullptr;
    FluentLineEdit *m_userAgentEdit = nullptr;
    FluentLineEdit *m_proxyEdit = nullptr;
    FluentCheckBox *m_pauseCheck = nullptr;

    bool m_advancedOpen = false;
    /// Guards the one aria2.addUri() per accept: a second click on the footer
    /// buttons must not queue the same links twice.
    bool m_submitted = false;
};

#endif // NEWTASKDIALOG_H
