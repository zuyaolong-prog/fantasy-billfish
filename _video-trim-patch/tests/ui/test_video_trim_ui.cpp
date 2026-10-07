#include "ui/player_trim_injector.hpp"
#include "ui/mpv_process_backend.hpp"
#include "ui/timeline_trim_overlay.hpp"
#include "ui/trim_diagnostics.hpp"
#include "ui/trim_editor_dialog.hpp"
#include "ui/trim_panel.hpp"
#include "ui/immersion_controls.hpp"
#include "ui/immersion_playback.hpp"
#include "ui/video_playback_shortcuts.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QCursor>
#include <QGridLayout>
#include <QMenu>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLibrary>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPluginLoader>
#include <QPushButton>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <Windows.h>

#include <cmath>

using billfish::trim::PlayerTrimInjector;
using billfish::trim::TimelineTrimOverlay;
using billfish::trim::TrimEditorDialog;
using billfish::trim::TrimMode;
using billfish::trim::TrimPanel;

namespace sdk_player {

class SdkPlayer final : public QObject {
    Q_OBJECT
public:
    enum State { Stopped = 0, Playing = 1, Paused = 2 };
    Q_ENUM(State)
    enum MediaStatus { Empty = 0, Loading = 1, Ready = 2 };
    Q_ENUM(MediaStatus)
    using QObject::QObject;
    Q_INVOKABLE qlonglong getPositionMs() const { return positionMs; }
    Q_INVOKABLE qlonglong getDurationMs() const { return durationMs; }
    Q_INVOKABLE qlonglong getVideoCurrentFrames() const { return currentFrames; }
    Q_INVOKABLE qlonglong getVideoTotalFrames() const { return totalFrames; }
    Q_INVOKABLE SdkPlayer::State GetPlayerState() const { return state; }
    Q_INVOKABLE SdkPlayer::MediaStatus GetMediaStatus() const { return mediaStatus; }
    qlonglong positionMs{0};
    qlonglong durationMs{377'000};
    qlonglong currentFrames{731};
    qlonglong totalFrames{9011};
    State state{Paused};
    MediaStatus mediaStatus{Ready};
    bool deferFrame{false};
public slots:
    void setPosition(qlonglong position) { positionMs = position; }
    void play() { state = Playing; }
    void pause() { state = Paused; }
    void frameBackward() {
        if (state == Paused && !deferFrame) positionMs = qMax(qlonglong{0}, positionMs - 40);
    }
    void frameForward() {
        if (state == Paused && !deferFrame) positionMs = qMin(durationMs, positionMs + 40);
    }
signals:
    void mediaStatusChanged(int status);
    void positionChanged(qlonglong position);
};

}  // namespace sdk_player

namespace {

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

bool createBillfishVideoLibrary(const QString& root, QString* error) {
    const QString databasePath = QDir(root).filePath(QStringLiteral(".bf/billfish.db"));
    if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        *error = QStringLiteral("Could not create Billfish database directory");
        return false;
    }
    const QString sqlitePath = QDir(QString::fromLocal8Bit(BILLFISH_TEST_ROOT))
                                   .filePath(QStringLiteral("sqlite3.dll"));
    QLibrary sqlite(sqlitePath);
    if (!sqlite.load()) {
        *error = QStringLiteral("Could not load SQLite fixture runtime: %1").arg(sqlite.errorString());
        return false;
    }
    using SqliteHandle = void;
    using OpenFunction = int (*)(const char*, SqliteHandle**, int, const char*);
    using ExecFunction = int (*)(SqliteHandle*, const char*, int (*)(void*, int, char**, char**),
                                 void*, char**);
    using CloseFunction = int (*)(SqliteHandle*);
    using FreeFunction = void (*)(void*);
    const auto open = reinterpret_cast<OpenFunction>(sqlite.resolve("sqlite3_open_v2"));
    const auto exec = reinterpret_cast<ExecFunction>(sqlite.resolve("sqlite3_exec"));
    const auto close = reinterpret_cast<CloseFunction>(sqlite.resolve("sqlite3_close"));
    const auto freeMemory = reinterpret_cast<FreeFunction>(sqlite.resolve("sqlite3_free"));
    if (!open || !exec || !close || !freeMemory) {
        *error = QStringLiteral("SQLite fixture runtime is missing symbols");
        return false;
    }
    SqliteHandle* database = nullptr;
    constexpr int readWriteCreate = 0x00000002 | 0x00000004;
    const QByteArray databaseUtf8 = QFileInfo(databasePath).absoluteFilePath().toUtf8();
    if (open(databaseUtf8.constData(), &database, readWriteCreate, nullptr) != 0 || !database) {
        *error = QStringLiteral("Could not open Billfish fixture database");
        if (database) {
            close(database);
        }
        return false;
    }
    const QByteArray sql =
        "CREATE TABLE bf_file (id INTEGER PRIMARY KEY, name TEXT, pid INTEGER);"
        "CREATE TABLE bf_folder (id INTEGER PRIMARY KEY, name TEXT, pid INTEGER);"
        "INSERT INTO bf_folder VALUES (10, 'Media', 0);"
        "INSERT INTO bf_file VALUES (42, 'a.mp4', 10);"
        "INSERT INTO bf_file VALUES (43, 'b.mp4', 10);"
        "CREATE TABLE bf_material_video (id INTEGER PRIMARY KEY, file_id INTEGER UNIQUE, duration INTEGER);"
        "INSERT INTO bf_material_video VALUES (1, 42, 10000);"
        "INSERT INTO bf_material_video VALUES (2, 43, 20000);";
    char* sqliteError = nullptr;
    const int status = exec(database, sql.constData(), nullptr, nullptr, &sqliteError);
    if (status != 0) {
        *error = sqliteError ? QString::fromUtf8(sqliteError)
                             : QStringLiteral("Could not create Billfish fixture schema");
        if (sqliteError) {
            freeMemory(sqliteError);
        }
        close(database);
        return false;
    }
    return close(database) == 0;
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), previous_(qgetenv(name)), existed_(qEnvironmentVariableIsSet(name)) {
        qputenv(name, value);
    }

    ~ScopedEnvironment() {
        if (existed_) {
            qputenv(name_.constData(), previous_);
        } else {
            qunsetenv(name_.constData());
        }
    }

private:
    QByteArray name_;
    QByteArray previous_;
    bool existed_{false};
};

struct FakeFfmpegPair final {
    QString ffmpegPath;
    QString ffprobePath;
};

bool createFakeFfmpegPair(
    const QString& rootDirectory,
    FakeFfmpegPair* pair,
    QString* error) {
    const QString toolDirectory = QDir(rootDirectory).filePath(QStringLiteral("fake-ffmpeg"));
    if (!QDir().mkpath(toolDirectory)) {
        *error = QStringLiteral("Could not create fake FFmpeg directory: %1").arg(toolDirectory);
        return false;
    }

    const QString source = QString::fromLocal8Bit(CORE_TEST_TOOL_PATH);
    if (!QFileInfo(source).isFile()) {
        *error = QStringLiteral("Core fake-tool PE does not exist: %1").arg(source);
        return false;
    }
    pair->ffmpegPath = QDir(toolDirectory).filePath(QStringLiteral("ffmpeg.exe"));
    pair->ffprobePath = QDir(toolDirectory).filePath(QStringLiteral("ffprobe.exe"));
    if (!QFile::copy(source, pair->ffmpegPath)
        || !QFile::copy(source, pair->ffprobePath)) {
        *error = QStringLiteral("Could not copy the x64 fake FFmpeg/FFprobe PE pair.");
        return false;
    }

    const QString qtCoreSource = QDir(QFileInfo(source).absolutePath())
                                     .filePath(QStringLiteral("Qt5Core.dll"));
    const QString qtCoreTarget = QDir(toolDirectory).filePath(QStringLiteral("Qt5Core.dll"));
    if (!QFileInfo(qtCoreSource).isFile() || !QFile::copy(qtCoreSource, qtCoreTarget)) {
        *error = QStringLiteral("Could not stage Qt5Core.dll beside the fake tools: %1")
                     .arg(qtCoreSource);
        return false;
    }
    if (!writeFile(
            QDir(toolDirectory).filePath(QStringLiteral("billfish-fake-ffmpeg.marker")),
            "fake-tool-contract-v1\n")) {
        *error = QStringLiteral("Could not write the fake FFmpeg contract marker.");
        return false;
    }
    return true;
}

class MediaSlider final : public QSlider {
    Q_OBJECT

public:
    explicit MediaSlider(QWidget* parent = nullptr) : QSlider(Qt::Horizontal, parent) {}
};

class PreviewDetailsWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 currentFileId READ currentFileId WRITE setCurrentFileId)

public:
    using QWidget::QWidget;

    [[nodiscard]] qint64 currentFileId() const noexcept { return currentFileId_; }
    void setCurrentFileId(const qint64 fileId) noexcept { currentFileId_ = fileId; }

signals:
    void signalPreviewFileIdChanged(qint64 fileId);
    void videoFramesChanged(qint64 secondFrames, qint64 currentFrames);
    void videoTotalFramesChanged(qint64 totalFrames, qint64 lastSecondFrames);

private:
    qint64 currentFileId_{-1};
};

class PreviewControlWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PreviewControlWidget(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        timeline = new MediaSlider(this);
        timeline->setObjectName(QStringLiteral("PreviewControlWidget_m_pVideoSlider"));
        timeline->setRange(0, 10'000);
        timeline->setValue(0);
        play = new QPushButton(this);
        play->setObjectName(QStringLiteral("PreviewControlWidget_StartBnt"));
        pause = new QPushButton(this);
        pause->setObjectName(QStringLiteral("PreviewControlWidget_PauseBnt"));
        frame = new QToolButton(this);
        frame->setObjectName(QStringLiteral("PreviewControlWidget_frame_btn"));
        auto* frameMenu = new QMenu(frame);
        frameMenu->addAction(QStringLiteral("保存当前帧"));
        frame->setMenu(frameMenu);
        frame->setPopupMode(QToolButton::InstantPopup);
        layout->addWidget(timeline, 1);
        layout->addWidget(play);
        layout->addWidget(pause);
        layout->addWidget(frame);
    }

    MediaSlider* timeline{nullptr};
    QPushButton* play{nullptr};
    QPushButton* pause{nullptr};
    QToolButton* frame{nullptr};

public slots:
    void slot_updatePlayPos(int positionMs) {
        const QSignalBlocker blocker(timeline);
        timeline->setValue(positionMs);
    }
};

