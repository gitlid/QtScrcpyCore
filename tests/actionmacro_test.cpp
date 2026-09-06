#include <functional>
#include <memory>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include "actionmacro.h"
#include "controlmsg.h"

namespace {
bool check(bool value, const char *message)
{
    if (!value) { qCritical("FAILED: %s", message); }
    return value;
}
QJsonObject key(int action = 0)
{
    ControlMsg msg(ControlMsg::CMT_INJECT_KEYCODE);
    msg.setInjectKeycodeMsgData(static_cast<AndroidKeyeventAction>(action), AKEYCODE_A, 0, AMETA_NONE);
    return msg.toJson();
}
QJsonObject touch(int action = 0, int width = 100, int height = 200)
{
    ControlMsg msg(ControlMsg::CMT_INJECT_TOUCH);
    msg.setInjectTouchMsgData(POINTER_ID_MOUSE, static_cast<AndroidMotioneventAction>(action),
        static_cast<AndroidMotioneventButtons>(0), static_cast<AndroidMotioneventButtons>(0),
        QRect(10, 20, width, height), action == 1 ? 0.0f : 1.0f);
    return msg.toJson();
}
QJsonObject back(int action = 0)
{
    ControlMsg msg(ControlMsg::CMT_BACK_OR_SCREEN_ON);
    msg.setBackOrScreenOnData(action == 0);
    return msg.toJson();
}
QJsonObject at(QJsonValue time, const QJsonObject &message)
{
    return QJsonObject{{"atMs", time}, {"message", message}};
}
QJsonObject root(const QJsonArray &events, int duration = -1)
{
    QJsonObject result{{"format", "QtScrcpyActionMacro"}, {"version", 1}, {"events", events}};
    if (duration >= 0) { result["durationMs"] = duration; }
    return result;
}
struct Fixture {
    QTemporaryDir directory;
    QVector<QJsonObject> sent;
    ActionMacro macro;
    QString error;
    Fixture() : macro([this](ControlMsg *message) { sent.append(message->toJson()); delete message; }) {}
    bool load(const QJsonObject &object)
    {
        QFile file(directory.filePath("input.json"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.close();
        error.clear();
        return macro.load(file.fileName(), &error);
    }
    void record(const QJsonObject &json)
    {
        std::unique_ptr<ControlMsg> msg(ControlMsg::fromJson(json));
        if (msg) { macro.record(*msg); }
    }
};
bool run(ActionMacro &macro, int repeats = 1, int interval = 0)
{
    QEventLoop loop;
    bool stopped = false;
    const auto connection = QObject::connect(&macro, &ActionMacro::stateChanged, &loop,
        [&](bool, bool playing, int) { if (!playing) { stopped = true; loop.quit(); } });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    const bool started = macro.play(repeats, interval);
    if (started && !stopped) { loop.exec(); }
    QObject::disconnect(connection);
    return started && !macro.isPlaying();
}
void drain(int ms = 25)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool semanticRoundTrip()
{
    QVector<QJsonObject> samples{key(), key(1), touch(), touch(1), back(), back(1)};
    ControlMsg scroll(ControlMsg::CMT_INJECT_SCROLL);
    scroll.setInjectScrollMsgData(QRect(1, 2, 100, 200), 1.0f, -2.0f, static_cast<AndroidMotioneventButtons>(0));
    samples.append(scroll.toJson());
    ControlMsg text(ControlMsg::CMT_INJECT_TEXT);
    QString unicode = QString::fromUtf8("Hello, 中文");
    text.setInjectTextMsgData(unicode);
    samples.append(text.toJson());
    for (const QJsonObject &json : samples) {
        std::unique_ptr<ControlMsg> first(ControlMsg::fromJson(json));
        if (!first) { return false; }
        std::unique_ptr<ControlMsg> second(ControlMsg::fromJson(first->toJson()));
        if (!second || first->serializeData() != second->serializeData()) { return false; }
    }
    return true;
}
bool invalidTimestamps()
{
    const QVector<QJsonValue> bad{-1, -2, 0.5, 1e300, 86400001.0, "1", QJsonValue(), true};
    for (const QJsonValue &time : bad) {
        Fixture f;
        if (f.load(root(QJsonArray{at(time, key())}))) { return false; }
    }
    Fixture f;
    return f.load(root(QJsonArray{at(0, key()), at(86400000.0, key(1))}));
}
bool reversedTimestamps()
{
    Fixture f;
    return !f.load(root(QJsonArray{at(10, key()), at(9, key(1))}));
}
bool formatAndVersion()
{
    const QJsonObject valid = root(QJsonArray{at(0, key())});
    Fixture f;
    for (const QJsonValue &version : QVector<QJsonValue>{1.5, 3, "1", QJsonValue(), true}) {
        QJsonObject bad = valid;
        bad["version"] = version;
        if (f.load(bad)) { return false; }
    }
    QJsonObject bad = valid;
    bad["format"] = "other";
    return !f.load(bad);
}
bool invalidEventArrays()
{
    Fixture f;
    if (f.load(root(QJsonArray())) || f.load(root(QJsonArray{1}))) { return false; }
    QJsonObject bad = root(QJsonArray{at(0, key())});
    bad["events"] = "not an array";
    if (f.load(bad)) { return false; }
    QJsonArray many;
    const QJsonObject event = at(0, key(1));
    for (int i = 0; i < 100001; ++i) { many.append(event); }
    return !f.load(root(many));
}
bool coordinateBounds()
{
    Fixture f;
    for (const QString &field : {QString("x"), QString("y")}) {
        QJsonObject msg = touch();
        QJsonObject position = msg.value("position").toObject();
        position[field] = field == "x" ? 100 : 200;
        msg["position"] = position;
        if (f.load(root(QJsonArray{at(0, msg)}))) { return false; }
        position[field] = -1;
        msg["position"] = position;
        if (f.load(root(QJsonArray{at(0, msg)}))) { return false; }
    }
    return f.load(root(QJsonArray{at(0, touch())}));
}
bool mixedScreens()
{
    Fixture f;
    return !f.load(root(QJsonArray{at(0, touch()), at(1, touch(1, 200, 100))}));
}
bool badMetadata()
{
    Fixture f;
    QJsonObject data = root(QJsonArray{at(0, touch())});
    data["screen"] = QJsonObject{{"width", 200}, {"height", 100}};
    if (f.load(data)) { return false; }
    data["screen"] = QJsonObject{{"width", 100}, {"height", 200}, {"orientation", "landscape"}};
    if (f.load(data)) { return false; }
    data["screen"] = QJsonObject{{"width", 100}, {"height", 200}, {"orientation", "portrait"}};
    return f.load(data);
}
bool kindAndUnsupportedTypes()
{
    Fixture f;
    QJsonObject msg = key();
    msg["kind"] = "touch";
    if (f.load(root(QJsonArray{at(0, msg)}))) { return false; }
    ControlMsg rotate(ControlMsg::CMT_ROTATE_DEVICE);
    return !f.load(root(QJsonArray{at(0, rotate.toJson())}));
}
bool malformedPointerBearingMessages()
{
    // Regression for deleting uninitialized union pointers after validation fails.
    for (int type : {int(ControlMsg::CMT_INJECT_TEXT), int(ControlMsg::CMT_SET_CLIPBOARD),
                     int(ControlMsg::CMT_START_APP), int(ControlMsg::CMT_SCAN_FILE)}) {
        for (int i = 0; i < 100; ++i) {
            QString error;
            std::unique_ptr<ControlMsg> msg(ControlMsg::fromJson(QJsonObject{{"type", type}}, &error));
            if (msg || error.isEmpty()) { return false; }
        }
    }
    return true;
}
bool emptyClipboard()
{
    for (bool paste : {false, true}) {
        QJsonObject json{{"type", int(ControlMsg::CMT_SET_CLIPBOARD)}, {"text", ""}, {"paste", paste}};
        std::unique_ptr<ControlMsg> msg(ControlMsg::fromJson(json));
        if (!msg || msg->toJson().value("paste").toBool() != paste) { return false; }
        if (msg->serializeData().size() != 14) { return false; }
    }
    return true;
}
bool invalidText()
{
    Fixture f;
    for (const QString &text : {QString(301, 'a'), QString("a") + QChar(0) + QString("b"), QString(150, QChar(0x4e2d))}) {
        QJsonObject msg{{"type", int(ControlMsg::CMT_INJECT_TEXT)}, {"text", text}};
        if (f.load(root(QJsonArray{at(0, msg)}))) { return false; }
    }
    return true;
}
bool transactionalLoad()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(0, key()), at(1, key(1))}))) { return false; }
    if (f.load(root(QJsonArray{at(-1, key())}))) { return false; }
    return f.macro.eventCount() == 2 && run(f.macro) && f.sent.size() == 2;
}
bool malformedJsonAndLargeFile()
{
    Fixture f;
    const QString path = f.directory.filePath("invalid.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { return false; }
    file.write("{\"events\": [");
    file.close();
    if (f.macro.load(path)) { return false; }
    if (!file.open(QIODevice::WriteOnly) || !file.resize(64LL * 1024 * 1024 + 1)) { return false; }
    file.close();
    return !f.macro.load(path);
}
bool finiteRepeat()
{
    Fixture f;
    return f.load(root(QJsonArray{at(0, key()), at(2, key(1))}, 4))
        && run(f.macro, 3, 1) && f.sent.size() == 6;
}
bool legacyFormat()
{
    Fixture f;
    return f.load(root(QJsonArray{at(0, touch()), at(1, touch(1))}))
        && run(f.macro) && f.sent.size() == 2;
}
bool durationAndInterval()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(10, key()), at(20, key(1))}, 80))) { return false; }
    QElapsedTimer elapsed;
    elapsed.start();
    return run(f.macro, 2, 20) && elapsed.elapsed() >= 180 && f.sent.size() == 4;
}
bool invalidDuration()
{
    Fixture f;
    QJsonObject data = root(QJsonArray{at(10, key())});
    for (const QJsonValue &value : QVector<QJsonValue>{-1, 9, 10.5, "10", 1e300}) {
        data["durationMs"] = value;
        if (f.load(data)) { return false; }
    }
    return true;
}
bool stopReleasesAllInputTypes()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(0, key()), at(0, touch()), at(0, back()), at(1000, key(1))}, 1000))) { return false; }
    QObject::connect(&f.macro, &ActionMacro::progressChanged, &f.macro,
        [&f](int event, int, int, int) { if (event == 3) { f.macro.stopPlayback(); } });
    if (!run(f.macro) || f.sent.size() != 6) { return false; }
    for (int i = 3; i < 6; ++i) { if (f.sent.at(i).value("action").toInt() != 1) { return false; } }
    drain();
    return f.sent.size() == 6;
}
bool infiniteStop()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(0, key()), at(1, key(1))}, 2))) { return false; }
    QObject::connect(&f.macro, &ActionMacro::progressChanged, &f.macro,
        [&f](int event, int, int loop, int) { if (loop == 3 && event == 2) { f.macro.stopPlayback(); } });
    if (!run(f.macro, 0, 1)) { return false; }
    drain();
    return f.sent.size() == 6;
}
bool incompleteLoopRelease()
{
    Fixture f;
    return f.load(root(QJsonArray{at(0, key())}, 1)) && run(f.macro, 3, 1) && f.sent.size() == 6
        && f.sent.last().value("action").toInt() == 1;
}
bool recordingReleaseAndReload()
{
    Fixture f;
    f.macro.setCurrentScreen(QSize(100, 200));
    if (!f.macro.startRecording()) { return false; }
    f.record(key()); f.record(touch()); f.record(back());
    QThread::msleep(5);
    if (!f.macro.stopRecording() || f.sent.size() != 3 || f.macro.eventCount() != 6) { return false; }
    const QString path = f.directory.filePath("saved.json");
    if (!f.macro.save(path) || !f.macro.load(path)) { return false; }
    f.sent.clear();
    return run(f.macro) && f.sent.size() == 6;
}
bool geometryGuard()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(0, touch()), at(10, touch(1))}))) { return false; }
    f.macro.setCurrentScreen(QSize(200, 100));
    if (f.macro.play(1, 0) || !f.sent.isEmpty()) { return false; }
    f.macro.setCurrentScreen(QSize(100, 200));
    QObject::connect(&f.macro, &ActionMacro::progressChanged, &f.macro,
        [&f](int event, int, int, int) { if (event == 1) { f.macro.setCurrentScreen(QSize(200, 100)); } });
    return run(f.macro) && f.sent.size() == 2 && f.sent.last().value("action").toInt() == 1;
}
bool recordingGeometryChange()
{
    Fixture f;
    f.macro.setCurrentScreen(QSize(100, 200));
    f.macro.startRecording();
    f.record(touch());
    f.macro.setCurrentScreen(QSize(200, 100));
    return !f.macro.isRecording() && f.sent.size() == 1 && f.sent.first().value("action").toInt() == 1;
}
bool invalidPlayArguments()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(0, key())}))) { return false; }
    return !f.macro.play(-1, 0) && !f.macro.play(10000, 0)
        && !f.macro.play(1, -1) && !f.macro.play(1, 600001);
}
bool transportAbortReentrancy()
{
    QVector<QJsonObject> sent;
    ActionMacro *pointer = Q_NULLPTR;
    ActionMacro macro([&](ControlMsg *msg) {
        sent.append(msg->toJson()); delete msg;
        pointer->abort("simulated transport failure");
    });
    pointer = &macro;
    macro.startRecording();
    ControlMsg down(ControlMsg::CMT_INJECT_KEYCODE);
    down.setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, AKEYCODE_A, 0, AMETA_NONE);
    macro.record(down);
    macro.stopRecording();
    sent.clear();
    return run(macro) && sent.size() == 2;
}
bool overdueEventsStop()
{
    Fixture f;
    if (!f.load(root(QJsonArray{at(0, key()), at(1, key(1))}))) { return false; }
    bool error = false;
    QObject::connect(&f.macro, &ActionMacro::errorOccurred, &f.macro, [&](const QString &) { error = true; });
    if (!f.macro.play(1, 0)) { return false; }
    QThread::msleep(2100);
    drain();
    return error && !f.macro.isPlaying() && f.sent.isEmpty();
}
bool busyStateProtection()
{
    Fixture f;
    f.macro.startRecording();
    f.record(key());
    if (f.macro.startRecording() || f.macro.play(1, 0) || f.macro.save(f.directory.filePath("busy.json"))) { return false; }
    if (f.load(root(QJsonArray{at(0, key())}))) { return false; }
    return f.macro.stopRecording();
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QVector<QPair<const char *, std::function<bool()>>> tests{
        {"semantic_round_trip", semanticRoundTrip}, {"invalid_timestamps", invalidTimestamps},
        {"reversed_timestamps", reversedTimestamps}, {"format_and_version", formatAndVersion},
        {"invalid_event_arrays", invalidEventArrays}, {"coordinate_bounds", coordinateBounds},
        {"mixed_screens", mixedScreens}, {"metadata", badMetadata},
        {"kind_and_type", kindAndUnsupportedTypes}, {"malformed_pointer_fields", malformedPointerBearingMessages},
        {"empty_clipboard", emptyClipboard}, {"invalid_text", invalidText},
        {"transactional_load", transactionalLoad}, {"malformed_and_large_file", malformedJsonAndLargeFile},
        {"finite_repeat", finiteRepeat}, {"legacy_format", legacyFormat},
        {"duration_and_interval", durationAndInterval}, {"invalid_duration", invalidDuration},
        {"emergency_release", stopReleasesAllInputTypes}, {"infinite_stop", infiniteStop},
        {"incomplete_loop_release", incompleteLoopRelease}, {"recording_release_reload", recordingReleaseAndReload},
        {"geometry_guard", geometryGuard}, {"recording_geometry_change", recordingGeometryChange},
        {"play_arguments", invalidPlayArguments}, {"transport_reentrancy", transportAbortReentrancy},
        {"overdue_stop", overdueEventsStop}, {"busy_state", busyStateProtection}
    };
    int passed = 0;
    int executed = 0;
    for (const auto &test : tests) {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) != test.first) { continue; }
        ++executed;
        const bool success = test.second();
        check(success, test.first);
        qInfo("%s %s", success ? "PASS" : "FAIL", test.first);
        if (success) { ++passed; }
    }
    qInfo("Action macro regression results: %d/%d passed", passed, executed);
    return executed > 0 && passed == executed ? 0 : 1;
}
