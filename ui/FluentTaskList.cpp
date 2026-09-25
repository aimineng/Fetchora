#include "ui/FluentTaskList.h"

#include "ui/FluentButton.h"
#include "ui/FluentInputs.h"
#include "ui/FluentWidgets.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>

namespace {

QString num(qint64 v) { return QString::number(v); }

} // namespace

// ============================================================================
//  FluentTaskCard
// ============================================================================
FluentTaskCard::FluentTaskCard(const QString &gid, QWidget *parent)
    : QFrame(parent)
    , m_gid(gid)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    // Width comes from the list layout, height from the content.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(280);
    setFixedHeight(preferredHeight());

    m_stateGlyph = new FluentIcon(this);
    m_stateGlyph->setIconSize(18);
    m_stateGlyph->setFixedSize(40, 40);
    m_stateGlyph->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_name = new QLabel(this);
    QFont nf = FluentTheme::uiFont(14);
    m_name->setFont(nf);
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_meta = new QLabel(this);
    m_meta->setFont(FluentTheme::uiFont(12));
    m_meta->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_speed = new QLabel(this);
    QFont sf = FluentTheme::uiFont(14);
    sf.setWeight(QFont::DemiBold);
    m_speed->setFont(sf);
    m_speed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_speed->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_eta = new QLabel(this);
    m_eta->setFont(FluentTheme::uiFont(11));
    m_eta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_eta->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_percent = new QLabel(this);
    m_percent->setFont(FluentTheme::uiFont(11));
    m_percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_percent->setFixedWidth(44);
    m_percent->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_progress = new FluentProgressBar(this);
    m_progress->setBarHeight(4);
    m_progress->setAttribute(Qt::WA_TransparentForMouseEvents);

    buildActions();
}

void FluentTaskCard::buildActions()
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

    m_pauseButton = makeButton(FluentTheme::Glyph::Pause, tr("暂停"));
    m_retryButton = makeButton(FluentTheme::Glyph::Refresh, tr("重试"));
    m_openButton = makeButton(FluentTheme::Glyph::OpenFile, tr("打开文件"));
    m_folderButton = makeButton(FluentTheme::Glyph::Folder, tr("打开所在文件夹"));
    m_copyButton = makeButton(FluentTheme::Glyph::Copy, tr("复制链接"));
    m_removeButton = makeButton(FluentTheme::Glyph::Delete, tr("移除"));

    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        const QString status = m_task.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("active"))
            emit pauseRequested(m_gid);
        else
            emit resumeRequested(m_gid);
    });
    connect(m_retryButton, &QPushButton::clicked, this, [this]() { emit resumeRequested(m_gid); });
    connect(m_openButton, &QPushButton::clicked, this, [this]() { emit openRequested(m_gid); });
    connect(m_folderButton, &QPushButton::clicked, this, [this]() { emit folderRequested(m_gid); });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() { emit copyLinkRequested(m_gid); });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() { emit removeRequested(m_gid); });

    m_actionBar->hide();
}

QChar FluentTaskCard::plateGlyph() const
{
    if (m_task.value(QStringLiteral("isTorrent")).toBool())
        return FluentTheme::Glyph::Torrent;
    return FluentTheme::fileGlyph(m_task.value(QStringLiteral("fileName")).toString());
}

QColor FluentTaskCard::statusTint() const
{
    return FluentTheme::instance()->statusColor(m_task.value(QStringLiteral("status")).toString());
}

QString FluentTaskCard::metaLine() const
{
    const FluentTheme *t = FluentTheme::instance();
    const QString status = m_task.value(QStringLiteral("status")).toString();
    const qint64 done = m_task.value(QStringLiteral("completedLength")).toLongLong();
    const qint64 total = m_task.value(QStringLiteral("totalLength")).toLongLong();
    const int files = m_task.value(QStringLiteral("fileCount")).toInt();
    const int conns = m_task.value(QStringLiteral("connections")).toInt();
    const int seeders = m_task.value(QStringLiteral("numSeeders")).toInt();
    QStringList bits;

    if (status == QLatin1String("error")) {
        const QString err = m_task.value(QStringLiteral("errorMessage")).toString();
        bits << (err.isEmpty() ? tr("未知错误") : err);
    } else if (status == QLatin1String("complete")) {
        bits << t->formatSize(total);
        const qint64 uploaded = m_task.value(QStringLiteral("uploadLength")).toLongLong();
        if (uploaded > 0)
            bits << tr("已做种 %1").arg(t->formatSize(uploaded));
    } else {
        bits << (t->formatSize(done) + QStringLiteral(" / ") + t->formatSize(total));
        if (files > 1)
            bits << tr("%1 个文件").arg(files);
        if (conns > 0)
            bits << tr("%1 连接").arg(conns);
        if (seeders > 0)
            bits << tr("%1 种子").arg(seeders);
    }
    return bits.join(QStringLiteral("  ·  "));
}

