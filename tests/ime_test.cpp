#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <cstdio>
#include <stdexcept>
#include "controller.h"
#include "controlmsg.h"
namespace {
void require(bool b, const char *m) { if (!b) throw std::runtime_error(m); }
void flush() { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); }
void wait(int ms) { QEventLoop e; QTimer::singleShot(ms, &e, &QEventLoop::quit); e.exec(); }
QString mapping() {
    QJsonArray nodes;
    for (const auto &k : {"Key_Shift", "Key_Control", "Key_Space"})
        nodes.append(QJsonObject{{"type","KMT_CLICK"},{"key",k},{"switchMap",false},{"pos",QJsonObject{{"x",.5},{"y",.5}}}});
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"switchKey","Key_QuoteLeft"},{"mouseLookEnabled",false},{"keyMapNodes",nodes}}).toJson());
}
struct Fixture {
    QVector<QByteArray> sent;
    bool fail = false;
    Controller c;
    Fixture() : c([this](const QByteArray &b) { sent.append(b); return fail ? qint64(-1) : qint64(b.size()); }) {
        c.setFrameSize(QSize(100,200)); require(c.setUhidKeyboardEnabled(true),"create HID"); sent.clear();
    }
    void key(int k, bool down) { QKeyEvent e(down?QEvent::KeyPress:QEvent::KeyRelease,k,Qt::NoModifier); c.keyEvent(&e,{100,200},{100,200}); flush(); }
    QVector<QByteArray> reports() const {
        QVector<QByteArray> r; for(const auto &b:sent) if(quint8(b[0])==13) {
            require(b.size()==13 && b.mid(1,4)==QByteArray::fromHex("00010008"),"same fixed keyboard ID"); r.append(b.mid(5));
        } return r;
    }
};
QByteArray report(int mods, int key=0) { QByteArray b(8,0); b[0]=char(mods); b[2]=char(key); return b; }
void checkShortcut(int which) {
    Fixture f; require(f.c.sendImeShortcut(which),"shortcut accepted"); auto r=f.reports();
    const QVector<QByteArray> expected = which==0 ? QVector<QByteArray>{report(2),report(0)}
        : QVector<QByteArray>{report(which==1?1:2),report(which==1?1:2,44),report(which==1?1:2),report(0)};
    require(r.size()>=expected.size(),"all report transitions");
    // Compare each wire report directly; QVector::operator== in Qt 5.15
    // instantiates deprecated MSVC checked iterators with recent /WX builds.
    for (int i = 0; i < expected.size(); ++i)
        require(r.at(r.size()-expected.size()+i)==expected.at(i),"modifier and Space order, release last");
    for(const auto &b:f.sent) require(quint8(b[0])==13,"no SDK event or second device");
}
void mapped() {
    Fixture f; require(f.c.applyAppKeymap(mapping()),"enable mapped modifiers"); f.sent.clear();
    require(f.c.sendImeShortcut(1),"shortcut not eaten by mapping");
    for(const auto &b:f.sent) require(quint8(b[0])==13,"no mapped click");
    require(f.c.isCurrentCustomKeymap(),"preserve keymap mode");
    f.key(Qt::Key_Shift,true); f.key(Qt::Key_Shift,false);
    int touches=0;for(const auto &b:f.sent)if(quint8(b[0])==2)++touches;
    require(touches==2,"physical mapped key still executes original mapping");
}
void invalidShortcut() { Fixture f; require(!f.c.sendImeShortcut(-1)&&!f.c.sendImeShortcut(3)&&f.sent.isEmpty(),"reject out of range without side effects"); }
void utf8Paste() {
    Fixture f; const QString text=QString::fromUtf8("你好，拼音测试\n中文😀");
    require(f.c.pasteImeText(text),"Unicode accepted"); int paste=0;
    for(const auto &b:f.sent) if(quint8(b[0])==9) {
        ++paste; require(b[9]==1,"paste flag"); require(b.mid(14)==text.toUtf8(),"UTF-8 survives exactly");
        require(qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()+10))==quint32(text.toUtf8().size()),"UTF-8 byte length");
    } else require(quint8(b[0])==13,"no literal ASCII key injection");
    require(paste==1,"only one Unicode commit");
}
void boundaries() {
    Fixture f;
    require(!f.c.pasteImeText(QString()) && !f.c.pasteImeText(QString(65537,'a'))
        && !f.c.pasteImeText(QString(22000,QChar(0x4e2d)))
        && !f.c.pasteImeText(QString("a")+QChar(0)+"b")
        && !f.c.pasteImeText(QString(QChar(0xd800))) && f.sent.isEmpty(),"reject empty, oversized and malformed text");
    require(f.c.pasteImeText(QString(65536,'a')),"64KiB exact boundary");
    require(f.sent.last().size()==65536+14,"no silent truncation");
}
void busy() {
    Fixture f; require(f.c.startActionRecording(),"record"); f.sent.clear();
    require(!f.c.sendImeShortcut(0)&&!f.c.pasteImeText("x")&&f.sent.isEmpty(),"no manual IME during recording");
    f.key(Qt::Key_A,true); wait(10); f.key(Qt::Key_A,false); require(f.c.stopActionRecording(),"stop record");
    require(f.c.playActionMacro(0,1000),"play"); f.sent.clear();
    require(!f.c.sendImeShortcut(1)&&!f.c.pasteImeText("x")&&f.sent.isEmpty(),"no IME during playback");
    require(f.c.pauseActionMacro(),"pause"); f.sent.clear();
    require(!f.c.sendImeShortcut(2)&&!f.c.pasteImeText("x")&&f.sent.isEmpty(),"paused playback still owns input");
    f.c.stopActionPlayback(); require(f.c.pasteImeText("x"),"stop unblocks");
}
void camera() { Fixture f;f.c.setCameraMode(true);require(!f.c.sendImeShortcut(0)&&!f.c.pasteImeText("x")&&f.sent.isEmpty(),"camera must reject"); }
void sdk() {
    Fixture f; f.c.setUhidKeyboardEnabled(false); f.sent.clear();
    require(!f.c.sendImeShortcut(1)&&f.sent.isEmpty(),"never pretend SDK is UHID");
    require(f.c.pasteImeText(QString::fromUtf8("中文"))&&f.sent.size()==1,"clipboard compatibility also works without UHID");
}
void failedSend() {Fixture f;f.fail=true;require(!f.c.pasteImeText("x"),"transport failure reported");require(!f.c.sendImeShortcut(1),"partial shortcut reports failure");require(f.reports().last()==report(0),"try neutral release on failure");}
void resumes() {
    Fixture f; f.c.sendImeShortcut(1); f.sent.clear(); f.key(Qt::Key_A,true);f.key(Qt::Key_A,false);
    const auto r=f.reports();require(r.size()==2&&r[0]==report(0,4)&&r[1]==report(0),"no stuck Ctrl after IME switch");
    f.c.pasteImeText("x");f.sent.clear();f.key(Qt::Key_B,true);f.key(Qt::Key_B,false);
    require(f.reports().size()==2&&f.reports()[0]==report(0,5),"UHID still connected after text commit");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QVector<QPair<QString,std::function<void()>>> cases{{"shift",[]{checkShortcut(0);}}, {"ctrl_space",[]{checkShortcut(1);}},
        {"shift_space",[]{checkShortcut(2);}}, {"mapped",mapped},{"invalid",invalidShortcut},{"unicode",utf8Paste},{"bounds",boundaries},
        {"busy",busy},{"camera",camera},{"sdk",sdk},{"failure",failedSend},{"resume",resumes}};
    for(const auto &t:cases)if(argc==2&&QString(argv[1])==t.first){try{t.second();std::printf("PASS %s\n",qPrintable(t.first));return 0;}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s: %s\n",qPrintable(t.first),e.what());return 1;}}
    return 2;
}