struct PlayerFixture final {
    QWidget root;
    QVBoxLayout layout{&root};
    PreviewDetailsWidget details{&root};
    PreviewControlWidget control{&root};

    explicit PlayerFixture(const QString& sourcePath = {}, const qint64 durationMs = 10'000) {
        root.setObjectName(QStringLiteral("FakePreviewWindow"));
        if (!sourcePath.isEmpty()) {
            root.setProperty("billfish.trim.inputPath", sourcePath);
            if (durationMs > 0) {
                root.setProperty("billfish.trim.durationMs", durationMs);
            }
        }
        layout.addWidget(&details);
        layout.addWidget(&control);
        root.resize(980, 220);
        root.show();
        QCoreApplication::processEvents();
    }
};

class BfLocalResWidget final : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};

class PreviewWidget final : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};

class PreviewStackWidget final : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};

class PreviewVideoWidget final : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};

class MediaWidget final : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
};

struct ImmersionFixture final {
    BfLocalResWidget root;
    QStackedWidget* pages;
    PreviewDetailsWidget* ordinary;
    PreviewWidget* immersion;
    PreviewVideoWidget* ordinaryVideo;
    PreviewVideoWidget* immersionVideo;
    QWidget* ordinarySurface;
    QWidget* immersionSurface;
    PreviewControlWidget* ordinaryControl;
    PreviewControlWidget* immersionControl;
    sdk_player::SdkPlayer* ordinarySdk;
    sdk_player::SdkPlayer* immersionSdk;
    QPushButton* enter;
    QPushButton* exit;
    QPushButton* previous;
    QPushButton* next;

    explicit ImmersionFixture(bool registerImmersion = true) {
        auto* rootLayout = new QVBoxLayout(&root);
        pages = new QStackedWidget(&root);
        rootLayout->addWidget(pages);
        ordinary = new PreviewDetailsWidget(pages);
        immersion = new PreviewWidget(pages);
        pages->addWidget(ordinary);
        pages->addWidget(immersion);
        const auto makePlayer = [](QWidget* host, PreviewVideoWidget** video,
                                   QWidget** surface,
                                   PreviewControlWidget** control,
                                   sdk_player::SdkPlayer** sdk) {
            auto* hostLayout = new QVBoxLayout(host);
            auto* stack = new PreviewStackWidget(host);
            hostLayout->addWidget(stack);
            auto* stackLayout = new QVBoxLayout(stack);
            *video = new PreviewVideoWidget(stack);
            stackLayout->addWidget(*video);
            auto* videoLayout = new QGridLayout(*video);
            videoLayout->setContentsMargins(0, 0, 0, 0);
            *surface = new MediaWidget(*video);
            (*surface)->setObjectName(QStringLiteral("MediaWidget"));
            videoLayout->addWidget(*surface, 0, 0);
            videoLayout->setRowStretch(0, 1);
            *control = new PreviewControlWidget(*video);
            (*control)->setFixedHeight(78);
            videoLayout->addWidget(*control, 1, 0);
            *sdk = new sdk_player::SdkPlayer(*video);
            (*control)->timeline->setRange(0, static_cast<int>((*sdk)->durationMs));
        };
        makePlayer(ordinary, &ordinaryVideo, &ordinarySurface, &ordinaryControl, &ordinarySdk);
        makePlayer(immersion, &immersionVideo, &immersionSurface, &immersionControl, &immersionSdk);
        enter = new QPushButton(ordinary);
        enter->setObjectName(QStringLiteral("ImmersionBtn"));
        ordinary->layout()->addWidget(enter);
        exit = new QPushButton(immersion);
        exit->setObjectName(QStringLiteral("materialviewer_exit_imm"));
        immersion->layout()->addWidget(exit);
        previous = new QPushButton(immersion);
        previous->setWindowFlags(Qt::Tool);
        previous->setObjectName(QStringLiteral("materialviewer_prev_file"));
        next = new QPushButton(immersion);
        next->setWindowFlags(Qt::Tool);
        next->setObjectName(QStringLiteral("materialviewer_next_file"));
        root.resize(900, 600);
        root.show();
        QCoreApplication::processEvents();
        billfish::trim::ImmersionPlayback::install(ordinaryControl);
        if (registerImmersion) billfish::trim::ImmersionPlayback::install(immersionControl);
    }

    void ready(sdk_player::SdkPlayer* sdk) {
        sdk->positionMs = 0;
        sdk->mediaStatus = sdk_player::SdkPlayer::Ready;
        emit sdk->mediaStatusChanged(2);
        sdk->play();  // The vendor starts playback immediately after Ready.
        QCoreApplication::processEvents();
    }

    sdk_player::SdkPlayer* activatePreview(int mode) {
        billfish::trim::VideoPlaybackShortcuts::install(ordinaryControl);
        billfish::trim::VideoPlaybackShortcuts::install(immersionControl);
        const bool immersive = mode == 1;
        pages->setCurrentWidget(immersive ? static_cast<QWidget*>(immersion)
                                         : static_cast<QWidget*>(ordinary));
        auto* surface = immersive ? immersionSurface : ordinarySurface;
        if (mode == 2) {
            // The vendor fullscreen surface remains owned by the video widget,
            // while PreviewVideoWidget itself stays in the ordinary main window.
            surface->setWindowFlags(Qt::Window);
            surface->showFullScreen();
        }
        surface->setFocusPolicy(Qt::StrongFocus);
        QCoreApplication::processEvents();
        QApplication::setActiveWindow(surface->window());
        surface->setFocus();
        return immersive ? immersionSdk : ordinarySdk;
    }
};

}  // namespace

class VideoTrimUiTest final : public QObject {
    Q_OBJECT

private slots:
    void playbackShortcutsSeekVisiblePlayer_data();
    void playbackShortcutsSeekVisiblePlayer();
    void playbackShortcutsStepFrames_data();
    void playbackShortcutsStepFrames();
    void playbackShortcutsAdjustSeekStepAcrossPreviewModes();
    void playbackShortcutsRespectInputScope_data();
    void playbackShortcutsRespectInputScope();
    void playbackShortcutsOverrideStockNavigationOnlyInPreview();
    void immersionTransfersPlayback_data();
    void immersionTransfersPlayback();
    void immersionDoesNotSeekDifferentNavigation_data();
    void immersionDoesNotSeekDifferentNavigation();
    void immersionControlsFollowBottomHover();
    void overlayPreservesStockSliderContract();
    void panelUsesOneTimelineAndSetPointsUsePlayhead();
    void panelShowsSelectionRelativeTime();
    void selectionChangeSeeksOutOfRangePlayheadToIn();
    void panelLoopsSelectedRange();
    void fileIdSignalIsConnectedByMetaObject();
    void currentFileIdIsSynchronizedWithoutUserSwitch();
    void lateFileIdSourceIsReboundAfterInjection();
    void diagnosticsAreOptIn();
    void diagnosticsStayBoundedAndSummarized();
    void sourceDoesNotPropagateAcrossPlayerRoots();
    void injectorRejectsAmbiguousPreviewScope();
    void samePreviewHostReopensCurrentSourceAndExportsIt();
    void injectionPreservesHostSliderWidth();
    void unknownDurationIsProbedOffTheGuiThread();
    void actionsAndModesAreVisible();
    void editorDialogUsesCompactReferenceGeometry();
    void editorDialogAppliesVerifiedSourceAndClosesCleanly();
    void editorDialogStartsAtCapturedFrameAndScrubsLive();
    void editorDialogProbesUnknownDurationWithoutPlayback();
    void editorDialogExportsItsSourceSnapshot();
    void openEditorSnapshotsSourceAcrossGlobalUpdates();
    void injectorUsesExactEvidenceAndPreventsDuplicates();
    void injectorSupportsExplicitOverride();
    void injectorDebouncesEventStorms();
    void exportRunsCoreOffTheGuiThread();
    void exportFailureReturnsToInteractiveState();
    void previewProxyForwardsTheQtPluginContract();
};

void VideoTrimUiTest::playbackShortcutsSeekVisiblePlayer_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("key");
    QTest::addColumn<bool>("playing");
    QTest::addColumn<qlonglong>("position");
    QTest::addColumn<qlonglong>("expected");
    QTest::newRow("ordinary-up-playing") << 0 << int(Qt::Key_Up) << true << qlonglong{8000} << qlonglong{13000};
    QTest::newRow("ordinary-down-paused") << 0 << int(Qt::Key_Down) << false << qlonglong{8000} << qlonglong{3000};
    QTest::newRow("immersion-up-paused") << 1 << int(Qt::Key_Up) << false << qlonglong{8000} << qlonglong{13000};
    QTest::newRow("immersion-down-playing") << 1 << int(Qt::Key_Down) << true << qlonglong{8000} << qlonglong{3000};
    QTest::newRow("fullscreen-clamp-end") << 2 << int(Qt::Key_Up) << true << qlonglong{376000} << qlonglong{377000};
    QTest::newRow("fullscreen-clamp-start") << 2 << int(Qt::Key_Down) << false << qlonglong{2000} << qlonglong{0};
}

