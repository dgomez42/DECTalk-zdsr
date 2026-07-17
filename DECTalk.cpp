#define _CRT_SECURE_NO_WARNINGS
#include "DECTalk.h"
#include "wave_player.h"
#include <string>
#include <vector>
#include <queue>
#include <stdio.h>
#include "resource.h"

#pragma comment(lib, "winmm.lib")

#define LOG(...) do { \
    FILE* _lf = fopen("DECTalk_debug.log", "a"); \
    if (_lf) { fprintf(_lf, __VA_ARGS__); fclose(_lf); } \
} while(0)

static const int SAMPLES = 16384;
static const int INDEX_ARRAY_SIZE = 3;

typedef int (__cdecl *PFN_TextToSpeechStartup)(HWND, void*, int, int);
typedef int (__cdecl *PFN_TextToSpeechSpeak)(void*, const char*, int);
typedef int (__cdecl *PFN_TextToSpeechReset)(void*, int);
typedef int (__cdecl *PFN_TextToSpeechShutdown)(void*);
typedef int (__cdecl *PFN_TextToSpeechOpenInMemory)(void*, int);
typedef int (__cdecl *PFN_TextToSpeechAddBuffer)(void*, void*);
typedef int (__cdecl *PFN_TextToSpeechGetCaps)(void*);

struct VoiceInfo {
    const WCHAR* name;
    int pitch;
    int inflection;
    int volume;
};

static const VoiceInfo g_voices[] = {
    { L"Perfect Paul",   122, 100, 81 },
    { L"Beautiful Betty", 208, 240, 75 },
    { L"Huge Harry",      89,  80, 76 },
    { L"Frail Frank",    155,  90, 73 },
    { L"Doctor Dennis",  110, 135, 70 },
    { L"Kit The Kid",    296, 180, 68 },
    { L"Uppity Ursula",  240, 135, 68 },
    { L"Rough Rita",     106,  80, 76 },
    { L"Whispering Wendy",200, 175, 70 },
};
static const int g_numVoices = sizeof(g_voices) / sizeof(g_voices[0]);

static const WCHAR* g_voiceNames[] = {
    L"Paul", L"Betty", L"Harry", L"Frank",
    L"Dennis", L"Kit", L"Ursula", L"Rita", L"Wendy",
};

struct LanguageInfo {
    const WCHAR* dllName;
    const WCHAR* displayName;
};
static const LanguageInfo g_languages[] = {
    { L"dtalk_us",     L"US English" },
    { L"dtalk_uk",     L"British English" },
    { L"dtalk_la",     L"Latin American Spanish" },
    { L"dtalk_sp",     L"Spanish" },
    { L"dtalk_gr",     L"German" },
    { L"dtalk_nws_us", L"New World Spanish (US)" },
    { L"dtalk_nws_la", L"New World Spanish (Latin American)" },
};
static const int g_numLanguages = sizeof(g_languages) / sizeof(g_languages[0]);

static const UINT WM_TTS_SPEAK = WM_APP + 1;
static const UINT WM_TTS_SYNC_SPEAK = WM_APP + 2;
static const UINT WM_TTS_STOP = WM_APP + 3;

static UINT g_wmBuffer = 0;
static UINT g_wmIndex = 0;

static HMODULE g_hModule = NULL;
static HMODULE g_hDecTalk = NULL;
static void* g_hTTS = NULL;
static volatile LONG g_speaking = FALSE;
static CRITICAL_SECTION g_cs;
static HWND g_hMessageWnd = NULL;
static HANDLE g_hWorkerThread = NULL;
static HANDLE g_hInitDone = NULL;

static PFN_TextToSpeechStartup pfnStartup = NULL;
static PFN_TextToSpeechSpeak pfnSpeak = NULL;
static PFN_TextToSpeechReset pfnReset = NULL;
static PFN_TextToSpeechShutdown pfnShutdown = NULL;
static PFN_TextToSpeechOpenInMemory pfnOpenInMemory = NULL;
static PFN_TextToSpeechAddBuffer pfnAddBuffer = NULL;

static WavePlayer* g_player = NULL;

