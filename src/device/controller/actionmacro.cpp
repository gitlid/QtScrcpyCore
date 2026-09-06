#include "actionmacro.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include "controlmsg.h"

namespace {

const char kFormatName[] = "QtScrcpyActionMacro";
const int kFormatVersion = 1;
const int kMaximumEvents = 100000;
const qint64 kMaximumDurationMs = 24LL * 60 * 60 * 1000;

} // namespace

ActionMacro::ActionMacro(std::function<void(ControlMsg *)> dispatch, QObject *parent)
    : QObject(parent)
    , m_dispatch(dispatch)
{
    m_playbackTimer.setSingleShot(true);
    connect(&m_playbackTimer, &QTimer::timeout, this, &ActionMacro::onPlaybackTimer);
}

bool ActionMacro::startRecording()
{
    if (m_playing || m_recording) {
        return false;
    }
    m_events.clear();
    m_recordingTimer.start();
    m_recording = true;
    emit stateChanged(m_recording, m_playing, m_events.size());
    return true;
}

bool ActionMacro::stopRecording()
{
    if (!m_recording) {
        return false;
    }
    m_recording = false;
    emit stateChanged(m_recording, m_playing, m_events.size());
    return true;
}

bool ActionMacro::shouldRecord(const ControlMsg &message)
{
    switch (message.type()) {
    case ControlMsg::CMT_GET_CLIPBOARD:
    case ControlMsg::CMT_RESET_VIDEO:
    case ControlMsg::CMT_RESIZE_DISPLAY:
    case ControlMsg::CMT_SCAN_FILE:
        return false;
    default:
        return true;
    }
}

void ActionMacro::record(const ControlMsg &message)
{
    if (!m_recording || !shouldRecord(message)) {
        return;
    }
    if (m_events.size() >= kMaximumEvents) {
        m_recording = false;
        emit errorOccurred(tr("The action macro reached the 100,000 event safety limit."));
        emit stateChanged(m_recording, m_playing, m_events.size());
        return;
    }
    Event event;
    event.atMs = m_recordingTimer.elapsed();
    event.message = message.toJson();
    m_events.append(event);
    emit stateChanged(m_recording, m_playing, m_events.size());
}

bool ActionMacro::setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

QJsonObject ActionMacro::recordedScreen() const
{
    for (const Event &event : m_events) {
        const QJsonObject position = event.message.value("position").toObject();
        if (!position.isEmpty()) {
            const int width = position.value("width").toInt();
            const int height = position.value("height").toInt();
            if (width > 0 && height > 0) {
                QJsonObject screen;
                screen["width"] = width;
                screen["height"] = height;
                screen["orientation"] = width >= height ? "landscape" : "portrait";
                return screen;
            }
        }
    }
    return QJsonObject();
}

bool ActionMacro::save(const QString &fileName, QString *error) const
{
    if (m_recording || m_playing) {
        return setError(error, tr("Stop recording or playback before saving."));
    }
    if (m_events.isEmpty()) {
        return setError(error, tr("There are no recorded actions to save."));
    }

    QJsonArray events;
    for (const Event &event : m_events) {
        QJsonObject item;
        item["atMs"] = static_cast<double>(event.atMs);
        item["message"] = event.message;
        events.append(item);
    }

    QJsonObject root;
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;
    root["createdUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["screen"] = recordedScreen();
    root["events"] = events;

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        return setError(error, tr("Cannot open '%1' for writing: %2").arg(fileName, file.errorString()));
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        return setError(error, tr("Cannot save '%1': %2").arg(fileName, file.errorString()));
    }
    return true;
}

