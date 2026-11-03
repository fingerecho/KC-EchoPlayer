#include "MainForm.h"

#include "logUtils.h"

#include <commdlg.h>
#include <gdiplus.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::vector<std::wstring> ShowOpenAudioDialog(HWND owner)
{
    static const wchar_t kFilter[] =
        L"音频文件\0*.mp3;*.mp2;*.mp1;*.ogg;*.wav;*.aiff;*.aif;*.xm;*.it;*.s3m;*.mod;*.mtm;*.umx;*.mo3\0"
        L"所有文件\0*.*\0\0";

    std::vector<wchar_t> buffer(64 * 1024, 0);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = L"打开音频文件";
    ofn.lpstrDefExt = L"mp3";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER |
                OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!::GetOpenFileNameW(&ofn)) {
        return {};
    }

    const wchar_t* ptr = buffer.data();
    const std::wstring first = ptr;
    ptr += first.size() + 1;

    std::vector<std::wstring> files;
    if (*ptr == L'\0') {
        // 单选：buffer 中就是完整路径
        files.push_back(first);
        return files;
    }

    // 多选：第一项是目录，后面依次是文件名
    while (*ptr != L'\0') {
        const std::wstring name = ptr;
        ptr += name.size() + 1;
        files.push_back(first + L"\\" + name);
    }
    return files;
}

bool IsSupportedAudioFile(const std::filesystem::path& path)
{
    std::wstring ext = path.extension().wstring();
    if (ext.empty()) {
        return false;
    }
    if (ext[0] == L'.') {
        ext.erase(ext.begin());
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    static const wchar_t* kExtensions[] = {
        L"mp3", L"mp2", L"mp1", L"ogg", L"wav", L"aiff", L"aif",
        L"xm", L"it", L"s3m", L"mod", L"mtm", L"umx", L"mo3"
    };
    for (const wchar_t* value : kExtensions) {
        if (ext == value) {
            return true;
        }
    }
    return false;
}

std::wstring ShowSelectFolderDialog(HWND owner, const wchar_t* title)
{
    BROWSEINFOW browseInfo = {};
    browseInfo.hwndOwner = owner;
    browseInfo.lpszTitle = title;
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST itemIdList = ::SHBrowseForFolderW(&browseInfo);
    if (itemIdList == nullptr) {
        return std::wstring();
    }
    wchar_t folderPath[MAX_PATH] = { 0 };
    const bool ok = ::SHGetPathFromIDListW(itemIdList, folderPath) != FALSE;
    ::CoTaskMemFree(itemIdList);
    return ok ? std::wstring(folderPath) : std::wstring();
}

std::vector<std::wstring> ScanAudioFiles(const std::wstring& folder)
{
    std::vector<std::wstring> files;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(folder, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        return files;
    }
    while (iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code entryError;
        if (entry.is_regular_file(entryError) && IsSupportedAudioFile(entry.path())) {
            files.push_back(entry.path().lexically_normal().wstring());
        }
        iterator.increment(error);
        if (error) {
            error.clear();
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

int64_t NowEpochSeconds()
{
    return static_cast<int64_t>(std::time(nullptr));
}

std::wstring FormatDateTime(int64_t epochSeconds)
{
    if (epochSeconds <= 0) {
        return L"-";
    }
    const std::time_t timeValue = static_cast<std::time_t>(epochSeconds);
    std::tm localTime = {};
    localtime_s(&localTime, &timeValue);
    wchar_t buffer[64] = { 0 };
    swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d",
               localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
               localTime.tm_hour, localTime.tm_min);
    return buffer;
}

std::wstring FormatRelativeTime(int64_t epochSeconds)
{
    if (epochSeconds <= 0) {
        return L"-";
    }
    const int64_t now = NowEpochSeconds();
    const int64_t diff = now - epochSeconds;
    if (diff < 60) {
        return L"刚刚";
    }
    if (diff < 3600) {
        return std::to_wstring(diff / 60) + L"分钟前";
    }
    if (diff < 86400) {
        return std::to_wstring(diff / 3600) + L"小时前";
    }
    if (diff < 7 * 86400) {
        return std::to_wstring(diff / 86400) + L"天前";
    }
    return FormatDateTime(epochSeconds);
}

std::wstring FormatFileSize(int64_t bytes)
{
    if (bytes <= 0) {
        return L"-";
    }
    wchar_t buffer[64] = { 0 };
    if (bytes >= 1024 * 1024) {
        swprintf_s(buffer, L"%.1fM", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024) {
        swprintf_s(buffer, L"%.1fK", static_cast<double>(bytes) / 1024.0);
    }
    else {
        swprintf_s(buffer, L"%lldB", static_cast<long long>(bytes));
    }
    return buffer;
}

std::wstring NonEmptyText(const std::wstring& text, const wchar_t* fallback)
{
    return text.empty() ? std::wstring(fallback) : text;
}

std::wstring TrimWideText(const std::wstring& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin]) != 0) {
        ++begin;
    }
    while (end > begin && std::iswspace(text[end - 1]) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring ReadTextFileAutoEncoding(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::wstring();
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return std::wstring();
    }

    const unsigned char* data = reinterpret_cast<const unsigned char*>(bytes.data());
    size_t size = bytes.size();
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        return std::wstring(reinterpret_cast<const wchar_t*>(data + 2), (size - 2) / sizeof(wchar_t));
    }
    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        std::wstring result;
        result.reserve((size - 2) / sizeof(wchar_t));
        for (size_t i = 2; i + 1 < size; i += 2) {
            wchar_t ch = static_cast<wchar_t>((data[i] << 8) | data[i + 1]);
            result.push_back(ch);
        }
        return result;
    }

    size_t offset = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;
    }
    const char* text = reinterpret_cast<const char*>(data + offset);
    const int textSize = static_cast<int>(size - offset);
    int wideSize = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, textSize, nullptr, 0);
    UINT codePage = CP_UTF8;
    if (wideSize <= 0) {
        codePage = CP_ACP;
        wideSize = ::MultiByteToWideChar(codePage, 0, text, textSize, nullptr, 0);
    }
    if (wideSize <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(wideSize), L'\0');
    ::MultiByteToWideChar(codePage, 0, text, textSize, result.data(), wideSize);
    return result;
}
} // namespace

MainForm::MainForm(Version version) :
    m_version(version)
{
}

