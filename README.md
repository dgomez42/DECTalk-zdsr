# DECTalk - ZDSR OpenTTS Voice Plugin

A [ZDSR](https://www.zdsr.com/) OpenTTS voice plugin based on the classic DECtalk text-to-speech engine.

This plugin implements the [ZDSR OpenTTS API v2](https://www.zdsr.com/docs/api/zdsr-opentts-api/) (ZDOpenTTSAPI), wrapping the DECtalk engine into a 32-bit DLL that ZDSR screen reader can load as a voice plugin.

## Features

- Wraps the classic DECtalk engine as a ZDSR voice plugin
- 7 voice variants: US English, UK English, Spanish, Latin American Spanish, German, plus Newscaster variants for US and Latin American
- Full implementation of all 16 ZDSR OpenTTS API v2 interfaces
- Adjustable speech rate, volume, pitch, voice selection, and more
- Language selection settings dialog
- Text-to-audio data output support

## Installation

1. Copy the compiled `DECTalk.dll` and the entire `DECTalk_Data/` directory to `{pf}\zdsr\common\opentts\DECTalk\`
2. Select "DECTalk" in ZDSR voice scheme settings

## Requirements

- Windows (32-bit)
- Visual Studio 2022 (with C++ tools) or MSVC build tools
- ZDSR screen reader (for runtime use)

## Building

### Command Line

```
build.bat
```

### Visual Studio

Open `DECTalk.sln` and build for `Win32` platform (Release or Debug configuration).

### Output

The build produces `DECTalk.dll`.

## Runtime Dependencies

`DECTalk_Data/` must be in the same directory as `DECTalk.dll`. It contains the following voice data files:

| File | Description |
|------|-------------|
| `dtalk_us.dll` / `dtalk_us.dic` | US English |
| `dtalk_uk.dll` / `dtalk_uk.dic` | UK English |
| `dtalk_sp.dll` / `dtalk_sp.dic` | Spanish |
| `dtalk_la.dll` / `dtalk_la.dic` | Latin American Spanish |
| `dtalk_gr.dll` / `dtalk_gr.dic` | German |
| `dtalk_nws_us.dll` / `dtalk_nws_us.dic` | US English (Newscaster) |
| `dtalk_nws_la.dll` / `dtalk_nws_la.dic` | Latin American Spanish (Newscaster) |

## Project Files

| File | Description |
|------|-------------|
| `DECTalk.h` | Public API header |
| `DECTalk.cpp` | Main implementation (DECtalk engine wrapper + ZDSR OpenTTS interface) |
| `wave_player.h` / `wave_player.cpp` | Audio playback wrapper (waveOut API) |
| `DECTalk.rc` / `resource.h` | Settings dialog resource |
| `DECTalk.def` | Module definition file (exported symbols) |
| `build.bat` | Command-line build script |

## API Implementation

This plugin implements all 16 exported functions defined by ZDSR OpenTTS API v2:

| Function | Description |
|----------|-------------|
| `GetOpenTTSInfo` | Get interface information |
| `Initial` | Initialize TTS engine |
| `GetVoiceCount` | Get the number of voices |
| `GetVoiceName` | Get voice name by index |
| `SetResponseSpeed` | Set speech rate |
| `SetParam` | Set engine parameters (voice, rate, volume, pitch, etc.) |
| `IsSpeaking` | Check speaking status |
| `IsControlNumricType` | Check if engine controls digit pronunciation |
| `Speak` | Speak text |
| `Stop` | Stop speaking |
| `Pause` | Pause speaking |
| `Resume` | Resume speaking |
| `GetAudioFormat` | Get audio sample format |
| `TextToAudio` | Synthesize text to audio data |
| `OpenSettings` | Open engine settings dialog |
| `UnInitial` | Release TTS engine |

## Additional Configuration

Create `DECTalk.ini` (UTF-16LE encoding) in the plugin directory to configure text preprocessing:

```ini
[Settings]
novocalchar= ; Character to replace unpronounceable characters, defaults to space
filterchars= ; Punctuation that the engine pronounces but should be silent
```

## Links

- [ZDSR Screen Reader](https://www.zdsr.com/)
- [ZDSR OpenTTS API Documentation](https://www.zdsr.com/docs/api/zdsr-opentts-api/)
- [DECtalk on Wikipedia](https://en.wikipedia.org/wiki/DECtalk)

## License

MIT License. See [LICENSE](LICENSE) for details.

Note: This project includes third-party DECtalk voice data files which may be subject to their own licensing terms.
