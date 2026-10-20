#include "automation/automation_ui.hpp"
#include "automation/app_input_scope.hpp"
#include "automation/scoped_popup.hpp"
#include "automation/plugin_manager.hpp"
#include "shell/plugin_glyph.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <utility>

namespace billfish::automation {
namespace {

QString displayStatus(const QString& status, const QString& mode)
{
    if (mode == QStringLiteral("dependency")) return status == QStringLiteral("updated") ?
        QStringLiteral("依赖更新待激活") : QStringLiteral("依赖已安装");
    if (status == QStringLiteral("ready")) return mode == QStringLiteral("window") ? QStringLiteral("已启用") :
        mode == QStringLiteral("service") ? QStringLiteral("后台运行中") : QStringLiteral("就绪");
    if (status == QStringLiteral("running")) return mode == QStringLiteral("commands") ? QStringLiteral("正在执行") : QStringLiteral("正在打开");
    if (status == QStringLiteral("disabled")) return QStringLiteral("已关闭");
    if (status == QStringLiteral("starting")) return QStringLiteral("正在启动");
    if (status == QStringLiteral("stopping")) return QStringLiteral("正在停止");
    if (status == QStringLiteral("failed")) return QStringLiteral("启动失败");
    if (status == QStringLiteral("incompatible")) return QStringLiteral("不兼容");
    if (status == QStringLiteral("updated")) return QStringLiteral("新版尚未启动");
    return status;
}

QString pluginCompletionMessage(const QJsonObject& response)
{
    const auto completion = response.value(QStringLiteral("completion")).toString();
    if (completion == QStringLiteral("plugin_window_opened")) return QStringLiteral("插件窗口已打开");
    if (completion == QStringLiteral("plugin_service_started"))
        return QStringLiteral("插件服务已启动");
    if (completion == QStringLiteral("plugin_installed")) return response.value(QStringLiteral("mode")).toString() == QStringLiteral("dependency") ?
        QStringLiteral("插件依赖已安装") : QStringLiteral("插件安装完成, 尚未启用");
    if (completion == QStringLiteral("plugin_updated")) return response.value(QStringLiteral("mode")).toString() == QStringLiteral("dependency") ?
        QStringLiteral("依赖包已更新, 等待激活") : QStringLiteral("插件更新完成, 新版尚未启动");
    return QStringLiteral("插件命令已完成");
}

QString errorMessage(const QJsonObject& result)
{
    return result.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
}

QString concise(const QString& text, int limit = 180)
{
    QString value = text.simplified();
    if (value.size() > limit) value = value.left(limit - 1) + QChar(0x2026);
    return value;
}

QString droppedPackage(const QMimeData* mime, bool allowZip)
{
    if (!mime || !mime->hasUrls() || mime->urls().size() != 1 || !mime->urls().first().isLocalFile()) return {};
    const auto path = mime->urls().first().toLocalFile();
    const QFileInfo file(path);
    const auto suffix = file.suffix().toLower();
    return file.isFile() && (suffix == QStringLiteral("eagleplugin") ||
        (allowZip && suffix == QStringLiteral("zip"))) ? path : QString();
}

QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#282828")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#dddddd")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#202020")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#262626")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#dddddd")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#999999")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#3d3d3d")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#eeeeee")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#3b5261")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#333333")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#eeeeee")));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#888888")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#888888")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#888888")));
    return palette;
}

void styleControl(QWidget* widget, const QString& style)
{
    // A local palette and stylesheet withstand the frozen host's dark theme
    // without depending on descendant matching across a custom QWidget class.
    widget->setPalette(darkPalette());
    auto font = widget->font();
    font.setPixelSize(13);
    widget->setFont(font);
    widget->setStyleSheet(style);
}

QString permissionSummary(const QStringList& permissions)
{
    QStringList labels;
    bool read = false;
    bool metadata = false;
    bool tags = false;
    for (const auto& permission : permissions) {
        if (permission == QStringLiteral("items.update")) metadata = true;
        else if (permission == QStringLiteral("tags.create") || permission == QStringLiteral("tags.assign")) tags = true;
        else read = true;
    }
    if (read) labels.append(QStringLiteral("读取资源库"));
    if (metadata) labels.append(QStringLiteral("修改备注与来源"));
    if (tags) labels.append(QStringLiteral("管理标签"));
    return labels.isEmpty() ? QStringLiteral("无宿主 API 权限") : labels.join(QStringLiteral(", "));
}

} // namespace