MainForm::~MainForm()
{
    if (m_timerTaskId != 0) {
        ui::GlobalManager::Instance().Thread().CancelTask(m_timerTaskId);
        m_timerTaskId = 0;
    }
    m_database.Close();
    if (m_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}

DString MainForm::GetSkinFolder()
{
    return _T("");
}

DString MainForm::GetSkinFile()
{
    if (m_version == Version::kNetEaseV2) {
        return _T("KCNetEaseVersion.xml");
    }
    return _T("KCEchoPlayerMain.xml");
}

void MainForm::OnInitWindow()
{
    BaseClass::OnInitWindow();
    InitDatabase();

    m_playlistBox = dynamic_cast<ui::VListBox*>(FindControl(_T("playlist")));
    m_btnOpen = dynamic_cast<ui::Button*>(FindControl(_T("btn_open")));
    m_btnPlay = dynamic_cast<ui::Button*>(FindControl(_T("btn_play")));
    m_btnPause = dynamic_cast<ui::Button*>(FindControl(_T("btn_pause")));
    m_btnPrev = dynamic_cast<ui::Button*>(FindControl(_T("btn_prev")));
    m_btnNext = dynamic_cast<ui::Button*>(FindControl(_T("btn_next")));
    m_btnRewind = dynamic_cast<ui::Button*>(FindControl(_T("btn_rewind")));
    m_btnForward = dynamic_cast<ui::Button*>(FindControl(_T("btn_forward")));
    m_btnMute = dynamic_cast<ui::Button*>(FindControl(_T("btn_mute")));
    m_sliderProgress = dynamic_cast<ui::Slider*>(FindControl(_T("slider_progress")));
    m_sliderVolume = dynamic_cast<ui::Slider*>(FindControl(_T("slider_volume")));
    m_lblCurrentTime = dynamic_cast<ui::Label*>(FindControl(_T("lbl_current_time")));
    m_lblTotalTime = dynamic_cast<ui::Label*>(FindControl(_T("lbl_total_time")));
    m_lblNowPlaying = dynamic_cast<ui::Label*>(FindControl(_T("lbl_now_playing")));
    m_pannel_lbl_now_playing = dynamic_cast<ui::Label*>(FindControl(_T("pannel_lbl_now_playing")));
    m_media_lbl_now_playing = dynamic_cast<ui::Label*>(FindControl(_T("media_lbl_now_playing")));
    m_mediaDisplay = FindControl(_T("media_display"));
    m_visual = dynamic_cast<ui::BitmapControl*>(FindControl(_T("visual_display")));

    m_btnSelectVersion = dynamic_cast<ui::Button*>(FindControl(_T("btn_select_version")));
    if (m_btnSelectVersion != nullptr) {
        m_btnSelectVersion->AttachClick([this](const ui::EventArgs& /*args*/) {
            OnSelectVersionMenu();
            return true;
            });
    }

    m_isNetEase = (m_version == Version::kNetEaseV2);
    if (m_isNetEase) {
        InitNetEaseUi();
    }

    const uint32_t labelTextStyle = ui::TEXT_LEFT | ui::TEXT_VCENTER | ui::TEXT_END_ELLIPSIS |
                                    ui::TEXT_NOCLIP | ui::TEXT_SINGLELINE;
    if (m_pannel_lbl_now_playing != nullptr) {
        m_pannel_lbl_now_playing->SetTextStyle(labelTextStyle, true);
    }
    if (m_media_lbl_now_playing != nullptr) {
        m_media_lbl_now_playing->SetTextStyle(labelTextStyle, true);
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        m_gdiplusToken = 0;
    }
    UpdateMediaBackground();

    // 清空 XML 中的示例项，后续只显示用户打开的文件
    if (m_playlistBox != nullptr) {
        m_playlistBox->RemoveAllItems();
        m_playlistBox->AttachSelect([this](const ui::EventArgs& args) {
            const int index = static_cast<int>(args.wParam);
            if ((index >= 0) && (index < static_cast<int>(m_playlist.size()))) {
                PlayIndex(static_cast<size_t>(index));
            }
            return true;
            });
    }

    if (m_btnOpen != nullptr) {
        m_btnOpen->AttachClick([this](const ui::EventArgs& /*args*/) {
            OnOpenFile();
            return true;
            });
    }

    if (m_btnPlay != nullptr) {
        m_btnPlay->AttachClick([this](const ui::EventArgs& /*args*/) {
            PlayCurrent();
            return true;
            });
    }
    if (m_btnPause != nullptr) {
        m_btnPause->AttachClick([this](const ui::EventArgs& /*args*/) {
            PauseCurrent();
            return true;
            });
    }
    if (m_btnPrev != nullptr) {
        m_btnPrev->AttachClick([this](const ui::EventArgs& /*args*/) {
            PlayPrevious();
            return true;
            });
    }
    if (m_btnNext != nullptr) {
        m_btnNext->AttachClick([this](const ui::EventArgs& /*args*/) {
            PlayNext();
            return true;
            });
    }
    if (m_btnRewind != nullptr) {
        m_btnRewind->AttachClick([this](const ui::EventArgs& /*args*/) {
            SeekRelative(-10.0);
            return true;
            });
    }
    if (m_btnForward != nullptr) {
        m_btnForward->AttachClick([this](const ui::EventArgs& /*args*/) {
            SeekRelative(10.0);
            return true;
            });
    }
    if (m_btnMute != nullptr) {
        m_btnMute->AttachClick([this](const ui::EventArgs& /*args*/) {
            m_systemVolume.SetMute(!m_systemVolume.GetMute());
            UpdateMuteButton();
            return true;
            });
    }

    if (m_sliderProgress != nullptr) {
        m_sliderProgress->SetMinValue(0);
        m_sliderProgress->SetMaxValue(1000);
        m_sliderProgress->AttachButtonDown([this](const ui::EventArgs& /*args*/) {
            m_seeking = true;
            return true;
            });
        m_sliderProgress->AttachButtonUp([this](const ui::EventArgs& /*args*/) {
            m_seeking = false;
            if (m_player.HasStream()) {
                const double duration = m_player.GetDuration();
                if (duration > 0.0) {
                    const double ratio = std::clamp(m_sliderProgress->GetValue() / 1000.0, 0.0, 1.0);
                    m_player.SeekTo(ratio * duration);
                }
            }
            return true;
            });
        m_sliderProgress->AttachValueChanged([this](const ui::EventArgs& /*args*/) {
            if (m_player.HasStream()) {
                const double duration = m_player.GetDuration();
                if (duration > 0.0) {
                    const double ratio = std::clamp(m_sliderProgress->GetValue() / 1000.0, 0.0, 1.0);
                    const double pos = ratio * duration;
                    m_player.SeekTo(pos);
                    if (m_lblCurrentTime != nullptr) {
                        m_lblCurrentTime->SetText(FormatTime(pos).c_str());
                    }
                }
            }
            return true;
            });
    }

    if (m_sliderVolume != nullptr) {
        m_sliderVolume->SetMinValue(0);
        m_sliderVolume->SetMaxValue(100);
        const float systemVolume = m_systemVolume.IsAvailable() ? m_systemVolume.GetVolume() : 0.5f;
        m_sliderVolume->SetValueNoEvent(static_cast<double>(systemVolume * 100.0f));
        m_sliderVolume->AttachValueChanged([this](const ui::EventArgs& /*args*/) {
            m_systemVolume.SetVolume(static_cast<float>(m_sliderVolume->GetValue() / 100.0));
            UpdateMuteButton();
            return true;
            });
    }

    UpdateMuteButton();
    UpdateNowPlayingText();
    UpdateVisualization();

    m_timerTaskId = ui::GlobalManager::Instance().Thread().PostRepeatedTask(
        ui::kThreadUI,
        ui::UiBind(this, [this]() {
            OnTimer();
        }),
        100);
}

void MainForm::OnOpenFile()
{
    const std::vector<std::wstring> files = ShowOpenAudioDialog(static_cast<HWND>(GetWindowHandle()));
    if (files.empty()) {
        return;
    }

    const size_t startIndex = m_playlist.size();
    for (const std::wstring& filePath : files) {
        m_playlist.push_back(filePath);
        AddPlaylistItem(filePath, m_playlist.size() - 1);
    }

    // 打开后自动播放本轮添加的第一首
    PlayIndex(startIndex);
}

void MainForm::OnVersionSelected(size_t index)
{
    Version version = Version::kDefault;
    if (index == 1) {
        version = Version::kNetEaseV2;
    }
    if (version == m_version) {
        return;
    }
    SwitchVersion(version);
}

void MainForm::SwitchVersion(Version version)
{
    ui::UiRect windowRect;
    GetWindowRect(windowRect);

    MainForm* newWindow = new MainForm(version);
    ui::WindowCreateParam createParam(_T("克铖播放器"));
    createParam.m_bCenterWindow = false;
    createParam.m_nX = windowRect.left;
    createParam.m_nY = windowRect.top;
    createParam.m_nWidth = windowRect.Width();
    createParam.m_nHeight = windowRect.Height();
    if (!newWindow->CreateWnd(nullptr, createParam)) {
        delete newWindow;
        return;
    }

    newWindow->PostQuitMsgWhenClosed(true);
    newWindow->ShowWindow(ui::kSW_SHOW_NORMAL);

    PostQuitMsgWhenClosed(false);
    CloseWnd();
}

void MainForm::OnSelectVersionMenu()
{
    if (m_btnSelectVersion == nullptr) {
        return;
    }

    ui::UiRect rect = m_btnSelectVersion->GetPos();
    ui::UiPoint point(rect.left, rect.bottom + 4);
    ClientToScreen(point);

    ui::Menu* menu = new ui::Menu(this, m_btnSelectVersion);
    menu->SetSkinFolder(_T("public/menu"));
    menu->AttachMenuItemActivated([this](const DString& /*menuName*/, int32_t /*nMenuLevel*/,
                                         const DString& itemName, size_t /*nItemIndex*/) {
        if (itemName == _T("version_default")) {
            OnVersionSelected(0);
        }
        else if (itemName == _T("version_netease_v2")) {
            OnVersionSelected(1);
        }
        });
    menu->ShowMenu(_T("version_menu.xml"), point, ui::MenuPopupPosType::LEFT_TOP);
}

void MainForm::InitNetEaseUi()
{
    m_neBodyNormal = FindControl(_T("ne_body_normal"));
    m_neBodyPlayer = FindControl(_T("ne_body_player"));
    m_nePageLocal = FindControl(_T("ne_page_local"));
    m_nePageRecent = FindControl(_T("ne_page_recent"));
    m_nePageFavorite = FindControl(_T("ne_page_favorite"));
    m_nePageSettings = FindControl(_T("ne_page_settings"));
    m_neNavLocal = dynamic_cast<ui::Option*>(FindControl(_T("nav_local")));

    auto bindNav = [this](const DString& name, ui::Control* page) {
        ui::Option* nav = dynamic_cast<ui::Option*>(FindControl(name));
        if (nav != nullptr) {
            nav->AttachSelect([this, page](const ui::EventArgs& /*args*/) {
                ShowNetEasePage(page);
                return true;
                });
        }
    };
    bindNav(_T("nav_local"), m_nePageLocal);
    bindNav(_T("nav_recent"), m_nePageRecent);
    bindNav(_T("nav_favorite"), m_nePageFavorite);
    bindNav(_T("nav_settings"), m_nePageSettings);

    m_neBtnShowPlayer = dynamic_cast<ui::Button*>(FindControl(_T("btn_show_player")));
    if (m_neBtnShowPlayer != nullptr) {
        m_neBtnShowPlayer->AttachClick([this](const ui::EventArgs& /*args*/) {
            ShowNetEasePlayer(true);
            return true;
            });
    }
    m_neBtnPlayerBack = dynamic_cast<ui::Button*>(FindControl(_T("btn_player_back")));
    if (m_neBtnPlayerBack != nullptr) {
        m_neBtnPlayerBack->AttachClick([this](const ui::EventArgs& /*args*/) {
            ShowNetEasePlayer(false);
            return true;
            });
    }
    m_neBtnPlayerBackTop = dynamic_cast<ui::Button*>(FindControl(_T("btn_player_back_top")));
    if (m_neBtnPlayerBackTop != nullptr) {
        m_neBtnPlayerBackTop->AttachClick([this](const ui::EventArgs& /*args*/) {
            ShowNetEasePlayer(false);
            return true;
            });
    }

    auto bindAction = [this](const DString& name, void (MainForm::*action)()) {
        ui::Button* button = dynamic_cast<ui::Button*>(FindControl(name));
        if (button != nullptr) {
            button->AttachClick([this, action](const ui::EventArgs& /*args*/) {
                (this->*action)();
                return true;
                });
        }
    };
    bindAction(_T("btn_ne_prev"), &MainForm::PlayPrevious);
    bindAction(_T("btn_ne_next"), &MainForm::PlayNext);
    bindAction(_T("btn_ne_prev_player"), &MainForm::PlayPrevious);
    bindAction(_T("btn_ne_next_player"), &MainForm::PlayNext);

    m_nePlayPauseNormal = dynamic_cast<ui::Button*>(FindControl(_T("btn_ne_play_pause")));
    m_nePlayPausePlayer = dynamic_cast<ui::Button*>(FindControl(_T("btn_ne_play_pause_player")));
    auto bindPlayPause = [this](ui::Button* button) {
        if (button != nullptr) {
            button->AttachClick([this](const ui::EventArgs& /*args*/) {
                ToggleNetEasePlayback();
                return true;
                });
        }
    };
    bindPlayPause(m_nePlayPauseNormal);
    bindPlayPause(m_nePlayPausePlayer);

    InitNetEaseProgressBar();

    m_neLocalList = dynamic_cast<ui::VListBox*>(FindControl(_T("ne_local_list")));
    m_neRecentList = dynamic_cast<ui::VListBox*>(FindControl(_T("ne_recent_list")));
    m_neFavoriteList = dynamic_cast<ui::VListBox*>(FindControl(_T("ne_favorite_list")));
    m_neLocalCount = dynamic_cast<ui::Label*>(FindControl(_T("lbl_local_count")));
    m_neFavoriteCount = dynamic_cast<ui::Label*>(FindControl(_T("lbl_favorite_count")));
    m_neChooseDir = dynamic_cast<ui::Button*>(FindControl(_T("btn_choose_dir")));
    m_neRecentSingleTab = dynamic_cast<ui::Button*>(FindControl(_T("btn_recent_single_tab")));
    m_neBottomTitle = dynamic_cast<ui::Label*>(FindControl(_T("ne_bottom_title")));
    m_neBottomArtist = dynamic_cast<ui::Label*>(FindControl(_T("ne_bottom_artist")));
    m_nePlayerBottomTitle = dynamic_cast<ui::Label*>(FindControl(_T("ne_player_bottom_title")));
    m_nePlayerBottomArtist = dynamic_cast<ui::Label*>(FindControl(_T("ne_player_bottom_artist")));
    m_neCheckAutoStart = dynamic_cast<ui::CheckBox*>(FindControl(_T("chk_auto_start")));
    m_neCheckAutoPlay = dynamic_cast<ui::CheckBox*>(FindControl(_T("chk_auto_play")));
    m_nePlayerPageTitle = dynamic_cast<ui::Label*>(FindControl(_T("ne_player_page_title")));
    m_nePlayerPageMeta = dynamic_cast<ui::Label*>(FindControl(_T("ne_player_page_meta")));

    m_lyricLabels.clear();
    for (int i = 0; i < 9; ++i) {
        wchar_t name[32] = { 0 };
        swprintf_s(name, L"ne_lyric_%d", i);
        m_lyricLabels.push_back(dynamic_cast<ui::Label*>(FindControl(name)));
    }

    if (m_neChooseDir != nullptr) {
        m_neChooseDir->AttachClick([this](const ui::EventArgs& /*args*/) {
            ChooseMusicSource();
            return true;
            });
    }
    if (m_neCheckAutoStart != nullptr) {
        m_neCheckAutoStart->AttachSelect([this](const ui::EventArgs& /*args*/) {
            if (m_neCheckAutoStart != nullptr) {
                SetAutoStart(m_neCheckAutoStart->IsSelected());
            }
            return true;
            });
    }
    if (m_neCheckAutoPlay != nullptr) {
        m_neCheckAutoPlay->AttachSelect([this](const ui::EventArgs& /*args*/) {
            if (m_neCheckAutoPlay != nullptr) {
                SetAutoPlayOnStartup(m_neCheckAutoPlay->IsSelected());
            }
            return true;
            });
    }

    LoadAppSettings();
    if (m_neCheckAutoStart != nullptr) {
        m_neCheckAutoStart->Selected(IsAutoStartEnabled(), false);
    }
    if (m_neCheckAutoPlay != nullptr) {
        m_neCheckAutoPlay->Selected(IsAutoPlayOnStartup(), false);
    }

    LoadLocalSongs();
    LoadRecentSongs();
    LoadFavoriteSongs();
    RefreshAllNetEaseLists();
    UpdateNetEaseBottomInfo();
    UpdateNetEasePlayButton();

    static bool startupAutoPlayHandled = false;
    if (!startupAutoPlayHandled && IsAutoPlayOnStartup() && !m_localSongs.empty()) {
        startupAutoPlayHandled = true;
        PlaySong(m_localSongs.front());
    }
}

void MainForm::ShowNetEasePage(ui::Control* page)
{
    if (!m_isNetEase) {
        return;
    }
    ui::Control* pages[] = { m_nePageLocal, m_nePageRecent, m_nePageFavorite, m_nePageSettings };
    for (ui::Control* item : pages) {
        if (item != nullptr) {
            item->SetVisible(item == page);
        }
    }
}

void MainForm::ShowNetEasePlayer(bool show)
{
    if (!m_isNetEase) {
        return;
    }
    if (m_neBodyNormal != nullptr) {
        m_neBodyNormal->SetVisible(!show);
    }
    if (m_neBodyPlayer != nullptr) {
        m_neBodyPlayer->SetVisible(show);
    }
    if (!show) {
        ShowNetEasePage(m_nePageLocal);
        if (m_neNavLocal != nullptr) {
            m_neNavLocal->Selected(true, false);
        }
    }
    UpdateNetEasePlayButton();
}

void MainForm::ToggleNetEasePlayback()
{
    if (m_player.IsPlaying()) {
        PauseCurrent();
    }
    else {
        PlayCurrent();
    }
    UpdateNetEasePlayButton();
}

void MainForm::UpdateNetEasePlayButton()
{
    const DString image = m_player.IsPlaying()
        ? _T("file='images/netease/pause_52.png' width='24' height='24' valign='center' halign='center'")
        : _T("file='images/netease/play_52.png' width='24' height='24' valign='center' halign='center'");

    auto updateButton = [&image](ui::Button* button) {
        if (button == nullptr) {
            return;
        }
        button->SetStateImage(ui::kControlStateNormal, image);
        button->SetStateImage(ui::kControlStateHovered, image);
        button->SetStateImage(ui::kControlStatePressed, image);
        button->Invalidate();
    };
    updateButton(m_nePlayPauseNormal);
    updateButton(m_nePlayPausePlayer);
}

void MainForm::InitNetEaseProgressBar()
{
    m_neProgress = dynamic_cast<ui::Slider*>(FindControl(_T("ne_progress")));
    m_neProgressTip = dynamic_cast<ui::Label*>(FindControl(_T("ne_progress_tip")));
    if (m_neProgress == nullptr) {
        return;
    }

    m_neProgress->SetMinValue(0);
    m_neProgress->SetMaxValue(1000);
    m_neProgress->SetThumbSize(ui::UiSize(0, 0), false);
    m_neProgress->SetValueNoEvent(47.0);

    m_neProgress->AttachMouseEnter([this](const ui::EventArgs& args) {
        m_neProgressHovered = true;
        SetNetEaseProgressHovered(true);
        UpdateNetEaseProgressFromMouse(args.ptMouse);
        return true;
        });
    m_neProgress->AttachMouseLeave([this](const ui::EventArgs& /*args*/) {
        m_neProgressHovered = false;
        if (!m_neSeeking) {
            SetNetEaseProgressHovered(false);
            if (m_neProgressTip != nullptr) {
                m_neProgressTip->SetVisible(false);
            }
        }
        return true;
        });
    m_neProgress->AttachMouseMove([this](const ui::EventArgs& args) {
        m_neProgressHovered = true;
        SetNetEaseProgressHovered(true);
        UpdateNetEaseProgressFromMouse(args.ptMouse);
        return true;
        });
    m_neProgress->AttachButtonDown([this](const ui::EventArgs& args) {
        m_neSeeking = true;
        m_neProgressHovered = true;
        SetNetEaseProgressHovered(true);
        UpdateNetEaseProgressFromMouse(args.ptMouse);
        return true;
        });
    m_neProgress->AttachButtonUp([this](const ui::EventArgs& /*args*/) {
        m_neSeeking = false;
        if (m_neProgress != nullptr) {
            const double ratio = std::clamp(m_neProgress->GetValue() / 1000.0, 0.0, 1.0);
            SeekNetEaseProgress(ratio);
            UpdateNetEaseProgressTip(ratio);
        }
        return true;
        });
    m_neProgress->AttachValueChanged([this](const ui::EventArgs& /*args*/) {
        if (m_neProgress == nullptr) {
            return true;
        }
        const double ratio = std::clamp(m_neProgress->GetValue() / 1000.0, 0.0, 1.0);
        if (m_neSeeking) {
            SeekNetEaseProgress(ratio);
        }
        UpdateNetEaseProgressTip(ratio);
        return true;
        });
}

double MainForm::GetNetEaseDuration() const
{
    if (m_player.HasStream()) {
        const double duration = m_player.GetDuration();
        if (duration > 0.0) {
            return duration;
        }
    }
    return 236.0;
}

void MainForm::SeekNetEaseProgress(double ratio)
{
    if (!m_player.HasStream()) {
        return;
    }
    const double duration = m_player.GetDuration();
    if (duration > 0.0) {
        m_player.SeekTo(std::clamp(ratio, 0.0, 1.0) * duration);
    }
}

void MainForm::SetNetEaseProgressHovered(bool hovered)
{
    if (m_neProgress != nullptr) {
        m_neProgress->SetFixedHeight(ui::UiFixedInt(hovered ? 7 : 4), true, true);
    }
}

void MainForm::UpdateNetEaseProgressFromMouse(const ui::UiPoint& ptMouse)
{
    if (m_neProgress == nullptr) {
        return;
    }
    const ui::UiRect rect = m_neProgress->GetPos();
    if (rect.Width() <= 0) {
        return;
    }
    const double ratio = std::clamp((ptMouse.x - rect.left) / static_cast<double>(rect.Width()), 0.0, 1.0);
    UpdateNetEaseProgressTip(ratio);
}

void MainForm::UpdateNetEaseProgressTip(double ratio)
{
    if ((m_neProgress == nullptr) || (m_neProgressTip == nullptr)) {
        return;
    }

    const double duration = GetNetEaseDuration();
    const double preview = std::clamp(ratio, 0.0, 1.0) * duration;
    std::wstring text = FormatTime(preview);
    text += L" / ";
    text += FormatTime(duration);
    m_neProgressTip->SetText(text.c_str());

    ui::UiRect rect = m_neProgress->GetPos();
    ui::UiRect tipRect = m_neProgressTip->GetPos();
    int tipWidth = tipRect.Width();
    int tipHeight = tipRect.Height();
    if (tipWidth <= 0) {
        tipWidth = static_cast<int>(120 * m_neProgress->Dpi().GetDisplayScale());
    }
    if (tipHeight <= 0) {
        tipHeight = static_cast<int>(24 * m_neProgress->Dpi().GetDisplayScale());
    }

    int x = rect.left + static_cast<int>(std::clamp(ratio, 0.0, 1.0) * rect.Width()) - tipWidth / 2;
    x = std::clamp(x, rect.left, std::max(rect.left, rect.right - tipWidth));
    const int y = rect.bottom + 4;
    int marginX = x;
    int marginY = y;
    if (m_neProgressTip->GetParent() != nullptr) {
        const ui::UiRect parentRect = m_neProgressTip->GetParent()->GetPos();
        marginX -= parentRect.left;
        marginY -= parentRect.top;
    }
    m_neProgressTip->SetMargin(ui::UiMargin(marginX, marginY, 0, 0), false);
    m_neProgressTip->SetVisible(true);
}

void MainForm::UpdateNetEaseListIndices()
{
    auto updateList = [this](const std::vector<ui::Button*>& buttons,
                             const std::vector<bool>& rowHovered,
                             const std::vector<bool>& iconHovered,
                             int activeRow) {
        const ui::ControlStateType states[] = {
            ui::kControlStateNormal,
            ui::kControlStateHovered,
            ui::kControlStatePressed
        };
        for (size_t i = 0; i < buttons.size(); ++i) {
            ui::Button* button = buttons[i];
            if (button == nullptr) {
                continue;
            }
            const bool hovered = (i < rowHovered.size()) && rowHovered[i];
            const bool iconHover = (i < iconHovered.size()) && iconHovered[i];
            const bool showPlay = (static_cast<int>(i) == activeRow) || hovered || iconHover;
            if (showPlay) {
                const DString image = iconHover
                    ? _T("file='images/netease/play_black_52.png' width='24' height='24' valign='center' halign='center'")
                    : _T("file='images/netease/play_lightgray_52.png' width='24' height='24' valign='center' halign='center'");
                button->SetText(_T(""));
                for (ui::ControlStateType state : states) {
                    button->SetStateImage(state, image);
                }
            }
            else {
                wchar_t number[16] = { 0 };
                swprintf_s(number, L"%02d", static_cast<int>(i + 1));
                button->SetText(number);
                for (ui::ControlStateType state : states) {
                    button->SetStateImage(state, _T(""));
                }
            }
        }
    };

    updateList(m_neLocalIndexButtons, m_neLocalRowHovered, m_neLocalIconHovered, m_neLocalActiveRow);
    updateList(m_neRecentIndexButtons, m_neRecentRowHovered, m_neRecentIconHovered, m_neRecentActiveRow);
    updateList(m_neFavoriteIndexButtons, m_neFavoriteRowHovered, m_neFavoriteIconHovered, m_neFavoriteActiveRow);
}

void MainForm::InitDatabase()
{
    wchar_t exePath[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path dbPath =
        std::filesystem::path(exePath).parent_path() / L"resources" / L"db" / L"KCEchoPlayer.db";
    if (!m_database.Open(dbPath.wstring())) {
        WriteLog(L"Database open failed: " + DatabaseMgr::FromUtf8(m_database.GetLastError()));
        return;
    }
    m_database.Execute("CREATE TABLE IF NOT EXISTS app_settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    m_database.Execute("INSERT OR IGNORE INTO app_settings(key, value) VALUES('auto_play_on_startup', '0');");
}

void MainForm::LoadLocalSongs()
{
    m_localSongs.clear();
    const DatabaseMgr::Rows rows = m_database.Query(
        "SELECT id, title, artist, album, duration_ms, file_path, file_size, COALESCE(cover_path, '') "
        "FROM songs ORDER BY id ASC");
    for (const DatabaseMgr::Row& row : rows) {
        if (row.size() < 8) {
            continue;
        }
        SongInfo song;
        song.id = row[0].AsInt();
        song.title = row[1].AsWide();
        song.artist = row[2].AsWide();
        song.album = row[3].AsWide();
        song.durationMs = row[4].AsInt();
        song.filePath = row[5].AsWide();
        song.fileSize = row[6].AsInt();
        song.coverPath = row[7].AsWide();
        m_localSongs.push_back(std::move(song));
    }
}

void MainForm::LoadRecentSongs()
{
    m_recentSongs.clear();
    const DatabaseMgr::Rows rows = m_database.Query(
        "SELECT song_id, title, artist, album, COALESCE(cover_path, ''), duration_ms, file_size, "
        "file_path, is_favorite, last_played_at, play_count, last_position_ms "
        "FROM v_recent_plays");
    for (const DatabaseMgr::Row& row : rows) {
        if (row.size() < 12) {
            continue;
        }
        RecentSongInfo song;
        song.id = row[0].AsInt();
        song.title = row[1].AsWide();
        song.artist = row[2].AsWide();
        song.album = row[3].AsWide();
        song.coverPath = row[4].AsWide();
        song.durationMs = row[5].AsInt();
        song.fileSize = row[6].AsInt();
        song.filePath = row[7].AsWide();
        song.favorite = row[8].AsInt() != 0;
        song.lastPlayedAt = row[9].AsInt();
        song.playCount = row[10].AsInt();
        song.lastPositionMs = row[11].AsInt();
        m_recentSongs.push_back(std::move(song));
    }
}

void MainForm::LoadFavoriteSongs()
{
    m_favoriteSongs.clear();
    const DatabaseMgr::Rows rows = m_database.Query(
        "SELECT song_id, title, artist, album, COALESCE(cover_path, ''), duration_ms, file_size, "
        "file_path, added_at, sort_order, COALESCE(note, ''), play_count "
        "FROM v_favorites");
    for (const DatabaseMgr::Row& row : rows) {
        if (row.size() < 12) {
            continue;
        }
        FavoriteSongInfo song;
        song.id = row[0].AsInt();
        song.title = row[1].AsWide();
        song.artist = row[2].AsWide();
        song.album = row[3].AsWide();
        song.coverPath = row[4].AsWide();
        song.durationMs = row[5].AsInt();
        song.fileSize = row[6].AsInt();
        song.filePath = row[7].AsWide();
        song.addedAt = row[8].AsInt();
        song.sortOrder = row[9].AsInt();
        song.note = row[10].AsWide();
        song.playCount = row[11].AsInt();
        m_favoriteSongs.push_back(std::move(song));
    }
}

void MainForm::RefreshLocalList()
{
    if (m_neLocalList == nullptr) {
        return;
    }
    m_neLocalList->RemoveAllItems();
    m_neLocalRowUi.clear();
    m_neLocalRows.clear();
    m_neLocalIndexButtons.clear();
    m_neLocalRowHovered.assign(m_localSongs.size(), false);
    m_neLocalIconHovered.assign(m_localSongs.size(), false);
    m_neLocalActiveRow = m_hasCurrentSong ? FindLocalIndexBySongId(m_currentSong.id) : -1;

    auto makeLabel = [this](const DString& cls, const std::wstring& text, int width, bool stretch) {
        ui::Label* label = new ui::Label(this);
        label->SetClass(cls);
        label->SetText(text.c_str());
        label->SetMouseEnabled(false);
        if (stretch) {
            label->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        }
        else {
            label->SetFixedWidth(ui::UiFixedInt(width), true, true);
        }
        return label;
    };
    auto makeTitleBox = [this, &makeLabel](const std::wstring& title, const std::wstring& artist,
                                           ui::Label*& titleLabel, ui::Label*& artistLabel) {
        ui::VBox* box = new ui::VBox(this);
        box->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        box->SetFixedHeight(ui::UiFixedInt::MakeAuto(), true, true);
        box->SetAttribute(_T("valign"), _T("center"));
        box->SetAttribute(_T("child_margin"), _T("1"));
        box->SetAttribute(_T("mouse_enabled"), _T("false"));
        titleLabel = makeLabel(_T("ne_song_title"), title, 0, true);
        artistLabel = makeLabel(_T("ne_song_artist"), artist, 0, true);
        box->AddItem(titleLabel);
        box->AddItem(artistLabel);
        return box;
    };

    for (size_t i = 0; i < m_localSongs.size(); ++i) {
        const SongInfo song = m_localSongs[i];
        ui::ListBoxItem* item = new ui::ListBoxItem(this);
        item->SetClass(_T("ne_list_row"));
        item->SetFixedHeight(ui::UiFixedInt(58), true, true);

        ui::HBox* row = new ui::HBox(this);
        row->SetPadding(ui::UiPadding(13, 0, 13, 0), true);
        row->SetFixedHeight(ui::UiFixedInt::MakeStretch(), true, true);
        row->SetAttribute(_T("child_margin"), _T("10"));
        row->SetMouseEnabled(false);

        ui::Button* index = new ui::Button(this);
        index->SetClass(_T("ne_index_btn"));
        index->SetFixedWidth(ui::UiFixedInt(36), true, true);

        ui::Label* titleLabel = nullptr;
        ui::Label* artistLabel = nullptr;
        ui::VBox* titleBox = makeTitleBox(NonEmptyText(song.title, L"未知歌曲"),
                                          NonEmptyText(song.artist, L"未知歌手"),
                                          titleLabel, artistLabel);
        ui::Label* album = makeLabel(_T("ne_col"), NonEmptyText(song.album, L"未知专辑"), 210, false);
        ui::Label* duration = makeLabel(_T("ne_col"), FormatTime(song.durationMs / 1000.0), 80, false);
        ui::Label* size = makeLabel(_T("ne_col"), FormatFileSize(song.fileSize), 80, false);

        row->AddItem(index);
        row->AddItem(titleBox);
        row->AddItem(album);
        row->AddItem(duration);
        row->AddItem(size);
        item->AddItem(row);
        item->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });

        auto* rowHovered = &m_neLocalRowHovered;
        auto* iconHovered = &m_neLocalIconHovered;
        item->AttachMouseEnter([this, i, rowHovered](const ui::EventArgs& /*args*/) {
            (*rowHovered)[i] = true;
            UpdateNetEaseListIndices();
            return true;
            });
        item->AttachMouseLeave([this, i, rowHovered, iconHovered](const ui::EventArgs& /*args*/) {
            (*rowHovered)[i] = false;
            (*iconHovered)[i] = false;
            UpdateNetEaseListIndices();
            return true;
            });
        index->AttachMouseEnter([this, i, iconHovered](const ui::EventArgs& /*args*/) {
            (*iconHovered)[i] = true;
            UpdateNetEaseListIndices();
            return true;
            });
        index->AttachMouseLeave([this, i, iconHovered](const ui::EventArgs& /*args*/) {
            (*iconHovered)[i] = false;
            UpdateNetEaseListIndices();
            return true;
            });
        row->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });
        index->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });

        m_neLocalList->AddItem(item);
        m_neLocalRows.push_back(row);
        m_neLocalIndexButtons.push_back(index);
        m_neLocalRowUi.push_back({ index, titleLabel, artistLabel, album, duration, size });
    }

    UpdateNetEaseLocalCount();
    UpdateNetEaseListIndices();
    UpdateNetEaseRowStyles();
}

