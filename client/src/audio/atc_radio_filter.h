#pragma once

#include <cmath>
#include <algorithm>

// -----------------------------------------------------------------------
// Biquad IIR filter - Direct Form II Transposed
// -----------------------------------------------------------------------
struct Biquad {
    double b0 = 0, b1 = 0, b2 = 0; // feedforward
    double a1 = 0, a2 = 0;          // feedback (a0 normalised to 1)
    double z1 = 0, z2 = 0;          // state

    static Biquad highpass(double fc, double sampleRate, double Q = M_SQRT1_2) {
        double w0     = 2.0 * M_PI * fc / sampleRate;
        double cosw0  = std::cos(w0);
        double alpha  = std::sin(w0) / (2.0 * Q);
        double a0     = 1.0 + alpha;
        Biquad f;
        f.b0 =  (1.0 + cosw0) / (2.0 * a0);
        f.b1 = -(1.0 + cosw0) /        a0;
        f.b2 =  (1.0 + cosw0) / (2.0 * a0);
        f.a1 = -2.0 * cosw0   /        a0;
        f.a2 =  (1.0 - alpha) /        a0;
        return f;
    }

    static Biquad lowpass(double fc, double sampleRate, double Q = M_SQRT1_2) {
        double w0     = 2.0 * M_PI * fc / sampleRate;
        double cosw0  = std::cos(w0);
        double alpha  = std::sin(w0) / (2.0 * Q);
        double a0     = 1.0 + alpha;
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
// DSP chain that simulates a VHF AM aviation radio transmission:
//
//  1. 4th-order bandpass (300 Hz HPF + 3 kHz LPF, each as two cascaded
//     Butterworth biquads -> ~24 dB/oct skirts)
//  2. Hard-knee compressor  (ratio 15:1, threshold -32 dBFS,
//     attack 2 ms, release 50 ms, makeup +12 dB)
//  3. tanh soft-clipper  (drive = 4) -- adds AM over-modulation "grit"
//  4. Noise gate / squelch  (-34 dBFS threshold, hard release)
//
// The filter is stateful; create one instance per audio stream (TX or
// per-source RX). Call process() once per 20 ms Opus frame.
// -----------------------------------------------------------------------
class AtcRadioFilter {
public:
    static constexpr double kSampleRate = 48000.0;

    AtcRadioFilter() { initFilters(); }

    // Apply the full ATC chain to `samples` in-place.
    void process(float* samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            float x = samples[i];

            // Track pre-filter level for the squelch gate so that
            // broadband speech energy opens the gate (not just 300-3 kHz).
            float rawLevel = std::abs(x);

            // --- 1. Bandpass ---
            x = m_hp[0].process(x);
            x = m_hp[1].process(x);
            x = m_lp[0].process(x);
            x = m_lp[1].process(x);

            // --- 2. Compressor ---
            float level = std::abs(x);
            if (level > m_envelope)
                m_envelope += m_attackCoeff  * (level - m_envelope);
            else
                m_envelope += m_releaseCoeff * (level - m_envelope);

            float gainReduction = kMakeupGain;
            if (m_envelope > kThreshold) {
                float dbOver   = 20.0f * static_cast<float>(std::log10(m_envelope / kThreshold));
                float dbReduce = dbOver * (1.0f - 1.0f / kRatio);
                gainReduction *= std::pow(10.0f, -dbReduce / 20.0f);
            }
            x *= gainReduction;

            // --- 3. Soft clipper (tanh waveshaper) ---
            x = static_cast<float>(std::tanh(kDrive * x) * m_invTanhDrive);

            // --- 4. Noise gate / squelch ---
            if (rawLevel < kGateThreshold)
                x = 0.0f;

            samples[i] = x;
        }
    }

    // Reset all filter state (call when enabling the effect to avoid
    // stale state leaking into the new audio block).
    void resetState() noexcept {
        for (auto& f : m_hp) f.resetState();
        for (auto& f : m_lp) f.resetState();
        m_envelope = 0.0f;
    }

private:
    void initFilters() {
        m_hp[0] = Biquad::highpass(300.0,  kSampleRate);
        m_hp[1] = Biquad::highpass(300.0,  kSampleRate);
        m_lp[0] = Biquad::lowpass (3000.0, kSampleRate);
        m_lp[1] = Biquad::lowpass (3000.0, kSampleRate);

        // alpha = 1 - exp(-1 / (tau * Fs))
        m_attackCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (0.002 * kSampleRate))); // 2 ms
        m_releaseCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.050 * kSampleRate))); // 50 ms
        m_invTanhDrive = 1.0 / std::tanh(kDrive);
    }

    Biquad m_hp[2];
    Biquad m_lp[2];

    float m_envelope     = 0.0f;
    float m_attackCoeff  = 0.0f;
    float m_releaseCoeff = 0.0f;
    double m_invTanhDrive = 1.0;

    static constexpr float  kThreshold     = 0.025f;  // ~-32 dBFS
    static constexpr float  kRatio         = 15.0f;
    static constexpr float  kMakeupGain    = 4.0f;    // +12 dB
    static constexpr double kDrive         = 4.0;
    static constexpr float  kGateThreshold = 0.020f;  // ~-34 dBFS
};
