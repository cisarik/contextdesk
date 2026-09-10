#include "app/ChordRecorder.h"

#include "core/ControlCatalog.h"

#include <QFocusEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLocale>

namespace contextdeck {
namespace {

QString qtKeyToChordKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar::fromLatin1(static_cast<char>('a' + (key - Qt::Key_A))));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar::fromLatin1(static_cast<char>('0' + (key - Qt::Key_0))));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return QStringLiteral("f%1").arg(1 + (key - Qt::Key_F1));
    }
    switch (key) {
    case Qt::Key_Escape:
        return QStringLiteral("esc");
    case Qt::Key_Tab:
        return QStringLiteral("tab");
    case Qt::Key_Space:
        return QStringLiteral("space");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("enter");
    case Qt::Key_Backspace:
        return QStringLiteral("backspace");
    case Qt::Key_Insert:
        return QStringLiteral("insert");
    case Qt::Key_Delete:
        return QStringLiteral("delete");
    case Qt::Key_Home:
        return QStringLiteral("home");
    case Qt::Key_End:
        return QStringLiteral("end");
    case Qt::Key_PageUp:
        return QStringLiteral("pageup");
    case Qt::Key_PageDown:
        return QStringLiteral("pagedown");
    case Qt::Key_Up:
        return QStringLiteral("up");
    case Qt::Key_Down:
        return QStringLiteral("down");
    case Qt::Key_Left:
        return QStringLiteral("left");
    case Qt::Key_Right:
        return QStringLiteral("right");
    case Qt::Key_Minus:
        return QStringLiteral("minus");
    case Qt::Key_Equal:
        return QStringLiteral("equal");
    case Qt::Key_Comma:
        return QStringLiteral("comma");
    case Qt::Key_Period:
        return QStringLiteral("dot");
    case Qt::Key_Slash:
        return QStringLiteral("slash");
    case Qt::Key_Semicolon:
        return QStringLiteral("semicolon");
    default:
        return {};
    }
}

QStringList modifiersFromEvent(const QKeyEvent *event)
{
    QStringList modifiers;
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        modifiers << QStringLiteral("ctrl");
    }
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        modifiers << QStringLiteral("shift");
    }
    if (event->modifiers().testFlag(Qt::AltModifier)) {
        modifiers << QStringLiteral("alt");
    }
    if (event->modifiers().testFlag(Qt::MetaModifier)) {
        modifiers << QStringLiteral("super");
    }
    return modifiers;
}

} // namespace

ChordRecorder::ChordRecorder(QQuickItem *parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(ItemIsFocusScope, true);
    setAcceptHoverEvents(false);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(8000);
    connect(&m_timeout, &QTimer::timeout, this, &ChordRecorder::cancel);
    refreshLayout();
}

void ChordRecorder::begin()
{
    m_recording = true;
    m_key.clear();
    m_modifiers.clear();
    refreshLayout();
    updateDisplay();
    emit recordingChanged();
    emit chordChanged();
    setFocus(true);
    forceActiveFocus(Qt::TabFocusReason);
    m_timeout.start();
}

void ChordRecorder::cancel()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    m_timeout.stop();
    updateDisplay();
    emit recordingChanged();
}

void ChordRecorder::keyPressEvent(QKeyEvent *event)
{
    if (!m_recording) {
        event->ignore();
        return;
    }
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift || event->key() == Qt::Key_Alt
        || event->key() == Qt::Key_Meta || event->key() == Qt::Key_AltGr) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancel();
        event->accept();
        return;
    }
    const QString key = qtKeyToChordKey(event->key());
    if (key.isEmpty() || !isAllowedChordKey(key) || chordKeyLooksLikeShellOrPath(key)) {
        event->accept();
        return;
    }
    finish(key, modifiersFromEvent(event));
    event->accept();
}

void ChordRecorder::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    cancel();
}

void ChordRecorder::refreshLayout()
{
    QString layout = QLocale::system().name();
    if (QGuiApplication::inputMethod() != nullptr && QGuiApplication::inputMethod()->locale().name().size() > 0) {
        layout = QGuiApplication::inputMethod()->locale().name();
    }
    if (layout != m_layoutContext) {
        m_layoutContext = layout;
        emit layoutContextChanged();
    }
}

void ChordRecorder::finish(const QString &key, const QStringList &modifiers)
{
    m_key = key;
    m_modifiers = modifiers;
    m_recording = false;
    m_timeout.stop();
    updateDisplay();
    emit recordingChanged();
    emit chordChanged();
}

void ChordRecorder::updateDisplay()
{
    QString text;
    if (m_recording) {
        text = QStringLiteral("Recording… press a key (Esc cancels)");
    } else if (!m_key.isEmpty()) {
        text = (m_modifiers + QStringList{m_key}).join(QLatin1Char('+'));
    } else {
        text = QStringLiteral("No chord recorded");
    }
    if (text != m_display) {
        m_display = text;
        emit displayChanged();
    }
}

} // namespace contextdeck
