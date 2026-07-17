#include "wave_player.h"

WavePlayer::WavePlayer(UINT channels, DWORD sampleRate, WORD bitsPerSample)
    : m_hwo(NULL)
    , m_playing(false)
    , m_deviceId(0)
{
    InitializeCriticalSection(&m_cs);

    m_wfx.wFormatTag = WAVE_FORMAT_PCM;
    m_wfx.nChannels = channels;
    m_wfx.nSamplesPerSec = sampleRate;
    m_wfx.wBitsPerSample = bitsPerSample;
    m_wfx.nBlockAlign = (channels * bitsPerSample) / 8;
    m_wfx.nAvgBytesPerSec = sampleRate * m_wfx.nBlockAlign;
    m_wfx.cbSize = 0;

    ZeroMemory(&m_currentHdr, sizeof(m_currentHdr));
}

bool WavePlayer::ensureOpen() {
    if (m_hwo) return true;
    MMRESULT res = waveOutOpen(
        &m_hwo,
        m_deviceId,
        &m_wfx,
        (DWORD_PTR)&waveOutProcStatic,
        (DWORD_PTR)this,
        CALLBACK_FUNCTION
    );
    return (res == MMSYSERR_NOERROR);
}

WavePlayer::~WavePlayer() {
    close();
    DeleteCriticalSection(&m_cs);
}

void CALLBACK WavePlayer::waveOutProcStatic(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (dwInstance) {
        ((WavePlayer*)dwInstance)->waveOutProc(uMsg, dwParam1, dwParam2);
    }
}

void CALLBACK WavePlayer::waveOutProc(UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE) {
        EnterCriticalSection(&m_cs);
        if (m_currentHdr.lpData && m_hwo) {
            waveOutUnprepareHeader(m_hwo, &m_currentHdr, sizeof(m_currentHdr));
            ZeroMemory(&m_currentHdr, sizeof(m_currentHdr));
        }
        LeaveCriticalSection(&m_cs);
        playNext();
    }
}

bool WavePlayer::isIdle() {
    EnterCriticalSection(&m_cs);
    bool idle = !m_playing && m_pending.empty();
    LeaveCriticalSection(&m_cs);
    return idle;
}

void WavePlayer::feed(const BYTE* data, DWORD size) {
    if (size == 0) return;
    if (!ensureOpen()) return;

    EnterCriticalSection(&m_cs);
    m_pending.push(std::vector<BYTE>(data, data + size));
    bool shouldStart = !m_playing;
    LeaveCriticalSection(&m_cs);

    if (shouldStart) {
        playNext();
    }
}

void WavePlayer::playNext() {
    EnterCriticalSection(&m_cs);
    if (m_pending.empty()) {
        m_playing = false;
        LeaveCriticalSection(&m_cs);
        return;
    }

    m_currentBuffer = std::move(m_pending.front());
    m_pending.pop();
    m_playing = true;
    LeaveCriticalSection(&m_cs);

    ZeroMemory(&m_currentHdr, sizeof(m_currentHdr));
    m_currentHdr.lpData = (LPSTR)m_currentBuffer.data();
    m_currentHdr.dwBufferLength = (DWORD)m_currentBuffer.size();
    m_currentHdr.dwFlags = 0;

    MMRESULT res = waveOutPrepareHeader(m_hwo, &m_currentHdr, sizeof(m_currentHdr));
    if (res != MMSYSERR_NOERROR) {
        m_playing = false;
        return;
    }

    res = waveOutWrite(m_hwo, &m_currentHdr, sizeof(m_currentHdr));
    if (res != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(m_hwo, &m_currentHdr, sizeof(m_currentHdr));
        m_playing = false;
    }
}

void WavePlayer::stop() {
    EnterCriticalSection(&m_cs);
    while (!m_pending.empty()) m_pending.pop();
    m_playing = false;
    LeaveCriticalSection(&m_cs);

    if (m_hwo) {
        // Close and reopen to avoid waveOutReset deadlock when called from
        // a window message handler (WM_TTS_STOP). waveOutReset can block
        // waiting for WOM_DONE processing on the wave system thread.
        // Save handle, clear m_hwo so callback won't dereference it,
        // then reset and close the old handle. feed() will reopen via ensureOpen().
        HWAVEOUT h = m_hwo;
        m_hwo = NULL;
        waveOutReset(h);
        waveOutClose(h);
    }
}

void WavePlayer::pause(bool doPause) {
    if (m_hwo) {
        if (doPause)
            waveOutPause(m_hwo);
        else
            waveOutRestart(m_hwo);
    }
}

bool WavePlayer::selectDevice(UINT deviceId) {
    UINT numDevs = waveOutGetNumDevs();
    if (deviceId >= numDevs) return false;
    if (deviceId == m_deviceId) return true;
    close();
    m_deviceId = deviceId;
    return true;
}

void WavePlayer::close() {
    stop();
    if (m_hwo) {
        waveOutClose(m_hwo);
        m_hwo = NULL;
    }
}
