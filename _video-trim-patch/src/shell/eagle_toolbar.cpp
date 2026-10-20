#include "shell/eagle_toolbar.hpp"
#include "shell/plugin_glyph.hpp"
#include "automation/app_input_scope.hpp"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QCryptographicHash>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSettings>
#include <QSpacerItem>
#include <QStandardItemModel>
#include <QSplitter>
#include <QStyleOptionToolButton>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <cstring>
#include <Windows.h>

namespace billfish::shell {
namespace {
enum class Glyph { Sidebar, Plugin, Layout, Filter, Pin, Add, Ascending, Descending };

bool belongsToWidget(const QWidget* root, const QWidget* widget) {
    for (auto* ancestor = widget; ancestor; ancestor = ancestor->parentWidget())
        if (ancestor == root) return true;
    return false;
}

class LineButton final : public QToolButton {
public:
    explicit LineButton(Glyph glyph, QWidget* parent = nullptr) : QToolButton(parent), glyph_(glyph) {
        setFixedSize(32, 32);
        setIconSize(QSize(24, 24));
        setAutoRaise(true);
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setFocusPolicy(Qt::NoFocus);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor background;
        if (isDown()) background = QColor(177, 191, 211, 42);
        else if (isChecked()) background = QColor(132, 150, 175, 34);
        else if (underMouse()) background = QColor(205, 213, 224, 20);
        if (background.isValid()) {
            painter.setPen(Qt::NoPen); painter.setBrush(background);
            painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
        }
        QColor color(isChecked() ? QColor(231, 237, 245) : QColor(210, 215, 223));
        if (!isEnabled()) color.setAlphaF(0.3);
        else if (!underMouse() && !isChecked()) color.setAlphaF(0.8);
        painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.translate((width() - 24) / 2.0, (height() - 24) / 2.0);
        switch (glyph_) {
        case Glyph::Ascending:
        case Glyph::Descending: {
            painter.drawLine(QPointF(5, 6), QPointF(14, 6));
            painter.drawLine(QPointF(5, 11), QPointF(12, 11));
            painter.drawLine(QPointF(5, 16), QPointF(10, 16));
            painter.drawLine(QPointF(18, 5), QPointF(18, 19));
            const bool up = glyph_ == Glyph::Ascending;
            const qreal y = up ? 5 : 19;
            painter.drawLine(QPointF(15, up ? 8 : 16), QPointF(18, y));
            painter.drawLine(QPointF(21, up ? 8 : 16), QPointF(18, y));
            break;
        }
        case Glyph::Add:
            painter.drawLine(QPointF(5, 12), QPointF(19, 12));
            painter.drawLine(QPointF(12, 5), QPointF(12, 19));
            break;
        case Glyph::Sidebar:
            painter.drawRoundedRect(QRectF(3, 5, 18, 14), 1.5, 1.5);
            painter.drawLine(QPointF(8, 5), QPointF(8, 19));
            painter.drawLine(QPointF(17, 5), QPointF(17, 19));
            break;
        case Glyph::Plugin: {
            painter.drawPath(pluginGlyphPath());
            break;
        }
        case Glyph::Layout:
            painter.drawRoundedRect(QRectF(4, 4, 16, 16), 1.5, 1.5);
            painter.drawLine(QPointF(4, 10), QPointF(20, 10));
            painter.drawLine(QPointF(11, 10), QPointF(11, 20));
            break;
        case Glyph::Filter: {
            QPainterPath path;
            path.moveTo(3, 5); path.lineTo(21, 5); path.lineTo(14, 13);
            path.lineTo(14, 19); path.lineTo(10, 21); path.lineTo(10, 13); path.closeSubpath();
            painter.drawPath(path); break;
        }
        case Glyph::Pin:
            painter.translate(12, 12); painter.rotate(isChecked() ? -35 : 35); painter.translate(-12, -12);
            painter.drawLine(QPointF(8, 4), QPointF(16, 4));
            painter.drawLine(QPointF(9, 4), QPointF(9, 10));
            painter.drawLine(QPointF(15, 4), QPointF(15, 10));
            painter.drawLine(QPointF(9, 10), QPointF(6, 14));
            painter.drawLine(QPointF(15, 10), QPointF(18, 14));
            painter.drawLine(QPointF(6, 14), QPointF(18, 14));
            painter.drawLine(QPointF(12, 14), QPointF(12, 21));
            break;
        }
    }
private:
    Glyph glyph_;
};

QWidget* uniqueWidget(QWidget* root, const char* className) {
    QWidget* result = nullptr;
    for (QWidget* widget : root->findChildren<QWidget*>()) {
        if (!widget->inherits(className)) continue;
        if (result) return nullptr;
        result = widget;
    }
    return result;
}

QMenu* uniqueMenu(const char* className) {
    QMenu* result = nullptr;
    for (QWidget* widget : QApplication::allWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (!menu || !menu->inherits(className)) continue;
        if (result) return nullptr;
        result = menu;
    }
    return result;
}

QMenu* dockSideMenu() {
    QMenu* result = nullptr;
    for (QWidget* widget : QApplication::allWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (!menu || !menu->inherits("BFStyleMenu") || menu->objectName() != QStringLiteral("BFDockAreaSideMenu")) continue;
        if (result) return nullptr;
        result = menu;
    }
    return result;
}

QAction* menuAction(QMenu* menu, const char* objectName) {
    if (!menu) return nullptr;
    for (QAction* action : menu->actions())
        if (action->objectName() == QLatin1String(objectName)) return action;
    return nullptr;
}

QMenu* importMenu() {
    QMenu* result = nullptr;
    for (QWidget* widget : QApplication::allWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (!menu || !menu->inherits("BFStyleMenu")) continue;
        bool files = false, folders = false, package = false;
        for (QAction* action : menu->actions()) {
            files |= action->text() == QStringLiteral("导入文件");
            folders |= action->text() == QStringLiteral("导入本地文件夹");
            package |= action->text() == QStringLiteral("导入Billfish素材包");
        }
        if (!files || !folders || !package) continue;
        if (result) return nullptr;
        result = menu;
    }
    return result;
}

bool refreshNativeMenu(QMenu* menu) {
    struct Binding { const char* className; quintptr rva; const char* bytes; };
    static const Binding bindings[] {
        {"ToolModule::BFNavInfoMenu", 0x76a0, "48895c2408574883ec20488b01488bf9ff9070010000ff15"},
        {"ToolModule::BFNavLayoutMenu", 0x78a0, "40534883ec20488b01488bd9ff9070010000ff15c8570400"},
        {"ToolModule::BFNavSortMenu", 0x79e0, "40534883ec20488b01488bd9ff9070010000ff1588560400"}
    };
    static HMODULE module = [] {
        HMODULE loaded = GetModuleHandleW(L"toolmodule.dll");
        if (!loaded) return static_cast<HMODULE>(nullptr);
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(loaded, path, 32768);
        QFile file(QString::fromWCharArray(path, static_cast<int>(length)));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!length || length >= 32768 || !file.open(QIODevice::ReadOnly) || !hash.addData(&file)
            || hash.result().toHex() != QByteArrayLiteral("dd1018659189f191ea9203d3dca4e3b0ffde3064b291f4e78635f38fdb7d1923"))
            return static_cast<HMODULE>(nullptr);
        return loaded;
    }();
    if (!menu || !module) return false;
    for (const Binding& binding : bindings) {
        if (!menu->inherits(binding.className)) continue;
        auto* address = reinterpret_cast<unsigned char*>(module) + binding.rva;
        const QByteArray bytes = QByteArray::fromHex(binding.bytes);
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT
            || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            || reinterpret_cast<quintptr>(address) + static_cast<quintptr>(bytes.size())
                > reinterpret_cast<quintptr>(region.BaseAddress) + region.RegionSize
            || std::memcmp(address, bytes.constData(), static_cast<size_t>(bytes.size())) != 0) return false;
        reinterpret_cast<void (*)(QMenu*)>(address)(menu);
        return true;
    }
    return false;
}

