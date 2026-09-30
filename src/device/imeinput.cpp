#include "device.h"
#include "controller.h"

namespace qsc {

bool Device::sendImeShortcut(int shortcut)
{
    return !isCameraMode() && m_controller && m_controller->sendImeShortcut(shortcut);
}

bool Device::pasteImeText(const QString &text)
{
    // Deliberately not forwarded to observers/group control: text targets only
    // the device selected when the composer was opened.
    return !isCameraMode() && m_controller && m_controller->pasteImeText(text);
}


} // namespace qsc
