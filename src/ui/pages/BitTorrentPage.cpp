#include "ui/pages/BitTorrentPage.h"

#include "Aria2Manager.h"
#include "SettingsManager.h"
#include "TrackerSources.h"
#include "TorrentUtils.h"
#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentTheme.h"
#include "ui/FluentWidgets.h"
#include "ui/pages/ui_BitTorrentPage.h"

#include <QComboBox>
#include <functional>
#include <QCompleter>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStringListModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

class SourcePopup : public QFrame
{
public:
    explicit SourcePopup(QWidget *parent)
        : QFrame(parent, Qt::Popup)
    {
        setObjectName(QStringLiteral("sourcePopup"));
        setAttribute(Qt::WA_StyledBackground, true);
        // Rounded corners need the window itself to be translucent: the stylesheet
        // rounds the frame, but without this the square window corners show through.
        setAttribute(Qt::WA_TranslucentBackground, true);
        setWindowFlag(Qt::NoDropShadowWindowHint, false);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(6);

        m_list = new QWidget(this);
        m_listLayout = new QVBoxLayout(m_list);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(2);
        root->addWidget(m_list, 1);

        auto *custom = new QWidget(this);
        auto *customLayout = new QHBoxLayout(custom);
        customLayout->setContentsMargins(0, 0, 0, 0);
        customLayout->setSpacing(6);
        m_url = new FluentLineEdit(custom);
        m_url->setPlaceholderText(QStringLiteral("https://example.com/list.txt"));
        customLayout->addWidget(m_url, 1);
        m_add = new FluentButton(custom);
        m_add->setGlyph(FluentTheme::Glyph::Add);
        m_add->setIconOnly(true);
        m_add->setRole(FluentButton::Standard);
        customLayout->addWidget(m_add);
        root->addWidget(custom);

        auto submitCustom = [this]() {
            const QString text = m_url->text().trimmed();
            if (text.isEmpty() || !onCustom)
                return;
            hide();
            onCustom(text);
        };
        connect(m_add, &QPushButton::clicked, this, submitCustom);
        connect(m_url, &QLineEdit::returnPressed, this, submitCustom);

        restyle();
        connect(FluentTheme::instance(), &FluentTheme::changed, this, [this]() { restyle(); });
    }

