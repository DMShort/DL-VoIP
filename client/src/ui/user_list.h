#pragma once

#include <QListWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

/// User list widget showing members of the current channel.
/// Displays username, mute/deafen/talking status indicators.
class UserList : public QListWidget {
    Q_OBJECT

public:
    explicit UserList(QWidget* parent = nullptr);

    /// Set the roster for the current channel.
    void setRoster(const QJsonArray& roster);

    /// Add a user to the list.
    void addUser(const QJsonObject& user);

    /// Remove a user by ID.
    void removeUser(int userId);

    /// Update a user's mute/deafen/talking state.
    void updateUserState(int userId, bool muted, bool deafened, bool talking);

    /// Clear all users.
    void clearUsers();

    /// Number of users currently shown.
    int userCount() const { return m_users.size(); }

private:
    struct UserInfo {
        int id;
        QString username;
        QString displayName;
        bool muted = false;
        bool deafened = false;
        bool talking = false;
    };

    QListWidgetItem* findItem(int userId) const;
    void updateItemDisplay(QListWidgetItem* item, const UserInfo& info);
    QString buildDisplayText(const UserInfo& info) const;

    QMap<int, UserInfo> m_users;
};
