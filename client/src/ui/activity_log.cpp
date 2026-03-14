#include "activity_log.h"

ActivityLog::ActivityLog(QWidget* parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::NoSelection);
    setWordWrap(true);
}

void ActivityLog::addEntry(const QString& message) {
    addEntry(message, QColor(180, 180, 180));
}

void ActivityLog::addEntry(const QString& message, const QColor& color) {
    // Enforce max entries
    while (count() >= MaxEntries) {
        delete takeItem(0);
    }

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString text = QString("[%1] %2").arg(timestamp, message);

    auto* item = new QListWidgetItem(text, this);
    item->setForeground(color);
    addItem(item);
    scrollToBottom();
}

void ActivityLog::clearLog() {
    clear();
}