    /// Rows for `wanted` kind of list, each marked subscribed or not; `filter` narrows
    /// them down and the popup shrinks to what is left.
    void rebuild(const QStringList &subscribed, const QString &filter, bool blacklistTab)
    {
        while (QLayoutItem *item = m_listLayout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        const QString needle = filter.trimmed().toLower();
        int rows = 0;
        for (const TrackerSource &source : builtInTrackerSources()) {
            if (source.blacklist != blacklistTab)
                continue;
            const QString haystack = (source.name + QLatin1Char(' ') + source.id).toLower();
            if (!needle.isEmpty() && !haystack.contains(needle))
                continue;
            const bool on = subscribed.contains(source.id);
            QStringList badges;
            badges << BitTorrentPage::tr("内置");
            if (source.cdn)
                badges << QStringLiteral("CDN");
            QString text = source.name + QStringLiteral("    ") + badges.join(QStringLiteral(" · "));
            if (on)
                text += QStringLiteral("    ✓");

            auto *row = new FluentButton(m_list);
            row->setText(text);
            row->setRole(FluentButton::Subtle);
            row->setCheckable(true);
            row->setChecked(on);
            row->setTooltipText(on ? BitTorrentPage::tr("点击取消订阅") : BitTorrentPage::tr("点击订阅并同步"));
            connect(row, &QPushButton::clicked, this, [this, id = source.id]() {
                hide();
                if (onPick)
                    onPick(id);
            });
            m_listLayout->addWidget(row);
            ++rows;
        }

        // Custom entries are listed too, otherwise the popup looks like it lost them.
        for (const QString &id : subscribed) {
            if (trackerSourceForId(id).url != id)
                continue;   // a built-in id: already covered above
            if (!needle.isEmpty() && !id.toLower().contains(needle))
                continue;
            auto *row = new FluentButton(m_list);
            row->setText(id + QStringLiteral("    ") + BitTorrentPage::tr("自定义") + QStringLiteral(" ✓"));
            row->setRole(FluentButton::Subtle);
            row->setCheckable(true);
            row->setChecked(true);
            row->setTooltipText(BitTorrentPage::tr("点击取消订阅"));
            connect(row, &QPushButton::clicked, this, [this, id]() {
                hide();
                if (onPick)
                    onPick(id);
            });
            m_listLayout->addWidget(row);
            ++rows;
        }

        if (rows == 0) {
            auto *none = new QLabel(BitTorrentPage::tr("没有匹配的列表，可在下面直接填入地址"), m_list);
            none->setProperty("fluentRole", "tertiary");
            m_listLayout->addWidget(none);
        }
        m_listLayout->addStretch(1);

        // Height follows the rows that survived the filter: with two matches the
        // popup is two rows tall, not as tall as it was for five.
        setFixedWidth(m_fieldWidth);
        setMaximumHeight(320);
        adjustSize();
        setFixedHeight(qMin(sizeHint().height(), 320));
    }

    /// The popup is as wide as the field it drops out of.
    void setFieldWidth(int width) { m_fieldWidth = qMax(240, width); }

    std::function<void(const QString &)> onPick;
    std::function<void(const QString &)> onCustom;

private:
    void restyle()
    {
        const FluentTheme *t = FluentTheme::instance();
        setStyleSheet(QStringLiteral("QFrame#sourcePopup { background: %1; border: 1px solid %2;"
                                     " border-radius: %3px; }")
                          .arg(t->cardSecondary().name(), t->strokeSubtle().name())
                          .arg(FluentTheme::RadiusMedium));
    }

    QWidget *m_list = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    FluentLineEdit *m_url = nullptr;
    FluentButton *m_add = nullptr;
    int m_fieldWidth = 440;
};

/**
 * A layout that lays widgets out in a row and wraps to the next line when they no
 * longer fit - what the source field needs, because the chips inside it must keep
 * their own size and the field has to grow downwards instead of squeezing them.
 */
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int margin = 0, int spacing = 6)
        : QLayout(parent)
    {
        setContentsMargins(margin, margin, margin, margin);
        setSpacing(spacing);
    }

    ~FlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }

    QLayoutItem *takeAt(int index) override
    {
        if (index < 0 || index >= m_items.size())
            return nullptr;
        return m_items.takeAt(index);
    }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }

    QSize sizeHint() const override { return minimumSize(); }

    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

