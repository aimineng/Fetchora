#include "ui/pages/HistoryPage.h"

#include "Aria2Manager.h"
#include "DownloadHistory.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_HistoryPage.h"

#include <QCursor>
#include <QDateTime>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollArea>
#include <QSet>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

#include <utility>

namespace {

/// 与 DownloadHistory::fetch() 的 filter 参数一一对应，顺序和下拉框一致。
const QStringList &filterKeys()
{
    static const QStringList keys = {QStringLiteral("all"), QStringLiteral("complete"),
                                     QStringLiteral("error"), QStringLiteral("removed")};
    return keys;
}

/// 一次拉取的上限；和 QML 版历史页保持一致。
const int kFetchLimit = 500;

/**
 * QLabel 样式串。
 *
 * 字号必须写在样式表里：应用级样式表有一条 `QLabel { font-size: 14px; }`，
 * 它会盖掉 setFont()。字族一律取自 FluentTheme::uiFont()，不手写字体列表。
 */
QString labelStyle(const QColor &color, int pixelSize, int weight = QFont::Normal)
{
    return QStringLiteral("QLabel { color: %1; font-family: \"%2\"; font-size: %3px;"
                          " font-weight: %4; background: transparent; }")
        .arg(color.name(QColor::HexArgb), FluentTheme::uiFont(pixelSize).family())
        .arg(pixelSize)
        .arg(weight);
}

/// 记录时间：优先完成时间，其次创建时间。
QString formatStamp(const QVariantMap &record)
{
    QString raw = record.value(QStringLiteral("finished_at")).toString();
    if (raw.isEmpty())
        raw = record.value(QStringLiteral("created_at")).toString();
    const QDateTime stamp = QDateTime::fromString(raw, Qt::ISODate);
    if (!stamp.isValid())
        return QStringLiteral("--");
    return stamp.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

/// 只有真正的远程链接才谈得上“重新下载”。
bool isRemoteUri(const QString &uri)
{
    return uri.startsWith(QLatin1String("http://")) || uri.startsWith(QLatin1String("https://"))
        || uri.startsWith(QLatin1String("ftp://")) || uri.startsWith(QLatin1String("ftps://"))
        || uri.startsWith(QLatin1String("magnet:"));
}

/// 失败原因：优先用 aria2 给的描述，否则把错误码翻译成中文。
QString errorText(const QVariantMap &record)
{
    const QString message = record.value(QStringLiteral("error_message")).toString();
    if (!message.isEmpty())
        return message;
    const QString code = record.value(QStringLiteral("error_code")).toString();
    if (code.isEmpty() || code == QLatin1String("0"))
        return QString();
    return Aria2Manager::aria2ErrorMessage(code);
}

/**
 * ElidedLabel - 按当前宽度省略的 QLabel。
 *
 * 文件名和路径必须省略，而 QLabel 自己不省略；这里在 paintEvent 里用
 * fontMetrics() 现算，所以行宽变化时不需要重新排版。
 */
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setMinimumWidth(1);
    }

    void setElideMode(Qt::TextElideMode mode) { m_mode = mode; }
    void setFullText(const QString &text)
    {
        m_full = text;
        setToolTip(text);
        update();
    }
    QString fullText() const { return m_full; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_full.isEmpty())
            return;
        QPainter painter(this);
        QStyleOption opt;
        opt.initFrom(this);
        // 和 QLabel 一样走 QStyle，这样样式表里的 color / 禁用态都能生效。
        style()->drawItemText(&painter, rect(), int(alignment()) | Qt::TextSingleLine,
                              opt.palette, isEnabled(),
                              fontMetrics().elidedText(m_full, m_mode, width()),
                              QPalette::WindowText);
    }

private:
    QString m_full;
    Qt::TextElideMode m_mode = Qt::ElideMiddle;
};

} // namespace