void MainForm::RefreshRecentList()
{
    if (m_neRecentList == nullptr) {
        return;
    }
    m_neRecentList->RemoveAllItems();
    m_neRecentRowUi.clear();
    m_neRecentRows.clear();
    m_neRecentIndexButtons.clear();
    m_neRecentRowHovered.assign(m_recentSongs.size(), false);
    m_neRecentIconHovered.assign(m_recentSongs.size(), false);
    m_neRecentActiveRow = m_hasCurrentSong ? FindRecentIndexBySongId(m_currentSong.id) : -1;

    auto makeLabel = [this](const DString& cls, const std::wstring& text, int width, bool stretch) {
        ui::Label* label = new ui::Label(this);
        label->SetClass(cls);
        label->SetText(text.c_str());
        label->SetMouseEnabled(false);
        if (stretch) {
            label->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        }
        else {
            label->SetFixedWidth(ui::UiFixedInt(width), true, true);
        }
        return label;
    };
    auto makeTitleBox = [this, &makeLabel](const std::wstring& title, const std::wstring& artist,
                                           ui::Label*& titleLabel, ui::Label*& artistLabel) {
        ui::VBox* box = new ui::VBox(this);
        box->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        box->SetFixedHeight(ui::UiFixedInt::MakeAuto(), true, true);
        box->SetAttribute(_T("valign"), _T("center"));
        box->SetAttribute(_T("child_margin"), _T("1"));
        box->SetAttribute(_T("mouse_enabled"), _T("false"));
        titleLabel = makeLabel(_T("ne_song_title"), title, 0, true);
        artistLabel = makeLabel(_T("ne_song_artist"), artist, 0, true);
        box->AddItem(titleLabel);
        box->AddItem(artistLabel);
        return box;
    };
    static const DString kCoverColors[] = {
        _T("#FF3D3D4A"), _T("#FF47536B"), _T("#FF8A6B58"), _T("#FFB36A4E"),
        _T("#FF4A6C9B"), _T("#FFC45B48"), _T("#FFC06B9D"), _T("#FF8F6E5B"),
        _T("#FF567B9E")
    };

    for (size_t i = 0; i < m_recentSongs.size(); ++i) {
        const RecentSongInfo song = m_recentSongs[i];
        ui::ListBoxItem* item = new ui::ListBoxItem(this);
        item->SetClass(_T("ne_list_row"));
        item->SetFixedHeight(ui::UiFixedInt(58), true, true);

        ui::HBox* row = new ui::HBox(this);
        row->SetPadding(ui::UiPadding(13, 0, 13, 0), true);
        row->SetFixedHeight(ui::UiFixedInt::MakeStretch(), true, true);
        row->SetAttribute(_T("child_margin"), _T("10"));
        row->SetMouseEnabled(false);

        ui::Button* index = new ui::Button(this);
        index->SetClass(_T("ne_index_btn"));
        index->SetFixedWidth(ui::UiFixedInt(36), true, true);

        ui::HBox* cover = new ui::HBox(this);
        cover->SetClass(_T("ne_cover_small"));
        cover->SetFixedWidth(ui::UiFixedInt(44), true, true);
        cover->SetFixedHeight(ui::UiFixedInt(44), true, true);
        cover->SetBkColor(kCoverColors[i % (sizeof(kCoverColors) / sizeof(kCoverColors[0]))]);
        ui::Label* coverText = new ui::Label(this);
        coverText->SetClass(_T("ne_cover_text"));
        coverText->SetText(_T("乐"));
        coverText->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        coverText->SetFixedHeight(ui::UiFixedInt::MakeStretch(), true, true);
        coverText->SetMouseEnabled(false);
        cover->AddItem(coverText);

        ui::Label* titleLabel = nullptr;
        ui::Label* artistLabel = nullptr;
        ui::VBox* titleBox = makeTitleBox(NonEmptyText(song.title, L"未知歌曲"),
                                          NonEmptyText(song.artist, L"未知歌手"),
                                          titleLabel, artistLabel);
        ui::Label* album = makeLabel(_T("ne_col"), NonEmptyText(song.album, L"未知专辑"), 250, false);
        ui::Button* favorite = new ui::Button(this);
        favorite->SetClass(_T("ne_ghost_btn"));
        favorite->SetFixedWidth(ui::UiFixedInt(64), true, true);
        favorite->SetFixedHeight(ui::UiFixedInt(28), true, true);
        favorite->SetAttribute(_T("valign"), _T("center"));
        favorite->SetText(song.favorite ? _T("已收藏") : _T("喜欢"));
        if (song.favorite) {
            favorite->SetEnabled(false);
        }
        ui::Label* playedTime = makeLabel(_T("ne_col"), FormatRelativeTime(song.lastPlayedAt), 90, false);

        row->AddItem(index);
        row->AddItem(cover);
        row->AddItem(titleBox);
        row->AddItem(album);
        row->AddItem(favorite);
        row->AddItem(playedTime);
        item->AddItem(row);
        item->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });

        auto* rowHovered = &m_neRecentRowHovered;
        auto* iconHovered = &m_neRecentIconHovered;
        item->AttachMouseEnter([this, i, rowHovered](const ui::EventArgs& /*args*/) {
            (*rowHovered)[i] = true;
            UpdateNetEaseListIndices();
            return true;
            });
        item->AttachMouseLeave([this, i, rowHovered, iconHovered](const ui::EventArgs& /*args*/) {
            (*rowHovered)[i] = false;
            (*iconHovered)[i] = false;
            UpdateNetEaseListIndices();
            return true;
            });
        index->AttachMouseEnter([this, i, iconHovered](const ui::EventArgs& /*args*/) {
            (*iconHovered)[i] = true;
            UpdateNetEaseListIndices();
            return true;
            });
        index->AttachMouseLeave([this, i, iconHovered](const ui::EventArgs& /*args*/) {
            (*iconHovered)[i] = false;
            UpdateNetEaseListIndices();
            return true;
            });
        row->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });
        index->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });
        if (!song.favorite) {
            favorite->AttachClick([this, song](const ui::EventArgs& /*args*/) {
                AddFavoriteBySongId(song.id);
                return true;
                });
        }

        m_neRecentList->AddItem(item);
        m_neRecentRows.push_back(row);
        m_neRecentIndexButtons.push_back(index);
        m_neRecentRowUi.push_back({ index, titleLabel, artistLabel, album, favorite, playedTime });
    }

    if (m_neRecentSingleTab != nullptr) {
        wchar_t buffer[64] = { 0 };
        swprintf_s(buffer, L"单曲 %zu", m_recentSongs.size());
        m_neRecentSingleTab->SetText(buffer);
    }

    UpdateNetEaseListIndices();
    UpdateNetEaseRowStyles();
}