int FluentTaskCard::preferredHeight() const
{
    const bool torrent = m_task.value(QStringLiteral("isTorrent")).toBool()
        && m_task.value(QStringLiteral("hasMetadata")).toBool();
    return torrent ? 92 : 78;
}

void FluentTaskCard::relayout()
{
    const int pad = 16;
    const int plate = 40;
    const int cy = height() / 2;

    m_stateGlyph->setGeometry(pad, cy - plate / 2, plate, plate);

    // Right-hand columns are fixed width; the text block takes the rest.
    const int actionsW = m_actionsVisible ? m_actionBar->sizeHint().width() + 12 : 0;
    const int infoW = 108;
    const int percentW = 54;

    const int textX = pad + plate + 14;
    const int textW = qMax(120, width() - textX - infoW - percentW - actionsW - pad - 12);

    m_name->setGeometry(textX, cy - 26, textW, 20);
    m_meta->setGeometry(textX, cy - 6, textW, 17);
    m_progress->setGeometry(textX, cy + 15, qMax(60, textW - percentW), 5);
    m_percent->setGeometry(textX + textW - percentW, cy + 11, percentW, 16);

    const int infoX = width() - actionsW - pad - infoW;
    m_speed->setGeometry(infoX, cy - 22, infoW, 20);
    m_eta->setGeometry(infoX, cy + 1, infoW, 16);

    if (m_actionsVisible) {
        const QSize s = m_actionBar->sizeHint();
        m_actionBar->setGeometry(width() - pad - s.width(), cy - s.height() / 2, s.width(), s.height());
    }
}

void FluentTaskCard::updateTask(const QVariantMap &task)
{
    m_task = task;
    const FluentTheme *t = FluentTheme::instance();

    const QString status = task.value(QStringLiteral("status")).toString();
    const bool isError = status == QLatin1String("error");
    const bool isActive = status == QLatin1String("active");
    const bool isPaused = status == QLatin1String("paused");
    const bool isWaiting = status == QLatin1String("waiting");
    const bool isDone = status == QLatin1String("complete");

    // ---- text ------------------------------------------------------------
    QString name = task.value(QStringLiteral("fileName")).toString();
    if (name.isEmpty())
        name = tr("正在获取元数据…");
    if (m_name->text() != name)
        m_name->setText(name);

    const QString meta = metaLine();
    if (m_meta->text() != meta) {
        m_meta->setText(meta);
        m_meta->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                                  .arg(isError ? t->critical().name() : t->textTertiary().name()));
    }

    // ---- progress --------------------------------------------------------
    const double progress = task.value(QStringLiteral("progress")).toDouble();
    const qint64 total = task.value(QStringLiteral("totalLength")).toLongLong();
    m_progress->setValue(progress);
    m_progress->setBarColor(statusTint());
    m_progress->setShowSegments(task.value(QStringLiteral("isTorrent")).toBool() && total > 0);
    m_progress->setIndeterminate(isActive && total <= 0);
    m_percent->setText(total > 0 ? QStringLiteral("%1%").arg(int(progress))
                                 : (isDone ? QStringLiteral("100%") : QStringLiteral("--")));
    m_percent->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->textSecondary().name()));

    // ---- speed / eta -----------------------------------------------------
    const qint64 speed = task.value(QStringLiteral("downloadSpeed")).toLongLong();
    const qint64 upload = task.value(QStringLiteral("uploadSpeed")).toLongLong();
    const double eta = task.value(QStringLiteral("eta")).toDouble();
    QString speedText = QStringLiteral("--");
    if (isActive)
        speedText = t->formatSpeed(speed);
    else if (upload > 0)
        speedText = QStringLiteral("↑ ") + t->formatSpeed(upload);
    else if (isDone)
        speedText = t->formatSize(total);
    m_speed->setText(speedText);
    m_speed->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                               .arg(isActive ? t->accent().name() : t->textSecondary().name()));

    QString etaText;
    if (isActive && eta > 0)
        etaText = tr("剩余 %1").arg(t->formatDuration(eta));
    else if (upload > 0)
        etaText = QStringLiteral("↑ ") + t->formatSpeed(upload);
    m_eta->setText(etaText);
    m_eta->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->textTertiary().name()));

    // ---- state plate -----------------------------------------------------
    m_stateGlyph->setGlyph(plateGlyph());
    m_stateGlyph->setIconColor(statusTint());
    m_stateGlyph->setStyleSheet(
        QStringLiteral("QLabel { font-family: \"%1\"; font-size: 18px; color: %2;"
                       " background: %3; border-radius: %4px; }")
            .arg(FluentTheme::iconFont())
            .arg(statusTint().name())
            .arg(QColor(statusTint().red(), statusTint().green(), statusTint().blue(),
                        t->isDark() ? 51 : 33)
                     .name(QColor::HexArgb))
            .arg(FluentTheme::RadiusMedium));
    m_stateGlyph->raise();

    // ---- actions ---------------------------------------------------------
    m_pauseButton->setVisible(isActive || isPaused || isWaiting);
    m_pauseButton->setGlyph(isActive ? FluentTheme::Glyph::Pause : FluentTheme::Glyph::Play);
    m_retryButton->setVisible(isError);
    m_openButton->setVisible(isDone);
    m_actionBar->adjustSize();
    setFixedHeight(preferredHeight());
    relayout();
    update();
}