bool textInputFocused() {
    for (QWidget* focus = QApplication::focusWidget(); focus; focus = focus->parentWidget()) {
        if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus)
            || qobject_cast<QAbstractSpinBox*>(focus)) return true;
        if (auto* combo = qobject_cast<QComboBox*>(focus)) if (combo->isEditable()) return true;
    }
    return false;
}

QString actionLabel(const QAction* action) {
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QAction* namedAction(QMenu* menu, const QStringList& names) {
    if (!menu) return nullptr;
    for (QAction* action : menu->actions())
        for (const QString& name : names)
            if (actionLabel(action).compare(name, Qt::CaseInsensitive) == 0) return action;
    return nullptr;
}

QWidget* dragRegion(QWidget* parent) {
    auto* region = new QWidget(parent);
    // CFramelessWindow's frozen WM_NCHITTEST recognizes this native caption
    // name. Its geometry is limited to a real empty layout spacer.
    region->setObjectName(QStringLiteral("spacerWidget"));
    region->setAttribute(Qt::WA_NoSystemBackground);
    region->setStyleSheet(QStringLiteral("background: transparent; border: 0;"));
    region->hide();
    return region;
}

class ActionSwitch final : public QToolButton {
public:
    explicit ActionSwitch(QWidget* parent) : QToolButton(parent) { setFixedSize(34, 22); setFocusPolicy(Qt::NoFocus); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled()) painter.setOpacity(0.35);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? QColor(83, 116, 158) : QColor(68, 72, 80));
        painter.drawRoundedRect(QRectF(1, 3, 32, 16), 8, 8);
        painter.setBrush(QColor(225, 229, 235));
        painter.drawEllipse(QRectF(isChecked() ? 19 : 3, 5, 12, 12));
    }
};

class ActionRow final : public QWidget {
public:
    ActionRow(const QString& text, QAction* action, QWidget* parent) : QWidget(parent), action_(action) {
        setObjectName(QStringLiteral("BillfishToggleRow"));
        setAttribute(Qt::WA_StyledBackground);
        setMinimumHeight(30);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 2, 0, 2);
        auto* label = new QLabel(text, this);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(label, 1);
        auto* toggle = new ActionSwitch(this);
        toggle->setDefaultAction(action);
        toggle->setAccessibleName(text);
        toggle->setToolTip(text);
        row->addWidget(toggle);
        const QPointer<QAction> owned(action);
        const auto update = [this, owned] {
            setEnabled(owned && owned->isEnabled());
            setVisible(owned && owned->isVisible());
        };
        connect(action, &QAction::changed, this, update);
        update();
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) { pressed_ = true; event->accept(); return; }
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            if (rect().contains(event->pos()) && action_ && action_->isEnabled()) action_->trigger();
            event->accept(); return;
        }
        QWidget::mouseReleaseEvent(event);
    }
private:
    QPointer<QAction> action_;
    bool pressed_ = false;
};

class ArrowCombo final : public QComboBox {
public:
    explicit ArrowCombo(QWidget* parent) : QComboBox(parent) {
        setMinimumHeight(28);
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        qApp->installEventFilter(this);
    }
    void showPopup() override {
        if (!billfish::automation::AppInputScope::active()) {
            QComboBox::showPopup();
            return;
        }
        delete scopedPopup_.data();
        auto* popup = new QListView(this);
        scopedPopup_ = popup;
        popup->setObjectName(QStringLiteral("BillfishLayoutChoicePopup"));
        popup->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        popup->setAttribute(Qt::WA_ShowWithoutActivating);
        popup->setEditTriggers(QAbstractItemView::NoEditTriggers);
        popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        popup->setModel(model());
        popup->setRootIndex(rootModelIndex());
        popup->setModelColumn(modelColumn());
        if (auto* original = qobject_cast<QListView*>(view()))
            for (int row = 0; row < count(); ++row) popup->setRowHidden(row, original->isRowHidden(row));
        popup->setCurrentIndex(model()->index(currentIndex(), modelColumn(), rootModelIndex()));
        const int rowHeight = std::max(24, popup->sizeHintForRow(std::max(0, currentIndex())));
        popup->resize(std::max(width(), popup->sizeHintForColumn(modelColumn()) + 24),
                      rowHeight * std::min(count(), maxVisibleItems()) + 4);
        QPoint position = mapToGlobal(QPoint(0, height()));
        if (auto* screen = QGuiApplication::screenAt(position)) {
            const QRect bounds = screen->availableGeometry();
            position.setX(std::clamp(position.x(), bounds.left(), std::max(bounds.left(), bounds.right() - popup->width() + 1)));
            if (position.y() + popup->height() > bounds.bottom() + 1)
                position.setY(std::max(bounds.top(), mapToGlobal(QPoint()).y() - popup->height()));
        }
        popup->move(position);
        connect(popup, &QListView::clicked, this, [this](const QModelIndex& index) { selectScopedItem(index); });
        popup->show();
    }
    void hidePopup() override {
        if (scopedPopup_) scopedPopup_->hide();
        QComboBox::hidePopup();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (scopedPopup_ && scopedPopup_->isVisible()) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (event->type() == QEvent::MouseButtonPress && widget
                && !belongsToWidget(scopedPopup_, widget)) {
                scopedPopup_->hide();
                if (widget == this) return true;
            } else if (event->type() == QEvent::KeyPress && widget
                       && (widget == this || belongsToWidget(scopedPopup_, widget))) {
                auto* key = static_cast<QKeyEvent*>(event);
                if (key->key() == Qt::Key_Escape) { scopedPopup_->hide(); return true; }
                if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                    selectScopedItem(scopedPopup_->currentIndex());
                    return true;
                }
                if (widget == this && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down
                    || key->key() == Qt::Key_Home || key->key() == Qt::Key_End
                    || key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown)) {
                    QCoreApplication::sendEvent(scopedPopup_, event);
                    return true;
                }
            } else if (event->type() == QEvent::Hide && watched == window()) {
                scopedPopup_->hide();
            }
        }
        return QComboBox::eventFilter(watched, event);
    }
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(isEnabled() ? QColor(209, 214, 223) : QColor(111, 116, 126), 1.3, Qt::SolidLine, Qt::RoundCap));
        const qreal x = width() - 12, y = height() / 2.0;
        painter.drawLine(QPointF(x - 3, y - 1), QPointF(x, y + 2));
        painter.drawLine(QPointF(x, y + 2), QPointF(x + 3, y - 1));
    }