static short* g_audioBuf = NULL;
static int g_audioLen = 0;
static int g_audioCap = 0;
static HANDLE g_hSyncDone = NULL;
static TTS_BUFFER_T* g_initBuf = NULL;
static HANDLE g_hLicenseMapping = NULL;

static int g_curVoice = 0;
static int g_curRate = 50;
static int g_curVolume = 80;
static int g_curPitch = -1;
static int g_curDigitalType = 0;
static int g_curPitchPercent = 50;
static WCHAR g_selectedDllName[64] = L"dtalk_us";

static int g_dt_pitch = 122;
static int g_dt_inflection = 100;
static int g_dt_volume = 81;

static volatile BOOL g_genDone = FALSE;
static UINT_PTR g_idleTimer = 0;
static const UINT_PTR IDLE_TIMER_ID = 1001;
static volatile BOOL g_syncInProgress = FALSE;

static void GetConfigPath(WCHAR* buf, DWORD size) {
    WCHAR user[MAX_PATH];
    GetEnvironmentVariableW(L"USERPROFILE", user, MAX_PATH);
    swprintf_s(buf, size, L"%s\\Documents\\my_zdsr\\zdsr\\userdata\\OpenTTS\\DECTalk.ini", user);
}

static void SaveConfig(const WCHAR* language) {
    WCHAR path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    FILE* f = _wfopen(path, L"w, ccs=UNICODE");
    if (f) {
        fwprintf(f, L"[settings]\nlanguage=%s\n", language);
        fclose(f);
    }
}

static void LoadConfig() {
    WCHAR path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    FILE* f = _wfopen(path, L"r, ccs=UNICODE");
    if (!f) return;
    WCHAR line[256];
    while (fgetws(line, 256, f)) {
        WCHAR* eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = L'\0';
        WCHAR* key = line;
        while (*key == L' ' || *key == L'\t') key++;
        WCHAR* val = eq + 1;
        while (*val == L' ' || *val == L'\t') val++;
        WCHAR* nl = wcschr(val, L'\n');
        if (nl) *nl = L'\0';
        nl = wcschr(val, L'\r');
        if (nl) *nl = L'\0';
        if (_wcsicmp(key, L"language") == 0) {
            wcscpy_s(g_selectedDllName, val);
        }
    }
    fclose(f);
}

static void StopEngine() {
    EnterCriticalSection(&g_cs);
    if (g_hWorkerThread) {
        if (g_hMessageWnd) PostMessageA(g_hMessageWnd, WM_QUIT, 0, 0);
        WaitForSingleObject(g_hWorkerThread, 3000);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }
    if (g_hDecTalk) {
        FreeLibrary(g_hDecTalk);
        g_hDecTalk = NULL;
    }
    if (g_audioBuf) {
        HeapFree(GetProcessHeap(), 0, g_audioBuf);
        g_audioBuf = NULL;
    }
    g_audioLen = 0;
    g_audioCap = 0;
    g_hMessageWnd = NULL;
    g_hTTS = NULL;
    LeaveCriticalSection(&g_cs);
}

static void RestoreVoiceParams() {
    if (!g_hMessageWnd) return;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "[:name %c]", g_voiceNames[g_curVoice][0]);
    PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
    int rateVal = 75 + (g_curRate * (650 - 75) / 100);
    snprintf(cmd, sizeof(cmd), "[:rate %d]", rateVal);
    PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
    snprintf(cmd, sizeof(cmd), "[:dv ap %d]", g_dt_pitch);
    PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
    snprintf(cmd, sizeof(cmd), "[:dv pr %d]", g_dt_inflection);
    PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
    snprintf(cmd, sizeof(cmd), "[:dv g5 %d]", g_dt_volume);
    PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
}

static const char* GetVoiceParamPrefix() {
    return "[:skip rule :comma 0 :comma -280 :period 0 :period -420 :mode spell off :punctuation none] ";
}

