from pathlib import Path
root = Path(__file__).resolve().parents[1]
def edit(p, old, new):
    f = root / p
    t = f.read_text(encoding='utf-8-sig')
    assert t.count(old) == 1, (p, old[:80], t.count(old))
    f.write_text(t.replace(old, new), encoding='utf8')
h = 'src/device/controller/actionmacro.h'
if 'bool pause();' in (root/h).read_text():
    raise SystemExit('Migration already applied; refusing to reapply.')
edit(h, '    bool play(int repeatCount, int intervalMs);', '''    bool play(int repeatCount, int intervalMs);
    bool play(int repeatCount, int intervalMs, double speed, qint64 limitMs);
    bool pause();
    bool resume();
    bool isPaused() const { return m_paused || m_recordingPaused; }
    bool interruptedInput() const { return m_interruptedInput; }
    qint64 activeElapsedMs() const;
    double playbackSpeed() const { return m_speed; }''')
edit(h, '    void scheduleNext();', '''    void scheduleNext();
    qint64 phaseElapsedNs() const;
    qint64 recordingElapsedMs() const;
    bool hasActiveInputs() const;
    void prepareResumeBoundary();''')
edit(h, '    bool m_recording = false;', '''    // Paused playback still owns the device: m_playing remains true.
    QElapsedTimer m_activeTimer;
    qint64 m_phaseBaseNs = 0;
    qint64 m_activeBaseNs = 0;
    qint64 m_recordingBaseNs = 0;
    qint64 m_limitMs = 0;
    qint64 m_resumeAtMs = 0;
    int m_resumeIndex = 0;
    double m_speed = 1.0;
    bool m_paused = false;
    bool m_recordingPaused = false;
    bool m_interruptedInput = false;
    bool m_recording = false;''')
p = 'src/device/controller/actionmacro.cpp'
edit(p, '    m_recordingTimer.start();', '''    m_recordingBaseNs = 0;
    m_recordingPaused = false;
    m_interruptedInput = false;
    m_recordingTimer.start();''')
edit(p, '    if (!m_recording) { return; }\n    const qint64 atMs = m_recordingTimer.elapsed();', '    if (!m_recording || m_recordingPaused) { return; }\n    const qint64 atMs = recordingElapsedMs();')
edit(p, '    m_durationMs = qMin(m_recordingTimer.elapsed(), kMaximumDurationMs);\n    const QVector<QJsonObject> releases = takeReleases();\n    for (const QJsonObject &release : releases) { append(release, m_durationMs); }', '''    m_durationMs = qMin(recordingElapsedMs(), kMaximumDurationMs);
    const QVector<QJsonObject> releases = takeReleases();
    if (!m_recordingPaused) {
        for (const QJsonObject &release : releases) { append(release, m_durationMs); }
    }
    m_recordingPaused = false;''')
