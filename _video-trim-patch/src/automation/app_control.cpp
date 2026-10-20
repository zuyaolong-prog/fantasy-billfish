#include "automation/app_control.hpp"
#include "automation/app_input_scope.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QSet>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>

namespace billfish::automation {
namespace {
QJsonObject failure(const QString& code, const QString& message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QJsonObject{
        {QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}
QJsonObject success(const QJsonObject& data) {
    return {{QStringLiteral("ok"), true}, {QStringLiteral("data"), data}};
}
QString rootKey(const QString& root) { return QDir::cleanPath(QDir::fromNativeSeparators(root)).toCaseFolded(); }
QJsonObject bounds(const QRect& rect) {
    return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}};
}
QString limited(const QString& text) { return text.left(4096); }
bool belongsToWidget(const QWidget* widget, const QWidget* ancestor) {
    for (auto* current = widget; current; current = current->parentWidget()) if (current == ancestor) return true;
    return false;
}
const QWidget* hierarchyRoot(const QWidget* widget) {
    if (!widget) return nullptr;
    while (widget->parentWidget()) widget = widget->parentWidget();
    return widget;
}
bool modalBlocked(const QWidget* widget) {
    for (QWidget* modal : QApplication::topLevelWidgets()) {
        if (!modal->isVisible() || modal->windowModality() == Qt::NonModal || belongsToWidget(widget, modal)) continue;
        if (modal->windowModality() == Qt::ApplicationModal || !widget || hierarchyRoot(widget) == hierarchyRoot(modal)) return true;
    }
    return false;
}
Qt::KeyboardModifiers modifiers(const QJsonArray& values, bool* valid) {
    Qt::KeyboardModifiers result;
    *valid = true;
    for (const QJsonValue& value : values) {
        const QString text = value.toString();
        if (text == QStringLiteral("Ctrl")) result |= Qt::ControlModifier;
        else if (text == QStringLiteral("Shift")) result |= Qt::ShiftModifier;
        else if (text == QStringLiteral("Alt")) result |= Qt::AltModifier;
        else *valid = false;
    }
    return result;
}
void mouse(QWidget* widget, QEvent::Type type, const QPoint& point, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
    QMouseEvent event(type, QPointF(point), QPointF(widget->mapTo(widget->window(), point)),
                      QPointF(widget->mapToGlobal(point)), button, buttons, mods,
                      Qt::MouseEventSynthesizedByApplication);
    QApplication::sendEvent(widget, &event);
}
}

AppControl::AppControl(QObject* parent) : QObject(parent) {}
void AppControl::setContextProvider(std::function<QJsonObject()> provider) { contextProvider_ = std::move(provider); }
bool AppControl::current(const QString& id, const QString& root) const {
    return !id.isEmpty() && id == snapshotId_ && rootKey(root) == libraryRoot_;
}
QString AppControl::registerTarget(QObject* object, QAbstractItemView* view, const QModelIndex& index, QWidget* ownerWidget) {
    const QString ref = QStringLiteral("u%1").arg(++nextRef_);
    targets_.insert(ref, Target{object, ownerWidget ? ownerWidget : qobject_cast<QWidget*>(object), view, QPersistentModelIndex(index), view != nullptr});
    return ref;
}

QJsonObject AppControl::snapshot(const QJsonObject& arguments, const QJsonObject& context) {
    targets_.clear();
    snapshotId_ = QString::number(++epoch_);
    libraryRoot_ = rootKey(context.value(QStringLiteral("library_root")).toString());
    const int maxNodes = qBound(1, arguments.value(QStringLiteral("max_nodes")).toInt(1000), 4000);
    const int maxItems = qBound(1, arguments.value(QStringLiteral("max_items")).toInt(100), 500);
    const bool hidden = arguments.value(QStringLiteral("include_hidden")).toBool();
    QJsonArray nodes, windows, selected;
    QJsonArray modalRefs;
    QString activeModalRef;
    QWidget* activeModal = QApplication::activeModalWidget();
    QSet<QObject*> seen;
    bool truncated = false;
    int sampled = 0;
    int itemCount = 0;
    struct Entry { QPointer<QWidget> widget; QString parent; QString window; };
    QList<Entry> queue;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!hidden && !widget->isVisible()) continue;
        queue.append({widget, QString(), QString()});
    }
    if (activeModal) queue.prepend({activeModal, QString(), QString()});
    std::function<void(QAction*, const QString&, const QString&, QWidget*)> addAction;
    addAction = [&](QAction* action, const QString& parent, const QString& window, QWidget* owner) {
        if (!action || seen.contains(action)) return;
        if (nodes.size() >= maxNodes) { truncated = true; return; }
        seen.insert(action);
        const QString ref = registerTarget(action, nullptr, QModelIndex(), owner);
        QJsonArray actions;
        if (action->isEnabled() && !action->isSeparator() && !action->menu()) actions.append(QStringLiteral("invoke"));
        nodes.append(QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("parent_ref"), parent},
            {QStringLiteral("window_ref"), window}, {QStringLiteral("kind"), QStringLiteral("action")},
            {QStringLiteral("class"), QString::fromLatin1(action->metaObject()->className())},
            {QStringLiteral("object_name"), action->objectName()}, {QStringLiteral("role"), QStringLiteral("menuitem")},
            {QStringLiteral("text"), limited(action->text())}, {QStringLiteral("value"), QJsonValue()},
            {QStringLiteral("visible"), action->isVisible()}, {QStringLiteral("enabled"), action->isEnabled()},
            {QStringLiteral("modal_blocked"), modalBlocked(owner)},
            {QStringLiteral("checked"), action->isCheckable() ? QJsonValue(action->isChecked()) : QJsonValue()},
            {QStringLiteral("bounds"), QJsonValue()}, {QStringLiteral("actions"), actions}});
        if (action->menu()) for (QAction* child : action->menu()->actions()) addAction(child, ref, window, owner);
    };
    for (int offset = 0; offset < queue.size(); ++offset) {
        QWidget* widget = queue.at(offset).widget;
        if (!widget || seen.contains(widget) || (!hidden && !widget->isVisible())) continue;
        if (nodes.size() >= maxNodes) { truncated = true; break; }
        seen.insert(widget);
        const QString ref = registerTarget(widget);
        const QString window = widget->isWindow() ? ref : queue.at(offset).window;
        if (widget->isWindow()) windows.append(ref);
        if (widget == activeModal) activeModalRef = ref;
        if (widget->isVisible() && widget->windowModality() != Qt::NonModal) modalRefs.append(ref);
        QString role = widget->isWindow() ? QStringLiteral("window") : QStringLiteral("widget");
        QString text = widget->isWindow() ? widget->windowTitle() : widget->accessibleName();
        QJsonValue value, checked;
        QJsonArray actions{QStringLiteral("click"), QStringLiteral("key"), QStringLiteral("scroll"),
                           QStringLiteral("drag"), QStringLiteral("capture")};
        if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            role = QStringLiteral("textbox");
            if (lineEdit->echoMode() == QLineEdit::Normal) value = limited(lineEdit->text());
            text = lineEdit->placeholderText();
            if (!lineEdit->isReadOnly() && lineEdit->echoMode() == QLineEdit::Normal) actions.append(QStringLiteral("type"));
        } else if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
            role = QStringLiteral("textbox"); value = limited(textEdit->toPlainText());
            if (!textEdit->isReadOnly()) actions.append(QStringLiteral("type"));
        } else if (auto* plainEdit = qobject_cast<QPlainTextEdit*>(widget)) {
            role = QStringLiteral("textbox"); value = limited(plainEdit->toPlainText());
            if (!plainEdit->isReadOnly()) actions.append(QStringLiteral("type"));
        } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            role = QStringLiteral("button"); text = button->text();
            if (button->isCheckable()) checked = button->isChecked();
        } else if (auto* label = qobject_cast<QLabel*>(widget)) {
            role = QStringLiteral("label"); text = label->text();
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            role = QStringLiteral("combobox"); value = combo->currentText();
        } else if (auto* slider = qobject_cast<QAbstractSlider*>(widget)) {
            role = QStringLiteral("slider"); value = slider->value();
        } else if (qobject_cast<QAbstractItemView*>(widget)) role = QStringLiteral("itemview");
        const QRect rect(widget->mapTo(widget->window(), QPoint()), widget->size());
        nodes.append(QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("parent_ref"), queue.at(offset).parent},
            {QStringLiteral("window_ref"), window}, {QStringLiteral("kind"), QStringLiteral("widget")},
            {QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className())},
            {QStringLiteral("object_name"), widget->objectName()}, {QStringLiteral("role"), role},
            {QStringLiteral("text"), limited(text)}, {QStringLiteral("value"), value},
            {QStringLiteral("visible"), widget->isVisible()}, {QStringLiteral("enabled"), widget->isEnabled()},
            {QStringLiteral("modal_blocked"), modalBlocked(widget)},
            {QStringLiteral("checked"), checked}, {QStringLiteral("bounds"), bounds(rect)},
            {QStringLiteral("local_bounds"), bounds(widget->rect())}, {QStringLiteral("actions"), actions}});
        // Sample the viewport, never enumerate an unbounded backing model.
        if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
            QSet<QPersistentModelIndex> items;
            QWidget* viewport = view->viewport();
            for (int y = 0; y < viewport->height(); y += 12) {
                for (int x = 0; x < viewport->width(); x += 12) {
                    if (++sampled > 30000 || itemCount >= maxItems || nodes.size() >= maxNodes) { truncated = true; break; }
                    const QModelIndex index = view->indexAt(QPoint(x, y));
                    if (!index.isValid() || items.contains(QPersistentModelIndex(index))) continue;
                    items.insert(QPersistentModelIndex(index));
                    const QRect itemRect = view->visualRect(index);
                    if (!itemRect.intersects(viewport->rect()) || itemRect.isEmpty()) continue;
                    const QString itemRef = registerTarget(view, view, index);
                    const bool isSelected = view->selectionModel() && view->selectionModel()->isSelected(index);
                    const QRect windowRect(viewport->mapTo(view->window(), itemRect.topLeft()), itemRect.size());
                    QJsonObject node{{QStringLiteral("ref"), itemRef}, {QStringLiteral("parent_ref"), ref},
                        {QStringLiteral("window_ref"), window}, {QStringLiteral("kind"), QStringLiteral("modelitem")},
                        {QStringLiteral("class"), QString::fromLatin1(view->metaObject()->className())},
                        {QStringLiteral("object_name"), QString()}, {QStringLiteral("role"), QStringLiteral("item")},
                        {QStringLiteral("text"), limited(index.data(Qt::DisplayRole).toString())},
                        {QStringLiteral("value"), QJsonValue()}, {QStringLiteral("visible"), view->isVisible()},
                        {QStringLiteral("enabled"), view->isEnabled() && bool(index.flags() & Qt::ItemIsEnabled)},
                        {QStringLiteral("modal_blocked"), modalBlocked(view)},
                        {QStringLiteral("selected"), isSelected}, {QStringLiteral("row"), index.row()},
                        {QStringLiteral("column"), index.column()}, {QStringLiteral("bounds"), bounds(windowRect)},
                        {QStringLiteral("actions"), QJsonArray{QStringLiteral("click"), QStringLiteral("key")}}};
                    nodes.append(node); ++itemCount;
                    if (isSelected) selected.append(QJsonObject{{QStringLiteral("ref"), itemRef},
                        {QStringLiteral("text"), node.value(QStringLiteral("text"))}});
                }
                if (sampled > 30000 || itemCount >= maxItems || nodes.size() >= maxNodes) break;
            }
        }
        for (QAction* action : widget->actions()) addAction(action, ref, window, widget);
        for (QObject* child : widget->children()) if (auto* childWidget = qobject_cast<QWidget*>(child))
            if (!childWidget->isWindow()) queue.append({childWidget, ref, window});
    }
    return success({{QStringLiteral("snapshot_id"), snapshotId_},
        {QStringLiteral("library_root"), context.value(QStringLiteral("library_root"))},
        {QStringLiteral("selected"), selected}, {QStringLiteral("windows"), windows}, {QStringLiteral("nodes"), nodes},
        {QStringLiteral("latest_action"), latestAction_},
        {QStringLiteral("active_modal_ref"), activeModalRef.isEmpty() ? QJsonValue() : QJsonValue(activeModalRef)},
        {QStringLiteral("modal_refs"), modalRefs},
        {QStringLiteral("truncated"), truncated}, {QStringLiteral("limits"), QJsonObject{
            {QStringLiteral("max_nodes"), maxNodes}, {QStringLiteral("max_items"), maxItems},
            {QStringLiteral("viewport_sample_step"), 12}, {QStringLiteral("max_viewport_samples"), 30000}}},
        {QStringLiteral("coverage"), QJsonObject{{QStringLiteral("native_dialogs"), false},
            {QStringLiteral("context_menu_event"), false},
            {QStringLiteral("right_click"), QStringLiteral("pointer_press_release_only")},
            {QStringLiteral("model_items"), QStringLiteral("sampled_visible_viewport")},
            {QStringLiteral("coordinates"), QStringLiteral("bounds_window_local_input_ref_local")}}}});
}

