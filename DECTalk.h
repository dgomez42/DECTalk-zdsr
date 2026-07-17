#pragma once

#include <Windows.h>

#ifdef DECTALK_EXPORTS
#define DECTALK_API __declspec(dllexport)
#else
#define DECTALK_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DO_NOT_USE_AUDIO_DEVICE  0x80000000
#define TTS_FORCE                1
#define FORMAT                   0x00000004

#pragma pack(push, 8)
typedef struct {
    DWORD dwIndexValue;
    DWORD dwIndexSampleNumber;
    DWORD dwReserved;
} TTS_INDEX_T;

typedef struct {
    char*          lpData;
    void*          lpPhonemeArray;
    TTS_INDEX_T*   lpIndexArray;
    DWORD          dwMaximumBufferLength;
    DWORD          dwMaximumNumberOfPhonemeChanges;
    DWORD          dwMaximumNumberOfIndexMarks;
    DWORD          dwBufferLength;
    DWORD          dwNumberOfPhonemeChanges;
    DWORD          dwNumberOfIndexMarks;
    DWORD          dwReserved;
} TTS_BUFFER_T;
#pragma pack(pop)

DECTALK_API int  WINAPI GetOpenTTSInfo(int* pReserved1, int* pReserved2, int* pReserved3);
DECTALK_API BOOL WINAPI Initial(void);
DECTALK_API int  WINAPI GetVoiceCount(void);
DECTALK_API void WINAPI GetVoiceName(int vid, WCHAR* voiceName);
DECTALK_API void WINAPI SetResponseSpeed(int value);
DECTALK_API void WINAPI SetParam(int type, int value);
DECTALK_API BOOL WINAPI IsSpeaking(void);
DECTALK_API BOOL WINAPI IsControlNumricType(void);
DECTALK_API void WINAPI Speak(WCHAR* text, int pitch);
DECTALK_API void WINAPI Stop(void);
DECTALK_API void WINAPI Pause(void);
DECTALK_API void WINAPI Resume(void);
DECTALK_API int  WINAPI GetAudioFormat(int* pBitsPerSample);
DECTALK_API int  WINAPI TextToAudio(WCHAR* text, int* ptr);
DECTALK_API void WINAPI OpenSettings(void);
DECTALK_API void WINAPI UnInitial(void);

#ifdef __cplusplus
}
#endif
