/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * AudioDuplexStream Implementation
 *
 * Full-duplex audio stream using PortAudio.
 */

#include <portaudio.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "internal/audio_duplex_stream.hpp"

// ============================================================================
// PortAudio Initialization Reference Counter
// ============================================================================

namespace {
    static int g_pa_ref_count = 0;
    static std::mutex g_pa_mutex;

    bool ensurePortAudioInitialized() {
        std::lock_guard<std::mutex> lock(g_pa_mutex);
        if (g_pa_ref_count == 0) {
            // Suppress ALSA error messages on Linux
            #ifdef __linux__
            FILE* null_file = freopen("/dev/null", "w", stderr);
            (void)null_file;
            #endif

            PaError err = Pa_Initialize();

            #ifdef __linux__
            FILE* tty_file = freopen("/dev/tty", "w", stderr);
            (void)tty_file;
            #endif

            if (err != paNoError) {
                std::cerr << "[AudioDuplexStream] Failed to initialize PortAudio: "
                            << Pa_GetErrorText(err) << std::endl;
                return false;
            }
        }
        g_pa_ref_count++;
        return true;
    }

    void releasePortAudio() {
        std::lock_guard<std::mutex> lock(g_pa_mutex);
        if (g_pa_ref_count > 0) {
            g_pa_ref_count--;
            if (g_pa_ref_count == 0) {
                Pa_Terminate();
            }
        }
    }
}  // namespace

// ============================================================================
// AudioDuplexStream Implementation
// ============================================================================

AudioDuplexStream::AudioDuplexStream()
    : stream_(nullptr)
    , callback_(nullptr)
    , user_data_(nullptr)
    , actual_sample_rate_(0)
    , actual_channels_(0)
    , input_device_index_(-1)
    , output_device_index_(-1)
    , is_running_(false)
    , is_open_(false)
    , frames_per_buffer_(0) {
}

AudioDuplexStream::~AudioDuplexStream() {
    close();
}

AudioDuplexStream::AudioDuplexStream(AudioDuplexStream&& other) noexcept
    : stream_(other.stream_)
    , callback_(std::move(other.callback_))
    , user_data_(other.user_data_)
    , actual_sample_rate_(other.actual_sample_rate_)
    , actual_channels_(other.actual_channels_)
    , input_device_index_(other.input_device_index_)
    , output_device_index_(other.output_device_index_)
    , is_running_(other.is_running_.load())
    , is_open_(other.is_open_.load())
    , aligned_input_buffer_(std::move(other.aligned_input_buffer_))
    , frames_per_buffer_(other.frames_per_buffer_) {
    other.stream_ = nullptr;
    other.is_running_.store(false);
    other.is_open_.store(false);
    other.frames_per_buffer_ = 0;
}

AudioDuplexStream& AudioDuplexStream::operator=(AudioDuplexStream&& other) noexcept {
    if (this != &other) {
        close();
        stream_ = other.stream_;
        callback_ = std::move(other.callback_);
        user_data_ = other.user_data_;
        actual_sample_rate_ = other.actual_sample_rate_;
        actual_channels_ = other.actual_channels_;
        input_device_index_ = other.input_device_index_;
        output_device_index_ = other.output_device_index_;
        is_running_.store(other.is_running_.load());
        is_open_.store(other.is_open_.load());
        aligned_input_buffer_ = std::move(other.aligned_input_buffer_);
        frames_per_buffer_ = other.frames_per_buffer_;

        other.stream_ = nullptr;
        other.is_running_.store(false);
        other.is_open_.store(false);
        other.frames_per_buffer_ = 0;
    }
    return *this;
}

void AudioDuplexStream::setCallback(AudioDuplexCallback callback, void* user_data) {
    callback_ = std::move(callback);
    user_data_ = user_data;
}

