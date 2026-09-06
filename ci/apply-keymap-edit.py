from pathlib import Path
root=Path(__file__).resolve().parents[1]
if 'currentKeymapScript' in (root/'include/QtScrcpyCore.h').read_text():
    raise SystemExit(0)
def edit(p,o,n):
 f=root/p;t=f.read_text(encoding='utf-8-sig');assert t.count(o)==1,(p,o);f.write_text(t.replace(o,n),encoding='utf8')
edit('include/QtScrcpyCore.h','    virtual bool pauseActionMacro() { return false; }','''    virtual QString currentKeymapScript() const { return QString(); }
    virtual void prepareKeymapEditing() {}
    virtual bool pauseActionMacro() { return false; }''')
edit('src/device/controller/controller.h','    bool pauseActionMacro();','''    QString currentKeymapScript() const { return m_gameScript; }
    void prepareKeymapEditing() { resetInputState(true); }
    bool pauseActionMacro();''')
edit('src/device/device.h','    bool pauseActionMacro();','''    QString currentKeymapScript() const override;
    void prepareKeymapEditing() override;
    bool pauseActionMacro();''')
edit('src/device/device.cpp','bool Device::pauseActionMacro()', '''QString Device::currentKeymapScript() const { return m_controller ? m_controller->currentKeymapScript() : QString(); }
void Device::prepareKeymapEditing() { if (m_controller && !isActionPlaying() && !isActionRecording()) { m_controller->prepareKeymapEditing(); } }
bool Device::pauseActionMacro()''')