// ============================================================================
//  HistoryRow - 一条自包含的历史记录
// ============================================================================
/**
 * 行布局：[状态色图标板] [名称 + 状态胶囊 / 链接或路径 / 体积 · 时间] [悬停操作]
 *
 * 行不依赖任何表格控件：它自己画卡片背景和状态导轨，操作按钮只在鼠标悬停
 * 时出现，双击行等同于“打开文件夹”。
 */
class HistoryPage::HistoryRow : public QFrame
{
public:
    HistoryRow(HistoryPage *page, QWidget *parent = nullptr);

    void setRecord(const QVariantMap &record);
    void retranslate();
    void restyle();

    QString gid() const { return m_gid; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QString statusKey() const;
    QString statusText() const;
    QString detailLine() const;
    QString metaLine() const;
    QString styleKey() const;
    QChar plateGlyph() const;
    QColor tint() const;
    void updateActions();

    HistoryPage *m_page = nullptr;
    QVariantMap m_record;
    QString m_gid;
    /// 决定配色的一小段指纹：状态 + 失败原因；没变就不重算样式表。
    QString m_styleKey;
    bool m_hovered = false;

    FluentIcon *m_plate = nullptr;
    ElidedLabel *m_name = nullptr;
    ElidedLabel *m_detail = nullptr;
    ElidedLabel *m_meta = nullptr;
    QLabel *m_pill = nullptr;

    QWidget *m_actionBar = nullptr;
    FluentButton *m_redownloadButton = nullptr;
    FluentButton *m_copyButton = nullptr;
    FluentButton *m_folderButton = nullptr;
    FluentButton *m_deleteButton = nullptr;
};

HistoryPage::HistoryRow::HistoryRow(HistoryPage *page, QWidget *parent)
    : QFrame(parent)
    , m_page(page)
{
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(320);
    // 三行文本（名称 / 链接 / 体积 · 时间）+ 上下留白。
    setFixedHeight(84);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(18, 10, 12, 10);
    row->setSpacing(14);

    // ------------------------------------------------------------ 图标板
    m_plate = new FluentIcon(FluentTheme::Glyph::File, 18, this);
    m_plate->setFixedSize(40, 40);
    row->addWidget(m_plate, 0, Qt::AlignVCenter);

    // ------------------------------------------------------------ 文本列
    auto *text = new QVBoxLayout;
    text->setSpacing(3);

    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(8);
    m_name = new ElidedLabel(this);
    m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    nameRow->addWidget(m_name, 1);

    m_pill = new QLabel(this);
    m_pill->setAlignment(Qt::AlignCenter);
    nameRow->addWidget(m_pill, 0, Qt::AlignVCenter);
    text->addLayout(nameRow);

    m_detail = new ElidedLabel(this);
    m_detail->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    text->addWidget(m_detail);

    m_meta = new ElidedLabel(this);
    m_meta->setElideMode(Qt::ElideRight);
    m_meta->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    text->addWidget(m_meta);

    row->addLayout(text, 1);

    // -------------------------------------------------------- 悬停操作
    m_actionBar = new QWidget(this);
    auto *actions = new QHBoxLayout(m_actionBar);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(4);

    auto makeButton = [this, actions](const QChar &glyph) {
        auto *button = new FluentButton(m_actionBar);
        button->setGlyph(glyph);
        button->setIconOnly(true);
        button->setCompact(true);
        button->setRole(FluentButton::Subtle);
        actions->addWidget(button);
        return button;
    };

    m_redownloadButton = makeButton(FluentTheme::Glyph::Refresh);
    m_copyButton = makeButton(FluentTheme::Glyph::Copy);
    m_folderButton = makeButton(FluentTheme::Glyph::Folder);
    m_deleteButton = makeButton(FluentTheme::Glyph::Delete);

    connect(m_redownloadButton, &QPushButton::clicked, this, [this]() {
        m_page->requestRedownload(m_record.value(QStringLiteral("uri")).toString());
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        m_page->copyLink(m_record.value(QStringLiteral("uri")).toString());
    });
    connect(m_folderButton, &QPushButton::clicked, this, [this]() {
        m_page->openEntryFolder(m_record.value(QStringLiteral("dir")).toString(),
                                m_record.value(QStringLiteral("name")).toString());
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() { m_page->removeEntry(m_gid); });

    m_actionBar->hide();
    row->addWidget(m_actionBar, 0, Qt::AlignVCenter);

    retranslate();
}

void HistoryPage::HistoryRow::retranslate()
{
    m_redownloadButton->setTooltipText(HistoryPage::tr("重新下载"));
    m_copyButton->setTooltipText(HistoryPage::tr("复制链接"));
    m_folderButton->setTooltipText(HistoryPage::tr("打开文件夹"));
    m_deleteButton->setTooltipText(HistoryPage::tr("删除记录"));
}

void HistoryPage::HistoryRow::setRecord(const QVariantMap &record)
{
    m_record = record;
    m_gid = record.value(QStringLiteral("gid")).toString();

    // 名称缺失时退回链接，至少让这一行有辨识度。
    QString name = record.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = record.value(QStringLiteral("uri")).toString();
    m_name->setFullText(name);

    m_detail->setFullText(detailLine());
    m_meta->setText(metaLine());
    m_pill->setText(statusText());
    m_plate->setGlyph(plateGlyph());
    updateActions();

    // 500 行 × 每次击键都重上色会很慢，只有配色指纹变了才重算。
    if (styleKey() != m_styleKey)
        restyle();
}

QString HistoryPage::HistoryRow::styleKey() const
{
    return statusKey() + QLatin1Char('\n') + errorText(m_record);
}

void HistoryPage::HistoryRow::updateActions()
{
    const QString uri = m_record.value(QStringLiteral("uri")).toString();
    const QString dir = m_record.value(QStringLiteral("dir")).toString();
    const QString name = m_record.value(QStringLiteral("name")).toString();

    m_redownloadButton->setEnabled(isRemoteUri(uri));
    m_copyButton->setEnabled(!uri.isEmpty());
    m_folderButton->setEnabled(!dir.isEmpty() || QFileInfo(name).isAbsolute());
    m_deleteButton->setEnabled(!m_gid.isEmpty());
}

QString HistoryPage::HistoryRow::statusKey() const
{
    // 已移除的记录按 action 判定，status 字段这时可能还是 complete。
    if (m_record.value(QStringLiteral("action")).toString() == QLatin1String("removed"))
        return QStringLiteral("removed");
    return m_record.value(QStringLiteral("status")).toString();
}

QString HistoryPage::HistoryRow::statusText() const
{
    const QString key = statusKey();
    if (key.isEmpty())
        return HistoryPage::tr("未知");
    return FluentTheme::statusLabel(key);
}

QString HistoryPage::HistoryRow::detailLine() const
{
    const QString uri = m_record.value(QStringLiteral("uri")).toString();
    if (!uri.isEmpty())
        return uri;
    return m_record.value(QStringLiteral("dir")).toString();
}

QString HistoryPage::HistoryRow::metaLine() const
{
    const FluentTheme *t = FluentTheme::instance();
    QStringList bits;
    bits << t->formatSize(m_record.value(QStringLiteral("total_length")).toDouble());
    bits << formatStamp(m_record);

    const double average = m_record.value(QStringLiteral("avg_speed")).toDouble();
    if (average > 0)
        bits << HistoryPage::tr("均速 %1").arg(t->formatSpeed(average));

    const int files = m_record.value(QStringLiteral("files")).toInt();
    if (files > 1)
        bits << HistoryPage::tr("%1 个文件").arg(files);

    const QString error = errorText(m_record);
    if (!error.isEmpty())
        bits << error;

    return bits.join(QStringLiteral("  ·  "));
}

QChar HistoryPage::HistoryRow::plateGlyph() const
{
    const QString name = m_record.value(QStringLiteral("name")).toString();
    if (m_record.value(QStringLiteral("is_torrent")).toBool())
        return FluentTheme::Glyph::Torrent;
    return FluentTheme::fileGlyph(name);
}

QColor HistoryPage::HistoryRow::tint() const
{
    return FluentTheme::instance()->statusColor(statusKey());
}

void HistoryPage::HistoryRow::restyle()
{
    const FluentTheme *t = FluentTheme::instance();
    const QColor rowTint = tint();

    m_name->setStyleSheet(labelStyle(t->textPrimary(), 14, QFont::DemiBold));
    m_detail->setStyleSheet(labelStyle(t->textSecondary(), 12));

    // 失败的行把错误原因显示成告警色。
    const bool failed = !errorText(m_record).isEmpty();
    m_meta->setStyleSheet(labelStyle(failed ? t->critical() : t->textTertiary(), 12));

    m_pill->setStyleSheet(
        QStringLiteral("QLabel { background: %1; color: %2; border-radius: 9px; padding: 2px 8px;"
                       " font-family: \"%3\"; font-size: 11px; }")
            .arg(QColor(rowTint.red(), rowTint.green(), rowTint.blue(), t->isDark() ? 56 : 38)
                     .name(QColor::HexArgb),
                 rowTint.name(), FluentTheme::uiFont(11).family()));

    m_plate->setIconColor(rowTint);
    m_plate->setFixedSize(40, 40);
    m_styleKey = styleKey();
    update();
}

void HistoryPage::HistoryRow::paintEvent(QPaintEvent *)
{
    const FluentTheme *t = FluentTheme::instance();
    const QColor rowTint = tint();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(m_hovered ? QColor(rowTint.red(), rowTint.green(), rowTint.blue(), 140)
                            : t->strokeSubtle(),
                  1));
    p.setBrush(m_hovered ? t->cardSecondary() : t->card());
    p.drawRoundedRect(r, FluentTheme::RadiusLarge, FluentTheme::RadiusLarge);

    // 状态色导轨。
    p.setPen(Qt::NoPen);
    p.setBrush(rowTint);
    p.drawRoundedRect(QRectF(r.left() + 4, r.top() + 12, 3, r.height() - 24), 1.5, 1.5);

    // 图标板底色（字形本身是子控件）。
    if (m_plate) {
        p.setBrush(QColor(rowTint.red(), rowTint.green(), rowTint.blue(), t->isDark() ? 51 : 33));
        p.drawRoundedRect(m_plate->geometry(), FluentTheme::RadiusMedium, FluentTheme::RadiusMedium);
    }
}

void HistoryPage::HistoryRow::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    m_hovered = true;
    m_actionBar->show();
    m_actionBar->raise();
    update();
}

