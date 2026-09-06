#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QTimer>
#include <QtEndian>
#include <functional>
#include "controller.h"
#include "keymap.h"

namespace {
const QSize screen(1000, 800);
void drain() { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); }
QJsonObject pos(double x, double y) { return {{"x", x}, {"y", y}}; }
QJsonObject click(QString binding, bool releaseLook = false) {
    return {{"type", "KMT_CLICK"}, {"key", binding}, {"pos", pos(.2,.3)}, {"switchMap", releaseLook}};
}
QJsonObject root(bool withLook = false) {
    QJsonObject r{{"switchKey", "Key_QuoteLeft"}, {"keyMapNodes", QJsonArray{click("BackButton"),click("Key_M",true)}}};
    if (withLook) { r["mouseMoveMap"] = QJsonObject{{"startPos",pos(.5,.5)},{"speedRatioX",1.},{"speedRatioY",1.}}; }
    return r;
}
QString json(const QJsonObject &r) { return QString::fromUtf8(QJsonDocument(r).toJson()); }
struct Fixture {
    QVector<QByteArray> sent;
    QVector<bool> captures;
    Controller c;
    explicit Fixture(const QJsonObject &r) : c([this](const QByteArray &b) { sent.append(b); return qint64(b.size()); }, json(r)) {
        c.setFrameSize(screen);
        QObject::connect(&c,&Controller::grabCursor,&c,[this](bool b){captures.append(b);});
    }
    void key(int k, bool press) { QKeyEvent e(press?QEvent::KeyPress:QEvent::KeyRelease,k,Qt::NoModifier);c.keyEvent(&e,screen,screen);drain(); }
    void toggle() { key(Qt::Key_QuoteLeft,true);key(Qt::Key_QuoteLeft,false); }
    void mouse(Qt::MouseButton b,bool press) {
        QMouseEvent e(press?QEvent::MouseButtonPress:QEvent::MouseButtonRelease,QPointF(900,700),b,press?Qt::MouseButtons(b):Qt::NoButton,Qt::NoModifier);
        c.mouseEvent(&e,screen,screen);drain();
    }
    void move(QPointF p=QPointF(700,500),Qt::MouseButtons held=Qt::NoButton,QSize shown=screen) {
        QMouseEvent e(QEvent::MouseMove,p,Qt::NoButton,held,Qt::NoModifier);c.mouseEvent(&e,screen,shown);drain();
    }
};
QVector<QByteArray> touches(const QVector<QByteArray>&sent) {
    QVector<QByteArray> result;for(const auto&b:sent)if(b.size()==32&&b.at(0)==char(2))result.append(b);return result;
}
int x(const QByteArray&b){return int(qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+10)));}
int action(const QByteArray&b){return int(quint8(b.at(1)));}
bool legacyLookOff() { KeyMap m;m.loadKeyMap(json(root(true)));return !m.isValidMouseMoveMap(); }
bool explicitLook(bool enabled) {
    auto r=root(true);r["mouseLookEnabled"]=enabled;KeyMap m;m.loadKeyMap(json(r));return m.isValidMouseMoveMap()==enabled;
}
bool noViewNoCapture() {
    Fixture f(root());f.toggle();f.move();f.move(QPointF(750,510));
    return f.c.isCurrentCustomKeymap()&&!f.captures.contains(true)&&f.sent.isEmpty();
}
bool legacyNoCapture() {
    Fixture f(root(true));f.toggle();f.move();f.move(QPointF(750,510));
    return !f.captures.contains(true)&&f.sent.isEmpty();
}
bool unmappedSide(bool enabled) {
    Fixture f(root());if(enabled)f.toggle();f.sent.clear();
    for(const auto b:{Qt::ForwardButton,Qt::TaskButton,Qt::ExtraButton24}){f.mouse(b,true);f.mouse(b,false);}
    return f.sent.isEmpty();
}
bool mappedTarget() {
    Fixture f(root());f.toggle();f.mouse(Qt::BackButton,true);f.mouse(Qt::BackButton,false);
    const auto t=touches(f.sent);return t.size()==2&&x(t[0])==200&&x(t[1])==200&&action(t[0])==0&&action(t[1])==1;
}
bool normalPrimaryOnly() {
    Fixture f(root());
    for(const auto b:{Qt::RightButton,Qt::MiddleButton,Qt::BackButton,Qt::ForwardButton}){f.mouse(b,true);f.mouse(b,false);}
    if(!f.sent.isEmpty())return false;
    f.mouse(Qt::LeftButton,true);f.move(QPointF(700,500),Qt::LeftButton);f.mouse(Qt::LeftButton,false);
    const auto t=touches(f.sent);return t.size()==3&&x(t[0])==900&&action(t[1])==2&&action(t[2])==1;
}
bool explicitLookMoves() {
    auto r=root(true);r["mouseLookEnabled"]=true;Fixture f(r);f.toggle();f.move();f.move(QPointF(730,505));
    const auto t=touches(f.sent);return f.captures.contains(true)&&t.size()>=2&&action(t.first())==0;
}
bool releasedSideStillMapped() {
    auto r=root(true);r["mouseLookEnabled"]=true;Fixture f(r);f.toggle();f.move();
    f.key(Qt::Key_M,true);f.key(Qt::Key_M,false);f.sent.clear();f.captures.clear();
    f.move(QPointF(650,450),Qt::NoButton,QSize(900,700));
    if(f.captures.contains(true)||!f.sent.isEmpty())return false;
    f.mouse(Qt::BackButton,true);f.mouse(Qt::BackButton,false);
    const auto t=touches(f.sent);return t.size()==2&&x(t[0])==200&&x(t[1])==200;
}
bool releaseFlagWithoutView() {
    Fixture f(root());f.toggle();
    for(int i=0;i<3;++i){f.key(Qt::Key_M,true);f.key(Qt::Key_M,false);}
    f.sent.clear();f.move();f.move(QPointF(750,500));return !f.captures.contains(true)&&f.sent.isEmpty();
}
bool replaceReleasesCapture() {
    auto r=root(true);r["mouseLookEnabled"]=true;Fixture f(r);f.toggle();f.captures.clear();
    f.c.updateScript(json(root()));drain();return !f.captures.isEmpty()&&!f.captures.last()&&!f.c.isCurrentCustomKeymap();
}
bool editReleasesCapture() {
    auto r=root(true);r["mouseLookEnabled"]=true;Fixture f(r);f.toggle();f.captures.clear();
    f.c.prepareKeymapEditing();f.sent.clear();f.move();f.move(QPointF(750,500));
    if(f.captures.isEmpty()||f.captures.last()||!f.sent.isEmpty())return false;
    f.mouse(Qt::BackButton,true);f.mouse(Qt::BackButton,false);return touches(f.sent).size()==2;
}
bool reloadRemovesOldView() {
    auto r=root(true);r["mouseLookEnabled"]=true;KeyMap m;m.loadKeyMap(json(r));if(!m.isValidMouseMoveMap())return false;
    m.loadKeyMap(json(root()));return !m.isValidMouseMoveMap()&&m.getKeyMapNodeMouse(Qt::BackButton).type==KeyMap::KMT_CLICK;
}
bool invalidLookFlags() {
    for(auto v:{QJsonValue("true"),QJsonValue(1),QJsonValue(QJsonValue::Null)}){
        auto r=root(true);r["mouseLookEnabled"]=v;KeyMap m;m.loadKeyMap(json(r));if(m.isValidMouseMoveMap())return false;
    }
    auto r=root();r["mouseLookEnabled"]=true;KeyMap m;m.loadKeyMap(json(r));return !m.isValidMouseMoveMap();
}
bool invalidVerticalRatio() {
    auto r=root(true);r["mouseLookEnabled"]=true;auto look=r["mouseMoveMap"].toObject();look["speedRatioY"]=0.;r["mouseMoveMap"]=look;
    KeyMap m;m.loadKeyMap(json(r));return !m.isValidMouseMoveMap();
}
bool unknownWhilePrimaryHeld() {
    Fixture f(root());f.toggle();f.mouse(Qt::LeftButton,true);f.mouse(Qt::ForwardButton,true);f.mouse(Qt::ForwardButton,false);f.mouse(Qt::LeftButton,false);
    const auto t=touches(f.sent);return t.size()==2&&action(t[0])==0&&action(t[1])==1;
}
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    const QVector<QPair<QString,std::function<bool()>>> tests{
        {"legacy_look_off",legacyLookOff},{"look_disabled",[]{return explicitLook(false);}},{"look_enabled",[]{return explicitLook(true);}},
        {"ordinary_no_capture",noViewNoCapture},{"legacy_no_capture",legacyNoCapture},
        {"unmapped_custom",[]{return unmappedSide(true);}},{"unmapped_normal",[]{return unmappedSide(false);}},
        {"mapped_target",mappedTarget},{"normal_primary_only",normalPrimaryOnly},{"look_moves",explicitLookMoves},
        {"released_side_mapped",releasedSideStillMapped},{"release_flag_no_view",releaseFlagWithoutView},
        {"replace_capture",replaceReleasesCapture},{"edit_capture",editReleasesCapture},{"reload_no_view",reloadRemovesOldView},
        {"invalid_look_flags",invalidLookFlags},{"invalid_vertical_ratio",invalidVerticalRatio},{"unknown_with_primary",unknownWhilePrimaryHeld}
    };
    int ran=0,passed=0;for(const auto&t:tests){if(argc>1&&QString(argv[1])!=t.first)continue;++ran;bool ok=t.second();if(ok)++passed;qInfo()<<t.first<<(ok?"PASS":"FAIL");}
    return ran>0&&ran==passed?0:1;
}
