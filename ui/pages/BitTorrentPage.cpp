#include "ui/pages/BitTorrentPage.h"

#include "Aria2Manager.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_BitTorrentPage.h"

#include <QComboBox>
#include <QCursor>
#include <QEnterEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

#include <utility>

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

} // namespace

// ============================================================================
//  BitTorrentPage::BtRow
// ============================================================================
/**
 * BtRow - one BitTorrent task row.
 *
 * Layout: [state plate] [name / meta / info hash / progress] [speeds, peers]
 * and, while the pointer is over the row, a strip of action buttons. The
 * rounded card, the leading status rail and the state plate are painted the
 * same way FluentTaskCard paints them so both lists read as one list.
 *
 * The class has no Q_OBJECT on purpose: it is a private implementation detail
 * and it talks back to its page directly (a nested class may reach the private
 * members of the class that encloses it).
 */
class BitTorrentPage::BtRow : public QFrame
{
public:
    BtRow(const QString &gid, BitTorrentPage *page, QWidget *parent = nullptr);

    QString gid() const { return m_gid; }

    void setTask(const QVariantMap &task);
    void setSelected(bool selected);
    /// Re-read the theme colours (FluentTheme::changed).
    void restyle();
    /// Rebuild every string (QEvent::LanguageChange).
    void retranslate();

    /// Re-apply the internal layout; called on resize and after content changes.
    void relayout();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    static int preferredHeight() { return 92; }

    void buildActions();
    /// Texts, colours, progress and action visibility in one place.
    void sync();
    QChar plateGlyph() const;
    QColor statusTint() const;

    QString m_gid;
    QVariantMap m_task;
    BitTorrentPage *m_page = nullptr;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_actionsVisible = false;

    FluentIcon *m_plate = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_meta = nullptr;
    QLabel *m_hash = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_upload = nullptr;
    QLabel *m_peers = nullptr;
    QLabel *m_percent = nullptr;
    FluentProgressBar *m_progress = nullptr;

    QWidget *m_actionBar = nullptr;
    FluentButton *m_pauseButton = nullptr;
    FluentButton *m_copyButton = nullptr;
    FluentButton *m_folderButton = nullptr;
    FluentButton *m_removeButton = nullptr;
};

BitTorrentPage::BtRow::BtRow(const QString &gid, BitTorrentPage *page, QWidget *parent)
    : QFrame(parent)
    , m_gid(gid)
    , m_page(page)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    // Width comes from the list layout, height from the content.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(280);
    setFixedHeight(preferredHeight());

    m_plate = new FluentIcon(this);
    m_plate->setIconSize(18);
    m_plate->setFixedSize(38, 38);
    m_plate->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_name = new QLabel(this);
    m_name->setFont(FluentTheme::uiFont(14));
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_meta = new QLabel(this);
    m_meta->setFont(FluentTheme::uiFont(12));
    m_meta->setAttribute(Qt::WA_TransparentForMouseEvents);

    // The info hash is monospaced; the family is resolved once by FluentTheme
    // and applied through the generated style sheet ("mono" label role).
    m_hash = new QLabel(this);
    m_hash->setProperty("fluentRole", "mono");
    m_hash->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_speed = new QLabel(this);
    m_speed->setFont(FluentTheme::uiFont(13, QFont::DemiBold));
    m_speed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_speed->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_upload = new QLabel(this);
    m_upload->setFont(FluentTheme::uiFont(11));
    m_upload->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_upload->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_peers = new QLabel(this);
    m_peers->setFont(FluentTheme::uiFont(11));
    m_peers->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_peers->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_percent = new QLabel(this);
    m_percent->setFont(FluentTheme::uiFont(11));
    m_percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_percent->setFixedWidth(44);
    m_percent->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_progress = new FluentProgressBar(this);
    m_progress->setBarHeight(4);
    m_progress->setAttribute(Qt::WA_TransparentForMouseEvents);

    buildActions();
    sync();
}