private:
    void selectScopedItem(const QModelIndex& index) {
        if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled) || !(index.flags() & Qt::ItemIsSelectable)) return;
        setCurrentIndex(index.row());
        scopedPopup_->hide();
        emit activated(index.row());
    }
    QPointer<QListView> scopedPopup_;
};

class OpaquePanel final : public QFrame {
public:
    using QFrame::QFrame;
protected:
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(37, 39, 43));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(59, 61, 66), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }
};

void darkPanel(QFrame* panel) {
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setAttribute(Qt::WA_TranslucentBackground, false);
    panel->setAttribute(Qt::WA_OpaquePaintEvent);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(37, 39, 43));
    palette.setColor(QPalette::Base, QColor(37, 39, 43));
    palette.setColor(QPalette::Button, QColor(31, 33, 37));
    palette.setColor(QPalette::WindowText, QColor(216, 220, 228));
    palette.setColor(QPalette::Text, QColor(216, 220, 228));
    palette.setColor(QPalette::ButtonText, QColor(216, 220, 228));
    palette.setColor(QPalette::Highlight, QColor(62, 76, 96));
    palette.setColor(QPalette::HighlightedText, QColor(237, 240, 245));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(111, 116, 126));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(111, 116, 126));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(111, 116, 126));
    panel->setPalette(palette);
    panel->setStyleSheet(QStringLiteral(
        "QFrame#BillfishLayoutPanel, QFrame#BillfishSidebarPanel { background: #25272b; border: 1px solid #3b3d42; border-radius: 8px; }"
        "QWidget { color: #d8dce4; font-size: 13px; background: transparent; }"
        "QLabel { border: 0; background: transparent; } QLabel:disabled { color: #737882; }"
        "QWidget#BillfishToggleRow:hover { background: rgba(169,181,200,10); border-radius: 4px; }"
        "QComboBox { background: #1e2024; color: #d8dce4; border: 1px solid #3b3d42; border-radius: 4px; padding: 3px 19px 3px 7px; }"
        "QComboBox:hover, QComboBox:focus { border-color: #5b6371; } QComboBox:disabled { color: #737882; }"
        "QComboBox::drop-down { border: 0; width: 17px; } QComboBox::down-arrow { image: none; border: 0; }"
        "QComboBox QAbstractItemView { background: #25272b; color: #d8dce4; border: 1px solid #3b3d42; selection-background-color: #3e4c60; selection-color: #edf0f5; outline: 0; }"
        "QFrame#BillfishPanelDivider { background: #3b3d42; border: 0; max-height: 1px; }"));
}

void divider(QVBoxLayout* body) {
    auto* line = new QFrame(body->parentWidget());
    line->setObjectName(QStringLiteral("BillfishPanelDivider"));
    line->setFixedHeight(1);
    body->addWidget(line);
}

void populateCombo(QComboBox* combo, const QList<QAction*>& actions) {
    QList<QPointer<QAction>> candidates;
    for (QAction* action : actions) {
        if (!action || !action->isVisible()) continue;
        candidates.append(action);
        combo->addItem(actionLabel(action));
        const int index = combo->count() - 1;
        const QPointer<QAction> owned(action);
        const auto synchronize = [combo, owned, index] {
            if (!owned) return;
            if (owned->isChecked()) combo->setCurrentIndex(index);
            if (auto* model = qobject_cast<QStandardItemModel*>(combo->model()))
                if (auto* item = model->item(index)) item->setEnabled(owned->isEnabled());
            if (auto* view = qobject_cast<QListView*>(combo->view())) view->setRowHidden(index, !owned->isVisible());
        };
        QObject::connect(action, &QAction::changed, combo, synchronize);
        synchronize();
    }
    QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [candidates](int index) {
        if (index >= 0 && index < candidates.size() && candidates.at(index)
            && candidates.at(index)->isEnabled() && candidates.at(index)->isVisible()) candidates.at(index)->trigger();
    });
}
} // namespace

EagleToolbar::EagleToolbar(QWidget* mainWindow, QTabWidget* tabs, QWidget* navigation,
                           MenuRefresher menuRefresher, QObject* parent)
    : QObject(parent), mainWindow_(mainWindow), tabs_(tabs), navigation_(navigation),
      refreshTimer_(new QTimer(this)), refreshMenu_(menuRefresher ? menuRefresher : &refreshNativeMenu) {
    setObjectName(QStringLiteral("BillfishEagleToolbar"));
    refreshTimer_->setSingleShot(true);
    connect(refreshTimer_, &QTimer::timeout, this, &EagleToolbar::refresh);
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Billfish"), QStringLiteral("maintained-shell"));
    if (settings.contains(QStringLiteral("filter-visible"))) filterVisiblePreference_ = settings.value(QStringLiteral("filter-visible")).toBool();
}

EagleToolbar::~EagleToolbar() {
    if (rightStretch_ && rightCorner_ && rightCorner_->layout()) {
        QLayout* layout = rightCorner_->layout();
        for (int index = 0; index < layout->count(); ++index) {
            if (layout->itemAt(index) != rightStretch_) continue;
            delete layout->takeAt(index);
            break;
        }
    }
    delete sidebarButton_.data();
    delete addButton_.data();
    delete pluginButton_.data();
    delete layoutButton_.data();
    delete filterButton_.data();
    delete pinButton_.data();
    delete panel_.data();
    delete leftDragRegion_.data();
    delete rightDragRegion_.data();
    for (const auto& region : dragRegions_) delete region.data();
}

