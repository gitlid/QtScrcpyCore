#include "actionmacro.h"
#include <cmath>
#include <limits>
#include <memory>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include "controlmsg.h"

namespace {
const char kFormatName[] = "QtScrcpyActionMacro";
const int kMaximumEvents = 100000;
const int kReleaseReserve = 512;
const qint64 kMaximumDurationMs = 86400000;
const qint64 kMaximumBytes = 64LL * 1024 * 1024;
const qint64 kMaximumLatenessMs = 2000;
}

ActionMacro::ActionMacro(std::function<void(ControlMsg *)> dispatch, QObject *parent)
    : QObject(parent), m_dispatch(dispatch)
{
    m_playbackTimer.setSingleShot(true);
    m_playbackTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playbackTimer, &QTimer::timeout, this, &ActionMacro::onPlaybackTimer);
}

void ActionMacro::notifyState()
{
    emit stateChanged(m_recording, m_playing, m_events.size());
}

bool ActionMacro::setError(QString *error, const QString &message)
{
    if (error) { *error = message; }
    return false;
}

bool ActionMacro::timestamp(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) { return false; }
    const double number = value.toDouble();
    // Range checks MUST precede the floating-to-integer conversion.
    if (!std::isfinite(number) || number < 0 || number > kMaximumDurationMs
        || std::floor(number) != number) { return false; }
    *result = static_cast<qint64>(number);
    return true;
}

bool ActionMacro::shouldRecord(const ControlMsg &message)
{
    switch (message.type()) {
    case ControlMsg::CMT_UHID_INPUT:
    case ControlMsg::CMT_INJECT_KEYCODE:
    case ControlMsg::CMT_INJECT_TEXT:
    case ControlMsg::CMT_INJECT_TOUCH:
    case ControlMsg::CMT_INJECT_SCROLL:
    case ControlMsg::CMT_BACK_OR_SCREEN_ON:
    case ControlMsg::CMT_EXPAND_NOTIFICATION_PANEL:
    case ControlMsg::CMT_EXPAND_SETTINGS_PANEL:
    case ControlMsg::CMT_COLLAPSE_PANELS:
    case ControlMsg::CMT_SET_CLIPBOARD:
    case ControlMsg::CMT_START_APP:
        return true;
    default:
        return false;
    }
}

QSize ActionMacro::screenOf(const QJsonObject &message)
{
    const QJsonObject p = message.value("position").toObject();
    return p.isEmpty() ? QSize() : QSize(p.value("width").toInt(), p.value("height").toInt());
}

bool ActionMacro::normalize(const QJsonObject &source, QJsonObject *result, QString *error)
{
    std::unique_ptr<ControlMsg> message(ControlMsg::fromJson(source, error));
    if (!message) { return false; }
    if (!shouldRecord(*message)) {
        return setError(error, tr("This control-message type is not supported in an action macro."));
    }
    const QJsonObject canonical = message->toJson();
    if (source.contains("kind") && source.value("kind") != canonical.value("kind")) {
        return setError(error, tr("The event kind does not match its type."));
    }
    const QJsonObject position = canonical.value("position").toObject();
    if (!position.isEmpty()
        && (position.value("x").toInt() >= position.value("width").toInt()
            || position.value("y").toInt() >= position.value("height").toInt())) {
        return setError(error, tr("Coordinates must be strictly inside the recorded screen."));
    }
    // Never silently truncate text or replay a different string.
    for (const QString &field : {QString("text"), QString("name")}) {
        if (source.contains(field)) {
            const QString text = source.value(field).toString();
            if (text.contains(QChar(0)) || canonical.value(field) != source.value(field)) {
                return setError(error, tr("Text contains NUL or exceeds the supported length."));
            }
            const int maximum = field == "name" ? CONTROL_MSG_START_APP_MAX_LENGTH
                : (message->type() == ControlMsg::CMT_INJECT_TEXT ? CONTROL_MSG_INJECT_TEXT_MAX_LENGTH
                   : CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH);
            if (text.toUtf8().size() > maximum) {
                return setError(error, tr("UTF-8 text exceeds the protocol byte limit."));
            }
        }
    }
    *result = canonical;
    return true;
}

