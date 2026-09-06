#ifndef ACTIONMACRO_H
#define ACTIONMACRO_H

#include <functional>

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

class ControlMsg;

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
    void stopPlayback();

    bool isRecording() const { return m_recording; }
    bool isPlaying() const { return m_playing; }
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
    void scheduleNext();
    void dispatchCurrent();
    void updateActiveInputs(const QJsonObject &message);
    void releaseActiveInputs();
    void finishPlayback();
    QJsonObject recordedScreen() const;

    std::function<void(ControlMsg *)> m_dispatch;
    QVector<Event> m_events;
    QElapsedTimer m_recordingTimer;
    QElapsedTimer m_loopTimer;
    QTimer m_playbackTimer;
    QHash<int, QJsonObject> m_activeKeys;
    QHash<QString, QJsonObject> m_activeTouches;
    bool m_recording = false;
    bool m_playing = false;
    bool m_waitingForNextLoop = false;
    int m_eventIndex = 0;
    int m_currentLoop = 0;
    int m_repeatCount = 1;
    int m_intervalMs = 0;
};

#endif // ACTIONMACRO_H
