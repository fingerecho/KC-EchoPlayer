#ifndef EXAMPLES_MAIN_FORM_H_
#define EXAMPLES_MAIN_FORM_H_

// duilib
#include "duilib/duilib.h"

#include "AudioPlayer.h"
#include "SystemVolume.h"
#include "Resource.h"
#include "DatabaseMgr.h"

#include <cstdint>
#include <string>
#include <vector>

class MainForm : public ui::WindowImplBase
{
    typedef ui::WindowImplBase BaseClass;
public:
    enum class Version
    {
        kDefault = 0,
        kNetEaseV2 = 1
    };

    explicit MainForm(Version version = Version::kDefault);
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

    struct LyricLine
    {
        int64_t timeMs = 0;
        std::wstring text;
    };

    struct SongInfo
    {
        int64_t id = 0;
        std::wstring title;
        std::wstring artist;
        std::wstring album;
        std::wstring filePath;
        std::wstring coverPath;
        int64_t durationMs = 0;
        int64_t fileSize = 0;
    };

    struct RecentSongInfo : SongInfo
    {
        int64_t lastPlayedAt = 0;
        int64_t playCount = 0;
        int64_t lastPositionMs = 0;
        bool favorite = false;
    };

    struct FavoriteSongInfo : SongInfo
    {
        int64_t addedAt = 0;
        int64_t sortOrder = 0;
        int64_t playCount = 0;
        std::wstring note;
    };

    struct LocalRowUi
    {
        ui::Button* index = nullptr;
        ui::Label* title = nullptr;
        ui::Label* artist = nullptr;
        ui::Label* album = nullptr;
        ui::Label* duration = nullptr;
        ui::Label* size = nullptr;
    };

    struct RecentRowUi
    {
        ui::Button* index = nullptr;
        ui::Label* title = nullptr;
        ui::Label* artist = nullptr;
        ui::Label* album = nullptr;
        ui::Button* favorite = nullptr;
        ui::Label* playedTime = nullptr;
    };

    struct FavoriteRowUi
    {
        ui::Button* index = nullptr;
        ui::Label* title = nullptr;
        ui::Label* artist = nullptr;
        ui::Label* album = nullptr;
        ui::Label* addedTime = nullptr;
        ui::Label* playCount = nullptr;
        ui::Button* remove = nullptr;
    };

    void OnOpenFile();
    void OnSelectVersionMenu();
    void OnVersionSelected(size_t index);
    void SwitchVersion(Version version);
    void InitNetEaseUi();
    void InitDatabase();
    void LoadLocalSongs();
    void LoadRecentSongs();
    void LoadFavoriteSongs();
    void RefreshLocalList();
    void RefreshRecentList();
    void RefreshFavoriteList();
    void RefreshAllNetEaseLists();
    void UpdateNetEaseLocalCount();
    void UpdateNetEaseFavoriteCount();
    void ChooseMusicSource();
    void ImportMusicFiles(std::vector<std::wstring> files);
    void ImportMusicFolder(const std::wstring& folder);
    bool AddSongToDatabase(const std::wstring& filePath, SongInfo& song);
    void PlaySong(const SongInfo& song);
    void PlayLocalRow(size_t index);
    void PlayRecentRow(size_t index);
    void PlayFavoriteRow(size_t index);
    void PlayNetEaseNext();
    void PlayNetEasePrevious();
    void AddFavoriteBySongId(int64_t songId);
    void RemoveFavoriteBySongId(int64_t songId);
    void RecordRecentPlay(const SongInfo& song);
    void UpdateNetEaseRowStyles();
    void UpdateNetEaseBottomInfo();
    void LoadLyricsForSong(const std::wstring& filePath);
    void UpdateLyricsPanel(double positionSeconds);
    int FindLocalIndexBySongId(int64_t songId) const;
    int FindRecentIndexBySongId(int64_t songId) const;
    int FindFavoriteIndexBySongId(int64_t songId) const;
    void ShowHint(const std::wstring& text);
    void LoadAppSettings();
    void SetAutoStart(bool enabled);
    bool IsAutoStartEnabled() const;
    void SetAutoPlayOnStartup(bool enabled);
    bool IsAutoPlayOnStartup();
    void ShowNetEasePage(ui::Control* page);
    void ShowNetEasePlayer(bool show);
    void ToggleNetEasePlayback();
    void UpdateNetEasePlayButton();
    void InitNetEaseProgressBar();
    void UpdateNetEaseListIndices();
    void SetNetEaseProgressHovered(bool hovered);
    void UpdateNetEaseProgressTip(double ratio);
    void UpdateNetEaseProgressFromMouse(const ui::UiPoint& ptMouse);
    void SeekNetEaseProgress(double ratio);
    double GetNetEaseDuration() const;
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
    Version m_version = Version::kDefault;
    ui::Button* m_btnSelectVersion = nullptr;