f = root/p
t = f.read_text()
a = t.index('bool ActionMacro::play(int repeatCount, int intervalMs)')
b = t.index('void ActionMacro::updateActiveInputs', a)
new = r'''qint64 ActionMacro::phaseElapsedNs() const
{
    return m_phaseBaseNs + (m_paused ? 0 : m_loopTimer.nsecsElapsed());
}

qint64 ActionMacro::activeElapsedMs() const
{
    if (m_recording) { return recordingElapsedMs(); }
    return (m_activeBaseNs + (m_playing && !m_paused ? m_activeTimer.nsecsElapsed() : 0)) / 1000000;
}

qint64 ActionMacro::recordingElapsedMs() const
{
    return (m_recordingBaseNs + (m_recordingPaused ? 0 : m_recordingTimer.nsecsElapsed())) / 1000000;
}

bool ActionMacro::hasActiveInputs() const
{
    return !m_activeKeys.isEmpty() || !m_activeTouches.isEmpty()
        || !m_activeBack.isEmpty() || !m_activeHidKeyboard.isEmpty();
}

void ActionMacro::prepareResumeBoundary()
{
    m_resumeIndex = m_eventIndex;
    m_resumeAtMs = 0;
    m_interruptedInput = hasActiveInputs();
    if (!m_interruptedInput) { return; }
    // Find the next neutral input boundary WITHOUT dispatching future events.
    // A released drag cannot be resumed as the same continuous touch. Skip
    // the interrupted group instead of silently synthesizing a second click.
    const auto keys = m_activeKeys;
    const auto touches = m_activeTouches;
    const auto back = m_activeBack;
    const auto hid = m_activeHidKeyboard;
    while (m_resumeIndex < m_events.size() && hasActiveInputs()) {
        const Event &event = m_events.at(m_resumeIndex++);
        updateActiveInputs(event.message);
        m_resumeAtMs = event.atMs;
    }
    if (hasActiveInputs()) { m_resumeAtMs = m_durationMs; }
    m_activeKeys = keys;
    m_activeTouches = touches;
    m_activeBack = back;
    m_activeHidKeyboard = hid;
}

bool ActionMacro::pause()
{
    if (m_stopping || isPaused()) { return false; }
    if (m_recording) {
        m_recordingBaseNs += m_recordingTimer.nsecsElapsed();
        m_recordingPaused = true;
        m_stopping = true;
        const auto releases = takeReleases();
        const qint64 atMs = qMin(recordingElapsedMs(), kMaximumDurationMs);
        for (const auto &release : releases) { append(release, atMs); }
        dispatchReleases(releases);
        m_stopping = false;
        notifyState();
        return true;
    }
    if (!m_playing) { return false; }
    m_phaseBaseNs += m_loopTimer.nsecsElapsed();
    m_activeBaseNs += m_activeTimer.nsecsElapsed();
    m_paused = true;
    m_playbackTimer.stop();
    prepareResumeBoundary();
    releaseInputs();
    notifyState();
    return true;
}

bool ActionMacro::resume()
{
    if (m_stopping) { return false; }
    if (m_recording && m_recordingPaused) {
        releaseInputs();
        m_recordingTimer.restart();
        m_recordingPaused = false;
        notifyState();
        return true;
    }
    if (!m_playing || !m_paused) { return false; }
    if (m_recordedScreen.isValid() && m_recordedScreen != m_currentScreen) {
        abort(tr("Display changed while paused. Playback cannot resume."));
        return false;
    }
    if (m_interruptedInput) {
        m_eventIndex = m_resumeIndex;
        m_phaseBaseNs = qMax(m_phaseBaseNs, static_cast<qint64>(m_resumeAtMs * 1000000.0 / m_speed));
    }
    m_interruptedInput = false;
    m_paused = false;
    m_loopTimer.restart();
    m_activeTimer.restart();
    notifyState();
    emit progressChanged(m_eventIndex, m_events.size(), m_currentLoop, m_repeatCount);
    scheduleNext();
    return true;
}

bool ActionMacro::play(int repeatCount, int intervalMs)
{
    return play(repeatCount, intervalMs, 1.0, 0);
}

bool ActionMacro::play(int repeatCount, int intervalMs, double speed, qint64 limitMs)
{
    if (m_stopping || m_recording || m_playing || m_events.isEmpty()
        || repeatCount < 0 || repeatCount > 9999 || intervalMs < 0 || intervalMs > 600000
        || !std::isfinite(speed) || speed < 0.25 || speed > 8.0
        || limitMs < 0 || limitMs > kMaximumDurationMs) { return false; }
    if (m_recordedScreen.isValid() && m_currentScreen.isValid() && m_recordedScreen != m_currentScreen) {
        emit errorOccurred(tr("The macro screen size or orientation differs from the current display. Record again."));
        return false;
    }
    releaseInputs();
    m_speed = speed;
    m_limitMs = limitMs;
    m_repeatCount = repeatCount;
    m_intervalMs = intervalMs;
    m_currentLoop = 1;
    m_eventIndex = 0;
    m_waitingForNextLoop = false;
    m_paused = false;
    m_interruptedInput = false;
    m_phaseBaseNs = 0;
    m_activeBaseNs = 0;
    m_playing = true;
    m_loopTimer.start();
    m_activeTimer.start();
    notifyState();
    emit progressChanged(0, m_events.size(), m_currentLoop, m_repeatCount);
    scheduleNext();
    return true;
}

void ActionMacro::scheduleNext()
{
    if (!m_playing || m_paused || m_stopping) { return; }
    const qint64 recordedDue = m_eventIndex < m_events.size() ? m_events.at(m_eventIndex).atMs : m_durationMs;
    const qint64 dueNs = m_waitingForNextLoop ? qMax(1, m_intervalMs) * 1000000LL
        : static_cast<qint64>(std::ceil(recordedDue * 1000000.0 / m_speed));
    qint64 delayNs = qMax<qint64>(0, dueNs - phaseElapsedNs());
    if (m_limitMs > 0) {
        const qint64 remaining = m_limitMs * 1000000LL - m_activeBaseNs - m_activeTimer.nsecsElapsed();
        delayNs = qMin(delayNs, qMax<qint64>(0, remaining));
    }
    m_playbackTimer.start(static_cast<int>(qMin<qint64>(2147483647, (delayNs + 999999) / 1000000)));
}

void ActionMacro::onPlaybackTimer()
{
    if (!m_playing || m_paused || m_stopping) { return; }
    if (m_limitMs > 0 && activeElapsedMs() >= m_limitMs) { finishPlayback(); return; }
    if (m_waitingForNextLoop) {
        if (phaseElapsedNs() < qMax(1, m_intervalMs) * 1000000LL) { scheduleNext(); return; }
        m_waitingForNextLoop = false;
        m_phaseBaseNs = 0;
        m_loopTimer.restart();
        emit progressChanged(0, m_events.size(), m_currentLoop, m_repeatCount);
        scheduleNext();
        return;
    }
    const qint64 recordedDue = m_eventIndex < m_events.size() ? m_events.at(m_eventIndex).atMs : m_durationMs;
    const qint64 dueNs = static_cast<qint64>(std::ceil(recordedDue * 1000000.0 / m_speed));
    const qint64 elapsed = phaseElapsedNs();
    if (elapsed < dueNs) { scheduleNext(); return; }
    if (m_eventIndex >= m_events.size()) {
        if (m_repeatCount > 0 && m_currentLoop >= m_repeatCount) { finishPlayback(); return; }
        releaseInputs();
        if (!m_playing) { return; }
        if (m_currentLoop == std::numeric_limits<int>::max()) { abort(tr("Loop counter safety limit reached.")); return; }
        ++m_currentLoop;
        m_eventIndex = 0;
        m_waitingForNextLoop = true;
        m_phaseBaseNs = 0;
        m_loopTimer.restart();
        scheduleNext();
        return;
    }
    if (elapsed - dueNs > kMaximumLatenessMs * 1000000LL) {
        abort(tr("Playback fell more than 2 seconds behind. Overdue clicks were not replayed."));
        return;
    }
    const QJsonObject json = m_events.at(m_eventIndex).message;
    QString error;
    ControlMsg *message = ControlMsg::fromJson(json, &error);
    if (!message) { abort(tr("Invalid playback event: %1").arg(error)); return; }
    updateActiveInputs(json);
    if (m_activeKeys.size() > 256 || m_activeTouches.size() > 10) {
        delete message;
        abort(tr("Playback exceeded the active-input safety limit."));
        return;
    }
    ++m_eventIndex;
    if (m_dispatch) { m_dispatch(message); } else { delete message; abort(tr("No control transport is available.")); }
    if (!m_playing) { return; }
    emit progressChanged(m_eventIndex, m_events.size(), m_currentLoop, m_repeatCount);
    scheduleNext();
}

'''
f.write_text(t[:a] + new + t[b:])
edit(p, '    m_playbackTimer.stop();\n    m_playing = false;', '''    m_playbackTimer.stop();
    if (m_playing && !m_paused) { m_activeBaseNs += m_activeTimer.nsecsElapsed(); }
    m_paused = false;
    m_interruptedInput = false;
    m_playing = false;''')
