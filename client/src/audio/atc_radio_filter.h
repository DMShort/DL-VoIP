#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>

// -----------------------------------------------------------------------
// Biquad IIR filter - Direct Form II Transposed
// -----------------------------------------------------------------------
struct Biquad {
    double b0 = 0, b1 = 0, b2 = 0;
    double a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    static Biquad highpass(double fc, double sampleRate, double Q = M_SQRT1_2) {
        double w0    = 2.0 * M_PI * fc / sampleRate;
        double cosw0 = std::cos(w0);
        double alpha = std::sin(w0) / (2.0 * Q);
        double a0    = 1.0 + alpha;
        Biquad f;
        f.b0 =  (1.0 + cosw0) / (2.0 * a0);
        f.b1 = -(1.0 + cosw0) /        a0;
        f.b2 =  (1.0 + cosw0) / (2.0 * a0);
        f.a1 = -2.0 * cosw0   /        a0;
        f.a2 =  (1.0 - alpha) /        a0;
        return f;
    }

    static Biquad lowpass(double fc, double sampleRate, double Q = M_SQRT1_2) {
        double w0    = 2.0 * M_PI * fc / sampleRate;
        double cosw0 = std::cos(w0);
        double alpha = std::sin(w0) / (2.0 * Q);
        double a0    = 1.0 + alpha;
        Biquad f;
        f.b0 = (1.0 - cosw0) / (2.0 * a0);
        f.b1 = (1.0 - cosw0) /        a0;
        f.b2 = (1.0 - cosw0) / (2.0 * a0);
        f.a1 = -2.0 * cosw0  /        a0;
        f.a2 = (1.0 - alpha) /        a0;
        return f;
    }

    inline float process(float x) noexcept {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }

    void resetState() noexcept { z1 = z2 = 0.0; }
};

// -----------------------------------------------------------------------
// AtcRadioFilter
//
// Parametric simulation of a VHF AM aviation radio transmission.
// Four independent DSP stages, each controlled by a 0-100 slider value:
//
//   setBandwidthRestriction(0-100)  -- 4th-order bandpass (HP+LP)
//   setCompressionSquash(0-100)     -- peak compressor
//   setAnalogGrit(0-100)            -- tanh soft-clipper / waveshaper
//   setSquelchThreshold(0-100)      -- noise gate / squelch
//
// All setters are safe to call from any thread at any time; internal
// atomics ensure the audio thread picks up the new values within one
// 20 ms Opus frame with no lock contention.
// -----------------------------------------------------------------------
class AtcRadioFilter {
public:
    static constexpr double kSampleRate = 48000.0;

    AtcRadioFilter() {
        initTimeConstants();
        // Apply defaults (all sliders at 50)
        applyBandwidthParams(300.0, 3000.0);
        applyCompressionParams(15.0f, dbToLinear(-40.0f), dbToLinear(12.0f));
        m_drive.store(4.0,   std::memory_order_relaxed);
        m_gateThreshold.store(dbToLinear(-40.0f), std::memory_order_relaxed);
    }

    // ---- Parameter setters (call from main/UI thread) ----

    // Bandwidth Restriction:  0 = wide (100/6000 Hz), 50 = classic (300/3000), 100 = extreme (600/2000)
    void setBandwidthRestriction(int t) {
        double hp = piecewiseLog(100.0, 300.0, 600.0, t);
        double lp = piecewiseLog(6000.0, 3000.0, 2000.0, t);
        m_targetHpFreq.store(hp, std::memory_order_relaxed);
        m_targetLpFreq.store(lp, std::memory_order_relaxed);
        m_filterDirty.store(true, std::memory_order_release);
    }

    // Compression Squash:  0 = gentle (4:1 / -24 dB), 50 = classic (15:1 / -40 dB), 100 = brickwall (50:1 / -60 dB)
    void setCompressionSquash(int t) {
        float ratio   = static_cast<float>(piecewiseLinear(4.0,   15.0,  50.0, t));
        float threshDb = static_cast<float>(piecewiseLinear(-24.0, -40.0, -60.0, t));
        float makeupDb = static_cast<float>(piecewiseLinear(6.0,   12.0,  18.0, t));
        applyCompressionParams(ratio, dbToLinear(threshDb), dbToLinear(makeupDb));
    }

    // Analog Grit:  0 = clean, 50 = mild overdrive, 100 = heavy fuzz
    void setAnalogGrit(int t) {
        double drive = piecewiseLinear(0.0, 4.0, 20.0, t);
        m_drive.store(drive, std::memory_order_relaxed);
    }

    // Squelch Threshold:  0 = very open (-60 dB), 50 = standard (-40 dB), 100 = aggressive (-20 dB)
    void setSquelchThreshold(int t) {
        float gateDb = static_cast<float>(piecewiseLinear(-60.0, -40.0, -20.0, t));
        m_gateThreshold.store(dbToLinear(gateDb), std::memory_order_relaxed);
    }

    // ---- Audio processing (call from audio thread) ----