private:
    /// Places the items; with `testOnly` it only reports the height they need.
    int doLayout(const QRect &rect, bool testOnly) const
    {
        const QMargins margins = contentsMargins();
        const QRect effective = rect.adjusted(margins.left(), margins.top(), -margins.right(),
                                              -margins.bottom());
        const int space = spacing();
        int x = effective.x();
        int y = effective.y();
        int lineHeight = 0;

        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            int next = x + hint.width() + space;
            if (next - space > effective.right() + 1 && lineHeight > 0) {
                x = effective.x();
                y += lineHeight + space;
                next = x + hint.width() + space;
                lineHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x = next;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> m_items;
};

namespace {


/**
 * The list that drops out of the source field.
 *
 * It is a Qt::Popup, not a dialog: it closes when you click elsewhere, takes the
 * keyboard while it is open and never behaves like a window - the same thing a
 * combo box does, which is what the field looks like. Rows are the published lists
 * with their badges, and the row of a list that is already subscribed carries a
 * tick; picking it again removes the subscription.
 */

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

QString clockText(qint64 seconds)
{
    if (seconds <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QTableWidgetItem *cell(const QString &text, const QColor &colour = QColor())
{
    auto *item = new QTableWidgetItem(text);
    if (colour.isValid())
        item->setForeground(colour);
    return item;
}

} // namespace

BitTorrentPage::BitTorrentPage(Aria2Manager *aria2, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::BitTorrentPage)
    , m_aria2(aria2)
{
    ui->setupUi(this);

    ui->injectCard->body()->addWidget(ui->injectBody);

    ui->pageTitle->setProperty("fluentRole", "title");
    ui->pageSubtitle->setProperty("fluentRole", "caption");
    ui->injectTitle->setProperty("fluentRole", "subtitle");
    ui->injectHint->setProperty("fluentRole", "tertiary");
    ui->statusLabel->setProperty("fluentRole", "tertiary");
    ui->trackerTable->setProperty("fluentRole", "table");

    buildTabs();
    buildSources();
    buildTable();
    wireManager();
    retranslate();
    setTab(Effective);
    refresh();
}

BitTorrentPage::~BitTorrentPage()
{
    delete ui;
}

void BitTorrentPage::buildTabs()
{
    m_effectiveTab = new FluentButton(this);
    m_effectiveTab->setText(tr("生效"));
    m_effectiveTab->setCheckable(true);
    m_effectiveTab->setRole(FluentButton::Subtle);
    ui->tabLayout->addWidget(m_effectiveTab);

    m_blacklistTab = new FluentButton(this);
    m_blacklistTab->setText(tr("黑名单"));
    m_blacklistTab->setCheckable(true);
    m_blacklistTab->setRole(FluentButton::Subtle);
    ui->tabLayout->addWidget(m_blacklistTab);

    m_removeButton = new FluentButton(this);
    m_removeButton->setGlyph(FluentTheme::Glyph::Close);
    m_removeButton->setText(tr("移除选中"));
    m_removeButton->setRole(FluentButton::Subtle);
    ui->tabLayout->addWidget(m_removeButton);

    ui->tabLayout->addStretch(1);

    connect(m_effectiveTab, &QPushButton::clicked, this, [this]() { setTab(Effective); });
    connect(m_blacklistTab, &QPushButton::clicked, this, [this]() { setTab(Blacklist); });
    connect(m_removeButton, &QPushButton::clicked, this, &BitTorrentPage::removeSelectedRow);

    m_syncButton = new FluentButton(this);
    m_syncButton->setGlyph(FluentTheme::Glyph::Refresh);
    m_syncButton->setText(tr("立即同步"));
    m_syncButton->setRole(FluentButton::Accent);
    ui->bottomLayout->addWidget(m_syncButton);
    connect(m_syncButton, &QPushButton::clicked, this, &BitTorrentPage::sync);

    m_countLabel = new QLabel(this);
    m_countLabel->setProperty("fluentRole", "tertiary");
    ui->bottomLayout->insertWidget(1, m_countLabel);
}

void BitTorrentPage::buildSources()
{
    m_chipsHost = ui->sourcesHost;

    // The field *is* the picker: clicking it drops the catalogue out of it, and what
    // is typed there filters that list (or, on Enter, becomes a custom entry). There
    // is no separate window - it is a popup, like a combo box's.
    // A plain QLineEdit on purpose: the *container* is the input box, so the text
    // field inside it must not paint a frame and focus underline of its own - that is
    // what made the chips look like they sat beside the field instead of inside it.
    m_sourceEdit = new QLineEdit(this);
    m_sourceEdit->setMinimumWidth(160);
    m_sourceEdit->setFrame(false);
    m_sourceEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
    m_sourceEdit->setPlaceholderText(tr("订阅源：点击选择内置列表，或填入网址 / 本地文件"));
    m_sourceEdit->installEventFilter(this);

    m_sourcePopup = new SourcePopup(this);
    m_sourcePopup->installEventFilter(this);
    m_sourcePopup->onPick = [this](const QString &id) {
        // Which list this lands in depends on the tab the popup was opened from:
        // picking a blacklist list while the blacklist tab was up used to subscribe it
        // as a source instead, so nothing ever appeared in that field.
        QStringList list = m_tab == Blacklist ? blacklist() : sources();
        if (list.contains(id))
            list.removeAll(id);   // picking a subscribed list again removes it
        else
            list << id;
        if (m_tab == Blacklist) {
            setBlacklist(list);
            rebuildChips();
        } else {
            setSources(list);
        }
    };
    m_sourcePopup->onCustom = [this](const QString &text) { addSource(text); };

    connect(m_sourceEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_sourcePopup->isVisible())
            m_sourcePopup->rebuild(m_tab == Blacklist ? blacklist() : sources(), text,
                                   m_tab == Blacklist);
    });
    connect(m_sourceEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_sourceEdit->text().trimmed();
        if (!text.isEmpty())
            addSource(text);
    });
    // The chips live inside the field, so it has to look like a field: the edit
    // itself is borderless and the container draws the frame.
    ui->sourcesHost->setObjectName(QStringLiteral("sourceField"));
    ui->sourcesHost->setAttribute(Qt::WA_StyledBackground, true);
    ui->sourcesHost->installEventFilter(this);
    ui->sourcesHost->setStyleSheet(sourceFieldStyle());
    m_sourceEdit->setFrame(false);
    m_sourceEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; }"));

    rebuildChips();
}

