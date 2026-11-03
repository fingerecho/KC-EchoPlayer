#include "AudioPlayer.h"

#include "BASS/bass.h"
#include "logUtils.h"

#include <algorithm>
#include <chrono>
#include <cwctype>

namespace
{
std::mutex g_bassMutex;
int g_bassRefCount = 0;
bool g_bassInitialized = false;

bool IsModuleFile(const std::wstring& filePath)
{
    const size_t pos = filePath.find_last_of(L'.');
    if (pos == std::wstring::npos) {
        return false;
    }
    std::wstring ext = filePath.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

    static const wchar_t* kModuleExts[] = {
        L"xm", L"it", L"s3m", L"mod", L"mtm", L"umx", L"mo3"
    };
    for (const wchar_t* moduleExt : kModuleExts) {
        if (ext == moduleExt) {
            return true;
        }
    }
    return false;
}

std::wstring BassErrorText(int errorCode)
{
    return L"BASS error code: " + std::to_wstring(errorCode);
}
} // namespace

AudioPlayer::AudioPlayer()
{
    m_thread = std::thread(&AudioPlayer::WorkerProc, this);
}

AudioPlayer::~AudioPlayer()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AudioPlayer::Open(const std::wstring& filePath, bool autoPlay)
{
    Command cmd;
    cmd.type = CmdType::kOpen;
    cmd.path = filePath;
    cmd.autoPlay = autoPlay;
    Push(std::move(cmd));
}

void AudioPlayer::Play()
{
    Command cmd;
    cmd.type = CmdType::kPlay;
    Push(std::move(cmd));
}

void AudioPlayer::Pause()
{
    Command cmd;
    cmd.type = CmdType::kPause;
    Push(std::move(cmd));
}

void AudioPlayer::Stop()
{
    Command cmd;
    cmd.type = CmdType::kStop;
    Push(std::move(cmd));
}

void AudioPlayer::SeekTo(double seconds)
{
    Command cmd;
    cmd.type = CmdType::kSeek;
    cmd.value = seconds;
    Push(std::move(cmd));
}

void AudioPlayer::SeekBy(double deltaSeconds)
{
    SeekTo(m_position.load() + deltaSeconds);
}

void AudioPlayer::Push(Command cmd)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_quit) {
            return;
        }
        m_commands.push_back(std::move(cmd));
    }
    m_cv.notify_one();
}

void AudioPlayer::WorkerProc()
{
    {
        std::lock_guard<std::mutex> lock(g_bassMutex);
        if (g_bassRefCount == 0) {
            if (!BASS_Init(-1, 44100, 0, nullptr, nullptr)) {
                WriteLog(L"BASS_Init failed: " + BassErrorText(BASS_ErrorGetCode()));
                g_bassInitialized = false;
            }
            else {
                g_bassInitialized = true;
            }
        }
        if (g_bassInitialized) {
            ++g_bassRefCount;
            m_bassInit.store(true);
        }
        else {
            m_bassInit.store(false);
        }
    }

    for (;;) {
        std::deque<Command> commands;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                return m_quit || !m_commands.empty();
            });
            if (m_quit) {
                break;
            }
            commands.swap(m_commands);
        }

        for (const Command& cmd : commands) {
            HandleCommand(cmd);
        }
        UpdateState();
        UpdateSpectrum();
    }

    if (m_stream != 0) {
        if (m_isMusic) {
            BASS_MusicFree(m_stream);
        }
        else {
            BASS_StreamFree(m_stream);
        }
        m_stream = 0;
        m_isMusic = false;
    }
    {
        std::lock_guard<std::mutex> lock(g_bassMutex);
        if (m_bassInit.load()) {
            if (g_bassRefCount > 0) {
                --g_bassRefCount;
            }
            if (g_bassRefCount == 0 && g_bassInitialized) {
                BASS_Free();
                g_bassInitialized = false;
            }
        }
    }
    m_bassInit.store(false);
    m_hasStream.store(false);
    m_playing.store(false);
}

