#pragma once

#include <QKeyEvent>
#include "keyboardroutingpolicy.h"
#include "uhidkeyboard.h"

class KeyboardRouting : public KeyboardRoutingPolicy {
public:
    Decision dispatch(const QKeyEvent &event, Route preferred) {
        const EventType type = event.type() == QEvent::KeyPress ? Press
            : event.type() == QEvent::KeyRelease ? Release : Other;
        return KeyboardRoutingPolicy::dispatch(identity(event), type,
                    event.isAutoRepeat(), event.key(), preferred);
    }
private:
    static quint64 identity(const QKeyEvent &event) {
        // Native positions distinguish left/right modifiers and remain stable
        // when a logical layout or the Shift/Tab representation changes.
        if (event.nativeScanCode()) { return (quint64(1) << 63) | event.nativeScanCode(); }
        const quint8 usage = UhidKeyboard::usageForEvent(event);
        if (usage) { return (quint64(1) << 62) | usage; }
        return quint32(event.key());
    }
};