int AudioDuplexStream::paCallback(const void* input_buffer,
                                    void* output_buffer,
                                    unsigned long frames_per_buffer,  // NOLINT(runtime/int)
                                    const PaStreamCallbackTimeInfo* time_info,
                                    PaStreamCallbackFlags status_flags,
                                    void* user_data) {
    (void)time_info;
    (void)status_flags;

    AudioDuplexStream* self = static_cast<AudioDuplexStream*>(user_data);
    if (!self || !self->callback_) {
        // Fill output with silence if no callback
        if (output_buffer) {
            std::memset(output_buffer, 0,
                        frames_per_buffer * self->actual_channels_ * sizeof(float));
        }
        return paContinue;
    }

    const float* input = static_cast<const float*>(input_buffer);
    float* output = static_cast<float*>(output_buffer);

#ifdef __riscv
    // RISC-V 严格对齐模式：检查 input_buffer 是否 4 字节对齐 (float 要求)
    // 如果未对齐，复制到预分配的对齐缓冲区
    if (input_buffer && (reinterpret_cast<uintptr_t>(input_buffer) & 0x03)) {
        size_t buffer_size = frames_per_buffer * self->actual_channels_;
        if (self->aligned_input_buffer_.size() < buffer_size) {
            self->aligned_input_buffer_.resize(buffer_size);
        }
        // 使用 memcpy 逐字节复制，避免对齐问题
        std::memcpy(self->aligned_input_buffer_.data(), input_buffer,
                    buffer_size * sizeof(float));
        input = self->aligned_input_buffer_.data();
    }
#endif

    // Call user callback with synchronized input/output
    self->callback_(input, output, frames_per_buffer, self->actual_channels_, self->user_data_);

    return paContinue;
}

bool AudioDuplexStream::open(const AudioDuplexConfig& config) {
    if (is_open_.load()) {
        std::cerr << "[AudioDuplexStream] Stream already open" << std::endl;
        return false;
    }

    if (!ensurePortAudioInitialized()) {
        return false;
    }

    // Find input device
    int inputDevice = config.input_device_index;
    if (config.input_device_name != nullptr && config.input_device_name[0] != '\0') {
        inputDevice = findInputDeviceByName(config.input_device_name);
        if (inputDevice < 0) {
            std::cerr << "[AudioDuplexStream] Input device not found: "
                        << config.input_device_name << std::endl;
            releasePortAudio();
            return false;
        }
    }
    if (inputDevice < 0) {
        inputDevice = Pa_GetDefaultInputDevice();
    }
    if (inputDevice == paNoDevice) {
        std::cerr << "[AudioDuplexStream] No default input device" << std::endl;
        releasePortAudio();
        return false;
    }

    // Find output device
    int outputDevice = config.output_device_index;
    if (config.output_device_name != nullptr && config.output_device_name[0] != '\0') {
        outputDevice = findOutputDeviceByName(config.output_device_name);
        if (outputDevice < 0) {
            std::cerr << "[AudioDuplexStream] Output device not found: "
                        << config.output_device_name << std::endl;
            releasePortAudio();
            return false;
        }
    }
    if (outputDevice < 0) {
        outputDevice = Pa_GetDefaultOutputDevice();
    }
    if (outputDevice == paNoDevice) {
        std::cerr << "[AudioDuplexStream] No default output device" << std::endl;
        releasePortAudio();
        return false;
    }

    // Get device info
    const PaDeviceInfo* inputInfo = Pa_GetDeviceInfo(inputDevice);
    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(outputDevice);

    if (!inputInfo || !outputInfo) {
        std::cerr << "[AudioDuplexStream] Failed to get device info" << std::endl;
        releasePortAudio();
        return false;
    }

    std::cout << "[AudioDuplexStream] Input device " << inputDevice << ": "
                << inputInfo->name << " (max inputs: " << inputInfo->maxInputChannels << ")"
                << std::endl;
    std::cout << "[AudioDuplexStream] Output device " << outputDevice << ": "
                << outputInfo->name << " (max outputs: " << outputInfo->maxOutputChannels << ")"
                << std::endl;

    // Verify channel count
    int channels = config.channels;
    if (channels > inputInfo->maxInputChannels) {
        std::cerr << "[AudioDuplexStream] Requested " << channels
                    << " channels but input device only has "
                    << inputInfo->maxInputChannels << std::endl;
        channels = inputInfo->maxInputChannels;
    }
    if (channels > outputInfo->maxOutputChannels) {
        std::cerr << "[AudioDuplexStream] Requested " << channels
                    << " channels but output device only has "
                    << outputInfo->maxOutputChannels << std::endl;
        channels = outputInfo->maxOutputChannels;
    }

    // Setup input parameters
    PaStreamParameters inputParams;
    inputParams.device = inputDevice;
    inputParams.channelCount = channels;
    inputParams.sampleFormat = paFloat32;
    // Use higher latency on Linux to avoid ALSA issues
    #ifdef __linux__
    inputParams.suggestedLatency = inputInfo->defaultHighInputLatency;
    #else
    inputParams.suggestedLatency = inputInfo->defaultLowInputLatency;
    #endif
    inputParams.hostApiSpecificStreamInfo = nullptr;

    // Setup output parameters
    PaStreamParameters outputParams;
    outputParams.device = outputDevice;
    outputParams.channelCount = channels;
    outputParams.sampleFormat = paFloat32;
    #ifdef __linux__
    outputParams.suggestedLatency = outputInfo->defaultHighOutputLatency;
    #else
    outputParams.suggestedLatency = outputInfo->defaultLowOutputLatency;
    #endif
    outputParams.hostApiSpecificStreamInfo = nullptr;

    std::cout << "[AudioDuplexStream] Input latency: "
                << inputParams.suggestedLatency * 1000 << " ms" << std::endl;
    std::cout << "[AudioDuplexStream] Output latency: "
                << outputParams.suggestedLatency * 1000 << " ms" << std::endl;

    // Open full-duplex stream
    PaError err = Pa_OpenStream(
        reinterpret_cast<PaStream**>(&stream_),
        &inputParams,
        &outputParams,
        config.sample_rate,
        config.frames_per_buffer,
        paClipOff,
        paCallback,
        this);

    if (err != paNoError) {
        std::cerr << "[AudioDuplexStream] Failed to open stream: "
                    << Pa_GetErrorText(err) << std::endl;
        releasePortAudio();
        return false;
    }

    input_device_index_ = inputDevice;
    output_device_index_ = outputDevice;
    actual_sample_rate_ = config.sample_rate;
    actual_channels_ = channels;
    frames_per_buffer_ = config.frames_per_buffer;
    is_open_.store(true);

#ifdef __riscv
    // RISC-V: 预分配对齐缓冲区，避免回调中动态分配
    aligned_input_buffer_.resize(config.frames_per_buffer * channels);
#endif

    std::cout << "[AudioDuplexStream] Opened: " << actual_sample_rate_ << "Hz, "
                << actual_channels_ << " channels, "
                << config.frames_per_buffer << " frames/buffer" << std::endl;
    return true;
}

