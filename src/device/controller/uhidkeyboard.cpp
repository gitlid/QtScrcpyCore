#include "uhidkeyboard.h"
#include <algorithm>

quint8 UhidKeyboard::usageForEvent(const QKeyEvent &event)
{
#ifdef Q_OS_WIN
    // Qt Windows supplies set-1 scan codes, with 0x100 for extended keys.
    // Use positions so a non-US Windows layout cannot change the HID key.
    if (event.key() == Qt::Key_Pause) { return 0x48; }
    const quint32 scan = event.nativeScanCode();
    if (scan) {
        if (scan & 0x100) {
            switch (scan & 0xff) {
            case 0x1c: return 0x58; case 0x1d: return 0xe4;
            case 0x35: return 0x54; case 0x37: return 0x46;
            case 0x38: return 0xe6; case 0x47: return 0x4a;
            case 0x48: return 0x52; case 0x49: return 0x4b;
            case 0x4b: return 0x50; case 0x4d: return 0x4f;
            case 0x4f: return 0x4d; case 0x50: return 0x51;
            case 0x51: return 0x4e; case 0x52: return 0x49;
            case 0x53: return 0x4c; case 0x5b: return 0xe3;
            case 0x5c: return 0xe7; case 0x5d: return 0x65;
            default: break;
            }
        } else {
            static const quint8 set1[] = {
                0,41,30,31,32,33,34,35,36,37,38,39,45,46,42,43,
                20,26,8,21,23,28,24,12,18,19,47,48,40,224,4,22,
                7,9,10,11,13,14,15,51,52,53,225,49,29,27,6,25,
                5,17,16,54,55,56,229,85,226,44,57,58,59,60,61,62,
                63,64,65,66,67,83,71,95,96,97,86,92,93,94,87,89,
                90,91,98,99,0,0,100,68,69
            };
            if (scan < sizeof(set1) && set1[scan]) { return set1[scan]; }
        }
    }
#endif
    const int key = event.key();
    if (event.modifiers() & Qt::KeypadModifier) {
        if (key >= Qt::Key_1 && key <= Qt::Key_9) { return quint8(0x59 + key - Qt::Key_1); }
        switch (key) {
        case Qt::Key_0: return 0x62; case Qt::Key_Period: case Qt::Key_Delete: return 0x63;
        case Qt::Key_Slash: return 0x54; case Qt::Key_Asterisk: return 0x55;
        case Qt::Key_Minus: return 0x56; case Qt::Key_Plus: return 0x57;
        default: break;
        }
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) { return quint8(4 + key - Qt::Key_A); }
    if (key >= Qt::Key_1 && key <= Qt::Key_9) { return quint8(30 + key - Qt::Key_1); }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) { return quint8(58 + key - Qt::Key_F1); }
    switch (key) {
    case Qt::Key_0: return 39; case Qt::Key_Return: return 40;
    case Qt::Key_Escape: return 41; case Qt::Key_Backspace: return 42;
    case Qt::Key_Tab: case Qt::Key_Backtab: return 43; case Qt::Key_Space: return 44;
    case Qt::Key_Minus: case Qt::Key_Underscore: return 45;
    case Qt::Key_Equal: case Qt::Key_Plus: return 46;
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return 47;
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: return 48;
    case Qt::Key_Backslash: case Qt::Key_Bar: return 49;
    case Qt::Key_Semicolon: case Qt::Key_Colon: return 51;
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return 52;
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return 53;
    case Qt::Key_Comma: case Qt::Key_Less: return 54;
    case Qt::Key_Period: case Qt::Key_Greater: return 55;
    case Qt::Key_Slash: case Qt::Key_Question: return 56;
    case Qt::Key_CapsLock: return 57; case Qt::Key_Print: return 70;
    case Qt::Key_ScrollLock: return 71; case Qt::Key_Pause: return 72;
    case Qt::Key_Insert: return 73; case Qt::Key_Home: return 74;
    case Qt::Key_PageUp: return 75; case Qt::Key_Delete: return 76;
    case Qt::Key_End: return 77; case Qt::Key_PageDown: return 78;
    case Qt::Key_Right: return 79; case Qt::Key_Left: return 80;
    case Qt::Key_Down: return 81; case Qt::Key_Up: return 82;
    case Qt::Key_NumLock: return 83; case Qt::Key_Enter: return 88;
    case Qt::Key_Menu: return 101;
    case Qt::Key_Control: return 224; case Qt::Key_Shift: return 225;
    case Qt::Key_Alt: return 226; case Qt::Key_Meta: return 227;
    case Qt::Key_AltGr: return 230;
    default: return 0;
    }
}

QByteArray UhidKeyboard::update(const QKeyEvent &event)
{
    if (event.isAutoRepeat() || (event.type() != QEvent::KeyPress && event.type() != QEvent::KeyRelease)) { return {}; }
    const quint8 usage = usageForEvent(event);
    if (!usage) { return {}; }
    const bool down = event.type() == QEvent::KeyPress;
    if (usage >= 0xe0 && usage <= 0xe7) {
        const quint8 bit = quint8(1u << (usage - 0xe0));
        if (down) { m_modifiers |= bit; } else { m_modifiers &= ~bit; }
    } else {
        if (down) { m_keys.insert(usage); } else { m_keys.remove(usage); }
        // Recover modifiers already held when the projection gained focus.
        const Qt::KeyboardModifiers flags[] = {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier, Qt::MetaModifier};
        for (int i = 0; i < 4; ++i) {
            const quint8 mask = quint8(0x11 << i);
            if (!(event.modifiers() & flags[i])) { m_modifiers &= ~mask; }
            else if (!(m_modifiers & mask)) { m_modifiers |= quint8(1 << i); }
        }
    }
    QByteArray report(8, 0);
    report[0] = char(m_modifiers);
    auto keys = m_keys.values();
    std::sort(keys.begin(), keys.end());
    if (keys.size() > 6) {
        for (int i = 2; i < 8; ++i) { report[i] = 1; } // HID ErrorRollOver
    } else {
        for (int i = 0; i < keys.size(); ++i) { report[2 + i] = char(keys[i]); }
    }
    if (report == m_lastReport) { return {}; }
    m_lastReport = report;
    return report;
}

void UhidKeyboard::clear()
{
    m_keys.clear();
    m_modifiers = 0;
    m_lastReport.fill(0);
}
