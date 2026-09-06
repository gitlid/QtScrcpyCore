#include <QApplication>
#include <QClipboard>
#include <QTimer>

#include "actionmacro.h"
#include "controller.h"
#include "controlmsg.h"
#include "inputconvertgame.h"
#include "receiver.h"
#include "videosocket.h"

Controller::Controller(std::function<qint64(const QByteArray&)> sendData, QString gameScript, QObject *parent)
    : QObject(parent)
    , m_sendData(sendData)
{
    m_receiver = new Receiver(this);
    Q_ASSERT(m_receiver);

    m_actionMacro = new ActionMacro([this](ControlMsg *message) {
        // Synchronous enqueue into the control socket. Macro events must not
        // survive Stop in Qt's posted-event queue.
        const bool sent = sendControl(message->serializeData());
        delete message;
        if (!sent && m_actionMacro) {
            m_actionMacro->abort(tr("The device control connection failed. Playback has stopped."));
        }
    }, this);
    connect(m_actionMacro, &ActionMacro::stateChanged, this,
            [this](bool recording, bool playing, int count) {
        const bool busy = recording || playing;
        const bool finished = m_macroWasBusy && !busy;
        m_macroWasBusy = busy;
        if (finished) { resetInputState(); }
        emit actionMacroStateChanged(recording, playing, count);
    });
    connect(m_actionMacro, &ActionMacro::progressChanged, this, &Controller::actionMacroProgress);
    connect(m_actionMacro, &ActionMacro::errorOccurred, this, &Controller::actionMacroError);

    updateScript(gameScript);
}

Controller::~Controller() {}

void Controller::setFrameSize(const QSize &size)
{
    m_frameSize = size;
    if (m_actionMacro) { m_actionMacro->setCurrentScreen(size); }
}

void Controller::resetInputState(bool preserveKeymap)
{
    if (m_inputBlocked) { return; }
    m_inputBlocked = true;
    const bool gameEnabled = preserveKeymap && isCurrentCustomKeymap();
    QCoreApplication::removePostedEvents(this, ControlMsg::Control);
    if (m_actionMacro) { m_actionMacro->releaseInputs(); }
    // Destroy the old mapper: its child timers and context-bound delayed
    // callbacks must not regenerate held touches after an emergency stop.
    updateScript(m_gameScript);
    InputConvertGame *game = qobject_cast<InputConvertGame *>(m_inputConvert.data());
    if (game) { game->restoreGameMap(gameEnabled); }
    emit grabCursor(false);
    m_inputBlocked = false;
}

void Controller::postControlMsg(ControlMsg *controlMsg)
{
    if (!controlMsg) {
        return;
    }

    if (m_cameraMode) {
        const auto type = controlMsg->type();
        const bool isCameraControl = type == ControlMsg::CMT_CAMERA_SET_TORCH
                || type == ControlMsg::CMT_CAMERA_ZOOM_IN
                || type == ControlMsg::CMT_CAMERA_ZOOM_OUT;
        if (!isCameraControl) {
            qWarning() << "Ignoring display control message in camera mode:" << type;
            delete controlMsg;
            return;
        }
    }

    if (m_inputBlocked || (m_actionMacro && m_actionMacro->isPlaying())) {
        delete controlMsg;
        return;
    }
    QCoreApplication::postEvent(this, controlMsg);
}

void Controller::setCameraMode(bool cameraMode)
{
    m_cameraMode = cameraMode;
}

void Controller::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    if (!m_receiver) {
        return;
    }

    m_receiver->recvDeviceMsg(deviceMsg);
}

void Controller::test(QRect rc)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_MOUSE), AMOTION_EVENT_ACTION_DOWN, AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_PRIMARY, rc, 1.0f);
    postControlMsg(controlMsg);
}

