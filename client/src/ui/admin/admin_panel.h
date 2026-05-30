#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>

class AdminApi;

/// Admin panel with Users, Channels, Roles, and Invites tabs.
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

    // Invites
    void refreshInvites();
    void onCreateInvite();
    void onRevokeInvite();
    void onCopyInviteCode();

private:
    void setupUi();
    QWidget* createUsersTab();
    QWidget* createChannelsTab();
    QWidget* createRolesTab();
    QWidget* createInvitesTab();
    void showError(const QString& message);
    void showSuccess(const QString& message);

    int selectedUserId() const;
    int selectedChannelId() const;
    int selectedRoleId() const;
    QString selectedInviteCode() const;

    AdminApi* m_api;
    QTabWidget* m_tabs;

    // Users tab
    QTableWidget* m_usersTable;

    // Channels tab
    QTableWidget* m_channelsTable;

    // Roles tab
    QTableWidget* m_rolesTable;

    // Invites tab
    QTableWidget* m_invitesTable;

    QLabel* m_statusLabel;
};