void HistoryPage::HistoryRow::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    // 指针移到按钮上时行仍然是“悬停”状态。
    m_hovered = rect().contains(mapFromGlobal(QCursor::pos()));
    if (!m_hovered)
        m_actionBar->hide();
    update();
}

void HistoryPage::HistoryRow::mouseDoubleClickEvent(QMouseEvent *event)
{
    QFrame::mouseDoubleClickEvent(event);
    if (event->button() == Qt::LeftButton)
        m_page->openEntryFolder(m_record.value(QStringLiteral("dir")).toString(),
                                m_record.value(QStringLiteral("name")).toString());
}

// ============================================================================
//  HistoryPage
// ============================================================================
HistoryPage::HistoryPage(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HistoryPage)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");

    buildHeader();
    buildStatCards();
    buildList();
    buildEmptyState();
    wireManager();

    retranslate();
    restyle();
}

HistoryPage::~HistoryPage()
{
    delete ui;
}

// ============================================================================
//  构建
// ============================================================================
void HistoryPage::buildHeader()
{
    // 搜索框与筛选下拉都在代码里建，插进 .ui 提供的宿主布局。
    m_search = new FluentLineEdit(this);
    m_search->setFieldGlyph(FluentTheme::Glyph::Search);
    ui->searchHostLayout->addWidget(m_search);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_searchText = text;
        refresh();
    });

    m_filterCombo = new FluentComboBox(this);
    ui->filterHostLayout->addWidget(m_filterCombo);
    connect(m_filterCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_filterIndex = index;
        refresh();
    });

    m_refreshButton = new FluentButton(this);
    m_refreshButton->setGlyph(FluentTheme::Glyph::Refresh);
    m_refreshButton->setIconOnly(true);
    m_refreshButton->setRole(FluentButton::Subtle);
    ui->actionHostLayout->addWidget(m_refreshButton);
    connect(m_refreshButton, &QPushButton::clicked, this, &HistoryPage::refresh);
}