PluginSettingsPanel::PluginSettingsPanel(PluginManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager)
{
    setObjectName(QStringLiteral("BillfishPluginSettingsPanel"));
    setAcceptDrops(true);
    styleControl(this, QStringLiteral("background:#282828; color:#ddd;"));
    setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    auto* toolbar = new QHBoxLayout;
    auto* install = new QPushButton(QStringLiteral("安装插件包..."), this);
    install->setObjectName(QStringLiteral("BillfishInstallPlugin"));
    auto* refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    auto* directory = new QPushButton(QStringLiteral("开发目录..."), this);
    directory->setObjectName(QStringLiteral("BillfishInstallPluginDirectory"));
    toolbar->addWidget(install);
    toolbar->addWidget(directory);
    toolbar->addWidget(refreshButton);
    toolbar->addStretch();
    inventory_ = new QLabel(this);
    inventory_->setObjectName(QStringLiteral("BillfishPluginInventory"));
    toolbar->addWidget(inventory_);
    layout->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    list_ = new QListWidget(splitter);
    list_->setObjectName(QStringLiteral("BillfishPluginList"));
    list_->setMinimumWidth(150);
    list_->setWordWrap(false);
    list_->setTextElideMode(Qt::ElideRight);
    auto* details = new QWidget(splitter);
    details->setObjectName(QStringLiteral("BillfishPluginDetails"));
    auto* detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(8, 0, 0, 0);
    detailLayout->setSpacing(5);
    enabled_ = new QCheckBox(QStringLiteral("启用插件"), details);
    enabled_->setObjectName(QStringLiteral("BillfishPluginEnabled"));
    enabled_->setFixedHeight(20);
    description_ = new QLabel(details);
    description_->setWordWrap(true);
    description_->setFixedHeight(50);
    description_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    permissions_ = new QLabel(details);
    permissions_->setWordWrap(true);
    permissions_->setFixedHeight(30);
    modeHelp_ = new QLabel(details);
    modeHelp_->setObjectName(QStringLiteral("BillfishPluginModeHelp"));
    modeHelp_->setWordWrap(true);
    modeHelp_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    commands_ = new QComboBox(details);
    commands_->setObjectName(QStringLiteral("BillfishPluginCommands"));
    arguments_ = new QPlainTextEdit(QStringLiteral("{}"), details);
    arguments_->setObjectName(QStringLiteral("BillfishPluginArguments"));
    arguments_->setPlaceholderText(QStringLiteral("命令参数 (JSON object)"));
    arguments_->setFixedHeight(46);
    run_ = new QPushButton(QStringLiteral("运行命令"), details);
    run_->setObjectName(QStringLiteral("BillfishRunPluginCommand"));
    result_ = new QPlainTextEdit(details);
    result_->setObjectName(QStringLiteral("BillfishPluginResult"));
    result_->setReadOnly(true);
    result_->setMinimumHeight(56);
    result_->setPlaceholderText(QStringLiteral("执行结果"));
    detailLayout->addWidget(enabled_);
    detailLayout->addWidget(description_);
    detailLayout->addWidget(permissions_);
    detailLayout->addWidget(modeHelp_);
    detailLayout->addWidget(commands_);
    detailLayout->addWidget(arguments_);
    detailLayout->addWidget(run_);
    detailLayout->addWidget(result_, 1);
    splitter->addWidget(list_);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({170, 330});
    layout->addWidget(splitter, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("BillfishPluginStatus"));
    status_->setWordWrap(true);
    status_->setFixedHeight(30);
    layout->addWidget(status_);

    styleControl(details, QStringLiteral("background:#282828; color:#ddd;"));
    styleControl(splitter, QStringLiteral("QSplitter::handle {background:#3b3b3b; width:4px;}"));
    const auto labelStyle = QStringLiteral("QLabel {color:#ddd; background:transparent; font-size:13px;}");
    for (auto* label : {inventory_, description_, status_, modeHelp_}) styleControl(label, labelStyle);
    styleControl(status_, QStringLiteral("QLabel {color:#ddd; background:transparent; font-size:12px;}"));
    styleControl(permissions_, QStringLiteral("QLabel {color:#aaa; background:transparent; font-size:11px;}"));
    styleControl(enabled_, QStringLiteral("QCheckBox {color:#ddd; background:transparent; spacing:6px; font-size:13px;}"));
    styleControl(list_, QStringLiteral(
        "QListWidget {background:#202020; color:#ddd; border:1px solid #454545; border-radius:3px; font-size:13px;}"
        "QListWidget::item {color:#ddd; background:#202020; padding:6px;}"
        "QListWidget::item:selected {background:#3b5261; color:#fff;}"));
    list_->viewport()->setPalette(darkPalette());
    const auto buttonStyle = QStringLiteral(
        "QPushButton {background:#3d3d3d; color:#eee; border:1px solid #555; border-radius:3px; padding:4px 8px; font-size:13px;}"
        "QPushButton:hover {background:#484848;} QPushButton:disabled {color:#888; background:#303030;}");
    for (auto* button : {install, directory, refreshButton}) {
        styleControl(button, buttonStyle);
        button->setFixedHeight(28);
    }
    styleControl(run_, buttonStyle);
    run_->setFixedHeight(26);
    styleControl(commands_, QStringLiteral(
        "QComboBox {background:#202020; color:#ddd; border:1px solid #454545; border-radius:3px; padding:3px 6px; font-size:13px;}"
        "QComboBox:disabled {color:#888;} QComboBox QAbstractItemView {background:#202020; color:#ddd; selection-background-color:#3b5261; selection-color:#fff;}"));
    commands_->setFixedHeight(26);
    commands_->setToolTip(QStringLiteral("选择插件命令"));
    commands_->view()->setPalette(darkPalette());
    commands_->view()->setStyleSheet(QStringLiteral("background:#202020; color:#ddd; selection-background-color:#3b5261; selection-color:#fff;"));
    const auto editorStyle = QStringLiteral(
        "QPlainTextEdit {background:#202020; color:#ddd; border:1px solid #454545; border-radius:3px; padding:4px; font-size:13px; selection-background-color:#3b5261; selection-color:#fff;}"
        "QPlainTextEdit:disabled {color:#888;}");
    for (auto* editor : {arguments_, result_}) {
        styleControl(editor, editorStyle);
        editor->viewport()->setPalette(darkPalette());
    }

    connect(refreshButton, &QPushButton::clicked, this, &PluginSettingsPanel::refresh);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateSelection(); });
    connect(install, &QPushButton::clicked, this, [this] {
        const auto package = QFileDialog::getOpenFileName(this, QStringLiteral("选择插件包"), QString(),
            QStringLiteral("Eagle 插件包 (*.eagleplugin *.zip)"));
        if (!package.isEmpty()) requestInstallation(package);
    });
    connect(directory, &QPushButton::clicked, this, [this] {
        const auto source = QFileDialog::getExistingDirectory(this, QStringLiteral("选择插件开发目录"));
        if (!source.isEmpty()) requestInstallation(source);
    });
    connect(enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        const auto id = selectedPluginId();
        if (id.isEmpty()) return;
        const auto result = manager_->setEnabled(id, enabled);
        const auto error = errorMessage(result);
        status_->setText(error.isEmpty() ? QStringLiteral("开关已保存, 即时生效.") : error);
        refresh();
    });
    connect(run_, &QPushButton::clicked, this, &PluginSettingsPanel::runCommand);
    connect(manager_, &PluginManager::changed, this, &PluginSettingsPanel::refresh);
    connect(manager_, &PluginManager::finished, this, [this](const QString& id, const QJsonObject& responseData) {
        if (id != runningRequest_) return;
        runningRequest_.clear();
        showResult(responseData);
        status_->setText(pluginCompletionMessage(responseData));
        updateSelection();
    });
    connect(manager_, &PluginManager::failed, this, [this](const QString& id, const QString&, const QString& message) {
        if (id != runningRequest_) return;
        runningRequest_.clear();
        result_->setPlainText(message);
        status_->setText(QStringLiteral("命令执行失败."));
        updateSelection();
    });
    refresh();
}

void PluginSettingsPanel::setActiveLibrary(const QString& libraryRoot)
{
    libraryRoot_ = libraryRoot;
    updateSelection();
}

void PluginSettingsPanel::setActiveLibraryProvider(std::function<QString()> provider)
{
    libraryProvider_ = std::move(provider);
}

void PluginSettingsPanel::selectPlugin(const QString& id, const QString& command)
{
    for (int row = 0; row < list_->count(); ++row) {
        if (list_->item(row)->data(Qt::UserRole).toString() == id) {
            list_->setCurrentRow(row);
            updateSelection();
            const int index = commands_->findData(command);
            if (index >= 0) commands_->setCurrentIndex(index);
            return;
        }
    }
}

