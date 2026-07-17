#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmsystem.h>
#include <vector>
#include <queue>

#pragma comment(lib, "winmm.lib")

class WavePlayer {
public:
    WavePlayer(UINT channels = 1, DWORD sampleRate = 11025, WORD bitsPerSample = 16);
    ~WavePlayer();

    bool isOpen() const { return m_hwo != NULL; }
    bool isIdle();
    void feed(const BYTE* data, DWORD size);
    void stop();
    void pause(bool doPause);
    void close();
    bool selectDevice(UINT deviceId);

private:
    static void CALLBACK waveOutProcStatic(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
    void CALLBACK waveOutProc(UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

    HWAVEOUT m_hwo;
    WAVEFORMATEX m_wfx;
    CRITICAL_SECTION m_cs;
    UINT m_deviceId;

    std::queue<std::vector<BYTE>> m_pending;
    std::vector<BYTE> m_currentBuffer;
    WAVEHDR m_currentHdr;
    bool m_playing;

    void playNext();
    bool ensureOpen();
};
