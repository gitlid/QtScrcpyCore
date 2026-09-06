
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QSize>

#include "inputconvertbase.h"

class QTcpSocket;
class Receiver;
class InputConvertBase;
class DeviceMsg;
class ActionMacro;
class Controller : public QObject
{
    Q_OBJECT
public:
    Controller(std::function<qint64(const QByteArray&)> sendData, QString gameScript = "", QObject *parent = Q_NULLPTR);
    virtual ~Controller();

    void postControlMsg(ControlMsg *controlMsg);
    void setCameraMode(bool cameraMode);
    void setFrameSize(const QSize &size);
    void recvDeviceMsg(DeviceMsg *deviceMsg);
    void test(QRect rc);

    void updateScript(QString gameScript = "");
    bool isCurrentCustomKeymap();

    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();
    void copy();
    void cut();
    void expandNotificationPanel();
    void expandSettingsPanel();
    void collapsePanel();
    void rotateDevice();
    void startApp(const QString &name);
    void scanFile(const QString &path);
    void resizeDisplay(const QSize &size);
    void setDisplayPower(bool on);
    void setCameraTorch(bool on);
    void cameraZoomIn();
    void cameraZoomOut();

    // for input convert
    void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);

    // turn the screen on if it was off, press BACK otherwise
    // If the screen is off, it is turned on only on down
    void postBackOrScreenOn(bool down);
    void requestDeviceClipboard();
    void getDeviceClipboard(bool cut = false);
    void setDeviceClipboard(bool pause = true);
    void clipboardPaste();
    void postTextInput(QString &text);

    bool startActionRecording();
    bool stopActionRecording();
    bool saveActionMacro(const QString &fileName, QString *error = Q_NULLPTR) const;
    bool loadActionMacro(const QString &fileName, QString *error = Q_NULLPTR);
    bool playActionMacro(int repeatCount, int intervalMs);
    void stopActionPlayback();
    bool isActionRecording() const;
    bool isActionPlaying() const;
    int actionMacroEventCount() const;

signals:
    void grabCursor(bool grab);
    void actionMacroStateChanged(bool recording, bool playing, int eventCount);
    void actionMacroProgress(int currentEvent, int totalEvents, int currentLoop, int totalLoops);
    void actionMacroError(const QString &message);

protected:
    bool event(QEvent *event);

private:
    bool sendControl(const QByteArray &buffer);
    void postKeyCodeClick(AndroidKeycode keycode);
    void sendPendingResize();
    void resetInputState(bool preserveKeymap = false);

private:
    QPointer<Receiver> m_receiver;
    QPointer<InputConvertBase> m_inputConvert;
    QPointer<ActionMacro> m_actionMacro;
    std::function<qint64(const QByteArray&)> m_sendData = Q_NULLPTR;
    QSize m_pendingResize;
    bool m_resizeQueued = false;
    bool m_cameraMode = false;
    bool m_inputBlocked = false;
    bool m_macroWasBusy = false;
    QString m_gameScript;
    QSize m_frameSize;
};

#endif // CONTROLLER_H