void PluginSettingsPanel::requestInstallation(const QString& sourcePath)
{
    status_->setText(QStringLiteral("正在安装插件, 可继续使用 Billfish..."));
    status_->setToolTip(sourcePath);
    emit installRequested(sourcePath);
}

void PluginSettingsPanel::showInstallationResult(const QJsonObject& result)
{
    refresh();
    selectPlugin(result.value(QStringLiteral("id")).toString());
    const bool updated = result.value(QStringLiteral("completion")).toString() == QStringLiteral("plugin_updated");
    status_->setText(result.value(QStringLiteral("mode")).toString() == QStringLiteral("dependency") ?
        (updated ? QStringLiteral("依赖包已更新. 点击激活更新后供其他插件使用.") :
         QStringLiteral("依赖已安装, 供其他插件使用, 无需单独运行.")) : updated ?
        QStringLiteral("更新完成, 新版尚未启动. 勾选启用后再打开.") :
        QStringLiteral("安装完成, 尚未启用. 勾选启用后可打开插件."));
    status_->setToolTip(status_->text());
}

void PluginSettingsPanel::showInstallationError(const QString& code, const QString& message)
{
    status_->setText(QStringLiteral("安装失败 (%1): %2").arg(code, concise(message, 100)));
    status_->setToolTip(message);
}

void PluginSettingsPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!droppedPackage(event->mimeData(), true).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void PluginSettingsPanel::dropEvent(QDropEvent* event)
{
    const auto package = droppedPackage(event->mimeData(), true);
    if (!package.isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        requestInstallation(package);
        return;
    }
    QWidget::dropEvent(event);
}

QString PluginSettingsPanel::selectedPluginId() const
{
    return list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
}

void PluginSettingsPanel::refresh()
{
    const auto previous = selectedPluginId();
    const QSignalBlocker blocker(list_);
    list_->clear();
    plugins_.clear();
    const auto inventory = manager_->listPlugins();
    const auto plugins = inventory.value(QStringLiteral("plugins")).toArray();
    for (const auto& entry : plugins) {
        const auto plugin = entry.toObject();
        const auto id = plugin.value(QStringLiteral("id")).toString();
        plugins_.insert(id, plugin);
        auto* item = new QListWidgetItem(
            plugin.value(QStringLiteral("name")).toString(id) + QStringLiteral("\n") +
            displayStatus(plugin.value(QStringLiteral("status")).toString(), plugin.value(QStringLiteral("mode")).toString()), list_);
        item->setData(Qt::UserRole, id);
        item->setForeground(QColor(QStringLiteral("#dddddd")));
        item->setToolTip(plugin.value(QStringLiteral("name")).toString(id) + QStringLiteral("\n") + id);
        item->setSizeHint(QSize(0, 52));
        if (id == previous) list_->setCurrentItem(item);
    }
    if (!list_->currentItem() && list_->count()) list_->setCurrentRow(0);
    inventory_->setText(QStringLiteral("%1 个插件").arg(list_->count()));
    if (plugins.isEmpty()) status_->setText(QStringLiteral("可安装 .eagleplugin / ZIP 包, 或拖入单个插件包."));
    updateSelection();
}

void PluginSettingsPanel::updateSelection()
{
    const auto id = selectedPluginId();
    const auto plugin = plugins_.value(id);
    if (id != selectedId_) {
        selectedId_ = id;
        status_->clear();
        result_->clear();
        arguments_->setPlainText(QStringLiteral("{}"));
    }
    const auto mode = plugin.value(QStringLiteral("mode")).toString(QStringLiteral("incompatible"));
    const bool compatible = plugin.value(QStringLiteral("compatible")).toBool() && mode != QStringLiteral("incompatible");
    const bool commandsMode = compatible && mode == QStringLiteral("commands");
    const bool serviceMode = compatible && mode == QStringLiteral("service");
    const bool windowMode = compatible && mode == QStringLiteral("window");
    const bool dependencyMode = compatible && mode == QStringLiteral("dependency");
    const bool updated = plugin.value(QStringLiteral("status")).toString() == QStringLiteral("updated");
    const QSignalBlocker enabledBlocker(enabled_);
    const bool builtin = plugin.value(QStringLiteral("builtin")).toBool();
    enabled_->setVisible(!dependencyMode && !builtin);
    enabled_->setText(updated ? QStringLiteral("启用新版插件") : QStringLiteral("启用插件"));
    enabled_->setEnabled(!id.isEmpty() && compatible && !dependencyMode && !builtin);
    enabled_->setChecked(compatible && !dependencyMode && !updated && plugin.value(QStringLiteral("enabled")).toBool());
    const QString title = plugin.value(QStringLiteral("name")).toString() + QStringLiteral("  ") +
                          plugin.value(QStringLiteral("version")).toString();
    const QString description = plugin.value(QStringLiteral("description")).toString();
    description_->setText(description_->fontMetrics().elidedText(title, Qt::ElideRight, 300) +
                          QStringLiteral("\n") + concise(description, 40));
    description_->setToolTip(title + QStringLiteral("\n") + description);
    QStringList permissions;
    for (const auto& entry : plugin.value(QStringLiteral("permissions")).toArray()) permissions.append(entry.toString());
    permissions_->setText(id.isEmpty() ? QString() : QStringLiteral("权限: ") + permissionSummary(permissions) +
        QStringLiteral("\n独立进程, 请仅安装可信插件."));
    permissions_->setToolTip(permissions.join(QStringLiteral("\n")));
    commands_->setVisible(commandsMode);
    arguments_->setVisible(commandsMode);
    result_->setVisible(commandsMode);
    modeHelp_->setVisible(!commandsMode && !id.isEmpty());
    if (windowMode) modeHelp_->setText(QStringLiteral("窗口插件. 点击打开后, 在插件窗口中操作."));
    else if (serviceMode) modeHelp_->setText(QStringLiteral("后台服务随开关启动或停止. 可打开界面进行操作."));
    else if (dependencyMode) modeHelp_->setText(updated ?
        QStringLiteral("依赖包已更新. 需先激活更新, 再供其他插件加载; 不可单独运行.") :
        QStringLiteral("依赖已安装, 由其他插件加载. 此资源包不可单独运行, 无需启用."));
    else modeHelp_->setText(QStringLiteral("当前插件运行时不支持此插件. 文件已保留, 无法启用.") +
        QStringLiteral("\n") + plugin.value(QStringLiteral("error")).toString());
    const bool canOpen = plugin.value(QStringLiteral("can_open_window")).toBool();
    run_->setVisible(!builtin && (commandsMode || windowMode || (serviceMode && canOpen) || (dependencyMode && updated)));
    if (builtin) modeHelp_->setText(QStringLiteral("Billfish 内置角色动画浏览器. 双击 FBX 或 GLB 素材打开, 随应用维护."));
    run_->setText(dependencyMode && updated ? QStringLiteral("激活更新") : commandsMode ? QStringLiteral("运行命令") :
        serviceMode ? QStringLiteral("打开界面") : QStringLiteral("打开插件"));
    const auto previousCommand = commands_->currentData().toString();
    commands_->clear();
    for (const auto& entry : plugin.value(QStringLiteral("commands")).toArray()) {
        const auto command = entry.toObject();
        const auto commandId = command.value(QStringLiteral("id")).toString();
        commands_->addItem(command.value(QStringLiteral("title")).toString(commandId), commandId);
        commands_->setItemData(commands_->count() - 1, command.value(QStringLiteral("description")).toString(), Qt::ToolTipRole);
    }
    const auto index = commands_->findData(previousCommand);
    if (index >= 0) commands_->setCurrentIndex(index);
    run_->setEnabled(!id.isEmpty() && compatible && runningRequest_.isEmpty() &&
        ((dependencyMode && updated) || (plugin.value(QStringLiteral("status")).toString() == QStringLiteral("ready") &&
         (commandsMode ? commands_->count() > 0 : canOpen))));
    arguments_->setEnabled(!id.isEmpty() && runningRequest_.isEmpty());
    commands_->setEnabled(runningRequest_.isEmpty());
    if (!plugin.value(QStringLiteral("error")).toString().isEmpty()) {
        status_->setText(plugin.value(QStringLiteral("error")).toString());
    }
}