void BitTorrentPage::addSource(const QString &id)
{
    const QString text = id.trimmed();
    if (text.isEmpty() || !m_aria2)
        return;
    m_sourceEdit->clear();
    if (m_tab == Blacklist) {
        // In the blacklist tab the field adds an entry to block, not a source.
        QStringList list = blacklist();
        if (!list.contains(text))
            list << text;
        setBlacklist(list);
        return;
    }
    QStringList list = sources();
    if (!list.contains(text))
        list << text;
    setSources(list);
}

void BitTorrentPage::showSourcePopup()
{
    if (!m_sourcePopup || !m_sourceEdit)
        return;
    m_sourcePopup->rebuild(m_tab == Blacklist ? blacklist() : sources(), m_sourceEdit->text(),
                           m_tab == Blacklist);
    const QPoint below = ui->sourcesHost->mapToGlobal(QPoint(0, ui->sourcesHost->height() + 4));
    // As wide as the field it drops out of, not a width of its own.
    m_sourcePopup->setFixedWidth(qMax(240, ui->sourcesHost->width()));
    m_sourcePopup->move(below);
    m_sourcePopup->show();
}

bool BitTorrentPage::eventFilter(QObject *watched, QEvent *event)
{
    // Clicking the field (or the chips' container) toggles the list, the way a combo
    // box does: click once to drop it down, click again to put it away. Reacting to
    // the press instead of the focus is what makes it work the second time, when the
    // field already has the focus and no FocusIn event would arrive.
    if (watched == m_sourceEdit || watched == ui->sourcesHost) {
        if (event->type() == QEvent::MouseButtonPress) {
            if (m_sourcePopup && m_sourcePopup->isVisible())
                m_sourcePopup->hide();
            else
                showSourcePopup();
            return true;
        }
        if (event->type() == QEvent::FocusIn && m_sourcePopup && !m_sourcePopup->isVisible())
            showSourcePopup();
    }
    return QWidget::eventFilter(watched, event);
}

void BitTorrentPage::buildTable()
{
    QTableWidget *table = ui->trackerTable;
    table->setColumnCount(4);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->setAlternatingRowColors(false);
    connect(ui->filterEdit, &QLineEdit::textChanged, this, [this]() { rebuildTable(); });
    connect(ui->injectSwitch, &QAbstractButton::toggled, this, [this](bool on) {
        if (!m_aria2)
            return;
        m_aria2->settings()->setBtTrackerInject(on);
        applyEffectiveList();
    });
}