    void process(float* samples, int numSamples) noexcept {
        // Reinit biquad coefficients if a bandwidth change is pending.
        // Only the audio thread writes to m_hp/m_lp, so this is safe.
        if (m_filterDirty.exchange(false, std::memory_order_acq_rel)) {
            double hp = m_targetHpFreq.load(std::memory_order_relaxed);
            double lp = m_targetLpFreq.load(std::memory_order_relaxed);
            m_hp[0] = Biquad::highpass(hp, kSampleRate);
            m_hp[1] = Biquad::highpass(hp, kSampleRate);
            m_lp[0] = Biquad::lowpass (lp, kSampleRate);
            m_lp[1] = Biquad::lowpass (lp, kSampleRate);
        }

        // Snapshot per-buffer parameters from atomics once.
        const float  threshold  = m_threshold.load(std::memory_order_relaxed);
        const float  ratio      = m_ratio.load(std::memory_order_relaxed);
        const float  makeupGain = m_makeupGain.load(std::memory_order_relaxed);
        const double drive      = m_drive.load(std::memory_order_relaxed);
        const float  gateThresh = m_gateThreshold.load(std::memory_order_relaxed);
        const bool   hasDrive   = (drive > 0.01);
        const double invTanh    = hasDrive ? 1.0 / std::tanh(drive) : 1.0;

        for (int i = 0; i < numSamples; ++i) {
            float x = samples[i];

            // Pre-filter level for squelch (broadband speech energy).
            float rawLevel = std::abs(x);

            // 1. Bandpass (4th-order: 2x HP cascade, then 2x LP cascade)
            x = m_hp[0].process(x);
            x = m_hp[1].process(x);
            x = m_lp[0].process(x);
            x = m_lp[1].process(x);

            // 2. Compressor -- exponential envelope follower
            float level = std::abs(x);
            if (level > m_envelope)
                m_envelope += m_attackCoeff  * (level - m_envelope);
            else
                m_envelope += m_releaseCoeff * (level - m_envelope);

            float gainReduction = makeupGain;
            if (m_envelope > threshold) {
                float dbOver   = 20.0f * static_cast<float>(std::log10(m_envelope / threshold));
                float dbReduce = dbOver * (1.0f - 1.0f / ratio);
                gainReduction *= std::pow(10.0f, -dbReduce / 20.0f);
            }
            x *= gainReduction;

            // 3. Soft clipper / waveshaper (bypassed when drive == 0)
            if (hasDrive)
                x = static_cast<float>(std::tanh(drive * x) * invTanh);

            // 4. Noise gate / squelch (hard gate on pre-filter level)
            if (rawLevel < gateThresh)
                x = 0.0f;

            samples[i] = x;
        }
    }

    // Reset all filter state. Call when enabling the effect to avoid
    // stale history leaking into the new audio block.
    void resetState() noexcept {
        for (auto& f : m_hp) f.resetState();
        for (auto& f : m_lp) f.resetState();
        m_envelope = 0.0f;
    }

private:
    // ---- Helpers ----

    static float dbToLinear(float db) noexcept {
        return std::pow(10.0f, db / 20.0f);
    }

    // Piecewise log interpolation through anchors at t=0, 50, 100.
    static double piecewiseLog(double v0, double v50, double v100, int t) noexcept {
        t = std::clamp(t, 0, 100);
        if (t <= 50) {
            double frac = t / 50.0;
            return std::exp(std::log(v0) + frac * (std::log(v50) - std::log(v0)));
        }
        double frac = (t - 50) / 50.0;
        return std::exp(std::log(v50) + frac * (std::log(v100) - std::log(v50)));
    }

    // Piecewise linear interpolation through anchors at t=0, 50, 100.
    static double piecewiseLinear(double v0, double v50, double v100, int t) noexcept {
        t = std::clamp(t, 0, 100);
        if (t <= 50) return v0  + (t / 50.0)        * (v50  - v0);
        return              v50 + ((t - 50) / 50.0) * (v100 - v50);
    }

    void initTimeConstants() {
        // alpha = 1 - exp(-1 / (tau * Fs))
        m_attackCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (0.002 * kSampleRate))); // 2 ms
        m_releaseCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.050 * kSampleRate))); // 50 ms
    }

    void applyBandwidthParams(double hpFreq, double lpFreq) {
        m_targetHpFreq.store(hpFreq, std::memory_order_relaxed);
        m_targetLpFreq.store(lpFreq, std::memory_order_relaxed);
        m_hp[0] = Biquad::highpass(hpFreq, kSampleRate);
        m_hp[1] = Biquad::highpass(hpFreq, kSampleRate);
        m_lp[0] = Biquad::lowpass (lpFreq, kSampleRate);
        m_lp[1] = Biquad::lowpass (lpFreq, kSampleRate);
    }

    void applyCompressionParams(float ratio, float threshold, float makeupGain) {
        m_ratio.store(ratio,      std::memory_order_relaxed);
        m_threshold.store(threshold,  std::memory_order_relaxed);
        m_makeupGain.store(makeupGain, std::memory_order_relaxed);
    }

    // ---- Filter stages (audio thread only after construction) ----
    Biquad m_hp[2];
    Biquad m_lp[2];

    // Pending target frequencies (written by main thread, read by audio thread)
    std::atomic<double> m_targetHpFreq{300.0};
    std::atomic<double> m_targetLpFreq{3000.0};
    std::atomic<bool>   m_filterDirty{false};

    // Compressor state (audio thread only)
    float m_envelope     = 0.0f;
    float m_attackCoeff  = 0.0f;
    float m_releaseCoeff = 0.0f;

    // Per-buffer parameters (atomic; written by main thread, read by audio thread)
    std::atomic<float>  m_threshold {0.01f};   // linear amplitude (~-40 dBFS)
    std::atomic<float>  m_ratio     {15.0f};
    std::atomic<float>  m_makeupGain{4.0f};    // +12 dB
    std::atomic<double> m_drive     {4.0};
    std::atomic<float>  m_gateThreshold{0.01f}; // ~-40 dBFS
};
