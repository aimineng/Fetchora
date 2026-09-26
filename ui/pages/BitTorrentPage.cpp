#include "ui/pages/BitTorrentPage.h"

#include "Aria2Manager.h"
#include "TorrentUtils.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_BitTorrentPage.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace {

/// True for a path this machine can read (a drive letter, a UNC path or a
/// file:// URL) rather than an http(s) address.
bool looksLikeLocalPath(const QString &text)
{
    if (text.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
        return true;
    if (text.contains(QStringLiteral("://")))
        return false;
    if (text.startsWith(QLatin1String("\\\\")))
        return true;
    return text.size() > 1 && text.at(1) == QLatin1Char(':');
}

} // namespace

BitTorrentPage::BitTorrentPage(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::BitTorrentPage)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    ui->trackerCard->body()->addWidget(ui->trackerBody);

    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->trackerTitle->setProperty("fluentRole", "subtitle");
    ui->trackerCountLabel->setProperty("fluentRole", "tertiary");
    ui->trackerScopeLabel->setProperty("fluentRole", "tertiary");
    ui->trackerHint->setProperty("fluentRole", "tertiary");
    ui->trackerList->setProperty("fluentRole", "textArea");
    ui->trackerList->setProperty("fluentMono", true);

    buildTrackerEditor();
    wireManager();
    retranslate();
}

BitTorrentPage::~BitTorrentPage()
{
    delete ui;
}

void BitTorrentPage::buildTrackerEditor()
{
    m_trackerAddButton = new FluentButton(this);
    m_trackerAddButton->setGlyph(FluentTheme::Glyph::Add);
    m_trackerAddButton->setText(tr("添加"));
    m_trackerAddButton->setRole(FluentButton::Standard);
    ui->trackerInputLayout->addWidget(m_trackerAddButton);

    m_trackerRemoveButton = new FluentButton(this);
    m_trackerRemoveButton->setGlyph(FluentTheme::Glyph::Close);
    m_trackerRemoveButton->setText(tr("移除选中"));
    m_trackerRemoveButton->setRole(FluentButton::Subtle);
    ui->trackerInputLayout->addWidget(m_trackerRemoveButton);

    m_trackerImportButton = new FluentButton(this);
    m_trackerImportButton->setGlyph(FluentTheme::Glyph::OpenFile);
    m_trackerImportButton->setText(tr("导入列表"));
    m_trackerImportButton->setRole(FluentButton::Subtle);
    m_trackerImportButton->setTooltipText(
        tr("从一个文件或一个网址导入 Tracker 列表：每行一个地址，非 Tracker 的行会被忽略"));
    ui->trackerInputLayout->addWidget(m_trackerImportButton);

    // Clearing the setting is how you get the built-in list back, so that state
    // needs a button of its own rather than "delete all 10 entries by hand".
    m_trackerResetButton = new FluentButton(this);
    m_trackerResetButton->setGlyph(FluentTheme::Glyph::Refresh);
    m_trackerResetButton->setText(tr("恢复内置"));
    m_trackerResetButton->setRole(FluentButton::Subtle);
    m_trackerResetButton->setTooltipText(tr("清空自定义列表，改用随程序内置的公共 Tracker"));
    ui->trackerInputLayout->addWidget(m_trackerResetButton);

    connect(m_trackerImportButton, &QPushButton::clicked, this, &BitTorrentPage::importTrackers);
    connect(m_trackerAddButton, &QPushButton::clicked, this, &BitTorrentPage::addTrackerFromInput);
    connect(m_trackerRemoveButton, &QPushButton::clicked, this,
            &BitTorrentPage::removeSelectedTracker);
    connect(m_trackerResetButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->setGlobalTrackers({});
    });
    connect(ui->trackerEdit, &QLineEdit::returnPressed, this, &BitTorrentPage::addTrackerFromInput);
    connect(ui->trackerEdit, &QLineEdit::textChanged, this, [this]() {
        m_trackerAddButton->setEnabled(!ui->trackerEdit->text().trimmed().isEmpty());
    });
}

void BitTorrentPage::wireManager()
{
    if (m_aria2) {
        connect(m_aria2, &Aria2Manager::tasksChanged, this, &BitTorrentPage::refresh);
        connect(m_aria2, &Aria2Manager::globalOptionsChanged, this, &BitTorrentPage::refresh);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &BitTorrentPage::restyle);
    refresh();
}

void BitTorrentPage::addTrackerFromInput()
{
    const QString url = ui->trackerEdit->text().trimmed();
    if (url.isEmpty() || !m_aria2) {
        emit toast(tr("请输入 Tracker 地址"), true);
        return;
    }
    QStringList trackers = m_aria2->globalTrackers();
    if (!trackers.contains(url))
        trackers << url;
    m_aria2->setGlobalTrackers(trackers);
    ui->trackerEdit->clear();
}

void BitTorrentPage::removeSelectedTracker()
{
    if (!m_aria2)
        return;
    // The list is read-only, so the "selection" is the highlighted line (or the
    // first line of a dragged selection).
    QString url = ui->trackerList->textCursor().selectedText();
    url.replace(QChar(0x2029), QLatin1Char('\n'));
    url = url.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (url.isEmpty())
        url = ui->trackerList->textCursor().block().text().trimmed();
    if (url.isEmpty()) {
        emit toast(tr("请先在列表中选择要移除的 Tracker"), true);
        return;
    }
    QStringList trackers = m_aria2->globalTrackers();
    trackers.removeAll(url);
    // Removing the last of the built-in entries would silently fall back to the
    // built-in list again, so say what happened instead of looking like a no-op.
    m_aria2->setGlobalTrackers(trackers);
}