    bool m_isNetEase = false;
    DatabaseMgr m_database;
    ui::VListBox* m_neLocalList = nullptr;
    ui::VListBox* m_neRecentList = nullptr;
    ui::VListBox* m_neFavoriteList = nullptr;
    ui::Label* m_neLocalCount = nullptr;
    ui::Label* m_neFavoriteCount = nullptr;
    ui::Button* m_neChooseDir = nullptr;
    ui::Button* m_neRecentSingleTab = nullptr;
    ui::Label* m_neBottomTitle = nullptr;
    ui::Label* m_neBottomArtist = nullptr;
    ui::Label* m_nePlayerBottomTitle = nullptr;
    ui::Label* m_nePlayerBottomArtist = nullptr;
    ui::Label* m_nePlayerPageTitle = nullptr;
    ui::Label* m_nePlayerPageMeta = nullptr;
    std::vector<ui::Label*> m_lyricLabels;
    std::vector<LyricLine> m_lyrics;
    int m_currentLyricIndex = -1;
    ui::CheckBox* m_neCheckAutoStart = nullptr;
    ui::CheckBox* m_neCheckAutoPlay = nullptr;

    std::vector<SongInfo> m_localSongs;
    std::vector<RecentSongInfo> m_recentSongs;
    std::vector<FavoriteSongInfo> m_favoriteSongs;
    std::vector<LocalRowUi> m_neLocalRowUi;
    std::vector<RecentRowUi> m_neRecentRowUi;
    std::vector<FavoriteRowUi> m_neFavoriteRowUi;
    std::vector<bool> m_neFavoriteRowHovered;
    std::vector<bool> m_neFavoriteIconHovered;
    int m_neFavoriteActiveRow = -1;
    SongInfo m_currentSong;
    bool m_hasCurrentSong = false;
    ui::Control* m_neBodyNormal = nullptr;
    ui::Control* m_neBodyPlayer = nullptr;
    ui::Control* m_nePageLocal = nullptr;
    ui::Control* m_nePageRecent = nullptr;
    ui::Control* m_nePageFavorite = nullptr;
    ui::Control* m_nePageSettings = nullptr;
    ui::Option* m_neNavLocal = nullptr;
    ui::Button* m_neBtnShowPlayer = nullptr;
    ui::Button* m_neBtnPlayerBack = nullptr;
    ui::Button* m_neBtnPlayerBackTop = nullptr;
    ui::Button* m_nePlayPauseNormal = nullptr;
    ui::Button* m_nePlayPausePlayer = nullptr;
    ui::Slider* m_neProgress = nullptr;
    ui::Label* m_neProgressTip = nullptr;
    bool m_neSeeking = false;
    bool m_neProgressHovered = false;
    int m_neLocalActiveRow = 0;
    int m_neRecentActiveRow = -1;
    std::vector<ui::Control*> m_neLocalRows;
    std::vector<ui::Button*> m_neLocalIndexButtons;
    std::vector<bool> m_neLocalRowHovered;
    std::vector<bool> m_neLocalIconHovered;
    std::vector<ui::Control*> m_neRecentRows;
    std::vector<ui::Button*> m_neRecentIndexButtons;
    std::vector<bool> m_neRecentRowHovered;
    std::vector<bool> m_neRecentIconHovered;
    std::vector<ui::Control*> m_neFavoriteRows;
    std::vector<ui::Button*> m_neFavoriteIndexButtons;

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