void HistoryPage::buildStatCards()
{
    struct Spec {
        QChar glyph;
        QString label;
        int tintSlot;   // 0 accent, 1 success, 2 critical
    };
    const QList<Spec> specs = {
        {FluentTheme::Glyph::History, tr("总条目"), 0},
        {FluentTheme::Glyph::Success, tr("已完成"), 1},
        {FluentTheme::Glyph::Error, tr("失败"), 2},
    };
    for (const Spec &s : specs) {
        auto *card = new StatCard(this);
        card->setGlyph(s.glyph);
        card->setLabel(s.label);
        card->setValue(QStringLiteral("--"));
        card->setProperty("tintSlot", s.tintSlot);
        m_cards << card;
        ui->statLayout->addWidget(card, 1);
    }
}

void HistoryPage::buildList()
{
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_rowsHost = new QWidget(m_scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 8, 0);
    m_rowsLayout->setSpacing(8);
    // 末尾留一个 stretch：行从上往下排，新的行插在它前面。
    m_rowsLayout->addStretch(1);
    m_scroll->setWidget(m_rowsHost);

    ui->listHostLayout->addWidget(m_scroll, 1);
}

void HistoryPage::buildEmptyState()
{
    m_emptyState = new QWidget(this);
    auto *outer = new QVBoxLayout(m_emptyState);
    outer->setContentsMargins(24, 0, 24, 0);
    outer->setSpacing(10);
    outer->addStretch(1);

    // 72px 圆角图标板：底色由 restyle() 上色，所以外面套一个宿主 widget。
    m_emptyPlate = new QWidget(m_emptyState);
    m_emptyPlate->setObjectName(QStringLiteral("emptyPlate"));
    m_emptyPlate->setAttribute(Qt::WA_StyledBackground, true);
    m_emptyPlate->setFixedSize(72, 72);
    auto *plateLayout = new QVBoxLayout(m_emptyPlate);
    plateLayout->setContentsMargins(0, 0, 0, 0);
    plateLayout->setSpacing(0);
    m_emptyGlyph = new FluentIcon(FluentTheme::Glyph::History, 30, m_emptyPlate);
    m_emptyGlyph->setFixedSize(72, 72);
    plateLayout->addWidget(m_emptyGlyph, 0, Qt::AlignCenter);

    m_emptyTitle = new QLabel(m_emptyState);
    m_emptyTitle->setAlignment(Qt::AlignCenter);

    m_emptyHint = new QLabel(m_emptyState);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setMaximumWidth(420);

    m_emptyResetButton = new FluentButton(m_emptyState);
    m_emptyResetButton->setGlyph(FluentTheme::Glyph::Filter);
    m_emptyResetButton->setRole(FluentButton::Standard);
    connect(m_emptyResetButton, &QPushButton::clicked, this, [this]() {
        m_search->clear();
        m_filterCombo->setCurrentIndex(0);
        refresh();
    });

    outer->addWidget(m_emptyPlate, 0, Qt::AlignHCenter);
    outer->addWidget(m_emptyTitle, 0, Qt::AlignHCenter);
    outer->addWidget(m_emptyHint, 0, Qt::AlignHCenter);
    outer->addWidget(m_emptyResetButton, 0, Qt::AlignHCenter);
    outer->addStretch(1);

    ui->listHostLayout->addWidget(m_emptyState, 1);
}

