#include "automation/app_control.hpp"
#include "automation/app_input_scope.hpp"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>

using billfish::automation::AppControl;
using billfish::automation::AppInputScope;

class DragPad final : public QWidget {
public:
    int moves = 0;
    bool pressed = false;
    bool scoped = false;
    QPoint last;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        pressed = event->button() == Qt::LeftButton;
        scoped = AppInputScope::active();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed && event->buttons().testFlag(Qt::LeftButton)) { ++moves; last = event->pos(); }
    }
    void mouseReleaseEvent(QMouseEvent*) override { pressed = false; }
};

class ClickRecordingButton final : public QPushButton {
public:
    using QPushButton::QPushButton;
    int presses = 0;
    int releases = 0;
    int doubleClicks = 0;
protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress) ++presses;
        if (event->type() == QEvent::MouseButtonRelease) ++releases;
        if (event->type() == QEvent::MouseButtonDblClick) ++doubleClicks;
        return QPushButton::event(event);
    }
};

class AppControlTest final : public QObject {
    Q_OBJECT
private:
    static QJsonObject data(const QJsonObject& response) { return response.value(QStringLiteral("data")).toObject(); }
    static QString refFor(const QJsonObject& snapshot, const QString& name, const QString& kind = QStringLiteral("widget")) {
        for (const QJsonValue& value : snapshot.value(QStringLiteral("nodes")).toArray()) {
            const QJsonObject node = value.toObject();
            if (node.value(QStringLiteral("kind")).toString() == kind &&
                node.value(QStringLiteral("object_name")).toString() == name) return node.value(QStringLiteral("ref")).toString();
        }
        return QString();
    }
    static QJsonObject args(const QJsonObject& snapshot, const QString& ref) {
        return {{QStringLiteral("snapshot_id"), snapshot.value(QStringLiteral("snapshot_id"))}, {QStringLiteral("ref"), ref}};
    }
    static QString error(const QJsonObject& result) {
        return result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
    }
private slots:
    void targetedInputDoesNotActivate() {
        QWidget target, foreground;
        target.setObjectName(QStringLiteral("targetWindow"));
        target.setAttribute(Qt::WA_ShowWithoutActivating);
        auto* layout = new QVBoxLayout(&target);
        auto* edit = new QLineEdit(&target); edit->setObjectName(QStringLiteral("search"));
        auto* button = new QPushButton(QStringLiteral("Run"), &target); button->setObjectName(QStringLiteral("run"));
        auto* pad = new DragPad; pad->setObjectName(QStringLiteral("pad")); pad->setMinimumSize(100, 100);
        layout->addWidget(edit); layout->addWidget(button); layout->addWidget(pad);
        target.show(); foreground.show(); foreground.activateWindow();
        QCoreApplication::processEvents();
        QWidget* activeBefore = QApplication::activeWindow();
        const QPoint cursorBefore = QCursor::pos();
        AppControl control;
        QJsonObject snapshot = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QSignalSpy edited(edit, &QLineEdit::textEdited);
        QJsonObject input = args(snapshot, refFor(snapshot, QStringLiteral("search")));
        input.insert(QStringLiteral("text"), QStringLiteral("new query"));
        QVERIFY(data(control.handle(QStringLiteral("ui.type"), input)).value(QStringLiteral("accepted")).toBool());
        QTRY_COMPARE(edit->text(), QStringLiteral("new query"));
        QVERIFY(!edited.isEmpty());
        input.insert(QStringLiteral("text"), QStringLiteral("replacement"));
        control.handle(QStringLiteral("ui.type"), input);
        QTRY_COMPARE(edit->text(), QStringLiteral("replacement"));
        QJsonObject keyInput = args(snapshot, refFor(snapshot, QStringLiteral("search")));
        keyInput.insert(QStringLiteral("key"), QStringLiteral("Ctrl+A"));
        control.handle(QStringLiteral("ui.key"), keyInput);
        QTRY_COMPARE(edit->selectedText(), QStringLiteral("replacement"));
        QSignalSpy clicked(button, &QPushButton::clicked);
        control.handle(QStringLiteral("ui.click"), args(snapshot, refFor(snapshot, QStringLiteral("run"))));
        QTRY_COMPARE(clicked.size(), 1);
        QJsonObject coordinateClick = args(snapshot, refFor(snapshot, QStringLiteral("targetWindow")));
        const QPoint buttonCenter = button->mapTo(&target, button->rect().center());
        coordinateClick.insert(QStringLiteral("x"), buttonCenter.x());
        coordinateClick.insert(QStringLiteral("y"), buttonCenter.y());
        control.handle(QStringLiteral("ui.click"), coordinateClick);
        QTRY_COMPARE(clicked.size(), 2);
        QJsonObject drag = args(snapshot, refFor(snapshot, QStringLiteral("pad")));
        drag.insert(QStringLiteral("from"), QJsonObject{{QStringLiteral("x"), 5}, {QStringLiteral("y"), 10}});
        drag.insert(QStringLiteral("to"), QJsonObject{{QStringLiteral("x"), 80}, {QStringLiteral("y"), 70}});
        control.handle(QStringLiteral("ui.drag"), drag);
        QTRY_COMPARE(pad->last, QPoint(80, 70));
        QVERIFY(pad->moves >= 2); QVERIFY(pad->scoped); QVERIFY(!pad->pressed);
        QVERIFY(!AppInputScope::active());
        QCOMPARE(QApplication::activeWindow(), activeBefore);
        QCOMPARE(QCursor::pos(), cursorBefore);
        QVERIFY(QWidget::mouseGrabber() == nullptr);
        const QJsonObject capture = data(control.handle(QStringLiteral("ui.capture"), args(snapshot, refFor(snapshot, QStringLiteral("pad")))));
        QVERIFY(QByteArray::fromBase64(capture.value(QStringLiteral("base64")).toString().toLatin1()).startsWith("\x89PNG"));
    }