bool ActionMacro::load(const QString &fileName, QString *error)
{
    if (m_recording || m_playing) {
        return setError(error, tr("Stop recording or playback before loading."));
    }
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return setError(error, tr("Cannot open '%1': %2").arg(fileName, file.errorString()));
    }
    if (file.size() > 64 * 1024 * 1024) {
        return setError(error, tr("The action macro file is too large."));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return setError(error, tr("Invalid action macro JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != kFormatName || root.value("version").toInt(-1) != kFormatVersion) {
        return setError(error, tr("Unsupported action macro format or version."));
    }
    const QJsonValue eventsValue = root.value("events");
    if (!eventsValue.isArray()) {
        return setError(error, tr("The action macro does not contain an events array."));
    }
    const QJsonArray events = eventsValue.toArray();
    if (events.isEmpty() || events.size() > kMaximumEvents) {
        return setError(error, tr("The action macro must contain between 1 and 100,000 events."));
    }

    QVector<Event> loaded;
    loaded.reserve(events.size());
    qint64 previousAt = -1;
    for (int index = 0; index < events.size(); ++index) {
        if (!events.at(index).isObject()) {
            return setError(error, tr("Event %1 must be an object.").arg(index + 1));
        }
        const QJsonObject item = events.at(index).toObject();
        const QJsonValue atValue = item.value("atMs");
        const double atNumber = atValue.toDouble(-1);
        const qint64 atMs = static_cast<qint64>(atNumber);
        if (!atValue.isDouble() || atNumber != static_cast<double>(atMs) || atMs < previousAt || atMs > kMaximumDurationMs) {
            return setError(error, tr("Event %1 has an invalid timestamp.").arg(index + 1));
        }
        if (!item.value("message").isObject()) {
            return setError(error, tr("Event %1 does not contain a message object.").arg(index + 1));
        }
        QString messageError;
        ControlMsg *message = ControlMsg::fromJson(item.value("message").toObject(), &messageError);
        if (!message) {
            return setError(error, tr("Event %1 is invalid: %2").arg(index + 1).arg(messageError));
        }
        delete message;

        Event event;
        event.atMs = atMs;
        event.message = item.value("message").toObject();
        loaded.append(event);
        previousAt = atMs;
    }

    m_events = loaded;
    emit stateChanged(m_recording, m_playing, m_events.size());
    return true;
}

bool ActionMacro::play(int repeatCount, int intervalMs)
{
    if (m_recording || m_playing || m_events.isEmpty() || repeatCount < 0 || intervalMs < 0) {
        return false;
    }
    m_repeatCount = repeatCount;
    m_intervalMs = intervalMs;
    m_currentLoop = 1;
    m_eventIndex = 0;
    m_waitingForNextLoop = false;
    m_activeKeys.clear();
    m_activeTouches.clear();
    m_playing = true;
    m_loopTimer.start();
    emit stateChanged(m_recording, m_playing, m_events.size());
    emit progressChanged(0, m_events.size(), m_currentLoop, m_repeatCount);
    scheduleNext();
    return true;
}

void ActionMacro::scheduleNext()
{
    if (!m_playing) {
        return;
    }
    if (m_eventIndex >= m_events.size()) {
        if (m_repeatCount > 0 && m_currentLoop >= m_repeatCount) {
            finishPlayback();
            return;
        }
        // Never carry an incomplete key or touch hold across a loop gap.
        releaseActiveInputs();
        ++m_currentLoop;
        m_eventIndex = 0;
        m_waitingForNextLoop = true;
        m_playbackTimer.start(m_intervalMs);
        return;
    }
    const int delay = static_cast<int>(qMax<qint64>(0, m_events.at(m_eventIndex).atMs - m_loopTimer.elapsed()));
    m_playbackTimer.start(delay);
}

void ActionMacro::onPlaybackTimer()
{
    if (!m_playing) {
        return;
    }
    if (m_waitingForNextLoop) {
        m_waitingForNextLoop = false;
        m_loopTimer.restart();
        emit progressChanged(0, m_events.size(), m_currentLoop, m_repeatCount);
        scheduleNext();
        return;
    }
    dispatchCurrent();
    scheduleNext();
}

void ActionMacro::dispatchCurrent()
{
    if (m_eventIndex < 0 || m_eventIndex >= m_events.size()) {
        return;
    }
    const QJsonObject json = m_events.at(m_eventIndex).message;
    QString error;
    ControlMsg *message = ControlMsg::fromJson(json, &error);
    if (!message) {
        emit errorOccurred(tr("Playback stopped because an event is invalid: %1").arg(error));
        stopPlayback();
        return;
    }
    updateActiveInputs(json);
    if (m_dispatch) {
        m_dispatch(message);
    } else {
        delete message;
    }
    ++m_eventIndex;
    emit progressChanged(m_eventIndex, m_events.size(), m_currentLoop, m_repeatCount);
}

void ActionMacro::updateActiveInputs(const QJsonObject &message)
{
    const int type = message.value("type").toInt(-1);
    const int action = message.value("action").toInt(-1);
    if (type == ControlMsg::CMT_INJECT_KEYCODE) {
        const int keycode = message.value("keycode").toInt();
        if (action == AKEY_EVENT_ACTION_DOWN) {
            m_activeKeys[keycode] = message;
        } else if (action == AKEY_EVENT_ACTION_UP) {
            m_activeKeys.remove(keycode);
        }
    } else if (type == ControlMsg::CMT_INJECT_TOUCH) {
        const QString pointerId = message.value("pointerId").toString();
        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_MOVE) {
            m_activeTouches[pointerId] = message;
        } else if (action == AMOTION_EVENT_ACTION_UP) {
            m_activeTouches.remove(pointerId);
        }
    }
}

void ActionMacro::releaseActiveInputs()
{
    if (!m_dispatch) {
        m_activeKeys.clear();
        m_activeTouches.clear();
        return;
    }
    for (QJsonObject message : m_activeKeys) {
        message["action"] = static_cast<int>(AKEY_EVENT_ACTION_UP);
        message["repeat"] = 0;
        ControlMsg *release = ControlMsg::fromJson(message);
        if (release) {
            m_dispatch(release);
        }
    }
    for (QJsonObject message : m_activeTouches) {
        message["action"] = static_cast<int>(AMOTION_EVENT_ACTION_UP);
        message["actionButtons"] = 0;
        message["buttons"] = 0;
        message["pressure"] = 0.0;
        ControlMsg *release = ControlMsg::fromJson(message);
        if (release) {
            m_dispatch(release);
        }
    }
    m_activeKeys.clear();
    m_activeTouches.clear();
}

void ActionMacro::finishPlayback()
{
    releaseActiveInputs();
    m_playbackTimer.stop();
    m_playing = false;
    m_waitingForNextLoop = false;
    emit stateChanged(m_recording, m_playing, m_events.size());
}

void ActionMacro::stopPlayback()
{
    if (!m_playing) {
        return;
    }
    releaseActiveInputs();
    m_playbackTimer.stop();
    m_playing = false;
    m_waitingForNextLoop = false;
    emit stateChanged(m_recording, m_playing, m_events.size());
}