void HistoryPage::wireManager()
{
    if (m_aria2) {
        if (DownloadHistory *history = m_aria2->history())
            connect(history, &DownloadHistory::changed, this, &HistoryPage::refresh);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &HistoryPage::restyle);
}

// ============================================================================
//  行内操作
// ============================================================================
void HistoryPage::requestRedownload(const QString &uri)
{
    if (!uri.isEmpty())
        emit redownloadRequested(uri);
}

void HistoryPage::copyLink(const QString &uri)
{
    if (m_aria2 && !uri.isEmpty())
        m_aria2->copyToClipboard(uri);
}

void HistoryPage::openEntryFolder(const QString &dir, const QString &name)
{
    if (!m_aria2)
        return;
    if (!dir.isEmpty()) {
        m_aria2->openPath(dir);
        return;
    }
    // 没有目录时退回按完整路径定位文件。
    if (QFileInfo(name).isAbsolute())
        m_aria2->revealFile(name);
}

void HistoryPage::removeEntry(const QString &gid)
{
    if (!m_aria2 || gid.isEmpty())
        return;
    if (DownloadHistory *history = m_aria2->history())
        history->remove(gid);   // remove() 会发 changed()，页面随即刷新
}

QString HistoryPage::filterKey() const
{
    return filterKeys().value(m_filterIndex, QStringLiteral("all"));
}

