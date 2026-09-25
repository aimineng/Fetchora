#include "ui/pages/NewTaskDialog.h"

#include "Aria2Manager.h"
#include "SettingsManager.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/pages/ui_NewTaskDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScreen>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

/// Sheet geometry. The height is applied by updateCompactSize().
constexpr int kDialogWidth = 640;
constexpr int kDialogMinHeight = 560;
constexpr int kUrlBoxHeight = 140;
constexpr int kUrlBoxMinHeight = 96;

/// aria2 takes every per-task option as a string. A bare number for the speed
/// limits would mean bytes per second, so the unit is always spelled out
/// ("2048K"); 0 is aria2's "unrestricted".
QString limitValue(int kbPerSecond)
{
    if (kbPerSecond <= 0)
        return QStringLiteral("0");
    return QString::number(kbPerSecond) + QLatin1Char('K');
}

/// A stored limit ("1M", "512K", "2048", "") as KB/s for the spin boxes.
int limitToKb(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return 0;

    static const QRegularExpression re(QStringLiteral("^(\\d+)\\s*([kKmMgG]?)[bB]?$"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return 0;

    const qint64 amount = match.captured(1).toLongLong();
    const QString unit = match.captured(2).toUpper();
    if (unit == QLatin1String("M"))
        return int(qMin<qint64>(amount * 1024, 1048576));
    if (unit == QLatin1String("G"))
        return 1048576;
    // "K" or a bare number: the fields in this dialog are labelled KB/s.
    return int(qMin<qint64>(amount, 1048576));
}

} // namespace

NewTaskDialog::NewTaskDialog(Aria2Manager *aria2, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::NewTaskDialog)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    // The shell is frameless; this sheet keeps a normal native frame so the OS
    // can move, snap and close it. The application style sheet is set on
    // QApplication (see main.cpp), so the dialog is styled like every other
    // window without a sheet of its own.
    setWindowFlag(Qt::FramelessWindowHint, false);

    resolveSettings();

    // Roles the application sheet keys off. The text area is marked so the
    // sheet styles it as a multi-line input rather than a single-line one.
    ui->subtitleLabel->setProperty("fluentRole", "caption");
    ui->urlLabel->setProperty("fluentRole", "caption");
    ui->urlEdit->setProperty("fluentRole", "textArea");

    buildHeader();
    buildUrlBox();
    buildSaveRow();
    buildAdvancedSection();
    buildFooter();

    // Text first (it drives the painted field headers and therefore the layout),
    // then the values, then the write-back connections: pre-filling a field must
    // not count as "the user changed it".
    retranslate();
    loadFromSettings();
    wireSettings();

    ui->advancedPanel->setVisible(m_advancedOpen);
    updateSubmitState();
    updateCompactSize();
    restyle();

    connect(ui->urlEdit, &QPlainTextEdit::textChanged, this, &NewTaskDialog::updateSubmitState);
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &NewTaskDialog::restyle);
}

NewTaskDialog::~NewTaskDialog()
{
    delete ui;
}

void NewTaskDialog::resolveSettings()
{
    // Aria2Manager owns the application SettingsManager; use exactly that
    // instance so a change made here is visible to the engine and to the
    // settings page without a restart.
    if (m_aria2)
        m_settings = m_aria2->settings();
    if (!m_settings && QCoreApplication::instance())
        m_settings = QCoreApplication::instance()->findChild<SettingsManager *>();
    if (!m_settings)
        m_settings = new SettingsManager(this);
}

// ============================================================================
//  Building the widgets the .ui file cannot express
// ============================================================================

void NewTaskDialog::buildHeader()
{
    m_closeButton = new FluentButton(this);
    m_closeButton->setGlyph(FluentTheme::Glyph::Close);
    m_closeButton->setRole(FluentButton::Subtle);
    m_closeButton->setIconOnly(true);
    m_closeButton->setCompact(true);
    ui->headerActionLayout->addWidget(m_closeButton, 0, Qt::AlignVCenter);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
}

void NewTaskDialog::buildUrlBox()
{
    m_pasteButton = new FluentButton(this);
    m_pasteButton->setGlyph(FluentTheme::Glyph::Copy);
    m_pasteButton->setRole(FluentButton::Subtle);
    ui->urlButtonLayout->addWidget(m_pasteButton);

    m_torrentButton = new FluentButton(this);
    m_torrentButton->setGlyph(FluentTheme::Glyph::AddFile);
    m_torrentButton->setRole(FluentButton::Outline);
    ui->urlButtonLayout->addWidget(m_torrentButton);

    connect(m_pasteButton, &QPushButton::clicked, this, &NewTaskDialog::pasteFromClipboard);
    connect(m_torrentButton, &QPushButton::clicked, this, &NewTaskDialog::pickTorrentFile);
}

