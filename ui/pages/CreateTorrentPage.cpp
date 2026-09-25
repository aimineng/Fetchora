#include "ui/pages/CreateTorrentPage.h"

#include "TorrentUtils.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_CreateTorrentPage.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

/// Public, well known HTTP/UDP announce URLs offered by 「填入常用 Tracker」.
/// Not translatable: they are protocol data, not prose.
QStringList wellKnownTrackers()
{
    return {
        QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
        QStringLiteral("udp://open.tracker.cl:1337/announce"),
        QStringLiteral("udp://tracker.openbittorrent.com:6969/announce"),
        QStringLiteral("udp://exodus.desync.com:6969/announce"),
        QStringLiteral("udp://tracker.torrent.eu.org:451/announce"),
        QStringLiteral("https://tracker.tamersunion.org:443/announce"),
        QStringLiteral("udp://open.demonii.com:1337/announce"),
        QStringLiteral("udp://tracker.dler.org:6969/announce"),
    };
}

/// Split a free-form list (newlines, commas, semicolons) into trimmed entries.
QStringList splitList(const QString &text)
{
    static const QRegularExpression separator(QStringLiteral("[\\r\\n,;]+"));
    QStringList out;
    const QStringList parts = text.split(separator, Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            out << trimmed;
    }
    return out;
}

} // namespace

CreateTorrentPage::CreateTorrentPage(TorrentUtils *torrents, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CreateTorrentPage)
    , m_torrents(torrents)
{
    ui->setupUi(this);

    // FluentCard owns its body layout, so the section bodies declared in the
    // .ui are handed to their card here - the .ui keeps the whole structure,
    // the card only supplies its surface and padding.
    ui->formCard->body()->addWidget(ui->formBody);
    ui->previewCard->body()->addWidget(ui->previewBody);

    // Static roles: everything colour related is applied by restyle().
    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->sourceSectionLabel->setProperty("fluentRole", "subtitle");
    ui->saveSectionLabel->setProperty("fluentRole", "subtitle");
    ui->trackerSectionLabel->setProperty("fluentRole", "subtitle");
    ui->optionSectionLabel->setProperty("fluentRole", "subtitle");
    ui->trackerHint->setProperty("fluentRole", "caption");
    ui->privateHint->setProperty("fluentRole", "caption");
    ui->hiddenHint->setProperty("fluentRole", "caption");
    ui->pieceLabel->setProperty("fluentRole", "body");
    ui->commentLabel->setProperty("fluentRole", "body");
    ui->createdByLabel->setProperty("fluentRole", "body");
    ui->webSeedLabel->setProperty("fluentRole", "body");
    ui->pieceHint->setProperty("fluentRole", "caption");
    ui->progressLabel->setProperty("fluentRole", "caption");
    ui->actionHint->setProperty("fluentRole", "caption");
    ui->sourceEdit->setProperty("fluentMono", true);
    ui->saveEdit->setProperty("fluentMono", true);
    ui->trackerEdit->setProperty("fluentRole", "textArea");
    ui->trackerEdit->setProperty("fluentMono", true);
    ui->previewTitle->setProperty("fluentRole", "subtitle");
    ui->previewName->setProperty("fluentRole", "subtitle");
    ui->previewHash->setProperty("fluentRole", "mono");
    ui->previewStats->setProperty("fluentRole", "caption");
    ui->previewFilesTitle->setProperty("fluentRole", "subtitle");
    ui->previewEmpty->setProperty("fluentRole", "caption");

    buildForm();
    buildPreview();
    wireUtils();

    restyle();
    // retranslate() also rebuilds the piece-size entries and the dynamic hints.
    retranslate();
}

CreateTorrentPage::~CreateTorrentPage()
{
    delete ui;
}