static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        g_wmBuffer = RegisterWindowMessageW(L"DECtalkBufferMessage");
        g_wmIndex = RegisterWindowMessageW(L"DECtalkIndexMessage");
        return 0;
    }

    if (msg == WM_TIMER && wParam == IDLE_TIMER_ID) {
        if (g_genDone && g_player && g_player->isIdle()) {
            KillTimer(hwnd, IDLE_TIMER_ID);
            g_idleTimer = 0;
            g_genDone = FALSE;
            InterlockedExchange(&g_speaking, FALSE);
        }
        return 0;
    }

    if (g_wmIndex && msg == g_wmIndex) {
        return 0;
    }

    if (g_wmBuffer && msg == g_wmBuffer) {
        TTS_BUFFER_T* pBuf = (TTS_BUFFER_T*)lParam;
        if (!pBuf) return 0;

        if (pBuf->dwBufferLength > 0 && pBuf->lpData && g_player && !g_syncInProgress) {
            g_player->feed((const BYTE*)pBuf->lpData, pBuf->dwBufferLength);
        }

        if (pBuf->dwBufferLength > 0 && pBuf->lpData) {
            int samples = pBuf->dwBufferLength / 2;
            int newLen = g_audioLen + samples;
            if (newLen > g_audioCap) {
                int newCap = (newLen > 64) ? newLen * 2 : 128;
                short* newBuf = (short*)HeapAlloc(GetProcessHeap(), 0, newCap * sizeof(short));
                if (newBuf) {
                    if (g_audioBuf) {
                        memcpy(newBuf, g_audioBuf, g_audioLen * sizeof(short));
                        HeapFree(GetProcessHeap(), 0, g_audioBuf);
                    }
                    g_audioBuf = newBuf;
                    g_audioCap = newCap;
                }
            }
            if (g_audioBuf) {
                memcpy(g_audioBuf + g_audioLen, pBuf->lpData, pBuf->dwBufferLength);
                g_audioLen += samples;
            }
        }

        if (pBuf->dwNumberOfIndexMarks > 0 && pBuf->lpIndexArray) {
            DWORD lastIdx = pBuf->lpIndexArray[pBuf->dwNumberOfIndexMarks - 1].dwIndexValue;
            if (lastIdx == 32000) {
                g_genDone = TRUE;
            }
        }

        pBuf->dwBufferLength = 0;
        if (g_hTTS && pfnAddBuffer) pfnAddBuffer(g_hTTS, pBuf);
        return 0;
    }

    if (msg == WM_TTS_SPEAK) {
        if (!pfnSpeak || !g_hTTS) {
            HeapFree(GetProcessHeap(), 0, (void*)lParam);
            return 0;
        }
        const char* text = (const char*)lParam;
        if (wParam) {
            if (g_wmBuffer) {
                MSG drain;
                while (PeekMessageW(&drain, hwnd, g_wmBuffer, g_wmBuffer, PM_REMOVE));
            }
            if (pfnReset) pfnReset(g_hTTS, 0);
            if (g_player) g_player->stop();
            g_genDone = FALSE;
        }
        g_genDone = FALSE;
        InterlockedExchange(&g_speaking, TRUE);
        pfnSpeak(g_hTTS, text, TTS_FORCE);
        HeapFree(GetProcessHeap(), 0, (void*)lParam);
        SetTimer(hwnd, IDLE_TIMER_ID, 100, NULL);
        return 0;
    }

    if (msg == WM_TTS_STOP) {
        if (g_player) g_player->stop();
        g_genDone = FALSE;
        if (g_idleTimer && hwnd) {
            KillTimer(hwnd, IDLE_TIMER_ID);
            g_idleTimer = 0;
        }
        InterlockedExchange(&g_speaking, FALSE);
        return 0;
    }

    if (msg == WM_TTS_SYNC_SPEAK) {
        if (!pfnSpeak || !g_hTTS) {
            HeapFree(GetProcessHeap(), 0, (void*)lParam);
            if (g_hSyncDone) SetEvent(g_hSyncDone);
            return 0;
        }
        const char* text = (const char*)lParam;
        g_audioLen = 0;
        g_genDone = FALSE;
        g_syncInProgress = TRUE;
        InterlockedExchange(&g_speaking, TRUE);
        pfnSpeak(g_hTTS, text, TTS_FORCE);
        HeapFree(GetProcessHeap(), 0, (void*)lParam);

        DWORD startTick = GetTickCount();
        while (!g_genDone && (GetTickCount() - startTick) < 30000) {
            MSG pump;
            BOOL got = PeekMessageW(&pump, NULL, 0, 0, PM_REMOVE);
            if (!got) {
                Sleep(5);
                continue;
            }
            if (pump.message == WM_QUIT) {
                PostQuitMessage((int)pump.wParam);
                break;
            }
            TranslateMessage(&pump);
            DispatchMessageW(&pump);
        }

        g_syncInProgress = FALSE;
        InterlockedExchange(&g_speaking, FALSE);
        if (g_hSyncDone) SetEvent(g_hSyncDone);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI TTSWorkerThread(LPVOID lpParam) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MessageWndProc;
    wc.lpszClassName = L"DECTalkPluginWnd";
    wc.hInstance = GetModuleHandleW(NULL);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DECTalkPluginWnd", L"DECTalk", 0, 0, 0, 0, 0,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        InterlockedExchange(&g_speaking, FALSE);
        SetEvent(g_hInitDone);
        return 2;
    }
    g_hMessageWnd = hwnd;

    WCHAR dllPath[MAX_PATH] = {0};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
    WCHAR* p = wcsrchr(dllPath, L'\\');
    WCHAR langPath[MAX_PATH];
    if (p) {
        p[1] = L'\0';
        swprintf_s(langPath, L"%sDECTalk_Data\\%s.dll", dllPath, g_selectedDllName);
        wcscpy_s(dllPath, langPath);
    } else {
        swprintf_s(dllPath, L"DECTalk_Data\\%s.dll", g_selectedDllName);
    }

    HMODULE hDecTalk = LoadLibraryW(dllPath);
    if (!hDecTalk) {
        WCHAR fallbackPath[MAX_PATH];
        swprintf_s(fallbackPath, L"DECTalk_Data\\%s.dll", g_selectedDllName);
        hDecTalk = LoadLibraryW(fallbackPath);
    }
    if (!hDecTalk) {
        SetEvent(g_hInitDone);
        return 2;
    }
    g_hDecTalk = hDecTalk;

    pfnStartup = (PFN_TextToSpeechStartup)GetProcAddress(hDecTalk, "TextToSpeechStartup");
    pfnSpeak = (PFN_TextToSpeechSpeak)GetProcAddress(hDecTalk, "TextToSpeechSpeak");
    pfnReset = (PFN_TextToSpeechReset)GetProcAddress(hDecTalk, "TextToSpeechReset");
    pfnShutdown = (PFN_TextToSpeechShutdown)GetProcAddress(hDecTalk, "TextToSpeechShutdown");
    pfnOpenInMemory = (PFN_TextToSpeechOpenInMemory)GetProcAddress(hDecTalk, "TextToSpeechOpenInMemory");
    pfnAddBuffer = (PFN_TextToSpeechAddBuffer)GetProcAddress(hDecTalk, "TextToSpeechAddBuffer");

    if (!pfnStartup || !pfnSpeak || !pfnShutdown) {
        SetEvent(g_hInitDone);
        return 2;
    }

    g_hLicenseMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 512, "a32DECtalkDllFileMap");
    if (g_hLicenseMapping) {
        void* buf = MapViewOfFile(g_hLicenseMapping, FILE_MAP_WRITE, 0, 0, 0);
        if (buf) {
            const BYTE license[] = "\0\0\0\0r250hRm2no9fmP75YwvRhnRB81Uv6vZOTb7SdKWKae8k3BXL8U6r??3B0P91";
            memcpy(buf, license, sizeof(license));
        }
    }

    WCHAR oldCwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, oldCwd);
    WCHAR decTalkDir[MAX_PATH];
    wcscpy_s(decTalkDir, dllPath);
    WCHAR* lastSlash = wcsrchr(decTalkDir, L'\\');
    if (lastSlash) {
        lastSlash[1] = L'\0';
        SetCurrentDirectoryW(decTalkDir);
    }
    void* handle = NULL;
    int result = pfnStartup(hwnd, &handle, 0, DO_NOT_USE_AUDIO_DEVICE);
    SetCurrentDirectoryW(oldCwd);
    if (result != 0) {
        SetEvent(g_hInitDone);
        return 2;
    }
    g_hTTS = handle;

    if (pfnOpenInMemory) {
        pfnOpenInMemory(handle, FORMAT);
    }

    if (pfnAddBuffer) {
        g_initBuf = (TTS_BUFFER_T*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TTS_BUFFER_T));
        if (g_initBuf) {
            char* bufData = (char*)HeapAlloc(GetProcessHeap(), 0, SAMPLES * 2);
            TTS_INDEX_T* idxArr = (TTS_INDEX_T*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(TTS_INDEX_T) * INDEX_ARRAY_SIZE);
            if (bufData && idxArr) {
                g_initBuf->lpData = bufData;
                g_initBuf->lpIndexArray = idxArr;
                g_initBuf->dwMaximumBufferLength = SAMPLES * 2;
                g_initBuf->dwMaximumNumberOfIndexMarks = INDEX_ARRAY_SIZE;
                pfnAddBuffer(handle, g_initBuf);
            } else {
                if (bufData) HeapFree(GetProcessHeap(), 0, bufData);
                if (idxArr) HeapFree(GetProcessHeap(), 0, idxArr);
                HeapFree(GetProcessHeap(), 0, g_initBuf);
                g_initBuf = NULL;
            }
        }
    }

    g_player = new WavePlayer(1, 11025, 16);

    InterlockedExchange(&g_speaking, FALSE);
    SetEvent(g_hInitDone);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_player) { g_player->close(); delete g_player; g_player = NULL; }
    if (g_hLicenseMapping) { CloseHandle(g_hLicenseMapping); g_hLicenseMapping = NULL; }
    g_hMessageWnd = NULL;
    g_hTTS = NULL;

    return 0;
}

