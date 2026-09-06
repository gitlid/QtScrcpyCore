#include <QApplication>
#include <QClipboard>
#include <QTimer>

#include "actionmacro.h"
#include "controller.h"
#include "devicemsg.h"
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
        const bool sent = sendMessage(message);
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
        if (finished) {
            resetInputState();
            if (!m_uhidEnabled) { shutdownKeyboard(); }
        }
        emit actionMacroStateChanged(recording, playing, count);
    });
    connect(m_actionMacro, &ActionMacro::progressChanged, this, &Controller::actionMacroProgress);
    connect(m_actionMacro, &ActionMacro::errorOccurred, this, &Controller::actionMacroError);

    updateScript(gameScript);
}

Controller::~Controller() { shutdownKeyboard(); }

bool Controller::ensureUhidKeyboard()
{
    if (m_cameraMode) { return false; }
    if (m_uhidCreated) { return true; }
    ControlMsg create(ControlMsg::CMT_UHID_CREATE);
    m_uhidCreated = sendControl(create.serializeData());
    return m_uhidCreated;
}

bool Controller::setUhidKeyboardEnabled(bool enabled)
{
    if (isActionRecording() || isActionPlaying() || m_cameraMode) { return false; }
    m_uhidEnabled = enabled;
    if (enabled && !ensureUhidKeyboard()) { return false; }
    if (!enabled) { shutdownKeyboard(); }
    return true;
}

bool Controller::sendMessage(ControlMsg *message)
{
    if (message->type() == ControlMsg::CMT_UHID_INPUT && !ensureUhidKeyboard()) { return false; }
    return sendControl(message->serializeData());
}

void Controller::uhidKeyEvent(const QKeyEvent *event)
{
    if (!event || !m_uhidEnabled || m_inputBlocked || isActionPlaying()) { return; }
    Qt::KeyboardModifiers modifiers = event->modifiers();
    InputConvertGame *game = qobject_cast<InputConvertGame *>(m_inputConvert.data());
    if (game) {
        const Qt::KeyboardModifier flags[] = {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier, Qt::MetaModifier};
        const int keys[] = {Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt, Qt::Key_Meta};
        for (int i = 0; i < 4; ++i) {
            if (game->handlesKeyboardKey(keys[i])) { modifiers &= ~flags[i]; }
        }
    }
    // UhidKeyboard recovers held modifiers from Qt flags. Do not resurrect a
    // modifier which belongs exclusively to a touch mapping or mode switch.
    QKeyEvent filtered(event->type(), event->key(), modifiers, event->nativeScanCode(),
                       event->nativeVirtualKey(), event->nativeModifiers(), event->text(),
                       event->isAutoRepeat(), ushort(event->count()));
    const QByteArray report = m_keyboard.update(filtered);
    if (report.isEmpty()) { return; }
    auto *message = new ControlMsg(ControlMsg::CMT_UHID_INPUT);
    message->setUhidKeyboardReport(report);
    postControlMsg(message);
}

void Controller::releaseKeyboard()
{
    if (isActionPlaying()) { return; } // Playback owns its keys until Stop.
    QCoreApplication::sendPostedEvents(this, ControlMsg::Control);
    m_keyboard.clear();
    m_keyboardRouting.clearUhid();
    if (!m_uhidCreated) { return; }
    ControlMsg release(ControlMsg::CMT_UHID_INPUT);
    if (sendMessage(&release) && m_actionMacro) { m_actionMacro->record(release); }
}

void Controller::shutdownKeyboard()
{
    QCoreApplication::removePostedEvents(this, ControlMsg::Control);
    m_keyboard.clear();
    m_keyboardRouting.clearUhid();
    if (!m_uhidCreated) { return; }
    ControlMsg release(ControlMsg::CMT_UHID_INPUT);
    sendControl(release.serializeData());
    ControlMsg destroy(ControlMsg::CMT_UHID_DESTROY);
    sendControl(destroy.serializeData());
    m_uhidCreated = false;
}


void Controller::setFrameSize(const QSize &size)
{
    m_frameSize = size;
    if (m_actionMacro) { m_actionMacro->setCurrentScreen(size); }
}