void ActionMacro::setCurrentScreen(const QSize &size)
{
    m_currentScreen = size;
    if ((m_recording || m_playing) && m_recordedScreen.isValid()
        && m_currentScreen != m_recordedScreen) {
        abort(tr("Screen size or orientation changed. Restore the original display or record again."));
    }
}

bool ActionMacro::startRecording()
{
    if (m_stopping || m_playing || m_recording) { return false; }
    releaseInputs();
    m_events.clear();
    m_durationMs = 0;
    m_encodedBytes = 1024;
    m_recordedScreen = m_currentScreen;
    m_recordingTimer.start();
    m_notificationTimer.start();
    m_recording = true;
    notifyState();
    return true;
}

bool ActionMacro::append(const QJsonObject &message, qint64 atMs)
{
    const qint64 cost = QJsonDocument(message).toJson(QJsonDocument::Compact).size() + 64;
    if (m_events.size() >= kMaximumEvents || m_encodedBytes + cost > kMaximumBytes) { return false; }
    Event event;
    event.atMs = atMs;
    event.message = message;
    m_events.append(event);
    m_encodedBytes += cost;
    return true;
}

void ActionMacro::record(const ControlMsg &message)
{
    // Observe ordinary messages AFTER the transport accepts them. Tracking
    // also covers manual holds immediately before recording starts.
    if (m_stopping || m_playing || !shouldRecord(message)) { return; }
    const QJsonObject raw = message.toJson();
    updateActiveInputs(raw);
    if (!m_recording) { return; }
    const qint64 atMs = m_recordingTimer.elapsed();
    if (atMs > kMaximumDurationMs || m_events.size() >= kMaximumEvents - kReleaseReserve
        || m_encodedBytes > kMaximumBytes - 512 * 1024) {
        abort(tr("Recording reached its duration, event-count or memory safety limit."));
        return;
    }
    QJsonObject json;
    QString error;
    if (!normalize(raw, &json, &error)) { abort(tr("Recording stopped: %1").arg(error)); return; }
    const QSize screen = screenOf(json);
    if (screen.isValid()) {
        if (m_recordedScreen.isValid() && screen != m_recordedScreen) {
            abort(tr("Recording stopped because the event screen dimensions changed."));
            return;
        }
        m_recordedScreen = screen;
    }
    if (m_activeKeys.size() > 256 || m_activeTouches.size() > 10 || !append(json, atMs)) {
        abort(tr("Recording reached the active-input or file-size safety limit."));
        return;
    }
    if (m_notificationTimer.elapsed() >= 50 || m_events.size() == 1) {
        m_notificationTimer.restart();
        notifyState();
    }
}

bool ActionMacro::stopRecording()
{
    if (!m_recording || m_stopping) { return false; }
    m_stopping = true;
    m_recording = false;
    m_durationMs = qMin(m_recordingTimer.elapsed(), kMaximumDurationMs);
    const QVector<QJsonObject> releases = takeReleases();
    for (const QJsonObject &release : releases) { append(release, m_durationMs); }
    dispatchReleases(releases);
    m_stopping = false;
    notifyState();
    return true;
}

bool ActionMacro::requiresUhidKeyboard() const
{
    for (const Event &event : m_events) {
        if (event.message.value("type").toInt() == ControlMsg::CMT_UHID_INPUT) { return true; }
    }
    return false;
}

