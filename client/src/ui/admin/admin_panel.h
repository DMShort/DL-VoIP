#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>

class AdminApi;

/// Admin panel with Users, Channels, and Roles tabs.
/// Only shown to users with admin permissions.
class AdminPanel : public QWidget {
    Q_OBJECT

public:
    explicit AdminPanel(AdminApi* api, QWidget* parent = nullptr);

    /// Refresh all tabs.
    void refresh();

private slots:
    void onTabChanged(int index);

    // Users
    void refreshUsers();
    void onAddUser();
    void onEditUser();
    void onDeleteUser();
    void onRevokeSessions();

    // Channels
    void refreshChannels();
    void onAddChannel();
    void onEditChannel();
    void onDeleteChannel();

    // Roles
    void refreshRoles();
    void onAddRole();
    void onEditRole();
    void onDeleteRole();
    void onAssignRole();

private:
    void setupUi();
    QWidget* createUsersTab();
    QWidget* createChannelsTab();
    QWidget* createRolesTab();
    void showError(const QString& message);
    void showSuccess(const QString& message);

    int selectedUserId() const;
    int selectedChannelId() const;
    int selectedRoleId() const;

    AdminApi* m_api;
    QTabWidget* m_tabs;

    // Users tab
    QTableWidget* m_usersTable;

    // Channels tab
    QTableWidget* m_channelsTable;

    // Roles tab
    QTableWidget* m_rolesTable;

    QLabel* m_statusLabel;
};
