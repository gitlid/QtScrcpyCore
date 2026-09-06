#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <functional>
#include "controller.h"
#include "controlmsg.h"

namespace {
const QSize screen(1000,800);
void drain(int ms=1) { QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec(); }
QJsonObject click(QString key,QString type="KMT_CLICK") {
    return {{"type",type},{"key",key},{"pos",QJsonObject{{"x",.2},{"y",.3}}},{"switchMap",false}};
}
QString script(QJsonArray nodes,QString toggle="Key_QuoteLeft") {
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"switchKey",toggle},{"keyMapNodes",nodes}}).toJson());
}
struct Fixture {
    QVector<QByteArray> sent;
    Controller c;
    Fixture(QJsonArray nodes={click("Key_W"),click("BackButton")},QString toggle="Key_QuoteLeft")
        :c([this](const QByteArray &b){sent.append(b);return qint64(b.size());},script(nodes,toggle)) {
        c.setFrameSize(screen);c.setUhidKeyboardEnabled(true);
    }
    void key(int k,bool down,Qt::KeyboardModifiers mods=Qt::NoModifier,bool repeat=false,quint32 scan=0) {
        QKeyEvent e(down?QEvent::KeyPress:QEvent::KeyRelease,k,mods,scan,0,0,QString(),repeat);
        c.keyEvent(&e,screen,screen);drain();
    }
    void mouse(Qt::MouseButton b,bool down) {
        QMouseEvent e(down?QEvent::MouseButtonPress:QEvent::MouseButtonRelease,QPointF(900,700),b,down?Qt::MouseButtons(b):Qt::NoButton,Qt::NoModifier);
        c.mouseEvent(&e,screen,screen);drain();
    }
    void toggle(){key(Qt::Key_QuoteLeft,true);key(Qt::Key_QuoteLeft,false);sent.clear();}
};
QVector<QByteArray> ofType(const QVector<QByteArray>&all,int type){QVector<QByteArray>r;for(const auto&b:all)if(!b.isEmpty()&&quint8(b.at(0))==type)r.append(b);return r;}
bool hidHas(const QVector<QByteArray>&all,int usage){for(const auto&b:ofType(all,13))if(b.mid(7,6).contains(char(usage)))return true;return false;}
bool unmapped(int k,int usage){Fixture f;f.toggle();f.key(k,true);f.key(k,false);auto h=ofType(f.sent,13);return hidHas(f.sent,usage)&&h.size()==2&&h.last().mid(5)==QByteArray(8,0)&&ofType(f.sent,2).isEmpty()&&ofType(f.sent,0).isEmpty();}
bool mappedPriority(){Fixture f;f.toggle();f.key(Qt::Key_W,true);f.key(Qt::Key_W,false);return ofType(f.sent,2).size()==2&&ofType(f.sent,13).isEmpty();}
bool mouseOnly(){Fixture f({click("BackButton")});f.toggle();for(int k:{Qt::Key_W,Qt::Key_A,Qt::Key_S,Qt::Key_D}){f.key(k,true);f.key(k,false);}f.mouse(Qt::BackButton,true);f.mouse(Qt::BackButton,false);return ofType(f.sent,13).size()==8&&ofType(f.sent,2).size()==2&&hidHas(f.sent,26);}
bool chord(){Fixture f;f.toggle();f.key(Qt::Key_Control,true,Qt::ControlModifier);f.key(Qt::Key_C,true,Qt::ControlModifier);f.key(Qt::Key_C,false,Qt::ControlModifier);f.key(Qt::Key_Control,false);auto h=ofType(f.sent,13);return h.size()==4&&quint8(h[1].at(5))==1&&h[1].at(7)==char(6)&&h.last().mid(5)==QByteArray(8,0);}
bool concurrent(){Fixture f;f.toggle();f.key(Qt::Key_W,true);f.key(Qt::Key_A,true);f.key(Qt::Key_W,false);f.key(Qt::Key_A,false);return ofType(f.sent,2).size()==2&&ofType(f.sent,13).size()==2&&hidHas(f.sent,4)&&!hidHas(f.sent,26);}
bool toggleRelease(){Fixture f;f.key(Qt::Key_A,true);f.toggle();f.key(Qt::Key_A,true,Qt::NoModifier,true);f.key(Qt::Key_A,false);if(!f.sent.isEmpty())return false;f.key(Qt::Key_A,true);f.key(Qt::Key_A,false);return hidHas(f.sent,4)&&ofType(f.sent,12).isEmpty()&&ofType(f.sent,14).isEmpty()&&f.c.isUhidKeyboardEnabled();}
bool mouseToggle(){Fixture f({click("Key_W")},"BackButton");f.key(Qt::Key_A,true);f.mouse(Qt::BackButton,true);f.mouse(Qt::BackButton,false);f.sent.clear();f.key(Qt::Key_A,false);f.key(Qt::Key_D,true);f.key(Qt::Key_D,false);return f.c.isCurrentCustomKeymap()&&hidHas(f.sent,7)&&ofType(f.sent,2).isEmpty()&&ofType(f.sent,14).isEmpty();}
bool repeat(){Fixture f;f.toggle();f.key(Qt::Key_A,true);f.key(Qt::Key_A,false,Qt::NoModifier,true);f.key(Qt::Key_A,true,Qt::NoModifier,true);f.key(Qt::Key_A,false);f.key(Qt::Key_W,true);f.key(Qt::Key_W,false,Qt::NoModifier,true);f.key(Qt::Key_W,true,Qt::NoModifier,true);f.key(Qt::Key_W,false);return ofType(f.sent,13).size()==2&&ofType(f.sent,2).size()==2;}
bool orphanRelease(){Fixture f({click("Key_W","KMT_CLICK_TWICE")});f.toggle();f.key(Qt::Key_W,false);f.key(Qt::Key_A,false);return f.sent.isEmpty();}
bool focusRelease(){Fixture f;f.toggle();f.key(Qt::Key_A,true);f.c.releaseKeyboard();f.sent.clear();f.key(Qt::Key_A,true,Qt::NoModifier,true);f.key(Qt::Key_A,false);if(!f.sent.isEmpty())return false;f.key(Qt::Key_D,true);f.key(Qt::Key_D,false);return hidHas(f.sent,7)&&f.c.isCurrentCustomKeymap();}
bool physicalPair(){Fixture f;f.toggle();f.key(Qt::Key_W,true,Qt::NoModifier,false,0x1e);f.key(Qt::Key_Q,false,Qt::NoModifier,false,0x1e);auto t=ofType(f.sent,2);return t.size()==2&&t[0].at(1)==char(0)&&t[1].at(1)==char(1)&&ofType(f.sent,13).isEmpty();}
bool toggleReserved(){Fixture f;f.toggle();f.key(Qt::Key_QuoteLeft,true);f.key(Qt::Key_QuoteLeft,true,Qt::NoModifier,true);f.key(Qt::Key_QuoteLeft,false);return !f.c.isCurrentCustomKeymap()&&!hidHas(f.sent,53)&&ofType(f.sent,2).isEmpty();}
bool mappedModifier(){Fixture f({click("Key_Shift")});f.toggle();f.key(Qt::Key_Shift,true,Qt::ShiftModifier);f.key(Qt::Key_A,true,Qt::ShiftModifier);f.key(Qt::Key_A,false,Qt::ShiftModifier);f.key(Qt::Key_Shift,false);auto h=ofType(f.sent,13);return ofType(f.sent,2).size()==2&&h.size()==2&&h[0].at(5)==char(0)&&hidHas(f.sent,4);}
bool disabledCompatibility(){Fixture f;f.c.setUhidKeyboardEnabled(false);f.toggle();f.key(Qt::Key_A,true);f.key(Qt::Key_A,false);if(!f.sent.isEmpty())return false;f.key(Qt::Key_W,true);f.key(Qt::Key_W,false);return ofType(f.sent,2).size()==2&&ofType(f.sent,13).isEmpty();}
bool reload(){Fixture f;f.toggle();f.key(Qt::Key_A,true);f.c.prepareKeymapEditing();f.c.updateScript(script({click("BackButton")}));f.toggle();f.key(Qt::Key_A,false);if(!f.sent.isEmpty())return false;f.key(Qt::Key_W,true);f.key(Qt::Key_W,false);return hidHas(f.sent,26)&&ofType(f.sent,2).isEmpty();}
bool mixedMacro(bool pause){Fixture f;f.toggle();QTemporaryDir dir;if(!f.c.startActionRecording())return false;f.key(Qt::Key_A,true);f.mouse(Qt::BackButton,true);drain(35);f.mouse(Qt::BackButton,false);f.key(Qt::Key_A,false);if(!f.c.stopActionRecording())return false;QString path=dir.filePath("mixed.json");if(!f.c.saveActionMacro(path))return false;QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;auto j=QJsonDocument::fromJson(file.readAll()).object();if(j["version"]!=2)return false;bool touch=false,hid=false;for(const auto&v:j["events"].toArray()){auto m=v.toObject()["message"].toObject();touch|=m["type"]==2;hid|=m["type"]==13;}if(!touch||!hid||!f.c.loadActionMacro(path))return false;f.sent.clear();if(!f.c.playActionMacroAdvanced(1,0,.25,0))return false;drain(10);if(pause){if(!f.c.pauseActionMacro())return false;int n=f.sent.size();f.key(Qt::Key_B,true);f.mouse(Qt::BackButton,true);if(f.sent.size()!=n)return false;f.c.stopActionPlayback();return !f.c.isActionPlaying();}drain(250);return !f.c.isActionPlaying()&&hidHas(f.sent,4)&&ofType(f.sent,2).size()>=2;}
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    const QVector<QPair<QString,std::function<bool()>>> tests{
        {"unmapped_letters",[]{return unmapped(Qt::Key_A,4);}}, {"tab",[]{return unmapped(Qt::Key_Tab,43);}},
        {"escape",[]{return unmapped(Qt::Key_Escape,41);}}, {"mapped_priority",mappedPriority},{"mouse_only",mouseOnly},
        {"chord",chord},{"concurrent",concurrent},{"toggle_release",toggleRelease},{"mouse_toggle",mouseToggle},
        {"repeat",repeat},{"orphan_release",orphanRelease},{"focus_release",focusRelease},{"physical_pair",physicalPair},
        {"toggle_reserved",toggleReserved},{"mapped_modifier",mappedModifier},{"legacy_mode",disabledCompatibility},
        {"reload",reload},{"mixed_macro",[]{return mixedMacro(false);}},{"paused_macro",[]{return mixedMacro(true);}}
    };
    int ran=0,passed=0;for(const auto&t:tests){if(argc>1&&QString(argv[1])!=t.first)continue;++ran;bool ok=t.second();passed+=ok;qInfo()<<t.first<<(ok?"PASS":"FAIL");}return ran>0&&ran==passed?0:1;
}