void BitTorrentPage::BtRow::buildActions()
{
    m_actionBar = new QWidget(this);
    auto *row = new QHBoxLayout(m_actionBar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    auto makeButton = [this, row](const QChar &glyph, const QString &tip) {
        auto *b = new FluentButton(m_actionBar);
        b->setGlyph(glyph);
        b->setIconOnly(true);
        b->setCompact(true);
        b->setRole(FluentButton::Subtle);
        b->setTooltipText(tip);
        row->addWidget(b);
        return b;
    };

    m_pauseButton = makeButton(FluentTheme::Glyph::Pause, BitTorrentPage::tr("暂停"));
    m_copyButton = makeButton(FluentTheme::Glyph::Magnet, BitTorrentPage::tr("复制磁力链接"));
    m_folderButton = makeButton(FluentTheme::Glyph::Folder, BitTorrentPage::tr("打开文件夹"));
    m_removeButton = makeButton(FluentTheme::Glyph::Delete, BitTorrentPage::tr("移除任务"));

    connect(m_pauseButton, &QPushButton::clicked, this, [this]() { m_page->toggleRowPause(m_gid); });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() { m_page->copyRowMagnet(m_gid); });
    connect(m_folderButton, &QPushButton::clicked, this, [this]() { m_page->openRowFolder(m_gid); });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() { m_page->removeRow(m_gid); });

    m_actionBar->hide();
}

QChar BitTorrentPage::BtRow::plateGlyph() const
{
    return m_task.value(QStringLiteral("isTorrent")).toBool() ? FluentTheme::Glyph::Torrent
                                                              : FluentTheme::fileGlyph(
                                                                    taskFileName(m_task));
}

QColor BitTorrentPage::BtRow::statusTint() const
{
    return FluentTheme::instance()->statusColor(m_task.value(QStringLiteral("status")).toString());
}

void BitTorrentPage::BtRow::setTask(const QVariantMap &task)
{
    m_task = task;
    sync();
}

void BitTorrentPage::BtRow::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    sync();
}

void BitTorrentPage::BtRow::restyle()
{
    sync();
}

void BitTorrentPage::BtRow::retranslate()
{
    sync();
}