void AudioDuplexStream::close() {
    if (!is_open_.load()) {
        return;
    }

    stop();

    if (stream_) {
        Pa_CloseStream(static_cast<PaStream*>(stream_));
        stream_ = nullptr;
    }

    is_open_.store(false);
    releasePortAudio();
    std::cout << "[AudioDuplexStream] Closed" << std::endl;
}

bool AudioDuplexStream::start() {
    if (!is_open_.load()) {
        std::cerr << "[AudioDuplexStream] Cannot start: stream not open" << std::endl;
        return false;
    }

    if (is_running_.load()) {
        return true;  // Already running
    }

    PaError err = Pa_StartStream(static_cast<PaStream*>(stream_));
    if (err != paNoError) {
        std::cerr << "[AudioDuplexStream] Failed to start stream: "
                    << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    is_running_.store(true);
    std::cout << "[AudioDuplexStream] Started" << std::endl;
    return true;
}

bool AudioDuplexStream::stop() {
    if (!is_running_.load()) {
        return true;  // Already stopped
    }

    PaError err = Pa_StopStream(static_cast<PaStream*>(stream_));
    if (err != paNoError) {
        std::cerr << "[AudioDuplexStream] Failed to stop stream: "
                    << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    is_running_.store(false);
    std::cout << "[AudioDuplexStream] Stopped" << std::endl;
    return true;
}

void AudioDuplexStream::listInputDevices(std::vector<std::string>& names,
                                            std::vector<int>& indices) {
    names.clear();
    indices.clear();

    if (!ensurePortAudioInitialized()) {
        return;
    }

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            names.push_back(info->name);
            indices.push_back(i);
        }
    }

    releasePortAudio();
}

void AudioDuplexStream::listOutputDevices(std::vector<std::string>& names,
                                            std::vector<int>& indices) {
    names.clear();
    indices.clear();

    if (!ensurePortAudioInitialized()) {
        return;
    }

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            names.push_back(info->name);
            indices.push_back(i);
        }
    }

    releasePortAudio();
}

