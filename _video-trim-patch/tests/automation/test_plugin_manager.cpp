#include "automation/plugin_manager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using billfish::automation::PluginManager;

class PluginManagerTest final : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> temporary_;
    QByteArray previousAppData_;
    QByteArray previousElectron_;
    QString installRoot_;
    QString runtimeRoot_;
    QString source_;

    void write(const QString& path, const QByteArray& data) {
        QVERIFY(temporary_ && temporary_->isValid());
        const auto destination = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
        const auto root = QDir::cleanPath(QDir::fromNativeSeparators(temporary_->path())) + QLatin1Char('/');
        QVERIFY2(destination.startsWith(root, Qt::CaseInsensitive), "Plugin test writes must remain within the current temporary fixture.");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), qint64(data.size()));
    }

    static QJsonObject plugin(const PluginManager& manager) {
        return pluginById(manager, QStringLiteral("test.lifecycle"));
    }

    static QJsonObject pluginById(const PluginManager& manager, const QString& id) {
        const auto plugins = manager.listPlugins().value(QStringLiteral("plugins")).toArray();
        for (const auto& value : plugins)
            if (value.toObject().value(QStringLiteral("id")).toString() == id) return value.toObject();
        return {};
    }

    static bool waitReady(const PluginManager& manager) {
        QTest::qWaitFor([&manager]() {
            const auto status = plugin(manager).value(QStringLiteral("status")).toString();
            return status == QStringLiteral("ready") || status == QStringLiteral("failed") ||
                status == QStringLiteral("incompatible") || status == QStringLiteral("waiting-context");
        }, 10000);
        return plugin(manager).value(QStringLiteral("status")).toString() == QStringLiteral("ready");
    }

    static QJsonObject installResult(PluginManager& manager, const QString& source) {
        QSignalSpy completed(&manager, &PluginManager::finished);
        QSignalSpy errors(&manager, &PluginManager::failed);
        manager.installPluginAsync(QStringLiteral("install-test"), source);
        if (!QTest::qWaitFor([&]() { return !completed.isEmpty() || !errors.isEmpty(); }, 15000))
            return {{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("timeout")}}}};
        if (!errors.isEmpty()) return {{QStringLiteral("error"), QJsonObject{
            {QStringLiteral("code"), errors.first().at(1).toString()},
            {QStringLiteral("message"), errors.first().at(2).toString()}}}};
        return completed.first().at(1).toJsonObject();
    }

    void install(PluginManager& manager) {
        manager.setLibraryRoot(QStringLiteral("X:/library"));
        const auto result = installResult(manager, source_);
        QVERIFY2(!result.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(result).toJson())));
    }

