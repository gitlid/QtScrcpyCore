#pragma once

#include <QByteArray>
#include <QKeyEvent>
#include <QSet>

// USB HID keyboard usage page 0x07; the phone owns layout, IME and repeat.
class UhidKeyboard
{
public:
    static quint8 usageForEvent(const QKeyEvent &event);
    QByteArray update(const QKeyEvent &event);
    void clear();
    void setLeds(quint8 leds) { m_leds = leds & 0x1f; }
    quint8 leds() const { return m_leds; }
private:
    QSet<quint8> m_keys;
    quint8 m_modifiers = 0;
    quint8 m_leds = 0;
    QByteArray m_lastReport = QByteArray(8, 0);
};