void NewTaskDialog::buildSaveRow()
{
    m_saveDirEdit = new FluentLineEdit(this);
    m_saveDirEdit->setReadOnly(true);
    ui->saveLayout->addWidget(m_saveDirEdit, 1);

    m_browseButton = new FluentButton(this);
    m_browseButton->setGlyph(FluentTheme::Glyph::Folder);
    m_browseButton->setRole(FluentButton::Standard);
    // The field paints a caption row above its box, so the button lines up with
    // the box instead of with the whole row.
    ui->saveLayout->addWidget(m_browseButton, 0, Qt::AlignBottom);

    connect(m_browseButton, &QPushButton::clicked, this, &NewTaskDialog::browseSaveDir);
}

void NewTaskDialog::buildAdvancedSection()
{
    m_advancedButton = new FluentButton(this);
    m_advancedButton->setGlyph(FluentTheme::Glyph::ChevronDown);
    m_advancedButton->setRole(FluentButton::Subtle);
    ui->advancedToggleLayout->addWidget(m_advancedButton, 0, Qt::AlignLeft);

    m_maxConcurrentSpin = new FluentSpinBox(this);
    m_maxConcurrentSpin->setRange(1, 16);

    m_splitSpin = new FluentSpinBox(this);
    m_splitSpin->setRange(1, 16);

    m_perServerSpin = new FluentSpinBox(this);
    m_perServerSpin->setRange(1, 16);

    m_downloadLimitSpin = new FluentSpinBox(this);
    m_downloadLimitSpin->setRange(0, 1048576);

    m_uploadLimitSpin = new FluentSpinBox(this);
    m_uploadLimitSpin->setRange(0, 1048576);

    m_refererEdit = new FluentLineEdit(this);
    m_userAgentEdit = new FluentLineEdit(this);
    m_proxyEdit = new FluentLineEdit(this);

    m_pauseCheck = new FluentCheckBox(this);

    ui->advancedGrid->addWidget(m_maxConcurrentSpin, 0, 0);
    ui->advancedGrid->addWidget(m_splitSpin, 0, 1);
    ui->advancedGrid->addWidget(m_perServerSpin, 1, 0);
    ui->advancedGrid->addWidget(m_downloadLimitSpin, 1, 1);
    ui->advancedGrid->addWidget(m_uploadLimitSpin, 2, 0);
    ui->advancedGrid->addWidget(m_refererEdit, 2, 1);
    ui->advancedGrid->addWidget(m_userAgentEdit, 3, 0);
    ui->advancedGrid->addWidget(m_proxyEdit, 3, 1);
    ui->advancedGrid->addWidget(m_pauseCheck, 4, 0, 1, 2);
    ui->advancedGrid->setColumnStretch(0, 1);
    ui->advancedGrid->setColumnStretch(1, 1);

    // NOTE: a 「仅下载种子中的部分文件（稍后选择）」 checkbox belongs on the last row as
    // well. The bitfield of the task that was just added can only be edited
    // once its gid and file list are known, but Aria2Manager::fetchTaskDetail()
    // is private and addFromText() does not report a gid, so there is no
    // supported way to get there from this dialog. The checkbox is therefore
    // omitted instead of inventing an API on the manager.

    connect(m_advancedButton, &QPushButton::clicked, this, &NewTaskDialog::toggleAdvanced);
}

void NewTaskDialog::buildFooter()
{
    m_cancelButton = new FluentButton(this);
    m_cancelButton->setRole(FluentButton::Standard);

    m_laterButton = new FluentButton(this);
    m_laterButton->setRole(FluentButton::Standard);

    m_submitButton = new FluentButton(this);
    m_submitButton->setGlyph(FluentTheme::Glyph::Download);
    m_submitButton->setRole(FluentButton::Accent);

    ui->footerButtonLayout->addWidget(m_cancelButton);
    ui->footerButtonLayout->addWidget(m_laterButton);
    ui->footerButtonLayout->addWidget(m_submitButton);

    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_laterButton, &QPushButton::clicked, this, [this]() { submit(true); });
    connect(m_submitButton, &QPushButton::clicked, this, [this]() { submit(false); });
}