bool ActionMacro::save(const QString &fileName, QString *error) const
{
    if (m_recording || m_playing || m_stopping) { return setError(error, tr("Stop the macro before saving.")); }
    if (m_events.isEmpty()) { return setError(error, tr("There are no actions to save.")); }
    QJsonArray events;
    for (const Event &event : m_events) {
        QJsonObject item;
        item["atMs"] = static_cast<double>(event.atMs);
        item["message"] = event.message;
        events.append(item);
    }
    QJsonObject screen;
    if (m_recordedScreen.isValid()) {
        screen["width"] = m_recordedScreen.width();
        screen["height"] = m_recordedScreen.height();
        screen["orientation"] = m_recordedScreen.width() >= m_recordedScreen.height() ? "landscape" : "portrait";
    }
    QJsonObject root;
    root["format"] = kFormatName;
    root["version"] = requiresUhidKeyboard() ? 2 : 1;
    root["createdUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["screen"] = screen;
    root["durationMs"] = static_cast<double>(m_durationMs);
    root["events"] = events;
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (data.size() > kMaximumBytes) { return setError(error, tr("The macro exceeds 64 MiB.")); }
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return setError(error, tr("Cannot save '%1': %2").arg(fileName, file.errorString()));
    }
    return true;
}

bool ActionMacro::load(const QString &fileName, QString *error)
{
    if (m_recording || m_playing || m_stopping) { return setError(error, tr("Stop the macro before loading.")); }
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) { return setError(error, file.errorString()); }
    if (file.size() > kMaximumBytes) { return setError(error, tr("The macro exceeds 64 MiB.")); }
    const QByteArray data = file.read(kMaximumBytes + 1);
    if (file.error() != QFile::NoError) { return setError(error, file.errorString()); }
    if (data.size() > kMaximumBytes || !file.atEnd()) { return setError(error, tr("The macro exceeds 64 MiB.")); }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return setError(error, tr("Invalid macro JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != kFormatName || !root.value("version").isDouble()
        || (root.value("version").toDouble() != 1.0 && root.value("version").toDouble() != 2.0)) {
        return setError(error, tr("Unsupported macro format or version."));
    }
    if (!root.value("events").isArray()) { return setError(error, tr("Events must be an array.")); }
    const QJsonArray events = root.value("events").toArray();
    if (events.isEmpty() || events.size() > kMaximumEvents) { return setError(error, tr("A macro needs 1 to 100,000 events.")); }
    QVector<Event> loaded;
    loaded.reserve(events.size());
    QSize recordedScreen;
    qint64 previousAt = 0;
    qint64 encodedBytes = 1024;
    for (int index = 0; index < events.size(); ++index) {
        if (!events.at(index).isObject()) { return setError(error, tr("Event %1 must be an object.").arg(index + 1)); }
        const QJsonObject item = events.at(index).toObject();
        Event event;
        if (!timestamp(item.value("atMs"), &event.atMs) || event.atMs < previousAt) {
            return setError(error, tr("Event %1 has an invalid timestamp.").arg(index + 1));
        }
        QString detail;
        if (!item.value("message").isObject() || !normalize(item.value("message").toObject(), &event.message, &detail)) {
            return setError(error, tr("Event %1 is invalid: %2").arg(index + 1).arg(detail));
        }
        if (event.message.value("type").toInt() == ControlMsg::CMT_UHID_INPUT
            && root.value("version").toInt() < 2) {
            return setError(error, tr("HID keyboard events require macro format version 2."));
        }
        const QSize screen = screenOf(event.message);
        if (screen.isValid()) {
            if (recordedScreen.isValid() && recordedScreen != screen) { return setError(error, tr("A macro cannot mix screen sizes.")); }
            recordedScreen = screen;
        }
        encodedBytes += QJsonDocument(event.message).toJson(QJsonDocument::Compact).size() + 64;
        if (encodedBytes > kMaximumBytes) { return setError(error, tr("The decoded macro exceeds its memory budget.")); }
        previousAt = event.atMs;
        loaded.append(event);
    }
    if (root.contains("screen")) {
        if (!root.value("screen").isObject()) { return setError(error, tr("Screen metadata must be an object.")); }
        const QJsonObject screen = root.value("screen").toObject();
        if (!screen.isEmpty()) {
            qint64 width = 0;
            qint64 height = 0;
            if (!timestamp(screen.value("width"), &width) || !timestamp(screen.value("height"), &height)
                || width < 1 || height < 1 || width > 65535 || height > 65535) {
                return setError(error, tr("Invalid screen metadata."));
            }
            const QSize declared(static_cast<int>(width), static_cast<int>(height));
            if (recordedScreen.isValid() && recordedScreen != declared) { return setError(error, tr("Screen metadata does not match the events.")); }
            if (screen.contains("orientation") && screen.value("orientation").toString()
                != (width >= height ? "landscape" : "portrait")) { return setError(error, tr("Invalid orientation metadata.")); }
            recordedScreen = declared;
        }
    }
    qint64 duration = previousAt;
    if (root.contains("durationMs") && (!timestamp(root.value("durationMs"), &duration) || duration < previousAt)) {
        return setError(error, tr("Invalid macro duration."));
    }
    // Commit only after every event validates: failed loads preserve the old macro.
    m_events = loaded;
    m_recordedScreen = recordedScreen;
    m_durationMs = duration;
    m_encodedBytes = encodedBytes;
    notifyState();
    return true;
}

