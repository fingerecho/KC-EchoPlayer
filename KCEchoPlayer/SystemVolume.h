#ifndef KCE_CHO_PLAYER_SYSTEM_VOLUME_H_
#define KCE_CHO_PLAYER_SYSTEM_VOLUME_H_

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

/** Windows 系统默认播放设备的音量/静音控制（Core Audio API）
 */
class SystemVolume
{
public:
    SystemVolume();
    ~SystemVolume();

    SystemVolume(const SystemVolume&) = delete;
    SystemVolume& operator = (const SystemVolume&) = delete;

public:
    bool IsAvailable() const { return m_endpoint != nullptr; }

    /** 获取系统音量，范围 [0.0, 1.0] */
    float GetVolume() const;

    /** 设置系统音量，范围 [0.0, 1.0] */
    void SetVolume(float volume);

    /** 获取系统静音状态 */
    bool GetMute() const;

    /** 设置系统静音状态 */
    void SetMute(bool muted);

private:
    bool m_comInitialized = false;
    IMMDeviceEnumerator* m_enumerator = nullptr;
    IAudioEndpointVolume* m_endpoint = nullptr;
};

#endif // KCE_CHO_PLAYER_SYSTEM_VOLUME_H_
