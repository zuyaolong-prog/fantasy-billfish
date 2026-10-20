#pragma once

#include <QThread>

namespace billfish::automation {
// Marks only events deliberately routed by the application control bridge.
// UI entry points can avoid system activation and grabs for this scope.
inline thread_local int appInputDepth = 0;
inline thread_local int appInputLoopLevel = -1;
class AppInputScope final {
public:
    explicit AppInputScope(bool enabled = true)
        : enabled_(enabled), previousLoopLevel_(appInputLoopLevel) {
        if (enabled_) { ++appInputDepth; appInputLoopLevel = QThread::currentThread()->loopLevel(); }
    }
    ~AppInputScope() { if (enabled_) { --appInputDepth; appInputLoopLevel = previousLoopLevel_; } }
    AppInputScope(const AppInputScope&) = delete;
    AppInputScope& operator=(const AppInputScope&) = delete;
    static bool active() { return appInputDepth > 0 && appInputLoopLevel == QThread::currentThread()->loopLevel(); }
private:
    bool enabled_;
    int previousLoopLevel_;
};
}