bool EagleToolbar::attach() {
    if (!mainWindow_ || !tabs_ || !navigation_) return false;
    auto* page = navigation_->findChild<QWidget*>(QStringLiteral("pag_normal_source"));
    normalPage_ = page;
    auto* contents = page ? page->findChild<QWidget*>(QStringLiteral("wid_main"), Qt::FindDirectChildrenOnly) : nullptr;
    auto* row = contents ? qobject_cast<QHBoxLayout*>(contents->layout()) : nullptr;
    auto* title = contents ? contents->findChild<QLabel*>(QStringLiteral("lab_view_name"), Qt::FindDirectChildrenOnly) : nullptr;
    auto* navigationStack = contents ? contents->findChild<QWidget*>(QStringLiteral("wid_btn_stack"), Qt::FindDirectChildrenOnly) : nullptr;
    auto* search = contents ? contents->findChild<QWidget*>(QStringLiteral("wid_search"), Qt::FindDirectChildrenOnly) : nullptr;
    auto* zoom = contents ? contents->findChild<QWidget*>(QStringLiteral("zoom_slider"), Qt::FindDirectChildrenOnly) : nullptr;
    auto* nativeButtons = contents ? contents->findChild<QWidget*>(QStringLiteral("btn_widget"), Qt::FindDirectChildrenOnly) : nullptr;
    auto* local = uniqueWidget(tabs_, "BfLocalResWidget");
    auto* splitter = local ? qobject_cast<QSplitter*>(uniqueWidget(local, "BFItemIconSplitter")) : nullptr;
    auto* searchField = search ? uniqueWidget(search, "ToolModule::NavSearchWid") : nullptr;
    if (!row || !title || !navigationStack || !search || !zoom || !nativeButtons || !searchField
        || !splitter || splitter->count() != 3 || !tabs_->cornerWidget(Qt::TopLeftCorner)
        || !tabs_->cornerWidget(Qt::TopRightCorner)) return false;
    const QList<QWidget*> original {navigationStack, title, search, zoom, nativeButtons};
    for (QWidget* widget : original) if (row->indexOf(widget) < 0) return false;
    for (int index = 0; index < row->count(); ++index) {
        auto* item = row->itemAt(index);
        if (item->layout() || (item->widget() && !original.contains(item->widget()))) return false;
    }
    contents_ = contents; row_ = row; title_ = title; navigationStack_ = navigationStack;
    search_ = search; searchField_ = searchField; zoom_ = zoom; nativeButtons_ = nativeButtons;
    leftCorner_ = tabs_->cornerWidget(Qt::TopLeftCorner); rightCorner_ = tabs_->cornerWidget(Qt::TopRightCorner);
    splitter_ = splitter; leftPane_ = splitter->widget(0); rightPane_ = splitter->widget(2);
    navigation_->setAttribute(Qt::WA_StyledBackground);
    navigation_->setStyleSheet(QStringLiteral("#BFNavigationBar, #stackwid_nav, #pag_normal_source { background: #202226; border: 0; }"));
    contents->setStyleSheet(QStringLiteral("QWidget#wid_main, #zoom_slider { background: transparent; } QLabel#lab_view_name, QLabel#BillfishViewCaption { color: #d8dce4; }"));
    leftCorner_->setAttribute(Qt::WA_StyledBackground);
    rightCorner_->setAttribute(Qt::WA_StyledBackground);
    leftCorner_->setStyleSheet(QStringLiteral("#LeftMainTitleBar { background: #202226; } QWidget#wid_main { background: transparent; }"));
    rightCorner_->setStyleSheet(QStringLiteral("#right_title { background: #202226; }"));
    bodyStack_ = local->parentWidget();
    const QVariantList savedMargins = bodyStack_->property("billfish.shell.originalBodyMargins").toList();
    if (savedMargins.size() == 4) bodyMargins_ = QMargins(savedMargins.at(0).toInt(), savedMargins.at(1).toInt(), savedMargins.at(2).toInt(), savedMargins.at(3).toInt());
    else {
        bodyMargins_ = bodyStack_->contentsMargins();
        bodyStack_->setProperty("billfish.shell.originalBodyMargins", QVariantList {bodyMargins_.left(), bodyMargins_.top(), bodyMargins_.right(), bodyMargins_.bottom()});
    }
    for (QWidget* widget : {navigation_.data(), page, contents, navigationStack, search, zoom}) widget->setFixedHeight(48);
    if (auto* stack = navigation_->findChild<QWidget*>(QStringLiteral("stackwid_nav"))) stack->setFixedHeight(48);
    searchField->setFixedHeight(32);
    if (auto* fieldContents = searchField->findChild<QWidget*>(QStringLiteral("wid_main"), Qt::FindDirectChildrenOnly)) {
        fieldContents->setFixedHeight(32);
        fieldContents->setMinimumWidth(0);
        fieldContents->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    const QString border = mainWindow_->palette().color(QPalette::Highlight).name();
    searchField->setStyleSheet(QStringLiteral("#m_search_wid { background: rgba(0,0,0,28); border: 1px solid rgba(255,255,255,25); border-radius: 6px; } #m_search_wid[billfishFocus=\"true\"] { border-color: %1; }").arg(border));
    if (auto* edit = searchField->findChild<QLineEdit*>(QStringLiteral("edit_search_input"))) {
        edit->setStyleSheet(QStringLiteral("background: transparent; border: 0; font-size: 13px;"));
        edit->installEventFilter(this);
    }
    title->setMinimumWidth(144);
    title->setMaximumWidth(240);
    title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    title->installEventFilter(this);
    addButton_ = new LineButton(Glyph::Add, contents);
    addButton_->setObjectName(QStringLiteral("BillfishAddMenuButton"));
    addButton_->setToolTip(QStringLiteral("导入素材"));
    addButton_->setPopupMode(QToolButton::InstantPopup);
    addButton_->hide();
    pluginButton_ = new LineButton(Glyph::Plugin, contents);
    pluginButton_->setObjectName(QStringLiteral("BillfishPluginToolbarButton"));
    pluginButton_->setToolTip(QStringLiteral("插件 (P)"));
    pluginButton_->hide();
    layoutButton_ = new LineButton(Glyph::Layout, contents);
    layoutButton_->setObjectName(QStringLiteral("BillfishLayoutToolbarButton"));
    layoutButton_->setToolTip(QStringLiteral("布局与排序"));
    layoutButton_->hide();
    filterButton_ = new LineButton(Glyph::Filter, contents);
    filterButton_->setObjectName(QStringLiteral("BillfishFilterToolbarButton"));
    filterButton_->hide();
    sidebarButton_ = new LineButton(Glyph::Sidebar, contents);
    sidebarButton_->setObjectName(QStringLiteral("BillfishSidebarToggleButton"));
    sidebarButton_->setToolTip(QStringLiteral("显示/隐藏所有栏位; 右键分别切换"));
    sidebarButton_->setCheckable(true);
    sidebarButton_->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebarButton_->hide();
    connect(sidebarButton_, &QToolButton::clicked, this, &EagleToolbar::toggleSidebars);
    connect(sidebarButton_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) { showSidebarMenu(sidebarButton_->mapToGlobal(point)); });
    connect(layoutButton_, &QToolButton::clicked, this, &EagleToolbar::showLayoutPanel);
    connect(pluginButton_, &QToolButton::pressed, this, [this] {
        if (pluginAction_) pluginAction_->setProperty("billfishPluginMenuPosition", pluginButton_->mapToGlobal(QPoint(0, pluginButton_->height() + 4)));
    });
    QHash<QWidget*, QLayoutItem*> items;
    QList<QLayoutItem*> spacers;
    while (row->count()) {
        QLayoutItem* item = row->takeAt(0);
        if (item->widget()) items.insert(item->widget(), item);
        else spacers.append(item);
    }
    row->setContentsMargins(8, 0, 8, 0);
    row->setSpacing(4);
    row->addWidget(sidebarButton_);
    row->addItem(items.value(navigationStack));
    row->addItem(items.value(title));
    if (spacers.isEmpty()) {
        auto* spacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
        row->addSpacerItem(spacer);
        dragItems_.append(spacer);
    }
    else {
        auto* spacer = spacers.takeFirst();
        spacer->spacerItem()->changeSize(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
        row->addItem(spacer);
        dragItems_.append(spacer);
    }
    row->addItem(items.value(zoom));
    auto* secondSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
    row->addSpacerItem(secondSpacer);
    dragItems_.append(secondSpacer);
    row->addWidget(pluginButton_);
    row->addWidget(layoutButton_);
    row->addWidget(filterButton_);
    row->addItem(items.value(search));
    row->addItem(items.value(nativeButtons));
    for (QLayoutItem* item : spacers) {
        item->spacerItem()->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        row->addItem(item);
    }
    for (int index = 0; index < dragItems_.size(); ++index) dragRegions_.append(dragRegion(contents));
    pinAction_ = mainWindow_->findChild<QAction*>(QStringLiteral("BillfishPinWindowAction"), Qt::FindDirectChildrenOnly);
    if (!pinAction_) {
        pinAction_ = new QAction(QStringLiteral("窗口置顶"), mainWindow_);
        pinAction_->setObjectName(QStringLiteral("BillfishPinWindowAction"));
        pinAction_->setCheckable(true);
        pinAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_T));
        pinAction_->setAutoRepeat(false);
        mainWindow_->addAction(pinAction_);
    }
    connect(pinAction_, &QAction::triggered, this, [this](bool checked) {
        if (!mainWindow_) return;
        const HWND handle = reinterpret_cast<HWND>(mainWindow_->winId());
        SetWindowPos(handle, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        pinAction_->setChecked((GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    });
    pinButton_ = new LineButton(Glyph::Pin, rightCorner_);
    pinButton_->setObjectName(QStringLiteral("BillfishPinButton"));
    pinButton_->setDefaultAction(pinAction_);
    pinButton_->setToolTip(QStringLiteral("窗口置顶 (Shift+T)"));
    if (auto* rightRow = qobject_cast<QHBoxLayout*>(rightCorner_->layout())) {
        rightStretch_ = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
        rightRow->insertSpacerItem(0, rightStretch_);
        int before = rightRow->indexOf(rightCorner_->findChild<QWidget*>(QStringLiteral("BFDockAreaMinimizeButton")));
        rightRow->insertWidget(std::max(0, before), pinButton_);
    }
    for (QWidget* corner : {leftCorner_.data(), rightCorner_.data()}) {
        corner->setProperty("billfish.shell.eagleHeader", true);
        corner->setFixedHeight(48);
    }
    if (auto* leftContents = leftCorner_->findChild<QWidget*>(QStringLiteral("wid_main"), Qt::FindDirectChildrenOnly)) {
        leftDragRegion_ = dragRegion(leftContents);
        leftContents->setFixedHeight(48);
        if (auto* leftRow = qobject_cast<QHBoxLayout*>(leftContents->layout())) {
            if (auto* menu = leftContents->findChild<QWidget*>(QStringLiteral("LeftMainTitleBar_btn_menu"))) {
                QWidget* host = menu->parentWidget();
                leftRow->removeWidget(host);
                leftRow->insertWidget(0, host);
            }
        }
    }
    rightDragRegion_ = dragRegion(rightCorner_);
    tabs_->tabBar()->setFixedHeight(48);
    qApp->installEventFilter(this);
    connect(splitter_, &QSplitter::splitterMoved, this, [this] { scheduleRefresh(); });
    bindActions();
    positionControls();
    navigation_->setProperty("billfish.shell.eagleToolbar", true);
    scheduleRefresh();
    return true;
}

void EagleToolbar::scheduleRefresh() {
    if (!refreshTimer_->isActive()) refreshTimer_->start(0);
}

void EagleToolbar::refresh() {
    bindActions();
    positionControls();
}

void EagleToolbar::bindActions() {
    if (!mainWindow_ || !navigation_ || !contents_ || !pluginButton_ || !layoutButton_ || !filterButton_ || !nativeButtons_) return;
    auto* plugin = mainWindow_->findChild<QAction*>(QStringLiteral("BillfishPluginMenuAction"), Qt::FindDirectChildrenOnly);
    if (plugin && plugin != pluginAction_) {
        pluginAction_ = plugin;
        pluginButton_->setDefaultAction(plugin);
        pluginButton_->setToolTip(QStringLiteral("插件 (P)"));
        pluginButton_->show();
    }
    if (!importMenu_) importMenu_ = importMenu();
    if (importMenu_) {
        addButton_->setMenu(importMenu_);
    }
    if (!conditionFilter_ && tabs_) {
        if (auto* local = uniqueWidget(tabs_, "BfLocalResWidget")) conditionFilter_ = uniqueWidget(local, "CConditionFilterWidget");
        if (conditionFilter_ && filterVisiblePreference_ == false) conditionFilter_->hide();
        else if (conditionFilter_ && filterVisiblePreference_ == true && normalPage_ && normalPage_->isVisible()
            && navigation_->isVisible() && leftCorner_ && leftCorner_->isVisible() && rightCorner_ && rightCorner_->isVisible())
            conditionFilter_->show();
        if (conditionFilter_ && conditionFilter_->metaObject()->indexOfSlot("slotAllFilterBtnClear()") >= 0) {
            const QFont font = title_->font();
            const QPalette palette = title_->palette();
            title_->setObjectName(QStringLiteral("BillfishViewCaption"));
            title_->setFont(font); title_->setPalette(palette);
            title_->setCursor(Qt::PointingHandCursor);
            title_->setToolTip(QStringLiteral("清除当前视图的搜索与筛选"));
        }
    }
    if (conditionFilter_ && !filterAction_) {
        filterAction_ = new QAction(QStringLiteral("筛选工具栏"), this);
        filterAction_->setObjectName(QStringLiteral("BillfishFilterBarAction"));
        filterAction_->setCheckable(true);
        filterAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        mainWindow_->addAction(filterAction_);
        connect(filterAction_, &QAction::triggered, this, [this](bool visible) {
            filterVisiblePreference_ = visible;
            QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Billfish"), QStringLiteral("maintained-shell"));
            settings.setValue(QStringLiteral("filter-visible"), visible);
            if (conditionFilter_) conditionFilter_->setVisible(visible);
            scheduleRefresh();
        });
        filterButton_->setDefaultAction(filterAction_);
        filterButton_->setToolTip(QStringLiteral("筛选工具栏 (Ctrl+Shift+F)"));
        filterButton_->show();
    }
    const auto observeMenu = [this](QPointer<QMenu>& stored, const char* className) {
        if (stored) return;
        stored = uniqueMenu(className);
        if (stored) connect(stored, &QObject::destroyed, this, [this] {
            if (panel_) panel_->hide();
            scheduleRefresh();
        });
    };
    observeMenu(layoutMenu_, "ToolModule::BFNavLayoutMenu");
    observeMenu(sortMenu_, "ToolModule::BFNavSortMenu");
    observeMenu(infoMenu_, "ToolModule::BFNavInfoMenu");
    if (layoutMenu_ && sortMenu_ && infoMenu_ && refreshMenu_(layoutMenu_) && refreshMenu_(sortMenu_) && refreshMenu_(infoMenu_)) {
        layoutButton_->show();
        nativeButtons_->hide();
        // BFNavigationBar owns four anonymous legacy shortcut actions. Leaving
        // those keys registered makes the maintained menu actions ambiguous.
        for (QAction* action : navigation_->actions()) {
            if (action->parent() != navigation_ || !action->text().isEmpty() || !action->objectName().isEmpty()) continue;
            for (int index = 0; index < 4; ++index)
                if (action->shortcut() == QKeySequence(Qt::ALT | (Qt::Key_1 + index))) action->setShortcut(QKeySequence());
        }
        const QStringList layoutNames {QStringLiteral("瀑布流"), QStringLiteral("自适应"), QStringLiteral("网格"), QStringLiteral("列表")};
        for (int index = 0; index < layoutNames.size(); ++index) {
            QAction* action = namedAction(layoutMenu_, {layoutNames.at(index)});
            if (action && !observedLayouts_.contains(action)) {
                observedLayouts_.insert(action);
                connect(action, &QObject::destroyed, this, [this, action] { observedLayouts_.remove(action); });
                connect(action, &QAction::triggered, this, [this] {
                    if (infoMenu_) refreshMenu_(infoMenu_);
                    if (sortMenu_) refreshMenu_(sortMenu_);
                });
            }
            if (action) {
                action->setShortcut(QKeySequence(Qt::ALT | (Qt::Key_1 + index)));
                action->setShortcutContext(Qt::WindowShortcut);
                action->setAutoRepeat(false);
                if (!mainWindow_->actions().contains(action)) mainWindow_->addAction(action);
            }
        }
    }
    if (QMenu* menu = dockSideMenu()) {
        const auto bindSide = [this](QPointer<QAction>& stored, QAction* action, int key) {
            if (!action) return;
            if (stored != action) {
                stored = action;
                // The frozen dock menu forwards to the current local page's
                // toggle command. Its final handler deliberately ignores bool.
                action->setCheckable(true);
                action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | key));
                if (!mainWindow_->actions().contains(action)) mainWindow_->addAction(action);
                connect(action, &QAction::triggered, this, [this] { scheduleRefresh(); });
            }
        };
        bindSide(leftAction_, menuAction(menu, "BFDockAreaHideLeftAction"), Qt::Key_1);
        bindSide(rightAction_, menuAction(menu, "BFDockAreaHideRightAction"), Qt::Key_2);
        if (leftAction_ && rightAction_) {
            sidebarButton_->show();
            if (auto* original = rightCorner_->findChild<QWidget*>(QStringLiteral("BFDockAreaSideButton"))) original->hide();
        }
    }
}

