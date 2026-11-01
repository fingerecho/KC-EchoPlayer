#include "SystemVolume.h"

#include <algorithm>

SystemVolume::SystemVolume()
{
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(comResult);

    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&m_enumerator));
    if (FAILED(hr) || m_enumerator == nullptr) {
        return;
    }

    IMMDevice* device = nullptr;
    hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || device == nullptr) {
        return;
    }

    hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&m_endpoint));
    device->Release();
    if (FAILED(hr)) {
        m_endpoint = nullptr;
    }
}

SystemVolume::~SystemVolume()
{
    if (m_endpoint != nullptr) {
        m_endpoint->Release();
        m_endpoint = nullptr;
    }
    if (m_enumerator != nullptr) {
        m_enumerator->Release();
        m_enumerator = nullptr;
    }
    if (m_comInitialized) {
        ::CoUninitialize();
    }
}

float SystemVolume::GetVolume() const
{
    if (m_endpoint == nullptr) {
        return 0.0f;
    }
    float volume = 0.0f;
    if (FAILED(m_endpoint->GetMasterVolumeLevelScalar(&volume))) {
        return 0.0f;
    }
    return std::clamp(volume, 0.0f, 1.0f);
}

void SystemVolume::SetVolume(float volume)
{
    if (m_endpoint == nullptr) {
        return;
    }
    const float value = std::clamp(volume, 0.0f, 1.0f);
    m_endpoint->SetMasterVolumeLevelScalar(value, nullptr);
}

bool SystemVolume::GetMute() const
{
    if (m_endpoint == nullptr) {
        return false;
    }
    BOOL muted = FALSE;
    if (FAILED(m_endpoint->GetMute(&muted))) {
        return false;
    }
    return muted != FALSE;
}

void SystemVolume::SetMute(bool muted)
{
    if (m_endpoint == nullptr) {
        return;
    }
    m_endpoint->SetMute(muted ? TRUE : FALSE, nullptr);
}