methods = '''    bool playActionMacroAdvanced(int repeatCount, int intervalMs, double speed, qint64 limitMs);
    bool pauseActionMacro();
    bool resumeActionMacro();
    bool isActionPaused() const;
    bool actionMacroInterruptedInput() const;
    qint64 actionMacroElapsedMs() const;
'''
for p in ['src/device/controller/controller.h', 'src/device/device.h']:
    edit(p, '    void stopActionPlayback()', methods + '    void stopActionPlayback()')
edit('include/QtScrcpyCore.h', '    virtual void stopActionPlayback() = 0;', '''    virtual bool playActionMacroAdvanced(int repeats, int interval, double speed, qint64 limitMs)
    { return speed == 1.0 && limitMs == 0 && playActionMacro(repeats, interval); }
    virtual bool pauseActionMacro() { return false; }
    virtual bool resumeActionMacro() { return false; }
    virtual bool isActionPaused() const { return false; }
    virtual bool actionMacroInterruptedInput() const { return false; }
    virtual qint64 actionMacroElapsedMs() const { return 0; }
    virtual void stopActionPlayback() = 0;''')
p = 'src/device/controller/controller.cpp'
edit(p, 'bool Controller::playActionMacro(int repeatCount, int intervalMs)\n{', '''bool Controller::playActionMacro(int repeatCount, int intervalMs)
{
    return playActionMacroAdvanced(repeatCount, intervalMs, 1.0, 0);
}

bool Controller::playActionMacroAdvanced(int repeatCount, int intervalMs, double speed, qint64 limitMs)
{''')
edit(p, 'm_actionMacro->play(repeatCount, intervalMs);', 'm_actionMacro->play(repeatCount, intervalMs, speed, limitMs);')
edit(p, 'void Controller::stopActionPlayback()', '''bool Controller::pauseActionMacro()
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

void Controller::stopActionPlayback()''')
p = 'src/device/device.cpp'
edit(p, 'void Device::stopActionPlayback()', '''bool Device::playActionMacroAdvanced(int repeats, int interval, double speed, qint64 limitMs)
{
    return !isCameraMode() && m_serverStartSuccess && m_controller
        && m_controller->playActionMacroAdvanced(repeats, interval, speed, limitMs);
}
bool Device::pauseActionMacro() { return m_controller && m_controller->pauseActionMacro(); }
bool Device::resumeActionMacro() { return m_serverStartSuccess && m_controller && m_controller->resumeActionMacro(); }
bool Device::isActionPaused() const { return m_controller && m_controller->isActionPaused(); }
bool Device::actionMacroInterruptedInput() const { return m_controller && m_controller->actionMacroInterruptedInput(); }
qint64 Device::actionMacroElapsedMs() const { return m_controller ? m_controller->actionMacroElapsedMs() : 0; }

void Device::stopActionPlayback()''')
print('Applied macro v2 implementation to checked source anchors.')