    void modelSelectionAndRefInvalidation() {
        QListView view; view.setObjectName(QStringLiteral("assets"));
        QStandardItemModel model;
        for (int row = 0; row < 10000; ++row) model.appendRow(new QStandardItem(QStringLiteral("asset-%1").arg(row)));
        view.setModel(&model); view.resize(300, 180); view.show();
        QCoreApplication::processEvents();
        AppControl control;
        QString root = QStringLiteral("C:/library-a");
        control.setContextProvider([&] { return QJsonObject{{QStringLiteral("library_root"), root}}; });
        QJsonObject snapshot = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QString item;
        for (const QJsonValue& value : snapshot.value(QStringLiteral("nodes")).toArray()) {
            const QJsonObject node = value.toObject();
            if (node.value(QStringLiteral("kind")).toString() == QStringLiteral("modelitem")) { item = node.value(QStringLiteral("ref")).toString(); break; }
        }
        QVERIFY(!item.isEmpty());
        QJsonObject input = args(snapshot, item);
        control.handle(QStringLiteral("ui.click"), input);
        QTRY_VERIFY(!view.selectionModel()->selectedIndexes().isEmpty());
        model.clear();
        QCOMPARE(error(control.handle(QStringLiteral("ui.click"), input)), QStringLiteral("stale_ref"));
        QJsonObject widgetInput = args(snapshot, refFor(snapshot, QStringLiteral("assets")));
        root = QStringLiteral("C:/library-b");
        QCOMPARE(error(control.handle(QStringLiteral("ui.click"), widgetInput)), QStringLiteral("stale_snapshot"));
        root = QStringLiteral("C:/library-a");
        const QJsonObject next = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QVERIFY(snapshot.value(QStringLiteral("snapshot_id")) != next.value(QStringLiteral("snapshot_id")));
        QVERIFY(refFor(snapshot, QStringLiteral("assets")) != refFor(next, QStringLiteral("assets")));
        QCOMPARE(error(control.handle(QStringLiteral("ui.click"), widgetInput)), QStringLiteral("stale_snapshot"));
    }

