#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

class QWidget;
class QLabel;
class QTimer;

namespace billfish::trim {

class VideoPlaybackShortcuts final : public QObject {
    Q_OBJECT
public:
    static void install(QWidget* previewControl);
    ~VideoPlaybackShortcuts() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void positionChanged(qlonglong positionMs);
    void mediaStatusChanged(int status);
    void nativePlaybackOperation();

private:
    explicit VideoPlaybackShortcuts(QObject* parent);
    void registerPlayer(QWidget* control);

    struct Player {
        QPointer<QWidget> video;
        QPointer<QWidget> control;
        QPointer<QWidget> page;
        QPointer<QObject> sdk;
    };
    const Player* focusedPlayer(QWidget* receiver) const;
    void refreshPosition(const Player& player);
    void finishKey(int key);
    void showOsd(const Player& player);
    void updateOsd();
    void hideOsd();

    QList<Player> players_;
    Player lastPlayer_;
    int lastKey_{0};
    int dispatchedCount_{0};
    qlonglong stepSeconds_{5};
    Player osdPlayer_;
    QPointer<QLabel> osd_;
    QTimer* osdHideTimer_{nullptr};
};

} // namespace billfish::trim
