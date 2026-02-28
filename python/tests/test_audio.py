#!/usr/bin/env python3
# Copyright 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
#
# SPDX-License-Identifier: Apache-2.0
"""
SpaceAudio 测试脚本
"""

import sys
import time
import os

# 添加父目录到路径，以便导入 space_audio
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import space_audio
from space_audio import AudioCapture, AudioPlayer


def test_list_devices():
    """测试设备列表"""
    print("=== 输入设备 ===")
    for idx, name in AudioCapture.list_devices():
        print(f"  [{idx}] {name}")

    print("\n=== 输出设备 ===")
    for idx, name in AudioPlayer.list_devices():
        print(f"  [{idx}] {name}")


def write_wav(filename: str, data: bytes, sample_rate: int = 16000, channels: int = 1):
    """写入 WAV 文件"""
    import struct
    with open(filename, 'wb') as f:
        # WAV header
        data_size = len(data)
        byte_rate = sample_rate * channels * 2
        block_align = channels * 2

        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + data_size))
        f.write(b'WAVE')
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))  # fmt chunk size
        f.write(struct.pack('<H', 1))   # PCM
        f.write(struct.pack('<H', channels))
        f.write(struct.pack('<I', sample_rate))
        f.write(struct.pack('<I', byte_rate))
        f.write(struct.pack('<H', block_align))
        f.write(struct.pack('<H', 16))  # bits per sample
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        f.write(data)


def test_capture(seconds=3, output_file=None):
    """测试录音"""
    total_bytes = 0
    chunks = []

    def on_audio(data: bytes):
        nonlocal total_bytes
        total_bytes += len(data)
        chunks.append(data)
        print(f"\r录音中... {total_bytes} bytes", end="", flush=True)

    config = space_audio.get_config()
    print(f"\n=== 测试录音 ({seconds}秒) ===")
    print(f"配置: {config['sample_rate']}Hz, {config['channels']}ch, chunk={config['chunk_size']}")

    with AudioCapture() as cap:
        cap.set_callback(on_audio)
        cap.start()  # 使用全局配置
        time.sleep(seconds)

    audio_data = b''.join(chunks)
    print(f"\n完成，共 {total_bytes} bytes, {len(chunks)} chunks")

    # 保存文件
    if output_file:
        write_wav(output_file, audio_data, config['sample_rate'], config['channels'])
        print(f"已保存到: {output_file}")

    return audio_data


def test_playback(audio_data: bytes = None):
    """测试播放"""
    config = space_audio.get_config()
    print("\n=== 测试播放 ===")
    print(f"配置: {config['sample_rate']}Hz, {config['channels']}ch")

    if audio_data is None:
        # 生成 1 秒静音
        sample_rate = config['sample_rate']
        channels = config['channels']
        duration = 1
        audio_data = b'\x00\x00' * (sample_rate * channels * duration)
        print("播放 1 秒静音...")
    else:
        print(f"播放 {len(audio_data)} bytes 音频...")

    with AudioPlayer() as player:
        player.start()  # 使用全局配置
        player.write(audio_data)

    print("播放完成")


def test_play_file(file_path: str):
    """测试播放文件"""
    if not os.path.exists(file_path):
        print(f"\n=== 跳过文件播放测试 ({file_path} 不存在) ===")
        return

    print(f"\n=== 测试播放文件: {file_path} ===")
    with AudioPlayer() as player:
        player.play_file(file_path)
    print("播放完成")


def test_record_and_play():
    """录音然后播放"""
    print("\n=== 录音并回放测试 ===")

    # 录音 2 秒
    audio_data = test_capture(2)

    # 播放刚录的音频
    print("\n播放刚录制的音频...")
    test_playback(audio_data)


def main():
    import argparse

    parser = argparse.ArgumentParser(description='SpaceAudio 测试')

    # 全局配置选项
    parser.add_argument('--sample-rate', '-s', type=int, default=16000,
                        help='采样率 (默认: 16000)')
    parser.add_argument('--channels', '-c', type=int, default=1,
                        help='声道数 (默认: 1)')
    parser.add_argument('--chunk-size', type=int, default=3200,
                        help='每次回调字节数 (默认: 3200)')
    parser.add_argument('--capture-device', type=int, default=-1,
                        help='录音设备索引 (默认: -1 自动选择)')
    parser.add_argument('--player-device', type=int, default=-1,
                        help='播放设备索引 (默认: -1 自动选择)')

    # 功能选项
    parser.add_argument('-l', '--list', action='store_true', help='列出设备')
    parser.add_argument('-r', '--record', type=int, metavar='SECONDS', help='录音测试')
    parser.add_argument('-o', '--output', type=str, metavar='FILE', help='录音输出文件 (配合 -r 使用)')
    parser.add_argument('-p', '--play', type=str, metavar='FILE', help='播放文件')
    parser.add_argument('-t', '--test', action='store_true', help='录音并回放测试')
    parser.add_argument('-a', '--all', action='store_true', help='运行所有测试')

    args = parser.parse_args()

    # 初始化全局配置
    space_audio.init(
        sample_rate=args.sample_rate,
        channels=args.channels,
        chunk_size=args.chunk_size,
        capture_device=args.capture_device,
        player_device=args.player_device,
    )

    print(f"配置: {space_audio.get_config()}")

    if args.list:
        test_list_devices()
    elif args.record:
        output = args.output or f"record_{args.record}s.wav"
        test_capture(args.record, output)
    elif args.play:
        test_play_file(args.play)
    elif args.test:
        test_record_and_play()
    elif args.all:
        test_list_devices()
        test_capture(2, "test_record.wav")
        test_playback()
        print("\n所有测试完成!")
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
