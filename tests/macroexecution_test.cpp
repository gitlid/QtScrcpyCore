#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <limits>
#include <functional>
#include "actionmacro.h"
#include "controlmsg.h"
static void waitMs(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static QJsonObject key(int code,int action) { ControlMsg m(ControlMsg::CMT_INJECT_KEYCODE); m.setInjectKeycodeMsgData(static_cast<AndroidKeyeventAction>(action),static_cast<AndroidKeycode>(code),0,AMETA_NONE); return m.toJson(); }
static QJsonObject touch(int action) { ControlMsg m(ControlMsg::CMT_INJECT_TOUCH); m.setInjectTouchMsgData(POINTER_ID_MOUSE,static_cast<AndroidMotioneventAction>(action),static_cast<AndroidMotioneventButtons>(0),static_cast<AndroidMotioneventButtons>(0),QRect(10,20,100,200),1.f);return m.toJson(); }
static QJsonObject text() { ControlMsg m(ControlMsg::CMT_INJECT_TEXT);QString t="test";m.setInjectTextMsgData(t);return m.toJson(); }
static QJsonObject ev(int ms,const QJsonObject &msg) { return {{"atMs",ms},{"message",msg}}; }
static bool load(ActionMacro &m,const QJsonArray &events,int duration,int version=1) { QTemporaryDir d;QFile f(d.filePath("macro.json"));if(!f.open(QIODevice::WriteOnly))return false;f.write(QJsonDocument(QJsonObject{{"format","QtScrcpyActionMacro"},{"version",version},{"durationMs",duration},{"events",events}}).toJson());f.close();m.setCurrentScreen(QSize(100,200));return m.load(f.fileName()); }
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);const QString selected=argc>1?argv[1]:"";int ran=0,passed=0;
 auto test=[&](const char*name,const std::function<bool()> &fn){if(!selected.isEmpty()&&selected!=name)return;++ran;const bool ok=fn();if(ok)++passed;qInfo("%s %s",ok?"PASS":"FAIL",name);};
 test("speed_bounds",[]{ActionMacro m([](ControlMsg*p){delete p;});load(m,{ev(0,text())},100);for(double s:{0.,0.24,8.01,9.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})if(m.play(1,0,s,0))return false;for(double s:{0.25,0.5,1.,1.5,2.,4.,8.}){if(!m.play(1,0,s,0))return false;m.stopPlayback();}return !m.play(1,0,1.,-1)&&!m.play(1,0,1.,86400001);});
 test("speed_eight",[]{int n=0;ActionMacro m([&](ControlMsg*p){++n;delete p;});if(!load(m,{ev(800,text())},800)||!m.play(1,0,8.,0))return false;waitMs(400);return n==1&&!m.isPlaying();});
 test("pause_wait",[]{int n=0;ActionMacro m([&](ControlMsg*p){++n;delete p;});load(m,{ev(600,text())},600);m.play(1,0,8.,0);if(!m.pause()||!m.isPlaying()||!m.isPaused())return false;waitMs(250);if(n||m.activeElapsedMs()>20)return false;if(!m.resume())return false;waitMs(150);return n==1&&!m.isPlaying();});
 test("pause_interval",[]{int n=0;ActionMacro m([&](ControlMsg*p){++n;delete p;});load(m,{ev(0,text())},0);m.play(2,250,8.,0);waitMs(25);if(n!=1||!m.pause())return false;waitMs(300);if(n!=1)return false;m.resume();waitMs(75);if(n!=1)return false;waitMs(230);return n==2&&!m.isPlaying();});
 test("time_limit",[]{QVector<QJsonObject>out;ActionMacro m([&](ControlMsg*p){out.append(p->toJson());delete p;});load(m,{ev(0,key(AKEYCODE_A,0)),ev(1000,key(AKEYCODE_A,1))},1000);m.play(0,0,1.,40);waitMs(140);return !m.isPlaying()&&out.size()==2&&out.last()["action"].toInt()==1;});
 test("limit_during_interval",[]{int n=0;ActionMacro m([&](ControlMsg*p){++n;delete p;});load(m,{ev(0,text())},0);m.play(0,500,8.,40);waitMs(150);return n==1&&!m.isPlaying();});
 test("limit_excludes_pause",[]{ActionMacro m([](ControlMsg*p){delete p;});load(m,{ev(0,text())},10000);m.play(0,0,1.,150);m.pause();waitMs(250);if(!m.isPlaying()||m.activeElapsedMs()>30)return false;m.resume();waitMs(250);return !m.isPlaying()&&m.activeElapsedMs()>=150;});
 test("stop_paused",[]{int n=0;ActionMacro m([&](ControlMsg*p){++n;delete p;});load(m,{ev(400,text())},400);m.play(0,0);m.pause();m.stopPlayback();waitMs(80);return !m.resume()&&!m.isPaused()&&!m.isPlaying()&&n==0;});
 test("touch_boundary",[]{QVector<QJsonObject>out;ActionMacro m([&](ControlMsg*p){out.append(p->toJson());delete p;});load(m,{ev(0,touch(0)),ev(600,touch(2)),ev(800,touch(1)),ev(900,text())},900);m.play(1,0,8.,0);waitMs(20);if(out.size()!=1||!m.pause()||!m.interruptedInput())return false;if(out.size()!=2||out.last()["action"].toInt()!=1)return false;m.resume();waitMs(150);return out.size()==3&&out.last()["kind"].toString()=="text"&&!m.isPlaying();});
 test("key_boundary",[]{QVector<QJsonObject>out;ActionMacro m([&](ControlMsg*p){out.append(p->toJson());delete p;});load(m,{ev(0,key(AKEYCODE_A,0)),ev(800,key(AKEYCODE_A,1)),ev(900,text())},900);m.play(1,0,8.,0);waitMs(20);m.pause();m.resume();waitMs(150);return out.size()==3&&out.last()["kind"].toString()=="text";});
 test("hid_pause_release",[]{QVector<QJsonObject>out;ActionMacro m([&](ControlMsg*p){out.append(p->toJson());delete p;});ControlMsg p(ControlMsg::CMT_UHID_INPUT),u(ControlMsg::CMT_UHID_INPUT);QByteArray report(8,0);report[0]=2;report[2]=4;p.setUhidKeyboardReport(report);if(!load(m,{ev(0,p.toJson()),ev(800,u.toJson()),ev(900,text())},900,2))return false;m.play(1,0,8.,0);waitMs(20);m.pause();if(out.size()!=2||out.last()["modifiers"].toInt()!=0||!out.last()["keys"].toArray().isEmpty())return false;m.resume();waitMs(150);return out.size()==3&&!m.isPlaying();});
 test("recording_pause",[]{ActionMacro m([](ControlMsg*p){delete p;});m.setCurrentScreen(QSize(100,200));m.startRecording();ControlMsg d(ControlMsg::CMT_INJECT_KEYCODE);d.setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN,AKEYCODE_A,0,AMETA_NONE);m.record(d);waitMs(20);m.pause();const qint64 before=m.activeElapsedMs();waitMs(200);m.record(d);if(m.activeElapsedMs()!=before||m.eventCount()!=2)return false;m.resume();m.record(d);m.stopRecording();return m.eventCount()==4&&!m.isPaused();});
 test("record_stop_paused",[]{ActionMacro m([](ControlMsg*p){delete p;});m.startRecording();ControlMsg p(ControlMsg::CMT_INJECT_KEYCODE);p.setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN,AKEYCODE_A,0,AMETA_NONE);m.record(p);m.pause();m.record(p);m.stopRecording();return m.eventCount()==2&&!m.isPaused();});
 test("geometry_paused",[]{ActionMacro m([](ControlMsg*p){delete p;});load(m,{ev(500,touch(0))},500);m.play(1,0);m.pause();m.setCurrentScreen(QSize(200,100));return !m.isPlaying()&&!m.resume();});
 test("pause_reentrancy",[]{int n=0;ActionMacro*ptr=nullptr;ActionMacro m([&](ControlMsg*p){++n;delete p;if(n==1)ptr->pause();});ptr=&m;load(m,{ev(0,text()),ev(80,text())},80);m.play(1,0);waitMs(20);if(n!=1||!m.isPaused())return false;m.resume();waitMs(150);return n==2&&!m.isPlaying();});
 test("busy_paused",[]{ActionMacro m([](ControlMsg*p){delete p;});load(m,{ev(200,text())},200);m.play(1,0);m.pause();QTemporaryDir dir;return !m.startRecording()&&!m.play(1,0)&&!m.save(dir.filePath("x.json"))&&!m.pause();});
 test("same_timestamp",[]{int n=0;ActionMacro m([&](ControlMsg*p){++n;delete p;});QJsonArray a;for(int i=0;i<300;++i)a.append(ev(0,text()));load(m,a,0);m.play(1,0,8.,0);waitMs(200);return n==300&&!m.isPlaying();});
 test("app_geometry",[]{
  QVector<QJsonObject>out;int interruptions=0;ActionMacro m([&](ControlMsg*p){out.append(p->toJson());delete p;});
  QObject::connect(&m,&ActionMacro::applicationInterrupted,&m,[&](){++interruptions;});
  if(!load(m,{ev(0,touch(0)),ev(800,touch(1)),ev(900,text())},900))return false;
  m.setApplicationBound(true);m.play(1,0,8.,0);waitMs(20);m.setCurrentScreen(QSize(200,100));
  if(interruptions!=1||!m.isPlaying()||!m.isPaused()||m.screenMatches()||out.size()!=2||out.last()["action"].toInt()!=1)return false;
  m.setCurrentScreen(QSize(100,200));if(!m.screenMatches()||!m.resume())return false;waitMs(150);
  return !m.isPlaying()&&out.size()==3&&out.last()["kind"].toString()=="text";
 });
 test("app_resume_wait",[]{
  ActionMacro m([](ControlMsg*p){delete p;});int interruptions=0;
  QObject::connect(&m,&ActionMacro::applicationInterrupted,&m,[&](){++interruptions;});
  load(m,{ev(500,touch(0))},500);m.setApplicationBound(true);m.play(1,0);m.pause();m.setCurrentScreen(QSize(200,100));
  if(interruptions||m.resume()||!m.isPlaying()||!m.isPaused())return false;
  m.setCurrentScreen(QSize(100,200));const bool ok=m.resume();m.stopPlayback();return ok;
 });
 test("app_stop_geometry",[]{
  ActionMacro m([](ControlMsg*p){delete p;});load(m,{ev(500,touch(0))},500);m.setApplicationBound(true);m.play(1,0);m.setCurrentScreen(QSize(200,100));
  if(!m.isPaused())return false;m.stopPlayback();m.setCurrentScreen(QSize(100,200));if(m.resume())return false;
  m.play(1,0);m.setCurrentScreen(QSize(200,100));return !m.isPlaying();
 });
 qInfo("Macro execution v2: %d/%d",passed,ran);return ran>0&&ran==passed?0:1;
}