void Controller::updateScript(QString gameScript)
{
    m_gameScript = gameScript;
    if (m_inputConvert) {
        delete m_inputConvert;
    }
    if (!gameScript.isEmpty()) {
        InputConvertGame *convertgame = new InputConvertGame(this);
        convertgame->loadKeyMap(gameScript);
        m_inputConvert = convertgame;
    } else {
        m_inputConvert = new InputConvertNormal(this);
    }
    Q_ASSERT(m_inputConvert);
    connect(m_inputConvert, &InputConvertBase::grabCursor, this, &Controller::grabCursor);
}

bool Controller::isCurrentCustomKeymap()
{
    if (!m_inputConvert) {
        return false;
    }

    return m_inputConvert->isCurrentCustomKeymap();
}

void Controller::postBackOrScreenOn(bool down)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_BACK_OR_SCREEN_ON);
    controlMsg->setBackOrScreenOnData(down);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::postGoHome()
{
    postKeyCodeClick(AKEYCODE_HOME);
}

void Controller::postGoMenu()
{
    postKeyCodeClick(AKEYCODE_MENU);
}

void Controller::postGoBack()
{
    postKeyCodeClick(AKEYCODE_BACK);
}

void Controller::postAppSwitch()
{
    postKeyCodeClick(AKEYCODE_APP_SWITCH);
}

void Controller::postPower()
{
    postKeyCodeClick(AKEYCODE_POWER);
}

void Controller::postVolumeUp()
{
    postKeyCodeClick(AKEYCODE_VOLUME_UP);
}

void Controller::postVolumeDown()
{
    postKeyCodeClick(AKEYCODE_VOLUME_DOWN);
}

void Controller::copy()
{
    postKeyCodeClick(AKEYCODE_COPY);
}

void Controller::cut()
{
    postKeyCodeClick(AKEYCODE_CUT);
}

void Controller::expandNotificationPanel()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_EXPAND_NOTIFICATION_PANEL);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::expandSettingsPanel()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_EXPAND_SETTINGS_PANEL));
}

void Controller::collapsePanel()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_COLLAPSE_PANELS);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::rotateDevice()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_ROTATE_DEVICE));
}

void Controller::startApp(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_START_APP);
    controlMsg->setStartAppData(name);
    postControlMsg(controlMsg);
}

void Controller::scanFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SCAN_FILE);
    controlMsg->setScanFileData(path);
    postControlMsg(controlMsg);
}

void Controller::resizeDisplay(const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0) {
        return;
    }
    m_pendingResize = size;
    if (m_resizeQueued) {
        return;
    }
    m_resizeQueued = true;
    QTimer::singleShot(0, this, &Controller::sendPendingResize);
}

void Controller::sendPendingResize()
{
    m_resizeQueued = false;
    if (m_pendingResize.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_RESIZE_DISPLAY);
    controlMsg->setResizeDisplayData(m_pendingResize);
    m_pendingResize = QSize();
    postControlMsg(controlMsg);
}

void Controller::requestDeviceClipboard()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::getDeviceClipboard(bool cut)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    ControlMsg::GetClipboardCopyKey copyKey = cut ? ControlMsg::GCCK_CUT : ControlMsg::GCCK_COPY;
    controlMsg->setGetClipboardMsgData(copyKey);
    postControlMsg(controlMsg);
}

void Controller::setDeviceClipboard(bool pause)
{
    QClipboard *board = QApplication::clipboard();
    QString text = board->text();
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    controlMsg->setSetClipboardMsgData(text, pause);
    postControlMsg(controlMsg);
}

void Controller::clipboardPaste()
{
    QClipboard *board = QApplication::clipboard();
    QString text = board->text();
    postTextInput(text);
}

void Controller::postTextInput(QString &text)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TEXT);
    if (!controlMsg) {
        return;
    }
    controlMsg->setInjectTextMsgData(text);
    postControlMsg(controlMsg);
}

bool Controller::startActionRecording()
{
    if (!m_actionMacro || m_cameraMode || m_actionMacro->isPlaying()
        || m_actionMacro->isRecording() || !m_frameSize.isValid()) { return false; }
    resetInputState(true);
    return m_actionMacro->startRecording();
}

