#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QVariant>
#include <QWidget>

#include <functional>

class QCheckBox;
class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QDragEnterEvent;
class QDropEvent;
class QWindow;

namespace billfish::automation {

class PluginManager;

class PluginSettingsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit PluginSettingsPanel(PluginManager* manager, QWidget* parent = nullptr);
    void setActiveLibrary(const QString& libraryRoot);
    void setActiveLibraryProvider(std::function<QString()> provider);
    void selectPlugin(const QString& id, const QString& command = QString());
    void requestInstallation(const QString& sourcePath);
    void showInstallationResult(const QJsonObject& result);
    void showInstallationError(const QString& code, const QString& message);

signals:
    void installRequested(QString sourcePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void refresh();
    void updateSelection();
    void runCommand();
    void showResult(const QJsonObject& result);
    QString selectedPluginId() const;
    PluginManager* manager_;
    QString libraryRoot_;
    std::function<QString()> libraryProvider_;
    QString runningRequest_;
    QString selectedId_;
    QHash<QString, QJsonObject> plugins_;
    QListWidget* list_;
    QLabel* inventory_;
    QLabel* description_;
    QLabel* permissions_;
    QLabel* modeHelp_;
    QLabel* status_;
    QCheckBox* enabled_;
    QComboBox* commands_;
    QPlainTextEdit* arguments_;
    QPushButton* run_;
    QPlainTextEdit* result_;
};

// All widgets belong to the application's GUI thread. Progress is a nonmodal
// child of the main window and passes pointer input through to the application.
class AutomationUi final : public QObject {
    Q_OBJECT
public:
    explicit AutomationUi(PluginManager* manager, QObject* parent = nullptr);
    void setActiveLibrary(const QString& libraryRoot);
    void setActiveLibraryProvider(std::function<QString()> provider);
    // Publishes child QAction "BillfishPluginMenuAction" for the host toolbar.
    // Its optional "billfishPluginMenuPosition" QPoint is consumed on trigger.
    void setMainWindow(QWidget* mainWindow);
    void setMaterialContextProvider(std::function<QJsonObject()> provider);
    void setFormatPreviewHandler(std::function<void(QString, QString, QString)> handler);

public slots:
    void updateProgress(QString requestId, int completed, int total, QString message);
    void finishTask(QString requestId, bool success, QString message);
    bool showPluginSettings();
    void showPluginMenu(const QPoint& globalPosition);
    void installPluginSource(const QString& sourcePath);

private slots:
    void nativePreferencesSelected();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Task {
        int completed = 0;
        int total = 0;
        QString message;
        QString completion;
        bool finished = false;
        bool success = true;
        quint64 sequence = 0;
    };
    struct LoadingWindowState {
        bool showWithoutActivating;
        QPointer<QWindow> window;
        QVariant nativeShowWithoutActivating;
    };
    void observeWidget(QWidget* widget);
    void forgetLoadingWindow(QObject* widget);
    void integratePreferences(QWidget* preferences);
    void selectPluginPage();
    void positionPluginButton();
    void createProgress();
    void renderProgress();
    void positionProgress();
    void runMenuPlugin(const QString& pluginId, const QString& command);
    void integrateFileMenu(QMenu* menu);
    void fillPluginMenu(QMenu* menu, bool materialContext);
    bool materialContextMatches(const QJsonObject& context) const;
    PluginManager* manager_;
    QString libraryRoot_;
    std::function<QString()> libraryProvider_;
    std::function<QJsonObject()> materialContextProvider_;
    std::function<void(QString, QString, QString)> formatPreviewHandler_;
    QPointer<QWidget> mainWindow_;
    QPointer<QAction> pluginMenuAction_;
    QPointer<QMenu> pluginMenu_;
    QPointer<QWidget> preferences_;
    QHash<QObject*, LoadingWindowState> scopedLoadingWindows_;
    QPointer<PluginSettingsPanel> panel_;
    QPointer<QStackedWidget> settingsStack_;
    QPointer<QPushButton> pluginButton_;
    QHash<QObject*, QPointer<QWidget>> nativePages_;
    bool pluginButtonManaged_ = false;
    bool pendingOpenPluginSettings_ = false;
    QString pendingPluginId_;
    QString pendingCommandId_;
    QJsonObject pendingInstallationResult_;
    QSet<QString> installationRequests_;
    QPointer<QWidget> progress_;
    QProgressBar* progressBar_ = nullptr;
    QLabel* progressMessage_ = nullptr;
    QTimer* hideTimer_;
    QHash<QString, Task> tasks_;
    quint64 sequence_ = 0;
};

} // namespace billfish::automation
