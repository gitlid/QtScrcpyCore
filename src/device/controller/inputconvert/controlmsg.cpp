#include <QDebug>
#include <QJsonValue>

#include <cmath>

#include "bufferutil.h"
#include "controlmsg.h"

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define CLAMP(V, X, Y) MIN(MAX((V), (X)), (Y))

namespace {

bool failJson(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool readInteger(const QJsonObject &json, const QString &key, qint64 minimum, qint64 maximum, qint64 *value, QString *error)
{
    const QJsonValue item = json.value(key);
    if (!item.isDouble()) {
        return failJson(error, QString("'%1' must be an integer").arg(key));
    }
    const double number = item.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum) {
        return failJson(error, QString("'%1' is outside the supported range").arg(key));
    }
    *value = static_cast<qint64>(number);
    return true;
}

bool readNumber(const QJsonObject &json, const QString &key, double minimum, double maximum, double *value, QString *error)
{
    const QJsonValue item = json.value(key);
    if (!item.isDouble()) {
        return failJson(error, QString("'%1' must be a number").arg(key));
    }
    const double number = item.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum) {
        return failJson(error, QString("'%1' is outside the supported range").arg(key));
    }
    *value = number;
    return true;
}

QJsonObject positionToJson(const QRect &position)
{
    QJsonObject json;
    json["x"] = position.x();
    json["y"] = position.y();
    json["width"] = position.width();
    json["height"] = position.height();
    return json;
}

bool positionFromJson(const QJsonValue &value, QRect *position, QString *error)
{
    if (!value.isObject()) {
        return failJson(error, "'position' must be an object");
    }
    const QJsonObject json = value.toObject();
    qint64 x = 0;
    qint64 y = 0;
    qint64 width = 0;
    qint64 height = 0;
    if (!readInteger(json, "x", 0, 0x7fffffff, &x, error)
        || !readInteger(json, "y", 0, 0x7fffffff, &y, error)
        || !readInteger(json, "width", 1, 0xffff, &width, error)
        || !readInteger(json, "height", 1, 0xffff, &height, error)) {
        return false;
    }
    if (x > width || y > height) {
        return failJson(error, "touch or scroll coordinates are outside the recorded screen");
    }
    *position = QRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height));
    return true;
}

QString messageTypeName(ControlMsg::ControlMsgType type)
{
    switch (type) {
    case ControlMsg::CMT_INJECT_KEYCODE: return "key";
    case ControlMsg::CMT_INJECT_TEXT: return "text";
    case ControlMsg::CMT_INJECT_TOUCH: return "touch";
    case ControlMsg::CMT_INJECT_SCROLL: return "scroll";
    case ControlMsg::CMT_BACK_OR_SCREEN_ON: return "back_or_screen_on";
    case ControlMsg::CMT_EXPAND_NOTIFICATION_PANEL: return "expand_notifications";
    case ControlMsg::CMT_EXPAND_SETTINGS_PANEL: return "expand_settings";
    case ControlMsg::CMT_COLLAPSE_PANELS: return "collapse_panels";
    case ControlMsg::CMT_GET_CLIPBOARD: return "get_clipboard";
    case ControlMsg::CMT_SET_CLIPBOARD: return "set_clipboard";
    case ControlMsg::CMT_SET_DISPLAY_POWER: return "display_power";
    case ControlMsg::CMT_ROTATE_DEVICE: return "rotate";
    case ControlMsg::CMT_OPEN_HARD_KEYBOARD_SETTINGS: return "keyboard_settings";
    case ControlMsg::CMT_START_APP: return "start_app";
    case ControlMsg::CMT_RESET_VIDEO: return "reset_video";
    case ControlMsg::CMT_CAMERA_SET_TORCH: return "camera_torch";
    case ControlMsg::CMT_CAMERA_ZOOM_IN: return "camera_zoom_in";
    case ControlMsg::CMT_CAMERA_ZOOM_OUT: return "camera_zoom_out";
    case ControlMsg::CMT_RESIZE_DISPLAY: return "resize_display";
    case ControlMsg::CMT_SCAN_FILE: return "scan_file";
    default: return "unknown";
    }
}

} // namespace