static BOOL EnsureInitialized() {
    if (g_hMessageWnd && g_hTTS) return TRUE;

    EnterCriticalSection(&g_cs);

    if (g_hWorkerThread) {
        LeaveCriticalSection(&g_cs);
        return TRUE;
    }

    g_hInitDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_hInitDone) {
        LeaveCriticalSection(&g_cs);
        return FALSE;
    }

    g_hWorkerThread = CreateThread(NULL, 0, TTSWorkerThread, NULL, 0, NULL);
    if (!g_hWorkerThread) {
        CloseHandle(g_hInitDone);
        g_hInitDone = NULL;
        LeaveCriticalSection(&g_cs);
        return FALSE;
    }

    WaitForSingleObject(g_hInitDone, 5000);
    CloseHandle(g_hInitDone);
    g_hInitDone = NULL;

    BOOL ok = (g_hMessageWnd && g_hTTS);
    LeaveCriticalSection(&g_cs);
    return ok;
}

int WINAPI GetOpenTTSInfo(int* pReserved1, int* pReserved2, int* pReserved3) {
    if (pReserved1) *pReserved1 = 0;
    if (pReserved2) *pReserved2 = 0;
    if (pReserved3) *pReserved3 = 0;
    return 2;
}

BOOL WINAPI Initial() {
    LoadConfig();
    return EnsureInitialized() ? TRUE : FALSE;
}