bool Controller::stopActionRecording()
{
    return m_actionMacro && m_actionMacro->stopRecording();
}

bool Controller::saveActionMacro(const QString &fileName, QString *error) const
{
    return m_actionMacro && m_actionMacro->save(fileName, error);
}

bool Controller::loadActionMacro(const QString &fileName, QString *error)
{
    return m_actionMacro && !m_cameraMode && m_actionMacro->load(fileName, error);
}

bool Controller::playActionMacro(int repeatCount, int intervalMs)
{
    if (!m_actionMacro || m_cameraMode || m_actionMacro->isRecording()
        || m_actionMacro->isPlaying() || !m_frameSize.isValid()) { return false; }
    resetInputState();
    return m_actionMacro->play(repeatCount, intervalMs);
}

void Controller::stopActionPlayback()
{
    if (m_actionMacro) {
        m_actionMacro->stopPlayback();
    }
}

bool Controller::isActionRecording() const
{
    return m_actionMacro && m_actionMacro->isRecording();
}

bool Controller::isActionPlaying() const
{
    return m_actionMacro && m_actionMacro->isPlaying();
}

int Controller::actionMacroEventCount() const
{
    return m_actionMacro ? m_actionMacro->eventCount() : 0;
}

void Controller::setDisplayPower(bool on)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SET_DISPLAY_POWER);
    if (!controlMsg) {
        return;
    }
    controlMsg->setDisplayPowerData(on);
    postControlMsg(controlMsg);
}

void Controller::setCameraTorch(bool on)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_CAMERA_SET_TORCH);
    controlMsg->setCameraTorchData(on);
    postControlMsg(controlMsg);
}

void Controller::cameraZoomIn()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_CAMERA_ZOOM_IN));
}

void Controller::cameraZoomOut()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_CAMERA_ZOOM_OUT));
}

void Controller::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputBlocked || (m_actionMacro && m_actionMacro->isPlaying())) { return; }
    setFrameSize(frameSize);
    if (m_inputConvert) {
        m_inputConvert->mouseEvent(from, frameSize, showSize);
    }
}

void Controller::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputBlocked || (m_actionMacro && m_actionMacro->isPlaying())) { return; }
    setFrameSize(frameSize);
    if (m_inputConvert) {
        m_inputConvert->wheelEvent(from, frameSize, showSize);
    }
}

void Controller::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputBlocked || (m_actionMacro && m_actionMacro->isPlaying())) { return; }
    setFrameSize(frameSize);
    if (m_inputConvert) {
        m_inputConvert->keyEvent(from, frameSize, showSize);
    }
}

bool Controller::event(QEvent *event)
{
    if (event && static_cast<ControlMsg::Type>(event->type()) == ControlMsg::Control) {
        ControlMsg *controlMsg = dynamic_cast<ControlMsg *>(event);
        if (controlMsg && !m_inputBlocked && !(m_actionMacro && m_actionMacro->isPlaying())) {
            if (sendControl(controlMsg->serializeData())) {
                if (m_actionMacro) { m_actionMacro->record(*controlMsg); }
            } else if (m_actionMacro && m_actionMacro->isRecording()) {
                m_actionMacro->abort(tr("The device control connection failed. Recording has stopped."));
            }
        }
        return true;
    }
    return QObject::event(event);
}

bool Controller::sendControl(const QByteArray &buffer)
{
    if (buffer.isEmpty()) {
        return false;
    }
    qint32 len = 0;
    if (m_sendData) {
        len = static_cast<qint32>(m_sendData(buffer));
    }
    return len == buffer.length() ? true : false;
}

void Controller::postKeyCodeClick(AndroidKeycode keycode)
{
    ControlMsg *controlEventDown = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlEventDown) {
        return;
    }
    controlEventDown->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, keycode, 0, AMETA_NONE);
    postControlMsg(controlEventDown);

    ControlMsg *controlEventUp = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlEventUp) {
        return;
    }
    controlEventUp->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_UP, keycode, 0, AMETA_NONE);
    postControlMsg(controlEventUp);
}
