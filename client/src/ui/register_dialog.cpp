#include "register_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

RegisterDialog::RegisterDialog(const QString& serverAddress, int port, bool useTls,
                               const QString& orgTag, QWidget* parent)
    : QDialog(parent)
    , m_serverAddress(serverAddress)
    , m_port(port)
    , m_useTls(useTls)
    , m_orgTag(orgTag)
    , m_nam(new QNetworkAccessManager(this))
{
    setupUi();
}

void RegisterDialog::setupUi() {
    setWindowTitle("DadLink — Register");
    setFixedWidth(420);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* mainLayout = new QVBoxLayout(this);

    // Info label
    auto* infoLabel = new QLabel(
        QString("Registering on: <b>%1:%2</b> &nbsp;|&nbsp; Org: <b>%3</b>")
            .arg(m_serverAddress).arg(m_port).arg(m_orgTag),
        this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("color: gray; padding: 4px;");
    mainLayout->addWidget(infoLabel);

    // Fields group
    auto* group = new QGroupBox("New Account", this);
    auto* form = new QFormLayout(group);

    m_inviteCode = new QLineEdit(this);
    m_inviteCode->setPlaceholderText("e.g. AB3DE7FG");
    m_inviteCode->setMaxLength(32);
    form->addRow("Invite Code:", m_inviteCode);

    m_username = new QLineEdit(this);
    m_username->setPlaceholderText("Your login name");
    m_username->setMaxLength(64);
    form->addRow("Username:", m_username);

    m_displayName = new QLineEdit(this);
    m_displayName->setPlaceholderText("Shown in channel (optional)");
    m_displayName->setMaxLength(128);
    form->addRow("Display Name:", m_displayName);

    m_password = new QLineEdit(this);
    m_password->setPlaceholderText("At least 8 characters");
    m_password->setEchoMode(QLineEdit::Password);
    form->addRow("Password:", m_password);

    m_confirmPassword = new QLineEdit(this);
    m_confirmPassword->setPlaceholderText("Repeat password");
    m_confirmPassword->setEchoMode(QLineEdit::Password);
    form->addRow("Confirm:", m_confirmPassword);

    mainLayout->addWidget(group);

    // Status / error
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(m_statusLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setStyleSheet("color: red; font-weight: bold;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progress->hide();
    mainLayout->addWidget(m_progress);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setMinimumWidth(80);
    btnLayout->addWidget(cancelBtn);

    m_registerBtn = new QPushButton("Register", this);
    m_registerBtn->setDefault(true);
    m_registerBtn->setMinimumWidth(100);
    m_registerBtn->setEnabled(false);
    btnLayout->addWidget(m_registerBtn);

    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_inviteCode,      &QLineEdit::textChanged, this, &RegisterDialog::validateInputs);
    connect(m_username,        &QLineEdit::textChanged, this, &RegisterDialog::validateInputs);
    connect(m_password,        &QLineEdit::textChanged, this, &RegisterDialog::validateInputs);
    connect(m_confirmPassword, &QLineEdit::textChanged, this, &RegisterDialog::validateInputs);

    connect(m_registerBtn, &QPushButton::clicked, this, &RegisterDialog::onRegisterClicked);
    connect(cancelBtn,     &QPushButton::clicked, this, &QDialog::reject);

    connect(m_confirmPassword, &QLineEdit::returnPressed, this, [this]() {
        if (m_registerBtn->isEnabled()) onRegisterClicked();
    });
}

void RegisterDialog::validateInputs() {
    bool ok = !m_inviteCode->text().trimmed().isEmpty()
           && !m_username->text().trimmed().isEmpty()
           && m_password->text().length() >= 8
           && m_password->text() == m_confirmPassword->text();
    m_registerBtn->setEnabled(ok);
}

void RegisterDialog::onRegisterClicked() {
    m_errorLabel->hide();
    m_progress->show();
    m_statusLabel->setText("Registering...");
    setFormEnabled(false);

    QString scheme = m_useTls ? "https" : "http";
    QUrl url(QString("%1://%2:%3/register").arg(scheme).arg(m_serverAddress).arg(m_port));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (m_useTls) {
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        req.setSslConfiguration(sslConfig);
    }

    QJsonObject body;
    body["invite_code"]  = m_inviteCode->text().trimmed().toUpper();
    body["username"]     = m_username->text().trimmed();
    body["password"]     = m_password->text();
    if (!m_displayName->text().trimmed().isEmpty()) {
        body["display_name"] = m_displayName->text().trimmed();
    }

    QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onNetworkReply(reply);
    });
}

void RegisterDialog::onNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_progress->hide();

    QByteArray body = reply->readAll();
    QJsonObject obj = QJsonDocument::fromJson(body).object();

    if (reply->error() != QNetworkReply::NoError) {
        setFormEnabled(true);
        m_statusLabel->clear();
        QString errMsg = obj["error"].toString();
        if (errMsg.isEmpty()) {
            errMsg = reply->errorString();
        }
        showError(errMsg);
        return;
    }

    m_registeredUsername = obj["username"].toString();
    m_statusLabel->setText("Account created! Returning to login...");

    QTimer::singleShot(1200, this, &QDialog::accept);
}

void RegisterDialog::setFormEnabled(bool enabled) {
    m_inviteCode->setEnabled(enabled);
    m_username->setEnabled(enabled);
    m_displayName->setEnabled(enabled);
    m_password->setEnabled(enabled);
    m_confirmPassword->setEnabled(enabled);
    m_registerBtn->setEnabled(enabled);
}

void RegisterDialog::showError(const QString& message) {
    m_errorLabel->setText(message);
    m_errorLabel->show();
}