int WINAPI GetVoiceCount() {
    return g_numVoices;
}

void WINAPI GetVoiceName(int vid, WCHAR* voiceName) {
    if (!voiceName) return;
    if (vid >= 0 && vid < g_numVoices) {
        wcscpy_s(voiceName, 64, g_voices[vid].name);
    } else {
        voiceName[0] = L'\0';
    }
}

void WINAPI SetResponseSpeed(int value) {
}

void WINAPI SetParam(int type, int value) {
    switch (type) {
        case 0:
            if (value >= 0 && value < g_numVoices) {
                g_curVoice = value;
                g_dt_pitch = g_voices[value].pitch;
                g_dt_inflection = g_voices[value].inflection;
                g_dt_volume = g_voices[value].volume;
                if (g_hMessageWnd && g_hTTS) {
                    char cmd[64];
                    snprintf(cmd, sizeof(cmd), "[:name %c]", g_voiceNames[value][0]);
                    PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
                }
            }
            break;
        case 1:
            g_curRate = max(0, min(100, value));
            if (g_hMessageWnd && g_hTTS) {
                int rateVal = 75 + (g_curRate * (650 - 75) / 100);
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "[:rate %d]", rateVal);
                PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
            }
            break;
        case 2:
            g_curVolume = max(0, min(100, value));
            g_dt_volume = g_voices[g_curVoice].volume * g_curVolume / 100;
            if (g_hMessageWnd && g_hTTS) {
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "[:dv g5 %d]", g_dt_volume);
                PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
            }
            break;
        case 3:
            g_curPitchPercent = max(0, min(100, value));
            {
                int base = g_voices[g_curVoice].pitch;
                g_dt_pitch = base + (int)((g_curPitchPercent - 50) * (base * 0.3f / 50));
                g_dt_pitch = max(50, min(350, g_dt_pitch));
            }
            if (g_hMessageWnd && g_hTTS) {
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "[:dv ap %d]", g_dt_pitch);
                PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 0, (LPARAM)_strdup(cmd));
            }
            break;
        case 4:
            if (g_player) g_player->selectDevice((UINT)value);
            break;
        case 5:
            g_curDigitalType = value ? 1 : 0;
            break;
    }
}

