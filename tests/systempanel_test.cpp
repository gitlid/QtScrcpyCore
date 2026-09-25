#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstdio>
#include <stdexcept>
#include <functional>
#include "controller.h"
#include "controlmsg.h"
namespace {
void require(bool ok, const char *reason) { if (!ok) throw std::runtime_error(reason); }
void wait(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
struct Fixture {
    QVector<QByteArray> output;
    bool fail = false;
    Controller c;
    explicit Fixture(QSize size = QSize(1080,2340)) : c([this](const QByteArray &b) {
        output.append(b); return fail ? qint64(-1) : qint64(b.size());
    }) { c.setFrameSize(size); }
    QVector<QByteArray> touches() const {
        QVector<QByteArray> result;
        for (const auto &b : output) if (b.size() == 32 && b[0] == char(ControlMsg::CMT_INJECT_TOUCH)) result.append(b);
        return result;
    }
};
int x(const QByteArray &b) { return int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()+10))); }
int y(const QByteArray &b) { return int(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()+14))); }
int action(const QByteArray &b) { return quint8(b[1]); }
void gesture(bool settings, QSize screen) {
    Fixture f(screen);
    if (settings) f.c.expandSettingsPanel(); else f.c.expandNotificationPanel();
    require(f.output.size() == 1 && f.output.first()[0] == char(ControlMsg::CMT_COLLAPSE_PANELS), "collapse any open shade before starting");
    require(f.touches().isEmpty(), "wait for collapse before touching");
    wait(700);
    const auto t = f.touches(); require(t.size() >= 4, "full animated swipe");
    require(action(t.first()) == 0 && action(t.last()) == 1, "paired down/up");
    require(y(t.first()) == 1 && y(t.last()) == screen.height()*3/4, "start on the status bar and pull down");
    int previous = 1;
    for (int i=0;i<t.size();++i) {
        require(x(t[i]) == screen.width()*(settings?3:1)/4, "choose physical left/right half");
        require(y(t[i]) >= previous, "monotonic swipe"); previous=y(t[i]);
        if(i>0 && i<t.size()-1)require(action(t[i])==2,"only moves between down and up");
        const auto *bytes=reinterpret_cast<const uchar *>(t[i].constData());
        require(qFromBigEndian<quint16>(bytes+18)==screen.width() && qFromBigEndian<quint16>(bytes+20)==screen.height(),"raw frame coordinates");
        require(qFromBigEndian<quint16>(bytes+22)==(i==t.size()-1?0:65535),"touch pressure including moves");
    }
}
void replacement() {
    Fixture f;f.c.expandNotificationPanel();wait(230);f.c.expandSettingsPanel();wait(700);
    int down=0,up=0;for(const auto &b:f.touches()){if(action(b)==0)++down;if(action(b)==1)++up;}
    require(down==2&&up==2,"replacing a swipe releases the old contact");
    require(x(f.touches().last())==810,"latest panel wins");
}
void cancel(bool early) {
    Fixture f;f.c.expandSettingsPanel();wait(early?50:230);f.c.stopActionPlayback();
    const int count=f.output.size();wait(550);require(f.output.size()==count,"no delayed input after emergency stop");
    require(early?f.touches().isEmpty():action(f.touches().last())==1,"release only established contacts");
}
void geometry() {
    Fixture f;f.c.expandNotificationPanel();wait(230);f.c.setFrameSize(QSize(2340,1080));
    const int count=f.output.size();wait(500);require(count==f.output.size()&&action(f.touches().last())==1,"rotation cancels old-size gesture");
}
void keymap() {
    Fixture f;f.c.expandSettingsPanel();wait(230);
    require(!f.c.applyAppKeymap(QString()),"automatic profile change must wait for the swipe");
    wait(500);require(f.c.applyAppKeymap(QString()),"profile allowed after completion");
}
void macro() {
    Fixture f;QTemporaryDir temp;require(f.c.startActionRecording(),"record start");
    f.c.expandSettingsPanel();wait(700);require(f.c.stopActionRecording(),"record stop");
    const auto recorded=f.touches();require(recorded.size()>=4,"recorded swipe exists");
    require(f.c.saveActionMacro(temp.filePath("panel.json")),"save gesture");
    require(f.c.loadActionMacro(temp.filePath("panel.json")),"load gesture");
    f.output.clear();require(f.c.playActionMacro(1,0),"play");f.c.expandNotificationPanel();wait(750);
    require(f.touches().size()==recorded.size(),"manual panel button cannot interfere with playback");
    for(const auto &b:f.touches())require(x(b)==810,"replay retains target side");
}
void paused() {
    Fixture f;f.c.startActionRecording();f.c.pauseActionMacro();f.output.clear();f.c.expandSettingsPanel();wait(600);
    require(f.output.isEmpty(),"paused recording owns input");f.c.stopActionRecording();
}
void unavailable() {
    Fixture f{QSize()};f.c.expandSettingsPanel();wait(40);require(f.output.isEmpty(),"no frame no gesture");
    f.c.setFrameSize(QSize(1080,2340));f.c.setCameraMode(true);f.c.expandNotificationPanel();wait(40);require(f.output.isEmpty(),"no gestures in camera mode");
}
void transport() {
    Fixture f;f.c.expandSettingsPanel();wait(230);f.fail=true;wait(100);const int count=f.output.size();wait(500);
    require(f.output.size()==count,"transport failure cancels timer");require(action(f.touches().last())==1,"best-effort release on transport failure");
}
void disconnect() {
    Fixture f;f.c.expandSettingsPanel();wait(230);f.c.shutdownKeyboard();f.c.setFrameSize(QSize());
    const int count=f.output.size();wait(500);require(f.output.size()==count&&action(f.touches().last())==1,"disconnect releases and cancels");
}
void manual() {
    Fixture f;f.c.expandSettingsPanel();wait(230);
    QMouseEvent move(QEvent::MouseButtonPress,QPointF(400,600),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    f.c.mouseEvent(&move,QSize(1080,2340),QSize(1080,2340));QCoreApplication::sendPostedEvents();
    const int count=f.output.size();wait(500);require(f.output.size()==count,"manual input cancels gesture");
    const auto t=f.touches();require(action(t[t.size()-2])==1&&action(t.last())==0&&x(t.last())==400,"release before manual down");
}
void hover() {
    Fixture f;f.c.expandSettingsPanel();wait(230);
    QMouseEvent move(QEvent::MouseMove,QPointF(400,600),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    f.c.mouseEvent(&move,QSize(1080,2340),QSize(1080,2340));wait(500);
    require(y(f.touches().last())==1755,"moving the desktop pointer without clicking must not interrupt the swipe");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QVector<QPair<QString,std::function<void()>>> tests{
        {"notifications",[]{gesture(false,QSize(1080,2340));}}, {"settings",[]{gesture(true,QSize(1080,2340));}},
        {"landscape",[]{gesture(true,QSize(2340,1080));}}, {"replace",replacement},
        {"cancel_early",[]{cancel(true);}}, {"cancel_contact",[]{cancel(false);}}, {"geometry",geometry},
        {"keymap",keymap},{"macro",macro},{"paused",paused},{"unavailable",unavailable},{"transport",transport},
        {"disconnect",disconnect},{"manual",manual},{"hover",hover}};
    int count=0,failed=0;
    for(const auto &test:tests){if(argc>1&&QString::fromLocal8Bit(argv[1])!=test.first)continue;++count;
        try{test.second();std::printf("PASS %s\n",qPrintable(test.first));}
        catch(const std::exception &e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",qPrintable(test.first),e.what());}}
    return count>0&&failed==0?0:1;
}
