#ifndef KCE_CHO_PLAYER_AUDIO_PLAYER_H_
#define KCE_CHO_PLAYER_AUDIO_PLAYER_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/** BASS 音频播放器封装（所有 BASS 调用都在独立工作线程中执行）
 */
class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator = (const AudioPlayer&) = delete;

public:
    /** 打开音频文件，autoPlay 为 true 时打开后立即播放 */
    void Open(const std::wstring& filePath, bool autoPlay);

    /** 开始/继续播放 */
    void Play();

    /** 暂停播放 */
    void Pause();

    /** 停止播放 */
    void Stop();

    /** 跳转到指定秒数 */
    void SeekTo(double seconds);

    /** 相对当前进度跳转（正数快进，负数快退），单位秒 */
    void SeekBy(double deltaSeconds);

public:
    bool HasStream() const { return m_hasStream.load(); }
    bool IsPlaying() const { return m_playing.load(); }
    bool IsBassInitialized() const { return m_bassInit.load(); }
    double GetDuration() const { return m_duration.load(); }
    double GetPosition() const { return m_position.load(); }

    /** 获取当前音频的 FFT 频谱数据（1024 个频段，幅度值 0~1）
    *   工作线程中定时计算，UI线程通过该接口安全获取快照
    */
    void GetSpectrum(std::vector<float>& spectrum) const;

    /** 取出并清除"播放结束"标志 */
    bool ConsumeEnded() { return m_ended.exchange(false); }

private:
    enum class CmdType
    {
        kOpen,
        kPlay,
        kPause,
        kStop,
        kSeek
    };

    struct Command
    {
        CmdType type = CmdType::kPause;
        std::wstring path;
        double value = 0.0;
        bool autoPlay = false;
    };

    void Push(Command cmd);
    void WorkerProc();
    void HandleCommand(const Command& cmd);
    void UpdateState();
    void UpdateSpectrum();

private:
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Command> m_commands;
    bool m_quit = false;

    std::atomic<bool> m_bassInit{ false };
    std::atomic<bool> m_hasStream{ false };
    std::atomic<bool> m_playing{ false };
    std::atomic<bool> m_ended{ false };
    std::atomic<double> m_duration{ 0.0 };
    std::atomic<double> m_position{ 0.0 };

    mutable std::mutex m_spectrumMutex;
    std::vector<float> m_spectrum;

    // 以下成员仅在工作线程中访问
    std::uint32_t m_stream = 0;
    bool m_isMusic = false;
};

#endif // KCE_CHO_PLAYER_AUDIO_PLAYER_H_
