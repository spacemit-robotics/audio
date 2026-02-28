# SpaceAudio

## 项目简介

音频采集和播放库，基于 PortAudio，支持 C++ 和 Python。提供录音、播放、全双工音频 I/O 及重采样功能，适用于 Spacemit K3 (RISC-V) 机器人平台。

## 功能特性

- 音频采集（录音）与播放
- 全双工音频 I/O（适用于 AEC 等场景）
- 音频重采样（含 RISC-V Vector 加速优化）
- C++ API 与 Python 绑定（pybind11）
- WAV 文件读写
- 设备枚举与选择

## 快速开始

### 环境准备

```bash
# Debian/Ubuntu
sudo apt install portaudio19-dev libsndfile1-dev

# 可选：高质量重采样支持
sudo apt install libsamplerate-dev

# 可选：Python 绑定
sudo apt install pybind11-dev
```

### 构建编译

**独立构建：**

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

**构建产物：**

```
libspace_audio.a            # C++ 音频库（PortAudio 封装）
libaudio_resampler.a        # 重采样库（无外部依赖）
bin/audio_demo              # C++ 示例程序
space_audio/                # Python 模块（_space_audio.so + __init__.py）
```

**CMake 选项：**

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `AUDIO_BUILD_PYTHON` | `ON` | 构建 Python 绑定（需要 pybind11） |
| `AUDIO_BUILD_EXAMPLES` | `ON` | 构建示例程序 |
| `AUDIO_SHARED` | `OFF` | 构建为动态库（默认静态库） |

```bash
# 示例：禁用 Python 绑定
cmake -DAUDIO_BUILD_PYTHON=OFF ..
```

**安装 Python 模块：**

```bash
# 方式一（推荐）：通过 pip 安装（会自动触发 CMake 构建）
cd python
pip install .

# 方式三：构建 wheel（whl生成于 dist 文件夹下）
cd python
pip wheel . -w dist/

# 方式三：通过 CMake target 安装到当前 Python 环境的 site-packages
cmake --build build --target audio-install-python
```

### 运行示例

**C++ Demo：**

```bash
audio_demo -l                    # 列出音频设备
audio_demo record 5 output.wav   # 录音 5 秒，保存到 output.wav
audio_demo play output.wav       # 播放 WAV 文件
```

**Python Demo：**

> 运行前提：需要先通过上述方式安装 `space_audio` Python 模块。

```bash
python examples/audio_demo.py -l                # 列出音频设备
python examples/audio_demo.py -r 5              # 录音 5 秒（自动保存为 record_5s.wav）
python examples/audio_demo.py -r 5 -o a.wav     # 录音 5 秒，保存到指定文件
python examples/audio_demo.py -p a.wav           # 播放 WAV 文件
python examples/audio_demo.py -t                 # 录音并立即回放
python examples/audio_demo.py -s 48000 -c 2 -r 5 -o stereo.wav  # 自定义采样率和声道
```

## 详细使用

### C++ API

```cpp
#include "audio_base.hpp"
using namespace SpaceAudio;

// 初始化全局配置 (可选)
Init(16000, 1, 3200, -1, -1);

// 录音
AudioCapture capture;
capture.SetCallback([](const uint8_t* data, size_t size) {
    // PCM16 little-endian 数据
});
capture.Start();  // 使用全局配置
// ...
capture.Stop();
capture.Close();

// 播放
AudioPlayer player;
player.Start();  // 使用全局配置
player.Write(pcm_data, size);
player.PlayFile("audio.wav");
player.Stop();
player.Close();
```

### Python API

```python
import time
import space_audio
from space_audio import AudioCapture, AudioPlayer

# 初始化全局配置 (可选)
space_audio.init(
    sample_rate=16000,
    channels=1,
    chunk_size=3200,
    capture_device=-1,
    player_device=-1,
)

# 录音 (回调模式，支持 context manager)
chunks = []
def on_audio(data: bytes):
    chunks.append(data)

with AudioCapture() as cap:
    cap.set_callback(on_audio)
    cap.start()  # 使用全局配置
    time.sleep(3)

# 播放
with AudioPlayer() as player:
    player.start()  # 使用全局配置
    for chunk in chunks:
        player.write(chunk)

# 播放文件
with AudioPlayer() as player:
    player.play_file("audio.wav")
```

### CMake 集成

在 SDK 内其他组件中使用：

```cmake
find_package(Components REQUIRED)
target_link_libraries(your_target PRIVATE components::space_audio)
```

或作为子目录引入：

```cmake
add_subdirectory(audio)
target_link_libraries(your_target PRIVATE space_audio)
```

详见 [API.md](API.md)

## 常见问题

- **找不到 PortAudio**：确保已安装 `portaudio19-dev`（`sudo apt install portaudio19-dev`），可用 `pkg-config --exists portaudio-2.0 && echo OK` 验证。
- **Python 模块编译失败**：检查 pybind11 是否已安装（`sudo apt install pybind11-dev` 或 `pip install pybind11`），或使用 `-DAUDIO_BUILD_PYTHON=OFF` 跳过 Python 绑定。
- **`import space_audio` 报 ModuleNotFoundError**：Python 模块需要先构建并安装。使用 `cmake --build build --target audio-install-python` 或 `cd python && pip install .` 安装。
- **设备列表为空**：检查系统音频设备是否正常，可使用 `arecord -l` / `aplay -l` 验证。
- **K1 平台声卡采样率限制**：snd-es8326 声卡默认只支持 48kHz 采样率的录音和播放。如果使用其他采样率（如 16kHz），需要通过 Resampler 进行重采样。
- **K3 平台声卡配置**：snd-es8326 无基座，需要自行配置外接麦克风和扬声器。
- **Failed to open stream: Invalid sample rate**：采样率与声卡不匹配。使用 `cat /proc/asound/card0/stream0`（card 后的数字根据实际声卡编号调整）查看声卡支持的输入输出采样率，确保配置的采样率在支持列表中。

## 版本与发布

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.1.0 | 2026-02-28 | 初始版本，支持录音、播放、全双工、重采样 |

## 贡献方式

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/xxx`)
3. 提交更改 (`git commit -m 'feat(audio): xxx'`)
4. 推送到分支 (`git push origin feature/xxx`)
5. 创建 Pull Request

## License

Apache-2.0