void VideoTrimUiTest::playbackShortcutsSeekVisiblePlayer() {
    QFETCH(int, mode);
    QFETCH(int, key);
    QFETCH(bool, playing);
    QFETCH(qlonglong, position);
    QFETCH(qlonglong, expected);
    ImmersionFixture fixture;
    auto* sdk = fixture.activatePreview(mode);
    if (mode == 2) {
        QCOMPARE(fixture.ordinarySurface->parentWidget(), fixture.ordinaryVideo);
        QVERIFY(fixture.ordinaryVideo->isVisible());
        QCOMPARE(fixture.ordinaryVideo->window(), static_cast<QWidget*>(&fixture.root));
        QCOMPARE(QApplication::activeWindow(), fixture.ordinarySurface);
        QCOMPARE(QApplication::focusWidget(), fixture.ordinarySurface);
    }
    auto* untouched = sdk == fixture.ordinarySdk ? fixture.immersionSdk : fixture.ordinarySdk;
    untouched->positionMs = 1234;
    sdk->positionMs = position;
    sdk->currentFrames = 3337;
    const auto state = playing ? sdk_player::SdkPlayer::Playing : sdk_player::SdkPlayer::Paused;
    sdk->state = state;
    QVERIFY(QApplication::focusWidget() != nullptr);
    QTest::keyClick(QApplication::focusWidget(), static_cast<Qt::Key>(key));
    QCOMPARE(sdk->positionMs, expected);
    QCOMPARE(sdk->state, state);
    QCOMPARE(untouched->positionMs, qlonglong{1234});
    auto* surface = mode == 1 ? fixture.immersionSurface : fixture.ordinarySurface;
    auto* osd = surface->findChild<QLabel*>(QStringLiteral("BillfishVideoPlaybackOsd"));
    QVERIFY(osd != nullptr);
    QVERIFY(osd->isVisible());
    QCOMPARE(osd->parentWidget(), surface);
    QVERIFY(osd->text().contains(QTime(0, 0).addMSecs(int(expected)).toString(QStringLiteral("HH:mm:ss.zzz"))));
    QVERIFY(osd->text().contains(QStringLiteral("00:06:17.000")));
    QVERIFY(osd->text().contains(QStringLiteral("3337")));
    QVERIFY(osd->text().contains(QStringLiteral("9011")));
    if (!playing) {
        auto* control = mode == 1 ? fixture.immersionControl : fixture.ordinaryControl;
        QCOMPARE(control->timeline->value(), int(expected));
    }
    if (mode == 0 && playing) {
        sdk->positionMs = expected + 42;
        sdk->currentFrames = 3342;
        emit sdk->positionChanged(sdk->positionMs);
        QVERIFY(osd->text().contains(QStringLiteral("00:00:13.042")));
        QVERIFY(osd->text().contains(QStringLiteral("3342")));
        QCOMPARE(sdk->state, state);
        QWidget otherWindow(&fixture.root, Qt::Window);
        otherWindow.show();
        QCoreApplication::processEvents();
        QApplication::setActiveWindow(&otherWindow);
        QCoreApplication::processEvents();
        QVERIFY(!osd->isVisible());
    }
}

void VideoTrimUiTest::playbackShortcutsStepFrames_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("direction");
    QTest::addColumn<bool>("asynchronous");
    for (int mode = 0; mode < 3; ++mode) {
        const QByteArray name = QByteArray::number(mode);
        QTest::newRow((name + "-backward").constData()) << mode << int(Qt::Key_S) << -1 << false;
        QTest::newRow((name + "-forward").constData()) << mode << int(Qt::Key_D) << 1 << false;
    }
    QTest::newRow("asynchronous-forward-after-release") << 0 << int(Qt::Key_D) << 1 << true;
}

void VideoTrimUiTest::playbackShortcutsStepFrames() {
    QFETCH(int, mode);
    QFETCH(int, key);
    QFETCH(int, direction);
    QFETCH(bool, asynchronous);
    ImmersionFixture fixture;
    auto* sdk = fixture.activatePreview(mode);
    if (mode == 2) {
        QVERIFY(fixture.ordinaryVideo->isVisible());
        QCOMPARE(QApplication::activeWindow(), fixture.ordinarySurface);
        QCOMPARE(QApplication::focusWidget(), fixture.ordinarySurface);
    }
    auto* untouched = sdk == fixture.ordinarySdk ? fixture.immersionSdk : fixture.ordinarySdk;
    untouched->positionMs = 1234;
    sdk->positionMs = 8000;
    sdk->state = sdk_player::SdkPlayer::Playing;
    auto* receiver = QApplication::focusWidget();
    QVERIFY(receiver != nullptr);
    QTest::keyPress(receiver, static_cast<Qt::Key>(key));
    QCOMPARE(sdk->state, sdk_player::SdkPlayer::Paused);
    QCOMPARE(sdk->positionMs, qlonglong{8000 + direction * 40});
    for (int repeat = 0; repeat < 3; ++repeat) {
        sdk->deferFrame = asynchronous && repeat == 2;
        QKeyEvent repeated(QEvent::KeyPress, key, Qt::NoModifier, QString(), true, 1);
        QApplication::sendEvent(receiver, &repeated);
    }
    const qlonglong afterRepeats = 8000 + direction * 160;
    QCOMPARE(sdk->positionMs, afterRepeats - (asynchronous ? direction * 40 : 0));
    QCOMPARE(sdk->state, sdk_player::SdkPlayer::Paused);
    QTest::keyRelease(receiver, static_cast<Qt::Key>(key));
    if (asynchronous) {
        sdk->positionMs = afterRepeats;
        sdk->currentFrames = 957;
        emit sdk->positionChanged(afterRepeats);
        QCOMPARE(fixture.ordinaryControl->timeline->value(), int(afterRepeats));
        auto* osd = fixture.ordinarySurface->findChild<QLabel*>(QStringLiteral("BillfishVideoPlaybackOsd"));
        QVERIFY(osd != nullptr && osd->isVisible());
        QVERIFY(osd->text().contains(QStringLiteral("00:00:08.160")));
        QVERIFY(osd->text().contains(QStringLiteral("957")));
        QVERIFY(osd->text().contains(QStringLiteral("9011")));
        sdk->mediaStatus = sdk_player::SdkPlayer::Loading;
        emit sdk->mediaStatusChanged(1);
        QVERIFY(!osd->isVisible());
    }
    QTest::qWait(100);
    QCOMPARE(sdk->positionMs, afterRepeats);
    QCOMPARE(untouched->positionMs, qlonglong{1234});
}

void VideoTrimUiTest::playbackShortcutsAdjustSeekStepAcrossPreviewModes() {
    ImmersionFixture fixture;
    auto* sdk = fixture.activatePreview(0);
    sdk->positionMs = 8000;
    sdk->state = sdk_player::SdkPlayer::Playing;
    for (int press = 0; press < 6; ++press)
        QTest::keyClick(fixture.ordinarySurface, Qt::Key_C);
    QCOMPARE(sdk->positionMs, qlonglong{8000});
    QCOMPARE(sdk->state, sdk_player::SdkPlayer::Playing);
    QTest::keyClick(fixture.ordinarySurface, Qt::Key_Up);
    QCOMPARE(sdk->positionMs, qlonglong{9000});
    for (int press = 0; press < 3; ++press)
        QTest::keyClick(fixture.ordinarySurface, Qt::Key_V);
    QCOMPARE(sdk->positionMs, qlonglong{9000});
    QCOMPARE(sdk->state, sdk_player::SdkPlayer::Playing);
    for (const Qt::Key key : {Qt::Key_C, Qt::Key_V}) {
        QKeyEvent repeated(QEvent::KeyPress, key, Qt::NoModifier, QString(), true, 1);
        QApplication::sendEvent(fixture.ordinarySurface, &repeated);
        sdk->positionMs = 8000;
        QTest::keyClick(fixture.ordinarySurface, Qt::Key_Up);
        QCOMPARE(sdk->positionMs, qlonglong{12000});
    }
    sdk = fixture.activatePreview(1);
    sdk->positionMs = 8000;
    QTest::keyClick(fixture.immersionSurface, Qt::Key_Down);
    QCOMPARE(sdk->positionMs, qlonglong{4000});
    // Restore the session default for subsequent fixtures through the public keys.
    QTest::keyClick(fixture.immersionSurface, Qt::Key_V);
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        // Qt offscreen tool/tooltip destruction clears the active window. The
        // Windows platform retains the preview focus without this fixture repair.
        QApplication::setActiveWindow(fixture.immersionSurface->window());
        fixture.immersionSurface->setFocus();
    } else {
        QCOMPARE(QApplication::activeWindow(), fixture.immersionSurface->window());
        QCOMPARE(QApplication::focusWidget(), fixture.immersionSurface);
    }
    sdk->positionMs = 8000;
    QTest::keyClick(fixture.immersionSurface, Qt::Key_Up);
    QCOMPARE(sdk->positionMs, qlonglong{13000});
}

void VideoTrimUiTest::playbackShortcutsRespectInputScope_data() {
    QTest::addColumn<int>("scope");
    QTest::addColumn<int>("adjustmentKey");
    const char* names[] = {"text-entry", "popup", "modal", "other-owned-window"};
    for (int scope = 0; scope < 4; ++scope) {
        const QByteArray name(names[scope]);
        QTest::newRow((name + "-decrease").constData()) << scope << int(Qt::Key_C);
        QTest::newRow((name + "-increase").constData()) << scope << int(Qt::Key_V);
    }
}

void VideoTrimUiTest::playbackShortcutsRespectInputScope() {
    QFETCH(int, scope);
    QFETCH(int, adjustmentKey);
    ImmersionFixture fixture;
    fixture.activatePreview(0);
    QLineEdit edit(fixture.ordinaryVideo);
    QMenu popup(&fixture.root);
    popup.addAction(QStringLiteral("First"));
    popup.addAction(QStringLiteral("Second"));
    QDialog modal(&fixture.root);
    QWidget otherWindow(fixture.ordinaryVideo, Qt::Window);
    QWidget* receiver = nullptr;
    if (scope == 0) {
        edit.show();
        edit.setFocus();
        receiver = &edit;
    } else if (scope == 1) {
        popup.popup(fixture.ordinarySurface->mapToGlobal(QPoint(20, 20)));
        receiver = &popup;
    } else if (scope == 2) {
        modal.setWindowModality(Qt::ApplicationModal);
        modal.show();
        QApplication::setActiveWindow(&modal);
        receiver = &modal;
    } else {
        otherWindow.show();
        QApplication::setActiveWindow(&otherWindow);
        receiver = &otherWindow;
    }
    QCoreApplication::processEvents();
    fixture.ordinarySdk->positionMs = 8000;
    fixture.ordinarySdk->state = sdk_player::SdkPlayer::Playing;
    for (const Qt::Key key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_S, Qt::Key_D}) {
        QTest::keyClick(receiver, key);
        QCOMPARE(fixture.ordinarySdk->positionMs, qlonglong{8000});
        QCOMPARE(fixture.ordinarySdk->state, sdk_player::SdkPlayer::Playing);
    }
    if (scope == 0) QCOMPARE(edit.text(), QStringLiteral("sd"));
    QTest::keyClick(receiver, static_cast<Qt::Key>(adjustmentKey));
    edit.hide();
    popup.close();
    modal.close();
    otherWindow.hide();
    QApplication::setActiveWindow(&fixture.root);
    fixture.ordinarySurface->setFocus();
    QCoreApplication::processEvents();
    QTest::keyClick(fixture.ordinarySurface, Qt::Key_Up);
    QCOMPARE(fixture.ordinarySdk->positionMs, qlonglong{13000});
}