BOOL WINAPI IsSpeaking() {
    return g_speaking;
}

BOOL WINAPI IsControlNumricType() {
    return FALSE;
}

void WINAPI Speak(WCHAR* text, int pitch) {
    if (!text || !text[0]) return;
    if (!EnsureInitialized()) return;

    int mbLen = WideCharToMultiByte(CP_ACP, 0, text, -1, NULL, 0, NULL, NULL);
    if (mbLen <= 0) return;

    char* mbText = (char*)HeapAlloc(GetProcessHeap(), 0, mbLen);
    if (!mbText) return;
    WideCharToMultiByte(CP_ACP, 0, text, -1, mbText, mbLen, NULL, NULL);

    const char* prefix = GetVoiceParamPrefix();
    int prefixLen = (int)strlen(prefix);
    const char* suffix = " [:index reply 32000]";
    int suffixLen = (int)strlen(suffix);

    int extraLen = 0;
    char pitchCmd[64] = "";
    if (pitch == -1) {
        snprintf(pitchCmd, sizeof(pitchCmd), "[:dv ap %d] ", g_dt_pitch);
        extraLen = (int)strlen(pitchCmd);
    } else if (pitch >= 0 && pitch <= 100) {
        int base = g_voices[g_curVoice].pitch;
        int p = base + (int)((pitch - 50) * (base * 0.3f / 50));
        p = max(50, min(350, p));
        snprintf(pitchCmd, sizeof(pitchCmd), "[:dv ap %d] ", p);
        extraLen = (int)strlen(pitchCmd);
    }

    int cmdLen = prefixLen + extraLen + mbLen + suffixLen;
    char* speakBuf = (char*)HeapAlloc(GetProcessHeap(), 0, cmdLen);
    if (!speakBuf) { HeapFree(GetProcessHeap(), 0, mbText); return; }

    int pos = 0;
    memcpy(speakBuf + pos, prefix, prefixLen); pos += prefixLen;
    if (extraLen > 0) {
        memcpy(speakBuf + pos, pitchCmd, extraLen); pos += extraLen;
    }
    memcpy(speakBuf + pos, mbText, mbLen); pos += mbLen - 1;
    memcpy(speakBuf + pos, suffix, suffixLen + 1);
    HeapFree(GetProcessHeap(), 0, mbText);

    if (!PostMessageA(g_hMessageWnd, WM_TTS_SPEAK, 1, (LPARAM)speakBuf)) {
        HeapFree(GetProcessHeap(), 0, speakBuf);
    }
}

