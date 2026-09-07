#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <functional>
#include <stdexcept>

#include "controller.h"
#include "controlmsg.h"

namespace {
const QSize screen(1000, 800);
const QPointF cursor(700, 500);
void require(bool ok, const char *message) {
    if (!ok) { throw std::runtime_error(message); }
}
void flush() { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); }
void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
QString profile(const QString &binding = "BackButton") {
    const QJsonObject node{{"type", "KMT_CLICK"}, {"key", binding},
        {"pos", QJsonObject{{"x", .2}, {"y", .3}}}, {"switchMap", false}};
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"switchKey", "Key_QuoteLeft"}, {"mouseLookEnabled", false},
        {"keyMapNodes", QJsonArray{node}}}).toJson());
}
struct Fixture {
    QVector<QByteArray> sent;
    Controller controller;
    explicit Fixture(const QString &script = QString())
        : controller([this](const QByteArray &bytes) {
            sent.append(bytes); return qint64(bytes.size());
        }, script) { controller.setFrameSize(screen); }
    void key(int key, bool down) {
        QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, key, Qt::NoModifier);
        controller.keyEvent(&event, screen, screen); flush();
    }
    void enableMapping() {
        key(Qt::Key_QuoteLeft, true); key(Qt::Key_QuoteLeft, false);
        require(controller.isCurrentCustomKeymap(), "mapping must be enabled");
        sent.clear();
    }
    void mouse(QEvent::Type type, Qt::MouseButton button = Qt::LeftButton,
               QPointF position = cursor, QSize shown = screen) {
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::MouseButtons(Qt::NoButton)
            : type == QEvent::MouseMove ? Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(button);
        QMouseEvent event(type, position, button, buttons, Qt::NoModifier);
        controller.mouseEvent(&event, screen, shown); flush();
    }
    void doubleClick(Qt::MouseButton button = Qt::LeftButton, QSize shown = screen) {
        // QWidget receives DblClick INSTEAD OF the second Press. Do not send both.
        mouse(QEvent::MouseButtonPress, button, cursor, shown);
        mouse(QEvent::MouseButtonRelease, button, cursor, shown);
        mouse(QEvent::MouseButtonDblClick, button, cursor, shown);
        mouse(QEvent::MouseButtonRelease, button, cursor, shown);
    }
    QVector<QByteArray> touches() const {
        QVector<QByteArray> result;
        for (const auto &bytes : sent) {
            if (!bytes.isEmpty() && quint8(bytes.at(0)) == ControlMsg::CMT_INJECT_TOUCH) {
                require(bytes.size() == 32, "touch wire length"); result.append(bytes);
            }
        }
        return result;
    }
};
void expect(const Fixture &f, const QVector<int> &actions, int x = 700, int y = 500) {
    const auto touches = f.touches();
    require(touches.size() == actions.size(), "complete touch DOWN/UP sequence required");
    for (int i = 0; i < touches.size(); ++i) {
        const auto *data = reinterpret_cast<const uchar *>(touches[i].constData());
        require(data[1] == actions[i], "touch action order");
        require(int(qFromBigEndian<quint32>(data + 10)) == x, "touch x");
        require(int(qFromBigEndian<quint32>(data + 14)) == y, "touch y");
        require(qFromBigEndian<quint16>(data + 18) == screen.width(), "frame width");
        require(qFromBigEndian<quint16>(data + 20) == screen.height(), "frame height");
        if (actions[i] == 0) { require(qFromBigEndian<quint16>(data + 22) > 0, "press pressure"); }
        if (actions[i] == 1) { require(qFromBigEndian<quint16>(data + 22) == 0, "release pressure"); }
        if (i) { require(touches[i].mid(2, 8) == touches[0].mid(2, 8), "paired pointer identity"); }
    }
}
void plain() { Fixture f; f.doubleClick(); expect(f, {0,1,0,1}); }
void disabled() { Fixture f(profile()); f.doubleClick(); expect(f, {0,1,0,1}); }
void unbound() { Fixture f(profile()); f.enableMapping(); f.doubleClick(); expect(f, {0,1,0,1}); }
void mapped() { Fixture f(profile("LeftButton")); f.enableMapping(); f.doubleClick(); expect(f, {0,1,0,1}, 200, 240); }
void scaled() {
    Fixture f;
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                          QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
        f.mouse(type, Qt::LeftButton, QPointF(350,250), QSize(500,400));
    }
    expect(f, {0,1,0,1});
}
void secondDrag() {
    Fixture f;
    f.mouse(QEvent::MouseButtonPress); f.mouse(QEvent::MouseButtonRelease);
    f.mouse(QEvent::MouseButtonDblClick);
    // Holding the second click is a real held contact, not an injected extra tap.
    expect(f, {0,1,0});
    f.mouse(QEvent::MouseMove, Qt::NoButton);
    f.mouse(QEvent::MouseButtonRelease);
    expect(f, {0,1,0,2,1});
}
void burst() {
    Fixture f; QVector<int> actions;
    for (int i = 0; i < 20; ++i) { f.doubleClick(); actions << 0 << 1 << 0 << 1; }
    expect(f, actions); // No throttle, synthetic clicks, or delayed dispatch.
}
void otherButtons() {
    for (const auto &script : {QString(), profile()}) {
        Fixture f(script); if (!script.isEmpty()) { f.enableMapping(); }
        for (const auto button : {Qt::RightButton, Qt::MiddleButton, Qt::ForwardButton, Qt::TaskButton}) {
            f.doubleClick(button);
        }
        require(f.touches().isEmpty(), "unbound non-primary buttons must not tap");
        f.doubleClick(); expect(f, {0,1,0,1});
    }
}
void uhid() {
    Fixture f(profile()); f.controller.setUhidKeyboardEnabled(true); f.enableMapping();
    f.doubleClick(); expect(f, {0,1,0,1});
    require(f.sent.size() == 4, "clicks must not create keyboard events");
    f.key(Qt::Key_A, true); f.key(Qt::Key_A, false);
    int reports = 0;
    for (const auto &bytes : f.sent) { if (quint8(bytes.at(0)) == ControlMsg::CMT_UHID_INPUT) { ++reports; } }
    require(reports == 2, "unbound UHID keyboard still works");
}
void macroRoundtrip() {
    Fixture f; QTemporaryDir directory;
    require(directory.isValid(), "temporary directory");
    require(f.controller.startActionRecording(), "start recording");
    f.doubleClick(); wait(10);
    require(f.controller.stopActionRecording(), "stop recording");
    const QString fileName = directory.filePath("doubleclick.json");
    require(f.controller.saveActionMacro(fileName), "save macro");
    QFile file(fileName); require(file.open(QIODevice::ReadOnly), "read saved macro");
    const auto events = QJsonDocument::fromJson(file.readAll()).object()["events"].toArray();
    require(events.size() == 4, "record both complete clicks exactly once");
    for (int i = 0; i < events.size(); ++i) {
        const auto message = events[i].toObject()["message"].toObject();
        require(message["type"].toInt(-1) == 2, "record touch semantics");
        require(message["action"].toInt(-1) == (i % 2), "record down/up order");
    }
    require(f.controller.loadActionMacro(fileName), "reload macro"); f.sent.clear();
    require(f.controller.playActionMacro(1, 0), "replay macro");
    QElapsedTimer timer; timer.start();
    while (f.controller.isActionPlaying() && timer.elapsed() < 2000) { wait(5); }
    require(!f.controller.isActionPlaying(), "playback must finish");
    expect(f, {0,1,0,1});
}
void stopReleases() {
    Fixture f; require(f.controller.startActionRecording(), "start recording");
    f.mouse(QEvent::MouseButtonPress); f.mouse(QEvent::MouseButtonRelease);
    f.mouse(QEvent::MouseButtonDblClick); expect(f, {0,1,0});
    require(f.controller.stopActionRecording(), "stop recording"); flush();
    expect(f, {0,1,0,1});
}
void invalidGeometry() {
    Fixture f;
    QMouseEvent event(QEvent::MouseButtonDblClick, cursor, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    f.controller.mouseEvent(&event, QSize(), screen);
    f.controller.mouseEvent(&event, screen, QSize()); flush();
    require(f.touches().isEmpty(), "invalid geometry must not inject");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QVector<QPair<QString, std::function<void()>>> cases{
        {"plain",plain}, {"disabled",disabled}, {"unbound",unbound}, {"mapped",mapped},
        {"scaled",scaled}, {"second_drag",secondDrag}, {"burst",burst},
        {"other_buttons",otherButtons}, {"uhid",uhid}, {"macro",macroRoundtrip},
        {"stop_release",stopReleases}, {"invalid_geometry",invalidGeometry}};
    int ran = 0, failed = 0;
    for (const auto &test : cases) {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) != test.first) { continue; }
        ++ran;
        try { test.second(); qInfo() << test.first << "PASS"; }
        catch (const std::exception &error) { ++failed; qCritical() << test.first << "FAIL:" << error.what(); }
    }
    return ran > 0 && failed == 0 ? 0 : 1;
}