void Controller::resetInputState(bool preserveKeymap)
{
    if (m_inputBlocked) { return; }
    m_inputBlocked = true;
    m_keyboardRouting.clear();
    const bool gameEnabled = preserveKeymap && isCurrentCustomKeymap();
    QCoreApplication::removePostedEvents(this, ControlMsg::Control);
    if (m_actionMacro) { m_actionMacro->releaseInputs(); }
    m_keyboard.clear();
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

    if (deviceMsg && deviceMsg->type() == DeviceMsg::DMT_UHID_OUTPUT
        && deviceMsg->uhidId() == ControlMsg::UhidKeyboardId && deviceMsg->uhidOutput().size() == 1) {
        m_keyboard.setLeds(quint8(deviceMsg->uhidOutput().at(0)));
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
    m_keyboardRouting.clear();
    releaseKeyboard();
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
    return playActionMacroAdvanced(repeatCount, intervalMs, 1.0, 0);
}

bool Controller::playActionMacroAdvanced(int repeatCount, int intervalMs, double speed, qint64 limitMs)
{
    if (!m_actionMacro || m_cameraMode || m_actionMacro->isRecording()
        || m_actionMacro->isPlaying() || !m_frameSize.isValid()) { return false; }
    resetInputState();
    if (m_actionMacro->requiresUhidKeyboard() && !ensureUhidKeyboard()) {
        emit actionMacroError(tr("Cannot create the UHID keyboard for this macro."));
        return false;
    }
    const bool playing = m_actionMacro->play(repeatCount, intervalMs, speed, limitMs);
    if (!playing && !m_uhidEnabled) { shutdownKeyboard(); }
    return playing;
}

bool Controller::pauseActionMacro()
{
    if (!m_actionMacro) { return false; }
    QCoreApplication::removePostedEvents(this, ControlMsg::Control);
    const bool ok = m_actionMacro->pause();
    if (ok && m_actionMacro->isRecording()) { resetInputState(true); }
    return ok;
}
bool Controller::resumeActionMacro()
{
    if (!m_actionMacro) { return false; }
    if (m_actionMacro->isRecording()) { resetInputState(true); }
    return m_actionMacro->resume();
}
bool Controller::isActionPaused() const { return m_actionMacro && m_actionMacro->isPaused(); }
bool Controller::actionMacroInterruptedInput() const { return m_actionMacro && m_actionMacro->interruptedInput(); }
qint64 Controller::actionMacroElapsedMs() const { return m_actionMacro ? m_actionMacro->activeElapsedMs() : 0; }

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
        const bool wasGameMap = isCurrentCustomKeymap();
        m_inputConvert->mouseEvent(from, frameSize, showSize);
        if (wasGameMap != isCurrentCustomKeymap()) {
            if (isCurrentCustomKeymap()) { releaseKeyboard(); }
            else { resetInputState(); }
        }
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
    if (!from || m_cameraMode || m_inputBlocked || isActionPlaying()
        || !frameSize.isValid() || !showSize.isValid()
        || (from->type() != QEvent::KeyPress && from->type() != QEvent::KeyRelease)) { return; }
    setFrameSize(frameSize);
    auto *game = qobject_cast<InputConvertGame *>(m_inputConvert.data());
    const bool mapped = game && game->handlesKeyboardKey(from->key());
    const auto preferred = m_uhidEnabled && !mapped ? KeyboardRouting::Uhid : KeyboardRouting::Converter;
    const auto decision = m_keyboardRouting.dispatch(*from, preferred);
    if (decision.route == KeyboardRouting::Ignore) { return; }
    if (decision.route == KeyboardRouting::Uhid) {
        // Keep the original native scancode and modifier events for Android's
        // physical keyboard. Never send this key through the touch mapper too.
        uhidKeyEvent(from);
        return;
    }
    if (m_inputConvert) {
        const bool wasGameMap = isCurrentCustomKeymap();
        // Release the same logical mapping chosen on key-down, even if the
        // keyboard layout or Shift/Tab representation has changed meanwhile.
        QKeyEvent paired(from->type(), decision.logicalKey, from->modifiers(),
                         from->nativeScanCode(), from->nativeVirtualKey(), from->nativeModifiers(),
                         from->text(), from->isAutoRepeat(), ushort(from->count()));
        m_inputConvert->keyEvent(&paired, frameSize, showSize);
        if (wasGameMap != isCurrentCustomKeymap()) {
            if (isCurrentCustomKeymap()) { releaseKeyboard(); }
            else { resetInputState(); }
        }
    }
}

bool Controller::event(QEvent *event)
{
    if (event && static_cast<ControlMsg::Type>(event->type()) == ControlMsg::Control) {
        ControlMsg *controlMsg = dynamic_cast<ControlMsg *>(event);
        if (controlMsg && !m_inputBlocked && !(m_actionMacro && m_actionMacro->isPlaying())) {
            if (sendMessage(controlMsg)) {
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
