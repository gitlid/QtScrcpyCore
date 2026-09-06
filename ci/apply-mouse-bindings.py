"""Apply the reviewed mouse binding changes to the exact v2 baseline once.
The CI job compiles/tests the resulting sources before committing them.
"""
from pathlib import Path
import hashlib

root = Path(__file__).resolve().parents[1]
expected = {
    'src/device/controller/controller.cpp': 'b7da5912d24d0c64e96a191a3fd8ef5b2f55eab22532c39030c8d948f318be51',
    'src/device/controller/inputconvert/inputconvertgame.cpp': '243cb7d7c51d1fdf7f6e2735a6daa52b2abf419c5c5351c126703752bc478f38',
    'src/device/controller/inputconvert/keymap/keymap.cpp': '5fb2fd9aea1b789c3857e00c2776eb7bedb4a5f8f0ee3f6ca2c0327bbeee6b30',
}
game = root / 'src/device/controller/inputconvert/inputconvertgame.cpp'
if 'int mouseBindingKey(int button)' in game.read_text():
    print('Mouse binding source migration already applied.')
    raise SystemExit(0)
for path, digest in expected.items():
    if hashlib.sha256((root / path).read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Unexpected baseline: {path}; refusing to overwrite it')

s = game.read_text()
s = s.replace('#define CURSOR_POS_CHECK 50', '''#define CURSOR_POS_CHECK 50

namespace {
// Mouse flags overlap keyboard numbers (TaskButton and Space are both 32).
// Give mouse touches distinct internal ownership IDs, including mouse look.
int mouseBindingKey(int button) { return -button; }
int bindingKey(const KeyMap::KeyNode &node)
{
    return node.type == KeyMap::AT_MOUSE ? mouseBindingKey(node.key) : node.key;
}
const int kMouseLookTouchKey = -0x10000000;
}''')
a = s.index('void InputConvertGame::mouseEvent(')
b = s.index('\nvoid InputConvertGame::wheelEvent', a)
s = s[:a] + '''void InputConvertGame::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!from || !frameSize.isValid() || !showSize.isValid()) { return; }
    if (!m_keyMap.isSwitchOnKeyboard() && m_keyMap.getSwitchKey() == int(from->button())) {
        if (from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonDblClick) {
            if (!switchGameMap()) { m_needBackMouseMove = false; }
        }
        return; // The toggle never also becomes a phone click.
    }
    if (m_gameMap) {
        updateSize(frameSize, showSize);
        const auto &node = m_keyMap.getKeyMapNodeMouse(from->button());
        if (m_needBackMouseMove && node.type == KeyMap::KMT_CLICK && node.data.click.switchMap) {
            if (processMouseClick(from)) { return; }
        }
        if (!m_needBackMouseMove) {
            if (m_keyMap.isValidMouseMoveMap() && processMouseMove(from)) { return; }
            if (processMouseClick(from)) { return; }
            if (from->type() == QEvent::MouseMove) {
                // Do not leak a second ordinary touch while holding a mapped button.
                for (unsigned int bit = 1; bit <= unsigned(Qt::MaxMouseButton); bit <<= 1) {
                    if ((int(from->buttons()) & int(bit))
                        && m_keyMap.getKeyMapNodeMouse(int(bit)).type != KeyMap::KMT_INVALID) { return; }
                }
            }
        }
    }
    InputConvertNormal::mouseEvent(from, frameSize, showSize);
}
''' + s[b:]
for direction in ('up', 'right', 'down'):
    s = s.replace(f'key == node.data.steerWheel.{direction}.key', f'key == bindingKey(node.data.steerWheel.{direction})')
s = s.replace('''        sendTouchUpEvent(getTouchID(m_ctrlSteerWheel.touchKey), m_ctrlSteerWheel.delayData.currentPos);
        detachTouchID(m_ctrlSteerWheel.touchKey);''', '''        const int held = getTouchID(m_ctrlSteerWheel.touchKey);
        if (held >= 0) { sendTouchUpEvent(held, m_ctrlSteerWheel.delayData.currentPos); }
        detachTouchID(m_ctrlSteerWheel.touchKey);''')
s = s.replace('''        int id = attachTouchID(m_ctrlSteerWheel.touchKey);
        sendTouchDownEvent(id, node.data.steerWheel.centerPos);''', '''        int id = attachTouchID(m_ctrlSteerWheel.touchKey);
        if (id < 0) { return; }
        m_ctrlSteerWheel.delayData.currentPos = node.data.steerWheel.centerPos;
        sendTouchDownEvent(id, node.data.steerWheel.centerPos);''')
a = s.index('void InputConvertGame::processKeyClick(')
b = s.index('\nvoid InputConvertGame::processKeyClickMulti', a)
s = s[:a] + '''void InputConvertGame::processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from)
{
    const int key = from->key();
    if (from->type() == QEvent::KeyPress) {
        if (getTouchID(key) >= 0) { return; }
        const int id = attachTouchID(key);
        if (id < 0) { return; }
        sendTouchDownEvent(id, clickPos);
        if (clickTwice) { sendTouchUpEvent(id, clickPos); detachTouchID(key); }
    } else if (from->type() == QEvent::KeyRelease) {
        int id = getTouchID(key);
        if (clickTwice) {
            id = attachTouchID(key);
            if (id < 0) { return; }
            sendTouchDownEvent(id, clickPos);
        }
        if (id >= 0) { sendTouchUpEvent(id, clickPos); }
        detachTouchID(key);
        if (switchMap && id >= 0) {
            m_needBackMouseMove = !m_needBackMouseMove;
            hideMouseCursor(!m_needBackMouseMove);
        }
    }
}
''' + s[b:]
a = s.index('bool InputConvertGame::processMouseClick(')
b = s.index('\nbool InputConvertGame::processMouseMove', a)
s = s[:a] + '''bool InputConvertGame::processMouseClick(const QMouseEvent *from)
{
    const bool press = from->type() == QEvent::MouseButtonPress || from->type() == QEvent::MouseButtonDblClick;
    if (!press && from->type() != QEvent::MouseButtonRelease) { return false; }
    const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeMouse(int(from->button()));
    if (node.type == KeyMap::KMT_INVALID) { return false; }
    // Reuse the action implementation, not Controller::keyEvent/UHID.
    // Select the correct union member instead of treating all nodes as clicks.
    QKeyEvent mapped(press ? QEvent::KeyPress : QEvent::KeyRelease,
                     mouseBindingKey(int(from->button())), from->modifiers());
    switch (node.type) {
    case KeyMap::KMT_CLICK:
        processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, &mapped);
        processAndroidKey(node.data.click.keyNode.androidKey, &mapped);
        break;
    case KeyMap::KMT_CLICK_TWICE:
        processKeyClick(node.data.clickTwice.keyNode.pos, true, false, &mapped);
        processAndroidKey(node.data.clickTwice.keyNode.androidKey, &mapped);
        break;
    case KeyMap::KMT_CLICK_MULTI:
        processKeyClickMulti(node.data.clickMulti.keyNode.delayClickNodes, node.data.clickMulti.keyNode.delayClickNodesCount, &mapped);
        break;
    case KeyMap::KMT_STEER_WHEEL: processSteerWheel(node, &mapped); break;
    case KeyMap::KMT_DRAG:
        processKeyDrag(node.data.drag.keyNode.pos, node.data.drag.keyNode.extendPos,
                       node.data.drag.startDelay, node.data.drag.dragSpeed, &mapped);
        break;
    case KeyMap::KMT_ANDROID_KEY: processAndroidKey(node.data.androidKey.keyNode.androidKey, &mapped); break;
    default: return false;
    }
    return true;
}
''' + s[b:]
for method in ('getTouchID', 'attachTouchID', 'detachTouchID'):
    s = s.replace(f'{method}(Qt::ExtraButton24)', f'{method}(kMouseLookTouchKey)')
game.write_text(s)
p = root / 'src/device/controller/controller.cpp'
s = p.read_text()
old = '''    if (m_inputConvert) {
        m_inputConvert->mouseEvent(from, frameSize, showSize);
    }
}'''
new = '''    if (m_inputConvert) {
        const bool wasGameMap = isCurrentCustomKeymap();
        m_inputConvert->mouseEvent(from, frameSize, showSize);
        if (wasGameMap != isCurrentCustomKeymap()) {
            if (isCurrentCustomKeymap()) { releaseKeyboard(); }
            else { resetInputState(); }
        }
    }
}'''
assert s.count(old) == 1
s = s.replace(old, new)
s = s.replace('''        if (wasGameMap != isCurrentCustomKeymap()) { releaseKeyboard(); }''', '''        if (wasGameMap != isCurrentCustomKeymap()) {
            if (isCurrentCustomKeymap()) { releaseKeyboard(); }
            else { resetInputState(); }
        }''')
p.write_text(s)
p = root / 'src/device/controller/inputconvert/keymap/keymap.cpp'
s = p.read_text().replace('''    if (key == -1 && btn == -1) {
        return { AT_INVALID, -1 };
    } else if (key != -1) {
        return { AT_KEY, key };
    } else {
        return { AT_MOUSE, btn };
    }''', '''    if (key != -1 && key != Qt::Key_unknown) { return { AT_KEY, key }; }
    if (btn > 0 && (btn & (btn - 1)) == 0 && btn <= int(Qt::MaxMouseButton)) {
        return { AT_MOUSE, btn };
    }
    return { AT_INVALID, -1 };''')
p.write_text(s)
