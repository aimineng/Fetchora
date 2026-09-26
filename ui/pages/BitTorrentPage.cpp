#include "ui/pages/BitTorrentPage.h"

#include "Aria2Manager.h"
#include "TorrentUtils.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_BitTorrentPage.h"

#include <QComboBox>
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
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char *const kTaskGidKey = "gid";
const char *const kIsTorrentKey = "isTorrent";

QString taskFileName(const QVariantMap &task)
{
    QString name = task.value(QStringLiteral("fileName")).toString();
    if (name.isEmpty())
        name = task.value(QStringLiteral("uri")).toString();
    return name;
}

/// True when the text is a path this machine can open (a drive letter, a UNC
/// path, a POSIX path or a file:// URL) rather than an http(s) address.
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

    // FluentCard owns its body layout, so the section body declared in the .ui is
    // handed to its card here.
    ui->trackerCard->body()->addWidget(ui->trackerBody);

    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->trackerTitle->setProperty("fluentRole", "subtitle");
    ui->trackerCountLabel->setProperty("fluentRole", "tertiary");
    ui->trackerTaskLabel->setProperty("fluentRole", "tertiary");
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

    // Tracker lists are shared as files (or as a URL to one), not typed in one by
    // one - so importing a list is the normal way to fill this in.
    m_trackerImportButton = new FluentButton(this);
    m_trackerImportButton->setGlyph(FluentTheme::Glyph::OpenFile);
    m_trackerImportButton->setText(tr("导入列表"));
    m_trackerImportButton->setRole(FluentButton::Subtle);
    m_trackerImportButton->setTooltipText(
        tr("从一个文件或一个网址导入 Tracker 列表：每行一个地址，非 Tracker 的行会被忽略"));
    ui->trackerInputLayout->addWidget(m_trackerImportButton);

    connect(m_trackerImportButton, &QPushButton::clicked, this, &BitTorrentPage::importTrackers);
    connect(m_trackerAddButton, &QPushButton::clicked, this, &BitTorrentPage::addTrackerFromInput);
    connect(m_trackerRemoveButton, &QPushButton::clicked, this,
            &BitTorrentPage::removeSelectedTracker);
    connect(ui->trackerEdit, &QLineEdit::returnPressed, this, &BitTorrentPage::addTrackerFromInput);
    connect(ui->trackerEdit, &QLineEdit::textChanged, this, [this]() {
        m_trackerAddButton->setEnabled(!trackerGid().isEmpty()
                                       && !ui->trackerEdit->text().trimmed().isEmpty());
    });
    connect(ui->trackerCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString gid = index >= 0 ? ui->trackerCombo->itemData(index).toString() : QString();
        if (m_aria2 && !gid.isEmpty() && m_aria2->detailGid() != gid)
            m_aria2->setDetailGid(gid);
        refreshTrackers();
    });
}

void BitTorrentPage::wireManager()
{
    if (m_aria2) {
        connect(m_aria2, &Aria2Manager::tasksChanged, this, &BitTorrentPage::refresh);
        connect(m_aria2, &Aria2Manager::detailGidChanged, this, &BitTorrentPage::refresh);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &BitTorrentPage::restyle);
    refresh();
}

QVariantMap BitTorrentPage::taskFor(const QString &gid) const
{
    if (!m_aria2 || gid.isEmpty())
        return {};
    const QVariantList tasks = m_aria2->tasks();
    for (const QVariant &v : tasks) {
        const QVariantMap task = v.toMap();
        if (task.value(QLatin1String(kTaskGidKey)).toString() == gid)
            return task;
    }
    return {};
}

QStringList BitTorrentPage::trackersOf(const QVariantMap &task)
{
    QStringList trackers;
    for (const QVariant &v : task.value(QStringLiteral("trackers")).toList()) {
        const QString tracker = v.toString();
        if (!tracker.isEmpty() && !trackers.contains(tracker))
            trackers << tracker;
    }
    return trackers;
}

QString BitTorrentPage::trackerGid() const
{
    if (!ui || !ui->trackerCombo)
        return {};
    return ui->trackerCombo->currentData().toString();
}

void BitTorrentPage::addTrackerFromInput()
{
    const QString gid = trackerGid();
    const QString url = ui->trackerEdit->text().trimmed();
    if (gid.isEmpty()) {
        emit toast(tr("请先选择一个种子任务"), true);
        return;
    }
    if (url.isEmpty()) {
        emit toast(tr("请输入 Tracker 地址"), true);
        return;
    }
    if (m_aria2)
        m_aria2->addTrackers(gid, QStringList{url});
    ui->trackerEdit->clear();
}

void BitTorrentPage::removeSelectedTracker()
{
    const QString gid = trackerGid();
    if (gid.isEmpty()) {
        emit toast(tr("请先选择一个种子任务"), true);
        return;
    }
    // The list is read-only, so the "selection" is the highlighted line (or the
    // selected text, if the user dragged over several lines: take the first).
    QString url = ui->trackerList->textCursor().selectedText();
    url.replace(QChar(0x2029), QLatin1Char('\n'));
    url = url.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (url.isEmpty())
        url = ui->trackerList->textCursor().block().text().trimmed();
    if (url.isEmpty()) {
        emit toast(tr("请先在列表中选择要移除的 Tracker"), true);
        return;
    }
    if (m_aria2)
        m_aria2->removeTracker(gid, url);
}