void VideoTrimUiTest::playbackShortcutsOverrideStockNavigationOnlyInPreview() {
    ImmersionFixture fixture;
    fixture.activatePreview(0);
    QShortcut stockUp(QKeySequence(Qt::Key_Up), &fixture.root);
    QShortcut stockDown(QKeySequence(Qt::Key_Down), &fixture.root);
    QSignalSpy upActivated(&stockUp, &QShortcut::activated);
    QSignalSpy downActivated(&stockDown, &QShortcut::activated);
    fixture.ordinarySdk->positionMs = 8000;
    QTest::keyClick(fixture.ordinarySurface, Qt::Key_Up);
    QCOMPARE(fixture.ordinarySdk->positionMs, qlonglong{13000});
    QTest::keyClick(fixture.ordinarySurface, Qt::Key_Down);
    QCOMPARE(fixture.ordinarySdk->positionMs, qlonglong{8000});
    QCOMPARE(upActivated.count(), 0);
    QCOMPARE(downActivated.count(), 0);

    fixture.ordinaryVideo->hide();
    fixture.enter->setFocus();
    QCoreApplication::processEvents();
    QTest::keyClick(fixture.enter, Qt::Key_Up);
    QTest::keyClick(fixture.enter, Qt::Key_Down);
    QCOMPARE(upActivated.count(), 1);
    QCOMPARE(downActivated.count(), 1);
    QCOMPARE(fixture.ordinarySdk->positionMs, qlonglong{8000});
}

void VideoTrimUiTest::immersionTransfersPlayback_data() {
    QTest::addColumn<bool>("playing");
    QTest::addColumn<bool>("escapeExit");
    QTest::addColumn<bool>("lazyTarget");
    QTest::newRow("paused-button-exit") << false << false << false;
    QTest::newRow("playing-shortcut-exit") << true << true << false;
    QTest::newRow("paused-first-entry") << false << false << true;
    QTest::newRow("playing-first-entry") << true << true << true;
}

void VideoTrimUiTest::immersionTransfersPlayback() {
    QFETCH(bool, playing);
    QFETCH(bool, escapeExit);
    QFETCH(bool, lazyTarget);
    ImmersionFixture fixture(!lazyTarget);
    const auto state = playing ? sdk_player::SdkPlayer::Playing : sdk_player::SdkPlayer::Paused;
    fixture.ordinarySdk->positionMs = 20'967;
    fixture.ordinarySdk->state = state;
    fixture.enter->click();
    fixture.pages->setCurrentWidget(fixture.immersion);
    fixture.ordinarySdk->positionMs = 0;
    fixture.ordinarySdk->state = sdk_player::SdkPlayer::Stopped;
    emit fixture.immersionSdk->mediaStatusChanged(1);
    fixture.ready(fixture.immersionSdk);
    if (lazyTarget) billfish::trim::ImmersionPlayback::install(fixture.immersionControl);
    QTRY_COMPARE(fixture.immersionSdk->positionMs, qlonglong{20'967});
    QTRY_COMPARE(fixture.immersionSdk->state, state);
    if (!playing) QCOMPARE(fixture.immersionControl->timeline->value(), 20'967);

    fixture.immersionSdk->positionMs = 83'456;
    const auto exitAndLoad = [&fixture] {
        fixture.pages->setCurrentWidget(fixture.ordinary);
        fixture.immersionSdk->positionMs = 0;
        fixture.immersionSdk->state = sdk_player::SdkPlayer::Stopped;
        emit fixture.ordinarySdk->mediaStatusChanged(1);
        fixture.ordinarySdk->positionMs = 0;
        emit fixture.ordinarySdk->mediaStatusChanged(2);
        fixture.ordinarySdk->play();
    };
    if (escapeExit) {
        QShortcut shortcut(QKeySequence(Qt::Key_Escape), fixture.immersion);
        connect(&shortcut, &QShortcut::activated, this, exitAndLoad);
        QSignalSpy activated(&shortcut, &QShortcut::activated);
        QApplication::setActiveWindow(&fixture.root);
        QTest::keyClick(fixture.immersionVideo, Qt::Key_Escape);
        QCOMPARE(activated.count(), 1);
    } else {
        fixture.exit->click();
        exitAndLoad();
    }
    QTRY_COMPARE(fixture.ordinarySdk->positionMs, qlonglong{83'456});
    QCOMPARE(fixture.ordinarySdk->state, state);
    if (!playing) QCOMPARE(fixture.ordinaryControl->timeline->value(), 83'456);
}

void VideoTrimUiTest::immersionDoesNotSeekDifferentNavigation_data() {
    QTest::addColumn<int>("navigation");
    QTest::newRow("different-duration") << 0;
    QTest::newRow("another-load") << 1;
    QTest::newRow("next-asset-key") << 2;
}

void VideoTrimUiTest::immersionDoesNotSeekDifferentNavigation() {
    QFETCH(int, navigation);
    ImmersionFixture fixture;
    fixture.ordinarySdk->positionMs = 20'967;
    fixture.enter->click();
    fixture.pages->setCurrentWidget(fixture.immersion);
    emit fixture.immersionSdk->mediaStatusChanged(1);
    if (navigation == 0) fixture.immersionSdk->durationMs = 125'000;
    else if (navigation == 1) emit fixture.immersionSdk->mediaStatusChanged(1);
    else QTest::keyClick(fixture.immersionVideo, Qt::Key_Right);
    fixture.ready(fixture.immersionSdk);
    QCOMPARE(fixture.immersionSdk->positionMs, qlonglong{0});
    QCOMPARE(fixture.immersionSdk->state, sdk_player::SdkPlayer::Playing);
}

void VideoTrimUiTest::immersionControlsFollowBottomHover() {
    ImmersionFixture fixture;
    const QPoint originalCursor = QCursor::pos();
    struct RestoreCursor {
        QPoint position;
        ~RestoreCursor() { QCursor::setPos(position); }
    } restoreCursor{originalCursor};
    QCursor::setPos(fixture.root.mapToGlobal(QPoint(0, 0)));
    billfish::trim::ImmersionControls::install(fixture.ordinaryControl);
    billfish::trim::ImmersionControls::install(fixture.immersionControl);
    QVERIFY(!fixture.ordinaryControl->isHidden());
    fixture.pages->setCurrentWidget(fixture.immersion);
    QTRY_VERIFY(fixture.immersionControl->isHidden());
    QTRY_VERIFY(fixture.previous->isHidden());
    QTRY_VERIFY(fixture.next->isHidden());
    QVERIFY(!fixture.ordinaryControl->isHidden());
    QTRY_COMPARE(fixture.immersionSurface->geometry(), fixture.immersionVideo->rect());
    const QRect surfaceWithoutControls = fixture.immersionSurface->geometry();
    QCursor::setPos(fixture.immersionVideo->mapToGlobal(
        QPoint(fixture.immersionVideo->width() / 2, fixture.immersionVideo->height() - 4)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.immersionControl->isVisible(), 1000);
    QCOMPARE(fixture.immersionSurface->geometry(), surfaceWithoutControls);
    QCursor::setPos(fixture.immersionVideo->mapToGlobal(
        QPoint(fixture.immersionVideo->width() / 2, 20)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.immersionControl->isHidden(), 2000);
    QCOMPARE(fixture.immersionSurface->geometry(), surfaceWithoutControls);
    fixture.root.resize(fixture.root.width() + 120, fixture.root.height() + 80);
    QTRY_VERIFY(fixture.immersionSurface->width() > surfaceWithoutControls.width());
    QTRY_COMPARE(fixture.immersionSurface->geometry(), fixture.immersionVideo->rect());
    const QRect resizedSurface = fixture.immersionSurface->geometry();
    QCursor::setPos(fixture.immersionVideo->mapToGlobal(
        QPoint(fixture.immersionVideo->width() / 2, fixture.immersionVideo->height() - 4)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.immersionControl->isVisible(), 1000);
    QCOMPARE(fixture.immersionSurface->geometry(), resizedSurface);
    QCOMPARE(fixture.immersionControl->parentWidget(), fixture.immersionVideo);
    QCOMPARE(fixture.immersionControl->width(), fixture.immersionVideo->width());
    QCOMPARE(fixture.immersionControl->geometry().bottom(), fixture.immersionVideo->rect().bottom());
    QCursor::setPos(fixture.immersion->mapToGlobal(
        QPoint(4, fixture.immersion->height() / 2)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.previous->isVisible(), 1000);
    QVERIFY(fixture.next->isHidden());
    QCursor::setPos(fixture.immersion->mapToGlobal(
        QPoint(fixture.immersion->width() - 4, fixture.immersion->height() / 2)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.next->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.previous->isHidden(), 2000);
    QCursor::setPos(fixture.immersion->mapToGlobal(fixture.immersion->rect().center()));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.next->isHidden(), 2000);
    QCursor::setPos(fixture.immersion->mapToGlobal(
        QPoint(4, fixture.immersion->height() / 2)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.previous->isVisible(), 1000);
    fixture.root.hide();
    QTRY_VERIFY(fixture.previous->isHidden());
    QTRY_VERIFY(fixture.next->isHidden());
    // A vendor Show after leaving must not expose a child tool window on the ordinary page.
    fixture.previous->show();
    fixture.next->show();
    QTRY_VERIFY(fixture.previous->isHidden());
    QTRY_VERIFY(fixture.next->isHidden());
    QCursor::setPos(fixture.immersion->mapToGlobal(fixture.immersion->rect().center()));
    fixture.root.show();
    QTRY_VERIFY(fixture.immersionControl->isHidden());
    QVERIFY(fixture.previous->isHidden());
    QVERIFY(fixture.next->isHidden());
    QCursor::setPos(fixture.immersion->mapToGlobal(
        QPoint(4, fixture.immersion->height() / 2)));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.previous->isVisible(), 1000);
    fixture.pages->setCurrentWidget(fixture.ordinary);
    QTRY_VERIFY(!fixture.immersionControl->isHidden());
    QTRY_VERIFY(fixture.previous->isHidden());
    QTRY_VERIFY(fixture.next->isHidden());
    QVERIFY(fixture.ordinaryControl->isVisible());
}

void VideoTrimUiTest::overlayPreservesStockSliderContract() {
    MediaSlider slider;
    slider.setObjectName(QStringLiteral("originalTimeline"));
    slider.setRange(100, 1100);
    slider.setValue(500);
    slider.resize(620, 44);
    slider.show();
    QCoreApplication::processEvents();

    QSignalSpy rangeSpy(&slider, &QSlider::rangeChanged);
    QSignalSpy valueSpy(&slider, &QSlider::valueChanged);
    TimelineTrimOverlay overlay(&slider);
    overlay.setSelection(300, 900);
    QCOMPARE(overlay.timeline(), &slider);
    QCOMPARE(slider.objectName(), QStringLiteral("originalTimeline"));
    QCOMPARE(slider.minimum(), 100);
    QCOMPARE(slider.maximum(), 1100);
    QCOMPARE(rangeSpy.count(), 0);

    const int playheadBeforeScrub = slider.value();
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(320, slider.height() / 2));
    QTest::mouseMove(&slider, QPoint(390, slider.height() / 2));
    QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(390, slider.height() / 2));
    QVERIFY(slider.value() != playheadBeforeScrub);
    QVERIFY(valueSpy.count() > 0);

    slider.setValue(600);
    const int stockValueBeforeHandleDrag = slider.value();
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(132, slider.height() / 2));
    QTest::mouseMove(&slider, QPoint(210, slider.height() / 2));
    QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(210, slider.height() / 2));
    QVERIFY(overlay.inValue() > 300);
    QCOMPARE(slider.value(), stockValueBeforeHandleDrag);
    QCOMPARE(rangeSpy.count(), 0);
}

void VideoTrimUiTest::panelUsesOneTimelineAndSetPointsUsePlayhead() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture fixture(input);
    const int originalMinimum = fixture.control.timeline->minimum();
    const int originalMaximum = fixture.control.timeline->maximum();
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));

    QCOMPARE(fixture.root.findChildren<QSlider*>().size(), 1);
    QCOMPARE(panel.hostTimeline(), fixture.control.timeline);
    QCOMPARE(panel.timelineOverlay()->parentWidget(), fixture.control.timeline);
    QCOMPARE(panel.findChildren<QSlider*>().size(), 0);

    fixture.control.timeline->setValue(2500);
    auto* setIn = panel.findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimSetInButton"));
    auto* setOut = panel.findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimSetOutButton"));
    QVERIFY(setIn != nullptr);
    QVERIFY(setOut != nullptr);
    QTest::mouseClick(setIn, Qt::LeftButton);
    fixture.control.timeline->setValue(7000);
    QTest::mouseClick(setOut, Qt::LeftButton);

    QCOMPARE(panel.timelineOverlay()->inValue(), 2500);
    QCOMPARE(panel.timelineOverlay()->outValue(), 7000);
    QCOMPARE(panel.selection().startMs, qint64{2500});
    QCOMPARE(panel.selection().endMs, qint64{7000});
    QCOMPARE(fixture.control.timeline->minimum(), originalMinimum);
    QCOMPARE(fixture.control.timeline->maximum(), originalMaximum);
}

void VideoTrimUiTest::panelShowsSelectionRelativeTime() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture fixture(input);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));
    panel.timelineOverlay()->setSelection(2000, 7000);

    auto* timeLabel = panel.findChild<QLabel*>(
        QStringLiteral("BillfishVideoTrimTimeLabel"));
    QVERIFY(timeLabel != nullptr);

    fixture.control.timeline->setValue(5000);
    QCOMPARE(timeLabel->text(), QStringLiteral("00:00:03 / 00:00:05"));
    QCOMPARE(timeLabel->toolTip(),
             QStringLiteral("选区内位置 00:00:03.000; 选区时长 00:00:05.000"));

    fixture.control.timeline->setValue(9000);
    QCOMPARE(timeLabel->text(), QStringLiteral("00:00:05 / 00:00:05"));

    fixture.control.timeline->setValue(1000);
    QCOMPARE(timeLabel->text(), QStringLiteral("00:00:00 / 00:00:05"));
}

