#include "channel_tree.h"

#include <QHeaderView>
#include <QMenu>
#include <algorithm>

ChannelTree::ChannelTree(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderLabel("Channels");
    setRootIsDecorated(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setExpandsOnDoubleClick(false);

    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (!item) return;
        int channelId = item->data(0, Qt::UserRole).toInt();
        if (channelId > 0) {
            emit channelActivated(channelId);
        }
    });

    // Right-click context menu for leaving channels
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = itemAt(pos);
        if (!item) return;
        int channelId = item->data(0, Qt::UserRole).toInt();
        if (channelId <= 0 || !m_joinedChannels.contains(channelId)) return;

        QMenu menu(this);

        bool isMuted   = m_mutedChannels.contains(channelId);
        bool isOpenMic = m_openMicChannels.contains(channelId);

        menu.addAction(isMuted ? "Unmute Channel" : "Mute Channel", this, [this, channelId, isMuted]() {
            if (isMuted) {
                m_mutedChannels.remove(channelId);
            } else {
                m_mutedChannels.insert(channelId);
            }
            QTreeWidgetItem* i = findItem(channelId);
            if (i) {
                bool om = m_openMicChannels.contains(channelId);
                bool mu = m_mutedChannels.contains(channelId);
                i->setForeground(0, QBrush(mu ? QColor(128, 128, 128)
                                         : om ? QColor(255, 165, 0)
                                              : QColor(0, 200, 0)));
            }
            emit channelMuteToggled(channelId, !isMuted);
        });

        menu.addAction(isOpenMic ? "Disable Open Mic" : "Enable Open Mic", this, [this, channelId, isOpenMic]() {
            if (isOpenMic) {
                m_openMicChannels.remove(channelId);
            } else {
                m_openMicChannels.insert(channelId);
            }
            QTreeWidgetItem* i = findItem(channelId);
            if (i) {
                bool om = m_openMicChannels.contains(channelId);
                bool mu = m_mutedChannels.contains(channelId);
                i->setForeground(0, QBrush(mu ? QColor(128, 128, 128)
                                         : om ? QColor(255, 165, 0)   // orange = open mic
                                              : QColor(0, 200, 0)));  // green  = normal joined
            }
            emit channelOpenMicToggled(channelId, !isOpenMic);
        });

        menu.addSeparator();
        menu.addAction("Leave Channel", this, [this, channelId]() {
            emit channelLeaveRequested(channelId);
        });
        menu.exec(viewport()->mapToGlobal(pos));
    });
}

void ChannelTree::setChannels(const QJsonArray& channels) {
    clear();
    m_items.clear();
    buildTree(channels);
    expandAll();
}

void ChannelTree::buildTree(const QJsonArray& channels) {
    // First pass: create all items as top-level
    QMap<int, QJsonObject> channelMap;
    for (const auto& val : channels) {
        QJsonObject ch = val.toObject();
        int id = ch["id"].toInt();
        channelMap[id] = ch;
    }

    // Sort by position
    QList<QJsonObject> sorted;
    for (const auto& ch : channelMap) {
        sorted.append(ch);
    }
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a["position"].toInt() < b["position"].toInt();
    });

    // Build items — two passes: first create all, then reparent
    for (const auto& ch : sorted) {
        int id = ch["id"].toInt();
        auto* item = new QTreeWidgetItem();
        item->setData(0, Qt::UserRole, id);
        updateItemText(item, ch);
        m_items[id] = item;
    }

    // Second pass: attach to parents or root
    for (const auto& ch : sorted) {
        int id = ch["id"].toInt();
        int parentId = ch["parent_id"].toInt(0);
        QTreeWidgetItem* item = m_items[id];

        if (parentId > 0 && m_items.contains(parentId)) {
            m_items[parentId]->addChild(item);
        } else {
            addTopLevelItem(item);
        }
    }
}

void ChannelTree::updateChannel(const QJsonObject& channel) {
    int id = channel["id"].toInt();
    QTreeWidgetItem* item = findItem(id);
    if (item) {
        updateItemText(item, channel);
    }
}

void ChannelTree::removeChannel(int channelId) {
    QTreeWidgetItem* item = findItem(channelId);
    if (item) {
        delete item;
        m_items.remove(channelId);
    }
}

void ChannelTree::setCurrentChannel(int channelId) {
    // Remove highlight from previous
    if (m_currentChannelId > 0) {
        QTreeWidgetItem* prev = findItem(m_currentChannelId);
        if (prev && !m_joinedChannels.contains(m_currentChannelId)) {
            QFont f = prev->font(0);
            f.setBold(false);
            prev->setFont(0, f);
        }
    }

    m_currentChannelId = channelId;

    // Highlight current
    QTreeWidgetItem* current = findItem(channelId);
    if (current) {
        QFont f = current->font(0);
        f.setBold(true);
        current->setFont(0, f);
        setCurrentItem(current);
    }
}

void ChannelTree::addJoinedChannel(int channelId) {
    m_joinedChannels.insert(channelId);
    QTreeWidgetItem* item = findItem(channelId);
    if (item) {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setForeground(0, QBrush(QColor(0, 200, 0))); // green = joined
    }
}

void ChannelTree::removeJoinedChannel(int channelId) {
    m_joinedChannels.remove(channelId);
    m_openMicChannels.remove(channelId);
    QTreeWidgetItem* item = findItem(channelId);
    if (item) {
        QFont f = item->font(0);
        f.setBold(channelId == m_currentChannelId);
        item->setFont(0, f);
        item->setForeground(0, QBrush()); // reset to default color
    }
}

void ChannelTree::clearJoinedChannels() {
    for (int chId : m_joinedChannels) {
        QTreeWidgetItem* item = findItem(chId);
        if (item) {
            QFont f = item->font(0);
            f.setBold(false);
            item->setFont(0, f);
            item->setForeground(0, QBrush());
        }
    }
    m_joinedChannels.clear();
}

void ChannelTree::setUserCount(int channelId, int count) {
    QTreeWidgetItem* item = findItem(channelId);
    if (item) {
        // Update the display text with user count
        QString name = item->data(0, Qt::UserRole + 1).toString();
        if (count > 0) {
            item->setText(0, QString("%1 (%2)").arg(name).arg(count));
        } else {
            item->setText(0, name);
        }
    }
}

QMap<int, QString> ChannelTree::channelNames() const {
    QMap<int, QString> result;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        result[it.key()] = it.value()->data(0, Qt::UserRole + 1).toString();
    }
    return result;
}

QTreeWidgetItem* ChannelTree::findItem(int channelId) const {
    return m_items.value(channelId, nullptr);
}

void ChannelTree::updateItemText(QTreeWidgetItem* item, const QJsonObject& channel) {
    QString name = channel["name"].toString();
    int userCount = channel["user_count"].toInt(0);
    bool hasPassword = channel["has_password"].toBool(false);

    QString display = name;
    if (hasPassword) {
        display += " [locked]";
    }
    if (userCount > 0) {
        display += QString(" (%1)").arg(userCount);
    }

    item->setText(0, display);
    item->setData(0, Qt::UserRole + 1, name); // store raw name
    item->setToolTip(0, channel["description"].toString());
}