void EagleToolbar::positionControls() {
    if (!mainWindow_ || !tabs_ || !navigation_ || !contents_ || !leftCorner_ || !rightCorner_ || !leftPane_ || !rightPane_ || !bodyStack_
        || !search_ || !searchField_ || !title_ || !navigationStack_ || !zoom_ || !pluginButton_
        || !layoutButton_ || !filterButton_ || !sidebarButton_) return;
    auto* local = uniqueWidget(tabs_, "BfLocalResWidget");
    if (!local) return;
    // The frozen DraggableTabBar still reports a 42-pixel sizeHint. Reserve the
    // measured difference inside the existing stack instead of overlapping its
    // local page or changing the native two-level parent chain.
    const bool headerVisible = leftCorner_->isVisibleTo(tabs_) && rightCorner_->isVisibleTo(tabs_);
    const int correction = headerVisible ? std::max(0, 48 - bodyStack_->geometry().top()) : 0;
    const QMargins body(bodyMargins_.left(), bodyMargins_.top() + correction, bodyMargins_.right(), bodyMargins_.bottom());
    if (bodyStack_->contentsMargins() != body) bodyStack_->setContentsMargins(body);
    const QList<int> paneSizes = splitter_->sizes();
    if (paneSizes.size() != 3) return;
    // A collapsed QSplitter child remains visible and retains its QWidget
    // width while Qt moves it outside the splitter. Use allocated pane sizes.
    const bool leftVisible = leftPane_->isVisibleTo(local) && paneSizes.at(0) > 0;
    const bool rightVisible = rightPane_->isVisibleTo(local) && paneSizes.at(2) > 0;
    if (leftAction_) leftAction_->setChecked(leftVisible);
    if (rightAction_) rightAction_->setChecked(rightVisible);
    if (filterAction_ && conditionFilter_) filterAction_->setChecked(!conditionFilter_->isHidden());
    const int leftWidth = leftVisible ? paneSizes.at(0) : 44;
    leftCorner_->setProperty("billfish.shell.headerWidth", leftWidth);
    rightCorner_->setProperty("billfish.shell.headerWidth", rightVisible ? paneSizes.at(2) : 0);
    if (auto* leftContents = leftCorner_->findChild<QWidget*>(QStringLiteral("wid_main"), Qt::FindDirectChildrenOnly)) {
        auto* leftRow = qobject_cast<QHBoxLayout*>(leftContents->layout());
        if (leftRow && sidebarButton_) {
            if (addButton_ && importMenu_) {
                if (leftVisible && addButton_->parentWidget() != leftContents) {
                    row_->removeWidget(addButton_);
                    leftRow->addWidget(addButton_);
                    addButton_->show();
                }
                addButton_->setVisible(leftVisible);
            }
            if (leftVisible && sidebarButton_->parentWidget() != leftContents) {
                row_->removeWidget(sidebarButton_);
                leftRow->addWidget(sidebarButton_);
                sidebarButton_->show();
            } else if (!leftVisible && sidebarButton_->parentWidget() != contents_) {
                leftRow->removeWidget(sidebarButton_);
                row_->insertWidget(0, sidebarButton_);
                sidebarButton_->show();
            }
        }
        leftContents->setFixedWidth(leftWidth);
    }
    leftCorner_->setFixedWidth(leftWidth);
    if (auto* rightRow = qobject_cast<QHBoxLayout*>(rightCorner_->layout())) {
        if (rightStretch_) rightStretch_->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        rightRow->setStretch(0, 0);
        rightRow->invalidate();
        const int minimum = rightCorner_->sizeHint().expandedTo(rightCorner_->minimumSizeHint()).width();
        const int width = rightVisible ? std::max(paneSizes.at(2), minimum) : minimum;
        if (rightStretch_) rightStretch_->changeSize(width - minimum, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        rightRow->invalidate();
        rightCorner_->setFixedWidth(width);
        rightRow->activate();
    }
    const int width = contents_->width();
    const int searchWidth = width >= 760 ? 220 : (width >= 540 ? 200 : 180);
    search_->setFixedWidth(searchWidth);
    searchField_->setFixedWidth(searchWidth);
    const int other = searchWidth + (pluginButton_->isHidden() ? 0 : 36)
        + (layoutButton_->isHidden() ? 0 : 36) + (filterButton_->isHidden() ? 0 : 36)
        + navigationStack_->width() + (leftVisible ? 0 : 36) + 32;
    zoom_->setVisible(width - other - 120 >= 130);
    title_->setVisible(width >= 400);
    const int captionBudget = std::clamp(width - other - (zoom_->isHidden() ? 0 : 130), 0, 240);
    // The frozen native caption setter elides against the current QLabel width,
    // then stores that shortened text in the label. Reserve useful space even
    // when its previous text was only ".."; its native Resize handler reuses
    // the complete context title retained by BFNavigationBar.
    title_->setMinimumWidth(std::min(144, captionBudget));
    title_->setMaximumWidth(captionBudget);
    sidebarButton_->setChecked(leftVisible && rightVisible);
    if (pinAction_ && mainWindow_->isVisible()) {
        const HWND handle = reinterpret_cast<HWND>(mainWindow_->winId());
        pinAction_->setChecked((GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    }
    row_->activate();
    const auto positionDrag = [headerVisible](QWidget* region, const QRect& rectangle) {
        if (!region) return;
        region->setGeometry(rectangle);
        region->setVisible(headerVisible && rectangle.width() > 0 && rectangle.height() > 0);
        region->raise();
    };
    for (int index = 0; index < dragItems_.size(); ++index) positionDrag(dragRegions_.at(index), dragItems_.at(index)->geometry());
    if (leftDragRegion_ && leftDragRegion_->parentWidget()->layout()) {
        QLayout* leftLayout = leftDragRegion_->parentWidget()->layout();
        leftLayout->activate();
        QRect empty;
        for (int index = 0; index < leftLayout->count(); ++index)
            if (leftLayout->itemAt(index)->spacerItem()) { empty = leftLayout->itemAt(index)->geometry(); break; }
        positionDrag(leftDragRegion_, empty);
    }
    positionDrag(rightDragRegion_, rightStretch_ ? rightStretch_->geometry() : QRect());
}

void EagleToolbar::resetCurrentView() {
    if (!searchField_ || !conditionFilter_ || conditionFilter_->metaObject()->indexOfSlot("slotAllFilterBtnClear()") < 0) return;
    auto* edit = searchField_->findChild<QLineEdit*>(QStringLiteral("edit_search_input"));
    if (!edit) return;
    // Native empty-search handling restores the saved provider first. Clearing
    // conditions afterwards does not navigate to a different folder or library.
    edit->clear();
    QMetaObject::invokeMethod(conditionFilter_, "slotAllFilterBtnClear", Qt::DirectConnection);
}

void EagleToolbar::toggleSidebars() {
    if (!tabs_ || !splitter_ || !leftPane_ || !rightPane_ || !leftAction_ || !rightAction_) return;
    auto* local = uniqueWidget(tabs_, "BfLocalResWidget");
    if (!local) return;
    const QList<int> sizes = splitter_->sizes();
    if (sizes.size() != 3) return;
    const bool leftVisible = leftPane_->isVisibleTo(local) && sizes.at(0) > 0;
    const bool rightVisible = rightPane_->isVisibleTo(local) && sizes.at(2) > 0;
    const bool show = !leftVisible;
    if (leftVisible != show) leftAction_->trigger();
    if (rightVisible != show) rightAction_->trigger();
    scheduleRefresh();
}

void EagleToolbar::showSidebarMenu(const QPoint& position) {
    if (!mainWindow_ || !leftAction_ || !rightAction_) return;
    delete panel_.data();
    const bool scopedInput = billfish::automation::AppInputScope::active();
    auto* panel = new OpaquePanel(mainWindow_, (scopedInput ? Qt::Tool : Qt::Popup) | Qt::FramelessWindowHint);
    panel->setAttribute(Qt::WA_ShowWithoutActivating, scopedInput);
    panel_ = panel;
    panel->setObjectName(QStringLiteral("BillfishSidebarPanel"));
    panel->setFixedWidth(240);
    darkPanel(panel);
    panel->setFocusPolicy(Qt::StrongFocus);
    panel->installEventFilter(this);
    auto* body = new QVBoxLayout(panel);
    body->setContentsMargins(12, 10, 12, 10);
    body->setSpacing(6);
    auto* all = new QAction(QStringLiteral("所有栏位"), panel);
    all->setCheckable(true);
    all->setChecked(sidebarButton_->isChecked());
    connect(all, &QAction::triggered, this, &EagleToolbar::toggleSidebars);
    const auto updateAll = [this, all] {
        if (tabs_ && splitter_ && leftPane_ && rightPane_)
            if (auto* local = uniqueWidget(tabs_, "BfLocalResWidget")) {
                const QList<int> sizes = splitter_->sizes();
                all->setChecked(sizes.size() == 3 && sizes.at(0) > 0 && sizes.at(2) > 0
                    && leftPane_->isVisibleTo(local) && rightPane_->isVisibleTo(local));
            }
    };
    connect(leftAction_, &QAction::changed, panel, updateAll);
    connect(rightAction_, &QAction::changed, panel, updateAll);
    body->addWidget(new ActionRow(QStringLiteral("所有栏位"), all, panel));
    divider(body);
    body->addWidget(new ActionRow(QStringLiteral("显示侧栏"), leftAction_, panel));
    body->addWidget(new ActionRow(QStringLiteral("显示检查器"), rightAction_, panel));
    panel->adjustSize();
    QPoint anchor = position;
    if (QScreen* screen = QGuiApplication::screenAt(anchor)) {
        const QRect bounds = screen->availableGeometry();
        anchor.setX(std::clamp(anchor.x(), bounds.left(), std::max(bounds.left(), bounds.right() - panel->width() + 1)));
        anchor.setY(std::clamp(anchor.y(), bounds.top(), std::max(bounds.top(), bounds.bottom() - panel->height() + 1)));
    }
    panel->move(anchor);
    panel->show();
    if (!scopedInput) panel->setFocus(Qt::PopupFocusReason);
}

void EagleToolbar::buildLayoutPanel() {
    delete panel_.data();
    const bool scopedInput = billfish::automation::AppInputScope::active();
    auto* panel = new OpaquePanel(mainWindow_, (scopedInput ? Qt::Tool : Qt::Popup) | Qt::FramelessWindowHint);
    panel->setAttribute(Qt::WA_ShowWithoutActivating, scopedInput);
    panel_ = panel;
    panel->setObjectName(QStringLiteral("BillfishLayoutPanel"));
    panel->setFixedWidth(320);
    darkPanel(panel);
    panel->setFocusPolicy(Qt::StrongFocus);
    panel->installEventFilter(this);
    auto* body = new QVBoxLayout(panel);
    body->setContentsMargins(12, 12, 12, 12);
    body->setSpacing(8);
    auto* layouts = new QHBoxLayout;
    layouts->addWidget(new QLabel(QStringLiteral("布局方式")), 1);
    auto* layoutMode = new ArrowCombo(panel);
    layoutMode->setObjectName(QStringLiteral("BillfishLayoutMode"));
    layoutMode->setFixedWidth(132);
    QList<QAction*> layoutActions;
    const QStringList names {QStringLiteral("瀑布流"), QStringLiteral("自适应"), QStringLiteral("网格"), QStringLiteral("列表")};
    for (const QString& name : names) {
        QAction* action = namedAction(layoutMenu_, {name});
        if (action) layoutActions.append(action);
    }
    populateCombo(layoutMode, layoutActions);
    layouts->addWidget(layoutMode);
    body->addLayout(layouts);
    divider(body);
    auto* sorting = new QHBoxLayout;
    sorting->setSpacing(4);
    sorting->addWidget(new QLabel(QStringLiteral("排列方式")), 1);
    auto* order = new ArrowCombo(panel);
    order->setObjectName(QStringLiteral("BillfishSortField"));
    order->setFixedWidth(120);
    QAction* ascending = namedAction(sortMenu_, {QStringLiteral("升序")});
    QAction* descending = namedAction(sortMenu_, {QStringLiteral("降序")});
    QList<QAction*> orderActions;
    for (QAction* action : sortMenu_->actions()) {
        if (action->isSeparator() || action == ascending || action == descending || !action->isCheckable()) continue;
        orderActions.append(action);
    }
    populateCombo(order, orderActions);
    sorting->addWidget(order);
    for (QAction* action : {ascending, descending}) {
        if (!action) continue;
        auto* button = new LineButton(action == ascending ? Glyph::Ascending : Glyph::Descending, panel);
        button->setFixedSize(28, 28);
        button->setDefaultAction(action);
        button->setAccessibleName(actionLabel(action));
        sorting->addWidget(button);
    }
    body->addLayout(sorting);
    divider(body);
    for (QAction* action : infoMenu_->actions()) {
        if (action->isSeparator() || !action->isCheckable()) continue;
        QString text = actionLabel(action);
        if (!text.startsWith(QStringLiteral("显示"))) text.prepend(QStringLiteral("显示"));
        body->addWidget(new ActionRow(text, action, panel));
    }
    if (leftAction_ && rightAction_) {
        divider(body);
        body->addWidget(new ActionRow(QStringLiteral("显示侧栏"), leftAction_, panel));
        body->addWidget(new ActionRow(QStringLiteral("显示检查器"), rightAction_, panel));
    }
    panel->adjustSize();
}

void EagleToolbar::showLayoutPanel() {
    if (!mainWindow_ || !layoutButton_ || !layoutMenu_ || !sortMenu_ || !infoMenu_ || !refreshMenu_(layoutMenu_)
        || !refreshMenu_(sortMenu_) || !refreshMenu_(infoMenu_)) return;
    buildLayoutPanel();
    QPoint position = layoutButton_->mapToGlobal(QPoint(0, layoutButton_->height() + 4));
    QScreen* screen = QGuiApplication::screenAt(position);
    if (screen) {
        const QRect bounds = screen->availableGeometry();
        position.setX(std::clamp(position.x(), bounds.left(), std::max(bounds.left(), bounds.right() - panel_->width() + 1)));
        position.setY(std::clamp(position.y(), bounds.top(), std::max(bounds.top(), bounds.bottom() - panel_->height() + 1)));
    }
    panel_->move(position);
    panel_->show();
    if (!billfish::automation::AppInputScope::active()) panel_->setFocus(Qt::PopupFocusReason);
}

bool EagleToolbar::eventFilter(QObject* watched, QEvent* event) {
    if (panel_ && panel_->isVisible() && panel_->windowType() == Qt::Tool
        && event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && !belongsToWidget(panel_, widget)) panel_->hide();
    }
    if (event->type() == QEvent::Show && watched == conditionFilter_ && filterVisiblePreference_ == false)
        conditionFilter_->hide();
    if (event->type() == QEvent::Show && watched == normalPage_ && filterVisiblePreference_ == true && conditionFilter_)
        conditionFilter_->show();
    if (watched == title_ && conditionFilter_) {
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            captionPress_ = static_cast<QMouseEvent*>(event)->pos(); captionPressed_ = true; return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            const bool click = captionPressed_ && (static_cast<QMouseEvent*>(event)->pos() - captionPress_).manhattanLength() < QApplication::startDragDistance();
            captionPressed_ = false;
            if (click) resetCurrentView();
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) return true;
    }
    if (watched == pluginButton_ && pluginAction_) {
        if (event->type() == QEvent::MouseButtonRelease
            && !pluginButton_->rect().contains(static_cast<QMouseEvent*>(event)->pos()))
            pluginAction_->setProperty("billfishPluginMenuPosition", QVariant());
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide) {
            const QPointer<QAction> action = pluginAction_;
            QTimer::singleShot(0, this, [action] {
                if (action) action->setProperty("billfishPluginMenuPosition", QVariant());
            });
        }
    }
    if (event->type() == QEvent::ShortcutOverride && textInputFocused()) {
        auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_T && key->modifiers() == Qt::ShiftModifier)
            || (key->key() >= Qt::Key_1 && key->key() <= Qt::Key_4 && key->modifiers() == Qt::AltModifier)) {
            key->accept();
            return true;
        }
    }
    if (watched == panel_ && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        panel_->hide();
        return true;
    }
    if (watched == title_ && event->type() == QEvent::Paint && title_->textFormat() != Qt::RichText
        && !Qt::mightBeRichText(title_->text()) && title_->fontMetrics().horizontalAdvance(title_->text()) > title_->width()) {
        QPainter painter(title_);
        painter.setPen(title_->palette().color(QPalette::WindowText));
        painter.drawText(title_->rect(), Qt::AlignVCenter | Qt::AlignLeft,
            title_->fontMetrics().elidedText(title_->text(), Qt::ElideRight, title_->width()));
        return true;
    }
    if (searchField_ && watched == searchField_->findChild<QLineEdit*>(QStringLiteral("edit_search_input"))
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        searchField_->setProperty("billfishFocus", event->type() == QEvent::FocusIn);
        searchField_->style()->unpolish(searchField_);
        searchField_->style()->polish(searchField_);
        searchField_->update();
    }
    if ((watched == leftPane_ || watched == rightPane_ || watched == navigation_ || watched == contents_ || watched == bodyStack_
            || watched == leftCorner_ || watched == rightCorner_)
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show || event->type() == QEvent::Hide)) scheduleRefresh();
    if (watched == mainWindow_ && (event->type() == QEvent::ChildAdded || event->type() == QEvent::ActionAdded)) scheduleRefresh();
    if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && watched == conditionFilter_) scheduleRefresh();
    return QObject::eventFilter(watched, event);
}

void installEagleToolbar(QWidget* mainWindow, QTabWidget* tabs, QWidget* navigation) {
    if (!navigation || navigation->findChild<EagleToolbar*>(QStringLiteral("BillfishEagleToolbar"), Qt::FindDirectChildrenOnly)) return;
    auto* toolbar = new EagleToolbar(mainWindow, tabs, navigation, nullptr, navigation);
    if (!toolbar->attach()) delete toolbar;
}

} // namespace billfish::shell