void MainForm::RefreshFavoriteList()
{
    if (m_neFavoriteList == nullptr) {
        return;
    }
    m_neFavoriteList->RemoveAllItems();
    m_neFavoriteRowUi.clear();
    m_neFavoriteRows.clear();
    m_neFavoriteIndexButtons.clear();
    m_neFavoriteRowHovered.assign(m_favoriteSongs.size(), false);
    m_neFavoriteIconHovered.assign(m_favoriteSongs.size(), false);
    m_neFavoriteActiveRow = m_hasCurrentSong ? FindFavoriteIndexBySongId(m_currentSong.id) : -1;

    auto makeLabel = [this](const DString& cls, const std::wstring& text, int width, bool stretch) {
        ui::Label* label = new ui::Label(this);
        label->SetClass(cls);
        label->SetText(text.c_str());
        label->SetMouseEnabled(false);
        if (stretch) {
            label->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        }
        else {
            label->SetFixedWidth(ui::UiFixedInt(width), true, true);
        }
        return label;
    };
    auto makeTitleBox = [this, &makeLabel](const std::wstring& title, const std::wstring& artist,
                                           ui::Label*& titleLabel, ui::Label*& artistLabel) {
        ui::VBox* box = new ui::VBox(this);
        box->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);
        box->SetFixedHeight(ui::UiFixedInt::MakeAuto(), true, true);
        box->SetAttribute(_T("valign"), _T("center"));
        box->SetAttribute(_T("child_margin"), _T("1"));
        box->SetAttribute(_T("mouse_enabled"), _T("false"));
        titleLabel = makeLabel(_T("ne_song_title"), title, 0, true);
        artistLabel = makeLabel(_T("ne_song_artist"), artist, 0, true);
        box->AddItem(titleLabel);
        box->AddItem(artistLabel);
        return box;
    };

    for (size_t i = 0; i < m_favoriteSongs.size(); ++i) {
        const FavoriteSongInfo song = m_favoriteSongs[i];
        ui::ListBoxItem* item = new ui::ListBoxItem(this);
        item->SetClass(_T("ne_list_row"));
        item->SetFixedHeight(ui::UiFixedInt(58), true, true);

        ui::HBox* row = new ui::HBox(this);
        row->SetPadding(ui::UiPadding(13, 0, 13, 0), true);
        row->SetFixedHeight(ui::UiFixedInt::MakeStretch(), true, true);
        row->SetAttribute(_T("child_margin"), _T("10"));
        row->SetMouseEnabled(false);

        ui::Button* index = new ui::Button(this);
        index->SetClass(_T("ne_index_btn"));
        index->SetFixedWidth(ui::UiFixedInt(36), true, true);

        ui::Label* titleLabel = nullptr;
        ui::Label* artistLabel = nullptr;
        ui::VBox* titleBox = makeTitleBox(NonEmptyText(song.title, L"未知歌曲"),
                                          NonEmptyText(song.artist, L"未知歌手"),
                                          titleLabel, artistLabel);
        ui::Label* album = makeLabel(_T("ne_col"), NonEmptyText(song.album, L"未知专辑"), 220, false);
        ui::Label* addedTime = makeLabel(_T("ne_col"), FormatDateTime(song.addedAt), 150, false);
        ui::Label* playCount = makeLabel(_T("ne_col"), std::to_wstring(song.playCount), 80, false);

        ui::Button* remove = new ui::Button(this);
        remove->SetClass(_T("ne_ghost_btn"));
        remove->SetFixedWidth(ui::UiFixedInt(90), true, true);
        remove->SetFixedHeight(ui::UiFixedInt(28), true, true);
        remove->SetAttribute(_T("valign"), _T("center"));
        remove->SetText(_T("取消收藏"));

        row->AddItem(index);
        row->AddItem(titleBox);
        row->AddItem(album);
        row->AddItem(addedTime);
        row->AddItem(playCount);
        row->AddItem(remove);
        item->AddItem(row);
        item->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });

        auto* rowHovered = &m_neFavoriteRowHovered;
        auto* iconHovered = &m_neFavoriteIconHovered;
        item->AttachMouseEnter([this, i, rowHovered](const ui::EventArgs& /*args*/) {
            (*rowHovered)[i] = true;
            UpdateNetEaseListIndices();
            return true;
            });
        item->AttachMouseLeave([this, i, rowHovered, iconHovered](const ui::EventArgs& /*args*/) {
            (*rowHovered)[i] = false;
            (*iconHovered)[i] = false;
            UpdateNetEaseListIndices();
            return true;
            });
        index->AttachMouseEnter([this, i, iconHovered](const ui::EventArgs& /*args*/) {
            (*iconHovered)[i] = true;
            UpdateNetEaseListIndices();
            return true;
            });
        index->AttachMouseLeave([this, i, iconHovered](const ui::EventArgs& /*args*/) {
            (*iconHovered)[i] = false;
            UpdateNetEaseListIndices();
            return true;
            });
        row->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });
        index->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            PlaySong(song);
            return true;
            });
        remove->AttachClick([this, song](const ui::EventArgs& /*args*/) {
            RemoveFavoriteBySongId(song.id);
            return true;
            });

        m_neFavoriteList->AddItem(item);
        m_neFavoriteRows.push_back(row);
        m_neFavoriteIndexButtons.push_back(index);
        m_neFavoriteRowUi.push_back({ index, titleLabel, artistLabel, album, addedTime, playCount, remove });
    }

    UpdateNetEaseFavoriteCount();
    UpdateNetEaseListIndices();
    UpdateNetEaseRowStyles();
}

