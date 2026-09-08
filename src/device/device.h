#ifndef DEVICE_H
#define DEVICE_H

#include <set>
#include <QElapsedTimer>
#include <QPointer>
#include <QTime>

#include "../../include/QtScrcpyCore.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class Recorder;
class Server;
class VideoBuffer;
class IDecoder;
class FileHandler;
class Demuxer;
class VideoForm;
class Controller;
struct AVFrame;

namespace qsc {

class Device : public IDevice
{
    Q_OBJECT
public:
    explicit Device(DeviceParams params, QObject *parent = nullptr);
    virtual ~Device();

    void setUserData(void* data) override;
    void* getUserData() override;

    void registerDeviceObserver(DeviceObserver* observer) override;
    void deRegisterDeviceObserver(DeviceObserver* observer) override;

    bool connectDevice() override;
    void disconnectDevice() override;

    // key map
    void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize) override;
    void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize) override;
    void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) override;

    void postGoBack() override;
    void postGoHome() override;
    void postGoMenu() override;
    void postAppSwitch() override;
    void postPower() override;
    void postVolumeUp() override;
    void postVolumeDown() override;
    void postCopy() override;
    void postCut() override;
    void setDisplayPower(bool on) override;
    void expandNotificationPanel() override;
    void expandSettingsPanel() override;
    void collapsePanel() override;
    void rotateDevice() override;
    void startApp(const QString &name) override;
    void resizeDisplay(const QSize &size) override;
    void postBackOrScreenOn(bool down) override;
    void postTextInput(QString &text) override;
    void requestDeviceClipboard() override;
    void setDeviceClipboard(bool pause = true) override;
    void clipboardPaste() override;
    void pushFileRequest(const QString &file, const QString &devicePath = "") override;
    void installApkRequest(const QString &apkFile) override;

    void screenshot() override;
    void showTouch(bool show) override;
    void setCameraTorch(bool on) override;
    void cameraZoomIn() override;
    void cameraZoomOut() override;

    bool isReversePort(quint16 port) override;
    const QString &getSerial() override;
    bool isCameraMode() const override;
    bool isFlexDisplay() const override;

    void updateScript(QString script) override;
    bool applyAppKeymap(const QString &script) override;
    void setActionMacroApplicationBound(bool enabled) override;
    bool actionMacroScreenMatches() const override;
    bool isCurrentCustomKeymap() override;

    bool isUhidKeyboardEnabled() const override;
    void releaseKeyboard() override;
    bool startActionRecording() override;
    bool stopActionRecording() override;
    bool saveActionMacro(const QString &fileName, QString *error = Q_NULLPTR) const override;
    bool loadActionMacro(const QString &fileName, QString *error = Q_NULLPTR) override;
    bool playActionMacro(int repeatCount = 1, int intervalMs = 0) override;
    bool playActionMacroAdvanced(int repeatCount, int intervalMs, double speed, qint64 limitMs);
    QString currentKeymapScript() const override;
    void prepareKeymapEditing() override;
    bool pauseActionMacro();
    bool resumeActionMacro();
    bool isActionPaused() const;
    bool actionMacroInterruptedInput() const;
    qint64 actionMacroElapsedMs() const;
    void stopActionPlayback() override;
    bool isActionRecording() const override;
    bool isActionPlaying() const override;
    int actionMacroEventCount() const override;

private:
    void initSignals();
    bool saveFrame(int width, int height, uint8_t* dataRGB32);

private:
    // server relevant
    QPointer<Server> m_server;
    bool m_serverStartSuccess = false;
    QPointer<IDecoder> m_decoder;
    QPointer<Controller> m_controller;
    QPointer<FileHandler> m_fileHandler;
    QPointer<Demuxer> m_stream;
    QPointer<Recorder> m_recorder;

    QElapsedTimer m_startTimeCount;
    DeviceParams m_params;
    std::set<DeviceObserver*> m_deviceObservers;
    void* m_userData = nullptr;
};

}

#endif // DEVICE_H
