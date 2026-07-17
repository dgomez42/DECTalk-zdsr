# DECTalk - ZDSR OpenTTS 语音插件

基于经典 DECtalk 语音引擎的 [ZDSR 争渡读屏](https://www.zdsr.com/) OpenTTS 语音插件。

该插件实现了[争渡读屏语音开放接口 v2 (ZDOpenTTSAPI)](https://www.zdsr.com/docs/api/zdsr-opentts-api/)，将 DECtalk 语音引擎封装为 32 位 DLL，供争渡读屏作为语音插件加载。

## 功能

- 将经典 DECtalk 语音引擎封装为 ZDSR 语音插件
- 支持 7 种语音变体：美式英语、英式英语、西班牙语、拉丁西班牙语、德语，以及美式/拉丁新播音员
- 完整实现 ZDSR OpenTTS API v2 全部 16 个接口
- 支持语速、音量、音高、角色选择等参数调整
- 包含语言选择设置对话框
- 支持文本转音频数据输出

## 安装

1. 将编译得到的 `DECTalk.dll` 及 `DECTalk_Data/` 整个目录放置到 `{pf}\zdsr\common\opentts\DECTalk\` 下
2. 在 ZDSR 语音方案中选择 "DECTalk" 即可

## 环境要求

- Windows（32 位）
- Visual Studio 2022（含 C++ 工具链）或 MSVC 构建工具
- ZDSR 争渡读屏（运行时使用）

## 构建

### 命令行构建

```
build.bat
```

### Visual Studio 构建

打开 `DECTalk.sln`，选择 `Win32` 平台，构建 Release 或 Debug 配置。

### 构建输出

编译生成 `DECTalk.dll`。

## 运行时依赖

`DECTalk_Data/` 目录必须与 `DECTalk.dll` 位于同一目录下，包含以下语音数据文件：

| 文件 | 说明 |
|------|------|
| `dtalk_us.dll` / `dtalk_us.dic` | 美式英语 |
| `dtalk_uk.dll` / `dtalk_uk.dic` | 英式英语 |
| `dtalk_sp.dll` / `dtalk_sp.dic` | 西班牙语 |
| `dtalk_la.dll` / `dtalk_la.dic` | 拉丁西班牙语 |
| `dtalk_gr.dll` / `dtalk_gr.dic` | 德语 |
| `dtalk_nws_us.dll` / `dtalk_nws_us.dic` | 美式英语（新闻） |
| `dtalk_nws_la.dll` / `dtalk_nws_la.dic` | 拉丁西班牙语（新闻） |

## 项目文件

| 文件 | 说明 |
|------|------|
| `DECTalk.h` | 公共 API 头文件 |
| `DECTalk.cpp` | 主实现（DECtalk 引擎封装 + ZDSR OpenTTS 接口实现） |
| `wave_player.h` / `wave_player.cpp` | 音频播放封装（waveOut API） |
| `DECTalk.rc` / `resource.h` | 设置对话框资源 |
| `DECTalk.def` | 模块定义文件（导出符号列表） |
| `build.bat` | 命令行构建脚本 |

## 接口实现

该插件完整实现 ZDSR OpenTTS API v2 定义的 16 个导出函数：

| 函数 | 对应 API 文档 |
|------|--------------|
| `GetOpenTTSInfo` | 获取本接口信息 |
| `Initial` | 初始化语音接口 |
| `GetVoiceCount` | 获取角色数量 |
| `GetVoiceName` | 获取指定角色的名称 |
| `SetResponseSpeed` | 设置响应速度 |
| `SetParam` | 设置语音引擎参数 |
| `IsSpeaking` | 获取朗读状态 |
| `IsControlNumricType` | 获取是否引擎控制数字读法 |
| `Speak` | 朗读文本 |
| `Stop` | 停止朗读 |
| `Pause` | 暂停朗读 |
| `Resume` | 恢复朗读 |
| `GetAudioFormat` | 获取音频采样位数和采样率 |
| `TextToAudio` | 获取对指定文本返回合成的音频数据 |
| `OpenSettings` | 打开引擎设置 |
| `UnInitial` | 释放语音接口 |

## 额外配置

在插件目录下创建 `DECTalk.ini`（UTF-16LE 编码）可配置文本预处理行为：

```ini
[Settings]
novocalchar= ; 替换发音字符，缺省为空格
filterchars= ; 读屏标点方案设置为不发音，而该引擎会发音的标点符号串
```

## 相关链接

- [ZDSR 争渡读屏](https://www.zdsr.com/)
- [争渡读屏语音开放接口 API 文档](https://www.zdsr.com/docs/api/zdsr-opentts-api/)
- [DECtalk 维基百科](https://en.wikipedia.org/wiki/DECtalk)

## 许可证

MIT License。详见 [LICENSE](LICENSE) 文件。

注意：本项目包含第三方 DECtalk 语音数据文件，这些文件可能受其自身许可证条款约束。
