#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "actionmacro.h"
#include "controlmsg.h"

namespace {

bool check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("FAILED: %s", message);
    }
    return condition;
}

void configureTouchMessage(ControlMsg &message, AndroidMotioneventAction action)
{
    message.setInjectTouchMsgData(POINTER_ID_MOUSE, action,
                                  action == AMOTION_EVENT_ACTION_DOWN ? AMOTION_EVENT_BUTTON_PRIMARY : static_cast<AndroidMotioneventButtons>(0),
                                  action == AMOTION_EVENT_ACTION_UP ? static_cast<AndroidMotioneventButtons>(0) : AMOTION_EVENT_BUTTON_PRIMARY,
                                  QRect(100, 200, 1080, 2400), action == AMOTION_EVENT_ACTION_UP ? 0.0f : 1.0f);
}

bool testSemanticRoundTrip()
{
    ControlMsg original(ControlMsg::CMT_INJECT_KEYCODE);
    original.setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, AKEYCODE_HOME, 3, AMETA_SHIFT_ON);
    const QJsonObject json = original.toJson();
    QString error;
    ControlMsg *restored = ControlMsg::fromJson(json, &error);
    const bool ok = check(restored != Q_NULLPTR, qPrintable(error))
            && check(json.value("kind").toString() == "key", "semantic kind is missing")
            && check(restored->serializeData() == original.serializeData(), "key message round trip changed wire data");
    delete restored;
    return ok;
}

bool testSaveLoadAndRepeat()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory could not be created")) {
        return false;
    }

    QVector<QJsonObject> dispatched;
    ActionMacro macro([&dispatched](ControlMsg *message) {
        dispatched.append(message->toJson());
        delete message;
    });
    if (!check(macro.startRecording(), "recording did not start")) {
        return false;
    }
    ControlMsg down(ControlMsg::CMT_INJECT_TOUCH);
    ControlMsg up(ControlMsg::CMT_INJECT_TOUCH);
    configureTouchMessage(down, AMOTION_EVENT_ACTION_DOWN);
    configureTouchMessage(up, AMOTION_EVENT_ACTION_UP);
    macro.record(down);
    QThread::msleep(5);
    macro.record(up);
    macro.stopRecording();

    const QString fileName = directory.filePath("macro.qsmacro.json");
    QString error;
    if (!check(macro.save(fileName, &error), qPrintable(error))) {
        return false;
    }
    QFile file(fileName);
    if (!check(file.open(QIODevice::ReadOnly), "saved macro could not be reopened")) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (!check(root.value("format").toString() == "QtScrcpyActionMacro", "format marker is missing")
        || !check(root.value("screen").toObject().value("width").toInt() == 1080, "screen metadata is missing")) {
        return false;
    }

    ActionMacro loaded([&dispatched](ControlMsg *message) {
        dispatched.append(message->toJson());
        delete message;
    });
    if (!check(loaded.load(fileName, &error), qPrintable(error))) {
        return false;
    }
    QEventLoop loop;
    bool playbackStarted = false;
    QObject::connect(&loaded, &ActionMacro::stateChanged, &loop,
                     [&loop, &playbackStarted](bool, bool playing, int) {
        playbackStarted = playbackStarted || playing;
        if (playbackStarted && !playing) {
            loop.quit();
        }
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    if (!check(loaded.play(2, 1), "repeat playback did not start")) {
        return false;
    }
    loop.exec();
    return check(!loaded.isPlaying(), "repeat playback did not finish")
            && check(dispatched.size() == 4, "repeat playback dispatched the wrong number of events");
}

bool testEmergencyRelease()
{
    QVector<QJsonObject> dispatched;
    ActionMacro macro([&dispatched](ControlMsg *message) {
        dispatched.append(message->toJson());
        delete message;
    });
    macro.startRecording();
    ControlMsg down(ControlMsg::CMT_INJECT_TOUCH);
    ControlMsg up(ControlMsg::CMT_INJECT_TOUCH);
    configureTouchMessage(down, AMOTION_EVENT_ACTION_DOWN);
    configureTouchMessage(up, AMOTION_EVENT_ACTION_UP);
    macro.record(down);
    QThread::msleep(100);
    macro.record(up);
    macro.stopRecording();

    QEventLoop loop;
    bool playbackStarted = false;
    QObject::connect(&macro, &ActionMacro::stateChanged, &loop,
                     [&loop, &playbackStarted](bool, bool playing, int) {
        playbackStarted = playbackStarted || playing;
        if (playbackStarted && !playing) {
            loop.quit();
        }
    });
    if (!check(macro.play(1, 0), "emergency-stop playback did not start")) {
        return false;
    }
    QTimer::singleShot(20, &macro, &ActionMacro::stopPlayback);
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    loop.exec();

    if (!check(!macro.isPlaying(), "emergency stop did not stop playback")
        || !check(dispatched.size() == 2, "emergency stop did not append exactly one release")) {
        return false;
    }
    return check(dispatched.first().value("action").toInt() == AMOTION_EVENT_ACTION_DOWN, "first event was not touch down")
            && check(dispatched.last().value("action").toInt() == AMOTION_EVENT_ACTION_UP, "held touch was not released");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const bool success = testSemanticRoundTrip() && testSaveLoadAndRepeat() && testEmergencyRelease();
    if (success) {
        qInfo("All action macro tests passed.");
    }
    return success ? 0 : 1;
}