void MainForm::RefreshAllNetEaseLists()
{
    RefreshLocalList();
    RefreshRecentList();
    RefreshFavoriteList();
}

void MainForm::UpdateNetEaseLocalCount()
{
    if (m_neLocalCount == nullptr) {
        return;
    }
    wchar_t buffer[64] = { 0 };
    swprintf_s(buffer, L"共 %zu 首", m_localSongs.size());
    m_neLocalCount->SetText(buffer);
}

void MainForm::UpdateNetEaseFavoriteCount()
{
    if (m_neFavoriteCount == nullptr) {
        return;
    }
    wchar_t buffer[64] = { 0 };
    swprintf_s(buffer, L"共 %zu 首", m_favoriteSongs.size());
    m_neFavoriteCount->SetText(buffer);
}

int MainForm::FindLocalIndexBySongId(int64_t songId) const
{
    for (size_t i = 0; i < m_localSongs.size(); ++i) {
        if (m_localSongs[i].id == songId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int MainForm::FindRecentIndexBySongId(int64_t songId) const
{
    for (size_t i = 0; i < m_recentSongs.size(); ++i) {
        if (m_recentSongs[i].id == songId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int MainForm::FindFavoriteIndexBySongId(int64_t songId) const
{
    for (size_t i = 0; i < m_favoriteSongs.size(); ++i) {
        if (m_favoriteSongs[i].id == songId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MainForm::ShowHint(const std::wstring& text)
{
    ::MessageBoxW(static_cast<HWND>(GetWindowHandle()), text.c_str(), L"提示",
                  MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

void MainForm::ChooseMusicSource()
{
    if (m_neChooseDir == nullptr) {
        return;
    }
    ui::UiRect rect = m_neChooseDir->GetPos();
    ui::UiPoint point(rect.left, rect.bottom + 4);
    ClientToScreen(point);

    ui::Menu* menu = new ui::Menu(this, m_neChooseDir);
    menu->SetSkinFolder(_T("public/menu"));
    menu->AttachMenuItemActivated([this](const DString& /*menuName*/, int32_t /*nMenuLevel*/,
                                         const DString& itemName, size_t /*nItemIndex*/) {
        if (itemName == _T("import_files")) {
            std::vector<std::wstring> files = ShowOpenAudioDialog(static_cast<HWND>(GetWindowHandle()));
            ImportMusicFiles(std::move(files));
        }
        else if (itemName == _T("import_folder")) {
            const std::wstring folder = ShowSelectFolderDialog(static_cast<HWND>(GetWindowHandle()),
                                                               L"选择音乐目录");
            ImportMusicFolder(folder);
        }
        });
    menu->ShowMenu(_T("import_menu.xml"), point, ui::MenuPopupPosType::LEFT_TOP);
}

void MainForm::ImportMusicFiles(std::vector<std::wstring> files)
{
    size_t added = 0;
    for (const std::wstring& file : files) {
        SongInfo song;
        if (AddSongToDatabase(file, song)) {
            ++added;
        }
    }
    if (added == 0) {
        if (!files.empty()) {
            ShowHint(L"没有可导入的音频文件。");
        }
        return;
    }
    LoadLocalSongs();
    RefreshLocalList();
}

void MainForm::ImportMusicFolder(const std::wstring& folder)
{
    if (folder.empty()) {
        return;
    }
    std::vector<std::wstring> files = ScanAudioFiles(folder);
    if (files.empty()) {
        ShowHint(L"所选目录中没有找到支持的音频文件。");
        return;
    }
    ImportMusicFiles(std::move(files));
}

bool MainForm::AddSongToDatabase(const std::wstring& filePath, SongInfo& song)
{
    std::error_code error;
    std::filesystem::path path(filePath);
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        path = canonicalPath;
    }
    if (!std::filesystem::exists(path, error) || !std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    const std::wstring normalizedPath = path.lexically_normal().wstring();
    const int64_t existingId = m_database.QueryInt(
        "SELECT id FROM songs WHERE file_path = ?1",
        { DatabaseMgr::Value::Text(normalizedPath) },
        0);
    if (existingId > 0) {
        song.id = existingId;
        song.filePath = normalizedPath;
        return true;
    }

    int64_t fileSize = 0;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (!error) {
        fileSize = static_cast<int64_t>(size);
    }

    song.id = 0;
    song.title = path.stem().wstring();
    song.artist = L"未知歌手";
    song.album = L"未知专辑";
    song.filePath = normalizedPath;
    song.durationMs = 0;
    song.fileSize = fileSize;

    int64_t insertedId = 0;
    const bool success = m_database.Execute(
        "INSERT INTO songs(title, artist, album, duration_ms, file_path, file_size, source) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, 'local')",
        {
            DatabaseMgr::Value::Text(song.title),
            DatabaseMgr::Value::Text(song.artist),
            DatabaseMgr::Value::Text(song.album),
            DatabaseMgr::Value::Int(song.durationMs),
            DatabaseMgr::Value::Text(song.filePath),
            DatabaseMgr::Value::Int(song.fileSize)
        },
        insertedId);
    if (!success) {
        WriteLog(L"Insert song failed: " + DatabaseMgr::FromUtf8(m_database.GetLastError()));
        return false;
    }
    song.id = insertedId;
    return true;
}

void MainForm::PlaySong(const SongInfo& song)
{
    std::error_code error;
    if (song.filePath.empty() || !std::filesystem::exists(song.filePath, error)) {
        ShowHint(L"歌曲文件不存在：\n" + song.filePath);
        return;
    }

    m_currentSong = song;
    m_hasCurrentSong = true;
    m_player.Open(song.filePath, true);

    LoadLyricsForSong(song.filePath);
    UpdateLyricsPanel(0.0);

    m_neLocalActiveRow = FindLocalIndexBySongId(song.id);
    m_neRecentActiveRow = FindRecentIndexBySongId(song.id);
    m_neFavoriteActiveRow = FindFavoriteIndexBySongId(song.id);

    UpdateNetEaseBottomInfo();
    UpdateNetEaseListIndices();
    UpdateNetEaseRowStyles();
    UpdateNetEasePlayButton();
    if (m_neProgress != nullptr) {
        m_neProgress->SetValueNoEvent(0.0);
    }
}

void MainForm::PlayLocalRow(size_t index)
{
    if (index < m_localSongs.size()) {
        PlaySong(m_localSongs[index]);
    }
}

void MainForm::PlayRecentRow(size_t index)
{
    if (index < m_recentSongs.size()) {
        PlaySong(m_recentSongs[index]);
    }
}

void MainForm::PlayFavoriteRow(size_t index)
{
    if (index < m_favoriteSongs.size()) {
        PlaySong(m_favoriteSongs[index]);
    }
}

void MainForm::PlayNetEaseNext()
{
    if (m_localSongs.empty()) {
        ShowHint(L"本地音乐列表为空，请先导入歌曲。");
        return;
    }
    const int current = m_hasCurrentSong ? FindLocalIndexBySongId(m_currentSong.id) : -1;
    if (current < 0) {
        PlaySong(m_localSongs.front());
        return;
    }
    const size_t next = static_cast<size_t>((current + 1) % static_cast<int>(m_localSongs.size()));
    PlaySong(m_localSongs[next]);
}

void MainForm::PlayNetEasePrevious()
{
    if (m_localSongs.empty()) {
        ShowHint(L"本地音乐列表为空，请先导入歌曲。");
        return;
    }
    const int current = m_hasCurrentSong ? FindLocalIndexBySongId(m_currentSong.id) : -1;
    if (current < 0) {
        PlaySong(m_localSongs.front());
        return;
    }
    const int count = static_cast<int>(m_localSongs.size());
    const size_t previous = static_cast<size_t>((current - 1 + count) % count);
    PlaySong(m_localSongs[previous]);
}

void MainForm::AddFavoriteBySongId(int64_t songId)
{
    if (songId <= 0) {
        return;
    }
    m_database.Execute(
        "INSERT OR IGNORE INTO favorites(song_id, added_at, sort_order) VALUES(?1, ?2, 0)",
        { DatabaseMgr::Value::Int(songId), DatabaseMgr::Value::Int(NowEpochSeconds()) });
    LoadRecentSongs();
    LoadFavoriteSongs();
    RefreshRecentList();
    RefreshFavoriteList();
}

void MainForm::RemoveFavoriteBySongId(int64_t songId)
{
    if (songId <= 0) {
        return;
    }
    m_database.Execute("DELETE FROM favorites WHERE song_id = ?1",
                       { DatabaseMgr::Value::Int(songId) });
    LoadRecentSongs();
    LoadFavoriteSongs();
    RefreshRecentList();
    RefreshFavoriteList();
}

void MainForm::RecordRecentPlay(const SongInfo& song)
{
    if (song.id <= 0) {
        return;
    }
    const int64_t now = NowEpochSeconds();
    const int64_t count = m_database.QueryInt(
        "SELECT COUNT(*) FROM recent_plays WHERE song_id = ?1",
        { DatabaseMgr::Value::Int(song.id) }, 0);
    if (count > 0) {
        m_database.Execute(
            "UPDATE recent_plays SET last_played_at = ?1, play_count = play_count + 1, "
            "last_position_ms = 0 WHERE song_id = ?2",
            { DatabaseMgr::Value::Int(now), DatabaseMgr::Value::Int(song.id) });
    }
    else {
        m_database.Execute(
            "INSERT INTO recent_plays(song_id, last_played_at, play_count, last_position_ms) "
            "VALUES(?1, ?2, 1, 0)",
            { DatabaseMgr::Value::Int(song.id), DatabaseMgr::Value::Int(now) });
    }
}

void MainForm::UpdateNetEaseRowStyles()
{
    auto setClass = [](ui::Label* label, const DString& cls) {
        if (label != nullptr) {
            label->SetClass(cls);
        }
    };
    for (size_t i = 0; i < m_neLocalRowUi.size(); ++i) {
        const bool active = (static_cast<int>(i) == m_neLocalActiveRow);
        setClass(m_neLocalRowUi[i].title, active ? _T("ne_song_title_red") : _T("ne_song_title"));
        setClass(m_neLocalRowUi[i].artist, active ? _T("ne_song_artist_red") : _T("ne_song_artist"));
        setClass(m_neLocalRowUi[i].album, active ? _T("ne_col_red") : _T("ne_col"));
        setClass(m_neLocalRowUi[i].duration, active ? _T("ne_col_red") : _T("ne_col"));
        setClass(m_neLocalRowUi[i].size, active ? _T("ne_col_red") : _T("ne_col"));
    }
    for (size_t i = 0; i < m_neRecentRowUi.size(); ++i) {
        const bool active = (static_cast<int>(i) == m_neRecentActiveRow);
        setClass(m_neRecentRowUi[i].title, active ? _T("ne_song_title_red") : _T("ne_song_title"));
        setClass(m_neRecentRowUi[i].artist, active ? _T("ne_song_artist_red") : _T("ne_song_artist"));
        setClass(m_neRecentRowUi[i].album, active ? _T("ne_col_red") : _T("ne_col"));
        setClass(m_neRecentRowUi[i].playedTime, active ? _T("ne_col_red") : _T("ne_col"));
    }
    for (size_t i = 0; i < m_neFavoriteRowUi.size(); ++i) {
        const bool active = (static_cast<int>(i) == m_neFavoriteActiveRow);
        setClass(m_neFavoriteRowUi[i].title, active ? _T("ne_song_title_red") : _T("ne_song_title"));
        setClass(m_neFavoriteRowUi[i].artist, active ? _T("ne_song_artist_red") : _T("ne_song_artist"));
        setClass(m_neFavoriteRowUi[i].album, active ? _T("ne_col_red") : _T("ne_col"));
        setClass(m_neFavoriteRowUi[i].addedTime, active ? _T("ne_col_red") : _T("ne_col"));
        setClass(m_neFavoriteRowUi[i].playCount, active ? _T("ne_col_red") : _T("ne_col"));
    }
}

void MainForm::LoadLyricsForSong(const std::wstring& filePath)
{
    m_lyrics.clear();
    m_currentLyricIndex = -1;

    std::filesystem::path lrcPath(filePath);
    lrcPath.replace_extension(L".lrc");
    std::error_code error;
    if (!std::filesystem::exists(lrcPath, error)) {
        lrcPath.replace_extension(L".LRC");
        if (!std::filesystem::exists(lrcPath, error)) {
            return;
        }
    }

    const std::wstring content = ReadTextFileAutoEncoding(lrcPath);
    if (content.empty()) {
        return;
    }

    std::wstringstream stream(content);
    std::wstring line;
    while (std::getline(stream, line)) {
        line = TrimWideText(line);
        if (line.empty()) {
            continue;
        }
        std::vector<int64_t> times;
        size_t pos = 0;
        while (pos < line.size() && line[pos] == L'[') {
            const size_t end = line.find(L']', pos + 1);
            if (end == std::wstring::npos) {
                break;
            }
            const std::wstring tag = line.substr(pos + 1, end - pos - 1);
            int minutes = 0;
            int seconds = 0;
            double fraction = 0.0;
            const int parsed = swscanf_s(tag.c_str(), L"%d:%d.%lf", &minutes, &seconds, &fraction);
            if (parsed == 3) {
                const int milliseconds = static_cast<int>(fraction * 1000.0 + 0.5);
                times.push_back((static_cast<int64_t>(minutes) * 60 + seconds) * 1000 + milliseconds);
            }
            else if (swscanf_s(tag.c_str(), L"%d:%d", &minutes, &seconds) == 2) {
                times.push_back((static_cast<int64_t>(minutes) * 60 + seconds) * 1000);
            }
            pos = end + 1;
        }
        if (times.empty() || pos >= line.size()) {
            continue;
        }
        const std::wstring lyricText = TrimWideText(line.substr(pos));
        if (lyricText.empty()) {
            continue;
        }
        for (int64_t timeMs : times) {
            m_lyrics.push_back({ timeMs, lyricText });
        }
    }

    std::sort(m_lyrics.begin(), m_lyrics.end(),
              [](const LyricLine& left, const LyricLine& right) {
                  return left.timeMs < right.timeMs;
              });
}

void MainForm::UpdateLyricsPanel(double positionSeconds)
{
    if (m_lyricLabels.empty()) {
        return;
    }
    const int center = static_cast<int>(m_lyricLabels.size() / 2);
    if (m_lyrics.empty()) {
        if (m_currentLyricIndex == -2) {
            return;
        }
        m_currentLyricIndex = -2;
        for (size_t i = 0; i < m_lyricLabels.size(); ++i) {
            ui::Label* label = m_lyricLabels[i];
            if (label == nullptr) {
                continue;
            }
            label->SetClass(_T("ne_lyric_line"));
            label->SetText(i == static_cast<size_t>(center) ? _T("暂无歌词") : _T(""));
        }
        return;
    }

    const int64_t positionMs = static_cast<int64_t>(std::max(0.0, positionSeconds) * 1000.0);
    int index = 0;
    if (positionMs >= m_lyrics.front().timeMs) {
        for (size_t i = 0; i < m_lyrics.size(); ++i) {
            if (m_lyrics[i].timeMs <= positionMs) {
                index = static_cast<int>(i);
            }
            else {
                break;
            }
        }
    }
    if (index == m_currentLyricIndex) {
        return;
    }
    m_currentLyricIndex = index;

    for (int i = 0; i < static_cast<int>(m_lyricLabels.size()); ++i) {
        ui::Label* label = m_lyricLabels[static_cast<size_t>(i)];
        if (label == nullptr) {
            continue;
        }
        const int lineIndex = index - center + i;
        if ((lineIndex >= 0) && (lineIndex < static_cast<int>(m_lyrics.size()))) {
            label->SetText(m_lyrics[static_cast<size_t>(lineIndex)].text.c_str());
        }
        else {
            label->SetText(_T(""));
        }
        label->SetClass(i == center ? _T("ne_lyric_line_active") : _T("ne_lyric_line"));
    }
}

void MainForm::UpdateNetEaseBottomInfo()
{
    const std::wstring title = m_hasCurrentSong ? NonEmptyText(m_currentSong.title, L"未选择") : L"未选择";
    const std::wstring artist = m_hasCurrentSong ? NonEmptyText(m_currentSong.artist, L"") : L"";
    if (m_neBottomTitle != nullptr) {
        m_neBottomTitle->SetText(title.c_str());
    }
    if (m_neBottomArtist != nullptr) {
        m_neBottomArtist->SetText(artist.c_str());
    }
    if (m_nePlayerBottomTitle != nullptr) {
        m_nePlayerBottomTitle->SetText(title.c_str());
    }
    if (m_nePlayerBottomArtist != nullptr) {
        m_nePlayerBottomArtist->SetText(artist.c_str());
    }
    if (m_nePlayerPageTitle != nullptr) {
        m_nePlayerPageTitle->SetText(title.c_str());
    }
    if (m_nePlayerPageMeta != nullptr) {
        if (m_hasCurrentSong) {
            const std::wstring album = NonEmptyText(m_currentSong.album, L"未知专辑");
            const std::wstring meta = L"专辑：" + album + L"     歌手：" + artist + L"     来源：本地音乐";
            m_nePlayerPageMeta->SetText(meta.c_str());
        }
        else {
            m_nePlayerPageMeta->SetText(_T("来源：本地音乐"));
        }
    }
}

void MainForm::LoadAppSettings()
{
    if (!m_database.IsOpen()) {
        return;
    }
    m_database.Execute("CREATE TABLE IF NOT EXISTS app_settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    m_database.Execute("INSERT OR IGNORE INTO app_settings(key, value) VALUES('auto_play_on_startup', '0');");
}

void MainForm::SetAutoStart(bool enabled)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    if (enabled) {
        wchar_t exePath[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        ::RegSetValueExW(key, L"KCEchoPlayer", 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(exePath),
                         static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t)));
    }
    else {
        ::RegDeleteValueW(key, L"KCEchoPlayer");
    }
    ::RegCloseKey(key);
}

bool MainForm::IsAutoStartEnabled() const
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    const bool exists = ::RegQueryValueExW(key, L"KCEchoPlayer", nullptr, nullptr,
                                           nullptr, nullptr) == ERROR_SUCCESS;
    ::RegCloseKey(key);
    return exists;
}

void MainForm::SetAutoPlayOnStartup(bool enabled)
{
    if (!m_database.IsOpen()) {
        return;
    }
    m_database.Execute(
        "INSERT OR REPLACE INTO app_settings(key, value) VALUES('auto_play_on_startup', ?1)",
        { DatabaseMgr::Value::Text(enabled ? L"1" : L"0") });
}

bool MainForm::IsAutoPlayOnStartup()
{
    if (!m_database.IsOpen()) {
        return false;
    }
    const std::string value = m_database.QueryText(
        "SELECT value FROM app_settings WHERE key = 'auto_play_on_startup'",
        {},
        "0");
    return value == "1";
}

void MainForm::AddPlaylistItem(const std::wstring& filePath, size_t index)
{
    if (m_playlistBox == nullptr) {
        return;
    }

    ui::ListBoxItem* item = new ui::ListBoxItem(this);
    item->SetClass(_T("mc_list_item"));

    ui::HBox* row = new ui::HBox(this);
    row->SetPadding(ui::UiPadding(12, 0, 12, 0), true);
    row->SetFixedHeight(ui::UiFixedInt::MakeStretch(), true, true);
    row->SetMouseEnabled(false);

    ui::Label* number = new ui::Label(this);
    number->SetClass(_T("mc_item_num"));
    number->SetMouseEnabled(false);
    wchar_t numberText[16] = { 0 };
    swprintf_s(numberText, L"%02d", static_cast<int>(index + 1));
    number->SetText(numberText);
    number->SetFixedWidth(ui::UiFixedInt(28), true, true);

    ui::Label* name = new ui::Label(this);
    name->SetClass(_T("mc_item_name"));
    name->SetMouseEnabled(false);
    name->SetText(FileNameOf(filePath).c_str());
    name->SetFixedWidth(ui::UiFixedInt::MakeStretch(), true, true);

    row->AddItem(number);
    row->AddItem(name);
    item->AddItem(row);

    item->AttachClick([this, index](const ui::EventArgs& /*args*/) {
        PlayIndex(index);
        return true;
        });

    m_playlistBox->AddItem(item);

    PlaylistUi uiInfo;
    uiInfo.item = item;
    uiInfo.number = number;
    uiInfo.name = name;
    m_playlistUi.push_back(uiInfo);
}

void MainForm::PlayIndex(size_t index)
{
    if (index >= m_playlist.size()) {
        return;
    }
    if ((m_currentIndex == static_cast<int>(index)) && m_player.HasStream()) {
        UpdatePlaylistHighlight();
        return;
    }

    m_currentIndex = static_cast<int>(index);
    m_player.Open(m_playlist[index], true);

    if (m_playlistBox != nullptr) {
        m_playlistBox->SelectItem(index, false, false);
    }
    UpdatePlaylistHighlight();
    UpdateNowPlayingText();
    if (m_sliderProgress != nullptr) {
        m_sliderProgress->SetValueNoEvent(0.0);
    }
    if (m_lblCurrentTime != nullptr) {
        m_lblCurrentTime->SetText(_T("00:00"));
    }
    if (m_lblTotalTime != nullptr) {
        m_lblTotalTime->SetText(_T("00:00"));
    }
}

void MainForm::PlayCurrent()
{
    if (m_isNetEase) {
        if (!m_hasCurrentSong) {
            if (!m_localSongs.empty()) {
                PlaySong(m_localSongs.front());
            }
            else {
                ShowHint(L"本地音乐列表为空，请先导入歌曲。");
            }
            return;
        }
        if (!m_player.HasStream()) {
            PlaySong(m_currentSong);
            return;
        }
        m_player.Play();
        UpdateNetEasePlayButton();
        return;
    }

    if (m_playlist.empty()) {
        return;
    }
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playlist.size())) {
        PlayIndex(0);
        return;
    }
    if (!m_player.HasStream()) {
        PlayIndex(static_cast<size_t>(m_currentIndex));
        return;
    }
    m_player.Play();
}

void MainForm::PauseCurrent()
{
    m_player.Pause();
}

void MainForm::PlayNext()
{
    if (m_isNetEase) {
        PlayNetEaseNext();
        return;
    }
    if (m_playlist.empty()) {
        return;
    }
    int next = m_currentIndex + 1;
    if (next < 0 || next >= static_cast<int>(m_playlist.size())) {
        next = 0;
    }
    PlayIndex(static_cast<size_t>(next));
}

void MainForm::PlayPrevious()
{
    if (m_isNetEase) {
        PlayNetEasePrevious();
        return;
    }
    if (m_playlist.empty()) {
        return;
    }
    int previous = m_currentIndex - 1;
    if (previous < 0) {
        previous = static_cast<int>(m_playlist.size()) - 1;
    }
    PlayIndex(static_cast<size_t>(previous));
}

void MainForm::SeekRelative(double seconds)
{
    if (!m_player.HasStream()) {
        return;
    }
    const double duration = m_player.GetDuration();
    double position = m_player.GetPosition() + seconds;
    if (position < 0.0) {
        position = 0.0;
    }
    if (duration > 0.0 && position > duration) {
        position = duration;
    }
    m_player.SeekTo(position);
    if (m_lblCurrentTime != nullptr) {
        m_lblCurrentTime->SetText(FormatTime(position).c_str());
    }
}

void MainForm::UpdateMuteButton()
{
    if (m_btnMute == nullptr) {
        return;
    }
    const bool muted = m_systemVolume.GetMute();
    if (muted != m_lastMuteState) {
        m_lastMuteState = muted;
        m_btnMute->SetText(muted ? _T("取消") : _T("静音"));
    }
}

void MainForm::UpdateNowPlayingText()
{
    if (m_lblNowPlaying == nullptr) {
        return;
    }
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_playlist.size())) {
        const std::wstring text = L"正在播放: " + FileNameOf(m_playlist[static_cast<size_t>(m_currentIndex)]);
        m_lblNowPlaying->SetText(text.c_str());
    }
    else {
        m_lblNowPlaying->SetText(_T("正在播放: 未选择"));
    }
    if (m_pannel_lbl_now_playing == nullptr) {
        return;
    }
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_playlist.size())) {
        const std::wstring text = FileNameOf(m_playlist[static_cast<size_t>(m_currentIndex)]);
        m_pannel_lbl_now_playing->SetText(text.c_str());
    }
    else {
        m_pannel_lbl_now_playing->SetText(_T("未选择"));
    }
    if (m_media_lbl_now_playing == nullptr) {
        return;
    }
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_playlist.size())) {
        const std::wstring text = FileNameOf(m_playlist[static_cast<size_t>(m_currentIndex)]);
        m_media_lbl_now_playing->SetText(text.c_str());
    }
    else {
        m_media_lbl_now_playing->SetText(_T("未选择"));
    }
}

void MainForm::UpdatePlaylistHighlight()
{
    for (size_t i = 0; i < m_playlistUi.size(); ++i) {
        const bool selected = (static_cast<int>(i) == m_currentIndex);
        if (m_playlistUi[i].number != nullptr) {
            m_playlistUi[i].number->SetClass(selected ? _T("mc_item_num_sel") : _T("mc_item_num"));
        }
        if (m_playlistUi[i].name != nullptr) {
            m_playlistUi[i].name->SetClass(selected ? _T("mc_item_name_sel") : _T("mc_item_name"));
        }
    }
}

bool MainForm::OnThemeChanged()
{
    const bool result = BaseClass::OnThemeChanged();
    UpdateMediaBackground();
    return result;
}

void MainForm::UpdateMediaBackground()
{
    if (m_mediaDisplay == nullptr) {
        return;
    }
    const bool dark = (ui::GlobalManager::Instance().Theme().GetCurrentThemeStyle() == ui::ThemeStyle::kDark);
    const DString imagePath = dark ? _T("images/KC_EchoPlayer-dark.png") : _T("images/KC_EchoPlayer-light.png");
    DString bkImage = _T("file='");
    bkImage += imagePath;
    bkImage += _T("' adaptive_dest_rect='true' halign='center' valign='center'");
    m_mediaDisplay->SetBkImage(bkImage);
}

void MainForm::UpdateVisualization()
{
    if ((m_visual == nullptr) || (m_gdiplusToken == 0)) {
        return;
    }

    const ui::UiRect rc = m_visual->GetRect();
    const int logicalWidth = rc.Width();
    const int logicalHeight = rc.Height();
    if ((logicalWidth <= 8) || (logicalHeight <= 8)) {
        return;
    }

    const float dpiScale = m_visual->Dpi().GetDisplayScale();
    const int pixelWidth = std::max(16, static_cast<int>(std::lround(logicalWidth * dpiScale)));
    const int pixelHeight = std::max(16, static_cast<int>(std::lround(logicalHeight * dpiScale)));

    std::vector<float> spectrum;
    m_player.GetSpectrum(spectrum);

    Gdiplus::Bitmap bitmap(pixelWidth, pixelHeight, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        const bool dark = (ui::GlobalManager::Instance().Theme().GetCurrentThemeStyle() == ui::ThemeStyle::kDark);
        const Gdiplus::Color bottomColor = dark ? Gdiplus::Color(235, 79, 139, 255)
                                                : Gdiplus::Color(235, 60, 150, 235);
        const Gdiplus::Color topColor = dark ? Gdiplus::Color(245, 140, 100, 255)
                                             : Gdiplus::Color(245, 120, 90, 240);

        if (!spectrum.empty()) {
            const int binCount = static_cast<int>(spectrum.size());
            const int barCount = std::min(64, std::max(16, pixelWidth / 12));
            if (barCount > 0) {
                Gdiplus::LinearGradientBrush brush(Gdiplus::Point(0, pixelHeight),
                                                   Gdiplus::Point(0, 0),
                                                   bottomColor, topColor);
                const float barWidth = static_cast<float>(pixelWidth) / static_cast<float>(barCount);
                for (int i = 0; i < barCount; ++i) {
                    const float from = std::pow(static_cast<float>(binCount), static_cast<float>(i) / barCount);
                    const float to = std::pow(static_cast<float>(binCount), static_cast<float>(i + 1) / barCount);
                    int binFrom = std::clamp(static_cast<int>(from), 1, binCount - 1);
                    int binTo = std::clamp(static_cast<int>(to), binFrom + 1, binCount - 1);

                    float peak = 0.0f;
                    for (int bin = binFrom; bin < binTo; ++bin) {
                        peak = std::max(peak, spectrum[static_cast<size_t>(bin)]);
                    }
                    // 使用平方根压缩动态范围，让高、低频都清晰可见
                    const float level = std::min(1.0f, std::sqrt(std::clamp(peak, 0.0f, 1.0f)) * 2.5f);
                    float barHeight = level * static_cast<float>(pixelHeight - 4);
                    if (barHeight < 2.0f) {
                        barHeight = 2.0f;
                    }
                    const float left = static_cast<float>(i) * barWidth + 1.0f;
                    const float width = std::max(1.0f, barWidth - 2.0f);
                    Gdiplus::RectF barRect(left, static_cast<float>(pixelHeight) - barHeight, width, barHeight);
                    graphics.FillRectangle(&brush, barRect);
                }
            }
        }
    }

    Gdiplus::BitmapData bitmapData = {};
    Gdiplus::Rect lockRect(0, 0, pixelWidth, pixelHeight);
    if (bitmap.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bitmapData) == Gdiplus::Ok) {
        const int32_t byteCount = static_cast<int32_t>(std::abs(bitmapData.Stride) * pixelHeight);
        m_visual->SetBitmapData(pixelWidth, pixelHeight,
                                reinterpret_cast<const uint8_t*>(bitmapData.Scan0),
                                byteCount);
        bitmap.UnlockBits(&bitmapData);
    }
}

