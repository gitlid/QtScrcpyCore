#pragma once
#include <QPointer>
#include <QMouseEvent>

#include "QtScrcpyCoreDef.h"

namespace qsc {

class DeviceObserver {
protected:
    DeviceObserver() {

    }
    virtual ~DeviceObserver() {

    }

public:
    virtual void onFrame(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV) {
        Q_UNUSED(width);
        Q_UNUSED(height);
        Q_UNUSED(dataY);
        Q_UNUSED(dataU);
        Q_UNUSED(dataV);
        Q_UNUSED(linesizeY);
        Q_UNUSED(linesizeU);
        Q_UNUSED(linesizeV);
    }
    // VideoToolbox Metal 路径帧回调（仅 macOS arm64）
    virtual void onFrameMetal(void* cvPixelBuffer, int width, int height) {
        Q_UNUSED(cvPixelBuffer);
        Q_UNUSED(width);
        Q_UNUSED(height);
    }
    virtual void updateFPS(quint32 fps) { Q_UNUSED(fps); }
    virtual void onVideoSessionChanged(const QSize &size, bool clientResized) {
        Q_UNUSED(size);
        Q_UNUSED(clientResized);
    }
    virtual void grabCursor(bool grab) {Q_UNUSED(grab);}

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) {
        Q_UNUSED(from);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
    }
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) {
        Q_UNUSED(from);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
    }
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) {
        Q_UNUSED(from);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
    }

    virtual void postGoBack() {}
    virtual void postGoHome() {}
    virtual void postGoMenu() {}
    virtual void postAppSwitch() {}
    virtual void postPower() {}
    virtual void postVolumeUp() {}
    virtual void postVolumeDown() {}
    virtual void postCopy() {}
    virtual void postCut() {}
    virtual void setDisplayPower(bool on)
    {
        Q_UNUSED(on);
    }
    virtual void expandNotificationPanel() {}
    virtual void expandSettingsPanel() {}
    virtual void collapsePanel() {}
    virtual void rotateDevice() {}
    virtual void startApp(const QString &name) { Q_UNUSED(name); }
    virtual void postBackOrScreenOn(bool down) { Q_UNUSED(down); }
    virtual void postTextInput(QString &text) { Q_UNUSED(text); }
    virtual void requestDeviceClipboard() {}
    virtual void setDeviceClipboard(bool pause = true) { Q_UNUSED(pause); }
    virtual void clipboardPaste() {}
    virtual void pushFileRequest(const QString &file, const QString &devicePath) {
        Q_UNUSED(file);
        Q_UNUSED(devicePath);
    }
    virtual void installApkRequest(const QString &apkFile) { Q_UNUSED(apkFile); }
    virtual void screenshot() {}
    virtual void showTouch(bool show) { Q_UNUSED(show); }
};

class IDevice : public QObject {
    Q_OBJECT
public:
    IDevice(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IDevice(){}

signals:
    void deviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void deviceDisconnected(QString serial);
    void actionMacroStateChanged(bool recording, bool playing, int eventCount);
    void actionMacroProgress(int currentEvent, int totalEvents, int currentLoop, int totalLoops);
    void actionMacroError(const QString &message);

public:
    virtual void setUserData(void* data) = 0;
    virtual void* getUserData() = 0;
    virtual void registerDeviceObserver(DeviceObserver* observer) = 0;
    virtual void deRegisterDeviceObserver(DeviceObserver* observer) = 0;

    virtual bool connectDevice() = 0;
    virtual void disconnectDevice() = 0;

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) = 0;
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) = 0;
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) = 0;

    virtual void postGoBack() = 0;
    virtual void postGoHome() = 0;
    virtual void postGoMenu() = 0;
    virtual void postAppSwitch() = 0;
    virtual void postPower() = 0;
    virtual void postVolumeUp() = 0;
    virtual void postVolumeDown() = 0;
    virtual void postCopy() = 0;
    virtual void postCut() = 0;
    virtual void setDisplayPower(bool on) = 0;
    virtual void expandNotificationPanel() = 0;
    virtual void expandSettingsPanel() = 0;
    virtual void collapsePanel() = 0;
    virtual void rotateDevice() = 0;
    virtual void startApp(const QString &name) = 0;
    virtual void resizeDisplay(const QSize &size) = 0;
    virtual void postBackOrScreenOn(bool down) = 0;
    virtual void postTextInput(QString &text) = 0;
    virtual void requestDeviceClipboard() = 0;
    virtual void setDeviceClipboard(bool pause = true) = 0;
    virtual void clipboardPaste() = 0;
    virtual void pushFileRequest(const QString &file, const QString &devicePath = "") = 0;
    virtual void installApkRequest(const QString &apkFile) = 0;

    virtual void screenshot() = 0;
    virtual void showTouch(bool show) = 0;
    virtual void setCameraTorch(bool on) { Q_UNUSED(on); }
    virtual void cameraZoomIn() {}
    virtual void cameraZoomOut() {}

    virtual bool isReversePort(quint16 port) = 0;
    virtual const QString &getSerial() = 0;
    virtual bool isCameraMode() const { return false; }
    virtual bool isFlexDisplay() const { return false; }

    virtual void updateScript(QString script) = 0;
    virtual bool isCurrentCustomKeymap() = 0;

    virtual bool isUhidKeyboardEnabled() const = 0;
    virtual void releaseKeyboard() = 0;
    virtual bool startActionRecording() = 0;
    virtual bool stopActionRecording() = 0;
    virtual bool saveActionMacro(const QString &fileName, QString *error = Q_NULLPTR) const = 0;
    virtual bool loadActionMacro(const QString &fileName, QString *error = Q_NULLPTR) = 0;
    // repeatCount == 0 means repeat until stopActionPlayback() is called.
    virtual bool playActionMacro(int repeatCount = 1, int intervalMs = 0) = 0;
    virtual bool playActionMacroAdvanced(int repeats, int interval, double speed, qint64 limitMs)
    { return speed == 1.0 && limitMs == 0 && playActionMacro(repeats, interval); }
    virtual QString currentKeymapScript() const { return QString(); }
    virtual void prepareKeymapEditing() {}
    virtual bool pauseActionMacro() { return false; }
    virtual bool resumeActionMacro() { return false; }
    virtual bool isActionPaused() const { return false; }
    virtual bool actionMacroInterruptedInput() const { return false; }
    virtual qint64 actionMacroElapsedMs() const { return 0; }
    virtual void stopActionPlayback() = 0;
    virtual bool isActionRecording() const = 0;
    virtual bool isActionPlaying() const = 0;
    virtual int actionMacroEventCount() const = 0;
};

class IDeviceManage : public QObject {
    Q_OBJECT
public:
    static IDeviceManage& getInstance();
    virtual bool connectDevice(DeviceParams params) = 0;
    virtual bool disconnectDevice(const QString &serial) = 0;
    virtual void disconnectAllDevice() = 0;
    virtual QPointer<IDevice> getDevice(const QString& serial) = 0;

signals:
    void deviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void deviceDisconnected(QString serial);
};

}