void BitTorrentPage::wireManager()
{
    if (m_aria2) {
        connect(m_aria2, &Aria2Manager::tasksChanged, this, &BitTorrentPage::refresh);
        connect(m_aria2, &Aria2Manager::globalOptionsChanged, this, &BitTorrentPage::refresh);
        connect(m_aria2, &Aria2Manager::trackerSubscriptionsChanged, this,
                &BitTorrentPage::refresh);
    }
    connect(FluentTheme::instance(), &FluentTheme::changed, this, &BitTorrentPage::restyle);
}

// ------------------------------------------------------------------- settings
QStringList BitTorrentPage::sources() const
{
    if (!m_aria2)
        return {};
    QStringList out;
    for (const QString &part : m_aria2->settings()
                                   ->btTrackerSources()
                                   .split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                          Qt::SkipEmptyParts)) {
        if (!out.contains(part))
            out << part;
    }
    return out;
}

void BitTorrentPage::setSources(const QStringList &list)
{
    if (!m_aria2)
        return;
    m_aria2->settings()->setBtTrackerSources(list.join(QLatin1Char(',')));
    rebuildChips();
    sync();
}

QStringList BitTorrentPage::blacklist() const
{
    return m_aria2 ? m_aria2->settings()->blacklistTrackers() : QStringList();
}

void BitTorrentPage::setBlacklist(const QStringList &list)
{
    if (!m_aria2)
        return;
    m_aria2->settings()->setBtTrackerBlacklist(list.join(QLatin1Char('\n')));
    applyEffectiveList();
}

// ----------------------------------------------------------------------- chips
void BitTorrentPage::rebuildChips()
{
    if (!m_flow) {
        // A flow layout inside the field: the chips wrap to a second line instead of
        // being squeezed, and the field grows downwards.
        m_flowHost = new QWidget(m_chipsHost);
        m_flow = new FlowLayout(m_flowHost, 4, 6);
        if (auto *hostLayout = qobject_cast<QHBoxLayout *>(m_chipsHost->layout()))
            hostLayout->addWidget(m_flowHost, 1);
    }
    QLayout *layout = m_flow;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            if (widget != m_sourceEdit) {
                widget->setParent(nullptr);
                widget->deleteLater();
            }
        }
        delete item;
    }

    if (m_tab == Effective) {
        for (const QString &id : sources()) {
            auto *chip = new FluentButton(m_chipsHost);
            chip->setText(trackerSourceLabel(id) + QStringLiteral("  ×"));
            chip->setRole(FluentButton::Standard);
            chip->setStyleSheet(chipStyle());
            chip->setTooltipText(tr("点击从订阅源中移除：%1").arg(id));
            connect(chip, &QPushButton::clicked, this, [this, id]() {
                QStringList list = sources();
                list.removeAll(id);
                setSources(list);
            });
            layout->addWidget(chip);
        }
    } else {
        // The blacklist tab shows what is blocked, not subscriptions: one chip per
        // entry would be a wall of chips, so it shows the count and clears them.
        const QStringList blocked = blacklist();
        if (!blocked.isEmpty()) {
            auto *chip = new FluentButton(m_chipsHost);
            chip->setText(tr("%1 条黑名单  ×").arg(blocked.size()));
            chip->setRole(FluentButton::Standard);
            chip->setStyleSheet(chipStyle());
            chip->setTooltipText(tr("清空黑名单"));
            connect(chip, &QPushButton::clicked, this, [this]() { setBlacklist({}); });
            layout->addWidget(chip);
        }
    }

    layout->addWidget(m_sourceEdit);
    // The hint only shows while nothing is picked: the field grows with its chips,
    // and a placeholder squeezed in beside them reads like one more chip.
    const bool any = m_tab == Effective ? !sources().isEmpty() : !blacklist().isEmpty();
    m_sourceEdit->setPlaceholderText(any ? QString()
                                         : (m_tab == Effective
                                                ? tr("璁㈤槄婧愶細鐐瑰嚮閫夋嫨鍐呯疆鍒楄〃锛屾垨濉叆缃戝潃 / 鏈湴鏂囦欢")
                                                : tr("榛戝悕鍗曪細濉叆瑕佸睆钄界殑 Tracker 鍦板潃")));
    m_sourceEdit->setMinimumWidth(any ? 120 : 260);
    m_flowHost->updateGeometry();
}