ControlMsg::ControlMsg(ControlMsgType controlMsgType) : QScrcpyEvent(Control)
{
    m_data.type = controlMsgType;
}

ControlMsg::~ControlMsg()
{
    if (CMT_SET_CLIPBOARD == m_data.type && Q_NULLPTR != m_data.setClipboard.text) {
        delete[] m_data.setClipboard.text;
        m_data.setClipboard.text = Q_NULLPTR;
    } else if (CMT_INJECT_TEXT == m_data.type && Q_NULLPTR != m_data.injectText.text) {
        delete[] m_data.injectText.text;
        m_data.injectText.text = Q_NULLPTR;
    } else if (CMT_START_APP == m_data.type && Q_NULLPTR != m_data.startApp.name) {
        delete[] m_data.startApp.name;
        m_data.startApp.name = Q_NULLPTR;
    } else if (CMT_SCAN_FILE == m_data.type && Q_NULLPTR != m_data.scanFile.path) {
        delete[] m_data.scanFile.path;
        m_data.scanFile.path = Q_NULLPTR;
    }
}

void ControlMsg::setInjectKeycodeMsgData(AndroidKeyeventAction action, AndroidKeycode keycode, quint32 repeat, AndroidMetastate metastate)
{
    m_data.injectKeycode.action = action;
    m_data.injectKeycode.keycode = keycode;
    m_data.injectKeycode.repeat = repeat;
    m_data.injectKeycode.metastate = metastate;
}

void ControlMsg::setInjectTextMsgData(QString &text)
{
    // write length (2 byte) + string (non nul-terminated)
    if (CONTROL_MSG_INJECT_TEXT_MAX_LENGTH < text.length()) {
        // injecting a text takes time, so limit the text length
        text = text.left(CONTROL_MSG_INJECT_TEXT_MAX_LENGTH);
    }
    QByteArray tmp = text.toUtf8();
    m_data.injectText.text = new char[tmp.length() + 1];
    memcpy(m_data.injectText.text, tmp.data(), tmp.length());
    m_data.injectText.text[tmp.length()] = '\0';
}

void ControlMsg::setInjectTouchMsgData(
    quint64 id,
    AndroidMotioneventAction action,
    AndroidMotioneventButtons actionButtons,
    AndroidMotioneventButtons buttons,
    QRect position,
    float pressure)
{
    m_data.injectTouch.id = id;
    m_data.injectTouch.action = action;
    m_data.injectTouch.actionButtons = actionButtons;
    m_data.injectTouch.buttons = buttons;
    m_data.injectTouch.position = position;
    m_data.injectTouch.pressure = pressure;
}

void ControlMsg::setInjectScrollMsgData(QRect position, float hScroll, float vScroll, AndroidMotioneventButtons buttons)
{
    m_data.injectScroll.position = position;
    m_data.injectScroll.hScroll = hScroll;
    m_data.injectScroll.vScroll = vScroll;
    m_data.injectScroll.buttons = buttons;
}

void ControlMsg::setGetClipboardMsgData(ControlMsg::GetClipboardCopyKey copyKey) 
{
    m_data.getClipboard.copyKey = copyKey;
}

void ControlMsg::setSetClipboardMsgData(QString &text, bool paste)
{
    if (text.isEmpty()) {
        m_data.setClipboard.text = Q_NULLPTR;
        return;
    }
    if (CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH < text.length()) {
        text = text.left(CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH);
    }

    QByteArray tmp = text.toUtf8();
    m_data.setClipboard.text = new char[tmp.length() + 1];
    memcpy(m_data.setClipboard.text, tmp.data(), tmp.length());
    m_data.setClipboard.text[tmp.length()] = '\0';
    m_data.setClipboard.paste = paste;
    m_data.setClipboard.sequence = 0;
}

void ControlMsg::setDisplayPowerData(bool on)
{
    m_data.setDisplayPower.on = on;
}