void WINAPI Stop() {
    if (g_hMessageWnd) {
        PostMessageA(g_hMessageWnd, WM_TTS_STOP, 0, 0);
    } else {
        if (g_player) g_player->stop();
        g_genDone = FALSE;
        InterlockedExchange(&g_speaking, FALSE);
    }
}

void WINAPI Pause() {
    if (g_player) g_player->pause(true);
}

void WINAPI Resume() {
    if (g_player) g_player->pause(false);
}

int WINAPI GetAudioFormat(int* pBitsPerSample) {
    if (pBitsPerSample) *pBitsPerSample = 16;
    return 11025;
}

int WINAPI TextToAudio(WCHAR* text, int* ptr) {
    if (ptr) *ptr = 0;
    if (!ptr || !text || !text[0]) return 0;

    if (!EnsureInitialized()) return 0;

    int mbLen = WideCharToMultiByte(CP_ACP, 0, text, -1, NULL, 0, NULL, NULL);
    if (mbLen <= 0) return 0;

    char* mbText = (char*)HeapAlloc(GetProcessHeap(), 0, mbLen);
    if (!mbText) return 0;
    WideCharToMultiByte(CP_ACP, 0, text, -1, mbText, mbLen, NULL, NULL);

    const char* prefix = GetVoiceParamPrefix();
    int prefixLen = (int)strlen(prefix);
    const char* suffix = " [:index reply 32000]";
    int suffixLen = (int)strlen(suffix);
    int cmdLen = prefixLen + mbLen + suffixLen;
    char* speakBuf = (char*)HeapAlloc(GetProcessHeap(), 0, cmdLen);
    if (!speakBuf) { HeapFree(GetProcessHeap(), 0, mbText); return 0; }
    memcpy(speakBuf, prefix, prefixLen);
    memcpy(speakBuf + prefixLen, mbText, mbLen);
    memcpy(speakBuf + prefixLen + mbLen - 1, suffix, suffixLen + 1);
    HeapFree(GetProcessHeap(), 0, mbText);

    if (!g_hSyncDone) g_hSyncDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    ResetEvent(g_hSyncDone);

    g_audioLen = 0;

    if (!PostMessageA(g_hMessageWnd, WM_TTS_SYNC_SPEAK, 0, (LPARAM)speakBuf)) {
        HeapFree(GetProcessHeap(), 0, speakBuf);
        return 0;
    }

    WaitForSingleObject(g_hSyncDone, 30000);

    if (g_audioLen > 0 && g_audioBuf) {
        *ptr = (int)(intptr_t)g_audioBuf;
    }
    return g_audioLen;
}

static INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            HWND hCombo = GetDlgItem(hwnd, IDC_LANG_COMBO);
            for (int i = 0; i < g_numLanguages; i++) {
                int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)g_languages[i].displayName);
                if (wcscmp(g_languages[i].dllName, g_selectedDllName) == 0)
                    SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)idx, 0);
            }
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    HWND hCombo = GetDlgItem(hwnd, IDC_LANG_COMBO);
                    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < g_numLanguages) {
                        wcscpy_s(g_selectedDllName, g_languages[sel].dllName);
                        SaveConfig(g_selectedDllName);
                    }
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

void WINAPI OpenSettings() {
    WCHAR oldLang[64];
    wcscpy_s(oldLang, g_selectedDllName);

    DialogBoxParamW(g_hModule, MAKEINTRESOURCEW(IDD_SETTINGS), NULL, SettingsDlgProc, 0);

    if (wcscmp(oldLang, g_selectedDllName) != 0) {
        StopEngine();
        EnsureInitialized();
        RestoreVoiceParams();
    }
}

void WINAPI UnInitial() {
    EnterCriticalSection(&g_cs);
    StopEngine();
    LeaveCriticalSection(&g_cs);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_cs);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_hWorkerThread) {
            PostThreadMessageW(GetThreadId(g_hWorkerThread), WM_QUIT, 0, 0);
            WaitForSingleObject(g_hWorkerThread, 3000);
            CloseHandle(g_hWorkerThread);
            g_hWorkerThread = NULL;
        }
    }
    return TRUE;
}