void BitTorrentPage::setTab(Tab tab)
{
    m_tab = tab;
    const bool effective = tab == Effective;
    m_effectiveTab->setChecked(effective);
    m_blacklistTab->setChecked(!effective);
    ui->injectCard->setVisible(effective);
    // The two tabs are two different fields: one takes lists to use, the other takes
    // trackers to avoid. Same widget, different question - and it says which.
    m_sourceEdit->setPlaceholderText(effective
                                         ? tr("订阅源：点击选择内置列表，或填入网址 / 本地文件")
                                         : tr("黑名单：填入要屏蔽的 Tracker 地址"));
    m_sourceEdit->clear();
    ui->sourcesHost->setStyleSheet(sourceFieldStyle());
    rebuildChips();
    rebuildTable();
}

QString BitTorrentPage::chipStyle() const
{
    // A chip has to read as a separate thing inside the field, so it gets its own
    // surface and a border rather than the field's fill.
    const FluentTheme *t = FluentTheme::instance();
    return QStringLiteral("QPushButton { background: %1; border: 1px solid %2;"
                          " border-radius: %3px; padding: 2px 8px; color: %4; }"
                          "QPushButton:hover { background: %5; }")
        .arg(t->cardTertiary().name(), t->strokeSubtle().name())
        .arg(FluentTheme::RadiusSmall)
        .arg(t->textPrimary().name(), t->controlFillHover().name());
}

QString BitTorrentPage::sourceFieldStyle() const
{
    const FluentTheme *t = FluentTheme::instance();
    return QStringLiteral("QWidget#sourceField { background: %1; border: 1px solid %2;"
                          " border-radius: %3px; }")
        .arg(t->controlFill().name(), t->strokeSubtle().name())
        .arg(FluentTheme::RadiusMedium);
}

// --------------------------------------------------------------------- syncing
void BitTorrentPage::sync()
{
    const QStringList list = sources();
    m_fetched.clear();
    m_fetchErrors.clear();
    m_pendingFetches = 0;

    for (const QString &id : list) {
        const TrackerSource source = trackerSourceForId(id);
        if (looksLikeLocalPath(source.url)) {
            QString path = source.url;
            if (path.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
                path = QUrl(path).toLocalFile();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                m_fetchErrors.insert(id, tr("无法读取"));
                continue;
            }
            const TrackerList::ParseResult parsed =
                TrackerList::parse(file.read(TrackerList::kMaxBytes + 1));
            if (parsed.binary)
                m_fetchErrors.insert(id, tr("不是文本文件"));
            else
                m_fetched.insert(id, parsed.trackers);
            continue;
        }
        fetchSource(id);
    }

    if (m_pendingFetches == 0)
        applyEffectiveList();
    else
        m_syncing = true;
    rebuildTable();
}

void BitTorrentPage::fetchSource(const QString &id)
{
    const TrackerSource source = trackerSourceForId(id);
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(source.url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);

    ++m_pendingFetches;
    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_fetchErrors.insert(id, reply->errorString());
        } else {
            const TrackerList::ParseResult parsed = TrackerList::parse(reply->readAll());
            if (parsed.binary)
                m_fetchErrors.insert(id, tr("返回的不是文本列表"));
            else
                m_fetched.insert(id, parsed.trackers);
        }
        if (--m_pendingFetches <= 0) {
            m_syncing = false;
            applyEffectiveList();
        }
        rebuildTable();
    });
}