void ControlMsg::setCameraTorchData(bool on)
{
    m_data.cameraTorch.on = on;
}

void ControlMsg::setBackOrScreenOnData(bool down)
{
    m_data.backOrScreenOn.action = down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
}

void ControlMsg::setStartAppData(const QString &name)
{
    QByteArray utf8 = name.toUtf8().left(CONTROL_MSG_START_APP_MAX_LENGTH);
    m_data.startApp.name = new char[utf8.size() + 1];
    memcpy(m_data.startApp.name, utf8.constData(), utf8.size());
    m_data.startApp.name[utf8.size()] = '\0';
}

void ControlMsg::setScanFileData(const QString &path)
{
    QByteArray utf8 = path.toUtf8().left(CONTROL_MSG_SCAN_FILE_PATH_MAX_LENGTH);
    m_data.scanFile.path = new char[utf8.size() + 1];
    memcpy(m_data.scanFile.path, utf8.constData(), utf8.size());
    m_data.scanFile.path[utf8.size()] = '\0';
}

void ControlMsg::setResizeDisplayData(const QSize &size)
{
    m_data.resizeDisplay.width = static_cast<quint16>(qBound(1, size.width(), 0xffff));
    m_data.resizeDisplay.height = static_cast<quint16>(qBound(1, size.height(), 0xffff));
}

void ControlMsg::writePosition(QBuffer &buffer, const QRect &value)
{
    BufferUtil::write32(buffer, value.left());
    BufferUtil::write32(buffer, value.top());
    BufferUtil::write16(buffer, value.width());
    BufferUtil::write16(buffer, value.height());
}

quint16 ControlMsg::flostToU16fp(float f)
{
    Q_ASSERT(f >= 0.0f && f <= 1.0f);
    quint32 u = f * 0x1p16f; // 2^16
    if (u >= 0xffff) {
        u = 0xffff;
    }
    return (quint16)u;
}

qint16 ControlMsg::flostToI16fp(float f)
{
    Q_ASSERT(f >= -1.0f && f <= 1.0f);
    qint32 i = f * 0x1p15f; // 2^15
    Q_ASSERT(i >= -0x8000);
    if (i >= 0x7fff) {
        Q_ASSERT(i == 0x8000); // for f == 1.0f
        i = 0x7fff;
    }
    return (qint16)i;
}