// ============================================================================
//  刷新 / 翻译 / 主题
// ============================================================================
void HistoryPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange) {
        restyle();
    }
}

void HistoryPage::refresh()
{
    if (!m_aria2 || !m_rowsLayout)
        return;

    DownloadHistory *history = m_aria2->history();
    const QVariantList records =
        history ? history->fetch(filterKey(), m_searchText, kFetchLimit) : QVariantList();

    // ---- 行：按 gid 复用，只新建缺的，其余原地更新 -----------------------
    QStringList order;
    QSet<QString> live;
    for (const QVariant &value : records) {
        const QVariantMap record = value.toMap();
        const QString gid = record.value(QStringLiteral("gid")).toString();
        if (gid.isEmpty())
            continue;
        order << gid;
        live.insert(gid);

        HistoryRow *row = m_rows.value(gid);
        if (!row) {
            row = new HistoryRow(this, m_rowsHost);
            m_rows.insert(gid, row);
            m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, row);
            row->show();
        }
        row->setRecord(record);
    }

    for (auto it = m_rows.begin(); it != m_rows.end();) {
        if (live.contains(it.key())) {
            ++it;
            continue;
        }
        HistoryRow *row = it.value();
        m_rowsLayout->removeWidget(row);   // removeWidget() 会隐藏控件
        row->deleteLater();
        it = m_rows.erase(it);
    }

    // 顺序只在真正变化时重排：QLayout::removeWidget() 之后必须重新 show()。
    if (order != m_order) {
        const bool updates = m_rowsHost->updatesEnabled();
        m_rowsHost->setUpdatesEnabled(false);
        for (HistoryRow *row : std::as_const(m_rows))
            m_rowsLayout->removeWidget(row);
        for (int i = 0; i < order.size(); ++i) {
            HistoryRow *row = m_rows.value(order.at(i));
            if (!row)
                continue;
            m_rowsLayout->insertWidget(i, row);
            row->show();
        }
        m_rowsHost->setUpdatesEnabled(updates);
        m_order = order;
    }

    // ---- 页头与统计 ------------------------------------------------------
    ui->pageSubtitle->setText(
        tr("显示 %1 / %2 条记录").arg(order.size()).arg(history ? history->count() : 0));

    const QVariantMap stats = history ? history->statistics() : QVariantMap();
    if (m_cards.size() == 3) {
        m_cards[0]->setValue(QString::number(stats.value(QStringLiteral("total")).toInt()));
        m_cards[0]->setSecondary(tr("累计下载 %1").arg(
            FluentTheme::formatSize(stats.value(QStringLiteral("downloaded")).toDouble())));
        m_cards[1]->setValue(QString::number(stats.value(QStringLiteral("completed")).toInt()));
        m_cards[1]->setSecondary(tr("已上传 %1").arg(
            FluentTheme::formatSize(stats.value(QStringLiteral("uploaded")).toDouble())));
        m_cards[2]->setValue(QString::number(stats.value(QStringLiteral("failed")).toInt()));
        m_cards[2]->setSecondary(tr("已移除的记录不计入失败"));
    }

    // ---- 空状态 ----------------------------------------------------------
    const bool empty = order.isEmpty();
    const bool filtering = !m_searchText.isEmpty() || m_filterIndex != 0;
    m_scroll->setVisible(!empty);
    m_emptyState->setVisible(empty);
    m_emptyGlyph->setGlyph(filtering ? FluentTheme::Glyph::Filter : FluentTheme::Glyph::History);
    m_emptyGlyph->setFixedSize(72, 72);
    m_emptyTitle->setText(filtering ? tr("没有符合条件的记录") : tr("暂无下载历史"));
    m_emptyHint->setText(filtering ? tr("试试清空搜索框，或把筛选切换回“全部”。")
                                   : tr("完成或失败的任务会自动记录在这里，随时可以重新下载。"));
    m_emptyResetButton->setVisible(filtering);

    // 这里不调用 restyle()：配色只跟主题有关，行内样式由 setRecord() 按需刷新。
}