bool ActionMacro::play(int repeatCount, int intervalMs)
{
    if (m_stopping || m_recording || m_playing || m_events.isEmpty()
        || repeatCount < 0 || repeatCount > 9999 || intervalMs < 0 || intervalMs > 600000) { return false; }
    if (m_recordedScreen.isValid() && m_currentScreen.isValid() && m_recordedScreen != m_currentScreen) {
        emit errorOccurred(tr("The macro screen size or orientation differs from the current display. Record again."));
        return false;
    }
    releaseInputs();
    m_repeatCount = repeatCount;
    m_intervalMs = intervalMs;
    m_currentLoop = 1;
    m_eventIndex = 0;
    m_waitingForNextLoop = false;
    m_playing = true;
    m_loopTimer.start();
    notifyState();
    emit progressChanged(0, m_events.size(), m_currentLoop, m_repeatCount);
    scheduleNext();
    return true;
}

void ActionMacro::scheduleNext()
{
    if (!m_playing) { return; }
    const qint64 due = m_eventIndex < m_events.size() ? m_events.at(m_eventIndex).atMs : m_durationMs;
    const qint64 delay = qMax<qint64>(0, due - m_loopTimer.elapsed());
    m_playbackTimer.start(static_cast<int>(delay));
}

void ActionMacro::onPlaybackTimer()
{
    if (!m_playing || m_stopping) { return; }
    if (m_waitingForNextLoop) {
        m_waitingForNextLoop = false;
        m_loopTimer.restart();
        emit progressChanged(0, m_events.size(), m_currentLoop, m_repeatCount);
        scheduleNext();
        return;
    }
    const qint64 due = m_eventIndex < m_events.size() ? m_events.at(m_eventIndex).atMs : m_durationMs;
    if (m_loopTimer.elapsed() < due) { scheduleNext(); return; }
    if (m_eventIndex >= m_events.size()) {
        if (m_repeatCount > 0 && m_currentLoop >= m_repeatCount) { finishPlayback(); return; }
        releaseInputs();
        if (!m_playing) { return; }
        if (m_currentLoop == std::numeric_limits<int>::max()) { abort(tr("Loop counter safety limit reached.")); return; }
        ++m_currentLoop;
        m_eventIndex = 0;
        m_waitingForNextLoop = true;
        m_playbackTimer.start(qMax(1, m_intervalMs));
        return;
    }
    if (m_loopTimer.elapsed() - due > kMaximumLatenessMs) {
        abort(tr("Playback fell more than 2 seconds behind, possibly after sleep or a blocked UI. Overdue clicks were not replayed."));
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
    if (m_dispatch) { m_dispatch(message); } else { delete message; abort(tr("No control transport is available.")); }
    if (!m_playing) { return; }
    ++m_eventIndex;
    emit progressChanged(m_eventIndex, m_events.size(), m_currentLoop, m_repeatCount);
    scheduleNext(); // Yield after each event so Stop remains responsive.
}

void ActionMacro::updateActiveInputs(const QJsonObject &message)
{
    const int type = message.value("type").toInt(-1);
    const int action = message.value("action").toInt(-1);
    if (type == ControlMsg::CMT_UHID_INPUT) {
        m_activeHidKeyboard = message.value("modifiers").toInt() || !message.value("keys").toArray().isEmpty()
            ? message : QJsonObject();
    } else if (type == ControlMsg::CMT_INJECT_KEYCODE) {
        const int keycode = message.value("keycode").toInt();
        if (action == AKEY_EVENT_ACTION_DOWN) { m_activeKeys[keycode] = message; }
        else if (action == AKEY_EVENT_ACTION_UP) { m_activeKeys.remove(keycode); }
    } else if (type == ControlMsg::CMT_BACK_OR_SCREEN_ON) {
        m_activeBack = action == AKEY_EVENT_ACTION_DOWN ? message : QJsonObject();
    } else if (type == ControlMsg::CMT_INJECT_TOUCH) {
        const QString pointerId = message.value("pointerId").toString();
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_MOVE) { m_activeTouches[pointerId] = message; }
        else if (action == AMOTION_EVENT_ACTION_UP) { m_activeTouches.remove(pointerId); }
    }
}