void BitTorrentPage::BtRow::sync()
{
    const FluentTheme *t = FluentTheme::instance();
    const QString status = m_task.value(QStringLiteral("status")).toString();
    const bool isActive = status == QLatin1String("active");
    const bool isPaused = status == QLatin1String("paused");
    const bool isWaiting = status == QLatin1String("waiting");
    const bool isError = status == QLatin1String("error");
    const bool isDone = status == QLatin1String("complete");
    const bool hasMetadata = m_task.value(QStringLiteral("hasMetadata")).toBool();

    // ---- name ------------------------------------------------------------
    QString name = taskFileName(m_task);
    if (name.isEmpty())
        name = BitTorrentPage::tr("获取元数据中…");
    m_name->setText(name);
    m_name->setFont(FluentTheme::uiFont(14, m_selected ? QFont::DemiBold : QFont::Normal));
    m_name->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textPrimary().name()));

    // ---- info hash (monospaced) -----------------------------------------
    const QString hash = m_task.value(QStringLiteral("infoHash")).toString();
    m_hash->setText(hash.isEmpty() ? BitTorrentPage::tr("元数据获取中")
                                   : FluentTheme::prettyInfoHash(hash));

    // ---- size / files / error -------------------------------------------
    const qint64 done = m_task.value(QStringLiteral("completedLength")).toLongLong();
    const qint64 total = m_task.value(QStringLiteral("totalLength")).toLongLong();
    const int fileCount = m_task.value(QStringLiteral("fileCount")).toInt();
    QStringList bits;
    QColor metaColor = t->textTertiary();
    if (isError) {
        const QString error = m_task.value(QStringLiteral("errorMessage")).toString();
        bits << (error.isEmpty() ? BitTorrentPage::tr("未知错误") : error);
        metaColor = t->critical();
    } else {
        bits << (FluentTheme::formatSize(done) + QStringLiteral(" / ")
                + FluentTheme::formatSize(total));
        if (fileCount > 1)
            bits << BitTorrentPage::tr("%1 个文件").arg(fileCount);
        if (!hasMetadata)
            bits << BitTorrentPage::tr("元数据获取中");
    }
    m_meta->setText(bits.join(QStringLiteral("  ·  ")));
    m_meta->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(metaColor.name()));

    // ---- progress --------------------------------------------------------
    const double progress = m_task.value(QStringLiteral("progress")).toDouble();
    m_progress->setValue(progress);
    m_progress->setBarColor(statusTint());
    m_progress->setShowSegments(total > 0);
    m_progress->setIndeterminate(isActive && total <= 0);
    m_percent->setText(total > 0 ? QStringLiteral("%1%").arg(int(progress))
                                 : (isDone ? QStringLiteral("100%") : QStringLiteral("--")));
    m_percent->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->textSecondary().name()));

    // ---- speeds ----------------------------------------------------------
    const qint64 speed = m_task.value(QStringLiteral("downloadSpeed")).toLongLong();
    const qint64 upload = m_task.value(QStringLiteral("uploadSpeed")).toLongLong();
    m_speed->setText(QStringLiteral("↓ ") + FluentTheme::formatSpeed(speed));
    m_speed->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                               .arg(isActive ? t->accent().name() : t->textSecondary().name()));
    m_upload->setText(QStringLiteral("↑ ") + FluentTheme::formatSpeed(upload));
    m_upload->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->textSecondary().name()));

    // ---- seeders / connections ------------------------------------------
    const int seeders = m_task.value(QStringLiteral("numSeeders")).toInt();
    const int connections = m_task.value(QStringLiteral("connections")).toInt();
    m_peers->setText(BitTorrentPage::tr("种子 %1 · 节点 %2").arg(seeders).arg(connections));
    m_peers->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));

    // ---- state plate -----------------------------------------------------
    m_plate->setGlyph(plateGlyph());
    m_plate->setIconColor(statusTint());
    m_plate->raise();

    // ---- actions ---------------------------------------------------------
    m_pauseButton->setVisible(isActive || isPaused || isWaiting);
    m_pauseButton->setGlyph(isActive ? FluentTheme::Glyph::Pause : FluentTheme::Glyph::Play);
    m_pauseButton->setTooltipText(isActive ? BitTorrentPage::tr("暂停")
                                           : BitTorrentPage::tr("继续"));
    m_copyButton->setEnabled(!hash.isEmpty());
    m_actionBar->adjustSize();

    relayout();
    update();
}

void BitTorrentPage::BtRow::relayout()
{
    const int pad = 16;
    const int plate = 38;
    const int cy = height() / 2;

    m_plate->setGeometry(pad, cy - plate / 2, plate, plate);

    // The action strip and the right-hand info column are fixed width; the text
    // block takes whatever is left.
    const int actionsW = m_actionsVisible ? m_actionBar->sizeHint().width() + 12 : 0;
    const int infoW = 176;
    const int percentW = 54;

    const int textX = pad + plate + 14;
    const int textW = qMax(160, width() - textX - infoW - percentW - actionsW - pad - 12);

    m_name->setGeometry(textX, cy - 36, textW, 20);
    m_meta->setGeometry(textX, cy - 16, textW, 17);
    m_hash->setGeometry(textX, cy + 2, textW, 16);
    m_progress->setGeometry(textX, cy + 24, qMax(60, textW - percentW), 5);
    m_percent->setGeometry(textX + textW - percentW, cy + 20, percentW, 16);

    const int infoX = width() - actionsW - pad - infoW;
    m_speed->setGeometry(infoX, cy - 36, infoW, 20);
    m_upload->setGeometry(infoX, cy - 14, infoW, 16);
    m_peers->setGeometry(infoX, cy + 8, infoW, 16);

    if (m_actionsVisible) {
        const QSize s = m_actionBar->sizeHint();
        m_actionBar->setGeometry(width() - pad - s.width(), cy - s.height() / 2, s.width(),
                                 s.height());
    }
}

