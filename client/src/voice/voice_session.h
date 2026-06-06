#pragma once

#include <QObject>
#include <QSet>
#include <QUdpSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <unordered_map>

#include "audio/audio_engine.h"
#include "audio/atc_radio_filter.h"
#include "audio/opus_codec.h"
#include "crypto/srtp_session.h"
#include "voice/voice_packet.h"
#include "voice/jitter_buffer.h"

/// Voice session orchestrating the full pipeline:
///   Capture -> Opus encode -> SRTP encrypt -> UDP send (transmit)
///   UDP recv -> SRTP decrypt -> Jitter buffer -> Opus decode -> Playback (receive)
class VoiceSession : public QObject {
    Q_OBJECT

public:
    explicit VoiceSession(QObject* parent = nullptr);
    ~VoiceSession() override;

    bool initialize(int inputDeviceId = -1, int outputDeviceId = -1,
                    int bitrate = 32000, int complexity = 10,
                    bool fec = true, bool dtx = true);

    void setSrtpSession(SrtpSession* session);
    void setServer(const QString& host, int voicePort);
    void setIdentity(uint32_t userId, uint32_t channelId);
    void setTransmitChannel(uint32_t channelId);

    bool start();
    void stop();

    void setPttActive(bool active);
    void setOpenMic(uint32_t channelId, bool enabled);
    void setInputVolume(float v);
    void setOutputVolume(float v);
    void setDevices(int inputDeviceId, int outputDeviceId);

    void setChannelVolume(uint32_t channelId, float volume);
    void setChannelPriority(uint32_t channelId, int priority);
    void setChannelMuted(uint32_t channelId, bool muted);
    void removeChannelSettings(uint32_t channelId);

    void setDuckingEnabled(bool enabled) { m_duckingEnabled.store(enabled, std::memory_order_relaxed); }
    void setDuckLevel(float level) { m_duckLevel.store(level, std::memory_order_relaxed); }

    /// Enable/disable the ATC/pilot radio voice filter on TX and RX audio.
    void setAtcRadioEnabled(bool enabled) {
        bool prev = m_atcRadioEnabled.exchange(enabled, std::memory_order_relaxed);
        if (!prev && enabled) {
            m_txAtcFilter.resetState();
            m_rxAtcFilter.resetState();
        }
    }
    bool atcRadioEnabled() const { return m_atcRadioEnabled.load(std::memory_order_relaxed); }

    float inputLevel() const;
    float outputLevel() const;
    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }
    AudioEngine& audioEngine() { return m_audioEngine; }

signals:
    void started();
    void stopped();
    void error(const QString& message);

private slots:
    void onTransmitTimer();
    void onReadyRead();

private:
    void processReceivedPacket(const QByteArray& data);

    AudioEngine m_audioEngine;
    OpusCodec m_opusCodec;
    SrtpSession* m_srtpSession = nullptr;

    QUdpSocket m_udpSocket;
    QString m_serverHost;
    QHostAddress m_resolvedAddr;
    int m_voicePort = 9001;
    int m_inputDeviceId = -1;
    int m_outputDeviceId = -1;

    uint32_t m_userId = 0;
    uint32_t m_channelId = 0;
    uint64_t m_txSequence = 0;
    uint64_t m_txTimestamp = 0;

    QTimer m_transmitTimer;

    struct SourceState {
        JitterBuffer jitterBuffer;
        OpusCodec decoder;
        uint32_t channelId = 0;
        SourceState() : jitterBuffer(20) {}
    };
    std::map<uint32_t, std::unique_ptr<SourceState>> m_sources;
    std::mutex m_sourcesMutex;

    struct ChannelAudioSettings {
        float volume = 1.0f;
        int priority = 5;
        bool muted = false;
    };
    std::unordered_map<uint32_t, ChannelAudioSettings> m_channelSettings;
    std::mutex m_channelSettingsMutex;

    std::atomic<bool>  m_duckingEnabled{true};
    std::atomic<float> m_duckLevel{0.3f};

    // ATC radio filter
    std::atomic<bool> m_atcRadioEnabled{false};
    AtcRadioFilter m_txAtcFilter; // applied to outgoing PCM before Opus encode
    AtcRadioFilter m_rxAtcFilter; // applied to mixed incoming PCM before playback

    QTimer m_playbackTimer;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_pttActive{false};
    QSet<uint32_t> m_openMicChannels;

    void onPlaybackTimer();
};