void BitTorrentPage::applyEffectiveList()
{
    if (!m_aria2)
        return;

    // Everything the sources returned, in one list, before the blacklist is applied
    // (Aria2Manager::effectiveTrackers() is what subtracts it).
    QStringList merged;
    for (auto it = m_fetched.constBegin(); it != m_fetched.constEnd(); ++it) {
        for (const QString &tracker : it.value()) {
            if (!merged.contains(tracker))
                merged << tracker;
        }
    }
    m_aria2->setSubscriptionTrackers(merged);

    const int failed = m_fetchErrors.size();
    ui->statusLabel->setText(
        failed == 0
            ? tr("已同步 %1 个订阅源，共 %2 个 Tracker（生效 %3 个）")
                  .arg(m_fetched.size())
                  .arg(merged.size())
                  .arg(m_aria2->effectiveTrackers().size())
            : tr("已同步 %1 个订阅源（%2 个失败），共 %3 个 Tracker")
                  .arg(m_fetched.size())
                  .arg(failed)
                  .arg(merged.size()));
}

// ---------------------------------------------------------------------- tables
void BitTorrentPage::rebuildTable()
{
    QTableWidget *table = ui->trackerTable;
    const QString needle = ui->filterEdit->text().trimmed().toLower();
    const QVariantMap health = m_aria2 ? m_aria2->trackerHealth() : QVariantMap();
    const QStringList blocked = blacklist();

    // What the *effective* tab lists: the manual entries, then everything a source
    // returned, then - with nothing configured - the built-in fallback, because that
    // is what the engine is actually using.
    QList<QPair<QString, QString>> rows;   // url, source label
    QStringList manual;
    if (m_aria2) {
        for (const QString &part : m_aria2->settings()
                                       ->btTracker()
                                       .split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                              Qt::SkipEmptyParts))
            manual << part;
    }

    if (m_tab == Effective) {
        for (const QString &url : manual)
            rows << qMakePair(url, tr("手动"));
        for (const QString &id : sources()) {
            const QString label = trackerSourceLabel(id);
            for (const QString &url : m_fetched.value(id)) {
                bool seen = false;
                for (const auto &row : rows) {
                    if (row.first == url) {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                    rows << qMakePair(url, label);
            }
        }
        if (rows.isEmpty() && m_aria2) {
            for (const QString &url : m_aria2->settings()->builtInTrackers())
                rows << qMakePair(url, tr("内置"));
        }
    } else {
        for (const QString &url : blocked)
            rows << qMakePair(url, tr("手动"));
        for (const QString &id : sources()) {
            const TrackerSource source = trackerSourceForId(id);
            if (!source.blacklist)
                continue;
            for (const QString &url : m_fetched.value(id))
                rows << qMakePair(url, trackerSourceLabel(id));
        }
        for (auto it = m_fetchErrors.constBegin(); it != m_fetchErrors.constEnd(); ++it)
            rows << qMakePair(it.key(), tr("订阅源获取失败：%1").arg(it.value()));
    }

    const bool effectiveTab = m_tab == Effective;
    table->setColumnCount(effectiveTab ? 4 : 2);
    QStringList headers;
    headers << tr("URL");
    if (effectiveTab)
        headers << tr("健康度") << tr("最后探测");
    headers << tr("来源");
    table->setHorizontalHeaderLabels(headers);

    table->setRowCount(0);
    table->setSortingEnabled(false);
    for (const auto &row : rows) {
        if (!needle.isEmpty() && !row.first.toLower().contains(needle))
            continue;
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, cell(row.first));
        if (effectiveTab) {
            const QVariantMap entry = health.value(row.first).toMap();
            const bool known = !entry.isEmpty();
            const bool ok = entry.value(QStringLiteral("ok")).toBool();
            const QColor tint = !known ? FluentTheme::instance()->textTertiary()
                                       : (ok ? FluentTheme::instance()->success()
                                             : FluentTheme::instance()->critical());
            table->setItem(r, 1,
                           cell(known ? (ok ? tr("● 可用") : tr("● 失败")) : tr("— 未知"), tint));
            table->setItem(r, 2,
                           cell(clockText(entry.value(QStringLiteral("lastSeen")).toLongLong())));
        }
        table->setItem(r, effectiveTab ? 3 : 1, cell(row.second));
    }

    const int shown = table->rowCount();
    const int total = rows.size();
    m_countLabel->setText(effectiveTab
                              ? tr("%1 / %2 个 Tracker（生效 %3 个）")
                                    .arg(shown)
                                    .arg(total)
                                    .arg(m_aria2 ? m_aria2->effectiveTrackers().size() : 0)
                              : tr("%1 / %2 条黑名单").arg(shown).arg(total));
    m_removeButton->setEnabled(shown > 0);
}

