#pragma once

#include <QObject>
#include <QMap>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

/// Push-to-Talk manager using a low-level keyboard hook (Windows).
/// Tracks per-channel hotkey bindings and provides atomic PTT state.
class PttManager : public QObject {
    Q_OBJECT

public:
    explicit PttManager(QObject* parent = nullptr);
    ~PttManager() override;

    PttManager(const PttManager&) = delete;
    PttManager& operator=(const PttManager&) = delete;

    /// Install the global keyboard hook. Returns true on success.
    bool install();

    /// Uninstall the keyboard hook.
    void uninstall();

    /// Set the PTT key for a specific channel. vkCode is a Windows virtual key code.
    void setHotkey(int channelId, int vkCode);

    /// Remove the PTT key for a channel.
    void removeHotkey(int channelId);

    /// Set a default PTT key (used when no channel-specific key is set).
    void setDefaultKey(int vkCode) { m_defaultKey = vkCode; }

    /// Whether PTT is currently active (any key held).
    bool isActive() const { return m_active.load(std::memory_order_relaxed); }

    /// The channel ID that PTT is currently targeting (-1 if none).
    int activeChannelId() const { return m_activeChannel.load(std::memory_order_relaxed); }

signals:
    /// Emitted when PTT state changes.
    void pttStateChanged(bool active, int channelId);

private:
#ifdef _WIN32
    static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    HHOOK m_hook = nullptr;
    static PttManager* s_instance;
#endif

    void onKeyDown(int vkCode);
    void onKeyUp(int vkCode);

    QMap<int, int> m_channelKeys; // vkCode -> channelId
    int m_defaultKey = 0;        // default PTT key (0 = none)
    std::atomic<bool> m_active{false};
    std::atomic<int> m_activeChannel{-1};
    int m_currentKeyDown = 0;    // currently held PTT key
};