void VideoTrimUiTest::selectionChangeSeeksOutOfRangePlayheadToIn() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture fixture(input);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));

    QSignalSpy movedSpy(fixture.control.timeline, &QSlider::sliderMoved);
    QSignalSpy releasedSpy(fixture.control.timeline, &QSlider::sliderReleased);

    fixture.control.timeline->setValue(8000);
    panel.timelineOverlay()->setSelection(2000, 7000);
    QCOMPARE(fixture.control.timeline->value(), 2000);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(releasedSpy.count(), 1);
    QVERIFY(!panel.isPreviewing());

    fixture.control.timeline->setValue(4000);
    panel.timelineOverlay()->setOutValue(3000);
    QCOMPARE(fixture.control.timeline->value(), 2000);
    QCOMPARE(movedSpy.count(), 2);
    QCOMPARE(releasedSpy.count(), 2);

    fixture.control.timeline->setValue(2500);
    panel.timelineOverlay()->setOutValue(3500);
    QCOMPARE(fixture.control.timeline->value(), 2500);
    QCOMPARE(movedSpy.count(), 2);
    QCOMPARE(releasedSpy.count(), 2);

    fixture.control.timeline->setValue(1000);
    panel.timelineOverlay()->setInValue(1500);
    QCOMPARE(fixture.control.timeline->value(), 1500);
    QCOMPARE(movedSpy.count(), 3);
    QCOMPARE(releasedSpy.count(), 3);
}

void VideoTrimUiTest::panelLoopsSelectedRange() {
    QTemporaryDir temporary;
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture fixture(input);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));
    panel.timelineOverlay()->setSelection(1200, 3400);

    QSignalSpy playSpy(fixture.control.play, &QPushButton::clicked);
    QSignalSpy pauseSpy(fixture.control.pause, &QPushButton::clicked);
    QStringList commandOrder;
    connect(&panel, &TrimPanel::previewStarted, this,
            [&commandOrder] { commandOrder.append(QStringLiteral("loop")); });
    connect(fixture.control.timeline, &QSlider::sliderReleased, this,
            [&commandOrder] { commandOrder.append(QStringLiteral("seek")); });
    connect(fixture.control.play, &QPushButton::clicked, this,
            [&commandOrder] { commandOrder.append(QStringLiteral("play")); });
    connect(fixture.control.pause, &QPushButton::clicked, this,
            [&commandOrder] { commandOrder.append(QStringLiteral("pause")); });
    connect(&panel, &TrimPanel::previewStopped, this,
            [&commandOrder] { commandOrder.append(QStringLiteral("clear-loop")); });
    auto* preview = panel.findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimPreviewButton"));
    QVERIFY(preview != nullptr);
    QTest::mouseClick(preview, Qt::LeftButton);
    QVERIFY(panel.isPreviewing());
    QCOMPARE(fixture.control.timeline->value(), 1200);
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(commandOrder,
             QStringList({QStringLiteral("loop"), QStringLiteral("seek"),
                          QStringLiteral("play")}));

    commandOrder.clear();
    fixture.control.timeline->setValue(3400);
    QCOMPARE(fixture.control.timeline->value(), 1200);
    QCOMPARE(playSpy.count(), 2);
    QCOMPARE(commandOrder,
             QStringList({QStringLiteral("seek"), QStringLiteral("play")}));

    commandOrder.clear();
    QTest::mouseClick(preview, Qt::LeftButton);
    QVERIFY(!panel.isPreviewing());
    QCOMPARE(pauseSpy.count(), 1);
    QCOMPARE(commandOrder,
             QStringList({QStringLiteral("pause"), QStringLiteral("clear-loop")}));

    commandOrder.clear();
    QTest::mouseClick(preview, Qt::LeftButton);
    QVERIFY(panel.isPreviewing());
    commandOrder.clear();
    panel.timelineOverlay()->setSelection(1200, 1200);
    QVERIFY(!panel.isPreviewing());
    QCOMPARE(pauseSpy.count(), 2);
    QCOMPARE(commandOrder,
             QStringList({QStringLiteral("pause"), QStringLiteral("clear-loop")}));
}

void VideoTrimUiTest::fileIdSignalIsConnectedByMetaObject() {
    QTemporaryDir libraryRoot;
    QVERIFY(libraryRoot.isValid());
    ScopedEnvironment rootEnvironment("BILLFISH_LIBRARY_ROOT", QFile::encodeName(libraryRoot.path()));
    PlayerFixture fixture;
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root, &fixture.details));
    const QString before = panel.statusText();
    emit fixture.details.signalPreviewFileIdChanged(42);
    QVERIFY(panel.statusText() != before);
    QVERIFY(!panel.statusText().isEmpty());
}

void VideoTrimUiTest::currentFileIdIsSynchronizedWithoutUserSwitch() {
    QTemporaryDir libraryRoot;
    QVERIFY(libraryRoot.isValid());
    ScopedEnvironment rootEnvironment("BILLFISH_LIBRARY_ROOT", QFile::encodeName(libraryRoot.path()));
    PlayerFixture fixture;
    fixture.details.setCurrentFileId(42);
    emit fixture.details.signalPreviewFileIdChanged(42);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root, &fixture.details, 42));
    QVERIFY(!panel.statusText().contains(QStringLiteral("未能读取当前视频路径")));
    QVERIFY(panel.statusText().contains(QStringLiteral("billfish.db"), Qt::CaseInsensitive));
}

void VideoTrimUiTest::lateFileIdSourceIsReboundAfterInjection() {
    QTemporaryDir libraryRoot;
    QVERIFY(libraryRoot.isValid());
    ScopedEnvironment rootEnvironment("BILLFISH_LIBRARY_ROOT", QFile::encodeName(libraryRoot.path()));
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    auto* control = new PreviewControlWidget(&root);
    layout->addWidget(control);
    root.show();
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&root));
    auto* panel = root.findChild<TrimPanel*>();
    QVERIFY(panel != nullptr);
    const QString before = panel->statusText();

    auto* lateDetails = new PreviewDetailsWidget(&root);
    layout->insertWidget(0, lateDetails);
    QVERIFY(!injector.tryInject(&root));
    emit lateDetails->signalPreviewFileIdChanged(77);
    QVERIFY(panel->statusText() != before);
    QVERIFY(panel->statusText().contains(QStringLiteral("billfish.db"), Qt::CaseInsensitive));
}

void VideoTrimUiTest::diagnosticsAreOptIn() {
    QTemporaryDir localAppData;
    QVERIFY(localAppData.isValid());
    ScopedEnvironment local("LOCALAPPDATA", QFile::encodeName(localAppData.path()));
    ScopedEnvironment debug("BILLFISH_TRIM_DEBUG", "placeholder");
    qunsetenv("BILLFISH_TRIM_DEBUG");
    billfish::trim::trimDiagnostic(QStringLiteral("must remain quiet"));
    QVERIFY(!QFileInfo::exists(billfish::trim::trimDiagnosticPath()));
    qputenv("BILLFISH_TRIM_DEBUG", "1");
    billfish::trim::trimDiagnostic(QStringLiteral("opt-in evidence"));
    QFile log(billfish::trim::trimDiagnosticPath());
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY(log.readAll().contains("opt-in evidence"));
}

