#pragma once

#include <QTreeWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>

/// Channel tree widget displaying available voice channels.
/// Supports hierarchical channels (parent_id), user counts, and password indicators.
class ChannelTree : public QTreeWidget {
    Q_OBJECT

public:
    explicit ChannelTree(QWidget* parent = nullptr);

    /// Set the full channel list (replaces existing items).
    void setChannels(const QJsonArray& channels);

    /// Update a single channel (name change, user count, etc.).
    void updateChannel(const QJsonObject& channel);

    /// Remove a channel by ID.
    void removeChannel(int channelId);

    /// Highlight the currently joined channel (single-channel mode, legacy).
    void setCurrentChannel(int channelId);

    /// Mark a channel as joined (bold + icon). Supports multiple joined channels.
    void addJoinedChannel(int channelId);

    /// Unmark a channel as joined.
    void removeJoinedChannel(int channelId);

    /// Clear all joined-channel highlights.
    void clearJoinedChannels();

    /// Update the user count for a channel.
    void setUserCount(int channelId, int count);

    /// Get a map of channelId -> channelName for all known channels.
    QMap<int, QString> channelNames() const;

signals:
    /// Emitted when a channel is double-clicked (request to join).
    void channelActivated(int channelId);

    /// Emitted when user requests to leave a channel (via context menu).
    void channelLeaveRequested(int channelId);

    /// Emitted when user toggles channel mute (via context menu).
    void channelMuteToggled(int channelId, bool muted);

    /// Emitted when user toggles open mic for a channel (via context menu).
    void channelOpenMicToggled(int channelId, bool enabled);

private:
    void buildTree(const QJsonArray& channels);
    QTreeWidgetItem* findItem(int channelId) const;
    void updateItemText(QTreeWidgetItem* item, const QJsonObject& channel);

    QMap<int, QTreeWidgetItem*> m_items; // channelId -> item
    int m_currentChannelId = -1;
    QSet<int> m_joinedChannels;
    QSet<int> m_mutedChannels;
    QSet<int> m_openMicChannels;
};
