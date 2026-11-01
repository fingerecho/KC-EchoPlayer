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

MainForm::MainForm()
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