QJsonObject AppControl::handle(const QString& operation, const QJsonObject& arguments, const QString& requestId) {
    if (QThread::currentThread() != thread()) return failure(QStringLiteral("wrong_thread"), QStringLiteral("UI operations require the GUI thread."));
    static const QHash<QString, QSet<QString>> fields{
        {QStringLiteral("ui.snapshot"), {QStringLiteral("max_nodes"), QStringLiteral("max_items"), QStringLiteral("include_hidden")}},
        {QStringLiteral("ui.click"), {QStringLiteral("snapshot_id"), QStringLiteral("ref"), QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("button"), QStringLiteral("count"), QStringLiteral("modifiers")}},
        {QStringLiteral("ui.type"), {QStringLiteral("snapshot_id"), QStringLiteral("ref"), QStringLiteral("text"), QStringLiteral("replace")}},
        {QStringLiteral("ui.key"), {QStringLiteral("snapshot_id"), QStringLiteral("ref"), QStringLiteral("key")}},
        {QStringLiteral("ui.scroll"), {QStringLiteral("snapshot_id"), QStringLiteral("ref"), QStringLiteral("delta_x"), QStringLiteral("delta_y"), QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("modifiers")}},
        {QStringLiteral("ui.drag"), {QStringLiteral("snapshot_id"), QStringLiteral("ref"), QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("steps"), QStringLiteral("modifiers")}},
        {QStringLiteral("ui.capture"), {QStringLiteral("snapshot_id"), QStringLiteral("ref")}},
        {QStringLiteral("ui.invoke"), {QStringLiteral("snapshot_id"), QStringLiteral("ref")}}};
    if (!fields.contains(operation)) return failure(QStringLiteral("unknown_operation"), QStringLiteral("Unknown application UI operation."));
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
        if (!fields.value(operation).contains(it.key())) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected UI argument: %1").arg(it.key()));
    }
    const auto integer = [&](const QString& name, int minimum, int maximum) {
        if (!arguments.contains(name)) return true;
        const QJsonValue value = arguments.value(name);
        const double number = value.toDouble();
        return value.isDouble() && std::isfinite(number) && std::floor(number) == number && number >= minimum && number <= maximum;
    };
    for (const QString& name : {QStringLiteral("include_hidden"), QStringLiteral("replace")})
        if (arguments.contains(name) && !arguments.value(name).isBool()) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Expected a boolean: %1").arg(name));
    if (!integer(QStringLiteral("max_nodes"), 1, 4000) || !integer(QStringLiteral("max_items"), 1, 500) ||
        !integer(QStringLiteral("count"), 1, 2) || !integer(QStringLiteral("steps"), 2, 64) ||
        !integer(QStringLiteral("x"), 0, 100000) || !integer(QStringLiteral("y"), 0, 100000) ||
        !integer(QStringLiteral("delta_x"), -12000, 12000) || !integer(QStringLiteral("delta_y"), -12000, 12000))
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("UI numeric argument is not an integer in the supported range."));
    if (arguments.contains(QStringLiteral("modifiers")) && !arguments.value(QStringLiteral("modifiers")).isArray())
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("modifiers must be an array."));
    if (arguments.contains(QStringLiteral("button")) && !arguments.value(QStringLiteral("button")).isString())
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("button must be a string."));
    if (arguments.contains(QStringLiteral("x")) != arguments.contains(QStringLiteral("y")))
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Both x and y are required."));
    const QJsonObject context = contextProvider_ ? contextProvider_() : QJsonObject();
    if (operation == QStringLiteral("ui.snapshot")) return snapshot(arguments, context);
    for (const QString& name : {QStringLiteral("snapshot_id"), QStringLiteral("ref")})
        if (!arguments.value(name).isString() || arguments.value(name).toString().isEmpty())
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("A nonempty string is required: %1").arg(name));
    const QString id = arguments.value(QStringLiteral("snapshot_id")).toString();
    const QString root = context.value(QStringLiteral("library_root")).toString();
    if (!current(id, root)) {
        if (rootKey(root) != libraryRoot_) { targets_.clear(); snapshotId_.clear(); }
        return failure(QStringLiteral("stale_snapshot"), QStringLiteral("Take a new UI snapshot after library changes or another snapshot."));
    }
    const QString ref = arguments.value(QStringLiteral("ref")).toString();
    if (!targets_.contains(ref)) return failure(QStringLiteral("invalid_ref"), QStringLiteral("The ref is not in the current snapshot."));
    const Target target = targets_.value(ref);
    if (!target.object || (target.modelItem && (!target.view || !target.index.isValid() || target.index.model() != target.view->model())))
        return failure(QStringLiteral("stale_ref"), QStringLiteral("The target was deleted or its model was reset."));
    QWidget* widget = target.modelItem ? target.view->viewport() : qobject_cast<QWidget*>(target.object.data());
    QAction* action = qobject_cast<QAction*>(target.object.data());
    if (operation != QStringLiteral("ui.capture") && modalBlocked(target.ownerWidget))
        return failure(QStringLiteral("modal_blocked"), QStringLiteral("The target is blocked by an application modal window."));
    if (operation == QStringLiteral("ui.invoke")) {
        if (!action || !action->isEnabled() || action->isSeparator() || action->menu())
            return failure(QStringLiteral("unsupported_target"), QStringLiteral("invoke requires an enabled leaf QAction."));
    } else if (!widget || !widget->isVisible() || !widget->isEnabled()) {
        return failure(QStringLiteral("unavailable_target"), QStringLiteral("The target widget must be visible and enabled."));
    }
    if (operation == QStringLiteral("ui.capture")) {
        if (target.modelItem) return failure(QStringLiteral("unsupported_target"), QStringLiteral("Capture a widget ref."));
        if (qint64(widget->width()) * widget->height() > 16000000)
            return failure(QStringLiteral("capture_too_large"), QStringLiteral("Widget capture exceeds 16 megapixels."));
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        const QPixmap pixmap = widget->grab();
        if (pixmap.isNull() || !pixmap.save(&buffer, "PNG")) return failure(QStringLiteral("capture_failed"), QStringLiteral("Widget capture failed."));
        if (bytes.size() > 12 * 1024 * 1024 - 4096)
            return failure(QStringLiteral("capture_too_large"), QStringLiteral("Encoded widget PNG exceeds the response limit; capture a smaller widget."));
        return success({{QStringLiteral("snapshot_id"), id}, {QStringLiteral("ref"), ref},
            {QStringLiteral("mime_type"), QStringLiteral("image/png")}, {QStringLiteral("base64"), QString::fromLatin1(bytes.toBase64())},
            {QStringLiteral("width"), pixmap.width()}, {QStringLiteral("height"), pixmap.height()},
            {QStringLiteral("device_pixel_ratio"), pixmap.devicePixelRatio()}});
    }
    bool modsValid;
    const Qt::KeyboardModifiers mods = modifiers(arguments.value(QStringLiteral("modifiers")).toArray(), &modsValid);
    if (!modsValid) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unknown keyboard modifier."));
    QPoint point = widget ? widget->rect().center() : QPoint();
    if (target.modelItem) {
        const QRect visual = target.view->visualRect(target.index).intersected(widget->rect());
        if (visual.isEmpty()) return failure(QStringLiteral("stale_ref"), QStringLiteral("The model item is no longer visible."));
        point = visual.center();
    } else if (arguments.contains(QStringLiteral("x")) || arguments.contains(QStringLiteral("y"))) {
        if (!arguments.value(QStringLiteral("x")).isDouble() || !arguments.value(QStringLiteral("y")).isDouble())
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Both x and y are required."));
        point = QPoint(arguments.value(QStringLiteral("x")).toInt(), arguments.value(QStringLiteral("y")).toInt());
    }
    if (widget && !widget->rect().contains(point)) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Coordinates fall outside the target widget."));
    const QString text = arguments.value(QStringLiteral("text")).toString();
    if (operation == QStringLiteral("ui.type")) {
        bool editable = false;
        if (auto* edit = qobject_cast<QLineEdit*>(widget)) editable = !edit->isReadOnly() && edit->echoMode() == QLineEdit::Normal;
        if (auto* edit = qobject_cast<QTextEdit*>(widget)) editable = !edit->isReadOnly();
        if (auto* edit = qobject_cast<QPlainTextEdit*>(widget)) editable = !edit->isReadOnly();
        if (!editable || !arguments.value(QStringLiteral("text")).isString() || text.size() > 65536)
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("type requires an editable text widget and text up to 65536 characters."));
    }
    int key = 0; Qt::KeyboardModifiers keyMods;
    if (operation == QStringLiteral("ui.key")) {
        const QKeySequence sequence(arguments.value(QStringLiteral("key")).toString(), QKeySequence::PortableText);
        if (sequence.count() != 1 || !sequence[0]) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("key requires one portable key sequence, such as Ctrl+A or Return."));
        key = sequence[0] & ~int(Qt::KeyboardModifierMask);
        if (key == Qt::Key_unknown) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unknown key sequence."));
        keyMods = Qt::KeyboardModifiers(sequence[0] & int(Qt::KeyboardModifierMask));
    }
    Qt::MouseButton button = Qt::LeftButton;
    const QString buttonName = arguments.value(QStringLiteral("button")).toString(QStringLiteral("left"));
    if (buttonName == QStringLiteral("right")) button = Qt::RightButton;
    else if (buttonName == QStringLiteral("middle")) button = Qt::MiddleButton;
    else if (buttonName != QStringLiteral("left")) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unknown mouse button."));
    const int count = arguments.value(QStringLiteral("count")).toInt(1);
    if (count < 1 || count > 2) return failure(QStringLiteral("invalid_arguments"), QStringLiteral("count must be 1 or 2."));
    QPoint from, to;
    if (operation == QStringLiteral("ui.drag")) {
        const auto readPoint = [&](const QString& name, QPoint* result) {
            const QJsonObject object = arguments.value(name).toObject();
            if (object.size() != 2) return false;
            if (!object.value(QStringLiteral("x")).isDouble() || !object.value(QStringLiteral("y")).isDouble()) return false;
            for (const QString& coordinate : {QStringLiteral("x"), QStringLiteral("y")}) {
                const double value = object.value(coordinate).toDouble();
                if (!std::isfinite(value) || std::floor(value) != value || value < 0 || value > 100000) return false;
            }
            *result = QPoint(object.value(QStringLiteral("x")).toInt(), object.value(QStringLiteral("y")).toInt());
            return widget->rect().contains(*result);
        };
        if (target.modelItem || !readPoint(QStringLiteral("from"), &from) || !readPoint(QStringLiteral("to"), &to))
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("drag requires widget-local from and to points inside the widget."));
    }
    const int dx = arguments.value(QStringLiteral("delta_x")).toInt();
    const int dy = arguments.value(QStringLiteral("delta_y")).toInt();
    if (operation == QStringLiteral("ui.scroll") && (qAbs(qint64(dx)) > 12000 || qAbs(qint64(dy)) > 12000 || (!dx && !dy)))
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("scroll requires nonzero angle deltas within +/-12000."));
    const bool replace = arguments.value(QStringLiteral("replace")).toBool(true);
    const int steps = qBound(2, arguments.value(QStringLiteral("steps")).toInt(12), 64);
    const QString actionId = requestId.isEmpty() ? QStringLiteral("action-%1").arg(++actionSequence_) : requestId;
    const QString acceptedLibrary = rootKey(root);
    latestAction_ = {{QStringLiteral("request_id"), actionId}, {QStringLiteral("ref"), ref},
        {QStringLiteral("state"), QStringLiteral("queued")}};
    // Reply before calling handlers: application slots may enter QDialog::exec().
    QTimer::singleShot(0, this, [=] {
        const QJsonObject now = contextProvider_ ? contextProvider_() : QJsonObject();
        const auto state = [this, actionId, ref](const QString& value, const QString& error = QString()) {
            if (latestAction_.value(QStringLiteral("request_id")).toString() != actionId) return;
            latestAction_ = {{QStringLiteral("request_id"), actionId}, {QStringLiteral("ref"), ref}, {QStringLiteral("state"), value}};
            if (!error.isEmpty()) latestAction_.insert(QStringLiteral("error"), error);
        };
        if (acceptedLibrary != rootKey(now.value(QStringLiteral("library_root")).toString()) || !target.object) {
            if (rootKey(now.value(QStringLiteral("library_root")).toString()) != libraryRoot_) { targets_.clear(); snapshotId_.clear(); }
            state(QStringLiteral("failed"), QStringLiteral("stale_snapshot_or_deleted_target")); return;
        }
        if (target.modelItem && (!target.view || !target.index.isValid() || target.index.model() != target.view->model())) {
            state(QStringLiteral("failed"), QStringLiteral("stale_model_item")); return;
        }
        if (modalBlocked(target.ownerWidget)) { state(QStringLiteral("failed"), QStringLiteral("modal_blocked")); return; }
        state(QStringLiteral("dispatching"));
        AppInputScope scope;
        if (operation == QStringLiteral("ui.invoke")) {
            if (!action->isEnabled()) { state(QStringLiteral("failed"), QStringLiteral("disabled_action")); return; }
            action->trigger(); state(QStringLiteral("dispatched")); return;
        }
        QPointer<QWidget> receiver = target.modelItem ? target.view->viewport() : qobject_cast<QWidget*>(target.object.data());
        if (!receiver || !receiver->isVisible() || !receiver->isEnabled()) {
            state(QStringLiteral("failed"), QStringLiteral("unavailable_target")); return;
        }
        QPoint at = point;
        QPoint dragFrom = from, dragTo = to;
        if (target.modelItem) {
            const QRect visual = target.view->visualRect(target.index).intersected(receiver->rect());
            if (visual.isEmpty()) { state(QStringLiteral("failed"), QStringLiteral("item_no_longer_visible")); return; }
            at = visual.center();
        }
        // A window/container ref plus local coordinates follows Qt hit testing.
        // Once a drag starts its receiver stays fixed, matching mouse capture
        // semantics without requesting a system mouse grab.
        if (!target.modelItem && (operation == QStringLiteral("ui.click") ||
            operation == QStringLiteral("ui.scroll") || operation == QStringLiteral("ui.drag"))) {
            QWidget* hit = receiver->childAt(operation == QStringLiteral("ui.drag") ? from : at);
            if (hit) {
                at = hit->mapFrom(receiver, at);
                dragFrom = hit->mapFrom(receiver, from);
                dragTo = hit->mapFrom(receiver, to);
                receiver = hit;
                if (!receiver->isEnabled()) { state(QStringLiteral("failed"), QStringLiteral("disabled_child_target")); return; }
            }
        }
        if (operation == QStringLiteral("ui.click")) {
            receiver->setFocus(Qt::MouseFocusReason);
            mouse(receiver, QEvent::MouseButtonPress, at, button, button, mods);
            if (!receiver || !receiver->isVisible()) { state(QStringLiteral("dispatched")); return; }
            mouse(receiver, QEvent::MouseButtonRelease, at, button, Qt::NoButton, mods);
            // Give work queued by the first click a turn before the second
            // pair. Vendor navigation can unload the source player before its
            // button is hidden; synchronous sendEvent calls would bypass it.
            if (count == 2 && receiver && receiver->isVisible()) {
                QTimer::singleShot(0, this, [=] {
                    const QJsonObject secondContext = contextProvider_ ? contextProvider_() : QJsonObject();
                    if (acceptedLibrary != rootKey(secondContext.value(QStringLiteral("library_root")).toString())) {
                        state(QStringLiteral("failed"), QStringLiteral("library_changed")); return;
                    }
                    if (!target.object || !receiver || !receiver->isVisible()) { state(QStringLiteral("dispatched")); return; }
                    if (!receiver->isEnabled() || modalBlocked(target.ownerWidget)) {
                        state(QStringLiteral("failed"), QStringLiteral("unavailable_or_modal_blocked_target")); return;
                    }
                    QPoint secondAt = at;
                    if (target.modelItem) {
                        if (!target.view || !target.index.isValid() || target.index.model() != target.view->model()) {
                            state(QStringLiteral("failed"), QStringLiteral("stale_model_item")); return;
                        }
                        const QRect visual = target.view->visualRect(target.index).intersected(receiver->rect());
                        if (visual.isEmpty()) { state(QStringLiteral("dispatched")); return; }
                        secondAt = visual.center();
                    }
                    AppInputScope secondScope;
                    mouse(receiver, QEvent::MouseButtonDblClick, secondAt, button, button, mods);
                    if (receiver && receiver->isVisible()) mouse(receiver, QEvent::MouseButtonRelease, secondAt, button, Qt::NoButton, mods);
                    state(QStringLiteral("dispatched"));
                });
                return;
            }
        } else if (operation == QStringLiteral("ui.type")) {
            receiver->setFocus(Qt::OtherFocusReason);
            if (replace) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
                QApplication::sendEvent(receiver, &press);
                if (!receiver) { state(QStringLiteral("dispatched")); return; }
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::ControlModifier);
                QApplication::sendEvent(receiver, &release);
            }
            if (receiver) { QInputMethodEvent event; event.setCommitString(text); QApplication::sendEvent(receiver, &event); }
        } else if (operation == QStringLiteral("ui.key")) {
            QKeyEvent press(QEvent::KeyPress, key, keyMods);
            QApplication::sendEvent(receiver, &press);
            if (receiver) { QKeyEvent release(QEvent::KeyRelease, key, keyMods); QApplication::sendEvent(receiver, &release); }
        } else if (operation == QStringLiteral("ui.scroll")) {
            QWheelEvent event(QPointF(at), QPointF(receiver->mapToGlobal(at)), QPoint(), QPoint(dx, dy),
                Qt::NoButton, mods, Qt::NoScrollPhase, false, Qt::MouseEventSynthesizedByApplication);
            QApplication::sendEvent(receiver, &event);
        } else if (operation == QStringLiteral("ui.drag")) {
            mouse(receiver, QEvent::MouseButtonPress, dragFrom, button, button, mods);
            for (int step = 1; step <= steps && receiver; ++step)
                mouse(receiver, QEvent::MouseMove, dragFrom + (dragTo - dragFrom) * step / steps, Qt::NoButton, button, mods);
            if (receiver) mouse(receiver, QEvent::MouseButtonRelease, dragTo, button, Qt::NoButton, mods);
        }
        state(QStringLiteral("dispatched"));
    });
    return success({{QStringLiteral("accepted"), true}, {QStringLiteral("queued"), true}, {QStringLiteral("request_id"), actionId},
        {QStringLiteral("snapshot_id"), id}, {QStringLiteral("ref"), ref}});
}
}
