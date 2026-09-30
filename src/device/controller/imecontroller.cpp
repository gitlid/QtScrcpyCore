#include "controller.h"
#include "controlmsg.h"

bool Controller::pasteImeText(const QString &text)
{
    if (m_cameraMode || m_inputBlocked || m_panelActive || isActionPlaying() || isActionRecording()
        || text.isEmpty() || text.size() > 65536 || text.contains(QChar(0))) { return false; }
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > 65536 || QString::fromUtf8(utf8) != text) { return false; }
    // SDK INJECT_TEXT only maps a subset of Unicode to Android key events.
    // Clipboard paste uses the existing server protocol and preserves Chinese.
    // Do not set QApplication::clipboard(): it may hold private unrelated data.
    resetInputState(true);
    QString committed = text;
    ControlMsg paste(ControlMsg::CMT_SET_CLIPBOARD);
    paste.setSetClipboardMsgData(committed, true);
    return sendMessage(&paste); // Sent, not an acknowledgement from the editor.
}

bool Controller::sendImeShortcut(int shortcut)
{
    if (shortcut < 0 || shortcut > 2 || !m_uhidEnabled || m_cameraMode || m_inputBlocked
        || m_panelActive || isActionPlaying() || isActionRecording()) { return false; }
    // Explicit UI action: release earlier touches/keys, then send via the same
    // UHID device. A mapped Shift/Space must not turn an IME switch into a tap.
    resetInputState(true);
    if (!ensureUhidKeyboard()) { return false; }
    auto sendReport = [this](const QByteArray &report) {
        ControlMsg input(ControlMsg::CMT_UHID_INPUT);
        input.setUhidKeyboardReport(report);
        return sendMessage(&input);
    };
    QByteArray report(8, 0);
    report[0] = shortcut == 1 ? char(0x01) : char(0x02); // left Ctrl / Shift
    bool ok = sendReport(report);
    if (ok && shortcut != 0) {
        report[2] = char(0x2c); // HID Space
        ok = sendReport(report);
        if (ok) { report[2] = 0; ok = sendReport(report); }
    }
    // Also try to release after a partial transport failure. Never add a second
    // keyboard or send these synthetic shortcuts back through game mappings.
    const bool released = sendReport(QByteArray(8, 0));
    return ok && released;
}

