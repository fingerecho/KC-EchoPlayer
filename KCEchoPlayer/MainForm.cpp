#include "MainForm.h"

#include "logUtils.h"

#include <commdlg.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
    ui::WindowCreateParam createParam(_T("KCEchoPlayer"));
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

    auto initRows = [this](const DString& hoverPrefix, const DString& indexPrefix, int count,
                           std::vector<ui::Control*>& rows,
                           std::vector<ui::Button*>& indexes,
                           std::vector<bool>& rowHovered,
                           std::vector<bool>& iconHovered) {
        rows.assign(count, nullptr);
        indexes.assign(count, nullptr);
        rowHovered.assign(count, false);
        iconHovered.assign(count, false);
        for (int i = 0; i < count; ++i) {
            wchar_t suffix[16] = { 0 };
            swprintf_s(suffix, L"%d", i);
            DString hoverName = hoverPrefix;
            hoverName += suffix;
            DString indexName = indexPrefix;
            indexName += suffix;

            ui::Control* row = FindControl(hoverName);
            ui::Button* index = dynamic_cast<ui::Button*>(FindControl(indexName));
            rows[static_cast<size_t>(i)] = row;
            indexes[static_cast<size_t>(i)] = index;

            auto* rowHoveredPtr = &rowHovered;
            auto* iconHoveredPtr = &iconHovered;
            if (row != nullptr) {
                row->AttachMouseEnter([this, i, rowHoveredPtr](const ui::EventArgs& /*args*/) {
                    (*rowHoveredPtr)[static_cast<size_t>(i)] = true;
                    UpdateNetEaseListIndices();
                    return true;
                    });
                row->AttachMouseLeave([this, i, rowHoveredPtr, iconHoveredPtr](const ui::EventArgs& /*args*/) {
                    (*rowHoveredPtr)[static_cast<size_t>(i)] = false;
                    (*iconHoveredPtr)[static_cast<size_t>(i)] = false;
                    UpdateNetEaseListIndices();
                    return true;
                    });
            }
            if (index != nullptr) {
                index->AttachMouseEnter([this, i, iconHoveredPtr](const ui::EventArgs& /*args*/) {
                    (*iconHoveredPtr)[static_cast<size_t>(i)] = true;
                    UpdateNetEaseListIndices();
                    return true;
                    });
                index->AttachMouseLeave([this, i, iconHoveredPtr](const ui::EventArgs& /*args*/) {
                    (*iconHoveredPtr)[static_cast<size_t>(i)] = false;
                    UpdateNetEaseListIndices();
                    return true;
                    });
            }
        }
    };
    initRows(_T("ne_local_hover_"), _T("ne_local_index_"), 3,
             m_neLocalRows, m_neLocalIndexButtons, m_neLocalRowHovered, m_neLocalIconHovered);
    initRows(_T("ne_recent_hover_"), _T("ne_recent_index_"), 9,
             m_neRecentRows, m_neRecentIndexButtons, m_neRecentRowHovered, m_neRecentIconHovered);

    UpdateNetEaseListIndices();
    UpdateNetEasePlayButton();
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
        if (m_currentIndex >= 0 && m_currentIndex + 1 < static_cast<int>(m_playlist.size())) {
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