QByteArray ControlMsg::serializeData()
{
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QBuffer::WriteOnly);
    buffer.putChar(m_data.type);

    switch (m_data.type) {
    case CMT_INJECT_KEYCODE:
        buffer.putChar(m_data.injectKeycode.action);
        BufferUtil::write32(buffer, m_data.injectKeycode.keycode);
        BufferUtil::write32(buffer, m_data.injectKeycode.repeat);
        BufferUtil::write32(buffer, m_data.injectKeycode.metastate);
        break;
    case CMT_INJECT_TEXT:
        BufferUtil::write32(buffer, static_cast<quint32>(strlen(m_data.injectText.text)));
        buffer.write(m_data.injectText.text, strlen(m_data.injectText.text));
        break;
    case CMT_INJECT_TOUCH: {
        buffer.putChar(m_data.injectTouch.action);
        BufferUtil::write64(buffer, m_data.injectTouch.id);
        writePosition(buffer, m_data.injectTouch.position);
        quint16 pressure = flostToU16fp(m_data.injectTouch.pressure);
        BufferUtil::write16(buffer, pressure);
        BufferUtil::write32(buffer, m_data.injectTouch.actionButtons);
        BufferUtil::write32(buffer, m_data.injectTouch.buttons);
    } break;
    case CMT_INJECT_SCROLL: {
        writePosition(buffer, m_data.injectScroll.position);
        // Accept values in the range [-16, 16].
        // Normalize to [-1, 1] in order to use sc_float_to_i16fp().
        float hscrollNorm = m_data.injectScroll.hScroll / 16;
        hscrollNorm = CLAMP(hscrollNorm, -1, 1);
        float vscrollNorm = m_data.injectScroll.vScroll / 16;
        vscrollNorm = CLAMP(vscrollNorm, -1, 1);
        qint16 hScroll = flostToI16fp(hscrollNorm);
        qint16 vScroll = flostToI16fp(vscrollNorm);
        BufferUtil::write16(buffer, (quint16)hScroll);
        BufferUtil::write16(buffer, (quint16)vScroll);
        BufferUtil::write32(buffer, m_data.injectScroll.buttons);
    } break;
    case CMT_BACK_OR_SCREEN_ON:
        buffer.putChar(m_data.backOrScreenOn.action);
        break;
    case CMT_GET_CLIPBOARD:
        buffer.putChar(m_data.getClipboard.copyKey);
        break;
    case CMT_SET_CLIPBOARD:
        BufferUtil::write64(buffer, m_data.setClipboard.sequence);
        buffer.putChar(!!m_data.setClipboard.paste);
        if (m_data.setClipboard.text != Q_NULLPTR) {
            BufferUtil::write32(buffer, static_cast<quint32>(strlen(m_data.setClipboard.text)));
            buffer.write(m_data.setClipboard.text, strlen(m_data.setClipboard.text));
        } else {
            BufferUtil::write32(buffer, 0);
            buffer.write(m_data.setClipboard.text, 0);
        }
        break;
    case CMT_SET_DISPLAY_POWER:
        buffer.putChar(m_data.setDisplayPower.on);
        break;
    case CMT_CAMERA_SET_TORCH:
        buffer.putChar(m_data.cameraTorch.on);
        break;
    case CMT_START_APP: {
        const quint8 length = m_data.startApp.name ? static_cast<quint8>(strlen(m_data.startApp.name)) : 0;
        buffer.putChar(length);
        if (length) {
            buffer.write(m_data.startApp.name, length);
        }
    } break;
    case CMT_RESIZE_DISPLAY:
        BufferUtil::write16(buffer, m_data.resizeDisplay.width);
        BufferUtil::write16(buffer, m_data.resizeDisplay.height);
        break;
    case CMT_SCAN_FILE: {
        const quint32 length = m_data.scanFile.path ? static_cast<quint32>(strlen(m_data.scanFile.path)) : 0;
        BufferUtil::write32(buffer, length);
        if (length) {
            buffer.write(m_data.scanFile.path, length);
        }
    } break;
    case CMT_EXPAND_NOTIFICATION_PANEL:
    case CMT_EXPAND_SETTINGS_PANEL:
    case CMT_COLLAPSE_PANELS:
    case CMT_ROTATE_DEVICE:
    case CMT_OPEN_HARD_KEYBOARD_SETTINGS:
    case CMT_RESET_VIDEO:
    case CMT_CAMERA_ZOOM_IN:
    case CMT_CAMERA_ZOOM_OUT:
        break;
    default:
        qDebug() << "Unknown event type:" << m_data.type;
        break;
    }
    buffer.close();
    return byteArray;
}