void BitTorrentPage::BtRow::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    relayout();
}

void BitTorrentPage::BtRow::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    m_hovered = true;
    if (!m_actionsVisible) {
        m_actionsVisible = true;
        m_actionBar->show();
        m_actionBar->raise();
        relayout();
    }
    update();
}

void BitTorrentPage::BtRow::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    // Keep the actions visible while the pointer is over one of the buttons.
    const QPoint local = mapFromGlobal(QCursor::pos());
    m_hovered = rect().contains(local);
    if (!m_hovered && !m_selected) {
        m_actionsVisible = false;
        m_actionBar->hide();
        relayout();
    }
    update();
}

void BitTorrentPage::BtRow::mouseReleaseEvent(QMouseEvent *event)
{
    QFrame::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        m_page->selectRow(m_gid);
}

void BitTorrentPage::BtRow::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor fill = t->card();
    if (m_selected)
        fill = QColor(t->accent().red(), t->accent().green(), t->accent().blue(),
                      t->isDark() ? 41 : 23);
    else if (m_hovered)
        fill = t->cardSecondary();

    const QColor border = m_selected
                              ? QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 166)
                              : t->strokeSubtle();

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, FluentTheme::RadiusLarge, FluentTheme::RadiusLarge);

    // Leading status rail.
    p.setPen(Qt::NoPen);
    p.setBrush(statusTint());
    p.drawRoundedRect(QRectF(r.left() + 1, r.top() + 10, 3, r.height() - 20), 1.5, 1.5);

    // The state plate background (the glyph itself is a child label).
    if (m_plate) {
        const QRect g = m_plate->geometry();
        p.setBrush(QColor(statusTint().red(), statusTint().green(), statusTint().blue(),
                          t->isDark() ? 51 : 33));
        p.drawRoundedRect(g, FluentTheme::RadiusMedium, FluentTheme::RadiusMedium);
    }
}

// ============================================================================
//  BitTorrentPage
// ============================================================================
BitTorrentPage::BitTorrentPage(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::BitTorrentPage)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    // FluentCard owns its body layout, so the section bodies declared in the
    // .ui are handed to their card here - the .ui keeps the whole structure,
    // the card only supplies its surface and padding.
    ui->tasksCard->body()->addWidget(ui->tasksBody);
    ui->trackerCard->body()->addWidget(ui->trackerBody);

    // Static roles: everything else is applied by restyle().
    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->tasksTitle->setProperty("fluentRole", "subtitle");
    ui->tasksCountLabel->setProperty("fluentRole", "tertiary");
    ui->tasksEmptyTitle->setProperty("fluentRole", "subtitle");
    ui->tasksEmptyHint->setProperty("fluentRole", "caption");
    ui->trackerTitle->setProperty("fluentRole", "subtitle");
    ui->trackerCountLabel->setProperty("fluentRole", "tertiary");
    ui->trackerTaskLabel->setProperty("fluentRole", "tertiary");
    ui->trackerHint->setProperty("fluentRole", "tertiary");
    ui->trackerList->setProperty("fluentRole", "textArea");
    ui->trackerList->setProperty("fluentMono", true);

    buildStatCards();
    buildCommandBar();
    buildTaskList();
    buildTrackerEditor();
    wireManager();

    // retranslate() also refreshes the dynamic texts (subtitle, counters, ...).
    retranslate();
}

BitTorrentPage::~BitTorrentPage()
{
    delete ui;
}