void CreateTorrentPage::buildForm()
{
    auto add = [this](QHBoxLayout *layout, const QChar &glyph, const QString &text,
                      FluentButton::Role role) {
        auto *b = new FluentButton(this);
        if (!glyph.isNull())
            b->setGlyph(glyph);
        b->setText(text);
        b->setRole(role);
        layout->addWidget(b);
        return b;
    };

    m_pickFileButton = add(ui->sourceLayout, FluentTheme::Glyph::OpenFile, tr("选择文件…"),
                           FluentButton::Standard);
    m_pickFolderButton = add(ui->sourceLayout, FluentTheme::Glyph::Folder, tr("选择文件夹…"),
                             FluentButton::Standard);
    m_pickSaveButton = add(ui->saveLayout, FluentTheme::Glyph::Save, tr("另存为…"),
                           FluentButton::Standard);
    m_commonTrackersButton = add(ui->trackerActionLayout, FluentTheme::Glyph::Rocket,
                                 tr("填入常用 Tracker"), FluentButton::Subtle);
    ui->trackerActionLayout->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding,
                                                     QSizePolicy::Minimum));
    m_createButton = add(ui->actionLayout, FluentTheme::Glyph::Rocket, tr("创建种子"),
                         FluentButton::Accent);

    // Progress bar + status line, and the two info bars.
    m_progressBar = new FluentProgressBar(this);
    m_progressBar->setBarHeight(6);
    m_progressBar->setBarColor(FluentTheme::instance()->accent());
    m_progressBar->setVisible(false);
    ui->progressLayout->insertWidget(0, m_progressBar, 1);

    m_banner = new InfoBar(this);
    m_banner->setSeverity(InfoBar::Info);
    m_banner->setClosable(true);
    m_banner->hide();
    ui->formBannerLayout->addWidget(m_banner);

    connect(m_pickFileButton, &QPushButton::clicked, this, &CreateTorrentPage::pickSourceFile);
    connect(m_pickFolderButton, &QPushButton::clicked, this, &CreateTorrentPage::pickSourceFolder);
    connect(m_pickSaveButton, &QPushButton::clicked, this, &CreateTorrentPage::pickSaveTarget);
    connect(m_commonTrackersButton, &QPushButton::clicked, this,
            &CreateTorrentPage::fillCommonTrackers);
    connect(m_createButton, &QPushButton::clicked, this, &CreateTorrentPage::startCreate);
    connect(ui->pieceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updatePieceHint();
    });
    connect(ui->hiddenCheck, &QCheckBox::toggled, this, [this](bool) {
        if (!m_sourcePath.isEmpty()) {
            m_suggestedPieceLength = TorrentUtils::suggestPieceLength(sourceSize(m_sourcePath));
            updatePieceHint();
        }
    });
}

void CreateTorrentPage::buildPreview()
{
    auto *inspect = new FluentButton(this);
    inspect->setGlyph(FluentTheme::Glyph::OpenFile);
    inspect->setText(tr("查看种子…"));
    inspect->setRole(FluentButton::Standard);
    ui->previewHeaderLayout->addWidget(inspect);

    m_copyMagnetButton = new FluentButton(this);
    m_copyMagnetButton->setGlyph(FluentTheme::Glyph::Magnet);
    m_copyMagnetButton->setText(tr("复制磁力链接"));
    m_copyMagnetButton->setRole(FluentButton::Subtle);
    m_copyMagnetButton->setEnabled(false);
    ui->previewHeaderLayout->addWidget(m_copyMagnetButton);

    m_previewBanner = new InfoBar(this);
    m_previewBanner->setSeverity(InfoBar::Error);
    m_previewBanner->setClosable(true);
    m_previewBanner->hide();
    ui->previewBannerLayout->addWidget(m_previewBanner);

    m_inspectButton = inspect;
    connect(inspect, &QPushButton::clicked, this, &CreateTorrentPage::pickTorrentToInspect);
    connect(m_copyMagnetButton, &QPushButton::clicked, this, &CreateTorrentPage::copyMagnet);

    clearPreview();
}