void MainForm::OnTimer()
{
    if (m_player.ConsumeEnded()) {
        if (m_isNetEase) {
            if (m_hasCurrentSong) {
                RecordRecentPlay(m_currentSong);
                LoadRecentSongs();
                RefreshRecentList();

                const int localIndex = FindLocalIndexBySongId(m_currentSong.id);
                if ((localIndex >= 0) && !m_localSongs.empty()) {
                    const size_t next = static_cast<size_t>((localIndex + 1) % static_cast<int>(m_localSongs.size()));
                    PlaySong(m_localSongs[next]);
                }
                else {
                    m_player.Stop();
                    if (m_neProgress != nullptr) {
                        m_neProgress->SetValueNoEvent(0.0);
                    }
                }
            }
        }
        else if (m_currentIndex >= 0 && m_currentIndex + 1 < static_cast<int>(m_playlist.size())) {
            PlayIndex(static_cast<size_t>(m_currentIndex + 1));
        }
        else {
            m_player.Stop();
            if (m_sliderProgress != nullptr) {
                m_sliderProgress->SetValueNoEvent(0.0);
            }
            if (m_neProgress != nullptr) {
                m_neProgress->SetValueNoEvent(0.0);
            }
            if (m_lblCurrentTime != nullptr) {
                m_lblCurrentTime->SetText(_T("00:00"));
            }
        }
    }

    if (m_player.HasStream()) {
        const double duration = m_player.GetDuration();
        const double position = m_player.GetPosition();
        if (duration > 0.0) {
            if (!m_seeking && (m_sliderProgress != nullptr)) {
                const double value = std::clamp(position / duration * 1000.0, 0.0, 1000.0);
                m_sliderProgress->SetValueNoEvent(value);
            }
            if (!m_neSeeking && (m_neProgress != nullptr)) {
                const double value = std::clamp(position / duration * 1000.0, 0.0, 1000.0);
                m_neProgress->SetValueNoEvent(value);
            }
            if (m_lblCurrentTime != nullptr) {
                m_lblCurrentTime->SetText(FormatTime(position).c_str());
            }
            if (m_lblTotalTime != nullptr) {
                m_lblTotalTime->SetText(FormatTime(duration).c_str());
            }
        }
    }

    // 系统音量可能被外部修改，定时同步到界面
    if ((m_sliderVolume != nullptr) && m_systemVolume.IsAvailable()) {
        const float systemValue = m_systemVolume.GetVolume() * 100.0f;
        if (std::abs(systemValue - static_cast<float>(m_sliderVolume->GetValue())) > 0.8f) {
            m_sliderVolume->SetValueNoEvent(static_cast<double>(systemValue));
        }
    }
    UpdateMuteButton();
    UpdateVisualization();
    if (m_isNetEase) {
        const double lyricsPosition = m_player.HasStream() ? m_player.GetPosition() : 0.0;
        UpdateLyricsPanel(lyricsPosition);
        UpdateNetEasePlayButton();
    }
}

std::wstring MainForm::FormatTime(double seconds)
{
    int total = static_cast<int>(seconds + 0.5);
    if (total < 0) {
        total = 0;
    }
    const int hour = total / 3600;
    const int minute = (total % 3600) / 60;
    const int second = total % 60;

    wchar_t buffer[32] = { 0 };
    if (hour > 0) {
        swprintf_s(buffer, L"%d:%02d:%02d", hour, minute, second);
    }
    else {
        swprintf_s(buffer, L"%02d:%02d", minute, second);
    }
    return buffer;
}

std::wstring MainForm::FileNameOf(const std::wstring& filePath)
{
    return std::filesystem::path(filePath).filename().wstring();
}
