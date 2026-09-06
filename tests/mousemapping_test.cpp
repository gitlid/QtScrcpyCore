#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QMetaEnum>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <functional>
#include "controller.h"
#include "controlmsg.h"

namespace {
const QSize screen(1000, 800);
void drain(int ms = 15) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
QJsonObject pos(double x, double y) { return {{"x", x}, {"y", y}}; }
QJsonObject click(const QString &key, double x = .2) {
    return {{"type", "KMT_CLICK"}, {"key", key}, {"pos", pos(x, .3)}, {"switchMap", false}};
}
QString script(const QJsonArray &nodes, const QString &toggle = "Key_QuoteLeft") {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"switchKey", toggle}, {"keyMapNodes", nodes}}).toJson());
}
void key(Controller &c, int k, bool down) {
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, k, Qt::NoModifier);
    c.keyEvent(&event, screen, screen); drain();
}
void mouse(Controller &c, Qt::MouseButton b, bool down) {
    QMouseEvent event(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                      QPointF(900, 700), b, down ? Qt::MouseButtons(b) : Qt::NoButton, Qt::NoModifier);
    c.mouseEvent(&event, screen, screen); drain();
}
QVector<QByteArray> touches(const QVector<QByteArray> &sent) {
    QVector<QByteArray> result; for (const auto &b : sent) { if (b.size() == 32 && b[0] == 2) { result.append(b); } }
    return result;
}
int action(const QByteArray &b) { return b.size() > 1 ? quint8(b[1]) : -1; }
int coord(const QByteArray &b, int offset) { return int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + offset))); }
QByteArray pointer(const QByteArray &b) { return b.mid(2, 8); }
struct Fixture {
    QVector<QByteArray> sent;
    Controller c;
    explicit Fixture(const QJsonArray &nodes, const QString &toggle = "Key_QuoteLeft")
        : c([this](const QByteArray &b) { sent.append(b); return qint64(b.size()); }, script(nodes, toggle)) {
        c.setFrameSize(screen);
        if (toggle == "Key_QuoteLeft") { key(c, Qt::Key_QuoteLeft, true); key(c, Qt::Key_QuoteLeft, false); }
        sent.clear();
    }
};
bool buttonClick(const QString &name, Qt::MouseButton button) {
    Fixture f({click(name)}); mouse(f.c, button, true); drain(35); mouse(f.c, button, false);
    auto t = touches(f.sent);
    return t.size() == 2 && action(t[0]) == 0 && action(t[1]) == 1 && pointer(t[0]) == pointer(t[1])
        && coord(t[0], 10) == 200 && coord(t[0], 14) == 240;
}
bool drag() {
    Fixture f({QJsonObject{{"type", "KMT_DRAG"}, {"key", "BackButton"}, {"startPos", pos(.2,.3)},
        {"endPos",pos(.5,.3)}, {"dragSpeed",1.}, {"startDelay",0}}});
    mouse(f.c, Qt::BackButton, true); mouse(f.c, Qt::BackButton, false); drain(400);
    auto t=touches(f.sent); return t.size() > 3 && action(t.first()) == 0 && action(t.last()) == 1
        && coord(t.first(),10) == 200 && coord(t.last(),10) > 400;
}
bool multi() {
    Fixture f({QJsonObject{{"type", "KMT_CLICK_MULTI"}, {"key", "ForwardButton"}, {"clickNodes", QJsonArray{
        QJsonObject{{"delay",0},{"pos",pos(.2,.3)}}, QJsonObject{{"delay",20},{"pos",pos(.7,.4)}}}}}});
    mouse(f.c, Qt::ForwardButton, true); mouse(f.c, Qt::ForwardButton, false); drain(150);
    const auto t=touches(f.sent); return t.size()==4 && action(t[0])==0 && action(t[1])==1
        && action(t[2])==0 && action(t[3])==1 && coord(t[2],10)==700;
}
bool twice() {
    Fixture f({QJsonObject{{"type","KMT_CLICK_TWICE"},{"key","RightButton"},{"pos",pos(.2,.3)}}});
    mouse(f.c,Qt::RightButton,true); mouse(f.c,Qt::RightButton,false);
    const auto t=touches(f.sent);return t.size()==4&&action(t[0])==0&&action(t[1])==1&&action(t[2])==0&&action(t[3])==1;
}
bool androidKey() {
    Fixture f({QJsonObject{{"type","KMT_ANDROID_KEY"},{"key","BackButton"},{"androidKey",3}}});
    mouse(f.c,Qt::BackButton,true);mouse(f.c,Qt::BackButton,false);
    return f.sent.size()==2&&f.sent[0][0]==0&&f.sent[1][0]==0&&action(f.sent[0])==0&&action(f.sent[1])==1;
}
bool joystick() {
    Fixture f({QJsonObject{{"type","KMT_STEER_WHEEL"},{"centerPos",pos(.3,.5)},
        {"leftKey","BackButton"},{"rightKey","ForwardButton"},{"upKey","Key_W"},{"downKey","Key_S"},
        {"leftOffset",.1},{"rightOffset",.1},{"upOffset",.1},{"downOffset",.1}}});
    mouse(f.c,Qt::BackButton,true);drain(150);mouse(f.c,Qt::BackButton,false);
    auto t=touches(f.sent);if(t.size()<3||coord(t.last(),10)>=300||action(t.last())!=1)return false;
    f.sent.clear();mouse(f.c,Qt::ForwardButton,true);drain(150);mouse(f.c,Qt::ForwardButton,false);
    t=touches(f.sent);return t.size()>=3&&coord(t.last(),10)>300&&action(t.last())==1;
}
bool namespaces() {
    Fixture f({click("Key_Space",.2),click("TaskButton",.7)});
    key(f.c,Qt::Key_Space,true);mouse(f.c,Qt::TaskButton,true);key(f.c,Qt::Key_Space,false);mouse(f.c,Qt::TaskButton,false);
    const auto t=touches(f.sent);return t.size()==4&&pointer(t[0])!=pointer(t[1])&&pointer(t[0])==pointer(t[2])&&pointer(t[1])==pointer(t[3]);
}
bool moveNoLeak() {
    Fixture f({click("LeftButton")});mouse(f.c,Qt::LeftButton,true);
    QMouseEvent event(QEvent::MouseMove,QPointF(500,500),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    f.c.mouseEvent(&event,screen,screen);drain();mouse(f.c,Qt::LeftButton,false);
    return touches(f.sent).size()==2;
}
bool toggle() {
    for(const auto button : {Qt::LeftButton,Qt::RightButton,Qt::MiddleButton,Qt::BackButton,Qt::ForwardButton}) {
        const QString name=QString::fromLatin1(QMetaEnum::fromType<Qt::MouseButtons>().valueToKey(button));
        Fixture f({click("Key_F")},name);mouse(f.c,button,true);mouse(f.c,button,false);
        if(!f.c.isCurrentCustomKeymap()||!touches(f.sent).isEmpty())return false;
        key(f.c,Qt::Key_F,true);mouse(f.c,button,true);mouse(f.c,button,false);
        const auto t=touches(f.sent);if(f.c.isCurrentCustomKeymap()||t.size()!=2||action(t.last())!=1)return false;
    }
    return true;
}
bool simultaneous() {
    Fixture f({click("BackButton",.2),click("ForwardButton",.7)});
    mouse(f.c,Qt::BackButton,true);mouse(f.c,Qt::ForwardButton,true);mouse(f.c,Qt::BackButton,false);mouse(f.c,Qt::ForwardButton,false);
    const auto t=touches(f.sent);return t.size()==4&&pointer(t[0])!=pointer(t[1])&&pointer(t[0])==pointer(t[2])&&pointer(t[1])==pointer(t[3]);
}
bool macroRoundtrip(bool pause) {
    Fixture f({click("BackButton")});QTemporaryDir dir;const QString path=dir.filePath("mouse.json");
    if(!f.c.startActionRecording())return false;
    mouse(f.c,Qt::BackButton,true);drain(160);mouse(f.c,Qt::BackButton,false);
    if(!f.c.stopActionRecording()||!f.c.saveActionMacro(path)||!f.c.loadActionMacro(path))return false;
    f.sent.clear();if(!f.c.playActionMacroAdvanced(pause?0:2,20,1.,0))return false;
    drain(40);
    if(pause) {
        if(!f.c.pauseActionMacro()||!f.c.isActionPaused())return false;
        auto t=touches(f.sent);if(t.size()!=2||action(t.last())!=1)return false;
        const int size=f.sent.size();drain(200);if(size!=f.sent.size())return false;
        f.c.stopActionPlayback();return !f.c.isActionPlaying();
    }
    drain(650);const auto t=touches(f.sent);return !f.c.isActionPlaying()&&t.size()==4;
}
bool toggleCancelsDelayed() {
    Fixture f({QJsonObject{{"type","KMT_CLICK_MULTI"},{"key","BackButton"},{"clickNodes",QJsonArray{
        QJsonObject{{"delay",200},{"pos",pos(.2,.3)}}}}}});
    mouse(f.c,Qt::BackButton,true);key(f.c,Qt::Key_QuoteLeft,true);key(f.c,Qt::Key_QuoteLeft,false);
    const int count=f.sent.size();drain(350);return !f.c.isCurrentCustomKeymap()&&f.sent.size()==count;
}
bool uhidPriority() {
    Fixture f({click("BackButton")});if(!f.c.setUhidKeyboardEnabled(true))return false;f.sent.clear();
    mouse(f.c,Qt::BackButton,true);mouse(f.c,Qt::BackButton,false);
    if(touches(f.sent).size()!=2)return false;
    for(const auto&b:f.sent)if(b[0]==13)return false;
    return true;
}
}
int main(int argc,char **argv) {
    // These are Controller/QObject tests, not window tests. A GUI platform
    // would introduce unrelated offscreen-plugin allocations into LeakSanitizer.
    QCoreApplication app(argc,argv);
    const QVector<QPair<QString,std::function<bool()>>> cases{
        {"left",[]{return buttonClick("LeftButton",Qt::LeftButton);}},
        {"right",[]{return buttonClick("RightButton",Qt::RightButton);}},
        {"middle",[]{return buttonClick("MiddleButton",Qt::MiddleButton);}},
        {"back",[]{return buttonClick("BackButton",Qt::BackButton);}},
        {"forward",[]{return buttonClick("ForwardButton",Qt::ForwardButton);}},
        {"aliases",[]{return buttonClick("XButton1",Qt::BackButton)&&buttonClick("ExtraButton2",Qt::ForwardButton);}},
        {"drag",drag},{"multi",multi},{"twice",twice},{"android",androidKey},{"joystick",joystick},
        {"namespaces",namespaces},{"move_no_leak",moveNoLeak},{"toggle",toggle},{"simultaneous",simultaneous},
        {"macro",[]{return macroRoundtrip(false);}}, {"pause",[]{return macroRoundtrip(true);}},
        {"toggle_cancels_delayed",toggleCancelsDelayed},{"uhid_priority",uhidPriority}
    };
    int ran=0,passed=0;for(const auto&test:cases){if(argc>1&&QString(argv[1])!=test.first)continue;++ran;const bool ok=test.second();if(ok)++passed;qInfo()<<test.first<<(ok?"PASS":"FAIL");}
    return ran>0&&ran==passed?0:1;
}
