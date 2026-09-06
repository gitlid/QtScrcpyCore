#ifndef ACTIONMACRO_H
#define ACTIONMACRO_H

#include <functional>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <QVector>

class ControlMsg;

// Format v1: optional durationMs preserves the final idle time.
class ActionMacro : public QObject
{
    Q_OBJECT
public:
    explicit ActionMacro(std::function<void(ControlMsg *)> dispatch, QObject *parent = Q_NULLPTR);
    bool startRecording();
    bool stopRecording();
    void record(const ControlMsg &message);
    bool save(const QString &fileName, QString *error = Q_NULLPTR) const;
    bool load(const QString &fileName, QString *error = Q_NULLPTR);
    bool play(int repeatCount, int intervalMs);
    bool play(int repeatCount, int intervalMs, double speed, qint64 limitMs);
    bool pause();
    bool resume();
    bool isPaused() const { return m_paused || m_recordingPaused; }
    bool interruptedInput() const { return m_interruptedInput; }
    qint64 activeElapsedMs() const;
    double playbackSpeed() const { return m_speed; }
    void stopPlayback();
    void abort(const QString &reason);
    void releaseInputs();
    void setCurrentScreen(const QSize &size);
    bool isRecording() const { return m_recording; }
    bool isPlaying() const { return m_playing; }
    bool requiresUhidKeyboard() const;
    int eventCount() const { return m_events.size(); }

signals:
    void stateChanged(bool recording, bool playing, int eventCount);
    void progressChanged(int currentEvent, int totalEvents, int currentLoop, int totalLoops);
    void errorOccurred(const QString &message);

private slots:
    void onPlaybackTimer();

private:
    struct Event {
        qint64 atMs = 0;
        QJsonObject message;
    };
    static bool shouldRecord(const ControlMsg &message);
    static bool setError(QString *error, const QString &message);
    static bool timestamp(const QJsonValue &value, qint64 *result);
    static bool normalize(const QJsonObject &source, QJsonObject *result, QString *error);
    static QSize screenOf(const QJsonObject &message);
    bool append(const QJsonObject &message, qint64 atMs);
    void scheduleNext();
    qint64 phaseElapsedNs() const;
    qint64 recordingElapsedMs() const;
    bool hasActiveInputs() const;
    void prepareResumeBoundary();
    void updateActiveInputs(const QJsonObject &message);
    QVector<QJsonObject> takeReleases();
    void dispatchReleases(const QVector<QJsonObject> &releases);
    void finishPlayback();
    void notifyState();

    std::function<void(ControlMsg *)> m_dispatch;
    QVector<Event> m_events;
    QElapsedTimer m_recordingTimer;
    QElapsedTimer m_notificationTimer;
    QElapsedTimer m_loopTimer;
    QTimer m_playbackTimer;
    QHash<int, QJsonObject> m_activeKeys;
    QHash<QString, QJsonObject> m_activeTouches;
    QJsonObject m_activeBack;
    QJsonObject m_activeHidKeyboard;
    QSize m_recordedScreen;
    QSize m_currentScreen;
    qint64 m_durationMs = 0;
    qint64 m_encodedBytes = 1024;
    // Paused playback still owns the device: m_playing remains true.
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
    bool m_recording = false;
    bool m_playing = false;
    bool m_stopping = false;
    bool m_waitingForNextLoop = false;
    int m_eventIndex = 0;
    int m_currentLoop = 0;
    int m_repeatCount = 1;
    int m_intervalMs = 0;
};
#endif