void CreateTorrentPage::wireUtils()
{
    if (m_torrents) {
        connect(m_torrents, &TorrentUtils::progress, this,
                [this](int percent, const QString &message) {
                    m_progressBar->setVisible(true);
                    m_progressBar->setIndeterminate(percent <= 0);
                    m_progressBar->setValue(percent);
                    ui->progressLabel->setText(message.isEmpty() ? tr("正在处理…") : message);
                });
        connect(m_torrents, &TorrentUtils::finished, this,
                [this](const QString &savePath, const QString &infoHash, qint64 totalLength) {
                    m_progressBar->setVisible(true);
                    m_progressBar->setIndeterminate(false);
                    m_progressBar->setValue(100);
                    ui->progressLabel->setText(tr("种子已生成"));
                    showResult(m_banner, true, tr("种子已生成"),
                               tr("%1\nInfo Hash %2 · 总大小 %3")
                                   .arg(QDir::toNativeSeparators(savePath),
                                        FluentTheme::prettyInfoHash(infoHash),
                                        FluentTheme::formatSize(double(totalLength))));
                    emit toast(tr("种子已生成：%1").arg(QDir::toNativeSeparators(savePath)), false);
                });
        connect(m_torrents, &TorrentUtils::failed, this, [this](const QString &reason) {
            m_progressBar->setVisible(false);
            m_progressBar->setValue(0);
            ui->progressLabel->setText(tr("就绪"));
            const QString message = reason.isEmpty() ? tr("创建种子失败") : reason;
            showResult(m_banner, false, tr("创建种子失败"), message);
            emit toast(message, true);
        });
        connect(m_torrents, &TorrentUtils::busyChanged, this,
                &CreateTorrentPage::updateActionState);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &CreateTorrentPage::restyle);
}

void CreateTorrentPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
}

// ---------------------------------------------------------------- source/save
void CreateTorrentPage::pickSourceFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择要分享的文件"), QDir::homePath(),
                                                      tr("所有文件 (*)"));
    if (!path.isEmpty())
        setSource(path);
}

void CreateTorrentPage::pickSourceFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("选择要分享的文件夹"),
                                                           QDir::homePath());
    if (!path.isEmpty())
        setSource(path);
}

void CreateTorrentPage::pickSaveTarget()
{
    QString suggested = m_savePath;
    if (suggested.isEmpty() && !m_sourcePath.isEmpty())
        suggested = m_sourcePath + QStringLiteral(".torrent");
    const QString path = QFileDialog::getSaveFileName(this, tr("保存种子文件"), suggested,
                                                      tr("种子文件 (*.torrent)"));
    if (!path.isEmpty())
        setSaveTarget(path);
}

void CreateTorrentPage::setSource(const QString &path)
{
    m_sourcePath = path;
    ui->sourceEdit->setText(QDir::toNativeSeparators(path));
    // Suggest the sibling .torrent path until the user picks one explicitly.
    if (m_savePath.isEmpty())
        setSaveTarget(path + QStringLiteral(".torrent"));
    // 自动 now has a real number to show: ask TorrentUtils for the size bucket.
    m_suggestedPieceLength = TorrentUtils::suggestPieceLength(sourceSize(path));
    updatePieceHint();
    updateActionState();
}

void CreateTorrentPage::setSaveTarget(const QString &path)
{
    m_savePath = path;
    ui->saveEdit->setText(QDir::toNativeSeparators(path));
    // A .torrent on disk (or about to be written) can always be turned into a
    // magnet link.
    m_copyMagnetButton->setEnabled(!m_savePath.isEmpty() || !m_inspectPath.isEmpty());
    updateActionState();
}

