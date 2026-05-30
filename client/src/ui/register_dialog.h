#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/// Self-registration dialog.
/// Makes a plain HTTP(S) POST to /register using an invite code supplied by an admin.
class RegisterDialog : public QDialog {
    Q_OBJECT

public:
    explicit RegisterDialog(const QString& serverAddress, int port, bool useTls,
                            const QString& orgTag, QWidget* parent = nullptr);

    /// Username that was successfully registered (valid after accept()).
    QString registeredUsername() const { return m_registeredUsername; }

private slots:
    void onRegisterClicked();
    void onNetworkReply(QNetworkReply* reply);
    void validateInputs();

private:
    void setupUi();
    void setFormEnabled(bool enabled);
    void showError(const QString& message);

    QString m_serverAddress;
    int m_port;
    bool m_useTls;
    QString m_orgTag;

    QLineEdit* m_inviteCode;
    QLineEdit* m_username;
    QLineEdit* m_displayName;
    QLineEdit* m_password;
    QLineEdit* m_confirmPassword;
    QPushButton* m_registerBtn;
    QProgressBar* m_progress;
    QLabel* m_statusLabel;
    QLabel* m_errorLabel;

    QNetworkAccessManager* m_nam;
    QString m_registeredUsername;
};
