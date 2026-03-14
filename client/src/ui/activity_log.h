#pragma once

#include <QListWidget>
#include <QDateTime>

/// Activity log widget — capped scrolling list of events.
/// Displays timestamped messages for joins, leaves, errors, etc.
class ActivityLog : public QListWidget {
    Q_OBJECT

public:
    explicit ActivityLog(QWidget* parent = nullptr);

    /// Add a log entry with auto-timestamp.
    void addEntry(const QString& message);

    /// Add a log entry with a color hint.
    void addEntry(const QString& message, const QColor& color);

    /// Clear all entries.
    void clearLog();

private:
    static constexpr int MaxEntries = 1000;
};
