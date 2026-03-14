#include "ptt_manager.h"

#ifdef _WIN32
PttManager* PttManager::s_instance = nullptr;
#endif

PttManager::PttManager(QObject* parent)
    : QObject(parent)
{
#ifdef _WIN32
    s_instance = this;
#endif
}

PttManager::~PttManager() {
    uninstall();
#ifdef _WIN32
    if (s_instance == this) {
        s_instance = nullptr;
    }
#endif
}

bool PttManager::install() {
#ifdef _WIN32
    if (m_hook) return true; // already installed

    m_hook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        keyboardHookProc,
        GetModuleHandle(nullptr),
        0
    );
    return m_hook != nullptr;
#else
    return false; // not implemented on non-Windows
#endif
}

void PttManager::uninstall() {
#ifdef _WIN32
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
#endif
}

void PttManager::setHotkey(int channelId, int vkCode) {
    // Remove any existing binding for this key
    m_channelKeys.remove(vkCode);
    // Remove any existing key for this channel
    for (auto it = m_channelKeys.begin(); it != m_channelKeys.end(); ) {
        if (it.value() == channelId) {
            it = m_channelKeys.erase(it);
        } else {
            ++it;
        }
    }
    m_channelKeys[vkCode] = channelId;
}

void PttManager::removeHotkey(int channelId) {
    for (auto it = m_channelKeys.begin(); it != m_channelKeys.end(); ) {
        if (it.value() == channelId) {
            it = m_channelKeys.erase(it);
        } else {
            ++it;
        }
    }
}

void PttManager::onKeyDown(int vkCode) {
    if (m_currentKeyDown != 0) return; // another PTT key already held

    int channelId = -1;

    // Check channel-specific keys first
    if (m_channelKeys.contains(vkCode)) {
        channelId = m_channelKeys[vkCode];
    } else if (vkCode == m_defaultKey && m_defaultKey != 0) {
        channelId = -1; // default key targets current channel (resolved by caller)
    } else {
        return; // not a PTT key
    }

    m_currentKeyDown = vkCode;
    m_active.store(true, std::memory_order_relaxed);
    m_activeChannel.store(channelId, std::memory_order_relaxed);
    emit pttStateChanged(true, channelId);
}

void PttManager::onKeyUp(int vkCode) {
    if (vkCode != m_currentKeyDown) return;

    m_currentKeyDown = 0;
    m_active.store(false, std::memory_order_relaxed);
    int channelId = m_activeChannel.load(std::memory_order_relaxed);
    m_activeChannel.store(-1, std::memory_order_relaxed);
    emit pttStateChanged(false, channelId);
}

#ifdef _WIN32
LRESULT CALLBACK PttManager::keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        auto* kbs = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        int vkCode = static_cast<int>(kbs->vkCode);

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            s_instance->onKeyDown(vkCode);
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            s_instance->onKeyUp(vkCode);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif
