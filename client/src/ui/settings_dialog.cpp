#include "settings_dialog.h"
#include "audio/audio_engine.h"
#include "input/ptt_manager.h"
#include "ui/channel_tree.h"
#include "voice/voice_session.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QHeaderView>

SettingsDialog::SettingsDialog(AppConfig& config, AudioEngine* audioEngine,
                               PttManager* pttManager, ChannelTree* channelTree,
                               VoiceSession* voiceSession, QWidget* parent)
    : QDialog(parent), m_config(config), m_audioEngine(audioEngine)
    , m_pttManager(pttManager), m_channelTree(channelTree), m_voiceSession(voiceSession)
{
    setupUi();
    loadFromConfig();
}

void SettingsDialog::setupUi() {
    setWindowTitle("Settings");
    setMinimumWidth(450);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* mainLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createAudioTab(),        "Audio");
    m_tabs->addTab(createVoiceTab(),        "Voice");
    m_tabs->addTab(createHotkeyTab(),       "Hotkeys");
    m_tabs->addTab(createChannelAudioTab(), "Channel Audio");
    m_tabs->addTab(createNetworkTab(),      "Network");
    m_tabs->addTab(createVoiceFiltersTab(), "Voice Filters");
    mainLayout->addWidget(m_tabs);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onOk);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::onApply);
}

QWidget* SettingsDialog::createAudioTab() {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);

    auto* deviceGroup = new QGroupBox("Devices", this);
    auto* deviceForm  = new QFormLayout(deviceGroup);
    m_inputDevice = new QComboBox(this);
    m_inputDevice->addItem("Default", -1);
    if (m_audioEngine && m_audioEngine->isInitialized())
        for (const auto& dev : m_audioEngine->inputDevices())
            m_inputDevice->addItem(QString::fromStdString(dev.name), dev.id);
    deviceForm->addRow("Input:", m_inputDevice);
    m_outputDevice = new QComboBox(this);
    m_outputDevice->addItem("Default", -1);
    if (m_audioEngine && m_audioEngine->isInitialized())
        for (const auto& dev : m_audioEngine->outputDevices())
            m_outputDevice->addItem(QString::fromStdString(dev.name), dev.id);
    deviceForm->addRow("Output:", m_outputDevice);
    layout->addWidget(deviceGroup);

    auto* volumeGroup = new QGroupBox("Volume", this);
    auto* volumeForm  = new QFormLayout(volumeGroup);
    auto* inputVolLayout = new QHBoxLayout();
    m_inputVolume = new QSlider(Qt::Horizontal, this); m_inputVolume->setRange(0, 200);
    m_inputVolLabel = new QLabel("100%", this); m_inputVolLabel->setMinimumWidth(45);
    inputVolLayout->addWidget(m_inputVolume); inputVolLayout->addWidget(m_inputVolLabel);
    volumeForm->addRow("Input:", inputVolLayout);
    connect(m_inputVolume, &QSlider::valueChanged, this, [this](int val) { m_inputVolLabel->setText(QString("%1%").arg(val)); });

    auto* outputVolLayout = new QHBoxLayout();
    m_outputVolume = new QSlider(Qt::Horizontal, this); m_outputVolume->setRange(0, 200);
    m_outputVolLabel = new QLabel("100%", this); m_outputVolLabel->setMinimumWidth(45);
    outputVolLayout->addWidget(m_outputVolume); outputVolLayout->addWidget(m_outputVolLabel);
    volumeForm->addRow("Output:", outputVolLayout);
    connect(m_outputVolume, &QSlider::valueChanged, this, [this](int val) { m_outputVolLabel->setText(QString("%1%").arg(val)); });

    auto* ngLayout = new QHBoxLayout();
    m_noiseGate = new QSlider(Qt::Horizontal, this); m_noiseGate->setRange(0, 100);
    m_noiseGateLabel = new QLabel("0.01", this); m_noiseGateLabel->setMinimumWidth(45);
    ngLayout->addWidget(m_noiseGate); ngLayout->addWidget(m_noiseGateLabel);
    volumeForm->addRow("Noise Gate:", ngLayout);
    connect(m_noiseGate, &QSlider::valueChanged, this, [this](int val) { m_noiseGateLabel->setText(QString::number(val / 100.0, 'f', 2)); });

    layout->addWidget(volumeGroup);
    layout->addStretch();
    return widget;
}

QWidget* SettingsDialog::createVoiceTab() {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    auto* codecGroup = new QGroupBox("Opus Codec", this);
    auto* form = new QFormLayout(codecGroup);
    m_bitrate = new QSpinBox(this); m_bitrate->setRange(8000, 128000); m_bitrate->setSingleStep(1000); m_bitrate->setSuffix(" bps");
    form->addRow("Bitrate:", m_bitrate);
    m_complexity = new QSpinBox(this); m_complexity->setRange(0, 10);
    form->addRow("Complexity:", m_complexity);
    m_enableFec = new QCheckBox("Enable Forward Error Correction", this); form->addRow("", m_enableFec);
    m_enableDtx = new QCheckBox("Enable Discontinuous Transmission", this); form->addRow("", m_enableDtx);
    layout->addWidget(codecGroup);
    layout->addStretch();
    return widget;
}

QWidget* SettingsDialog::createHotkeyTab() {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    auto* info = new QLabel("Assign a PTT key to each channel. The default key transmits to "
        "the currently viewed channel. Per-channel keys transmit directly to that channel.", this);
    info->setWordWrap(true); info->setStyleSheet("padding: 4px; color: #ccc;");
    layout->addWidget(info);

    auto* group = new QGroupBox("Channel PTT Bindings", this);
    auto* groupLayout = new QVBoxLayout(group);
    m_hotkeyTable = new QTableWidget(0, 3, this);
    m_hotkeyTable->setHorizontalHeaderLabels({"Channel", "Key", ""});
    m_hotkeyTable->horizontalHeader()->setStretchLastSection(false);
    m_hotkeyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_hotkeyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_hotkeyTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_hotkeyTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_hotkeyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_hotkeyTable->verticalHeader()->hide();
    groupLayout->addWidget(m_hotkeyTable);

    auto* addLayout = new QHBoxLayout();
    m_hotkeyChannelCombo = new QComboBox(this); m_hotkeyChannelCombo->setMinimumWidth(150);
    if (m_channelTree) {
        auto names = m_channelTree->channelNames();
        for (auto it = names.constBegin(); it != names.constEnd(); ++it)
            m_hotkeyChannelCombo->addItem(it.value(), it.key());
    }
    addLayout->addWidget(new QLabel("Channel:", this));
    addLayout->addWidget(m_hotkeyChannelCombo);
    m_hotkeyKeyCombo = new QComboBox(this);
    struct KeyEntry { int vk; const char* name; };
    static const KeyEntry keys[] = {
        {0x70,"F1"},{0x71,"F2"},{0x72,"F3"},{0x73,"F4"},{0x74,"F5"},{0x75,"F6"},
        {0x76,"F7"},{0x77,"F8"},{0x78,"F9"},{0x79,"F10"},{0x7A,"F11"},{0x7B,"F12"},
        {0x14,"Caps Lock"},{0xA0,"Left Shift"},{0xA1,"Right Shift"},
        {0xA2,"Left Ctrl"},{0xA3,"Right Ctrl"},{0xA4,"Left Alt"},{0xA5,"Right Alt"},
        {0xC0,"~ (Tilde)"},{0xDC,"\\ (Backslash)"},
        {0x6A,"Numpad *"},{0x6B,"Numpad +"},{0x6D,"Numpad -"},
        {0x60,"Numpad 0"},{0x61,"Numpad 1"},{0x62,"Numpad 2"},{0x63,"Numpad 3"},
        {0x64,"Numpad 4"},{0x65,"Numpad 5"},{0x66,"Numpad 6"},{0x67,"Numpad 7"},
        {0x68,"Numpad 8"},{0x69,"Numpad 9"},{0x05,"Mouse X1"},{0x06,"Mouse X2"},
    };
    for (const auto& k : keys) m_hotkeyKeyCombo->addItem(k.name, k.vk);
    addLayout->addWidget(new QLabel("Key:", this));
    addLayout->addWidget(m_hotkeyKeyCombo);
    auto* addBtn = new QPushButton("Add", this);
    addLayout->addWidget(addBtn);
    groupLayout->addLayout(addLayout);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        int channelId = m_hotkeyChannelCombo->currentData().toInt();
        int vkCode    = m_hotkeyKeyCombo->currentData().toInt();
        if (channelId <= 0 || vkCode <= 0) return;
        if (m_pttManager) m_pttManager->setHotkey(channelId, vkCode);
        for (auto it = m_config.hotkeys.begin(); it != m_config.hotkeys.end(); ) {
            if (it.value() == channelId) it = m_config.hotkeys.erase(it); else ++it;
        }
        m_config.hotkeys[QString::number(vkCode)] = channelId;
        loadHotkeyTable();
    });
    layout->addWidget(group);
    layout->addStretch();
    return widget;
}