// ============================================================================
//  Settings
// ============================================================================

void NewTaskDialog::loadFromSettings()
{
    if (!m_settings)
        return;

    // The fallbacks mirror SettingsManager::loadSettings(), because an untouched
    // key is absent from QSettings and value() would hand back an invalid
    // QVariant (which reads as 0 and would look like a user edit).
    m_saveDirEdit->setText(QDir::toNativeSeparators(m_settings->downloadDir()));

    m_maxConcurrentSpin->setValue(
        m_settings->value(QStringLiteral("maxConcurrentDownloads"), 5).toInt());
    m_splitSpin->setValue(m_settings->value(QStringLiteral("split"), 16).toInt());
    m_perServerSpin->setValue(
        m_settings->value(QStringLiteral("maxConnectionPerServer"), 16).toInt());
    m_downloadLimitSpin->setValue(limitToKb(
        m_settings->value(QStringLiteral("maxDownloadLimit")).toString()));
    m_uploadLimitSpin->setValue(limitToKb(
        m_settings->value(QStringLiteral("maxUploadLimit")).toString()));

    m_refererEdit->setText(m_settings->value(QStringLiteral("referer")).toString());
    m_userAgentEdit->setText(m_settings->value(QStringLiteral("userAgent")).toString());
    m_proxyEdit->setText(m_settings->value(QStringLiteral("allProxy")).toString());

    // Snapshot what the sheet shows now: only a real edit becomes an option.
    m_defaults.maxConcurrentDownloads = m_maxConcurrentSpin->value();
    m_defaults.split = m_splitSpin->value();
    m_defaults.maxConnectionPerServer = m_perServerSpin->value();
    m_defaults.maxDownloadLimitKb = m_downloadLimitSpin->value();
    m_defaults.maxUploadLimitKb = m_uploadLimitSpin->value();
    m_defaults.referer = m_refererEdit->text();
    m_defaults.userAgent = m_userAgentEdit->text();
    m_defaults.allProxy = m_proxyEdit->text();
}

void NewTaskDialog::wireSettings()
{
    if (!m_settings)
        return;

    // Edited values become the new defaults, so the next sheet opens the way the
    // user left this one. Connected after the pre-fill on purpose.
    connect(m_maxConcurrentSpin, &QSpinBox::valueChanged, this, [this](int value) {
        persistInt(QStringLiteral("maxConcurrentDownloads"), value);
    });
    connect(m_splitSpin, &QSpinBox::valueChanged, this, [this](int value) {
        persistInt(QStringLiteral("split"), value);
    });
    connect(m_perServerSpin, &QSpinBox::valueChanged, this, [this](int value) {
        persistInt(QStringLiteral("maxConnectionPerServer"), value);
    });
    connect(m_downloadLimitSpin, &QSpinBox::valueChanged, this, [this](int value) {
        persistLimit(QStringLiteral("maxDownloadLimit"), value);
    });
    connect(m_uploadLimitSpin, &QSpinBox::valueChanged, this, [this](int value) {
        persistLimit(QStringLiteral("maxUploadLimit"), value);
    });

    // Line edits are persisted when they lose focus, not on every keystroke.
    connect(m_refererEdit, &QLineEdit::editingFinished, this, [this]() {
        persistString(QStringLiteral("referer"), m_refererEdit->text().trimmed());
    });
    connect(m_userAgentEdit, &QLineEdit::editingFinished, this, [this]() {
        persistString(QStringLiteral("userAgent"), m_userAgentEdit->text().trimmed());
    });
    connect(m_proxyEdit, &QLineEdit::editingFinished, this, [this]() {
        persistString(QStringLiteral("allProxy"), m_proxyEdit->text().trimmed());
    });
}

void NewTaskDialog::persistInt(const QString &key, int value)
{
    if (!m_settings || m_settings->value(key).toInt() == value)
        return;
    m_settings->setValue(key, value);
}

void NewTaskDialog::persistString(const QString &key, const QString &value)
{
    if (!m_settings || m_settings->value(key).toString() == value)
        return;
    m_settings->setValue(key, value);
}

void NewTaskDialog::persistLimit(const QString &key, int kbPerSecond)
{
    if (!m_settings || limitToKb(m_settings->value(key).toString()) == kbPerSecond)
        return;
    // "" clears the key, which is how SettingsManager spells "unlimited".
    m_settings->setValue(key, kbPerSecond > 0 ? limitValue(kbPerSecond) : QString());
}

