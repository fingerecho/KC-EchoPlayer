#ifndef EXAMPLES_MAIN_FORM_H_
#define EXAMPLES_MAIN_FORM_H_

// duilib
#include "duilib/duilib.h"

#include "AudioPlayer.h"
#include "SystemVolume.h"

#include <string>
#include <vector>

class MainForm : public ui::WindowImplBase
{
    typedef ui::WindowImplBase BaseClass;
public:
    MainForm();
    virtual ~MainForm() override;

    /** 资源相关接口
     * GetSkinFolder 接口设置你要绘制的窗口皮肤资源路径
     * GetSkinFile 接口设置你要绘制的窗口的 xml 描述文件
     */
    virtual DString GetSkinFolder() override;
    virtual DString GetSkinFile() override;

    /** 当窗口创建完成以后调用此函数，供子类中做一些初始化的工作
    */
    virtual void OnInitWindow() override;

    /** 主题切换时刷新媒体区背景图
    */
    virtual bool OnThemeChanged() override;

private:
    struct PlaylistUi
    {
        ui::ListBoxItem* item = nullptr;
        ui::Label* number = nullptr;
        ui::Label* name = nullptr;
    };

    void OnOpenFile();
    void AddPlaylistItem(const std::wstring& filePath, size_t index);
    void PlayIndex(size_t index);
    void PlayCurrent();
    void PauseCurrent();
    void PlayNext();
    void PlayPrevious();
    void SeekRelative(double seconds);
    void UpdateMuteButton();
    void UpdateNowPlayingText();
    void UpdateMediaBackground();
    void UpdateVisualization();
    void UpdatePlaylistHighlight();
    void OnTimer();

    static std::wstring FormatTime(double seconds);
    static std::wstring FileNameOf(const std::wstring& filePath);

private:
    ui::VListBox* m_playlistBox = nullptr;
    ui::Button* m_btnOpen = nullptr;
    ui::Button* m_btnPlay = nullptr;
    ui::Button* m_btnPause = nullptr;
    ui::Button* m_btnPrev = nullptr;
    ui::Button* m_btnNext = nullptr;
    ui::Button* m_btnRewind = nullptr;
    ui::Button* m_btnForward = nullptr;
    ui::Button* m_btnMute = nullptr;
    ui::Slider* m_sliderProgress = nullptr;
    ui::Slider* m_sliderVolume = nullptr;
    ui::Label* m_lblCurrentTime = nullptr;
    ui::Label* m_lblTotalTime = nullptr;
    ui::Label* m_lblNowPlaying = nullptr;
    ui::Label* m_pannel_lbl_now_playing = nullptr;
    ui::Label* m_media_lbl_now_playing = nullptr;
    ui::Control* m_mediaDisplay = nullptr;
    ui::BitmapControl* m_visual = nullptr;

    std::vector<std::wstring> m_playlist;
    std::vector<PlaylistUi> m_playlistUi;
    int m_currentIndex = -1;

    AudioPlayer m_player;
    SystemVolume m_systemVolume;

    size_t m_timerTaskId = 0;
    bool m_seeking = false;
    bool m_lastMuteState = false;
    ULONG_PTR m_gdiplusToken = 0;
};

#endif //EXAMPLES_MAIN_FORM_H_