void AudioPlayer::HandleCommand(const Command& cmd)
{
    if (cmd.type == CmdType::kOpen) {
        if (m_stream != 0) {
            if (m_isMusic) {
                BASS_MusicFree(m_stream);
            }
            else {
                BASS_StreamFree(m_stream);
            }
            m_stream = 0;
            m_isMusic = false;
        }
        m_hasStream.store(false);
        m_playing.store(false);
        m_ended.store(false);
        m_duration.store(0.0);
        m_position.store(0.0);

        if (!m_bassInit.load()) {
            WriteLog(L"BASS is not initialized, cannot open: " + cmd.path);
            return;
        }

        DWORD handle = 0;
        if (IsModuleFile(cmd.path)) {
            handle = BASS_MusicLoad(FALSE, cmd.path.c_str(), 0, 0, BASS_MUSIC_PRESCAN, 0);
            m_isMusic = (handle != 0);
        }
        else {
            handle = BASS_StreamCreateFile(FALSE, cmd.path.c_str(), 0, 0, 0);
            m_isMusic = false;
        }
        if (handle == 0) {
            WriteLog(L"BASS open failed: " + cmd.path + L", " + BassErrorText(BASS_ErrorGetCode()));
            return;
        }

        m_stream = handle;
        m_hasStream.store(true);

        const QWORD length = BASS_ChannelGetLength(handle, BASS_POS_BYTE);
        if (length != static_cast<QWORD>(-1)) {
            const double duration = BASS_ChannelBytes2Seconds(handle, length);
            if (duration > 0.0) {
                m_duration.store(duration);
            }
        }

        if (cmd.autoPlay) {
            BASS_ChannelPlay(handle, TRUE);
        }
    }
    else if (cmd.type == CmdType::kPlay) {
        if (m_stream == 0) {
            return;
        }
        const DWORD active = BASS_ChannelIsActive(m_stream);
        if (active == BASS_ACTIVE_PAUSED) {
            BASS_ChannelPlay(m_stream, FALSE);
        }
        else if (active != BASS_ACTIVE_PLAYING && active != BASS_ACTIVE_STALLED) {
            BASS_ChannelSetPosition(m_stream, 0, BASS_POS_BYTE);
            m_position.store(0.0);
            BASS_ChannelPlay(m_stream, FALSE);
        }
        m_ended.store(false);
    }
    else if (cmd.type == CmdType::kPause) {
        if (m_stream != 0) {
            BASS_ChannelPause(m_stream);
        }
    }
    else if (cmd.type == CmdType::kStop) {
        if (m_stream != 0) {
            BASS_ChannelStop(m_stream);
            BASS_ChannelSetPosition(m_stream, 0, BASS_POS_BYTE);
        }
        m_ended.store(false);
        m_position.store(0.0);
    }
    else if (cmd.type == CmdType::kSeek) {
        if (m_stream == 0) {
            return;
        }
        double distance = cmd.value;
        const double duration = m_duration.load();
        if (distance < 0.0) {
            distance = 0.0;
        }
        if (duration > 0.0 && distance > duration) {
            distance = duration;
        }
        const QWORD pos = BASS_ChannelSeconds2Bytes(m_stream, distance);
        if (pos != static_cast<QWORD>(-1)) {
            BASS_ChannelSetPosition(m_stream, pos, BASS_POS_BYTE);
            m_position.store(distance);
        }
        m_ended.store(false);
    }
}

void AudioPlayer::UpdateState()
{
    if (m_stream == 0) {
        m_playing.store(false);
        return;
    }

    const QWORD bytePos = BASS_ChannelGetPosition(m_stream, BASS_POS_BYTE);
    if (bytePos != static_cast<QWORD>(-1)) {
        const double pos = BASS_ChannelBytes2Seconds(m_stream, bytePos);
        if (pos >= 0.0) {
            m_position.store(pos);
        }
    }

    const DWORD active = BASS_ChannelIsActive(m_stream);
    const bool playing = (active == BASS_ACTIVE_PLAYING) || (active == BASS_ACTIVE_STALLED);
    if (m_playing.load() && (active == BASS_ACTIVE_STOPPED)) {
        m_ended.store(true);
    }
    m_playing.store(playing);
}

void AudioPlayer::GetSpectrum(std::vector<float>& spectrum) const
{
    std::lock_guard<std::mutex> lock(m_spectrumMutex);
    spectrum = m_spectrum;
}

void AudioPlayer::UpdateSpectrum()
{
    if ((m_stream == 0) || !m_playing.load()) {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_spectrum.clear();
        return;
    }

    // BASS_DATA_FFT2048 返回 1024 个频段的幅度值（BASS 内部已做 Hann 加窗）
    float fft[1024] = {};
    BASS_ChannelGetData(m_stream, fft, BASS_DATA_FFT2048 | BASS_DATA_FFT_REMOVEDC);

    std::lock_guard<std::mutex> lock(m_spectrumMutex);
    m_spectrum.assign(fft, fft + 1024);
}
