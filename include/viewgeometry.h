#ifndef QSC_VIEWGEOMETRY_H
#define QSC_VIEWGEOMETRY_H
#include <QMouseEvent>
#include <QSize>
#include <QtGlobal>

namespace qsc {
// Clockwise quarter turns of the desktop view only. Frame/recording coordinates
// are NEVER rotated. The same inverse is used by the video shader and input.
namespace ViewGeometry {
inline int normalized(int turns) { return (turns % 4 + 4) % 4; }
inline QSize sourceSize(const QSize &view, int turns) {
    return normalized(turns) % 2 ? QSize(view.height(), view.width()) : view;
}
inline QPointF inverseUnit(const QPointF &p, int turns) {
    switch (normalized(turns)) {
    case 1: return QPointF(p.y(), 1.0 - p.x());
    case 2: return QPointF(1.0 - p.x(), 1.0 - p.y());
    case 3: return QPointF(1.0 - p.y(), p.x());
    default: return p;
    }
}
inline QPointF toSource(const QPointF &p, const QSize &view, int turns) {
    if (view.width() <= 1 || view.height() <= 1) return QPointF();
    const QPointF unit(qBound(0.0, p.x() / (view.width() - 1), 1.0),
                       qBound(0.0, p.y() / (view.height() - 1), 1.0));
    const auto mapped = inverseUnit(unit, turns);
    const auto source = sourceSize(view, turns);
    return QPointF(mapped.x() * (source.width() - 1), mapped.y() * (source.height() - 1));
}
inline QPointF toView(const QPointF &p, const QSize &view, int turns) {
    return toSource(p, sourceSize(view, turns), -turns);
}
inline QPoint deltaToSource(const QPoint &p, int turns) {
    switch (normalized(turns)) {
    case 1: return QPoint(p.y(), -p.x());
    case 2: return -p;
    case 3: return QPoint(-p.y(), p.x());
    default: return p;
    }
}
}

// Controller dispatch is synchronous. Carry the actual desktop transform with
// each event so relative mouse-look edge warps also work in a rotated view.
// Ordinary clients can continue to supply plain QMouseEvent instances.
class ViewMouseEvent : public QMouseEvent {
public:
    ViewMouseEvent(QEvent::Type type, const QPointF &local, const QPointF &global,
                   Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers,
                   const QPoint &viewOrigin, const QSize &viewSize, int turns)
        : QMouseEvent(type, local, global, button, buttons, modifiers),
          m_origin(viewOrigin), m_size(viewSize), m_turns(turns) {}
    QPoint desktopPosition(const QPointF &sourcePosition) const {
        return m_origin + ViewGeometry::toView(sourcePosition, m_size, m_turns).toPoint();
    }
private:
    QPoint m_origin;
    QSize m_size;
    int m_turns;
};
}
#endif