QWidget* SettingsDialog::createChannelAudioTab() {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    auto* info = new QLabel("Set per-channel volume and priority. Higher priority channels (lower number) "
        "will duck lower-priority channels when active.", this);
    info->setWordWrap(true); info->setStyleSheet("padding: 4px; color: #ccc;");
    layout->addWidget(info);

    auto* group = new QGroupBox("Channel Audio Settings", this);
    auto* groupLayout = new QVBoxLayout(group);
    m_channelAudioTable = new QTableWidget(0, 4, this);
    m_channelAudioTable->setHorizontalHeaderLabels({"Channel", "Volume", "Priority", "Muted"});
    m_channelAudioTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_channelAudioTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_channelAudioTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_channelAudioTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_channelAudioTable->setColumnWidth(1, 160); m_channelAudioTable->setColumnWidth(2, 80); m_channelAudioTable->setColumnWidth(3, 60);
    m_channelAudioTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_channelAudioTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_channelAudioTable->verticalHeader()->hide();
    groupLayout->addWidget(m_channelAudioTable);
    layout->addWidget(group);

    auto* duckGroup  = new QGroupBox("Audio Ducking", this);
    auto* duckLayout = new QFormLayout(duckGroup);
    m_duckingEnabled = new QCheckBox("Enable priority-based audio ducking", this);
    duckLayout->addRow("", m_duckingEnabled);
    auto* duckSliderLayout = new QHBoxLayout();
    m_duckLevel = new QSlider(Qt::Horizontal, this); m_duckLevel->setRange(0, 100);
    m_duckLevelLabel = new QLabel("30%", this); m_duckLevelLabel->setMinimumWidth(45);
    duckSliderLayout->addWidget(m_duckLevel); duckSliderLayout->addWidget(m_duckLevelLabel);
    duckLayout->addRow("Duck Level:", duckSliderLayout);
    auto* duckInfo = new QLabel("Duck level controls how much lower-priority channels are reduced "
        "when a higher-priority channel is active. 0% = silent, 100% = no ducking.", this);
    duckInfo->setWordWrap(true); duckInfo->setStyleSheet("color: gray; font-size: 10px;");
    duckLayout->addRow(duckInfo);
    connect(m_duckLevel, &QSlider::valueChanged, this, [this](int val) { m_duckLevelLabel->setText(QString("%1%").arg(val)); });
    layout->addWidget(duckGroup);
    layout->addStretch();
    return widget;
}

void SettingsDialog::loadChannelAudioTable() {
    if (!m_channelAudioTable) return;
    m_channelAudioTable->setRowCount(0);
    QMap<int, QString> channelNameMap;
    if (m_channelTree) channelNameMap = m_channelTree->channelNames();
    for (auto it = channelNameMap.constBegin(); it != channelNameMap.constEnd(); ++it) {
        int channelId = it.key();
        ChannelAudioConfig cfg;
        if (m_config.channelAudio.contains(channelId)) cfg = m_config.channelAudio[channelId];
        int row = m_channelAudioTable->rowCount();
        m_channelAudioTable->insertRow(row);
        m_channelAudioTable->setItem(row, 0, new QTableWidgetItem(it.value()));
        auto* volSlider = new QSlider(Qt::Horizontal, this); volSlider->setRange(0, 100);
        volSlider->setValue(static_cast<int>(cfg.volume * 100));
        m_channelAudioTable->setCellWidget(row, 1, volSlider);
        connect(volSlider, &QSlider::valueChanged, this, [this, channelId](int val) {
            float volume = val / 100.0f; m_config.channelAudio[channelId].volume = volume;
            if (m_voiceSession) m_voiceSession->setChannelVolume(static_cast<uint32_t>(channelId), volume);
        });
        auto* prioritySpin = new QSpinBox(this); prioritySpin->setRange(1, 10); prioritySpin->setValue(cfg.priority);
        m_channelAudioTable->setCellWidget(row, 2, prioritySpin);
        connect(prioritySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, channelId](int val) {
            m_config.channelAudio[channelId].priority = val;
            if (m_voiceSession) m_voiceSession->setChannelPriority(static_cast<uint32_t>(channelId), val);
        });
        auto* muteCheck = new QCheckBox(this); muteCheck->setChecked(cfg.muted);
        auto* muteContainer = new QWidget(this); auto* muteLayout = new QHBoxLayout(muteContainer);
        muteLayout->addWidget(muteCheck); muteLayout->setAlignment(Qt::AlignCenter); muteLayout->setContentsMargins(0,0,0,0);
        m_channelAudioTable->setCellWidget(row, 3, muteContainer);
        connect(muteCheck, &QCheckBox::toggled, this, [this, channelId](bool muted) {
            m_config.channelAudio[channelId].muted = muted;
            if (m_voiceSession) m_voiceSession->setChannelMuted(static_cast<uint32_t>(channelId), muted);
        });
    }
}