// ============================================================================
//  Text and theme
// ============================================================================

void NewTaskDialog::retranslate()
{
    // Everything the .ui file owns was just re-applied by ui->retranslateUi();
    // this rebuilds the text created in C++.
    m_closeButton->setTooltipText(tr("关闭"));

    m_pasteButton->setText(tr("从剪贴板粘贴"));
    m_pasteButton->setTooltipText(tr("粘贴剪贴板中的内容"));
    m_torrentButton->setText(tr("选择种子文件…"));
    m_torrentButton->setTooltipText(tr("打开 .torrent / .metalink 文件"));

    m_saveDirEdit->setHeader(tr("保存到"));
    m_browseButton->setText(tr("浏览…"));
    m_browseButton->setTooltipText(tr("选择保存目录"));

    m_advancedButton->setText(tr("高级选项"));
    m_advancedButton->setTooltipText(tr("展开或收起高级选项"));

    m_maxConcurrentSpin->setHeader(tr("最大同时下载数"));
    m_splitSpin->setHeader(tr("单任务连接数"));
    m_perServerSpin->setHeader(tr("每服务器连接数"));
    m_downloadLimitSpin->setHeader(tr("下载限速（KB/s）"));
    m_uploadLimitSpin->setHeader(tr("上传限速（KB/s）"));
    m_refererEdit->setHeader(tr("引用页 Referer"));
    m_userAgentEdit->setHeader(tr("用户代理 User-Agent"));
    m_proxyEdit->setHeader(tr("代理服务器"));

    m_refererEdit->setPlaceholderText(tr("留空则使用全局设置"));
    m_userAgentEdit->setPlaceholderText(tr("留空则使用全局设置"));
    m_proxyEdit->setPlaceholderText(tr("例如 http://127.0.0.1:7890"));

    m_pauseCheck->setText(tr("开始后立即暂停"));

    m_cancelButton->setText(tr("取消"));
    m_laterButton->setText(tr("稍后下载"));
    m_laterButton->setTooltipText(tr("添加任务，但保持暂停"));
    m_submitButton->setText(tr("立即下载"));
    m_submitButton->setTooltipText(tr("添加任务并立即开始"));

    // The painted headers change the field heights, so the sheet is re-fitted.
    updateCompactSize();
}

void NewTaskDialog::restyle()
{
    const FluentTheme *t = FluentTheme::instance();

    // Colours that no fluentRole rule covers are applied here. The font family
    // never is: it comes from the application sheet, which resolves it once
    // through FluentTheme.
    ui->titleLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 20px; font-weight: 600;"
                                                 " color: %1; }")
                                      .arg(t->textPrimary().name()));
    ui->subtitleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                         .arg(t->textTertiary().name()));
}

void NewTaskDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
}

// ============================================================================
//  Geometry
// ============================================================================

void NewTaskDialog::updateCompactSize()
{
    // A compact, non-resizable sheet. It is 640x560 while the advanced section
    // is closed and grows with it (the eight fields plus the checkbox need about
    // 280 px more), so the painted captions are never squeezed. If the screen is
    // too short the URL box gives the room back first.
    const QScreen *scr = screen();
    const int limit = scr ? qMax(kDialogMinHeight, scr->availableGeometry().height() - 80) : 1000;

    ui->urlEdit->setFixedHeight(kUrlBoxHeight);
    int height = layout()->sizeHint().height();
    if (height > limit) {
        const int give = qMin(kUrlBoxHeight - kUrlBoxMinHeight, height - limit);
        ui->urlEdit->setFixedHeight(kUrlBoxHeight - give);
        height = layout()->sizeHint().height();
    }

    const QSize wanted(kDialogWidth, qBound(kDialogMinHeight, height, limit));
    if (minimumSize() == wanted && maximumSize() == wanted)
        return;

    // Keep the sheet centred on the shell while it grows and shrinks.
    const QPoint centre = frameGeometry().center();
    setFixedSize(wanted);
    if (isVisible())
        move(centre - QPoint(wanted.width() / 2, wanted.height() / 2));
}

void NewTaskDialog::updateSubmitState()
{
    const bool ready = !m_submitted && !normalizedText().isEmpty();
    m_submitButton->setEnabled(ready);
    m_laterButton->setEnabled(ready);
}