qint64 CreateTorrentPage::sourceSize(const QString &path) const
{
    const QFileInfo info(path);
    if (info.isFile())
        return info.size();
    if (!info.isDir())
        return 0;
    QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    if (ui->hiddenCheck->isChecked())
        filters |= QDir::Hidden;
    qint64 total = 0;
    QDirIterator it(path, filters, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// ------------------------------------------------------------------ trackers
QStringList CreateTorrentPage::trackerUrls() const
{
    return splitList(ui->trackerEdit->toPlainText());
}

QStringList CreateTorrentPage::webSeedUrls() const
{
    return splitList(ui->webSeedEdit->text());
}

void CreateTorrentPage::fillCommonTrackers()
{
    QStringList urls = trackerUrls();
    int added = 0;
    const QStringList common = wellKnownTrackers();
    for (const QString &url : common) {
        if (urls.contains(url, Qt::CaseInsensitive))
            continue;
        urls << url;
        ++added;
    }
    if (added == 0) {
        emit toast(tr("常用 Tracker 已全部在列表中"), false);
        return;
    }
    ui->trackerEdit->setPlainText(urls.join(QLatin1Char('\n')));
    emit toast(tr("已填入 %1 个常用 Tracker").arg(added), false);
}

// ------------------------------------------------------------------- pieces
int CreateTorrentPage::pieceLength() const
{
    return ui->pieceCombo->currentData().toInt();
}

void CreateTorrentPage::updatePieceHint()
{
    const int index = ui->pieceCombo->currentIndex();
    if (index > 0) {
        ui->pieceHint->setText(tr("固定分片大小 %1").arg(ui->pieceCombo->itemText(index)));
    } else if (m_suggestedPieceLength > 0) {
        ui->pieceHint->setText(
            tr("自动（根据体积建议 %1）").arg(FluentTheme::formatSize(m_suggestedPieceLength)));
    } else {
        ui->pieceHint->setText(tr("自动（根据体积选择 256 KiB – 16 MiB）"));
    }
}

// -------------------------------------------------------------------- create
void CreateTorrentPage::updateActionState()
{
    const bool busy = m_torrents && m_torrents->busy();
    m_createButton->setLoading(busy);
    m_createButton->setEnabled(!busy && !m_sourcePath.isEmpty() && !m_savePath.isEmpty());
    ui->actionHint->setVisible(m_sourcePath.isEmpty());
}

void CreateTorrentPage::startCreate()
{
    if (!m_torrents || m_torrents->busy())
        return;
    if (m_sourcePath.isEmpty()) {
        const QString message = tr("请先选择要分享的文件或文件夹");
        showResult(m_banner, false, tr("创建种子失败"), message);
        emit toast(message, true);
        return;
    }
    if (m_savePath.isEmpty()) {
        const QString message = tr("请先选择 .torrent 文件的保存位置");
        showResult(m_banner, false, tr("创建种子失败"), message);
        emit toast(message, true);
        return;
    }

    m_banner->hide();
    m_progressBar->setVisible(true);
    m_progressBar->setIndeterminate(true);
    m_progressBar->setValue(0);
    ui->progressLabel->setText(tr("正在读取文件…"));

    const bool ok = m_torrents->create(m_sourcePath,
                                       m_savePath,
                                       trackerUrls().join(QLatin1Char('\n')),
                                       pieceLength(),
                                       ui->commentEdit->text().trimmed(),
                                       ui->createdByEdit->text().trimmed(),
                                       ui->privateCheck->isChecked(),
                                       webSeedUrls(),
                                       ui->hiddenCheck->isChecked());

    updateActionState();
    // create() reports the precise reason through failed(); only fall back to a
    // generic message when it failed without saying why.
    if (!ok && !m_banner->isVisible()) {
        const QString message = tr("生成失败，请检查源路径、目标路径与写入权限");
        showResult(m_banner, false, tr("创建种子失败"), message);
        emit toast(message, true);
    }
}

// ------------------------------------------------------------------- preview
void CreateTorrentPage::pickTorrentToInspect()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("打开 .torrent 文件"), QDir::homePath(),
                                                      tr("种子文件 (*.torrent)"));
    if (!path.isEmpty())
        showPreview(path);
}

void CreateTorrentPage::clearPreview()
{
    m_inspectPath.clear();
    ui->previewName->setText(tr("尚未选择种子文件"));
    ui->previewHash->clear();
    ui->previewStats->clear();
    ui->previewFiles->clear();
    ui->previewFiles->hide();
    ui->previewFilesTitle->hide();
    ui->previewEmpty->show();
    m_previewBanner->hide();
    m_copyMagnetButton->setEnabled(!m_savePath.isEmpty());
}

