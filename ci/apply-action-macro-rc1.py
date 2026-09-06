"""One-time, checked source migration for the action-macro RC1 branch.

Only the explicit source files below are edited. Every replacement must match
exactly once; a mismatched upstream file aborts without committing anything.
Run by the dedicated maintenance workflow, not during normal builds.
"""
from pathlib import Path

changes = {}

def edit(path, old, new):
    text = changes.get(path, Path(path).read_text(encoding='utf-8-sig'))
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: expected exactly one source anchor: {old[:90]!r}')
    changes[path] = text.replace(old, new, 1)

p = 'src/device/controller/inputconvert/controlmsg.cpp'
edit(p, '    m_data.type = controlMsgType;\n}', '''    m_data.type = controlMsgType;
    // Initialize pointer-bearing union alternatives before JSON validation:
    // fromJson() deletes partially populated messages on malformed input.
    switch (controlMsgType) {
    case CMT_INJECT_TEXT: m_data.injectText.text = Q_NULLPTR; break;
    case CMT_SET_CLIPBOARD:
        m_data.setClipboard.sequence = 0;
        m_data.setClipboard.text = Q_NULLPTR;
        m_data.setClipboard.paste = false;
        break;
    case CMT_START_APP: m_data.startApp.name = Q_NULLPTR; break;
    case CMT_SCAN_FILE: m_data.scanFile.path = Q_NULLPTR; break;
    case CMT_GET_CLIPBOARD: m_data.getClipboard.copyKey = GCCK_NONE; break;
    default: break;
    }
}''')
edit(p, 'void ControlMsg::setSetClipboardMsgData(QString &text, bool paste)\n{', '''void ControlMsg::setSetClipboardMsgData(QString &text, bool paste)
{
    m_data.setClipboard.paste = paste;
    m_data.setClipboard.sequence = 0;''')
edit(p, 'if (x > width || y > height)', 'if (x >= width || y >= height)')

p = 'src/device/controller/inputconvert/inputconvertgame.h'
edit(p, '    void loadKeyMap(const QString &json);', '''    void loadKeyMap(const QString &json);
    // Restore only the enabled flag after clearing all held/delayed inputs.
    void restoreGameMap(bool active) { m_gameMap = active; }''')

p = 'src/device/controller/controller.h'
edit(p, '    void setCameraMode(bool cameraMode);', '''    void setCameraMode(bool cameraMode);
    void setFrameSize(const QSize &size);''')
edit(p, '    void sendPendingResize();', '''    void sendPendingResize();
    void resetInputState(bool preserveKeymap = false);''')
edit(p, '    bool m_cameraMode = false;', '''    bool m_cameraMode = false;
    bool m_inputBlocked = false;
    bool m_macroWasBusy = false;
    QString m_gameScript;
    QSize m_frameSize;''')

p = 'src/device/controller/controller.cpp'
edit(p, '''    m_actionMacro = new ActionMacro([this](ControlMsg *message) {
        postControlMsg(message);
    }, this);''', '''    m_actionMacro = new ActionMacro([this](ControlMsg *message) {
        // Synchronous enqueue into the control socket. Macro events must not
        // survive Stop in Qt's posted-event queue.
        const bool sent = sendControl(message->serializeData());
        delete message;
        if (!sent && m_actionMacro) {
            m_actionMacro->abort(tr("The device control connection failed. Playback has stopped."));
        }
    }, this);''')
edit(p, '    connect(m_actionMacro, &ActionMacro::stateChanged, this, &Controller::actionMacroStateChanged);', '''    connect(m_actionMacro, &ActionMacro::stateChanged, this,
            [this](bool recording, bool playing, int count) {
        const bool busy = recording || playing;
        const bool finished = m_macroWasBusy && !busy;
        m_macroWasBusy = busy;
        if (finished) { resetInputState(); }
        emit actionMacroStateChanged(recording, playing, count);
    });''')
edit(p, '''    if (m_actionMacro) {
        m_actionMacro->record(*controlMsg);
    }

    QCoreApplication::postEvent(this, controlMsg);''', '''    if (m_inputBlocked || (m_actionMacro && m_actionMacro->isPlaying())) {
        delete controlMsg;
        return;
    }
    QCoreApplication::postEvent(this, controlMsg);''')
edit(p, 'void Controller::updateScript(QString gameScript)\n{', '''void Controller::updateScript(QString gameScript)
{
    m_gameScript = gameScript;''')
edit(p, '''bool Controller::startActionRecording()
{
    return m_actionMacro && !m_cameraMode && m_actionMacro->startRecording();
}''', '''bool Controller::startActionRecording()
{
    if (!m_actionMacro || m_cameraMode || m_actionMacro->isPlaying()
        || m_actionMacro->isRecording() || !m_frameSize.isValid()) { return false; }
    resetInputState(true);
    return m_actionMacro->startRecording();
}''')
edit(p, '''bool Controller::playActionMacro(int repeatCount, int intervalMs)
{
    return m_actionMacro && !m_cameraMode && m_actionMacro->play(repeatCount, intervalMs);
}''', '''bool Controller::playActionMacro(int repeatCount, int intervalMs)
{
    if (!m_actionMacro || m_cameraMode || m_actionMacro->isRecording()
        || m_actionMacro->isPlaying() || !m_frameSize.isValid()) { return false; }
    resetInputState();
    return m_actionMacro->play(repeatCount, intervalMs);
}''')
for name, cls in [('mouseEvent','QMouseEvent'),('wheelEvent','QWheelEvent'),('keyEvent','QKeyEvent')]:
    old = f'void Controller::{name}(const {cls} *from, const QSize &frameSize, const QSize &showSize)\n{{'
    edit(p, old, old + '''
    if (m_inputBlocked || (m_actionMacro && m_actionMacro->isPlaying())) { return; }
    setFrameSize(frameSize);''')
edit(p, '''        if (controlMsg) {
            sendControl(controlMsg->serializeData());
        }''', '''        if (controlMsg && !m_inputBlocked && !(m_actionMacro && m_actionMacro->isPlaying())) {
            if (sendControl(controlMsg->serializeData())) {
                if (m_actionMacro) { m_actionMacro->record(*controlMsg); }
            } else if (m_actionMacro && m_actionMacro->isRecording()) {
                m_actionMacro->abort(tr("The device control connection failed. Recording has stopped."));
            }
        }''')
edit(p, 'Controller::~Controller() {}', '''Controller::~Controller() {}

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
}''')

p = 'src/device/device.cpp'
edit(p, '''            m_serverStartSuccess = success;
            emit deviceConnected''', '''            m_serverStartSuccess = success;
            if (m_controller) { m_controller->setFrameSize(success ? size : QSize()); }
            emit deviceConnected''')
edit(p, '''            qInfo() << "Video session changed to" << size << "client resized:" << clientResized;''', '''            qInfo() << "Video session changed to" << size << "client resized:" << clientResized;
            if (m_controller) { m_controller->setFrameSize(size); }''')
edit(p, '''        m_controller->stopActionPlayback();
        m_controller->stopActionRecording();''', '''        m_controller->stopActionPlayback();
        m_controller->stopActionRecording();
        m_controller->setFrameSize(QSize());''')

for path, text in changes.items():
    Path(path).write_text(text, encoding='utf-8', newline='\n')
    print('UPDATED', path)