QJsonObject ControlMsg::toJson() const
{
    QJsonObject json;
    json["type"] = static_cast<int>(m_data.type);
    json["kind"] = messageTypeName(m_data.type);

    switch (m_data.type) {
    case CMT_INJECT_KEYCODE:
        json["action"] = static_cast<int>(m_data.injectKeycode.action);
        json["keycode"] = static_cast<int>(m_data.injectKeycode.keycode);
        json["repeat"] = static_cast<double>(m_data.injectKeycode.repeat);
        json["metastate"] = static_cast<int>(m_data.injectKeycode.metastate);
        break;
    case CMT_INJECT_TEXT:
        json["text"] = QString::fromUtf8(m_data.injectText.text ? m_data.injectText.text : "");
        break;
    case CMT_INJECT_TOUCH:
        json["pointerId"] = QString::number(m_data.injectTouch.id);
        json["action"] = static_cast<int>(m_data.injectTouch.action);
        json["actionButtons"] = static_cast<int>(m_data.injectTouch.actionButtons);
        json["buttons"] = static_cast<int>(m_data.injectTouch.buttons);
        json["position"] = positionToJson(m_data.injectTouch.position);
        json["pressure"] = m_data.injectTouch.pressure;
        break;
    case CMT_INJECT_SCROLL:
        json["position"] = positionToJson(m_data.injectScroll.position);
        json["horizontal"] = m_data.injectScroll.hScroll;
        json["vertical"] = m_data.injectScroll.vScroll;
        json["buttons"] = static_cast<int>(m_data.injectScroll.buttons);
        break;
    case CMT_BACK_OR_SCREEN_ON:
        json["action"] = static_cast<int>(m_data.backOrScreenOn.action);
        break;
    case CMT_GET_CLIPBOARD:
        json["copyKey"] = static_cast<int>(m_data.getClipboard.copyKey);
        break;
    case CMT_SET_CLIPBOARD:
        json["text"] = QString::fromUtf8(m_data.setClipboard.text ? m_data.setClipboard.text : "");
        json["paste"] = m_data.setClipboard.paste;
        break;
    case CMT_SET_DISPLAY_POWER:
        json["on"] = m_data.setDisplayPower.on;
        break;
    case CMT_CAMERA_SET_TORCH:
        json["on"] = m_data.cameraTorch.on;
        break;
    case CMT_START_APP:
        json["name"] = QString::fromUtf8(m_data.startApp.name ? m_data.startApp.name : "");
        break;
    case CMT_RESIZE_DISPLAY:
        json["width"] = m_data.resizeDisplay.width;
        json["height"] = m_data.resizeDisplay.height;
        break;
    case CMT_SCAN_FILE:
        json["path"] = QString::fromUtf8(m_data.scanFile.path ? m_data.scanFile.path : "");
        break;
    default:
        break;
    }
    return json;
}

