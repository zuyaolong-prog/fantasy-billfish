#pragma once

#include <QObject>
#include <QPointer>
#include <QList>

class QWidget;

namespace billfish::trim {

class ImmersionPlayback final : public QObject {
    Q_OBJECT
public:
    static void install(QWidget* previewControl);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void mediaStatusChanged(int status);

private:
    explicit ImmersionPlayback(QObject* parent);
    void registerPlayer(QWidget* control);
    void capture(QWidget* trigger, bool entering);
    void restore(quint64 generation);

    struct Player {
        QPointer<QWidget> video;
        QPointer<QWidget> control;
        QPointer<QWidget> page;
        QPointer<QObject> sdk;
        bool immersion;
    };
    QList<Player> players_;
    struct Transfer {
        QPointer<QObject> sdk;
        QPointer<QWidget> video;
        QPointer<QWidget> control;
        qlonglong positionMs{0};
        qlonglong durationMs{0};
        bool playing{false};
        bool loading{false};
        QPointer<QWidget> page;
        bool immersion{false};
    } pending_;
    quint64 generation_{0};
};

} // namespace billfish::trim