// ============================================================================
//  Actions
// ============================================================================

QString NewTaskDialog::normalizedText() const
{
    const QStringList raw = ui->urlEdit->toPlainText().split(
        QRegularExpression(QStringLiteral("[\\r\\n]+")));
    QStringList lines;
    for (const QString &line : raw) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            lines << trimmed;
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantMap NewTaskDialog::buildOptions(bool startPaused) const
{
    QVariantMap options;

    // aria2 expects a string for every per-task option.
    const QString dir = m_saveDirEdit->text().trimmed();
    if (!dir.isEmpty())
        options.insert(QStringLiteral("dir"), dir);

    // Only fields the user moved away from the value the sheet opened with, so
    // an untouched dialog does not override the global configuration.
    if (m_maxConcurrentSpin->value() != m_defaults.maxConcurrentDownloads) {
        options.insert(QStringLiteral("max-concurrent-downloads"),
                       QString::number(m_maxConcurrentSpin->value()));
    }
    if (m_splitSpin->value() != m_defaults.split)
        options.insert(QStringLiteral("split"), QString::number(m_splitSpin->value()));
    if (m_perServerSpin->value() != m_defaults.maxConnectionPerServer) {
        options.insert(QStringLiteral("max-connection-per-server"),
                       QString::number(m_perServerSpin->value()));
    }
    if (m_downloadLimitSpin->value() != m_defaults.maxDownloadLimitKb) {
        options.insert(QStringLiteral("max-download-limit"),
                       limitValue(m_downloadLimitSpin->value()));
    }
    if (m_uploadLimitSpin->value() != m_defaults.maxUploadLimitKb)
        options.insert(QStringLiteral("max-upload-limit"), limitValue(m_uploadLimitSpin->value()));

    // An empty field means "use the global setting": aria2 has no useful empty
    // value for these.
    const QString referer = m_refererEdit->text().trimmed();
    if (!referer.isEmpty() && referer != m_defaults.referer)
        options.insert(QStringLiteral("referer"), referer);

    const QString userAgent = m_userAgentEdit->text().trimmed();
    if (!userAgent.isEmpty() && userAgent != m_defaults.userAgent)
        options.insert(QStringLiteral("user-agent"), userAgent);

    const QString proxy = m_proxyEdit->text().trimmed();
    if (!proxy.isEmpty() && proxy != m_defaults.allProxy)
        options.insert(QStringLiteral("all-proxy"), proxy);

    if (startPaused || m_pauseCheck->isChecked())
        options.insert(QStringLiteral("pause"), QStringLiteral("true"));

    return options;
}

void NewTaskDialog::submit(bool startPaused)
{
    if (m_submitted)
        return; // exactly one aria2.addUri() per accept

    const QString text = normalizedText();
    if (text.isEmpty())
        return;

    m_submitted = true;
    if (m_aria2)
        m_aria2->addFromText(text, buildOptions(startPaused));
    accept();
}

void NewTaskDialog::pasteFromClipboard()
{
    if (!m_aria2)
        return;

    const QString text = m_aria2->clipboardText();
    if (text.isEmpty())
        return;

    ui->urlEdit->setPlainText(text);
    ui->urlEdit->moveCursor(QTextCursor::End); // textChanged re-enables the footer
}

void NewTaskDialog::pickTorrentFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择种子文件"), QString(),
                                                      tr("种子文件 (*.torrent *.metalink)"));
    if (path.isEmpty())
        return;

    if (m_aria2)
        m_aria2->addTorrentFile(path);

    m_submitted = true; // the sheet is done: no second add from the footer
    accept();
}

void NewTaskDialog::browseSaveDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择保存目录"),
                                                          m_saveDirEdit->text());
    if (dir.isEmpty())
        return;

    const QString native = QDir::toNativeSeparators(dir);
    m_saveDirEdit->setText(native);

    // Remembered as the default save directory for the next task.
    if (m_settings)
        m_settings->setValue(QStringLiteral("downloadDir"), native);
}

void NewTaskDialog::toggleAdvanced()
{
    m_advancedOpen = !m_advancedOpen;
    ui->advancedPanel->setVisible(m_advancedOpen);
    m_advancedButton->setGlyph(m_advancedOpen ? FluentTheme::Glyph::ChevronUp
                                              : FluentTheme::Glyph::ChevronDown);
    updateCompactSize();
}
