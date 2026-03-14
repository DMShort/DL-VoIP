#include "user_list.h"

UserList::UserList(QWidget* parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::NoSelection);
}

void UserList::setRoster(const QJsonArray& roster) {
    clearUsers();
    for (const auto& val : roster) {
        addUser(val.toObject());
    }
}

void UserList::addUser(const QJsonObject& user) {
    int id = user["id"].toInt();
    if (m_users.contains(id)) return;

    UserInfo info;
    info.id = id;
    info.username = user["username"].toString();
    info.displayName = user["display_name"].toString();
    if (info.displayName.isEmpty()) {
        info.displayName = info.username;
    }
    info.muted = user["muted"].toBool(false);
    info.deafened = user["deafened"].toBool(false);
    info.talking = user["talking"].toBool(false);

    m_users[id] = info;

    auto* item = new QListWidgetItem(this);
    item->setData(Qt::UserRole, id);
    updateItemDisplay(item, info);
    addItem(item);
}

void UserList::removeUser(int userId) {
    QListWidgetItem* item = findItem(userId);
    if (item) {
        delete item;
    }
    m_users.remove(userId);
}

void UserList::updateUserState(int userId, bool muted, bool deafened, bool talking) {
    if (!m_users.contains(userId)) return;

    UserInfo& info = m_users[userId];
    info.muted = muted;
    info.deafened = deafened;
    info.talking = talking;

    QListWidgetItem* item = findItem(userId);
    if (item) {
        updateItemDisplay(item, info);
    }
}

void UserList::clearUsers() {
    clear();
    m_users.clear();
}

QListWidgetItem* UserList::findItem(int userId) const {
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem* it = item(i);
        if (it->data(Qt::UserRole).toInt() == userId) {
            return it;
        }
    }
    return nullptr;
}

void UserList::updateItemDisplay(QListWidgetItem* item, const UserInfo& info) {
    item->setText(buildDisplayText(info));

    // Color coding for state
    if (info.talking) {
        item->setForeground(QColor(0, 200, 0)); // green = talking
    } else if (info.deafened) {
        item->setForeground(QColor(180, 0, 0)); // red = deafened
    } else if (info.muted) {
        item->setForeground(QColor(200, 200, 0)); // yellow = muted
    } else {
        item->setForeground(QColor(200, 200, 200)); // default
    }
}

QString UserList::buildDisplayText(const UserInfo& info) const {
    QString text = info.displayName;
    QStringList indicators;

    if (info.talking) indicators << "TALK";
    if (info.muted) indicators << "M";
    if (info.deafened) indicators << "D";

    if (!indicators.isEmpty()) {
        text += " [" + indicators.join(", ") + "]";
    }
    return text;
}