ControlMsg *ControlMsg::fromJson(const QJsonObject &json, QString *error)
{
    qint64 rawType = 0;
    if (!readInteger(json, "type", CMT_INJECT_KEYCODE, CMT_SCAN_FILE, &rawType, error)) {
        return Q_NULLPTR;
    }
    const ControlMsgType type = static_cast<ControlMsgType>(rawType);
    ControlMsg *message = new ControlMsg(type);
    qint64 first = 0;
    qint64 second = 0;
    qint64 third = 0;
    qint64 fourth = 0;
    double number = 0;
    QRect position;

    switch (type) {
    case CMT_INJECT_KEYCODE:
        if (!readInteger(json, "action", 0, 1, &first, error)
            || !readInteger(json, "keycode", 0, 0x7fffffff, &second, error)
            || !readInteger(json, "repeat", 0, 0xffffffffLL, &third, error)
            || !readInteger(json, "metastate", 0, 0x7fffffff, &fourth, error)) {
            delete message;
            return Q_NULLPTR;
        }
        message->setInjectKeycodeMsgData(static_cast<AndroidKeyeventAction>(first), static_cast<AndroidKeycode>(second),
                                         static_cast<quint32>(third), static_cast<AndroidMetastate>(fourth));
        break;
    case CMT_INJECT_TEXT: {
        if (!json.value("text").isString()) {
            delete message;
            failJson(error, "'text' must be a string");
            return Q_NULLPTR;
        }
        QString text = json.value("text").toString();
        message->setInjectTextMsgData(text);
        break;
    }
    case CMT_INJECT_TOUCH: {
        const QJsonValue pointerValue = json.value("pointerId");
        bool pointerOk = false;
        const quint64 pointerId = pointerValue.isString() ? pointerValue.toString().toULongLong(&pointerOk) : 0;
        if (!pointerOk
            || !readInteger(json, "action", 0, 2, &first, error)
            || !readInteger(json, "actionButtons", 0, 0x7fffffff, &second, error)
            || !readInteger(json, "buttons", 0, 0x7fffffff, &third, error)
            || !positionFromJson(json.value("position"), &position, error)
            || !readNumber(json, "pressure", 0.0, 1.0, &number, error)) {
            delete message;
            if (!pointerOk && error) {
                *error = "'pointerId' must be an unsigned integer string";
            }
            return Q_NULLPTR;
        }
        message->setInjectTouchMsgData(pointerId, static_cast<AndroidMotioneventAction>(first),
                                       static_cast<AndroidMotioneventButtons>(second), static_cast<AndroidMotioneventButtons>(third),
                                       position, static_cast<float>(number));
        break;
    }
    case CMT_INJECT_SCROLL: {
        double horizontal = 0;
        double vertical = 0;
        if (!positionFromJson(json.value("position"), &position, error)
            || !readNumber(json, "horizontal", -16.0, 16.0, &horizontal, error)
            || !readNumber(json, "vertical", -16.0, 16.0, &vertical, error)) {
            delete message;
            return Q_NULLPTR;
        } else {
            if (!readInteger(json, "buttons", 0, 0x7fffffff, &first, error)) {
                delete message;
                return Q_NULLPTR;
            }
            message->setInjectScrollMsgData(position, static_cast<float>(horizontal), static_cast<float>(vertical),
                                             static_cast<AndroidMotioneventButtons>(first));
        }
        break;
    }
    case CMT_BACK_OR_SCREEN_ON:
        if (!readInteger(json, "action", 0, 1, &first, error)) {
            delete message;
            return Q_NULLPTR;
        }
        message->setBackOrScreenOnData(first == AKEY_EVENT_ACTION_DOWN);
        break;
    case CMT_GET_CLIPBOARD:
        if (!readInteger(json, "copyKey", GCCK_NONE, GCCK_CUT, &first, error)) {
            delete message;
            return Q_NULLPTR;
        }
        message->setGetClipboardMsgData(static_cast<GetClipboardCopyKey>(first));
        break;
    case CMT_SET_CLIPBOARD: {
        if (!json.value("text").isString() || !json.value("paste").isBool()) {
            delete message;
            failJson(error, "clipboard message requires string 'text' and boolean 'paste'");
            return Q_NULLPTR;
        }
        QString text = json.value("text").toString();
        message->setSetClipboardMsgData(text, json.value("paste").toBool());
        break;
    }
    case CMT_SET_DISPLAY_POWER:
    case CMT_CAMERA_SET_TORCH:
        if (!json.value("on").isBool()) {
            delete message;
            failJson(error, "'on' must be a boolean");
            return Q_NULLPTR;
        }
        if (type == CMT_SET_DISPLAY_POWER) {
            message->setDisplayPowerData(json.value("on").toBool());
        } else {
            message->setCameraTorchData(json.value("on").toBool());
        }
        break;
    case CMT_START_APP: {
        if (!json.value("name").isString() || json.value("name").toString().isEmpty()) {
            delete message;
            failJson(error, "'name' must be a non-empty string");
            return Q_NULLPTR;
        }
        message->setStartAppData(json.value("name").toString());
        break;
    }
    case CMT_RESIZE_DISPLAY:
        if (!readInteger(json, "width", 1, 0xffff, &first, error)
            || !readInteger(json, "height", 1, 0xffff, &second, error)) {
            delete message;
            return Q_NULLPTR;
        }
        message->setResizeDisplayData(QSize(static_cast<int>(first), static_cast<int>(second)));
        break;
    case CMT_SCAN_FILE:
        if (!json.value("path").isString() || json.value("path").toString().isEmpty()) {
            delete message;
            failJson(error, "'path' must be a non-empty string");
            return Q_NULLPTR;
        }
        message->setScanFileData(json.value("path").toString());
        break;
    case CMT_EXPAND_NOTIFICATION_PANEL:
    case CMT_EXPAND_SETTINGS_PANEL:
    case CMT_COLLAPSE_PANELS:
    case CMT_ROTATE_DEVICE:
    case CMT_OPEN_HARD_KEYBOARD_SETTINGS:
    case CMT_RESET_VIDEO:
    case CMT_CAMERA_ZOOM_IN:
    case CMT_CAMERA_ZOOM_OUT:
        break;
    default:
        delete message;
        failJson(error, "unsupported action message type");
        return Q_NULLPTR;
    }
    return message;
}