QWidget* SettingsDialog::createVoiceFiltersTab() {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);

    auto* info = new QLabel(
        "Voice filters modify audio in real-time to simulate different radio "
        "or communication environments. Filters apply to both your outgoing "
        "microphone and incoming voices from other users.", this);
    info->setWordWrap(true); info->setStyleSheet("padding: 4px; color: #ccc;");
    layout->addWidget(info);

    auto* atcGroup  = new QGroupBox("ATC / Pilot Radio Filter", this);
    auto* atcLayout = new QVBoxLayout(atcGroup);

    m_atcRadioEnabled = new QCheckBox("Enable ATC/Pilot Radio Filter", this);
    m_atcRadioEnabled->setToolTip(
        "Processes audio to simulate a VHF AM aviation radio transmission:\n"
        "  * Bandpass filter 300 Hz - 3 kHz (4th-order, ~24 dB/oct)\n"
        "  * Aggressive compressor (15:1 ratio, fast attack/release)\n"
        "  * Soft-clipper / waveshaper for AM over-modulation grit\n"
        "  * Noise gate / squelch that cuts off silence abruptly");
    atcLayout->addWidget(m_atcRadioEnabled);

    auto* atcDesc = new QLabel(
        "Applies a VHF AM aviation radio simulation to your microphone and all incoming voices:\n"
        "  Strict 300-3000 Hz bandpass (telephone-thin, 24 dB/oct roll-off)\n"
        "  Heavy compressor (15:1 ratio, 2 ms attack, 50 ms release)\n"
        "  Soft-clipper waveshaper -- adds AM over-modulation grit\n"
        "  Noise gate / squelch -- hard cut-off when the channel is silent", this);
    atcDesc->setWordWrap(true); atcDesc->setStyleSheet("color: gray; font-size: 10px; padding-left: 20px;");
    atcLayout->addWidget(atcDesc);

    // Toggle takes effect immediately without needing Apply
    connect(m_atcRadioEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_voiceSession) m_voiceSession->setAtcRadioEnabled(checked);
    });

    layout->addWidget(atcGroup);
    layout->addStretch();
    return widget;
}

QWidget* SettingsDialog::createNetworkTab() {
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    auto* group = new QGroupBox("Connection", this);
    auto* form  = new QFormLayout(group);
    m_useTls = new QCheckBox("Use TLS (requires server restart to take effect)", this);
    form->addRow("", m_useTls);
    auto* info = new QLabel("Server address and port are configured in the login dialog.", this);
    info->setWordWrap(true); info->setStyleSheet("color: gray;");
    form->addRow(info);
    layout->addWidget(group);
    layout->addStretch();
    return widget;
}

void SettingsDialog::loadFromConfig() {
    int inputIdx = m_inputDevice->findData(m_config.audio.inputDeviceId);
    if (inputIdx >= 0) m_inputDevice->setCurrentIndex(inputIdx);
    int outputIdx = m_outputDevice->findData(m_config.audio.outputDeviceId);
    if (outputIdx >= 0) m_outputDevice->setCurrentIndex(outputIdx);
    m_inputVolume->setValue(static_cast<int>(m_config.audio.inputVolume * 100));
    m_outputVolume->setValue(static_cast<int>(m_config.audio.outputVolume * 100));
    m_noiseGate->setValue(static_cast<int>(m_config.audio.noiseGateThreshold * 100));
    m_bitrate->setValue(m_config.opus.bitrate);
    m_complexity->setValue(m_config.opus.complexity);
    m_enableFec->setChecked(m_config.opus.enableFec);
    m_enableDtx->setChecked(m_config.opus.enableDtx);
    m_useTls->setChecked(m_config.server.useTls);
    loadHotkeyTable();
    loadChannelAudioTable();
    if (m_duckingEnabled) m_duckingEnabled->setChecked(m_config.voice.duckingEnabled);
    if (m_duckLevel)      m_duckLevel->setValue(static_cast<int>(m_config.voice.duckLevel * 100));
    if (m_atcRadioEnabled) m_atcRadioEnabled->setChecked(m_config.voice.atcRadioEnabled);
}