QVector<QJsonObject> ActionMacro::takeReleases()
{
    QVector<QJsonObject> releases;
    if (!m_activeHidKeyboard.isEmpty()) {
        m_activeHidKeyboard["modifiers"] = 0;
        m_activeHidKeyboard["keys"] = QJsonArray();
        releases.append(m_activeHidKeyboard);
        m_activeHidKeyboard = QJsonObject();
    }
    for (QJsonObject key : m_activeKeys) {
        key["action"] = static_cast<int>(AKEY_EVENT_ACTION_UP);
        key["repeat"] = 0;
        key["metastate"] = 0;
        releases.append(key);
    }
    for (QJsonObject touch : m_activeTouches) {
        touch["action"] = static_cast<int>(AMOTION_EVENT_ACTION_UP);
        touch["actionButtons"] = 0;
        touch["buttons"] = 0;
        touch["pressure"] = 0.0;
        releases.append(touch);
    }
    if (!m_activeBack.isEmpty()) {
        m_activeBack["action"] = static_cast<int>(AKEY_EVENT_ACTION_UP);
        releases.append(m_activeBack);
    }
    m_activeKeys.clear();
    m_activeTouches.clear();
    m_activeBack = QJsonObject();
    return releases;
}

void ActionMacro::dispatchReleases(const QVector<QJsonObject> &releases)
{
    for (const QJsonObject &release : releases) {
        ControlMsg *message = ControlMsg::fromJson(release);
        if (!message) { continue; }
        if (m_dispatch) { m_dispatch(message); } else { delete message; }
    }
}

void ActionMacro::releaseInputs()
{
    const bool wasStopping = m_stopping;
    m_stopping = true;
    dispatchReleases(takeReleases());
    m_stopping = wasStopping;
}

void ActionMacro::finishPlayback()
{
    if (m_stopping) { return; }
    // Invalidate timers and state BEFORE invoking transport or callbacks.
    m_playbackTimer.stop();
    m_playing = false;
    m_waitingForNextLoop = false;
    releaseInputs();
    notifyState();
}

void ActionMacro::stopPlayback()
{
    if (m_playing) { finishPlayback(); }
}

void ActionMacro::abort(const QString &reason)
{
    if (m_stopping) { return; }
    stopPlayback();
    stopRecording();
    releaseInputs();
    emit errorOccurred(reason); // No device-driving work remains active.
}