void BitTorrentPage::removeSelectedRow()
{
    QTableWidget *table = ui->trackerTable;
    QStringList urls;
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    for (QTableWidgetItem *item : selected) {
        if (item->column() != 0)
            continue;
        const QString url = item->text();
        if (!url.isEmpty() && !urls.contains(url))
            urls << url;
    }
    if (urls.isEmpty())
        return;

    if (m_tab == Effective) {
        // A row from a source cannot be deleted here - the source owns it - so the
        // removal goes to the blacklist, which is the list that means "never use
        // this", and the row disappears from 生效 on the next rebuild.
        QStringList list = blacklist();
        for (const QString &url : urls) {
            if (!list.contains(url))
                list << url;
        }
        setBlacklist(list);
        emit toast(tr("已加入黑名单：%1 条").arg(urls.size()), false);
        return;
    }

    QStringList list = blacklist();
    for (const QString &url : urls)
        list.removeAll(url);
    setBlacklist(list);
}

// --------------------------------------------------------------- housekeeping
void BitTorrentPage::refresh()
{
    if (m_aria2) {
        const QSignalBlocker blocker(ui->injectSwitch);
        ui->injectSwitch->setChecked(m_aria2->settings()->btTrackerInject());
    }
    rebuildTable();
    restyle();
}

void BitTorrentPage::restyle()
{
    // The field and its chips carry their colours in a stylesheet, so a theme change
    // has to re-apply them; without this the old colours stay until something else
    // rebuilds the row (switching tabs, for instance).
    if (ui->sourcesHost)
        ui->sourcesHost->setStyleSheet(sourceFieldStyle());
    rebuildChips();

    const FluentTheme *t = FluentTheme::instance();
    ui->pageSubtitle->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                        .arg(t->textTertiary().name()));
    ui->injectHint->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                      .arg(t->textTertiary().name()));
    ui->statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                       .arg(t->textTertiary().name()));
    ui->trackerTable->setStyleSheet(
        QStringLiteral("QTableWidget { background: %1; border: 1px solid %2;"
                       "              border-radius: %3px; }"
                       "QHeaderView::section { background: transparent; border: none;"
                       "              color: %4; padding: 6px 8px; }"
                       "QTableWidget::item { color: %5; padding: 4px 8px; }"
                       "QTableWidget::item:selected { background: %6; }")
            .arg(t->card().name(), t->strokeSubtle().name())
            .arg(FluentTheme::RadiusMedium)
            .arg(t->textTertiary().name(), t->textPrimary().name(),
                 QColor(t->accent().red(), t->accent().green(), t->accent().blue(), 60)
                     .name(QColor::HexArgb)));
}

void BitTorrentPage::retranslate()
{
    m_effectiveTab->setText(tr("生效"));
    m_blacklistTab->setText(tr("黑名单"));
    m_removeButton->setText(tr("移除选中"));
    m_syncButton->setText(tr("立即同步"));
    ui->injectTitle->setText(tr("注入精选 Tracker"));
    ui->injectHint->setText(tr("将选中的订阅源合并后追加到所有 BT 任务"));
    ui->filterEdit->setPlaceholderText(tr("按 URL 过滤…"));
    if (m_sourceEdit)
        m_sourceEdit->setPlaceholderText(tr("订阅源：点击选择内置列表，或填入网址 / 本地文件"));
    rebuildTable();
}

void BitTorrentPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslate();
    }
}