void CreateTorrentPage::showPreview(const QString &torrentPath)
{
    const QVariantMap info = m_torrents ? m_torrents->inspect(torrentPath) : QVariantMap();
    m_inspectPath = torrentPath;

    if (!info.value(QStringLiteral("ok")).toBool()) {
        clearPreview();
        m_inspectPath = torrentPath;
        ui->previewName->setText(tr("无法读取该种子文件"));
        showResult(m_previewBanner, false, tr("无法读取种子"), tr("无法解析该种子文件"));
        emit toast(tr("无法解析该种子文件"), true);
        m_copyMagnetButton->setEnabled(true);
        return;
    }

    m_previewBanner->hide();
    m_copyMagnetButton->setEnabled(true);

    const QString name = info.value(QStringLiteral("name")).toString();
    const QString hash = info.value(QStringLiteral("infoHash")).toString();
    const qint64 total = info.value(QStringLiteral("totalLength")).toLongLong();
    const qint64 pieceSize = info.value(QStringLiteral("pieceLength")).toLongLong();
    const int pieces = info.value(QStringLiteral("pieces")).toInt();
    const bool isPrivate = info.value(QStringLiteral("isPrivate")).toBool();
    const bool multiFile = info.value(QStringLiteral("isMultiFile")).toBool();
    const QVariantList files = info.value(QStringLiteral("files")).toList();
    const QStringList trackers = info.value(QStringLiteral("announceList")).toStringList();

    ui->previewName->setText(name.isEmpty() ? tr("未命名种子") : name);
    ui->previewHash->setText(hash.isEmpty() ? QString()
                                            : tr("Info Hash  %1").arg(FluentTheme::prettyInfoHash(hash)));
    ui->previewStats->setText(tr("总大小 %1 · %2 个分片 × %3 · %4 个文件 · %5 · %6 · Tracker %7")
                                  .arg(FluentTheme::formatSize(double(total)))
                                  .arg(pieces)
                                  .arg(FluentTheme::formatSize(double(pieceSize)))
                                  .arg(files.size())
                                  .arg(multiFile ? tr("多文件") : tr("单文件"))
                                  .arg(isPrivate ? tr("私有种子") : tr("公开种子"))
                                  .arg(trackers.size()));

    // First 50 entries: a torrent can carry thousands of files and the list is
    // only meant to give the shape of the payload.
    const int limit = qMin<int>(int(files.size()), 50);
    ui->previewFiles->clear();
    for (int i = 0; i < limit; ++i) {
        const QVariantMap file = files.at(i).toMap();
        const QString filePath = file.value(QStringLiteral("path")).toString();
        auto *item = new QListWidgetItem(QStringLiteral("%1    %2")
                                             .arg(filePath,
                                                  FluentTheme::formatSize(
                                                      double(file.value(QStringLiteral("length"))
                                                                 .toLongLong()))),
                                         ui->previewFiles);
        item->setFont(FluentTheme::uiFont(12));
        item->setToolTip(filePath);
    }
    if (files.size() > limit) {
        auto *item = new QListWidgetItem(tr("还有 %1 个文件未显示").arg(files.size() - limit),
                                         ui->previewFiles);
        item->setFont(FluentTheme::uiFont(12));
    }

    const bool hasFiles = !files.isEmpty();
    ui->previewFiles->setVisible(hasFiles);
    ui->previewFilesTitle->setVisible(hasFiles);
    ui->previewEmpty->hide();
}