void VideoTrimUiTest::diagnosticsStayBoundedAndSummarized() {
    QTemporaryDir localAppData;
    QTemporaryDir media;
    QVERIFY(localAppData.isValid());
    QVERIFY(media.isValid());
    ScopedEnvironment local("LOCALAPPDATA", QFile::encodeName(localAppData.path()));
    ScopedEnvironment debug("BILLFISH_TRIM_DEBUG", "1");

    const QString input = media.filePath(QStringLiteral("diagnostic-source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture fixture(input);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));
    for (int index = 0; index < 100; ++index) {
        billfish::trim::trimDiagnostic(QStringLiteral("diagnostic-debounce-evidence"));
    }

    QFile log(billfish::trim::trimDiagnosticPath());
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QByteArray summary = log.readAll();
    log.close();
    QVERIFY(summary.contains("path-resolution"));
    QVERIFY(!summary.contains("object-scan"));
    QVERIFY(!summary.contains(" method "));
    QCOMPARE(summary.count("diagnostic-debounce-evidence"), 1);

    QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(log.resize(billfish::trim::trimDiagnosticMaximumBytes() - 8));
    log.close();
    billfish::trim::trimDiagnostic(QStringLiteral("bounded-evidence\nsecond-line"));

    const QFileInfo boundedInfo(billfish::trim::trimDiagnosticPath());
    QVERIFY(boundedInfo.size() <= billfish::trim::trimDiagnosticMaximumBytes());
    QVERIFY(boundedInfo.size() < 64 * 1024);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QByteArray bounded = log.readAll();
    QVERIFY(bounded.contains("diagnostic-log-reset reason=size-limit"));
    QVERIFY(bounded.contains("bounded-evidence second-line"));
    QVERIFY(!bounded.contains("bounded-evidence\nsecond-line"));
}

void VideoTrimUiTest::sourceDoesNotPropagateAcrossPlayerRoots() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("global-source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture globalFixture(input);
    PlayerFixture localFixture;
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&globalFixture.root));
    QVERIFY(injector.tryInject(&localFixture.root));
    auto* globalPanel = globalFixture.root.findChild<TrimPanel*>();
    auto* localPanel = localFixture.root.findChild<TrimPanel*>();
    QVERIFY(globalPanel != nullptr);
    QVERIFY(localPanel != nullptr);
    QVERIFY(localPanel->sourcePath().isEmpty());
    QVERIFY(localPanel->mediaDurationMs() <= 0);

    QVERIFY(!localPanel->refreshRuntimeBindings(&localFixture.control, &plugin));
    QVERIFY(localPanel->sourcePath().isEmpty());
    QVERIFY(localPanel->statusText().contains(QStringLiteral("无法") ) ||
            localPanel->statusText().contains(QStringLiteral("当前")));
}

void VideoTrimUiTest::injectorRejectsAmbiguousPreviewScope() {
    QWidget root;
    auto* layout = new QVBoxLayout(&root);
    PreviewDetailsWidget firstDetails(&root);
    PreviewDetailsWidget secondDetails(&root);
    PreviewControlWidget control(&root);
    layout->addWidget(&firstDetails);
    layout->addWidget(&secondDetails);
    layout->addWidget(&control);
    root.show();
    QCoreApplication::processEvents();

    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&root));
    auto* action = root.findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    QVERIFY(action != nullptr);
    action->trigger();
    QCoreApplication::processEvents();
    QVERIFY(root.findChild<TrimEditorDialog*>() == nullptr);
}

