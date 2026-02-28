/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * SpaceAudio Demo - 录音、播放示例
 *
 * Usage:
 *   ./audio_demo -l                          # 列出设备
 *   ./audio_demo record 5 a.wav              # 录音 5 秒（默认双声道）
 *   ./audio_demo play a.wav                  # 播放文件
 *   ./audio_demo -i 2 -c 1 record 5 a.wav   # 用设备2、单声道录音
 *   ./audio_demo -s 16000 record 5 a.wav    # 16kHz采样率录音
 *   ./audio_demo -o 3 play a.wav             # 用设备3播放
 */

#include "audio_base.hpp"

#include <getopt.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using SpaceAudio::AudioCapture;
using SpaceAudio::AudioPlayer;

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

void listDevices() {
    std::cout << "=== Input Devices ===" << std::endl;
    for (auto& [idx, name] : AudioCapture::ListDevices()) {
        std::cout << "  [" << idx << "] " << name << std::endl;
    }

    std::cout << "\n=== Output Devices ===" << std::endl;
    for (auto& [idx, name] : AudioPlayer::ListDevices()) {
        std::cout << "  [" << idx << "] " << name << std::endl;
    }
}

void writeWavHeader(std::ofstream& file, int sample_rate, int channels, int data_size) {
    int byte_rate = sample_rate * channels * 2;
    int block_align = channels * 2;
    int file_size = 36 + data_size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&file_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    int fmt_size = 16;
    file.write(reinterpret_cast<char*>(&fmt_size), 4);
    int16_t audio_format = 1;  // PCM
    file.write(reinterpret_cast<char*>(&audio_format), 2);
    int16_t ch = channels;
    file.write(reinterpret_cast<char*>(&ch), 2);
    file.write(reinterpret_cast<char*>(&sample_rate), 4);
    file.write(reinterpret_cast<char*>(&byte_rate), 4);
    int16_t ba = block_align;
    file.write(reinterpret_cast<char*>(&ba), 2);
    int16_t bps = 16;
    file.write(reinterpret_cast<char*>(&bps), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&data_size), 4);
}

void recordAudio(int seconds, const std::string& filename, int device = -1, int channels = 2, int sample_rate = 48000) {
    std::vector<uint8_t> all_data;

    AudioCapture capture(device);
    capture.SetCallback([&](const uint8_t* data, size_t size) {
        all_data.insert(all_data.end(), data, data + size);
    });

    std::cout << "Recording " << seconds << "s to " << filename << "..." << std::endl;

    if (!capture.Start(sample_rate, channels)) {
        std::cerr << "Failed to start capture" << std::endl;
        return;
    }

    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    capture.Stop();
    capture.Close();

    // Write WAV file
    std::ofstream file(filename, std::ios::binary);
    writeWavHeader(file, sample_rate, channels, all_data.size());
    file.write(reinterpret_cast<char*>(all_data.data()), all_data.size());
    file.close();

    std::cout << "Saved " << all_data.size() << " bytes" << std::endl;
}

void playAudio(const std::string& filename, int device = -1) {
    std::cout << "Playing " << filename << "..." << std::endl;

    AudioPlayer player(device);
    if (!player.PlayFile(filename)) {
        std::cerr << "Failed to play file" << std::endl;
    }

    std::cout << "Done" << std::endl;
}

void printUsage(const char* prog) {
    std::cout << "Usage:\n"
        << "  " << prog << " [options] -l                    List devices\n"
        << "  " << prog << " [options] record <secs> <file>  Record audio\n"
        << "  " << prog << " [options] play <file>           Play audio\n"
        << "\nOptions:\n"
        << "  -s <rate>  Sample rate in Hz (default: 48000)\n"
        << "  -c <num>   Channels (default: 2)\n"
        << "  -i <idx>   Input device index (default: -1, auto)\n"
        << "  -o <idx>   Output device index (default: -1, auto)\n"
        << "  -l         List available devices\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);

    int sample_rate = 48000;
    int channels = 2;
    int input_device = -1;
    int output_device = -1;
    bool list = false;

    int opt;
    while ((opt = getopt(argc, argv, "s:c:i:o:l")) != -1) {
        switch (opt) {
            case 's': sample_rate = std::atoi(optarg); break;
            case 'c': channels = std::atoi(optarg); break;
            case 'i': input_device = std::atoi(optarg); break;
            case 'o': output_device = std::atoi(optarg); break;
            case 'l': list = true; break;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    if (list) {
        listDevices();
        return 0;
    }

    if (optind >= argc) {
        printUsage(argv[0]);
        return 1;
    }

    const char* cmd = argv[optind];
    if (std::strcmp(cmd, "record") == 0 && optind + 2 < argc) {
        int seconds = std::atoi(argv[optind + 1]);
        recordAudio(seconds, argv[optind + 2], input_device, channels, sample_rate);
    } else if (std::strcmp(cmd, "play") == 0 && optind + 1 < argc) {
        playAudio(argv[optind + 1], output_device);
    } else {
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