// ------------------------------------------------------------------ importing
void BitTorrentPage::importTrackers()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("导入 Tracker 列表"));
    box.setText(tr("从哪里读取 Tracker 列表？\n每行一个地址；不是 Tracker 的内容会被忽略。"));
    QPushButton *fromFile = box.addButton(tr("本地文件…"), QMessageBox::AcceptRole);
    QPushButton *fromUrl = box.addButton(tr("网址或路径…"), QMessageBox::ActionRole);
    box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == fromFile)
        importTrackersFromFile();
    else if (box.clickedButton() == fromUrl)
        importTrackersFromUrl();
}

void BitTorrentPage::importTrackersFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 Tracker 列表文件"), QString(),
        tr("文本文件 (*.txt *.list *.md);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit toast(tr("无法读取 %1").arg(path), true);
        return;
    }
    applyImportedTrackers(file.read(TrackerList::kMaxBytes + 1), QFileInfo(path).fileName());
}

void BitTorrentPage::importTrackersFromUrl()
{
    bool ok = false;
    const QString input = QInputDialog::getText(this, tr("从网址或路径导入 Tracker"),
                                                tr("列表地址（http/https）或本地文件路径"),
                                                QLineEdit::Normal, QString(), &ok)
                              .trimmed();
    if (!ok || input.isEmpty())
        return;

    // A path typed here reads exactly like a URL does: both end up as bytes through
    // TrackerList::parse, so a local list and a remote one cannot behave differently.
    if (looksLikeLocalPath(input)) {
        QString path = input;
        if (path.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
            path = QUrl(path).toLocalFile();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            emit toast(tr("无法读取 %1").arg(path), true);
            return;
        }
        applyImportedTrackers(file.read(TrackerList::kMaxBytes + 1), QFileInfo(path).fileName());
        return;
    }
    if (!input.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !input.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        emit toast(tr("只支持 http、https 地址或本地文件路径"), true);
        return;
    }

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(input)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, input, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit toast(tr("下载列表失败：%1").arg(reply->errorString()), true);
            return;
        }
        applyImportedTrackers(reply->readAll(), QUrl(input).host());
    });
}

void BitTorrentPage::applyImportedTrackers(const QByteArray &data, const QString &source)
{
    const TrackerList::ParseResult parsed = TrackerList::parse(data);
    if (parsed.binary) {
        emit toast(tr("这个文件看起来不是文本（可能是种子、压缩包或程序），已忽略"), true);
        return;
    }
    if (parsed.tooLarge) {
        emit toast(tr("文件太大，已忽略"), true);
        return;
    }
    if (parsed.trackers.isEmpty()) {
        emit toast(tr("%1 里没有找到 Tracker 地址（已跳过 %2 行）").arg(source).arg(parsed.rejected),
                   true);
        return;
    }

    QStringList trackers = m_aria2 ? m_aria2->globalTrackers() : QStringList();
    int added = 0;
    for (const QString &tracker : parsed.trackers) {
        if (trackers.contains(tracker))
            continue;
        trackers << tracker;
        ++added;
    }
    if (m_aria2 && added > 0)
        m_aria2->setGlobalTrackers(trackers);

    // Nothing silently dropped: both the sum and the lines that were not trackers
    // are reported, because a list the user cannot see the outcome of is unusable.
    emit toast(tr("已从 %1 导入 %2 个 Tracker（新增 %3），跳过 %4 行")
                   .arg(source)
                   .arg(parsed.trackers.size())
                   .arg(added)
                   .arg(parsed.rejected),
               false);
}

// ------------------------------------------------------------------ refreshing
void BitTorrentPage::refresh()
{
    refreshTrackers();
    restyle();
}

void BitTorrentPage::refreshTrackers()
{
    if (!m_aria2)
        return;

    const QStringList trackers = m_aria2->globalTrackers();
    const QString text = trackers.join(QLatin1Char('\n'));
    // Only touch the view when the list really changed: re-setting the text on every
    // poll would drop the user's line selection.
    if (ui->trackerList->toPlainText() != text)
        ui->trackerList->setPlainText(text);
    ui->trackerCountLabel->setText(tr("共 %1 个 Tracker").arg(trackers.size()));
    ui->trackerHint->setVisible(trackers.isEmpty());
    ui->trackerEdit->setEnabled(true);
    m_trackerAddButton->setEnabled(!ui->trackerEdit->text().trimmed().isEmpty());
    m_trackerRemoveButton->setEnabled(!trackers.isEmpty());
    m_trackerResetButton->setEnabled(true);
}

// -------------------------------------------------------------------- theming
void BitTorrentPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    ui->pageSubtitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerCountLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerScopeLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerHint->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
}

void BitTorrentPage::retranslate()
{
    m_trackerAddButton->setText(tr("添加"));
    m_trackerRemoveButton->setText(tr("移除选中"));
    m_trackerImportButton->setText(tr("导入列表"));
    m_trackerImportButton->setTooltipText(
        tr("从一个文件或一个网址导入 Tracker 列表：每行一个地址，非 Tracker 的行会被忽略"));
    m_trackerResetButton->setText(tr("恢复内置"));
    m_trackerResetButton->setTooltipText(tr("清空自定义列表，改用随程序内置的公共 Tracker"));
    ui->trackerTitle->setText(tr("全局 Tracker 列表"));
    ui->trackerScopeLabel->setText(tr("应用于所有 BitTorrent 任务"));
    refreshTrackers();
}

void BitTorrentPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
}
