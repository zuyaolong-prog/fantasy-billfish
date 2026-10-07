#include <QObject>

class StockPreviewFixture final : public QObject {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.billfish.video-trim.stock-preview-fixture/1.0")
};

#include "stock_preview_fixture.moc"