void PluginSettingsPanel::runCommand()
{
    const QString pluginId = selectedPluginId();
    const auto plugin = plugins_.value(pluginId);
    if (plugin.value(QStringLiteral("mode")).toString() == QStringLiteral("dependency")) {
        const auto activated = manager_->setEnabled(pluginId, true);
        const auto error = errorMessage(activated);
        status_->setText(error.isEmpty() ? QStringLiteral("依赖更新已激活, 可供其他插件使用.") : QStringLiteral("激活失败: ") + error);
        status_->setToolTip(error.isEmpty() ? status_->text() : error);
        refresh();
        return;
    }
    const bool commandsMode = plugin.value(QStringLiteral("mode")).toString() == QStringLiteral("commands");
    QJsonObject arguments;
    if (commandsMode) {
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(arguments_->toPlainText().toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            status_->setText(QStringLiteral("参数必须是有效的 JSON object: ") + parse.errorString());
            return;
        }
        arguments = document.object();
    }
    const QString commandId = commandsMode ? commands_->currentData().toString() :
        plugin.value(QStringLiteral("open_command")).toString(QStringLiteral("run"));
    const QString libraryRoot = libraryProvider_ ? libraryProvider_() : libraryRoot_;
    runningRequest_ = QStringLiteral("plugin-ui-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    status_->setText(commandsMode ? QStringLiteral("正在运行插件命令...") : QStringLiteral("正在打开插件界面..."));
    result_->clear();
    updateSelection();
    manager_->run(runningRequest_, pluginId, commandId, arguments, libraryRoot);
}

void PluginSettingsPanel::showResult(const QJsonObject& result)
{
    result_->setPlainText(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
}

AutomationUi::AutomationUi(PluginManager* manager, QObject* parent)
    : QObject(parent), manager_(manager), hideTimer_(new QTimer(this))
{
    hideTimer_->setSingleShot(true);
    connect(hideTimer_, &QTimer::timeout, this, [this] {
        if (progress_) progress_->hide();
        tasks_.clear();
    });
    connect(manager_, &PluginManager::progress, this, &AutomationUi::updateProgress);
    connect(manager_, &PluginManager::finished, this, [this](QString id, const QJsonObject& response) {
        const bool ownInstall = installationRequests_.remove(id);
        const auto completion = response.value(QStringLiteral("completion")).toString();
        const bool installResult = response.value(QStringLiteral("operation")).toString() == QStringLiteral("plugins.install") &&
            (completion == QStringLiteral("plugin_installed") || completion == QStringLiteral("plugin_updated"));
        if (ownInstall || installResult) {
            pendingPluginId_ = response.value(QStringLiteral("id")).toString();
            pendingInstallationResult_ = response;
            showPluginSettings();
        }
        tasks_[id].completion = completion;
        finishTask(id, true, pluginCompletionMessage(response));
    });
    connect(manager_, &PluginManager::failed, this, [this](QString id, const QString& code, QString message) {
        if (installationRequests_.remove(id) && panel_) panel_->showInstallationError(code, message);
        finishTask(id, false, message);
    });
    qApp->installEventFilter(this);
    QTimer::singleShot(0, this, [this] {
        for (auto* widget : QApplication::allWidgets()) observeWidget(widget);
    });
}

void AutomationUi::setActiveLibrary(const QString& libraryRoot)
{
    libraryRoot_ = libraryRoot;
    if (panel_) panel_->setActiveLibrary(libraryRoot);
}

void AutomationUi::setActiveLibraryProvider(std::function<QString()> provider)
{
    libraryProvider_ = std::move(provider);
    if (panel_) panel_->setActiveLibraryProvider(libraryProvider_);
}

void AutomationUi::setMaterialContextProvider(std::function<QJsonObject()> provider)
{
    materialContextProvider_ = std::move(provider);
}

void AutomationUi::setFormatPreviewHandler(std::function<void(QString, QString, QString)> handler)
{
    formatPreviewHandler_ = std::move(handler);
}

void AutomationUi::setMainWindow(QWidget* mainWindow)
{
    if (mainWindow_ == mainWindow) return;
    if (progress_) delete progress_.data();
    progressBar_ = nullptr;
    progressMessage_ = nullptr;
    if (pluginMenuAction_) delete pluginMenuAction_.data();
    mainWindow_ = mainWindow;
    if (mainWindow_) {
        mainWindow_->setAcceptDrops(true);
        auto* action = new QAction(QStringLiteral("插件"), mainWindow_);
        action->setObjectName(QStringLiteral("BillfishPluginMenuAction"));
        action->setShortcut(QKeySequence(Qt::Key_P));
        action->setShortcutContext(Qt::WindowShortcut);
        action->setAutoRepeat(false);
        mainWindow_->addAction(action);
        pluginMenuAction_ = action;
        connect(action, &QAction::triggered, this, [this, action] {
            const auto position = action->property("billfishPluginMenuPosition");
            action->setProperty("billfishPluginMenuPosition", QVariant());
            showPluginMenu(position.canConvert<QPoint>() ? position.toPoint() : QCursor::pos());
        });
    }
    if (!tasks_.isEmpty()) renderProgress();
}

void AutomationUi::updateProgress(QString requestId, int completed, int total, QString message)
{
    if (hideTimer_->isActive()) tasks_.clear();
    hideTimer_->stop();
    auto& task = tasks_[requestId];
    task.completed = std::max(0, completed);
    task.total = total > 0 ? total : 0;
    if (task.total > 0) task.completed = std::min(task.completed, task.total);
    task.message = concise(message);
    task.completion.clear();
    task.finished = false;
    task.success = true;
    task.sequence = ++sequence_;
    renderProgress();
}

void AutomationUi::finishTask(QString requestId, bool success, QString message)
{
    auto& task = tasks_[requestId];
    task.finished = true;
    task.success = success;
    task.message = concise(message);
    task.sequence = ++sequence_;
    if (success && task.total > 0) task.completed = task.total;
    renderProgress();
    bool active = false;
    bool allSuccessful = true;
    for (const auto& entry : tasks_) {
        active = active || !entry.finished;
        allSuccessful = allSuccessful && entry.success;
    }
    if (!active) hideTimer_->start(allSuccessful ? 2500 : 5000);
}

void AutomationUi::createProgress()
{
    if (progress_ || !mainWindow_) return;
    auto* frame = new QFrame(mainWindow_);
    frame->setObjectName(QStringLiteral("BillfishAutomationProgress"));
    frame->setAttribute(Qt::WA_ShowWithoutActivating);
    frame->setAttribute(Qt::WA_TransparentForMouseEvents);
    frame->setFocusPolicy(Qt::NoFocus);
    frame->setStyleSheet(QStringLiteral(
        "QFrame#BillfishAutomationProgress {background:#2d343a; border:1px solid #52616d; border-radius:5px;}"
        "QFrame#BillfishAutomationProgress QLabel {color:#eee; background:transparent; border:0;}"
        "QProgressBar {border:0; background:#20262a; color:#eee; border-radius:3px; text-align:center;}"
        "QProgressBar::chunk {background:#4389b4; border-radius:3px;}"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(6);
    progressMessage_ = new QLabel(frame);
    progressMessage_->setObjectName(QStringLiteral("BillfishAutomationProgressMessage"));
    progressMessage_->setFocusPolicy(Qt::NoFocus);
    progressBar_ = new QProgressBar(frame);
    progressBar_->setObjectName(QStringLiteral("BillfishAutomationProgressBar"));
    progressBar_->setFocusPolicy(Qt::NoFocus);
    progressBar_->setFixedHeight(14);
    layout->addWidget(progressMessage_);
    layout->addWidget(progressBar_);
    progress_ = frame;
}

void AutomationUi::renderProgress()
{
    createProgress();
    if (!progress_ || tasks_.isEmpty()) return;
    const Task* latest = nullptr;
    int active = 0;
    qint64 completed = 0;
    qint64 total = 0;
    bool indeterminate = false;
    bool success = true;
    for (auto it = tasks_.cbegin(); it != tasks_.cend(); ++it) {
        const auto& task = it.value();
        if (!task.finished) {
            ++active;
            indeterminate = indeterminate || task.total == 0;
            completed += task.completed;
            total += task.total;
            if (!latest || latest->finished || task.sequence > latest->sequence) latest = &task;
        } else if (!latest || (latest->finished && task.sequence > latest->sequence)) {
            latest = &task;
        }
        success = success && task.success;
    }
    QString message = latest ? latest->message : QString();
    if (message.isEmpty()) message = active ? QStringLiteral("AI / 插件任务正在运行") : QStringLiteral("任务已完成");
    if (active > 1) message = QStringLiteral("%1 个任务: ").arg(active) + message;
    if (active == 0) {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(success ? 100 : 0);
        const auto completion = latest ? latest->completion : QString();
        progressBar_->setFormat(!success ? QStringLiteral("失败") : completion == QStringLiteral("plugin_window_opened") ?
            QStringLiteral("已打开") : completion == QStringLiteral("plugin_service_started") ?
            QStringLiteral("已启动") : completion == QStringLiteral("plugin_installed") ? QStringLiteral("已安装") :
            completion == QStringLiteral("plugin_updated") ? QStringLiteral("已更新") : QStringLiteral("完成"));
    } else if (indeterminate || total == 0) {
        progressBar_->setRange(0, 0);
    } else {
        // Compute in 64-bit space; the widget receives a bounded percentage.
        progressBar_->setRange(0, 100);
        progressBar_->setValue(static_cast<int>(completed * 100 / total));
        progressBar_->setFormat(QStringLiteral("%1 / %2 (%p%)").arg(completed).arg(total));
    }
    progressMessage_->setText(message);
    positionProgress();
    progress_->show();
    progress_->raise();
}

void AutomationUi::positionProgress()
{
    if (!mainWindow_ || !progress_) return;
    const int width = std::max(1, std::min(480, mainWindow_->width() - 32));
    progress_->setGeometry(std::max(0, mainWindow_->width() - width - 16),
                           std::max(0, mainWindow_->height() - 82), width, 66);
    if (progressMessage_) {
        const auto message = progressMessage_->text();
        progressMessage_->setToolTip(message);
    }
}

void AutomationUi::observeWidget(QWidget* widget)
{
    if (!widget) return;
    if (AppInputScope::active() && QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("BFLoadingDialog")
        && widget->objectName() == QStringLiteral("kLoading_dialog") && !scopedLoadingWindows_.contains(widget)) {
        auto* window = widget->windowHandle();
        scopedLoadingWindows_.insert(widget, {widget->testAttribute(Qt::WA_ShowWithoutActivating), window,
            window ? window->property("_q_showWithoutActivating") : QVariant()});
        connect(widget, &QObject::destroyed, this, &AutomationUi::forgetLoadingWindow, Qt::UniqueConnection);
        // Show precedes show_sys, but Qt has already copied this attribute to
        // QWindow during create(). Update both values before qwindows shows it.
        widget->setAttribute(Qt::WA_ShowWithoutActivating);
        if (window) window->setProperty("_q_showWithoutActivating", true);
    }
    if (QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("CFramelessWindow")) setMainWindow(widget);
    if (QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("Preferences")) integratePreferences(widget);
    if (QByteArray(widget->metaObject()->className()) == QByteArrayLiteral("BFFileMenu"))
        integrateFileMenu(qobject_cast<QMenu*>(widget));
}

void AutomationUi::forgetLoadingWindow(QObject* widget)
{
    scopedLoadingWindows_.remove(widget);
}

bool AutomationUi::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop) {
        auto* widget = qobject_cast<QWidget*>(watched);
        const bool onPage = widget && panel_ && (widget == panel_ || panel_->isAncestorOf(widget));
        const bool onMainWindow = widget && mainWindow_ && widget->window() == mainWindow_;
        if (onPage || onMainWindow) {
            auto* drop = static_cast<QDropEvent*>(event);
            const auto package = droppedPackage(drop->mimeData(), onPage);
            if (!package.isEmpty()) {
                drop->setDropAction(Qt::CopyAction);
                drop->accept();
                if (event->type() == QEvent::Drop) {
                    if (onPage) panel_->requestInstallation(package);
                    else installPluginSource(package);
                }
                return true;
            }
        }
    }
    if (event->type() == QEvent::Show) observeWidget(qobject_cast<QWidget*>(watched));
    if (event->type() == QEvent::Hide && scopedLoadingWindows_.contains(watched)) {
        const auto original = scopedLoadingWindows_.take(watched);
        static_cast<QWidget*>(watched)->setAttribute(Qt::WA_ShowWithoutActivating, original.showWithoutActivating);
        if (original.window) original.window->setProperty("_q_showWithoutActivating", original.nativeShowWithoutActivating);
    }
    if (watched == mainWindow_ && event->type() == QEvent::Resize) positionProgress();
    if (watched == preferences_ && event->type() == QEvent::Resize) positionPluginButton();
    return QObject::eventFilter(watched, event);
}

void AutomationUi::integratePreferences(QWidget* preferences)
{
    if (AppInputScope::active()) preferences->setAttribute(Qt::WA_ShowWithoutActivating);
    if (preferences_ == preferences && panel_) {
        positionPluginButton();
        if (pendingOpenPluginSettings_) selectPluginPage();
        return;
    }
    // These direct-parent identities and the native 40 px navigation spacing
    // were observed in runtime-preferences-parents.json for the frozen app.
    // Its root grid can coexist with manually positioned direct children.
    auto* common = preferences->findChild<QWidget*>(QStringLiteral("PreferencesCommonWidget"));
    auto* stack = common ? qobject_cast<QStackedWidget*>(common->parentWidget()) : nullptr;
    if (!stack || stack->parentWidget() != preferences) return;
    const QStringList pageNames = {
        QStringLiteral("PreferencesCommonWidget"), QStringLiteral("PreferencesLibraryWidget"),
        QStringLiteral("PreferencesMediaWidget"), QStringLiteral("PreferencesUserInterface"),
        QStringLiteral("PreferencesAction"), QStringLiteral("PreferencesShortcut"),
        QStringLiteral("PreferencesNotification")};
    const QStringList buttonNames = {
        QStringLiteral("btn_common"), QStringLiteral("btn_library"), QStringLiteral("btn_media"),
        QStringLiteral("btn_ui"), QStringLiteral("btn_action"), QStringLiteral("btn_shortcut"),
        QStringLiteral("btn_notification")};
    QHash<QObject*, QPointer<QWidget>> nativePages;
    for (int index = 0; index < pageNames.size(); ++index) {
        auto* page = stack->findChild<QWidget*>(pageNames.at(index), Qt::FindDirectChildrenOnly);
        auto* button = preferences->findChild<QWidget*>(buttonNames.at(index), Qt::FindDirectChildrenOnly);
        if (!page || stack->indexOf(page) < 0 || !button ||
            QByteArray(page->metaObject()->className()) != pageNames.at(index).toLatin1() ||
            QByteArray(button->metaObject()->className()) != QByteArrayLiteral("IconButtonWidget") ||
            button->metaObject()->indexOfSignal("SignBtnSelect(SettingMode)") < 0) return;
        nativePages.insert(button, page);
    }
    auto* last = preferences->findChild<QWidget*>(QStringLiteral("btn_notification"), Qt::FindDirectChildrenOnly);
    auto* previous = preferences->findChild<QWidget*>(QStringLiteral("btn_shortcut"), Qt::FindDirectChildrenOnly);
    auto* grid = qobject_cast<QGridLayout*>(preferences->layout());
    if (!grid) return;
    const int lastIndex = grid->indexOf(last);
    int nextRow = -1;
    int column = -1;
    int columnSpan = 1;
    if (lastIndex >= 0) {
        const int previousIndex = grid->indexOf(previous);
        if (previousIndex < 0) return;
        int row = 0, rowSpan = 0, previousRow = 0, previousColumn = 0, previousRowSpan = 0, previousColumnSpan = 0;
        grid->getItemPosition(lastIndex, &row, &column, &rowSpan, &columnSpan);
        grid->getItemPosition(previousIndex, &previousRow, &previousColumn, &previousRowSpan, &previousColumnSpan);
        if (rowSpan != 1 || previousRowSpan != 1 || column != previousColumn || row <= previousRow) return;
        nextRow = row + (row - previousRow);
        for (int col = column; col < column + columnSpan; ++col)
            if (grid->itemAtPosition(nextRow, col)) return;
    } else if (last->y() <= previous->y() || last->height() <= 0) {
        return;
    }

    preferences_ = preferences;
    settingsStack_ = stack;
    nativePages_ = nativePages;
    panel_ = new PluginSettingsPanel(manager_, stack);
    connect(panel_, &PluginSettingsPanel::installRequested, this, &AutomationUi::installPluginSource);
    panel_->setActiveLibrary(libraryRoot_);
    panel_->setActiveLibraryProvider(libraryProvider_);
    stack->addWidget(panel_);
    auto* button = new QPushButton(QStringLiteral("插件"), preferences);
    button->setObjectName(QStringLiteral("BillfishPluginsNavigation"));
    button->setCheckable(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {color:#ddd; background:transparent; border:0; border-radius:3px; text-align:left; padding-left:42px;}"
        "QPushButton:hover {background:#343434;}"
        "QPushButton:checked {background:#39434b; color:#75b7df;}"));
    pluginButton_ = button;
    pluginButtonManaged_ = lastIndex >= 0;
    if (pluginButtonManaged_) grid->addWidget(button, nextRow, column, 1, columnSpan);
    else positionPluginButton();
    button->show();
    button->raise();
    connect(button, &QPushButton::clicked, this, &AutomationUi::selectPluginPage);
    for (auto it = nativePages_.cbegin(); it != nativePages_.cend(); ++it)
        connect(it.key(), SIGNAL(SignBtnSelect(SettingMode)), this, SLOT(nativePreferencesSelected()));
    connect(stack, &QStackedWidget::currentChanged, button, [this](int) {
        if (pluginButton_ && settingsStack_ && panel_)
            pluginButton_->setChecked(settingsStack_->currentWidget() == panel_);
    });
    if (pendingOpenPluginSettings_) selectPluginPage();
}

void AutomationUi::positionPluginButton()
{
    if (!pluginButton_ || !preferences_ || pluginButtonManaged_) return;
    auto* last = preferences_->findChild<QWidget*>(QStringLiteral("btn_notification"), Qt::FindDirectChildrenOnly);
    auto* previous = preferences_->findChild<QWidget*>(QStringLiteral("btn_shortcut"), Qt::FindDirectChildrenOnly);
    if (!last || !previous) return;
    const int step = last->y() - previous->y();
    if (step <= 0) return;
    pluginButton_->setGeometry(last->x(), last->y() + step, last->width(), last->height());
}

void AutomationUi::selectPluginPage()
{
    if (!settingsStack_ || !panel_) return;
    pendingOpenPluginSettings_ = false;
    for (auto it = nativePages_.cbegin(); it != nativePages_.cend(); ++it)
        it.key()->setProperty("checked", false);
    settingsStack_->setCurrentWidget(panel_);
    if (pluginButton_) pluginButton_->setChecked(true);
    if (!pendingPluginId_.isEmpty()) {
        panel_->selectPlugin(pendingPluginId_, pendingCommandId_);
        pendingPluginId_.clear();
        pendingCommandId_.clear();
    }
    if (!pendingInstallationResult_.isEmpty()) {
        panel_->showInstallationResult(pendingInstallationResult_);
        pendingInstallationResult_ = QJsonObject{};
    }
}

void AutomationUi::nativePreferencesSelected()
{
    const auto page = nativePages_.value(sender());
    if (settingsStack_ && page) settingsStack_->setCurrentWidget(page);
    if (pluginButton_) pluginButton_->setChecked(false);
}

bool AutomationUi::showPluginSettings()
{
    QWidget* existing = preferences_;
    for (auto* widget : QApplication::allWidgets()) {
        if (QByteArray(widget->metaObject()->className()) != QByteArrayLiteral("Preferences")) continue;
        if (!existing) existing = widget;
        if (widget->isVisible()) { existing = widget; break; }
    }
    if (existing) integratePreferences(existing);
    if (preferences_ && panel_ && settingsStack_) {
        pendingOpenPluginSettings_ = true;
        const bool scopedInput = AppInputScope::active();
        preferences_->setAttribute(Qt::WA_ShowWithoutActivating, scopedInput);
        preferences_->show();
        selectPluginPage();
        if (!scopedInput) {
            preferences_->raise();
            preferences_->activateWindow();
        }
        return true;
    }

    // The shipped menu exposes Ctrl+, for Preferences. Trigger the actual
    // QAction, rather than synthesizing keys or constructing a vendor dialog.
    QSet<QAction*> actions;
    for (auto* action : qApp->findChildren<QAction*>()) actions.insert(action);
    for (auto* widget : QApplication::topLevelWidgets())
        for (auto* action : widget->findChildren<QAction*>()) actions.insert(action);
    for (auto* widget : QApplication::allWidgets())
        for (auto* action : widget->actions()) actions.insert(action);
    QList<QAction*> matching;
    const QKeySequence preferencesShortcut(Qt::CTRL | Qt::Key_Comma);
    for (auto* action : actions) {
        if (!action->isEnabled() || action->isSeparator()) continue;
        for (const auto& shortcut : action->shortcuts()) {
            if (shortcut.matches(preferencesShortcut) == QKeySequence::ExactMatch) {
                matching.append(action);
                break;
            }
        }
    }
    if (matching.size() != 1) return false;
    pendingOpenPluginSettings_ = true;
    // A native handler may enter QDialog::exec(). Queue it so the API request
    // can return while the application's normal event loop opens the dialog.
    auto* action = matching.first();
    const bool scopedInput = AppInputScope::active();
    QTimer::singleShot(0, action, [action, scopedInput] {
        AppInputScope scope(scopedInput);
        action->trigger();
    });
    return true;
}

void AutomationUi::runMenuPlugin(const QString& pluginId, const QString& command)
{
    const QString libraryRoot = libraryProvider_ ? libraryProvider_() : libraryRoot_;
    const QString requestId = QStringLiteral("plugin-menu-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    manager_->run(requestId, pluginId, command, QJsonObject{}, libraryRoot);
}

void AutomationUi::installPluginSource(const QString& sourcePath)
{
    const auto requestId = QStringLiteral("plugin-install-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    installationRequests_.insert(requestId);
    manager_->installPluginAsync(requestId, sourcePath);
}

void AutomationUi::showPluginMenu(const QPoint& globalPosition)
{
    if (pluginMenu_) pluginMenu_->close();
    auto* menu = new QMenu(mainWindow_);
    menu->setObjectName(QStringLiteral("BillfishPluginMenu"));
    styleControl(menu, QStringLiteral(
        "QMenu {background:#282828; color:#ddd; border:1px solid #555; padding:4px;}"
        "QMenu::item {padding:7px 24px 7px 12px;}"
        "QMenu::item:selected {background:#3b5261; color:#fff;}"
        "QMenu::item:disabled {color:#888;}"
        "QMenu::separator {height:1px; background:#484848; margin:4px 6px;}"));
    pluginMenu_ = menu;
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    fillPluginMenu(menu, false);
    showScopedPopup(menu, globalPosition);
}

bool AutomationUi::materialContextMatches(const QJsonObject& context) const
{
    if (!materialContextProvider_ || !context.value(QStringLiteral("available")).toBool() ||
        context.value(QStringLiteral("library_root")).toString().isEmpty() ||
        context.value(QStringLiteral("items")).toArray().isEmpty()) return false;
    return materialContextProvider_() == context;
}

void AutomationUi::integrateFileMenu(QMenu* menu)
{
    if (!menu) return;
    QAction* exportAction = nullptr;
    auto* plugins = menu->findChild<QMenu*>(QStringLiteral("BillfishContextPluginMenu"), Qt::FindDirectChildrenOnly);
    for (auto* action : menu->actions()) {
        if (action->menu() && action->menu()->objectName() == QStringLiteral("BillfishContextPluginMenu"))
            plugins = action->menu();
        const auto label = action->text().remove(QLatin1Char('&')).trimmed();
        if (action->menu() && (label == QStringLiteral("导出") || label.compare(QStringLiteral("Export"), Qt::CaseInsensitive) == 0))
            exportAction = action;
    }
    if (!exportAction) return;
    if (!plugins) {
        plugins = new QMenu(QStringLiteral("插件"), menu);
        plugins->setObjectName(QStringLiteral("BillfishContextPluginMenu"));
        plugins->menuAction()->setObjectName(QStringLiteral("BillfishContextPluginAction"));
        connect(plugins, &QMenu::aboutToShow, this, [this, plugins] { fillPluginMenu(plugins, true); });
    }
    if (!menu->actions().contains(plugins->menuAction())) menu->insertMenu(exportAction, plugins);
    const auto* nativeSubmenu = exportAction->menu();
    plugins->setWindowFlags(nativeSubmenu->windowFlags());
    plugins->setAttribute(Qt::WA_TranslucentBackground, nativeSubmenu->testAttribute(Qt::WA_TranslucentBackground));
    plugins->setFont(nativeSubmenu->font());
    plugins->setPalette(nativeSubmenu->palette());
    plugins->setStyle(nativeSubmenu->style());
    plugins->setStyleSheet(nativeSubmenu->styleSheet());
    QIcon icon;
    for (const int scale : {1, 2}) {
        QPixmap pixmap(24 * scale, 24 * scale);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(scale);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        // BFFileMenu draws its foreground through QSS (#D9D9D9); its
        // WindowText palette remains black even in the installed dark theme.
        painter.setPen(QPen(QColor(QStringLiteral("#D9D9D9")), 1.25,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(billfish::shell::pluginGlyphPath());
        painter.end();
        icon.addPixmap(pixmap);
    }
    plugins->menuAction()->setIcon(icon);
    fillPluginMenu(plugins, true);
}

void AutomationUi::fillPluginMenu(QMenu* menu, bool materialContext)
{
    // QMenu::clear() removes menu actions but does not own their QMenu objects.
    // This menu persists across native context-menu invocations.
    for (auto* action : menu->actions())
        if (action->menu() && action->menu()->parentWidget() == menu) delete action->menu();
    menu->clear();
    const auto context = materialContext && materialContextProvider_ ? materialContextProvider_() : QJsonObject{};
    const bool contextAvailable = !materialContext || materialContextMatches(context);
    int count = 0;
    for (const auto& entry : manager_->listPlugins().value(QStringLiteral("plugins")).toArray()) {
        const auto plugin = entry.toObject();
        if (!plugin.value(QStringLiteral("compatible")).toBool() || !plugin.value(QStringLiteral("enabled")).toBool()) continue;
        const auto mode = plugin.value(QStringLiteral("mode")).toString();
        const auto id = plugin.value(QStringLiteral("id")).toString();
        if (plugin.value(QStringLiteral("builtin")).toBool() || (materialContext && id == QStringLiteral("89fb801f-7761-4de6-b7ce-3785a084edaf"))) continue;
        const auto name = plugin.value(QStringLiteral("name")).toString(id);
        const bool ready = plugin.value(QStringLiteral("status")).toString() == QStringLiteral("ready");
        const auto items = context.value(QStringLiteral("items")).toArray();
        const auto item = items.size() == 1 ? items.first().toObject() : QJsonObject{};
        const auto extension = item.value(QStringLiteral("extension")).toString();
        const bool preview = materialContext && formatPreviewHandler_ && contextAvailable &&
            !extension.isEmpty() && manager_->previewPlugin(QStringLiteral("item.") + extension,
                QStringLiteral("viewer"), id) == id;
        if (preview) {
            auto* action = menu->addAction(name + QStringLiteral("预览"));
            action->setObjectName(QStringLiteral("BillfishContextFormatPreview"));
            connect(action, &QAction::triggered, this, [this, context, id, item] {
                if (!materialContextMatches(context)) return;
                const auto extension = item.value(QStringLiteral("extension")).toString();
                if (manager_->previewPlugin(QStringLiteral("item.") + extension, QStringLiteral("viewer"), id) != id) return;
                formatPreviewHandler_(id, item.value(QStringLiteral("item_id")).toString(),
                    context.value(QStringLiteral("library_root")).toString());
            });
            ++count;
        }
        if (mode == QStringLiteral("commands")) {
            auto* commandsMenu = menu->addMenu(name);
            commandsMenu->setEnabled(ready && contextAvailable);
            for (const auto& declaration : plugin.value(QStringLiteral("manifest_commands")).toArray()) {
                const auto command = declaration.toObject();
                const auto commandId = command.value(QStringLiteral("id")).toString();
                auto* action = commandsMenu->addAction(command.value(QStringLiteral("title")).toString(commandId) + QStringLiteral("..."));
                connect(action, &QAction::triggered, this, [this, id, commandId, materialContext, context] {
                    if (materialContext && !materialContextMatches(context)) return;
                    pendingPluginId_ = id;
                    pendingCommandId_ = commandId;
                    if (!showPluginSettings()) {
                        pendingPluginId_.clear();
                        pendingCommandId_.clear();
                    }
                });
            }
            ++count;
        } else if ((mode == QStringLiteral("window") || mode == QStringLiteral("service")) &&
                   plugin.value(QStringLiteral("can_open_window")).toBool()) {
            const bool hasPreview = !plugin.value(QStringLiteral("preview_extensions")).toObject().isEmpty();
            auto* action = menu->addAction(name + (materialContext && hasPreview ? QStringLiteral("设置...") : QString()));
            action->setEnabled(ready && contextAvailable);
            action->setToolTip(plugin.value(QStringLiteral("description")).toString());
            const auto command = plugin.value(QStringLiteral("open_command")).toString(QStringLiteral("run"));
            connect(action, &QAction::triggered, this, [this, id, command, materialContext, context] {
                if (materialContext && !materialContextMatches(context)) return;
                runMenuPlugin(id, command);
            });
            ++count;
        }
    }
    if (count == 0) menu->addAction(QStringLiteral("暂无可用的插件功能"))->setEnabled(false);
    menu->addSeparator();
    auto* manage = menu->addAction(QStringLiteral("插件管理..."));
    connect(manage, &QAction::triggered, this, [this] { showPluginSettings(); });
}

} // namespace billfish::automation