void HistoryPage::retranslate()
{
    // ---- 搜索框：占位文字随语言变化 --------------------------------------
    m_search->setPlaceholderText(tr("搜索名称、链接或目录"));

    // ---- 筛选下拉：重建条目后恢复原来的选择 ------------------------------
    if (m_filterCombo) {
        const int previous = m_filterCombo->currentIndex();
        m_filterCombo->blockSignals(true);
        m_filterCombo->clear();
        m_filterCombo->addItems({tr("全部"), tr("已完成"), tr("失败"), tr("已移除")});
        const int restored = (previous >= 0 && previous < m_filterCombo->count()) ? previous : 0;
        m_filterCombo->setCurrentIndex(restored);
        m_filterCombo->blockSignals(false);
        m_filterIndex = restored;
    }

    // ---- 统计卡片 --------------------------------------------------------
    const QStringList cardLabels = {tr("总条目"), tr("已完成"), tr("失败")};
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setLabel(cardLabels.value(i));

    // ---- 页头与空状态按钮 ------------------------------------------------
    m_refreshButton->setTooltipText(tr("重新读取历史记录"));
    m_emptyResetButton->setText(tr("显示全部"));
    m_emptyResetButton->setTooltipText(tr("清空搜索并回到“全部”筛选"));

    // ---- 行：按钮提示 + 行内文本 -----------------------------------------
    for (HistoryRow *row : std::as_const(m_rows))
        row->retranslate();

    refresh();   // 行内文本（状态、均速、时间）都带 tr()
}

void HistoryPage::restyle()
{
    const FluentTheme *t = FluentTheme::instance();

    // ---- 统计卡片强调色 --------------------------------------------------
    const QList<QColor> tints = {t->accent(), t->success(), t->critical()};
    for (StatCard *card : std::as_const(m_cards))
        card->setTint(tints.value(card->property("tintSlot").toInt(), t->accent()));

    ui->pageSubtitle->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));

    // ---- 空状态 ----------------------------------------------------------
    m_emptyPlate->setStyleSheet(
        QStringLiteral("QWidget#emptyPlate { background: %1; border: 1px solid %2;"
                       " border-radius: %3px; }")
            .arg(t->card().name(), t->strokeSubtle().name())
            .arg(FluentTheme::RadiusXLarge));
    m_emptyGlyph->setIconColor(t->textTertiary());
    m_emptyGlyph->setFixedSize(72, 72);
    m_emptyTitle->setStyleSheet(labelStyle(t->textSecondary(), 18, QFont::DemiBold));
    m_emptyHint->setStyleSheet(labelStyle(t->textTertiary(), 12));

    // ---- 行 --------------------------------------------------------------
    for (HistoryRow *row : std::as_const(m_rows))
        row->restyle();
}