void CreateTorrentPage::copyMagnet()
{
    const QString path = m_inspectPath.isEmpty() ? m_savePath : m_inspectPath;
    if (path.isEmpty()) {
        emit toast(tr("请先选择或创建一个种子文件"), true);
        return;
    }
    const QString magnet = m_torrents ? m_torrents->toMagnet(path) : QString();
    if (magnet.isEmpty()) {
        emit toast(tr("无法从该种子生成磁力链接"), true);
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(magnet);
    emit toast(tr("已复制磁力链接"), false);
}

void CreateTorrentPage::showResult(InfoBar *bar, bool success, const QString &title,
                                   const QString &message)
{
    bar->setSeverity(success ? InfoBar::Success : InfoBar::Error);
    bar->setTitle(title);
    bar->setMessage(message);
    bar->show();
}

// ------------------------------------------------------------------- theming
void CreateTorrentPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();

    // The column floats on the page background instead of painting its own
    // frame: both the viewport and the container must stop filling it.
    ui->scroll->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"
                       "QScrollArea > QWidget > QWidget { background: transparent; }"));
    ui->previewFiles->setStyleSheet(
        QStringLiteral("QListWidget { background: %1; border: 1px solid %2; border-radius: %3px; }")
            .arg(t->cardSecondary().name(), t->strokeSubtle().name())
            .arg(FluentTheme::RadiusMedium));

    ui->pageSubtitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    const QList<QLabel *> captions = {
        ui->trackerHint,   ui->privateHint,  ui->hiddenHint,  ui->pieceHint,
        ui->progressLabel, ui->actionHint,   ui->previewStats, ui->previewEmpty,
    };
    for (QLabel *label : captions)
        label->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));

    m_progressBar->setBarColor(t->accent());
}

void CreateTorrentPage::retranslate()
{
    // ---- piece size entries (the previous pick is restored) --------------
    {
        struct Piece {
            QString label;
            int bytes;
        };
        const QList<Piece> pieces = {
            {tr("自动"), 0},
            {tr("16 KiB"), 16 * 1024},
            {tr("32 KiB"), 32 * 1024},
            {tr("64 KiB"), 64 * 1024},
            {tr("128 KiB"), 128 * 1024},
            {tr("256 KiB"), 256 * 1024},
            {tr("512 KiB"), 512 * 1024},
            {tr("1 MiB"), 1024 * 1024},
            {tr("2 MiB"), 2 * 1024 * 1024},
            {tr("4 MiB"), 4 * 1024 * 1024},
            {tr("8 MiB"), 8 * 1024 * 1024},
            {tr("16 MiB"), 16 * 1024 * 1024},
        };
        const int previous = ui->pieceCombo->currentIndex();
        const QSignalBlocker blocker(ui->pieceCombo);
        ui->pieceCombo->clear();
        for (const Piece &piece : pieces)
            ui->pieceCombo->addItem(piece.label, piece.bytes);
        ui->pieceCombo->setCurrentIndex(previous >= 0 && previous < ui->pieceCombo->count()
                                            ? previous
                                            : 0);
    }

    // ---- buttons ---------------------------------------------------------
    m_pickFileButton->setText(tr("选择文件…"));
    m_pickFileButton->setTooltipText(tr("选择要分享的单个文件"));
    m_pickFolderButton->setText(tr("选择文件夹…"));
    m_pickFolderButton->setTooltipText(tr("选择要分享的整个目录"));
    m_pickSaveButton->setText(tr("另存为…"));
    m_pickSaveButton->setTooltipText(tr("选择 .torrent 文件的保存位置"));
    m_commonTrackersButton->setText(tr("填入常用 Tracker"));
    m_commonTrackersButton->setTooltipText(tr("填入一组公共 Tracker 地址"));
    m_createButton->setText(tr("创建种子"));
    m_createButton->setTooltipText(tr("开始读取文件并生成 .torrent"));
    m_inspectButton->setText(tr("查看种子…"));
    m_inspectButton->setTooltipText(tr("打开一个 .torrent 文件查看内容"));
    m_copyMagnetButton->setText(tr("复制磁力链接"));
    m_copyMagnetButton->setTooltipText(tr("由当前 .torrent 生成并复制磁力链接"));

    // ---- dynamic texts ---------------------------------------------------
    ui->progressLabel->setText(tr("就绪"));
    if (m_inspectPath.isEmpty())
        ui->previewName->setText(tr("尚未选择种子文件"));
    updatePieceHint();
    updateActionState();
}