// ------------------------------------------------------------------ importing
void BitTorrentPage::importTrackers()
{
    if (trackerGid().isEmpty()) {
        emit toast(tr("请先选择一个种子任务"), true);
        return;
    }
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
    // The size check is in the parser as well; this one avoids reading a 4 GB file
    // into memory just to find out it is not a tracker list.
    if (!file.open(QIODevice::ReadOnly)) {
        emit toast(tr("无法读取 %1").arg(path), true);
        return;
    }
    const QByteArray data = file.read(TrackerList::kMaxBytes + 1);
    file.close();

    const TrackerList::ParseResult parsed = TrackerList::parse(data);
    if (parsed.binary) {
        emit toast(tr("这个文件看起来不是文本（可能是种子、压缩包或程序），已忽略"), true);
        return;
    }
    if (parsed.tooLarge) {
        emit toast(tr("文件太大，已忽略"), true);
        return;
    }
    applyImportedTrackers(parsed.trackers, parsed.rejected, QFileInfo(path).fileName(),
                          parsed.truncated);
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

    // A path typed here (or dragged in) reads the same way a URL does: both end up
    // as bytes through TrackerList::parse, so a local list and a remote one cannot
    // behave differently.
    if (looksLikeLocalPath(input)) {
        QString path = input;
        if (path.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
            path = QUrl(path).toLocalFile();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            emit toast(tr("无法读取 %1").arg(path), true);
            return;
        }
        const QByteArray data = file.read(TrackerList::kMaxBytes + 1);
        file.close();
        const TrackerList::ParseResult parsed = TrackerList::parse(data);
        if (parsed.binary) {
            emit toast(tr("这个文件看起来不是文本（可能是种子、压缩包或程序），已忽略"), true);
            return;
        }
        if (parsed.tooLarge) {
            emit toast(tr("文件太大，已忽略"), true);
            return;
        }
        applyImportedTrackers(parsed.trackers, parsed.rejected, QFileInfo(path).fileName(),
                              parsed.truncated);
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
        const QByteArray data = reply->readAll();
        const TrackerList::ParseResult parsed = TrackerList::parse(data);
        if (parsed.binary) {
            emit toast(tr("这个地址返回的不是文本列表，已忽略"), true);
            return;
        }
        if (parsed.tooLarge) {
            emit toast(tr("列表太大，已忽略"), true);
            return;
        }
        applyImportedTrackers(parsed.trackers, parsed.rejected, QUrl(input).host(),
                              parsed.truncated);
    });
}

void BitTorrentPage::applyImportedTrackers(const QStringList &trackers, int rejected,
                                           const QString &source, bool truncated)
{
    const QString gid = trackerGid();
    if (gid.isEmpty()) {
        emit toast(tr("请先选择一个种子任务"), true);
        return;
    }
    if (trackers.isEmpty()) {
        // Nothing usable: say what was looked at rather than failing silently.
        emit toast(tr("%1 里没有找到 Tracker 地址（已跳过 %2 行）").arg(source).arg(rejected), true);
        return;
    }
    if (m_aria2)
        m_aria2->addTrackers(gid, trackers);
    // A long list is capped rather than refused, and the cap is reported: silently
    // dropping two thirds of a list would look like it worked.
    emit toast(truncated
                   ? tr("已从 %1 导入 %2 个 Tracker（超出上限，只取了前 %2 个），跳过 %3 行")
                         .arg(source)
                         .arg(trackers.size())
                         .arg(rejected)
                   : tr("已从 %1 导入 %2 个 Tracker，跳过 %3 行")
                         .arg(source)
                         .arg(trackers.size())
                         .arg(rejected),
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

    const QString previous = trackerGid();

    QStringList gids;
    QStringList names;
    const QVariantList tasks = m_aria2->tasks();
    for (const QVariant &v : tasks) {
        const QVariantMap task = v.toMap();
        if (!task.value(QLatin1String(kIsTorrentKey)).toBool())
            continue;
        const QString gid = task.value(QLatin1String(kTaskGidKey)).toString();
        if (gid.isEmpty())
            continue;
        gids << gid;
        const QString name = taskFileName(task);
        names << (name.isEmpty() ? tr("未命名任务") : name);
    }

    // Rebuild only when the task set really changed, and never lose the selection:
    // the combo is repopulated with its signals blocked and the entry matching the
    // previous gid is picked again.
    if (gids != m_comboGids) {
        m_comboGids = gids;
        const QSignalBlocker blocker(ui->trackerCombo);
        ui->trackerCombo->clear();
        if (gids.isEmpty()) {
            ui->trackerCombo->addItem(tr("暂无种子任务"), QString());
            ui->trackerCombo->setCurrentIndex(0);
        } else {
            for (int i = 0; i < gids.size(); ++i)
                ui->trackerCombo->addItem(names.at(i), gids.at(i));
            const int restored = gids.indexOf(previous);
            ui->trackerCombo->setCurrentIndex(restored >= 0 ? restored : 0);
        }
    }

    const QString gid = trackerGid();
    const QStringList trackers = trackersOf(taskFor(gid));
    // Only touch the view when the list really changed: re-setting the text on
    // every poll would drop the user's line selection.
    const QString text = trackers.join(QLatin1Char('\n'));
    if (ui->trackerList->toPlainText() != text)
        ui->trackerList->setPlainText(text);
    ui->trackerCountLabel->setText(tr("共 %1 个 Tracker").arg(trackers.size()));
    ui->trackerHint->setVisible(trackers.isEmpty());
    ui->trackerEdit->setEnabled(!gid.isEmpty());
    m_trackerAddButton->setEnabled(!gid.isEmpty() && !ui->trackerEdit->text().trimmed().isEmpty());
    m_trackerRemoveButton->setEnabled(!gid.isEmpty() && !trackers.isEmpty());
    m_trackerImportButton->setEnabled(!gid.isEmpty());
}

// -------------------------------------------------------------------- theming
void BitTorrentPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    ui->pageSubtitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerCountLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerTaskLabel->setStyleSheet(
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
    ui->trackerTitle->setText(tr("Tracker 列表"));
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
