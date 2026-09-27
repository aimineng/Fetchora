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

    /// Rows for the catalogue, each marked subscribed or not; `filter` narrows them.
    void rebuild(const QStringList &subscribed, const QString &filter)
    {
        while (QLayoutItem *item = m_listLayout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        const QString needle = filter.trimmed().toLower();
        for (const TrackerSource &source : builtInTrackerSources()) {
            const QString haystack = (source.name + QLatin1Char(' ') + source.id).toLower();
            if (!needle.isEmpty() && !haystack.contains(needle))
                continue;
            const bool on = subscribed.contains(source.id);
            QStringList badges;
            badges << QObject::tr("内置");
            if (source.cdn)
                badges << QStringLiteral("CDN");
            if (source.blacklist)
                badges << QObject::tr("黑名单");
            QString text = source.name + QStringLiteral("    ")
                + badges.join(QStringLiteral(" · "));
            if (on)
                text += QStringLiteral("    ✓");

            auto *row = new FluentButton(m_list);
            row->setText(text);
            row->setRole(FluentButton::Subtle);
            row->setCheckable(true);
            row->setChecked(on);
            row->setTooltipText(on ? QObject::tr("点击取消订阅") : QObject::tr("点击订阅并同步"));
            connect(row, &QPushButton::clicked, this, [this, id = source.id]() {
                hide();
                if (onPick)
                    onPick(id);
            });
            m_listLayout->addWidget(row);
        }

        // A custom entry that is already configured still deserves a row, otherwise
        // the popup looks like it lost it.
        for (const QString &id : subscribed) {
            if (trackerSourceForId(id).url != id)
                continue;   // a built-in id: already listed above
            if (!needle.isEmpty() && !id.toLower().contains(needle))
                continue;
            auto *row = new FluentButton(m_list);
            row->setText(id + QStringLiteral("    ") + QObject::tr("自定义") + QStringLiteral(" ✓"));
            row->setRole(FluentButton::Subtle);
            row->setCheckable(true);
            row->setChecked(true);
            row->setTooltipText(QObject::tr("点击取消订阅"));
            connect(row, &QPushButton::clicked, this, [this, id]() {
                hide();
                if (onPick)
                    onPick(id);
            });
            m_listLayout->addWidget(row);
        }
        m_listLayout->addStretch(1);
        setFixedWidth(440);
    }

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
    m_sourceEdit = new FluentLineEdit(this);
    m_sourceEdit->setMinimumWidth(320);
    m_sourceEdit->setPlaceholderText(tr("订阅源：点击选择内置列表，或填入网址 / 本地文件"));
    m_sourceEdit->installEventFilter(this);

    m_sourcePopup = new SourcePopup(this);
    m_sourcePopup->onPick = [this](const QString &id) {
        QStringList list = sources();
        if (list.contains(id))
            list.removeAll(id);   // picking a subscribed list again removes it
        else
            list << id;
        setSources(list);
    };
    m_sourcePopup->onCustom = [this](const QString &text) { addSource(text); };

    connect(m_sourceEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (m_sourcePopup->isVisible())
            m_sourcePopup->rebuild(sources(), text);
    });
    connect(m_sourceEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_sourceEdit->text().trimmed();
        if (!text.isEmpty())
            addSource(text);
    });

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
    m_sourcePopup->rebuild(sources(), m_sourceEdit->text());
    m_sourcePopup->adjustSize();
    const QPoint below = m_sourceEdit->mapToGlobal(QPoint(0, m_sourceEdit->height() + 4));
    m_sourcePopup->move(below);
    m_sourcePopup->show();
}

bool BitTorrentPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_sourceEdit) {
        // Mouse press rather than focus-in: clicking the field again should re-open
        // the list even when it already has the focus.
        if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::FocusIn) {
            showSourcePopup();
            if (event->type() == QEvent::MouseButtonPress
                && m_sourcePopup->isVisible())
                return true;   // the popup takes it from here
        }
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
    auto *layout = qobject_cast<QHBoxLayout *>(m_chipsHost->layout());
    if (!layout)
        return;

    // The chip row and the field are rebuilt together: the chips are what the user
    // sees of the setting, the field is how another one gets in.
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            if (widget != m_sourceEdit)
                widget->deleteLater();
        }
        delete item;
    }

    if (m_tab == Effective) {
        for (const QString &id : sources()) {
            auto *chip = new FluentButton(m_chipsHost);
            chip->setText(trackerSourceLabel(id) + QStringLiteral("  ×"));
            chip->setRole(FluentButton::Subtle);
            chip->setTooltipText(tr("点击从订阅源中移除：%1").arg(id));
            connect(chip, &QPushButton::clicked, this, [this, id]() {
                QStringList list = sources();
                list.removeAll(id);
                setSources(list);
            });
            layout->addWidget(chip);
        }
    } else {
        auto *chip = new FluentButton(m_chipsHost);
        chip->setText(tr("黑名单条目  ×"));
        chip->setRole(FluentButton::Subtle);
        chip->setTooltipText(tr("清空黑名单（当前 %1 条）").arg(blacklist().size()));
        connect(chip, &QPushButton::clicked, this, [this]() { setBlacklist({}); });
        layout->addWidget(chip);
    }

    layout->addWidget(m_sourceEdit, 1);
}

void BitTorrentPage::setTab(Tab tab)
{
    m_tab = tab;
    const bool effective = tab == Effective;
    m_effectiveTab->setChecked(effective);
    m_blacklistTab->setChecked(!effective);
    ui->injectCard->setVisible(effective);
    rebuildChips();
    rebuildTable();
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
