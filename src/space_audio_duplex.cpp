/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * SpaceAudio Full-Duplex Implementation
 */

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "audio_duplex.hpp"
#include "internal/audio_duplex_stream.hpp"

namespace SpaceAudio {

// ============================================================================
// AudioDuplex Implementation
// ============================================================================

struct AudioDuplex::Impl {
    AudioDuplexStream stream;
    Callback user_callback;
    int input_device_index = -1;
    int output_device_index = -1;
    int sample_rate = 48000;
    int channels = 1;

    void onAudioData(const float* input, float* output, size_t frames, int ch) {
        if (user_callback) {
            user_callback(input, output, frames, ch);
        } else if (output) {
            // Fill with silence if no callback
            std::memset(output, 0, frames * ch * sizeof(float));
        }
    }
};

AudioDuplex::AudioDuplex(int input_device, int output_device)
    : impl_(std::make_unique<Impl>()) {
    impl_->input_device_index = input_device;
    impl_->output_device_index = output_device;
}

AudioDuplex::~AudioDuplex() {
    Close();
}

void AudioDuplex::SetCallback(Callback cb) {
    impl_->user_callback = std::move(cb);
}

bool AudioDuplex::Start(int sample_rate, int channels, int frames_per_buffer) {
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;

    // Set stream callback
    impl_->stream.setCallback(
        [this](const float* input, float* output, size_t frames, int ch, void*) {
            impl_->onAudioData(input, output, frames, ch);
        },
        nullptr);

    // Configure and open stream
    AudioDuplexConfig config;
    config.sample_rate = sample_rate;
    config.channels = channels;
    config.frames_per_buffer = frames_per_buffer;
    config.input_device_index = impl_->input_device_index;
    config.output_device_index = impl_->output_device_index;

    if (!impl_->stream.open(config)) {
        return false;
    }

    return impl_->stream.start();
}

void AudioDuplex::Stop() {
    impl_->stream.stop();
}

void AudioDuplex::Close() {
    impl_->stream.close();
}

bool AudioDuplex::IsRunning() const {
    return impl_->stream.isRunning();
}

int AudioDuplex::GetSampleRate() const {
    return impl_->stream.getSampleRate();
}

int AudioDuplex::GetChannels() const {
    return impl_->stream.getChannels();
}

int AudioDuplex::GetInputDevice() const {
    return impl_->stream.getInputDeviceIndex();
}

int AudioDuplex::GetOutputDevice() const {
    return impl_->stream.getOutputDeviceIndex();
}

std::vector<std::pair<int, std::string>> AudioDuplex::ListInputDevices() {
    std::vector<std::string> names;
    std::vector<int> indices;
    AudioDuplexStream::listInputDevices(names, indices);

    std::vector<std::pair<int, std::string>> result;
    for (size_t i = 0; i < names.size(); ++i) {
        result.emplace_back(indices[i], names[i]);
    }
    return result;
}

std::vector<std::pair<int, std::string>> AudioDuplex::ListOutputDevices() {
    std::vector<std::string> names;
    std::vector<int> indices;
    AudioDuplexStream::listOutputDevices(names, indices);

    std::vector<std::pair<int, std::string>> result;
    for (size_t i = 0; i < names.size(); ++i) {
        result.emplace_back(indices[i], names[i]);
    }
    return result;
}

}  // namespace SpaceAudio