void BitTorrentPage::buildCommandBar()
{
    auto add = [this](const QChar &glyph, const QString &text, FluentButton::Role role) {
        auto *b = new FluentButton(this);
        if (!glyph.isNull())
            b->setGlyph(glyph);
        if (!text.isEmpty())
            b->setText(text);
        else
            b->setIconOnly(true);
        b->setRole(role);
        ui->commandLayout->addWidget(b);
        return b;
    };

    m_addTorrentButton = add(FluentTheme::Glyph::AddFile, tr("添加种子"), FluentButton::Accent);
    m_magnetButton = add(FluentTheme::Glyph::Magnet, tr("添加磁力链接"), FluentButton::Standard);
    m_pauseAllButton = add(FluentTheme::Glyph::Pause, tr("全部暂停"), FluentButton::Standard);
    m_resumeAllButton = add(FluentTheme::Glyph::Play, tr("全部开始"), FluentButton::Standard);
    m_refreshButton = add(FluentTheme::Glyph::Refresh, QString(), FluentButton::Subtle);

    // The magnet row is revealed by its own button and confirmed with 「添加」.
    m_magnetSubmitButton = new FluentButton(this);
    m_magnetSubmitButton->setGlyph(FluentTheme::Glyph::Check);
    m_magnetSubmitButton->setText(tr("添加"));
    m_magnetSubmitButton->setRole(FluentButton::Accent);
    m_magnetSubmitButton->setEnabled(false);
    ui->magnetLayout->addWidget(m_magnetSubmitButton);

    connect(m_addTorrentButton, &QPushButton::clicked, this, &BitTorrentPage::torrentPickerRequested);
    connect(m_magnetButton, &QPushButton::clicked, this, &BitTorrentPage::toggleMagnetPanel);
    connect(m_magnetSubmitButton, &QPushButton::clicked, this, &BitTorrentPage::submitMagnet);
    connect(ui->magnetEdit, &QLineEdit::returnPressed, this, &BitTorrentPage::submitMagnet);
    connect(ui->magnetEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_magnetSubmitButton->setEnabled(!text.trimmed().isEmpty());
    });
    connect(m_pauseAllButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->pauseAll();
    });
    connect(m_resumeAllButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->resumeAll();
    });
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_aria2)
            m_aria2->refreshNow();
    });
}

void BitTorrentPage::buildStatCards()
{
    struct Spec {
        QChar glyph;
        int tintSlot;   // 0 accent, 1 info, 2 success, 3 caution
    };
    const QList<Spec> specs = {
        {FluentTheme::Glyph::Torrent, 0},
        {FluentTheme::Glyph::Download, 1},
        {FluentTheme::Glyph::Upload, 2},
        {FluentTheme::Glyph::Peer, 3},
    };
    for (const Spec &s : specs) {
        auto *card = new StatCard(this);
        card->setGlyph(s.glyph);
        card->setValue(QStringLiteral("--"));
        card->setProperty("tintSlot", s.tintSlot);
        m_cards << card;
        ui->statLayout->addWidget(card, 1);
    }
}

void BitTorrentPage::buildTaskList()
{
    m_tasksIcon = new FluentIcon(FluentTheme::Glyph::Torrent, 16, ui->tasksBody);
    m_tasksIcon->useAccentColor();
    // The .ui created the header row; the glyph leads the title and the refresh
    // button trails the count.
    ui->tasksHeaderLayout->insertWidget(0, m_tasksIcon);
    ui->tasksHeaderLayout->addWidget(m_refreshButton);
    m_refreshButton->setTooltipText(tr("刷新"));

    // Empty state badge, above the .ui-declared texts.
    m_emptyIcon = new FluentIcon(FluentTheme::Glyph::Magnet, 28, ui->tasksEmpty);
    m_emptyIcon->useTertiaryColor();
    ui->tasksEmptyLayout->insertWidget(0, m_emptyIcon, 0, Qt::AlignHCenter);
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

    connect(m_trackerAddButton, &QPushButton::clicked, this, &BitTorrentPage::addTrackerFromInput);
    connect(m_trackerRemoveButton, &QPushButton::clicked, this,
            &BitTorrentPage::removeSelectedTracker);
    connect(ui->trackerEdit, &QLineEdit::returnPressed, this, &BitTorrentPage::addTrackerFromInput);
    connect(ui->trackerEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_trackerAddButton->setEnabled(!trackerGid().isEmpty() && !text.trimmed().isEmpty());
    });
    connect(ui->trackerCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString gid = index >= 0 ? ui->trackerCombo->itemData(index).toString() : QString();
        if (gid.isEmpty())
            return;
        setSelectedRow(gid);
        if (m_aria2 && m_aria2->detailGid() != gid)
            m_aria2->setDetailGid(gid);
        refreshTrackers();
    });
}