void FluentTaskCard::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    relayout();
}

void FluentTaskCard::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void FluentTaskCard::enterEvent(QEnterEvent *event)
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

void FluentTaskCard::leaveEvent(QEvent *event)
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

void FluentTaskCard::mouseReleaseEvent(QMouseEvent *event)
{
    QFrame::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked(m_gid);
}

void FluentTaskCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    QFrame::mouseDoubleClickEvent(event);
    if (event->button() == Qt::LeftButton)
        emit doubleClicked(m_gid);
}

bool FluentTaskCard::eventFilter(QObject *, QEvent *)
{
    return false;
}

void FluentTaskCard::paintEvent(QPaintEvent *)
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

    QColor border = m_selected
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
    if (m_stateGlyph) {
        const QRect g = m_stateGlyph->geometry();
        p.setBrush(QColor(statusTint().red(), statusTint().green(), statusTint().blue(),
                          t->isDark() ? 51 : 33));
        p.drawRoundedRect(g, FluentTheme::RadiusMedium, FluentTheme::RadiusMedium);
    }
}

// ============================================================================
//  FluentTaskList
// ============================================================================
FluentTaskList::FluentTaskList(QWidget *parent)
    : QWidget(parent)
    , m_emptyTitleText(tr("还没有下载任务"))
    , m_emptyHintText(tr("点击“新建”或直接把链接粘贴进来"))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_container = new QWidget(m_scroll);
    m_container->setObjectName(QStringLiteral("taskContainer"));
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(0, 0, 8, 0);
    m_layout->setSpacing(8);

    m_emptyState = new QWidget(m_container);
    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(0, 60, 0, 0);
    emptyLayout->setSpacing(10);
    m_emptyIcon = new FluentIcon(FluentTheme::Glyph::Download, 30, m_emptyState);
    m_emptyIcon->useTertiaryColor();
    m_emptyIcon->setFixedSize(72, 72);
    restyleEmptyState();
    m_emptyTitle = new QLabel(m_emptyTitleText, m_emptyState);
    QFont etf = FluentTheme::uiFont(18);
    m_emptyTitle->setFont(etf);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyHint = new QLabel(m_emptyHintText, m_emptyState);
    m_emptyHint->setFont(FluentTheme::uiFont(12));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIcon, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(m_emptyTitle);
    emptyLayout->addWidget(m_emptyHint);
    emptyLayout->addStretch(1);

    m_layout->addWidget(m_emptyState);
    m_layout->addStretch(1);
    m_scroll->setWidget(m_container);
    outer->addWidget(m_scroll);

    connect(FluentTheme::instance(), &FluentTheme::changed, this, &FluentTaskList::restyleEmptyState);
}

void FluentTaskList::restyleEmptyState()
{
    if (!m_emptyIcon)
        return;
    const FluentTheme *t = FluentTheme::instance();
    m_emptyIcon->setStyleSheet(
        QStringLiteral("QLabel { font-family: \"%1\"; font-size: 30px; color: %2;"
                       " background: %3; border-radius: %4px; }")
            .arg(FluentTheme::iconFont())
            .arg(t->textTertiary().name())
            .arg(t->card().name())
            .arg(FluentTheme::RadiusXLarge));
}