void VideoTrimUiTest::samePreviewHostReopensCurrentSourceAndExportsIt() {
    QTemporaryDir temporary;
    QTemporaryDir libraryRoot;
    QVERIFY(temporary.isValid());
    QVERIFY(libraryRoot.isValid());
    const QString mediaRoot = QDir(libraryRoot.path()).filePath(QStringLiteral("Media"));
    QVERIFY(QDir().mkpath(mediaRoot));
    const QString firstInput = QDir(mediaRoot).filePath(QStringLiteral("a.mp4"));
    const QString secondInput = QDir(mediaRoot).filePath(QStringLiteral("b.mp4"));
    QVERIFY(writeFile(firstInput, "fake-media\ndurationMs=10000\nsource=A\n"));
    QVERIFY(writeFile(secondInput, "fake-media\ndurationMs=20000\nsource=B\n"));
    QString databaseError;
    QVERIFY2(createBillfishVideoLibrary(libraryRoot.path(), &databaseError),
             qPrintable(databaseError));
    ScopedEnvironment rootEnvironment("BILLFISH_LIBRARY_ROOT",
                                      QFile::encodeName(libraryRoot.path()));
    const QString sqlitePath = QDir(QString::fromLocal8Bit(BILLFISH_TEST_ROOT))
                                   .filePath(QStringLiteral("sqlite3.dll"));
    ScopedEnvironment sqliteEnvironment("BILLFISH_SQLITE3", QFile::encodeName(sqlitePath));

    FakeFfmpegPair tools;
    QString toolError;
    QVERIFY2(createFakeFfmpegPair(temporary.path(), &tools, &toolError), qPrintable(toolError));
    ScopedEnvironment ffmpeg("BILLFISH_FFMPEG", QFile::encodeName(tools.ffmpegPath));
    ScopedEnvironment ffprobe("BILLFISH_FFPROBE", QFile::encodeName(tools.ffprobePath));
    ScopedEnvironment copyInputMarker("BILLFISH_TEST_COPY_INPUT_MARKER", "1");

    QWidget root;
    auto* rootLayout = new QVBoxLayout(&root);
    auto* host = new QWidget(&root);
    auto* hostLayout = new QVBoxLayout(host);
    auto* details = new PreviewDetailsWidget(host);
    auto* control = new PreviewControlWidget(host);
    hostLayout->addWidget(details);
    hostLayout->addWidget(control);
    auto* secondHost = new QWidget(&root);
    auto* secondHostLayout = new QVBoxLayout(secondHost);
    auto* secondDetails = new PreviewDetailsWidget(secondHost);
    auto* secondControl = new PreviewControlWidget(secondHost);
    secondHostLayout->addWidget(secondDetails);
    secondHostLayout->addWidget(secondControl);
    rootLayout->addWidget(host);
    rootLayout->addWidget(secondHost);
    secondHost->hide();
    details->setCurrentFileId(42);
    secondDetails->setCurrentFileId(43);
    root.show();
    QCoreApplication::processEvents();

    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&root));
    emit details->signalPreviewFileIdChanged(42);
    auto* action = host->findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    auto* secondAction = secondHost->findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    QVERIFY(action != nullptr);
    QVERIFY(secondAction != nullptr);

    control->timeline->setValue(2'000);
    emit details->videoTotalFramesChanged(1'000, 1'000);
    emit details->videoFramesChanged(0, 200);
    action->trigger();
    QCoreApplication::processEvents();
    auto* firstEditor = root.findChild<TrimEditorDialog*>();
    QVERIFY(firstEditor != nullptr);
    QCOMPARE(QFileInfo(firstEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());

    auto* resolver = host->findChild<TrimPanel*>(
        QString::fromLatin1(PlayerTrimInjector::kResolverObjectName));
    QVERIFY(resolver != nullptr);
    details->setCurrentFileId(-1);
    emit details->signalPreviewFileIdChanged(-1);
    QVERIFY(resolver->sourcePath().isEmpty());
    QCOMPARE(QFileInfo(firstEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());

    details->setCurrentFileId(43);
    emit details->signalPreviewFileIdChanged(43);
    QCOMPARE(QFileInfo(firstEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());
    control->timeline->setValue(8'000);
    emit details->videoTotalFramesChanged(2'000, 2'000);
    emit details->videoFramesChanged(0, 1'200);
    action->trigger();
    QCoreApplication::processEvents();
    auto* secondEditor = root.findChild<TrimEditorDialog*>();
    QVERIFY(secondEditor != nullptr);
    QCOMPARE(QFileInfo(secondEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(secondInput).canonicalFilePath());
    QCOMPARE(secondEditor->mediaDurationMs(), qint64{20'000});

    details->setCurrentFileId(42);
    emit details->signalPreviewFileIdChanged(42);
    host->hide();
    secondHost->show();
    QCoreApplication::processEvents();
    emit secondDetails->signalPreviewFileIdChanged(43);
    secondAction->trigger();
    QCoreApplication::processEvents();
    auto* reopenedFromSecondHost = root.findChild<TrimEditorDialog*>();
    QVERIFY(reopenedFromSecondHost != nullptr);
    QCOMPARE(QFileInfo(reopenedFromSecondHost->sourcePath()).canonicalFilePath(),
             QFileInfo(secondInput).canonicalFilePath());

    const QString output = temporary.filePath(QStringLiteral("b-clip.mp4"));
    QVERIFY(reopenedFromSecondHost->trimPanel()->exportToPath(output));
    QTRY_VERIFY_WITH_TIMEOUT(!reopenedFromSecondHost->trimPanel()->isBusy(), 15'000);
    QFile outputFile(output);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    const QByteArray outputBytes = outputFile.readAll();
    QVERIFY2(outputBytes.contains("durationMs=20000"), outputBytes.constData());
    QVERIFY2(outputBytes.contains("source=B"), outputBytes.constData());
    reopenedFromSecondHost->close();
}

void VideoTrimUiTest::injectionPreservesHostSliderWidth() {
    QWidget root;
    auto* rootLayout = new QVBoxLayout(&root);
    auto* playbackRow = new QWidget(&root);
    auto* playbackLayout = new QHBoxLayout(playbackRow);
    auto* control = new PreviewControlWidget(playbackRow);
    playbackLayout->addWidget(control);
    rootLayout->addWidget(playbackRow);
    root.resize(1000, 220);
    root.show();
    QCoreApplication::processEvents();
    const int widthBefore = control->timeline->width();

    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&root));
    QCoreApplication::processEvents();
    const int widthAfter = control->timeline->width();
    auto* resolver = root.findChild<TrimPanel*>(
        QString::fromLatin1(PlayerTrimInjector::kResolverObjectName));
    QVERIFY(resolver != nullptr);
    const QString widthEvidence = QStringLiteral("before=%1 after=%2 root=%3")
                                      .arg(widthBefore)
                                      .arg(widthAfter)
                                      .arg(root.width());
    QVERIFY2(std::abs(widthAfter - widthBefore) <= 2, qPrintable(widthEvidence));
    QCOMPARE(rootLayout->indexOf(playbackRow), 0);
    QCOMPARE(rootLayout->indexOf(resolver), -1);
    QVERIFY(resolver->isHidden());
    QVERIFY(resolver->timelineOverlay() == nullptr);
    auto* openAction = root.findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    QVERIFY(openAction != nullptr);
    QCOMPARE(openAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+E")));
}

void VideoTrimUiTest::unknownDurationIsProbedOffTheGuiThread() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFile(input, "fake-media\ndurationMs=7654\n"));
    FakeFfmpegPair tools;
    QString toolError;
    QVERIFY2(createFakeFfmpegPair(temporary.path(), &tools, &toolError), qPrintable(toolError));
    ScopedEnvironment ffmpeg("BILLFISH_FFMPEG", QFile::encodeName(tools.ffmpegPath));
    ScopedEnvironment ffprobe("BILLFISH_FFPROBE", QFile::encodeName(tools.ffprobePath));
    PlayerFixture fixture(input, -1);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));
    QTRY_COMPARE_WITH_TIMEOUT(panel.mediaDurationMs(), qint64{7654}, 10'000);
    auto* exportButton = panel.findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimExportButton"));
    QVERIFY(exportButton->isEnabled());
}

void VideoTrimUiTest::actionsAndModesAreVisible() {
    QTemporaryDir temporary;
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));
    PlayerFixture fixture(input);
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&fixture.root));
    auto* openAction = fixture.root.findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    QVERIFY(openAction != nullptr);
    openAction->trigger();
    QCoreApplication::processEvents();
    auto* editor = fixture.root.findChild<TrimEditorDialog*>();
    QVERIFY(editor != nullptr);
    auto* panel = editor->trimPanel();
    QVERIFY(panel != nullptr && panel->timelineOverlay() != nullptr);
    panel->timelineOverlay()->setSelection(200'000, 800'000);
    editor->timeline()->setValue(500'000);
    QCoreApplication::processEvents();

    auto* exportButton = panel->findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimExportButton"));
    auto* copyButton = panel->findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimCopyButton"));
    auto* previewButton = panel->findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimPreviewButton"));
    QVERIFY(exportButton != nullptr && exportButton->text() == QStringLiteral("导出片段"));
    QVERIFY(copyButton != nullptr && copyButton->text() == QStringLiteral("复制到剪贴板"));
    QVERIFY(previewButton != nullptr);
    QVERIFY(exportButton->isEnabled());
    QVERIFY(copyButton->isEnabled());
    QVERIFY(!panel->exportToPath(QString{}));
    QVERIFY(panel->isVisible());
    QVERIFY(exportButton->isVisible());
    QVERIFY(copyButton->isVisible());
    QVERIFY(exportButton->menu() != nullptr);
    QCOMPARE(exportButton->menu()->actions().size(), 2);
    QCOMPARE(panel->trimMode(), TrimMode::AccurateReencode);
    exportButton->menu()->actions().at(1)->trigger();
    QCOMPARE(panel->trimMode(), TrimMode::FastCopy);
    const QString snapshotPath = QString::fromLocal8Bit(qgetenv("BILLFISH_UI_SNAPSHOT"));
    if (!snapshotPath.isEmpty()) {
        QVERIFY(editor->grab().save(snapshotPath));
    }
    editor->close();
}

void VideoTrimUiTest::editorDialogUsesCompactReferenceGeometry() {
    TrimEditorDialog editor;
    editor.resize(960, 825);
    editor.show();
    QCoreApplication::processEvents();

    QCOMPARE(editor.minimumSize(), QSize(720, 540));
    QCOMPARE(editor.size(), QSize(960, 825));
    QCOMPARE(editor.timeline()->height(), 40);
    QVERIFY(editor.trimPanel()->height() >= 58);
    QVERIFY(editor.trimPanel()->height() <= 60);
    QCOMPARE(editor.timeline()->width(), editor.width());
    QCOMPARE(editor.videoHost()->width(), editor.width());
    QVERIFY(editor.videoHost()->height() >= 700);
    QCOMPARE(editor.findChildren<QSlider*>().size(), 1);
    QVERIFY(editor.property(TrimEditorDialog::kEditorHostProperty).toBool());

    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(!injector.tryInject(&editor));
    editor.close();
}

void VideoTrimUiTest::editorDialogAppliesVerifiedSourceAndClosesCleanly() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("dialog-source.mp4"));
    QVERIFY(writeFile(input, "durationMs=12500\n"));

    QPointer<TrimEditorDialog> editor = new TrimEditorDialog;
    editor->setAttribute(Qt::WA_DeleteOnClose, true);
    editor->setSource(input, 12'500);
    editor->show();
    QCoreApplication::processEvents();

    QCOMPARE(QFileInfo(editor->sourcePath()).canonicalFilePath(),
             QFileInfo(input).canonicalFilePath());
    QCOMPARE(editor->mediaDurationMs(), qint64{12'500});
    QCOMPARE(QFileInfo(editor->trimPanel()->sourcePath()).canonicalFilePath(),
             QFileInfo(input).canonicalFilePath());
    QCOMPARE(editor->trimPanel()->mediaDurationMs(), qint64{12'500});
    QCOMPARE(editor->trimPanel()->selection().startMs, qint64{0});
    QCOMPARE(editor->trimPanel()->selection().endMs, qint64{12'500});
    auto* timeLabel = editor->findChild<QLabel*>(QStringLiteral("BillfishVideoTrimTimeLabel"));
    QVERIFY(timeLabel != nullptr);
    QVERIFY(timeLabel->text().contains(QStringLiteral(" / ")));
    QVERIFY(!editor->isModal());

    QPointer<QWidget> guardedHost = editor->videoHost();
    bool backendDestroyedWhileHostAlive = false;
    connect(editor->playbackBackend(), &QObject::destroyed, this,
            [&guardedHost, &backendDestroyedWhileHostAlive] {
                backendDestroyedWhileHostAlive = guardedHost != nullptr;
            });
    editor->close();
    QTRY_VERIFY(editor.isNull());
    QVERIFY(backendDestroyedWhileHostAlive);
}

void VideoTrimUiTest::editorDialogStartsAtCapturedFrameAndScrubsLive() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("captured-position.mp4"));
    QVERIFY(writeFile(input, "durationMs=10000\n"));

    TrimEditorDialog editor;
    editor.setSource(input, 10'000, 4'250);
    QCOMPARE(editor.timeline()->value(), 425'000);
    QCOMPARE(editor.trimPanel()->selection().startMs, qint64{4'250});
    QCOMPARE(editor.trimPanel()->selection().endMs, qint64{10'000});

    QSignalSpy scrubSpy(&editor, &TrimEditorDialog::scrubSeekRequested);
    editor.timeline()->setSliderDown(true);
    editor.timeline()->setSliderPosition(675'000);
    QTRY_VERIFY_WITH_TIMEOUT(scrubSpy.count() >= 1, 500);
    QCOMPARE(scrubSpy.constLast().at(0).toLongLong(), qint64{6'750});
    QVERIFY(editor.timeline()->isSliderDown());
    editor.timeline()->setSliderDown(false);
    QCOMPARE(scrubSpy.constLast().at(0).toLongLong(), qint64{6'750});
    editor.close();
}

void VideoTrimUiTest::editorDialogProbesUnknownDurationWithoutPlayback() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("probe-source.mp4"));
    QVERIFY(writeFile(input, "fake-media\ndurationMs=7654\n"));
    FakeFfmpegPair tools;
    QString toolError;
    QVERIFY2(createFakeFfmpegPair(temporary.path(), &tools, &toolError), qPrintable(toolError));
    ScopedEnvironment ffmpeg("BILLFISH_FFMPEG", QFile::encodeName(tools.ffmpegPath));
    ScopedEnvironment ffprobe("BILLFISH_FFPROBE", QFile::encodeName(tools.ffprobePath));
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", "Z:/missing/mpv.exe");

    TrimEditorDialog editor;
    editor.setSource(input, -1);
    editor.show();
    QTRY_COMPARE_WITH_TIMEOUT(editor.trimPanel()->mediaDurationMs(), qint64{7654}, 10'000);
    QCOMPARE(QFileInfo(editor.trimPanel()->sourcePath()).canonicalFilePath(),
             QFileInfo(input).canonicalFilePath());
    auto* exportButton = editor.findChild<QToolButton*>(
        QStringLiteral("BillfishVideoTrimExportButton"));
    QVERIFY(exportButton != nullptr);
    QVERIFY(exportButton->isEnabled());
    editor.close();
}

void VideoTrimUiTest::editorDialogExportsItsSourceSnapshot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("editor-export-source.mp4"));
    const QString output = temporary.filePath(QStringLiteral("editor-export.mp4"));
    QVERIFY(writeFile(input, "fake-media\ndurationMs=10000\n"));
    FakeFfmpegPair tools;
    QString toolError;
    QVERIFY2(createFakeFfmpegPair(temporary.path(), &tools, &toolError), qPrintable(toolError));
    ScopedEnvironment ffmpeg("BILLFISH_FFMPEG", QFile::encodeName(tools.ffmpegPath));
    ScopedEnvironment ffprobe("BILLFISH_FFPROBE", QFile::encodeName(tools.ffprobePath));

    TrimEditorDialog editor;
    editor.setSource(input, 10'000);
    editor.trimPanel()->timelineOverlay()->setSelection(100'000, 300'000);
    QSignalSpy finishedSpy(editor.trimPanel(), &TrimPanel::exportFinished);
    QVERIFY(editor.trimPanel()->exportToPath(output));
    QTRY_VERIFY_WITH_TIMEOUT(!editor.trimPanel()->isBusy(), 15'000);
    QCOMPARE(finishedSpy.count(), 1);
    QFile outputFile(output);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QVERIFY(outputFile.readAll().contains("durationMs=2000"));
    editor.close();
}

void VideoTrimUiTest::openEditorSnapshotsSourceAcrossGlobalUpdates() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString firstInput = temporary.filePath(QStringLiteral("first.mp4"));
    const QString secondInput = temporary.filePath(QStringLiteral("second.mp4"));
    QVERIFY(writeFile(firstInput, "durationMs=10000\n"));
    QVERIFY(writeFile(secondInput, "durationMs=20000\n"));

    PlayerFixture firstFixture(firstInput, 10'000);
    PlayerFixture secondFixture(secondInput, 20'000);
    secondFixture.root.hide();
    QCoreApplication::processEvents();
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&firstFixture.root));
    emit firstFixture.details.signalPreviewFileIdChanged(42);
    firstFixture.control.timeline->setValue(2'500);
    emit firstFixture.details.videoTotalFramesChanged(1'000, 0);
    emit firstFixture.details.videoFramesChanged(0, 400);

    auto* firstAction = firstFixture.root.findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    QVERIFY(firstAction != nullptr);
    firstAction->trigger();
    QCoreApplication::processEvents();
    QPointer<TrimEditorDialog> firstEditor =
        firstFixture.root.findChild<TrimEditorDialog*>();
    QVERIFY(firstEditor != nullptr);
    QCOMPARE(QFileInfo(firstEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());
    QCOMPARE(firstEditor->timeline()->value(), 400'000);
    QCOMPARE(firstEditor->trimPanel()->selection().startMs, qint64{4'000});

    firstFixture.root.hide();
    secondFixture.root.show();
    QCoreApplication::processEvents();
    QVERIFY(injector.tryInject(&secondFixture.root));
    auto* firstResolver = firstFixture.root.findChild<TrimPanel*>(
        QString::fromLatin1(PlayerTrimInjector::kResolverObjectName));
    auto* secondResolver = secondFixture.root.findChild<TrimPanel*>(
        QString::fromLatin1(PlayerTrimInjector::kResolverObjectName));
    QVERIFY(firstResolver != nullptr);
    QVERIFY(secondResolver != nullptr);
    QCoreApplication::processEvents();

    QCOMPARE(QFileInfo(firstResolver->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());
    QCOMPARE(QFileInfo(secondResolver->sourcePath()).canonicalFilePath(),
             QFileInfo(secondInput).canonicalFilePath());
    QCOMPARE(QFileInfo(firstEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());
    QCOMPARE(QFileInfo(firstEditor->trimPanel()->sourcePath()).canonicalFilePath(),
             QFileInfo(firstInput).canonicalFilePath());
    QCOMPARE(firstEditor->mediaDurationMs(), qint64{10'000});
    emit secondFixture.details.signalPreviewFileIdChanged(43);
    secondFixture.control.timeline->setValue(8'000);
    emit secondFixture.details.videoTotalFramesChanged(2'000, 0);
    emit secondFixture.details.videoFramesChanged(0, 1'200);
    auto* secondAction = secondFixture.root.findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName));
    QVERIFY(secondAction != nullptr);
    secondAction->trigger();
    QCoreApplication::processEvents();
    QTRY_VERIFY(firstEditor.isNull());
    auto* secondEditor = secondFixture.root.findChild<TrimEditorDialog*>();
    QVERIFY(secondEditor != nullptr);
    QCOMPARE(QFileInfo(secondEditor->sourcePath()).canonicalFilePath(),
             QFileInfo(secondInput).canonicalFilePath());
    QCOMPARE(secondEditor->timeline()->value(), 600'000);
    QCOMPARE(secondEditor->trimPanel()->selection().startMs, qint64{12'000});
    QCOMPARE(secondEditor->trimPanel()->selection().endMs, qint64{20'000});
    secondEditor->close();
}

void VideoTrimUiTest::injectorUsesExactEvidenceAndPreventsDuplicates() {
    PlayerFixture fixture;
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&fixture.root));
    QCOMPARE(fixture.root.findChildren<TrimPanel*>().size(), 1);
    QCOMPARE(fixture.root.findChildren<QAction*>(
                 QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName)).size(), 1);
    QVERIFY(fixture.root.property(PlayerTrimInjector::kInjectedProperty).toBool());
    QVERIFY(!injector.tryInject(&fixture.root));
    QCOMPARE(fixture.root.findChildren<TrimPanel*>().size(), 1);
    QCOMPARE(fixture.root.findChildren<QAction*>(
                 QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName)).size(), 1);
    QCOMPARE(fixture.layout.indexOf(&fixture.control), 1);
    QCOMPARE(fixture.layout.indexOf(fixture.root.findChild<TrimPanel*>()), -1);
}

void VideoTrimUiTest::injectorSupportsExplicitOverride() {
    QWidget root;
    root.setProperty(PlayerTrimInjector::kPlayerRootProperty, true);
    auto* layout = new QVBoxLayout(&root);
    auto* controls = new QWidget(&root);
    auto* controlsLayout = new QHBoxLayout(controls);
    auto* slider = new QSlider(Qt::Horizontal, controls);
    slider->setObjectName(QStringLiteral("PreviewControlWidget_m_pVideoSlider"));
    auto* frame = new QToolButton(controls);
    frame->setObjectName(QStringLiteral("PreviewControlWidget_frame_btn"));
    frame->setMenu(new QMenu(frame));
    controlsLayout->addWidget(slider);
    controlsLayout->addWidget(frame);
    layout->addWidget(controls);
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    QVERIFY(injector.tryInject(&root));
    QVERIFY(root.findChild<TrimPanel*>() != nullptr);
    QVERIFY(root.findChild<QAction*>(
        QString::fromLatin1(PlayerTrimInjector::kOpenActionObjectName)) != nullptr);
}

void VideoTrimUiTest::injectorDebouncesEventStorms() {
    PlayerFixture fixture;
    QObject plugin;
    PlayerTrimInjector injector(&plugin);
    injector.start();
    QTRY_VERIFY_WITH_TIMEOUT(injector.scanPassCount() >= 1, 3000);
    const quint64 baseline = injector.scanPassCount();

    for (int index = 0; index < 100; ++index) {
        fixture.root.setProperty("previewPath", QString::number(index));
    }

    QTRY_COMPARE_WITH_TIMEOUT(injector.scanPassCount(), baseline + 1, 3000);
    QTest::qWait(PlayerTrimInjector::kMinimumScanIntervalMs + 100);
    QCOMPARE(injector.scanPassCount(), baseline + 1);
}

void VideoTrimUiTest::exportRunsCoreOffTheGuiThread() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    const QString outputWithoutExtension = temporary.filePath(QStringLiteral("clip"));
    const QString output = outputWithoutExtension + QStringLiteral(".mp4");
    QVERIFY(writeFile(input, "fake-media\ndurationMs=10000\n"));
    FakeFfmpegPair tools;
    QString toolError;
    QVERIFY2(createFakeFfmpegPair(temporary.path(), &tools, &toolError), qPrintable(toolError));
    ScopedEnvironment ffmpeg("BILLFISH_FFMPEG", QFile::encodeName(tools.ffmpegPath));
    ScopedEnvironment ffprobe("BILLFISH_FFPROBE", QFile::encodeName(tools.ffprobePath));
    ScopedEnvironment fail("BILLFISH_TEST_FAIL_FFMPEG", QByteArray());
    qunsetenv("BILLFISH_TEST_FAIL_FFMPEG");

    PlayerFixture fixture(input);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));
    panel.timelineOverlay()->setSelection(1000, 3000);
    QSignalSpy finishedSpy(&panel, &TrimPanel::exportFinished);
    QSignalSpy failureSpy(&panel, &TrimPanel::operationFailed);
    QSignalSpy progressSpy(&panel, &TrimPanel::operationProgress);
    QVERIFY(panel.exportToPath(outputWithoutExtension));
    QVERIFY(panel.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(!panel.isBusy(), 15'000);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(progressSpy.count() >= 4);
    QVERIFY(QFileInfo(output).isFile());
    QFile outputFile(output);
    QVERIFY(outputFile.open(QIODevice::ReadOnly));
    QVERIFY(outputFile.readAll().contains("durationMs=2000"));
}

void VideoTrimUiTest::exportFailureReturnsToInteractiveState() {
    QTemporaryDir temporary;
    const QString input = temporary.filePath(QStringLiteral("source.mp4"));
    const QString output = temporary.filePath(QStringLiteral("failed.mp4"));
    QVERIFY(writeFile(input, "fake-media\ndurationMs=10000\n"));
    FakeFfmpegPair tools;
    QString toolError;
    QVERIFY2(createFakeFfmpegPair(temporary.path(), &tools, &toolError), qPrintable(toolError));
    ScopedEnvironment ffmpeg("BILLFISH_FFMPEG", QFile::encodeName(tools.ffmpegPath));
    ScopedEnvironment ffprobe("BILLFISH_FFPROBE", QFile::encodeName(tools.ffprobePath));
    ScopedEnvironment fail("BILLFISH_TEST_FAIL_FFMPEG", "1");

    PlayerFixture fixture(input);
    TrimPanel panel;
    QVERIFY(panel.attachToPlayer(&fixture.root));
    panel.timelineOverlay()->setSelection(1000, 3000);
    QSignalSpy failureSpy(&panel, &TrimPanel::operationFailed);
    QVERIFY(panel.exportToPath(output));
    QTRY_VERIFY_WITH_TIMEOUT(!panel.isBusy(), 15'000);
    QCOMPARE(failureSpy.count(), 1);
    QVERIFY(!panel.statusText().isEmpty());
    auto* exportButton = panel.findChild<QToolButton*>(QStringLiteral("BillfishVideoTrimExportButton"));
    QVERIFY(exportButton->isEnabled());
}

void VideoTrimUiTest::previewProxyForwardsTheQtPluginContract() {
    const QString proxyPath = QString::fromLocal8Bit(PREVIEW_PROXY_PATH);
    QVERIFY2(QFileInfo::exists(proxyPath), qPrintable(proxyPath));
    QPluginLoader loader(proxyPath);
    QCOMPARE(loader.metaData().value(QStringLiteral("IID")).toString(),
             QStringLiteral("Billfish.Plugin.IPreviewPlugin/1.0"));
    QObject* instance = loader.instance();
    QVERIFY2(instance != nullptr, qPrintable(loader.errorString()));
    QCOMPARE(QString::fromLatin1(instance->metaObject()->className()), QStringLiteral("StockPreviewFixture"));
    QVERIFY(loader.unload());
    QVERIFY(GetModuleHandleW(L"preview.dll") != nullptr);
}

QTEST_MAIN(VideoTrimUiTest)

#include "test_video_trim_ui.moc"