void BitTorrentPage::wireManager()
{
    if (m_aria2) {
        connect(m_aria2, &Aria2Manager::tasksChanged, this, &BitTorrentPage::refresh);
        connect(m_aria2, &Aria2Manager::statisticsChanged, this, &BitTorrentPage::refresh);
        connect(m_aria2, &Aria2Manager::detailGidChanged, this, [this]() {
            const QString gid = m_aria2->detailGid();
            if (gid.isEmpty())
                return;
            const int index = ui->trackerCombo->findData(gid);
            if (index >= 0 && index != ui->trackerCombo->currentIndex()) {
                // Rebuilding the combo must not look like a user pick.
                const QSignalBlocker blocker(ui->trackerCombo);
                ui->trackerCombo->setCurrentIndex(index);
            }
            setSelectedRow(gid);
            refreshTrackers();
        });
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &BitTorrentPage::restyle);
}

void BitTorrentPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
}

// ---------------------------------------------------------------- magnet row
void BitTorrentPage::toggleMagnetPanel()
{
    const bool show = !ui->magnetPanel->isVisible();
    ui->magnetPanel->setVisible(show);
    if (show)
        ui->magnetEdit->setFocus();
}

void BitTorrentPage::submitMagnet()
{
    const QString text = ui->magnetEdit->text().trimmed();
    if (text.isEmpty() || !m_aria2)
        return;
    m_aria2->addMagnet(text);
    ui->magnetEdit->clear();
    ui->magnetPanel->hide();
    emit toast(tr("已添加磁力链接"), false);
}

// --------------------------------------------------------------------- rows
QVariantMap BitTorrentPage::taskFor(const QString &gid) const
{
    if (!m_aria2 || gid.isEmpty())
        return QVariantMap();
    const QVariantList tasks = m_aria2->tasks();
    for (const QVariant &v : tasks) {
        const QVariantMap task = v.toMap();
        if (task.value(QLatin1String(kTaskGidKey)).toString() == gid)
            return task;
    }
    return QVariantMap();
}

void BitTorrentPage::selectRow(const QString &gid)
{
    if (gid.isEmpty())
        return;
    setSelectedRow(gid);
    const int index = ui->trackerCombo->findData(gid);
    if (index >= 0 && index != ui->trackerCombo->currentIndex()) {
        const QSignalBlocker blocker(ui->trackerCombo);
        ui->trackerCombo->setCurrentIndex(index);
        refreshTrackers();
    }
    if (m_aria2 && m_aria2->detailGid() != gid)
        m_aria2->setDetailGid(gid);
}

void BitTorrentPage::setSelectedRow(const QString &gid)
{
    if (m_selectedGid == gid)
        return;
    m_selectedGid = gid;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it)
        it.value()->setSelected(it.key() == gid);
}

void BitTorrentPage::toggleRowPause(const QString &gid)
{
    if (m_aria2)
        m_aria2->togglePauseTask(gid);
}

void BitTorrentPage::copyRowMagnet(const QString &gid)
{
    const QVariantMap task = taskFor(gid);
    const QString hash = task.value(QStringLiteral("infoHash")).toString();
    if (hash.isEmpty()) {
        emit toast(tr("该任务还没有 Info Hash，请等待元数据"), true);
        return;
    }
    if (m_aria2)
        m_aria2->copyToClipboard(QStringLiteral("magnet:?xt=urn:btih:") + hash);
    emit toast(tr("已复制磁力链接"), false);
}

void BitTorrentPage::openRowFolder(const QString &gid)
{
    if (m_aria2)
        m_aria2->openFolder(gid);
}