int AudioDuplexStream::findInputDeviceByName(const char* name_hint) {
    if (!name_hint || name_hint[0] == '\0') {
        return -1;
    }

    if (!ensurePortAudioInitialized()) {
        return -1;
    }

    int result = -1;
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            if (strstr(info->name, name_hint) != nullptr) {
                result = i;
                break;
            }
        }
    }

    releasePortAudio();
    return result;
}

int AudioDuplexStream::findOutputDeviceByName(const char* name_hint) {
    if (!name_hint || name_hint[0] == '\0') {
        return -1;
    }

    if (!ensurePortAudioInitialized()) {
        return -1;
    }

    int result = -1;
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            if (strstr(info->name, name_hint) != nullptr) {
                result = i;
                break;
            }
        }
    }

    releasePortAudio();
    return result;
}


// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

struct AudioDuplexStreamHandle {
    AudioDuplexStream* stream;
    AudioDuplexCallbackC c_callback;
    void* c_user_data;
};

// C callback wrapper
static void duplexCallbackWrapper(const float* input, float* output,
                                    size_t frames, int channels, void* user_data) {
    AudioDuplexStreamHandle* handle = static_cast<AudioDuplexStreamHandle*>(user_data);
    if (handle && handle->c_callback) {
        handle->c_callback(input, output, frames, channels, handle->c_user_data);
    } else if (output) {
        // Fill with silence if no callback
        std::memset(output, 0, frames * channels * sizeof(float));
    }
}

AudioDuplexStreamHandle* audio_duplex_create(void) {
    AudioDuplexStreamHandle* handle = new (std::nothrow) AudioDuplexStreamHandle();
    if (!handle) return nullptr;

    handle->stream = new (std::nothrow) AudioDuplexStream();
    if (!handle->stream) {
        delete handle;
        return nullptr;
    }

    handle->c_callback = nullptr;
    handle->c_user_data = nullptr;
    return handle;
}

void audio_duplex_destroy(AudioDuplexStreamHandle* handle) {
    if (handle) {
        delete handle->stream;
        delete handle;
    }
}

void audio_duplex_set_callback(AudioDuplexStreamHandle* handle,
                                AudioDuplexCallbackC callback,
                                void* user_data) {
    if (!handle || !handle->stream) return;

    handle->c_callback = callback;
    handle->c_user_data = user_data;

    if (callback) {
        handle->stream->setCallback(duplexCallbackWrapper, handle);
    } else {
        handle->stream->setCallback(nullptr, nullptr);
    }
}

int audio_duplex_open(AudioDuplexStreamHandle* handle,
                        int sample_rate,
                        int channels,
                        int frames_per_buffer,
                        int input_device_index,
                        int output_device_index) {
    if (!handle || !handle->stream) return 0;

    AudioDuplexConfig config;
    config.sample_rate = sample_rate;
    config.channels = channels;
    config.frames_per_buffer = frames_per_buffer;
    config.input_device_index = input_device_index;
    config.output_device_index = output_device_index;

    return handle->stream->open(config) ? 1 : 0;
}

void audio_duplex_close(AudioDuplexStreamHandle* handle) {
    if (handle && handle->stream) {
        handle->stream->close();
    }
}

int audio_duplex_start(AudioDuplexStreamHandle* handle) {
    if (!handle || !handle->stream) return 0;
    return handle->stream->start() ? 1 : 0;
}

int audio_duplex_stop(AudioDuplexStreamHandle* handle) {
    if (!handle || !handle->stream) return 0;
    return handle->stream->stop() ? 1 : 0;
}

int audio_duplex_is_running(AudioDuplexStreamHandle* handle) {
    if (!handle || !handle->stream) return 0;
    return handle->stream->isRunning() ? 1 : 0;
}

int audio_duplex_get_sample_rate(AudioDuplexStreamHandle* handle) {
    if (!handle || !handle->stream) return 0;
    return handle->stream->getSampleRate();
}

int audio_duplex_get_channels(AudioDuplexStreamHandle* handle) {
    if (!handle || !handle->stream) return 0;
    return handle->stream->getChannels();
}

}  // extern "C"
