#ifdef _WIN32
#include "vdjVideo8.h"
#include "BlackoutRenderer.hpp"
#include "Diagnostics.hpp"

#include <cstring>

class BlackoutPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
        info->PluginName = "Blackout";
        info->Author = "Slava / OpenAI";
        info->Description = "Pure black video curtain";
        info->Version = "1.0.0";
        info->Flags = VDJFLAG_PROCESSFIRST | VDJFLAG_VIDEO_OUTPUTRESOLUTION |
                      VDJFLAG_VIDEO_MASTERONLY;
        info->Bitmap = nullptr;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }

    HRESULT VDJ_API OnDeviceInit() override {
        ID3D11Device* device = nullptr;
        if (FAILED(GetDevice(VdjVideoEngineDirectX11, reinterpret_cast<void**>(&device))) || !device) {
            Diagnostics::Error(L"Blackout did not receive a DirectX 11 device");
            return E_FAIL;
        }
        if (!renderer_.Initialize(device)) {
            Diagnostics::Error(L"Blackout renderer initialization failed");
            return E_FAIL;
        }
        Diagnostics::Info(L"Blackout renderer initialized");
        return S_OK;
    }

    HRESULT VDJ_API OnDeviceClose() override {
        renderer_.Reset();
        return S_OK;
    }

    HRESULT VDJ_API OnDraw() override {
        if (!renderer_.Draw()) {
            Diagnostics::Error(L"Blackout frame rendering failed");
            return E_FAIL;
        }
        return S_OK;
    }

private:
    BlackoutRenderer renderer_;
};

STDAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, LPVOID* object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (std::memcmp(&classId, &CLSID_VdjPlugin8, sizeof(GUID)) != 0 ||
        std::memcmp(&interfaceId, &IID_IVdjPluginVideoFx8, sizeof(GUID)) != 0) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    *object = new BlackoutPlugin();
    return S_OK;
}
#endif