void SettingsDialog::loadHotkeyTable() {
    if (!m_hotkeyTable) return;
    m_hotkeyTable->setRowCount(0);
    QMap<int, QString> channelNameMap;
    if (m_channelTree) channelNameMap = m_channelTree->channelNames();
    for (auto it = m_config.hotkeys.constBegin(); it != m_config.hotkeys.constEnd(); ++it) {
        int vkCode = it.key().toInt(), channelId = it.value();
        if (vkCode <= 0 || channelId <= 0) continue;
        int row = m_hotkeyTable->rowCount();
        m_hotkeyTable->insertRow(row);
        m_hotkeyTable->setItem(row, 0, new QTableWidgetItem(channelNameMap.value(channelId, QString("#%1").arg(channelId))));
        m_hotkeyTable->setItem(row, 1, new QTableWidgetItem(vkCodeToName(vkCode)));
        auto* removeBtn = new QPushButton("Remove", this);
        m_hotkeyTable->setCellWidget(row, 2, removeBtn);
        connect(removeBtn, &QPushButton::clicked, this, [this, vkCode, channelId]() {
            m_config.hotkeys.remove(QString::number(vkCode));
            if (m_pttManager) m_pttManager->removeHotkey(channelId);
            loadHotkeyTable();
        });
    }
}

void SettingsDialog::saveToConfig() {
    m_config.audio.inputDeviceId      = m_inputDevice->currentData().toInt();
    m_config.audio.outputDeviceId     = m_outputDevice->currentData().toInt();
    m_config.audio.inputVolume        = m_inputVolume->value() / 100.0f;
    m_config.audio.outputVolume       = m_outputVolume->value() / 100.0f;
    m_config.audio.noiseGateThreshold = m_noiseGate->value() / 100.0f;
    m_config.opus.bitrate     = m_bitrate->value();
    m_config.opus.complexity  = m_complexity->value();
    m_config.opus.enableFec   = m_enableFec->isChecked();
    m_config.opus.enableDtx   = m_enableDtx->isChecked();
    m_config.server.useTls    = m_useTls->isChecked();
    if (m_duckingEnabled) m_config.voice.duckingEnabled = m_duckingEnabled->isChecked();
    if (m_duckLevel)      m_config.voice.duckLevel      = m_duckLevel->value() / 100.0f;
    if (m_voiceSession) {
        m_voiceSession->setDuckingEnabled(m_config.voice.duckingEnabled);
        m_voiceSession->setDuckLevel(m_config.voice.duckLevel);
    }
    if (m_atcRadioEnabled) m_config.voice.atcRadioEnabled = m_atcRadioEnabled->isChecked();
    m_config.save();
}

void SettingsDialog::onApply() { saveToConfig(); emit audioSettingsChanged(); emit voiceSettingsChanged(); }
void SettingsDialog::onOk()    { saveToConfig(); emit audioSettingsChanged(); emit voiceSettingsChanged(); accept(); }

QString SettingsDialog::vkCodeToName(int vkCode) {
    static const QMap<int, QString> names = {
        {0x70,"F1"},{0x71,"F2"},{0x72,"F3"},{0x73,"F4"},{0x74,"F5"},{0x75,"F6"},
        {0x76,"F7"},{0x77,"F8"},{0x78,"F9"},{0x79,"F10"},{0x7A,"F11"},{0x7B,"F12"},
        {0x14,"Caps Lock"},{0xA0,"Left Shift"},{0xA1,"Right Shift"},
        {0xA2,"Left Ctrl"},{0xA3,"Right Ctrl"},{0xA4,"Left Alt"},{0xA5,"Right Alt"},
        {0xC0,"~ (Tilde)"},{0xDC,"\\ (Backslash)"},
        {0x6A,"Numpad *"},{0x6B,"Numpad +"},{0x6D,"Numpad -"},
        {0x60,"Numpad 0"},{0x61,"Numpad 1"},{0x62,"Numpad 2"},{0x63,"Numpad 3"},
        {0x64,"Numpad 4"},{0x65,"Numpad 5"},{0x66,"Numpad 6"},{0x67,"Numpad 7"},
        {0x68,"Numpad 8"},{0x69,"Numpad 9"},{0x05,"Mouse X1"},{0x06,"Mouse X2"},
    };
    return names.value(vkCode, QString("Key 0x%1").arg(vkCode, 2, 16, QChar('0')).toUpper());
}
