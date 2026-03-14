#include "audio_engine.h"

#include <cmath>
#include <cstring>

AudioEngine::AudioEngine()
    : m_captureBuffer(std::make_unique<RingBuffer<float>>(SampleRate / 5))   // ~200ms
    , m_playbackBuffer(std::make_unique<RingBuffer<float>>(SampleRate / 5))
{}

AudioEngine::~AudioEngine() {
    terminate();
}

bool AudioEngine::initialize() {
    if (m_initialized) return true;

    PaError err = Pa_Initialize();
    if (err != paNoError) return false;

    m_initialized = true;
    return true;
}

void AudioEngine::terminate() {
    stopCapture();
    stopPlayback();
    if (m_initialized) {
        Pa_Terminate();
        m_initialized = false;
    }
}

std::vector<AudioDeviceInfo> AudioEngine::inputDevices() const {
    std::vector<AudioDeviceInfo> result;
    if (!m_initialized) return result;

    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            AudioDeviceInfo dev;
            dev.id = i;
            dev.name = info->name;
            dev.maxInputChannels = info->maxInputChannels;
            dev.maxOutputChannels = info->maxOutputChannels;
            dev.defaultSampleRate = info->defaultSampleRate;
            result.push_back(dev);
        }
    }
    return result;
}

std::vector<AudioDeviceInfo> AudioEngine::outputDevices() const {
    std::vector<AudioDeviceInfo> result;
    if (!m_initialized) return result;

    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            AudioDeviceInfo dev;
            dev.id = i;
            dev.name = info->name;
            dev.maxInputChannels = info->maxInputChannels;
            dev.maxOutputChannels = info->maxOutputChannels;
            dev.defaultSampleRate = info->defaultSampleRate;
            result.push_back(dev);
        }
    }
    return result;
}

bool AudioEngine::startCapture(int deviceId) {
    if (!m_initialized || m_captureStream) return false;

    PaStreamParameters inputParams;
    inputParams.device = (deviceId < 0) ? Pa_GetDefaultInputDevice() : deviceId;
    if (inputParams.device == paNoDevice) return false;

    inputParams.channelCount = Channels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    m_captureBuffer->reset();

    PaError err = Pa_OpenStream(
        &m_captureStream,
        &inputParams,
        nullptr, // no output
        SampleRate,
        FramesPerBuffer,
        paClipOff,
        captureCallback,
        this
    );
    if (err != paNoError) {
        m_captureStream = nullptr;
        return false;
    }

    err = Pa_StartStream(m_captureStream);
    if (err != paNoError) {
        Pa_CloseStream(m_captureStream);
        m_captureStream = nullptr;
        return false;
    }

    return true;
}

void AudioEngine::stopCapture() {
    if (m_captureStream) {
        Pa_StopStream(m_captureStream);
        Pa_CloseStream(m_captureStream);
        m_captureStream = nullptr;
    }
}

bool AudioEngine::startPlayback(int deviceId) {
    if (!m_initialized || m_playbackStream) return false;

    PaStreamParameters outputParams;
    outputParams.device = (deviceId < 0) ? Pa_GetDefaultOutputDevice() : deviceId;
    if (outputParams.device == paNoDevice) return false;

    outputParams.channelCount = Channels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    m_playbackBuffer->reset();

    PaError err = Pa_OpenStream(
        &m_playbackStream,
        nullptr, // no input
        &outputParams,
        SampleRate,
        FramesPerBuffer,
        paClipOff,
        playbackCallback,
        this
    );
    if (err != paNoError) {
        m_playbackStream = nullptr;
        return false;
    }

    err = Pa_StartStream(m_playbackStream);
    if (err != paNoError) {
        Pa_CloseStream(m_playbackStream);
        m_playbackStream = nullptr;
        return false;
    }

    return true;
}

void AudioEngine::stopPlayback() {
    if (m_playbackStream) {
        Pa_StopStream(m_playbackStream);
        Pa_CloseStream(m_playbackStream);
        m_playbackStream = nullptr;
    }
}

size_t AudioEngine::readCapture(float* buffer, size_t frames) {
    return m_captureBuffer->read(buffer, frames);
}

size_t AudioEngine::writePlayback(const float* buffer, size_t frames) {
    return m_playbackBuffer->write(buffer, frames);
}

// --- Audio callbacks (real-time safe — no alloc, no mutex, no IO) ---

int AudioEngine::captureCallback(const void* input, void* /*output*/,
                                  unsigned long frameCount,
                                  const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                  PaStreamCallbackFlags /*statusFlags*/,
                                  void* userData) {
    auto* engine = static_cast<AudioEngine*>(userData);
    const auto* samples = static_cast<const float*>(input);

    if (!samples) return paContinue;

    // Apply input volume and compute RMS
    float volume = engine->m_inputVolume.load(std::memory_order_relaxed);
    float sumSquares = 0.0f;

    // Use stack buffer to avoid allocation
    float adjusted[960]; // FramesPerBuffer
    size_t count = (frameCount > 960) ? 960 : frameCount;

    for (size_t i = 0; i < count; ++i) {
        adjusted[i] = samples[i] * volume;
        sumSquares += adjusted[i] * adjusted[i];
    }

    float rms = std::sqrt(sumSquares / static_cast<float>(count));
    engine->m_inputLevel.store(rms, std::memory_order_relaxed);

    engine->m_captureBuffer->write(adjusted, count);
    return paContinue;
}

int AudioEngine::playbackCallback(const void* /*input*/, void* output,
                                   unsigned long frameCount,
                                   const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                   PaStreamCallbackFlags /*statusFlags*/,
                                   void* userData) {
    auto* engine = static_cast<AudioEngine*>(userData);
    auto* samples = static_cast<float*>(output);

    size_t read = engine->m_playbackBuffer->read(samples, frameCount);

    // Zero-fill if not enough data (silence)
    if (read < frameCount) {
        std::memset(samples + read, 0, (frameCount - read) * sizeof(float));
    }

    // Apply output volume and compute RMS
    float volume = engine->m_outputVolume.load(std::memory_order_relaxed);
    float sumSquares = 0.0f;

    for (unsigned long i = 0; i < frameCount; ++i) {
        samples[i] *= volume;
        sumSquares += samples[i] * samples[i];
    }

    float rms = std::sqrt(sumSquares / static_cast<float>(frameCount));
    engine->m_outputLevel.store(rms, std::memory_order_relaxed);

    return paContinue;
}