void FluentTaskList::setTasks(const QVariantList &tasks)
{
    // ---- 1. flatten the model -------------------------------------------
    QStringList newOrder;
    QList<QVariantMap> maps;
    for (const QVariant &v : tasks) {
        const QVariantMap t = v.toMap();
        const QString gid = t.value(QStringLiteral("gid")).toString();
        if (gid.isEmpty())
            continue;
        newOrder << gid;
        maps << t;
    }

    // ---- 2. retire cards whose task disappeared --------------------------
    const QSet<QString> live(newOrder.begin(), newOrder.end());
    for (auto it = m_cards.begin(); it != m_cards.end();) {
        if (live.contains(it.key())) {
            ++it;
            continue;
        }
        FluentTaskCard *dead = it.value();
        m_layout->removeWidget(dead);   // NOTE: removeWidget() hides the widget
        dead->deleteLater();
        it = m_cards.erase(it);
    }

    // ---- 3. create the missing cards -------------------------------------
    for (const QVariantMap &t : std::as_const(maps)) {
        const QString gid = t.value(QStringLiteral("gid")).toString();
        if (m_cards.contains(gid))
            continue;
        auto *card = new FluentTaskCard(gid, m_container);
        connect(card, &FluentTaskCard::clicked, this, [this](const QString &g) {
            setSelectedGid(g);
        });
        connect(card, &FluentTaskCard::doubleClicked, this, [this](const QString &g) {
            setSelectedGid(g);
            FluentTaskCard *c = m_cards.value(g);
            const QString status = c ? c->task().value(QStringLiteral("status")).toString()
                                     : QString();
            if (status == QLatin1String("complete"))
                emit openRequested(g);
            else if (status == QLatin1String("active"))
                emit pauseRequested(g);
            else
                emit resumeRequested(g);
        });
        connect(card, &FluentTaskCard::pauseRequested, this, &FluentTaskList::pauseRequested);
        connect(card, &FluentTaskCard::resumeRequested, this, &FluentTaskList::resumeRequested);
        connect(card, &FluentTaskCard::openRequested, this, &FluentTaskList::openRequested);
        connect(card, &FluentTaskCard::folderRequested, this, &FluentTaskList::folderRequested);
        connect(card, &FluentTaskCard::removeRequested, this, &FluentTaskList::removeRequested);
        connect(card, &FluentTaskCard::copyLinkRequested, this, &FluentTaskList::copyLinkRequested);
        m_cards.insert(gid, card);
        // Insert *before* the empty state / trailing stretch pair.
        m_layout->insertWidget(m_layout->count() - 2, card);
        card->show();
    }

    // ---- 4. refresh the content in place ---------------------------------
    for (const QVariantMap &t : std::as_const(maps)) {
        if (FluentTaskCard *card = m_cards.value(t.value(QStringLiteral("gid")).toString()))
            card->updateTask(t);
    }

    // ---- 5. re-order only when the model order actually changed ----------
    // QLayout::removeWidget() hides the widget it detaches, so every card has
    // to be shown again after being put back.
    if (newOrder != m_order) {
        const bool updates = m_container->updatesEnabled();
        m_container->setUpdatesEnabled(false);
        for (FluentTaskCard *card : std::as_const(m_cards))
            m_layout->removeWidget(card);
        for (int i = 0; i < newOrder.size(); ++i) {
            FluentTaskCard *card = m_cards.value(newOrder.at(i));
            if (!card)
                continue;
            m_layout->insertWidget(i, card);
            card->show();
        }
        m_container->setUpdatesEnabled(updates);
        m_order = newOrder;
    }

    m_emptyState->setVisible(m_cards.isEmpty());
    if (!m_cards.isEmpty() && !m_selectedGid.isEmpty() && !m_cards.contains(m_selectedGid)) {
        m_selectedGid.clear();
        emit selectionChanged(QString());
    }
}

void FluentTaskList::setSelectedGid(const QString &gid)
{
    if (m_selectedGid == gid)
        return;
    if (FluentTaskCard *old = m_cards.value(m_selectedGid))
        old->setSelected(false);
    m_selectedGid = gid;
    if (FluentTaskCard *card = m_cards.value(gid))
        card->setSelected(true);
    emit selectionChanged(gid);
}

void FluentTaskList::setEmptyStateText(const QString &title, const QString &hint)
{
    m_emptyTitleText = title;
    m_emptyHintText = hint;
    rebuildEmptyState();
}

void FluentTaskList::rebuildEmptyState()
{
    m_emptyTitle->setText(m_emptyTitleText);
    m_emptyHint->setText(m_emptyHintText);
}

void FluentTaskList::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The cards stretch with the viewport; nothing to do but repaint the
    // rounded corners at the new width.
    for (FluentTaskCard *card : std::as_const(m_cards))
        card->relayout();
}