    void deletedAndQueuedStaleTarget() {
        AppControl control;
        QString root = QStringLiteral("one");
        control.setContextProvider([&] { return QJsonObject{{QStringLiteral("library_root"), root}}; });
        auto* widget = new QPushButton(QStringLiteral("delete me")); widget->setObjectName(QStringLiteral("ephemeral")); widget->show();
        const QJsonObject snapshot = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        const QJsonObject input = args(snapshot, refFor(snapshot, QStringLiteral("ephemeral")));
        delete widget;
        QCOMPARE(error(control.handle(QStringLiteral("ui.click"), input)), QStringLiteral("stale_ref"));
        QPushButton button; button.setObjectName(QStringLiteral("queued")); button.show();
        const QJsonObject second = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QSignalSpy clicked(&button, &QPushButton::clicked);
        control.handle(QStringLiteral("ui.click"), args(second, refFor(second, QStringLiteral("queued"))), QStringLiteral("queued-change"));
        root = QStringLiteral("two");
        QCoreApplication::processEvents();
        QCOMPARE(clicked.size(), 0);
        const QJsonObject latest = data(control.handle(QStringLiteral("ui.snapshot"), {})).value(QStringLiteral("latest_action")).toObject();
        QCOMPARE(latest.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QCOMPARE(latest.value(QStringLiteral("request_id")).toString(), QStringLiteral("queued-change"));
    }

    void acceptedInputSurvivesObservation() {
        QPushButton button; button.setObjectName(QStringLiteral("observed")); button.show();
        AppControl control;
        const QJsonObject first = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QSignalSpy clicked(&button, &QPushButton::clicked);
        control.handle(QStringLiteral("ui.click"), args(first, refFor(first, QStringLiteral("observed"))), QStringLiteral("before-observe"));
        control.handle(QStringLiteral("ui.snapshot"), {});
        QTRY_COMPARE(clicked.size(), 1);
        QCOMPARE(data(control.handle(QStringLiteral("ui.snapshot"), {})).value(QStringLiteral("latest_action")).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("dispatched"));
    }

    void doubleClickStopsWhenFirstClickHidesReceiver() {
        ClickRecordingButton button;
        button.setObjectName(QStringLiteral("hideAfterClick"));
        connect(&button, &QPushButton::clicked, &button, &QWidget::hide);
        button.show();
        AppControl control;
        const QJsonObject snapshot = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QJsonObject input = args(snapshot, refFor(snapshot, QStringLiteral("hideAfterClick")));
        input.insert(QStringLiteral("count"), 2);
        control.handle(QStringLiteral("ui.click"), input);
        QTRY_VERIFY(!button.isVisible());
        QCOMPARE(button.presses, 1);
        QCOMPARE(button.releases, 1);
        QCOMPARE(button.doubleClicks, 0);

        // A visible receiver still gets the complete Qt double-click sequence.
        disconnect(&button, &QPushButton::clicked, &button, &QWidget::hide);
        button.show();
        control.handle(QStringLiteral("ui.click"), input);
        QTRY_COMPARE(button.doubleClicks, 1);
        QCOMPARE(button.presses, 2);
        QCOMPARE(button.releases, 3);
    }

    void doubleClickObservesQueuedNavigation() {
        ClickRecordingButton button;
        button.setObjectName(QStringLiteral("hideOnQueuedNavigation"));
        connect(&button, &QPushButton::clicked, &button, [&button] {
            QTimer::singleShot(0, &button, &QWidget::hide);
        });
        button.show();
        AppControl control;
        const QJsonObject snapshot = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QJsonObject input = args(snapshot, refFor(snapshot, QStringLiteral("hideOnQueuedNavigation")));
        input.insert(QStringLiteral("count"), 2);
        control.handle(QStringLiteral("ui.click"), input);
        QTRY_VERIFY(!button.isVisible());
        const auto completed = [&control] {
            return data(control.handle(QStringLiteral("ui.snapshot"), {})).value(QStringLiteral("latest_action")).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("dispatched");
        };
        QTRY_VERIFY(completed());
        QCOMPARE(button.presses, 1);
        QCOMPARE(button.releases, 1);
        QCOMPARE(button.doubleClicks, 0);
    }

    void modalWindowsBlockInputAndActions() {
        QPushButton main; main.setObjectName(QStringLiteral("main")); main.show();
        QAction mainAction(QStringLiteral("main action"), &main); mainAction.setObjectName(QStringLiteral("mainAction")); main.addAction(&mainAction);
        AppControl control;
        const QJsonObject before = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QSignalSpy clicked(&main, &QPushButton::clicked);
        control.handle(QStringLiteral("ui.click"), args(before, refFor(before, QStringLiteral("main"))), QStringLiteral("blocked-after-queue"));
        QDialog modal(&main); modal.setObjectName(QStringLiteral("modal")); modal.setWindowModality(Qt::ApplicationModal); modal.show();
        QCoreApplication::processEvents();
        QCOMPARE(clicked.size(), 0);
        const QJsonObject current = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        QCOMPARE(current.value(QStringLiteral("active_modal_ref")).toString(), refFor(current, QStringLiteral("modal")));
        QCOMPARE(current.value(QStringLiteral("latest_action")).toObject().value(QStringLiteral("error")).toString(), QStringLiteral("modal_blocked"));
        QCOMPARE(error(control.handle(QStringLiteral("ui.click"), args(current, refFor(current, QStringLiteral("main"))))), QStringLiteral("modal_blocked"));
        QCOMPARE(error(control.handle(QStringLiteral("ui.invoke"), args(current, refFor(current, QStringLiteral("mainAction"), QStringLiteral("action"))))), QStringLiteral("modal_blocked"));
        QJsonObject key = args(current, refFor(current, QStringLiteral("modal"))); key.insert(QStringLiteral("key"), QStringLiteral("Escape"));
        QVERIFY(data(control.handle(QStringLiteral("ui.key"), key)).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(!modal.isVisible());
    }

    void nativeArgumentsAreStrict() {
        AppControl control;
        QCOMPARE(error(control.handle(QStringLiteral("ui.snapshot"), {{QStringLiteral("unexpected"), true}})), QStringLiteral("invalid_arguments"));
        QCOMPARE(error(control.handle(QStringLiteral("ui.snapshot"), {{QStringLiteral("max_items"), 1.5}})), QStringLiteral("invalid_arguments"));
        QCOMPARE(error(control.handle(QStringLiteral("ui.click"), {})), QStringLiteral("invalid_arguments"));
    }

    void queuedModalActionCanBeObserved() {
        QWidget window; QAction action(QStringLiteral("Modal action"), &window);
        action.setObjectName(QStringLiteral("modal")); window.addAction(&action); window.show();
        AppControl control;
        bool observed = false, innerInvoked = false;
        connect(&action, &QAction::triggered, &window, [&] {
            QVERIFY(AppInputScope::active());
            QDialog dialog(&window);
            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
            QAction inner(QStringLiteral("inner"), &dialog); inner.setObjectName(QStringLiteral("inner")); dialog.addAction(&inner);
            connect(&inner, &QAction::triggered, &dialog, [&] { innerInvoked = AppInputScope::active(); });
            QTimer::singleShot(0, &dialog, [&] {
                QVERIFY(!AppInputScope::active());
                { AppInputScope nestedScope; QVERIFY(AppInputScope::active()); }
                QVERIFY(!AppInputScope::active());
                const QJsonObject nested = data(control.handle(QStringLiteral("ui.snapshot"), {}));
                observed = nested.value(QStringLiteral("latest_action")).toObject().value(QStringLiteral("state")).toString() == QStringLiteral("dispatching");
                const QJsonObject accepted = control.handle(QStringLiteral("ui.invoke"), args(nested, refFor(nested, QStringLiteral("inner"), QStringLiteral("action"))), QStringLiteral("inner-action"));
                QVERIFY(accepted.value(QStringLiteral("ok")).toBool());
                QTimer::singleShot(0, &dialog, &QDialog::accept);
            });
            dialog.exec();
        });
        const QJsonObject snapshot = data(control.handle(QStringLiteral("ui.snapshot"), {}));
        const QJsonObject result = data(control.handle(QStringLiteral("ui.invoke"), args(snapshot, refFor(snapshot, QStringLiteral("modal"), QStringLiteral("action")))));
        QVERIFY(result.value(QStringLiteral("queued")).toBool());
        QVERIFY(!observed);
        QTRY_VERIFY(observed);
        QVERIFY(innerInvoked);
        const QJsonObject latest = data(control.handle(QStringLiteral("ui.snapshot"), {})).value(QStringLiteral("latest_action")).toObject();
        QCOMPARE(latest.value(QStringLiteral("state")).toString(), QStringLiteral("dispatched"));
        QCOMPARE(latest.value(QStringLiteral("request_id")).toString(), QStringLiteral("inner-action"));
    }
};
QTEST_MAIN(AppControlTest)
#include "test_app_control.moc"