void BitTorrentPage::removeRow(const QString &gid)
{
    if (m_aria2)
        m_aria2->removeTask(gid, 0);
    emit toast(tr("已移除任务"), false);
}

// ----------------------------------------------------------------- trackers
QStringList BitTorrentPage::trackersOf(const QVariantMap &task)
{
    // Aria2Manager::rebuildLists() publishes the announce list as "trackers";
    // older revisions used "trackerUrls", so both spellings are accepted.
    QStringList urls = task.value(QStringLiteral("trackerUrls")).toStringList();
    if (urls.isEmpty())
        urls = task.value(QStringLiteral("trackers")).toStringList();
    return urls;
}

QString BitTorrentPage::trackerGid() const
{
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
    emit toast(tr("已添加 Tracker"), false);
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
    emit toast(tr("已移除 Tracker"), false);
}

// ------------------------------------------------------------------ refresh
void BitTorrentPage::refresh()
{
    if (!m_aria2)
        return;
    refreshStats();
    refreshTaskList();
    refreshTrackers();
    restyle();
}

void BitTorrentPage::refreshStats()
{
    const QVariantMap stats = m_aria2->statistics();

    /*
        aria2's getGlobalStat has no per-protocol counters, so statistics() only
        carries whole-engine numbers (downloadSpeed, uploadSpeed, activeCount,
        ...). Every BitTorrent-only figure below is therefore summed here from
        the per-task map published by Aria2Manager::rebuildLists(): the BT task
        count and how many of them are active, Σ uploadSpeed, Σ numSeeders and
        Σ connections. The engine-wide speeds come from statistics() as-is.
    */
    const QVariantList tasks = m_aria2->tasks();
    int btCount = 0;
    int activeCount = 0;
    int seeders = 0;
    int connections = 0;
    double btUploadSpeed = 0;
    for (const QVariant &v : tasks) {
        const QVariantMap task = v.toMap();
        if (!task.value(QLatin1String(kIsTorrentKey)).toBool())
            continue;
        ++btCount;
        if (task.value(QStringLiteral("status")).toString() == QLatin1String("active"))
            ++activeCount;
        seeders += task.value(QStringLiteral("numSeeders")).toInt();
        connections += task.value(QStringLiteral("connections")).toInt();
        btUploadSpeed += task.value(QStringLiteral("uploadSpeed")).toDouble();
    }
    ui->pageSubtitle->setText(tr("%1 个种子任务 · %2 个正在下载").arg(btCount).arg(activeCount));    ui->tasksCountLabel->setText(tr("%1 个").arg(btCount));

    m_pauseAllButton->setEnabled(activeCount > 0);
    m_resumeAllButton->setEnabled(btCount - activeCount > 0);

    if (m_cards.size() < 4)
        return;

    m_cards[0]->setValue(QString::number(activeCount));
    m_cards[0]->setSecondary(tr("共 %1 个").arg(btCount));
    m_cards[0]->setProgress(btCount > 0 ? 100.0 * activeCount / btCount : -1);

    m_cards[1]->setValue(FluentTheme::formatSpeed(
        stats.value(QStringLiteral("downloadSpeed")).toDouble()));
    m_cards[1]->setSecondary(tr("引擎全局速度"));

    m_cards[2]->setValue(FluentTheme::formatSpeed(
        stats.value(QStringLiteral("uploadSpeed")).toDouble()));
    m_cards[2]->setSecondary(tr("种子合计 %1").arg(FluentTheme::formatSpeed(btUploadSpeed)));

    m_cards[3]->setValue(QString::number(connections));
    m_cards[3]->setSecondary(tr("种子 %1").arg(seeders));
}