private slots:
    void builtinViewerCancellationIgnoresLateResultsAndKeepsThumbnailWork() {
        const auto builtinDirectory = QDir(installRoot_).filePath(QStringLiteral("automation/animation_browser"));
        QVERIFY(QDir().mkpath(builtinDirectory + QStringLiteral("/ui")));
        write(builtinDirectory + QStringLiteral("/ui/index.html"), "<!doctype html><body>transport fixture</body>");
        write(builtinDirectory + QStringLiteral("/manifest.json"), R"JSON({"id":"billfish.animation-browser","name":"Builtin","version":"1","main":{"url":"ui/index.html"},"preview":{"fbx":{"viewer":{"path":"ui/index.html"},"thumbnail":{"path":"ui/index.html"}}}})JSON");
        // This isolated peer deliberately completes the cancelled request after
        // the newer request starts. Release files control completion, not time.
        write(QDir(installRoot_).filePath(QStringLiteral("automation/plugin_runtime/main.cjs")), R"JS(
const {app}=require('electron'),net=require('node:net'),readline=require('node:readline'),fs=require('node:fs'),path=require('node:path');
fs.mkdirSync(process.env.BILLFISH_PLUGIN_PROFILE,{recursive:true});
app.setPath('userData',process.env.BILLFISH_PLUGIN_PROFILE);
app.on('window-all-closed',()=>{});
app.whenReady().then(()=>{
  const pipe=net.createConnection('\\\\.\\pipe\\'+process.argv[3]);
  const send=value=>pipe.write(JSON.stringify(value)+'\n');
  const complete=message=>send({type:'result',id:message.id,data:{completion:message.kind==='viewer'?'plugin_viewer_rendered':'plugin_thumbnail_rendered',item_id:message.item.id}});
  let old,latestIntent,feedback;
  pipe.on('connect',()=>send({type:'ready',capabilities:{preview:true,previewProtocol:1}}));
  readline.createInterface({input:pipe}).on('line',line=>{
    const message=JSON.parse(line);
    if(message.type==='shutdown'){app.exit(0);return;}
    if(message.type==='preview_intent'){latestIntent=message.id;feedback=message.feedback;return;}
    if(message.type==='preview_intent_error'){if(message.id===latestIntent&&message.code==='preview_source_missing'&&message.message==='Source missing')fs.writeFileSync(path.join(process.argv[2],'intent-error-observed'),'accepted');return;}
    if(message.type!=='preview')return;
    if(message.kind==='viewer'&&message.viewer_intent!==latestIntent){send({type:'error',id:message.id,code:'missing_intent',message:'viewer intent did not match'});return;}
    if(message.id==='old-viewer'){if(feedback?.item_id!=='1'||feedback?.extension!=='.fbx'||feedback?.library_root!=='X:/library'||feedback?.background!==true){send({type:'error',id:message.id,code:'missing_feedback',message:'native loading context did not match'});return;}old=message;return;}
    if(message.id==='new-viewer'){
      complete(old);
      send({type:'progress',id:message.id,completed:0,total:1,message:'held'});
    }
    const release=path.join(process.argv[2],message.kind==='viewer'?'release-viewer':'release-thumbnail');
    const timer=setInterval(()=>{if(fs.existsSync(release)){clearInterval(timer);complete(message);}},10);
  });
});
)JS");
        PluginManager manager(installRoot_);
        QSignalSpy completed(&manager, &PluginManager::finished), progress(&manager, &PluginManager::progress), errors(&manager, &PluginManager::failed);
        manager.setLibraryRoot(QStringLiteral("X:/library"));
        const auto builtin = QStringLiteral("billfish.animation-browser");
        QTRY_COMPARE_WITH_TIMEOUT(pluginById(manager, builtin).value(QStringLiteral("status")).toString(), QStringLiteral("ready"), 10000);
        const QJsonObject item{{QStringLiteral("id"), QStringLiteral("1")}, {QStringLiteral("path"), QStringLiteral("X:/library/fixture.fbx")}};
        manager.admitViewerIntent(QStringLiteral("intent-old"), {{QStringLiteral("item_id"), QStringLiteral("1")},
            {QStringLiteral("extension"), QStringLiteral(".fbx")}, {QStringLiteral("library_root"), QStringLiteral("X:/library")},
            {QStringLiteral("background"), true}});
        manager.preview(QStringLiteral("old-viewer"), builtin, QStringLiteral("viewer"), item, QStringLiteral("X:/library"), true, QStringLiteral("intent-old"));
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.last().at(1).toString() + QLatin1Char(':') + errors.last().at(2).toString()));
        manager.admitViewerIntent(QStringLiteral("intent-new"));
        manager.failViewerIntent(QStringLiteral("intent-new"), QStringLiteral("preview_source_missing"), QStringLiteral("Source missing"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(builtinDirectory + QStringLiteral("/intent-error-observed")), 5000);
        manager.cancelViewerPreview(QStringLiteral("old-viewer"));
        QCOMPARE(pluginById(manager, builtin).value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        manager.preview(QStringLiteral("new-viewer"), builtin, QStringLiteral("viewer"), item, QStringLiteral("X:/library"), true, QStringLiteral("intent-new"));
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.last().at(1).toString() + QLatin1Char(':') + errors.last().at(2).toString()));
        QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 5000);
        QVERIFY(completed.isEmpty());
        manager.cancelViewerPreview(QStringLiteral("old-viewer"));
        QCOMPARE(pluginById(manager, builtin).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
        write(builtinDirectory + QStringLiteral("/release-viewer"), "release");
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QCOMPARE(completed.first().first().toString(), QStringLiteral("new-viewer"));
        manager.preview(QStringLiteral("thumbnail"), builtin, QStringLiteral("thumbnail"), item, QStringLiteral("X:/library"), true);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.last().at(1).toString() + QLatin1Char(':') + errors.last().at(2).toString()));
        manager.cancelViewerPreview(QStringLiteral("thumbnail"));
        QCOMPARE(pluginById(manager, builtin).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
        write(builtinDirectory + QStringLiteral("/release-thumbnail"), "release");
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 5000);
        QCOMPARE(completed.last().first().toString(), QStringLiteral("thumbnail"));
        manager.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(pluginById(manager, builtin).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void builtinDescriptorIsAlwaysEnabledAndCannotBeInstalledOrDisabled() {
        const auto directory = QDir(installRoot_).filePath(QStringLiteral("automation/animation_browser"));
        QVERIFY(QDir().mkpath(directory + QStringLiteral("/ui")));
        write(directory + QStringLiteral("/ui/index.html"), "<!doctype html><body>builtin fixture</body>");
        write(directory + QStringLiteral("/manifest.json"), R"JSON({"id":"billfish.animation-browser","name":"Animation Browser","version":"1","main":{"url":"ui/index.html"},"preview":{"fbx,glb":{"viewer":{"path":"ui/index.html"},"thumbnail":{"path":"ui/index.html"}}},"billfish":{"permissions":[]}})JSON");
        PluginManager manager(installRoot_);
        const auto builtin = pluginById(manager, QStringLiteral("billfish.animation-browser"));
        QVERIFY(builtin.value(QStringLiteral("builtin")).toBool());
        QVERIFY(builtin.value(QStringLiteral("read_only")).toBool());
        QVERIFY(builtin.value(QStringLiteral("enabled")).toBool());
        QVERIFY(builtin.value(QStringLiteral("commands")).toArray().isEmpty());
        QVERIFY(!builtin.value(QStringLiteral("can_open_window")).toBool());
        QCOMPARE(manager.setEnabled(QStringLiteral("billfish.animation-browser"), false).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("builtin_read_only"));
        QCOMPARE(installResult(manager, directory).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("builtin_read_only"));
    }

    void mixedFormatManifestMapsKindsAndDisabledPluginsCannotOwnFormats() {
        const auto manifestPath = QDir(source_).filePath(QStringLiteral("manifest.json"));
        QFile file(manifestPath); QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        write(QDir(source_).filePath(QStringLiteral("viewer.html")), "<!doctype html><body>format viewer</body>");
        write(QDir(source_).filePath(QStringLiteral("thumbnail.js")), R"JS(
module.exports = async ({dest}) => {
  require('node:fs').writeFileSync(dest, Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAIAAAADCAIAAAA2iEnWAAAAEklEQVR4nGPQSNmikbKFAYUCAEuNB4GmiAhBAAAAAElFTkSuQmCC','base64'), {flag:'wx'});
  return {width:99,height:99};
};
)JS");
        manifest.insert(QStringLiteral("preview"), QJsonObject{
            {QStringLiteral("obj,ply"), QJsonObject{
                {QStringLiteral("viewer"), QJsonObject{{QStringLiteral("path"), QStringLiteral("viewer.html")}}},
                {QStringLiteral("thumbnail"), QJsonObject{{QStringLiteral("path"), QStringLiteral("thumbnail.js")}}}}},
            {QStringLiteral("hdr,exr"), QJsonObject{{QStringLiteral("viewer"), QJsonObject{{QStringLiteral("path"), QStringLiteral("viewer.html")}}}}}});
        write(manifestPath, QJsonDocument(manifest).toJson());
        PluginManager manager(installRoot_); install(manager);
        const auto installed = plugin(manager);
        QVERIFY(installed.value(QStringLiteral("compatible")).toBool());
        QVERIFY(installed.value(QStringLiteral("preview_extensions")).toObject().value(QStringLiteral("obj")).toObject().contains(QStringLiteral("thumbnail")));
        QVERIFY(!installed.value(QStringLiteral("preview_extensions")).toObject().value(QStringLiteral("hdr")).toObject().contains(QStringLiteral("thumbnail")));
        QVERIFY(manager.previewPlugin(QStringLiteral("asset.OBJ"), QStringLiteral("viewer")).isEmpty());
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY(waitReady(manager));
        QCOMPARE(manager.previewPlugin(QStringLiteral("asset.OBJ"), QStringLiteral("viewer")), QStringLiteral("test.lifecycle"));
        QCOMPARE(manager.previewPlugin(QStringLiteral("asset.exr"), QStringLiteral("viewer")), QStringLiteral("test.lifecycle"));
        QVERIFY(manager.previewPlugin(QStringLiteral("asset.exr"), QStringLiteral("thumbnail")).isEmpty());
        QVERIFY(manager.previewPlugin(QStringLiteral("asset.png"), QStringLiteral("viewer")).isEmpty());
        const auto source = QDir(temporary_->path()).filePath(QStringLiteral("envelope-fixture.obj"));
        write(source, "Placeholder source: this test exercises the host envelope and PNG hook, not a 3D loader.");
        QSignalSpy completed(&manager, &PluginManager::finished);
        QSignalSpy errors(&manager, &PluginManager::failed);
        manager.preview(QStringLiteral("format-envelope"), QStringLiteral("test.lifecycle"), QStringLiteral("thumbnail"),
            {{QStringLiteral("id"), QStringLiteral("1")}, {QStringLiteral("path"), source}, {QStringLiteral("name"), QStringLiteral("fixture.obj")}},
            QStringLiteral("X:/library"), true);
        QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty() || !errors.isEmpty(), 10000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(2).toString()));
        const auto result = completed.first().at(1).toJsonObject();
        QCOMPARE(result.value(QStringLiteral("completion")).toString(), QStringLiteral("plugin_thumbnail_rendered"));
        QCOMPARE(result.value(QStringLiteral("width")).toInt(), 2);
        QCOMPARE(result.value(QStringLiteral("height")).toInt(), 3);
        QVERIFY(result.value(QStringLiteral("bytes")).toInt() > 33);
        const auto dest = result.value(QStringLiteral("dest")).toString();
        QVERIFY(QFileInfo::exists(dest));
        QVERIFY(QDir::fromNativeSeparators(dest).startsWith(QDir::fromNativeSeparators(manager.previewStagingRoot(QStringLiteral("test.lifecycle"))) + QLatin1Char('/')));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QVERIFY(manager.previewPlugin(QStringLiteral("asset.obj"), QStringLiteral("viewer")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void viewerCoverPrivatePipeBindsSessionAndPropagatesNativeFailureAndCompletion() {
        const auto manifestPath = QDir(source_).filePath(QStringLiteral("manifest.json"));
        QFile file(manifestPath); QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        manifest.insert(QStringLiteral("preview"), QJsonObject{{QStringLiteral("obj"), QJsonObject{
            {QStringLiteral("viewer"), QJsonObject{{QStringLiteral("path"), QStringLiteral("viewer.html")}}}}}});
        write(manifestPath, QJsonDocument(manifest).toJson());
        // This is a transport fixture, not a claim about geometry rendering or
        // captured animation pixels. The real viewer/pose QA is separate.
        write(QDir(source_).filePath(QStringLiteral("viewer.html")), R"HTML(<!doctype html><body><script>
const fs=require('node:fs'),path=require('node:path'),{ipcRenderer}=require('electron');
window.addEventListener('load',()=>setTimeout(async()=>{
  const png=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAIAAAADCAIAAAA2iEnWAAAAEklEQVR4nGPQSNmikbKFAYUCAEuNB4GmiAhBAAAAAElFTkSuQmCC','base64');
  const pose={clip_index:0,clip_name:'fixture',time:1.25,fps:24};
  try{
    const first=await ipcRenderer.invoke('billfish:preview-cover',{png,pose,item:{id:'999'},source:{sha256:'forged'},dest:'X:/forged.png'});
    fs.writeFileSync(path.join(eagle.plugin.path,'cover-first.json'),JSON.stringify(first));
    const second=await ipcRenderer.invoke('billfish:preview-cover',{png,pose});
    fs.writeFileSync(path.join(eagle.plugin.path,'cover-second.json'),JSON.stringify(second));
  }catch(error){fs.writeFileSync(path.join(eagle.plugin.path,'cover-transport-error.txt'),String(error.stack||error));}
},100));
</script></body>)HTML");
        const auto source = QDir(temporary_->path()).filePath(QStringLiteral("frame-source.obj"));
        write(source, "Private viewer cover transport fixture; not a geometry parser input.");
        PluginManager manager(installRoot_); install(manager);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true); QVERIFY(waitReady(manager));
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        manager.preview(QStringLiteral("frame-viewer"), QStringLiteral("test.lifecycle"), QStringLiteral("viewer"),
            {{QStringLiteral("id"), QStringLiteral("1")}, {QStringLiteral("path"), source}}, QStringLiteral("X:/library"), true);
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 10000);
        QCOMPARE(calls.first().at(1).toString(), QStringLiteral("plugins.set_cover"));
        QCOMPARE(calls.first().at(3).toString(), QStringLiteral("X:/library"));
        const auto capture = calls.first().at(2).toJsonObject();
        QCOMPARE(capture.value(QStringLiteral("item_id")).toString(), QStringLiteral("1"));
        QCOMPARE(capture.value(QStringLiteral("plugin_id")).toString(), QStringLiteral("test.lifecycle"));
        QCOMPARE(QDir::fromNativeSeparators(capture.value(QStringLiteral("source_path")).toString()), QDir::fromNativeSeparators(source));
        QCOMPARE(capture.value(QStringLiteral("source_fingerprint")).toObject().value(QStringLiteral("sha256")).toString().size(), 64);
        QVERIFY(capture.value(QStringLiteral("source_fingerprint")).toObject().value(QStringLiteral("mtime_ns")).isString());
        const auto dest = capture.value(QStringLiteral("dest")).toString();
        QVERIFY(QFileInfo::exists(dest));
        QVERIFY(QDir::fromNativeSeparators(dest).startsWith(QDir::fromNativeSeparators(manager.previewStagingRoot(QStringLiteral("test.lifecycle"))) + QLatin1Char('/')));
        manager.failHostCall(calls.first().at(0).toString(), QStringLiteral("native_cover_failed"), QStringLiteral("Native cover outcome is unknown."),
            {{QStringLiteral("active_status"), QStringLiteral("unknown")}});
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 2, 10000);
        const auto directory = plugin(manager).value(QStringLiteral("directory")).toString();
        QFile first(QDir(directory).filePath(QStringLiteral("cover-first.json")));
        QVERIFY(first.open(QIODevice::ReadOnly));
        const auto failure = QJsonDocument::fromJson(first.readAll()).object();
        QVERIFY(!failure.contains(QStringLiteral("data")));
        QCOMPARE(failure.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("native_cover_failed"));
        QCOMPARE(failure.value(QStringLiteral("error")).toObject().value(QStringLiteral("details")).toObject().value(QStringLiteral("active_status")).toString(), QStringLiteral("unknown"));
        const auto pose = capture.value(QStringLiteral("pose")).toObject();
        manager.completeHostCall(calls.last().at(0).toString(), {
            {QStringLiteral("completion"), QStringLiteral("native_custom_cover_verified")}, {QStringLiteral("item_id"), QStringLiteral("1")},
            {QStringLiteral("source_unchanged"), true}, {QStringLiteral("metadata_unchanged"), true},
            {QStringLiteral("cover_path"), QStringLiteral("X:/library/.bf/.preview/01/1.cover.png")}, {QStringLiteral("pose"), pose},
            {QStringLiteral("image"), QJsonObject{{QStringLiteral("width"), 2}, {QStringLiteral("height"), 3}, {QStringLiteral("codec"), QStringLiteral("png")}}}});
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(directory).filePath(QStringLiteral("cover-second.json"))), 5000);
        QFile second(QDir(directory).filePath(QStringLiteral("cover-second.json"))); QVERIFY(second.open(QIODevice::ReadOnly));
        const auto success = QJsonDocument::fromJson(second.readAll()).object();
        QVERIFY(!success.contains(QStringLiteral("error")));
        QCOMPARE(success.value(QStringLiteral("data")).toObject().value(QStringLiteral("completion")).toString(), QStringLiteral("native_custom_cover_verified"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void unsafeAndDuplicateFormatMappingsAreRejectedWithoutReplacingInstalledPlugin() {
        PluginManager manager(installRoot_); install(manager);
        const auto manifestPath = QDir(source_).filePath(QStringLiteral("manifest.json"));
        QFile file(manifestPath); QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        manifest.insert(QStringLiteral("preview"), QJsonObject{{QStringLiteral("obj"), QJsonObject{
            {QStringLiteral("viewer"), QJsonObject{{QStringLiteral("path"), QStringLiteral("../outside.html")}}}}}});
        write(manifestPath, QJsonDocument(manifest).toJson());
        QCOMPARE(installResult(manager, source_).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("invalid_plugin"));
        write(QDir(source_).filePath(QStringLiteral("viewer.html")), "<!doctype html><body>viewer</body>");
        const QJsonObject handler{{QStringLiteral("viewer"), QJsonObject{{QStringLiteral("path"), QStringLiteral("viewer.html")}}}};
        manifest.insert(QStringLiteral("preview"), QJsonObject{{QStringLiteral("obj,ply"), handler}, {QStringLiteral("obj"), handler}});
        write(manifestPath, QJsonDocument(manifest).toJson());
        QCOMPARE(installResult(manager, source_).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("invalid_plugin"));
        QVERIFY(plugin(manager).value(QStringLiteral("preview_extensions")).toObject().isEmpty());
    }

    void importFailurePreservesPersistedItemDetailsForPluginRecovery() {
        const auto manifestPath = QDir(source_).filePath(QStringLiteral("manifest.json"));
        QFile file(manifestPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto extension = manifest.value(QStringLiteral("billfish")).toObject();
        extension.insert(QStringLiteral("permissions"), QJsonArray{QStringLiteral("items.import_path")});
        manifest.insert(QStringLiteral("billfish"), extension);
        write(manifestPath, QJsonDocument(manifest).toJson());
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), R"JS(
billfish.registerCommand('host', async () => {
    try { await billfish.call('items.import_path',{source_path:'X:/synthetic.png'}); }
    catch (error) { return {code:error.code,details:error.details}; }
    throw new Error('Expected an import failure');
});
)JS");
        PluginManager manager(installRoot_);
        install(manager);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        QSignalSpy completed(&manager, &PluginManager::finished);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY(waitReady(manager));
        manager.run(QStringLiteral("import-recovery"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 5000);
        QCOMPARE(calls.first().at(1).toString(), QStringLiteral("items.import_path"));
        const QJsonObject details{{QStringLiteral("item_id"), QStringLiteral("persisted-123")},
                                  {QStringLiteral("native_import_persisted"), true}};
        manager.failHostCall(calls.first().at(0).toString(), QStringLiteral("import_metadata_failed"), QStringLiteral("Native item exists; metadata update failed."), details);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        const auto result = completed.first().at(1).toJsonObject();
        QCOMPARE(result.value(QStringLiteral("code")).toString(), QStringLiteral("import_metadata_failed"));
        QCOMPARE(result.value(QStringLiteral("details")).toObject(), details);
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void eagleDefaultImportPermissionsRespectExplicitDeclarations() {
        const auto manifestPath = QDir(source_).filePath(QStringLiteral("manifest.json"));
        QFile file(manifestPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto explicitManifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto standardManifest = explicitManifest;
        standardManifest.remove(QStringLiteral("billfish"));
        write(manifestPath, QJsonDocument(standardManifest).toJson());
        PluginManager manager(installRoot_);
        const auto standard = installResult(manager, source_);
        QVERIFY2(!standard.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(standard).toJson())));
        const auto defaults = standard.value(QStringLiteral("permissions")).toArray();
        QVERIFY(defaults.contains(QStringLiteral("items.import_path")));
        QVERIFY(defaults.contains(QStringLiteral("items.import_url")));
        QVERIFY(!defaults.contains(QStringLiteral("video.capture")));
        auto extension = explicitManifest.value(QStringLiteral("billfish")).toObject();
        extension.insert(QStringLiteral("permissions"), QJsonArray{QStringLiteral("items.get"), QStringLiteral("items.import_path")});
        explicitManifest.insert(QStringLiteral("billfish"), extension);
        write(manifestPath, QJsonDocument(explicitManifest).toJson());
        const auto explicitPlugin = installResult(manager, source_);
        QVERIFY2(!explicitPlugin.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(explicitPlugin).toJson())));
        const auto permissions = explicitPlugin.value(QStringLiteral("permissions")).toArray();
        QCOMPARE(permissions, QJsonArray({QStringLiteral("items.get"), QStringLiteral("items.import_path")}));
        QVERIFY(!permissions.contains(QStringLiteral("items.import_url")));
    }

    void updatedEnabledPreferenceWaitsForExplicitActivationAfterRestart() {
        {
            PluginManager manager(installRoot_);
            const auto installed = installResult(manager, source_);
            QVERIFY2(!installed.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(installed).toJson())));
            manager.setEnabled(QStringLiteral("test.lifecycle"), true);
            QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("waiting-context"));
            const auto result = installResult(manager, source_);
            QVERIFY(result.value(QStringLiteral("enabled")).toBool());
            QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("updated"));
        }
        PluginManager manager(installRoot_);
        manager.setLibraryRoot(QStringLiteral("X:/library"));
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("updated"));
        const auto directory = plugin(manager).value(QStringLiteral("directory")).toString();
        QVERIFY(!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("created.txt"))));
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY(waitReady(manager));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void replacementRemovesStaleFilesAndInvalidUpdatePreservesPrevious() {
        PluginManager manager(installRoot_);
        install(manager);
        const auto directory = plugin(manager).value(QStringLiteral("directory")).toString();
        write(QDir(directory).filePath(QStringLiteral("stale.txt")), "old");
        const auto updated = installResult(manager, source_);
        QCOMPARE(updated.value(QStringLiteral("completion")).toString(), QStringLiteral("plugin_updated"));
        QVERIFY(!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("stale.txt"))));
        write(QDir(source_).filePath(QStringLiteral("manifest.json")), "invalid json");
        QCOMPARE(installResult(manager, source_).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("invalid_plugin"));
        QFile original(QDir(directory).filePath(QStringLiteral("manifest.json")));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(original.readAll()).isObject());
    }

    void stateWriteFailureRestoresPreviousPluginDirectory() {
        PluginManager manager(installRoot_);
        install(manager);
        const auto directory = plugin(manager).value(QStringLiteral("directory")).toString();
        write(QDir(directory).filePath(QStringLiteral("preserved.txt")), "previous version");
        const auto state = QDir(QFileInfo(manager.pluginsDirectory()).absolutePath()).filePath(QStringLiteral("plugins-state.json"));
        QVERIFY(QFile::remove(state));
        QVERIFY(QDir().mkpath(state));
        const auto result = installResult(manager, source_);
        QCOMPARE(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("state_write_failed"));
        QFile preserved(QDir(directory).filePath(QStringLiteral("preserved.txt")));
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("previous version"));
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"));
    }

    void dependencyPackageIsInstalledWithoutCommandsOrExecution() {
        write(QDir(source_).filePath(QStringLiteral("manifest.json")), R"JSON({"id":"test-win-x64","name":"Dependency","version":"1","platform":"win","arch":"x64","preview":{}})JSON");
        PluginManager manager(installRoot_);
        const auto result = installResult(manager, source_);
        QVERIFY2(!result.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(result).toJson())));
        QCOMPARE(result.value(QStringLiteral("mode")).toString(), QStringLiteral("dependency"));
        QVERIFY(result.value(QStringLiteral("manifest_commands")).toArray().isEmpty());
        QVERIFY(!result.value(QStringLiteral("enabled")).toBool());
        QVERIFY(!result.value(QStringLiteral("can_open_window")).toBool());
    }

    void dependencyUpdateRequiresStoppedConsumersAndExplicitActivation() {
        const auto dependencySource = QDir(temporary_->path()).filePath(QStringLiteral("dependency-source"));
        QVERIFY(QDir().mkpath(dependencySource));
        write(QDir(dependencySource).filePath(QStringLiteral("manifest.json")), R"JSON({"id":"testdep-win-x64","name":"Dependency","version":"1","preview":{}})JSON");
        QFile file(QDir(source_).filePath(QStringLiteral("manifest.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        manifest.insert(QStringLiteral("dependencies"), QJsonArray{QStringLiteral("testdep")});
        write(file.fileName(), QJsonDocument(manifest).toJson());
        PluginManager manager(installRoot_);
        const auto installed = installResult(manager, dependencySource);
        QVERIFY2(!installed.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(installed).toJson())));
        install(manager);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QTRY_COMPARE_WITH_TIMEOUT(pluginById(manager, QStringLiteral("test.lifecycle")).value(QStringLiteral("status")).toString(), QStringLiteral("ready"), 10000);
        QCOMPARE(installResult(manager, dependencySource).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("plugin_running"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(pluginById(manager, QStringLiteral("test.lifecycle")).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
        const auto updated = installResult(manager, dependencySource);
        QVERIFY2(!updated.value(QStringLiteral("error")).isObject(), qPrintable(QString::fromUtf8(QJsonDocument(updated).toJson())));
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY(pluginById(manager, QStringLiteral("test.lifecycle")).value(QStringLiteral("error")).toString().startsWith(QStringLiteral("dependency_activation_required:")));
        manager.setEnabled(QStringLiteral("testdep-win-x64"), true);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QTRY_COMPARE_WITH_TIMEOUT(pluginById(manager, QStringLiteral("test.lifecycle")).value(QStringLiteral("status")).toString(), QStringLiteral("ready"), 10000);
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(pluginById(manager, QStringLiteral("test.lifecycle")).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void runningPluginCannotBeReplaced() {
        PluginManager manager(installRoot_);
        install(manager);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY(waitReady(manager));
        const auto result = installResult(manager, source_);
        QCOMPARE(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("plugin_running"));
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void initTestCase() {
        const auto runner = QFINDTESTDATA("../../../automation/plugin_runtime/main.cjs");
        QVERIFY(!runner.isEmpty());
        QDir root(QFileInfo(runner).absolutePath()); root.cdUp(); root.cdUp(); runtimeRoot_ = root.absolutePath();
    }

    void init() {
        previousAppData_ = qgetenv("APPDATA"); previousElectron_ = qgetenv("BILLFISH_ELECTRON");
        temporary_ = std::make_unique<QTemporaryDir>();
        QVERIFY(temporary_->isValid());
        installRoot_ = QDir(temporary_->path()).filePath(QStringLiteral("installation"));
        const auto runtime = QDir(installRoot_).filePath(QStringLiteral("automation/plugin_runtime"));
        QVERIFY(QDir().mkpath(runtime + QStringLiteral("/vendor")));
        // The installed runtime is a read-only source. Every mutable manifest,
        // built-in fixture and plugin directory belongs to this temporary root.
        const QDir sourceRuntime(QDir(runtimeRoot_).filePath(QStringLiteral("automation/plugin_runtime")));
        const QStringList fixtureRuntime = {QStringLiteral("main.cjs"), QStringLiteral("schema.cjs"),
            QStringLiteral("preview-runtime.cjs"), QStringLiteral("window-owner.cjs"), QStringLiteral("preview-cover.cjs"),
            QStringLiteral("preload.cjs"), QStringLiteral("eagle-sdk.cjs"), QStringLiteral("extra-modules.cjs"),
            QStringLiteral("context-menu.cjs"), QStringLiteral("preview-preload.cjs"), QStringLiteral("preview-host.html"),
            QStringLiteral("animation-controls.cjs"), QStringLiteral("animation-controller.cjs"), QStringLiteral("animation-controls.css"),
            QStringLiteral("package.json"), QStringLiteral("package_installer.py")};
        for (const auto& name : fixtureRuntime)
            QVERIFY(QFile::copy(sourceRuntime.filePath(name), QDir(runtime).filePath(name)));
        const QDir vendor(sourceRuntime.filePath(QStringLiteral("vendor")));
        for (const auto& name : vendor.entryList(QDir::Files))
            QVERIFY(QFile::copy(vendor.filePath(name), QDir(runtime).filePath(QStringLiteral("vendor/") + name)));
        qputenv("APPDATA", temporary_->path().toUtf8());
        auto electron = previousElectron_.isEmpty() ? QDir(runtimeRoot_).filePath(QStringLiteral("tools/electron/electron.exe")) : QString::fromUtf8(previousElectron_);
        if (!QFileInfo::exists(electron)) electron = QDir(runtimeRoot_).filePath(QStringLiteral("automation/plugin_runtime/node_modules/electron/dist/electron.exe"));
        QVERIFY2(QFileInfo::exists(electron), "Install the pinned Electron runtime before running plugin tests.");
        qputenv("BILLFISH_ELECTRON", electron.toUtf8());
        source_ = QDir(temporary_->path()).filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(source_));
        write(QDir(source_).filePath(QStringLiteral("manifest.json")), R"JSON({
          "id":"test.lifecycle","name":"Lifecycle","version":"1","main":{"url":"index.html","width":320,"height":200},
          "billfish":{"permissions":["items.get","items.selected"],
          "commands":[{"id":"host","title":"Host call","input_schema":{"type":"object"}},
                      {"id":"denied","title":"Denied","input_schema":{"type":"object"}},
                      {"id":"fail","title":"Fail","input_schema":{"type":"object"}},
                      {"id":"selection","title":"Selection","input_schema":{"type":"object"}},
                      {"id":"checked","title":"Checked","input_schema":{
                         "type":"object","properties":{"name":{"type":"string","minLength":2,"maxLength":5},
                         "count":{"type":"integer","minimum":1,"maximum":3},
                         "mode":{"type":"string","enum":["a","b"]},
                         "ids":{"type":"array","minItems":1,"maxItems":2,"items":{"type":"integer"}}},
                         "required":["name","count","mode","ids"],"additionalProperties":false}}]}
        })JSON");
        write(QDir(source_).filePath(QStringLiteral("index.html")), "<!doctype html><html><body><script src='plugin.js'></script></body></html>");
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), R"JS(
const fs = require('node:fs');
const path = require('node:path');
const marker = name => path.join(eagle.plugin.path,name);
eagle.onPluginCreate(() => { fs.writeFileSync(marker('created.txt'),'started'); console.log('ordinary plugin print'); });
for (const command of ['checked','host','denied','fail','selection']) billfish.registerCommand(command, async args => {
    fs.writeFileSync(marker('run-entered.txt'),command);
    if (command === 'checked') return {validated:args};
    if (command === 'fail') throw new Error('expected failure');
    if (command === 'selection') return billfish.call('items.selected',{});
    if (command === 'denied') return billfish.call('items.update',{updates:[]});
    billfish.progress(0,1,'before call');
    const data = await billfish.call('items.get',{item_id:'123'});
    billfish.progress(1,1,'after call');
    return data;
});
eagle.onPluginBeforeExit(() => { fs.writeFileSync(marker('stopped.txt'),'stopped'); });
)JS");
    }

    void cleanup() {
        qputenv("APPDATA", previousAppData_); qputenv("BILLFISH_ELECTRON", previousElectron_);
        temporary_.reset();
    }

    void installDoesNotExecuteAndEnableRunsLifecycle() {
        PluginManager manager(installRoot_);
        install(manager);
        const auto path = plugin(manager).value(QStringLiteral("directory")).toString();
        QVERIFY(!QFileInfo::exists(QDir(path).filePath(QStringLiteral("created.txt"))));
        QVERIFY(!plugin(manager).value(QStringLiteral("enabled")).toBool());
        QCOMPARE(installResult(manager, source_).value(QStringLiteral("completion")).toString(), QStringLiteral("plugin_updated"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        QVERIFY(QFileInfo::exists(QDir(path).filePath(QStringLiteral("created.txt"))));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
        QVERIFY(QFileInfo::exists(QDir(path).filePath(QStringLiteral("stopped.txt"))));
    }

    void hostCallCorrelationAndErrorsRecover() {
        PluginManager manager(installRoot_);
        install(manager);
        QSignalSpy completed(&manager, &PluginManager::finished);
        QSignalSpy errors(&manager, &PluginManager::failed);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        QSignalSpy progress(&manager, &PluginManager::progress);
        manager.run(QStringLiteral("early"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("X:/library"));
        QCOMPARE(errors.size(), 1);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.run(QStringLiteral("success"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 5000);
        QCOMPARE(calls.at(0).at(1).toString(), QStringLiteral("items.get"));
        QCOMPARE(calls.at(0).at(3).toString(), QStringLiteral("X:/library"));
        manager.completeHostCall(calls.at(0).at(0).toString(), {{QStringLiteral("name"), QStringLiteral("asset.png")}});
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("success"));
        QCOMPARE(completed.at(0).at(1).toJsonObject().value(QStringLiteral("name")).toString(), QStringLiteral("asset.png"));
        QCOMPARE(progress.size(), 3); // Initial busy indication plus two plugin updates.
        manager.run(QStringLiteral("denied"), QStringLiteral("test.lifecycle"), QStringLiteral("denied"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 5000);
        QCOMPARE(errors.last().at(1).toString(), QStringLiteral("permission_denied"));
        QCOMPARE(calls.size(), 1); // Rejected before dispatching to native host.
        manager.run(QStringLiteral("failure"), QStringLiteral("test.lifecycle"), QStringLiteral("fail"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 3, 5000);
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void disableDuringHostCallStopsAndIgnoresLateResult() {
        PluginManager manager(installRoot_);
        install(manager);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        QSignalSpy completed(&manager, &PluginManager::finished);
        QSignalSpy errors(&manager, &PluginManager::failed);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.run(QStringLiteral("active"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 5000);
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.at(0).at(1).toString(), QStringLiteral("plugin_disabled"));
        QVERIFY(plugin(manager).value(QStringLiteral("commands")).toArray().isEmpty());
        manager.completeHostCall(calls.at(0).at(0).toString(), {{QStringLiteral("late"), true}});
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
        QCOMPARE(completed.size(), 0);
    }

    void enabledIntentSurvivesHostRestart() {
        {
            PluginManager manager(installRoot_);
            install(manager);
            manager.setEnabled(QStringLiteral("test.lifecycle"), true);
            QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
            manager.shutdown();
            QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
        }
        PluginManager manager(installRoot_);
        manager.setLibraryRoot(QStringLiteral("X:/library"));
        QVERIFY(plugin(manager).value(QStringLiteral("enabled")).toBool());
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void startupFailureIsVisible() {
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), "eagle.onPluginCreate(() => { throw new Error('startup broken'); });\n");
        PluginManager manager(installRoot_);
        install(manager);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("failed"), 10000);
        QVERIFY(plugin(manager).value(QStringLiteral("enabled")).toBool());
        QVERIFY(plugin(manager).value(QStringLiteral("error")).toString().contains(QStringLiteral("startup broken")));
        QVERIFY(plugin(manager).value(QStringLiteral("commands")).toArray().isEmpty());
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"));
        QVERIFY(plugin(manager).value(QStringLiteral("error")).toString().isEmpty());
    }

    void invalidArgumentsNeverEnterPluginCode() {
        PluginManager manager(installRoot_);
        install(manager);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        const auto marker = QDir(plugin(manager).value(QStringLiteral("directory")).toString()).filePath(QStringLiteral("run-entered.txt"));
        QSignalSpy errors(&manager, &PluginManager::failed);
        QSignalSpy completed(&manager, &PluginManager::finished);
        manager.run(QStringLiteral("invalid"), QStringLiteral("test.lifecycle"), QStringLiteral("checked"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        QCOMPARE(errors.at(0).at(1).toString(), QStringLiteral("invalid_arguments"));
        QVERIFY(!QFileInfo::exists(marker));
        manager.run(QStringLiteral("valid"), QStringLiteral("test.lifecycle"), QStringLiteral("checked"),
                    {{QStringLiteral("name"), QStringLiteral("good")}, {QStringLiteral("count"), 2},
                     {QStringLiteral("mode"), QStringLiteral("a")}, {QStringLiteral("ids"), QJsonArray{1}}}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QVERIFY(QFileInfo::exists(marker));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void unsupportedSchemaIsRejectedAtInstallation() {
        QFile file(QDir(source_).filePath(QStringLiteral("manifest.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto extension = manifest.value(QStringLiteral("billfish")).toObject();
        auto commands = extension.value(QStringLiteral("commands")).toArray();
        auto command = commands.at(0).toObject();
        auto schema = command.value(QStringLiteral("input_schema")).toObject();
        schema.insert(QStringLiteral("oneOf"), QJsonArray{});
        command.insert(QStringLiteral("input_schema"), schema); commands.replace(0, command);
        extension.insert(QStringLiteral("commands"), commands);
        manifest.insert(QStringLiteral("billfish"), extension);
        write(file.fileName(), QJsonDocument(manifest).toJson());
        PluginManager manager(installRoot_);
        const auto result = installResult(manager, source_);
        QCOMPARE(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("invalid_plugin"));
        QVERIFY(pluginById(manager, QStringLiteral("test.lifecycle")).isEmpty());
    }

    void hostCallsRemainAvailableAfterCommandReturns() {
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), R"JS(
const fs = require('node:fs'), path = require('node:path');
billfish.registerCommand('host', async () => {
    setTimeout(async () => {
        const data = await billfish.call('items.get',{item_id:'123'});
        fs.writeFileSync(path.join(eagle.plugin.path,'late-call.txt'),data.name);
    },100);
    return {scheduled:true};
});
)JS");
        PluginManager manager(installRoot_); install(manager);
        QSignalSpy completed(&manager, &PluginManager::finished);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.run(QStringLiteral("schedule"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 5000);
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        manager.completeHostCall(calls.at(0).at(0).toString(), {{QStringLiteral("name"), QStringLiteral("late.png")}});
        const auto marker = QDir(plugin(manager).value(QStringLiteral("directory")).toString()).filePath(QStringLiteral("late-call.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5000);
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void onCreateCanCallHostBeforeReady() {
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), "eagle.onPluginCreate(async () => { await billfish.call('items.get',{item_id:'123'}); });\n");
        PluginManager manager(installRoot_); install(manager);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 10000);
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("starting"));
        QCOMPARE(calls.at(0).at(3).toString(), QStringLiteral("X:/library"));
        manager.completeHostCall(calls.at(0).at(0).toString(), {});
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void libraryChangeDiscardsOldSessionAndLateHostResult() {
        PluginManager manager(installRoot_); install(manager);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        QSignalSpy completed(&manager, &PluginManager::finished);
        QSignalSpy errors(&manager, &PluginManager::failed);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.run(QStringLiteral("old"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 5000);
        manager.setLibraryRoot(QStringLiteral("Y:/next-library"));
        QCOMPARE(errors.size(), 1);
        manager.completeHostCall(calls.at(0).at(0).toString(), {{QStringLiteral("name"), QStringLiteral("old.png")}});
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        QCOMPARE(completed.size(), 0);
        manager.run(QStringLiteral("new"), QStringLiteral("test.lifecycle"), QStringLiteral("host"), {}, QStringLiteral("Y:/next-library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 2, 5000);
        QCOMPARE(calls.at(1).at(3).toString(), QStringLiteral("Y:/next-library"));
        manager.completeHostCall(calls.at(1).at(0).toString(), {{QStringLiteral("name"), QStringLiteral("new.png")}});
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("new"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void legacyPythonDirectoryRemainsVisibleButCannotRun() {
        const auto legacy = QDir(temporary_->path()).filePath(QStringLiteral("Billfish/automation/plugins/legacy.python"));
        QVERIFY(QDir().mkpath(legacy));
        write(QDir(legacy).filePath(QStringLiteral("manifest.json")), "{\"apiVersion\":1,\"id\":\"legacy.python\",\"name\":\"Legacy\",\"version\":\"1\",\"entry\":\"plugin.py\",\"permissions\":[],\"commands\":[]}");
        write(QDir(legacy).filePath(QStringLiteral("plugin.py")), "raise RuntimeError('must never execute')\n");
        PluginManager manager(installRoot_);
        QCOMPARE(pluginById(manager, QStringLiteral("legacy.python")).value(QStringLiteral("status")).toString(), QStringLiteral("incompatible"));
        QVERIFY(!pluginById(manager, QStringLiteral("legacy.python")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(manager.setEnabled(QStringLiteral("legacy.python"), true).value(QStringLiteral("error")).isObject());
        QVERIFY(QFileInfo::exists(QDir(legacy).filePath(QStringLiteral("plugin.py"))));
    }

    void oversizedSelectionReturnsCatchableErrorAndKeepsSessionAlive() {
        PluginManager manager(installRoot_); install(manager);
        QSignalSpy calls(&manager, &PluginManager::hostCall);
        QSignalSpy errors(&manager, &PluginManager::failed);
        QSignalSpy completed(&manager, &PluginManager::finished);
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        manager.run(QStringLiteral("large"), QStringLiteral("test.lifecycle"), QStringLiteral("selection"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 1, 5000);
        manager.completeHostCall(calls.at(0).at(0).toString(), {{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("padding"), QString(4 * 1024 * 1024, QLatin1Char('x'))}}}}});
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        QCOMPARE(errors.at(0).at(1).toString(), QStringLiteral("selection_too_large"));
        QCOMPARE(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        manager.run(QStringLiteral("small"), QStringLiteral("test.lifecycle"), QStringLiteral("selection"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(calls.size(), 2, 5000);
        manager.completeHostCall(calls.at(1).at(0).toString(), {{QStringLiteral("items"), QJsonArray{}}, {QStringLiteral("total"), 0}});
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("small"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }

    void serviceWindowHidesWithoutEndingItsSession() {
        write(QDir(source_).filePath(QStringLiteral("manifest.json")), R"JSON({
          "id":"test.lifecycle","name":"Service","version":"1","main":{"url":"index.html","serviceMode":true,"width":320,"height":200}
        })JSON");
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), R"JS(
const fs = require('node:fs'), path = require('node:path');
const mark = name => fs.appendFileSync(path.join(eagle.plugin.path,name),'event\n');
eagle.onPluginCreate(() => mark('created.txt'));
eagle.onPluginRun(args => { if (args.args.close) setTimeout(() => window.close(),100); });
eagle.onPluginHide(() => mark('hidden.txt'));
eagle.onPluginBeforeExit(() => mark('stopped.txt'));
)JS");
        PluginManager manager(installRoot_); install(manager);
        QSignalSpy completed(&manager, &PluginManager::finished);
        const QDir directory(plugin(manager).value(QStringLiteral("directory")).toString());
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("created.txt"))));
        manager.run(QStringLiteral("open"), QStringLiteral("test.lifecycle"), QStringLiteral("open"), {{QStringLiteral("close"), true}}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(directory.filePath(QStringLiteral("hidden.txt"))), 5000);
        QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("stopped.txt"))));
        manager.run(QStringLiteral("service"), QStringLiteral("test.lifecycle"), QStringLiteral("run"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 5000);
        QCOMPARE(completed.at(1).at(1).toJsonObject().value(QStringLiteral("completion")).toString(), QStringLiteral("plugin_service_started"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("stopped.txt"))));
    }

    void standardWindowIsCreatedOnlyWhenRun() {
        write(QDir(source_).filePath(QStringLiteral("manifest.json")), R"JSON({
          "id":"test.lifecycle","name":"Window","version":"1","main":{"url":"index.html","width":320,"height":200}
        })JSON");
        write(QDir(source_).filePath(QStringLiteral("plugin.js")), R"JS(
const fs = require('node:fs'), path = require('node:path');
eagle.onPluginCreate(() => fs.writeFileSync(path.join(eagle.plugin.path,'created.txt'),'created'));
eagle.onPluginRun(() => {});
)JS");
        PluginManager manager(installRoot_); install(manager);
        QSignalSpy completed(&manager, &PluginManager::finished);
        const auto marker = QDir(plugin(manager).value(QStringLiteral("directory")).toString()).filePath(QStringLiteral("created.txt"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), true);
        QVERIFY2(waitReady(manager), qPrintable(QString::fromUtf8(QJsonDocument(plugin(manager)).toJson(QJsonDocument::Compact))));
        QVERIFY(!QFileInfo::exists(marker));
        manager.run(QStringLiteral("open"), QStringLiteral("test.lifecycle"), QStringLiteral("run"), {}, QStringLiteral("X:/library"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
        QVERIFY(QFileInfo::exists(marker));
        QCOMPARE(completed.at(0).at(1).toJsonObject().value(QStringLiteral("completion")).toString(), QStringLiteral("plugin_window_opened"));
        manager.setEnabled(QStringLiteral("test.lifecycle"), false);
        QTRY_COMPARE_WITH_TIMEOUT(plugin(manager).value(QStringLiteral("status")).toString(), QStringLiteral("disabled"), 5000);
    }
};

QTEST_GUILESS_MAIN(PluginManagerTest)
#include "test_plugin_manager.moc"
