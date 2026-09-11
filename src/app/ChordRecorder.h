#pragma once

#include <QQuickItem>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace contextdeck {

class ChordRecorder : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QString display READ display NOTIFY displayChanged)
    Q_PROPERTY(QString layoutContext READ layoutContext NOTIFY layoutContextChanged)
    Q_PROPERTY(QString key READ key NOTIFY chordChanged)
    Q_PROPERTY(QStringList modifiers READ modifiers NOTIFY chordChanged)

public:
    explicit ChordRecorder(QQuickItem *parent = nullptr);

    [[nodiscard]] bool recording() const { return m_recording; }
    [[nodiscard]] QString display() const { return m_display; }
    [[nodiscard]] QString layoutContext() const { return m_layoutContext; }
    [[nodiscard]] QString key() const { return m_key; }
    [[nodiscard]] QStringList modifiers() const { return m_modifiers; }

    Q_INVOKABLE void begin();
    Q_INVOKABLE void cancel();

signals:
    void recordingChanged();
    void displayChanged();
    void layoutContextChanged();
    void chordChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void refreshLayout();
    void finish(const QString &key, const QStringList &modifiers);
    void updateDisplay();

    bool m_recording = false;
    QString m_display;
    QString m_layoutContext;
    QString m_key;
    QStringList m_modifiers;
    QTimer m_timeout;
};

} // namespace contextdeck