void BitTorrentPage::refreshTaskList()
{
    const QVariantList tasks = m_aria2->tasks();
    QStringList order;
    QList<QVariantMap> torrents;
    for (const QVariant &v : tasks) {
        const QVariantMap task = v.toMap();
        if (!task.value(QLatin1String(kIsTorrentKey)).toBool())
            continue;
        const QString gid = task.value(QLatin1String(kTaskGidKey)).toString();
        if (gid.isEmpty())
            continue;
        order << gid;
        torrents << task;
    }

    // Drop the rows of tasks aria2 no longer reports.
    const QStringList previous = m_rowOrder;
    for (const QString &gid : previous) {
        if (order.contains(gid))
            continue;
        if (BtRow *row = m_rows.take(gid)) {
            ui->taskLayout->removeWidget(row);
            row->deleteLater();
        }
    }
    m_rowOrder = order;

    // One row per task, updated in place and re-ordered to follow the model.
    for (int i = 0; i < torrents.size(); ++i) {
        const QVariantMap &task = torrents.at(i);
        const QString gid = task.value(QLatin1String(kTaskGidKey)).toString();
        BtRow *row = m_rows.value(gid);
        if (!row) {
            row = new BtRow(gid, this, ui->taskContainer);
            m_rows.insert(gid, row);
        }
        row->setSelected(gid == m_selectedGid);
        row->setTask(task);
        if (ui->taskLayout->indexOf(row) != i)
            ui->taskLayout->insertWidget(i, row);
    }

    const bool empty = torrents.isEmpty();
    ui->taskScroll->setVisible(!empty);
    ui->tasksEmpty->setVisible(empty);
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

    // Rebuild only when the task set really changed, and never lose the
    // selection: the combo is repopulated with its signals blocked and the
    // entry matching the previous gid is picked again.
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
    m_trackerAddButton->setEnabled(!gid.isEmpty()
                                   && !ui->trackerEdit->text().trimmed().isEmpty());
    m_trackerRemoveButton->setEnabled(!gid.isEmpty() && !trackers.isEmpty());
}

// ------------------------------------------------------------------- theming
void BitTorrentPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    const QList<QColor> tints = {t->accent(), t->info(), t->success(), t->caution()};
    for (StatCard *card : std::as_const(m_cards)) {
        const int slot = card->property("tintSlot").toInt();
        card->setTint(tints.value(slot, t->accent()));
    }

    // The list floats on the card surface instead of painting its own frame:
    // both the viewport and the container must stop filling their background.
    const QString scrollSheet =
        QStringLiteral("QScrollArea { background: transparent; border: none; }"
                       "QScrollArea > QWidget > QWidget { background: transparent; }");
    ui->taskScroll->setStyleSheet(scrollSheet);

    ui->pageSubtitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->tasksCountLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->tasksEmptyTitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textSecondary().name()));
    ui->tasksEmptyHint->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerCountLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerTaskLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));
    ui->trackerHint->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));

    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it)
        it.value()->restyle();
}

void BitTorrentPage::retranslate()
{
    // Command bar.
    m_addTorrentButton->setText(tr("添加种子"));
    m_addTorrentButton->setTooltipText(tr("打开 .torrent / .metalink 文件"));
    m_magnetButton->setText(tr("添加磁力链接"));
    m_magnetButton->setTooltipText(tr("粘贴 magnet:?xt=urn:btih:... 链接"));
    m_magnetSubmitButton->setText(tr("添加"));
    m_pauseAllButton->setText(tr("全部暂停"));
    m_pauseAllButton->setTooltipText(tr("暂停全部 BitTorrent 任务"));
    m_resumeAllButton->setText(tr("全部开始"));
    m_resumeAllButton->setTooltipText(tr("继续全部 BitTorrent 任务"));
    m_refreshButton->setTooltipText(tr("刷新"));
    m_trackerAddButton->setText(tr("添加"));
    m_trackerRemoveButton->setText(tr("移除选中"));

    // Stat cards (the values are refreshed below).
    const QStringList labels = {
        tr("活动种子数"),
        tr("总下载速度"),
        tr("总上传速度"),
        tr("连接节点数"),
    };
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards.at(i)->setLabel(labels.value(i));

    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it)
        it.value()->retranslate();

    // Force the combo (and its "no task" entry) to be rebuilt in the new
    // language; refreshTrackers() restores the previous selection.
    m_comboGids.clear();
    refresh();
}
