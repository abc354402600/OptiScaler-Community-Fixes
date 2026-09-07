#include "pch.h"
#include "menu_common.h"
#include <dlssnr/DlssNr_ExposureScan.h>

#include <algorithm>
#include <cfloat>

#include <dlssnr/DlssNr.h>

#include "input/input_system.h"

#include "font/Hack_Compressed.h"

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Streamline_Proxy.h>

#include <framegen/nvngx/Nvngx_FG.h>
#include <framegen/dlssg/MfgUnlock.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>

#include <version_check.h>

#include <upscaler_time/UpscalerTime_Vk.h>

#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <mutex>
#include <cstdarg>

#include <array>
#include <chrono>
#include <memory>
#include <type_traits>
#include <misc/IdentifyGpu.h>
#include <hooks/Xell_Hooks.h>
#include <low_latency/input/input_common.h>

enum class UiTargetMode
{
    SDR,
    LinearHDR,
    ScRGB,
    PQ,
    HLG
};

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

static float fontSize = 14.0f; // just changing this doesn't make other elements scale ideally
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];

static bool inputMenu = false;
static bool inputFG = false;
static bool inputFps = false;
static bool inputFpsCycle = false;
static uint64_t lastInputTick = 0;
constexpr uint64_t debounceThreshold = 1000;

static bool hasGamepad = false;
static bool ffxInitTried = false;
static bool xefgInitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;
static ImVec2 lastPosition(-1000.0f, -1000.0f);

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;
static std::vector<std::string> splashText = { "Cope smarter, not harder",
                                               "Coping is strong with this one...",
                                               "This is where the fun begins...",
                                               "Got any more of them scalers?...",
                                               "Fake pixels and even faker frames...",
                                               "Fake frames, get your fake frames...",
                                               "I'm here to kick pixels and chew frames...",
                                               "I find your lack of supersampling disturbing...",
                                               "Frame by frame, I scale-up!",
                                               "Resistance is futile. Your pixels will be upscaled.",
                                               "I've got 99 problems, but low-res ain't one.",
                                               "It's over, DLSS, I have the higher ground!",
                                               "This isn't the resolution you're looking for",
                                               "To infinity and beyond... with ray tracing off",
                                               "I have a bad feeling about this frame pacing",
                                               "It's Dangerous to Go Alone-Take This Upscaler",
                                               "Upscaled beyond recognition.",
                                               "Trust the process. Ignore the shimmer.",
                                               "Real fake frames. Certified.",
                                               "The illusion of performance",
                                               "This upscaler belongs in a museum!",
                                               "Because native rendering is overrated.",
                                               "The more you upscaler, the more you save",
                                               "It's never too late to buy a better GPU",
                                               "We don't need real pixels where we're going",
                                               "Did you know that Intel released XeFG for everyone?",
                                               "MFG totally works with Nukem's 100%% no scam",
                                               "Some of those pixels might even be real!",
                                               "Just don't look too closely at the image",
                                               "Even supports \"software\" XeSS!",
                                               "It's too blurry to go alone, take RCAS with you",
                                               "Thanks nitec, back to you nitec",
                                               "Tested and approved by By-U",
                                               "0.8 was an inside job",
                                               "FSR4 DP4a wenETA, AMD plz",
                                               "OptiCopers, assemble!",
                                               "The Way It's Meant To Be Upscaled",
                                               "Your game may not even crash today",
                                               "Expanded and Enhanced",
                                               "It's only my 5th crash today",
                                               "Latency with FG? But I have good internet",
                                               "Console peasants can't do that",
                                               "Hope you don't have a good eyesight",
                                               "Such an aggressive upscaling? A bold move",
                                               "I almost don't feel the input lag",
                                               "And that's how you get to 60 FPS",
                                               "Together We Upscale",
                                               "For upscalers, by upscalers",
                                               "Opti Sports, it's in the sampling",
                                               "Render in your world. Upscale in ours",
                                               "All your pixels are belong to us",
                                               "Upscaling for the masses, not the classes",
                                               "Generating discord since 2023",
                                               "Enabling DLSS since 2023",
                                               "[REDACTED] never looked better",
                                               "Free and always free",
                                               "Getting unshackled from green chains in progress...",
                                               "Who's Nukem anyway?",
                                               "Compiling shaders... ETA: 05h:49m",
                                               "Did you really just pay 70 EUR for this game?!",
                                               "Guess who forgot about a nullptr check again",
                                               "AI can't outslop this",
                                               "Guess we're pre-alpha build demos now",
                                               "New app on the block - TH",
                                               "One more stutter and I might lose it",
                                               "Mostly stable, unlike the driver",
                                               "Vul... what? ~AMD",
                                               "My 8 points are floating",
                                               "No floating here - I'm strictly between -128 and 127",
                                               "Fake it til you bake it",
                                               "Worst case just turn it off and on",
                                               "*On a generative damage control mode at geometry level*",
                                               "Deep Learning Slop Sampling 5",
                                               "2D AI filters, now powered by just 2x 5090s",
                                               "Neural Slop Sampling with DLSS5",
                                               "DLSS 5 - the way it's meant to be slopped",
                                               "Just when I think I'm out, they scale me back in",
                                               "Like going in the first gear on the highway",
                                               "Nitec's Bizarre Upscaling",
                                               "\"Framegen really attracts some strange clientelle\"",
                                               "How to remove those corny messages?!",
                                               "<Your funny text goes here>" };

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;
static CustomOptional<uint32_t> comboPreset { 0 };
static int lastKey = 0;
static bool inputDlssNr = false;
static bool capturingKey = false;

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

struct FsExistsCache
{
    std::wstring lastPath;
    bool cached { false };
    std::chrono::steady_clock::time_point nextRefresh { std::chrono::steady_clock::time_point::min() };
    std::chrono::milliseconds interval { 2000 };

    bool Get(const std::filesystem::path& path)
    {
        auto now = std::chrono::steady_clock::now();
        if (path != lastPath || now >= nextRefresh)
        {
            lastPath = path;
            cached = std::filesystem::exists(path);
            nextRefresh = now + interval;
        }
        return cached;
    }
};

static FsExistsCache nukemsExists;
static FsExistsCache enablerExists;

struct FlagDefinition
{
    std::string name;
    uint32_t mask;
    std::string description;
};

inline std::string StrFmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

void MenuCommon::UpdateManualInput(HWND targetHwnd)
{
    OptiInput::BeginFrame(targetHwnd);

    const auto config = Config::Instance();

    auto CheckShortcut = [&](int vk, bool& inputFlag, const char* logMessage)
    {
        if (inputFlag)
            return;

        if (vk <= 0 || vk >= 256)
            return;

        if (OptiInput::IsKeyReleased(vk))
        {
            lastKey = vk;
            // receivingWmInputs = false;
            inputFlag = true;
            LOG_DEBUG("{}", logMessage);
        }
    };

    const auto currentTick = GetTickCount64();
    const bool canAcceptInputs = lastInputTick + debounceThreshold < currentTick;

    if (!capturingKey && canAcceptInputs)
    {
        CheckShortcut(config->ShortcutKey.value_or_default(), inputMenu, "Menu key pressed, will be switching menu");
        CheckShortcut(config->FpsShortcutKey.value_or_default(), inputFps, "Menu key pressed, will be switching FPS");
        CheckShortcut(config->FGShortcutKey.value_or_default(), inputFG, "Menu key pressed, will be switching FG mode");
        CheckShortcut(config->FpsCycleShortcutKey.value_or_default(), inputFpsCycle,
                      "Menu key pressed, will be switching FPS mode");
        CheckShortcut(config->DlssNrToggleKey.value_or_default(), inputDlssNr,
                      "Neural Rendering key pressed, will be toggling the pass");
    }
    else if (capturingKey)
    {
        lastInputTick = currentTick;
    }

    lastKey = OptiInput::GetLastPressedKey();
}

void MenuCommon::ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::Text(tip);
        ImGui::EndTooltip();
    }
}

void MenuCommon::ShowHelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ShowTooltip(tip);
}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (ImGui::Button(buttonName.c_str()))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (!State::Instance().currentFeature)
        return;

    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
        State::Instance().newBackend = Upscaler::DLSSD;
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    auto marker = "(?) ";
    ImGui::SeparatorTextEx(0, label, ImGui::FindRenderedTextEnd(label),
                           ImGui::CalcTextSize(marker, ImGui::FindRenderedTextEnd(marker)).x);
    ShowHelpMarker(tip);
}

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    static std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
    {
        if (virtualKey == (USHORT) UnboundKey)
            return "Unbound";

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

    void Render(CustomOptional<int>& configKey)
    {
        ImGui::PushID(id);
        if (ImGui::Button(name.c_str()))
        {
            waitingForKey = true;
            capturingKey = true;
            lastKey = 0;
        }
        ImGui::PopID();

        if (waitingForKey)
        {
            ImGui::SameLine();
            ImGui::Text("Press any key...");

            if (lastKey == 0 || lastKey == VK_LBUTTON || lastKey == VK_RBUTTON || lastKey == VK_MBUTTON)
                return;

            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                capturingKey = false;
                return;
            }

            if (lastKey == VK_BACK)
                lastKey = UnboundKey;

            configKey = lastKey;
            waitingForKey = false;
            capturingKey = false;
            return;
        }

        ImGui::SameLine();
        ImGui::Text(KeyNameFromVirtualKeyCode(configKey.value_or_default()).c_str());

        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::Button("R"))
        {
            configKey.reset();
        }
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID(id + 100);
        if (ImGui::Button("해제")) // Backspace와 동일, Unbound(-1) 저장
        {
            configKey = UnboundKey;
        }
        ImGui::PopID();
    }
};

Upscaler MenuCommon::GetBackendCode(const API api)
{
    if (auto feature = State::Instance().currentFeature)
        return feature->GetUpscalerType();

    Upscaler upscaler;

    if (api == DX11)
        upscaler = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        upscaler = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        upscaler = Config::Instance()->VulkanUpscaler.value_or_default();

    return upscaler;
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

void MenuCommon::RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // Determine display name
    Upscaler targetBackend = State::Instance().newBackend;
    if (targetBackend == Upscaler::Reset)
        targetBackend = currentUpscaler;

    std::string selectedName = UpscalerDisplayName(targetBackend, api);

    if (ImGui::BeginCombo("##UpscalerCombo", selectedName.c_str()))
    {
        for (auto opt : options)
        {
            // Check if GPU is capable of a given backend
            if (opt == Upscaler::DLSS && !primaryGpu.dlssCapable)
                continue;

            // Not all Intel GPUs support native DX11 XeSS but don't think we have a good way to check exactly
            if (opt == Upscaler::XeSS && api == API::DX11 && primaryGpu.vendorId != VendorId::Intel)
                continue;

            bool isSelected = (currentUpscaler == opt);
            if (ImGui::Selectable(UpscalerDisplayName(opt, api).c_str(), isSelected))
            {
                State::Instance().newBackend = opt;
            }
        }
        ImGui::EndCombo();
    }
}

void MenuCommon::AddDx11Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX11, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR22, Upscaler::FSR31, Upscaler::XeSS_on12, Upscaler::FSR21_on12,
                          Upscaler::FSR22_on12, Upscaler::FFX_on12, Upscaler::DLSS, Upscaler::DLSS_on12 });
}

void MenuCommon::AddDx12Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX12, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::DLSS });
}

void MenuCommon::AddVulkanBackends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::Vulkan, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::FSR21_on12,
                          Upscaler::FFX_on12, Upscaler::DLSS });
}

template <HasDefaultValue B> void MenuCommon::AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value)
{
    const char* states[] = { "AUTO",
                             "COMMON",
                             "VERTEX_AND_CONSTANT_BUFFER",
                             "INDEX_BUFFER",
                             "RENDER_TARGET",
                             "UNORDERED_ACCESS",
                             "DEPTH_WRITE",
                             "DEPTH_READ",
                             "NON_PIXEL_SHADER_RESOURCE",
                             "PIXEL_SHADER_RESOURCE",
                             "STREAM_OUT",
                             "INDIRECT_ARGUMENT",
                             "COPY_DEST",
                             "COPY_SOURCE",
                             "RESOLVE_DEST",
                             "RESOLVE_SOURCE",
                             "RAYTRACING_ACCELERATION_STRUCTURE",
                             "SHADING_RATE_SOURCE",
                             "GENERIC_READ",
                             "ALL_SHADER_RESOURCE",
                             "PRESENT",
                             "PREDICATION",
                             "VIDEO_DECODE_READ",
                             "VIDEO_DECODE_WRITE",
                             "VIDEO_PROCESS_READ",
                             "VIDEO_PROCESS_WRITE",
                             "VIDEO_ENCODE_READ",
                             "VIDEO_ENCODE_WRITE" };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   310, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

static uint32_t GetPresetIndex(IFeature* feature, bool dlssd = false)
{
    auto ratio = (float) feature->TargetWidth() / (float) feature->RenderWidth();

    if (!dlssd)
    {
        if (State::Instance().dlssPresetsOverridenByOpti)
        {
            LOG_DEBUG("DLSS Presets overridden by Opti, using Opti preset indices with ratio: {}", ratio);

            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssPresetsOverriddenExternally)
        {
            LOG_DEBUG("DLSS Presets overridden externally, using external preset index: {}",
                      State::Instance().dlssRenderPresetExternal);

            return State::Instance().dlssRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssRenderPresetDLAA;
            }
        }
    }
    else
    {
        if (State::Instance().dlssdPresetsOverridenByOpti)
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssdPresetsOverriddenExternally)
        {
            return State::Instance().dlssdRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssdRenderPresetDLAA;
            }
        }
    }

    return 0;
}

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, "DEFAULT", 
            "Whatever the game uses" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            "DLSS 3.7+, a better D preset\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            "Default for Ultra Perf mode\n2nd Gen Transformers" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            "Default for Perf mode\n2nd Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            "Latest supported by the dll" }
    };
    // clang-format on

    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", "Whatever the game uses" },
        { 1, "PRESET A", "Preset A\nRemoved on recent versions!" },
        { 2, "PRESET B", "Preset B\nRemoved on recent versions!" },
        { 3, "PRESET C", "Preset C\nRemoved on recent versions!" },
        { 4, "PRESET D", "Default model, Transformer" },
        { 5, "PRESET E", "Latest Transformer model\nMust use if DoF guide is needed" },
        { 6, "PRESET F", "Latest Transformer model\nMust use if DoF guide is needed" },
        { NV_PRESET_LATEST, "Latest", "Latest supported by the dll" }
    };

    PopulateCombo(name, *value, presets);
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const std::string& name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = "Unknown";
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.label;
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), preview.c_str()))
    {
        for (const auto& opt : options)
        {
            if (opt.hidden)
                continue;

            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (ImGui::Selectable(opt.label.c_str(), isSelected))
                currentValue = opt.value;

            // Show tooltip for the individual item if it exists
            if (!opt.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", opt.tooltip.c_str());

            if (opt.disabled)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

static UiTargetMode getUiTargetMode()
{
    const auto& state = State::Instance();

    const bool fallback = !Config::Instance()->OverlayMenu.value_or_default();

    if (fallback)
    {
        // We have no swapchain information here.
        // Only classify the upscaled working image.
        if (state.currentFeature && state.currentFeature->IsHdr())
            return UiTargetMode::LinearHDR;

        return UiTargetMode::SDR;
    }

    // Normal overlay path: actual swapchain encoding is known.
    switch (state.swapchainEncoding)
    {
    case ColorEncoding::ScRGB:
        return UiTargetMode::ScRGB;

    case ColorEncoding::PQ:
        return UiTargetMode::PQ;

    case ColorEncoding::HLG:
        return UiTargetMode::HLG;

    case ColorEncoding::SDR:
    default:
        return UiTargetMode::SDR;
    }
}

static float srgbToLinear(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);

    if (x <= 0.04045f)
        return x / 12.92f;

    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

static float linearToPQ(float nits)
{
    // SMPTE ST.2084
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;

    float y = std::clamp(nits / 10000.0f, 0.0f, 1.0f);

    float ym1 = std::pow(y, m1);

    return std::pow((c1 + c2 * ym1) / (1.0f + c3 * ym1), m2);
}

static float linearToHLG(float x)
{
    // BT.2100 HLG OETF
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;

    x = std::max(x, 0.0f);

    if (x <= (1.0f / 12.0f))
        return std::sqrt(3.0f * x);

    return a * std::log(12.0f * x - b) + c;
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    const auto mode = getUiTargetMode();

    switch (mode)
    {
    case UiTargetMode::SDR:
        // Standard ImGui colors are already authored for SDR/sRGB.
        return color;

    case UiTargetMode::LinearHDR:
    {
        // Fallback mode: rendering directly into the upscaled HDR image.
        //
        // We don't know the final swapchain encoding here, so do NOT apply
        // PQ/HLG encoding. Just convert ImGui's sRGB colors to linear.
        //
        // If we later determine that the upscaled image is pre-exposed,
        // this is where the pre-exposure scale should be applied.
        constexpr float workingSpaceScale = 1.0f;

        return ImVec4(srgbToLinear(color.x) * workingSpaceScale, srgbToLinear(color.y) * workingSpaceScale,
                      srgbToLinear(color.z) * workingSpaceScale, color.w);
    }

    case UiTargetMode::ScRGB:
    {
        // scRGB is linear and uses ~80 nits for value 1.0.
        constexpr float scRgbReferenceWhiteNits = 80.0f;
        constexpr float hdrUiWhiteNits = 203.0f;

        // On SDR output keep ordinary SDR white at scRGB 1.0.
        // When HDR output is active, raise UI reference white.
        const float uiWhiteNits = State::Instance().hdrOutputActive ? hdrUiWhiteNits : scRgbReferenceWhiteNits;

        const float scale = uiWhiteNits / scRgbReferenceWhiteNits;

        return ImVec4(srgbToLinear(color.x) * scale, srgbToLinear(color.y) * scale, srgbToLinear(color.z) * scale,
                      color.w);
    }

    case UiTargetMode::PQ:
    {
        // HDR10 / ST.2084.
        //
        // ImGui colors are interpreted as SDR-relative colors where
        // 1.0 corresponds to our chosen HDR UI reference white.
        constexpr float uiWhiteNits = 203.0f;

        return ImVec4(linearToPQ(srgbToLinear(color.x) * uiWhiteNits), linearToPQ(srgbToLinear(color.y) * uiWhiteNits),
                      linearToPQ(srgbToLinear(color.z) * uiWhiteNits), color.w);
    }

    case UiTargetMode::HLG:
    {
        // HLG is relative rather than absolute-nits based.
        return ImVec4(linearToHLG(srgbToLinear(color.x)), linearToHLG(srgbToLinear(color.y)),
                      linearToHLG(srgbToLinear(color.z)), color.w);
    }

    default:
        return color;
    }
}

static void MenuHdrCheck(ImGuiIO io)
{
    if (!_hdrTonemapApplied)
    {
        ImGuiStyle& style = ImGui::GetStyle();

        CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

        // Apply tone mapping to the ImGui style
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
        {
            ImVec4 color = style.Colors[i];
            style.Colors[i] = toneMapColor(color);
        }

        _hdrTonemapApplied = true;
    }
}

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return result;
}

inline static std::string GetSourceString(UINT source)
{
    switch (source)
    {
    case 1:
        return "RTV";
    case 2:
        return "SRV";
    case 4:
        return "UAV";
    case 8:
        return "OM";
    case 16:
        return "Ups";
    case 32:
        return "SCR";
    case 64:
        return "SGR";
    default:
        return std::format("{}", source);
    }
}

inline static std::string GetDispatchString(UINT source)
{
    switch (source)
    {
    case 512:
        return "DI";
    case 1024:
        return "DII";
    case 256:
        return "Disp";
    default:
        return std::format("{}", source);
    }
}

static void ApplyThemeStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    auto conf = Config::Instance();
    bool lightTheme = conf->LightTheme.value_or_default();

    style.WindowRounding = 2.0f;
    style.ChildRounding = 1.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.FrameBorderSize = lightTheme ? 1.0f : 0.0f;
    style.TabBorderSize = lightTheme ? 1.0f : 0.0f;

    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;

    auto Clamp01 = [](float v) { return std::max(0.0f, std::min(v, 1.0f)); };

    auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
    { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

    auto Luminance = [](const ImVec4& c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };

    auto Saturate = [&](const ImVec4& color, float amount)
    {
        float lum = Luminance(color);

        return ImVec4(Clamp01(lum + (color.x - lum) * amount), Clamp01(lum + (color.y - lum) * amount),
                      Clamp01(lum + (color.z - lum) * amount), color.w);
    };

    ImVec4 accent = ImVec4(conf->MenuAccentColorR.value_or_default(), conf->MenuAccentColorG.value_or_default(),
                           conf->MenuAccentColorB.value_or_default(), 1.0f);

    ImVec4 bgAccent = ImVec4(conf->MenuBGColorR.value_or_default(), conf->MenuBGColorG.value_or_default(),
                             conf->MenuBGColorB.value_or_default(), 1.0f);

    float luminance = Luminance(accent);

    const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

    const ImVec4 textPrimary = lightTheme ? ImVec4(0.05f, 0.06f, 0.08f, 1.00f) : ImVec4(0.90f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textDim = lightTheme ? ImVec4(0.22f, 0.25f, 0.31f, 1.00f) : ImVec4(0.54f, 0.58f, 0.62f, 1.00f);

    const ImVec4 borderCol = lightTheme ? ImVec4(0.35f, 0.40f, 0.50f, 1.00f) : ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    const ImVec4 dimBg = lightTheme ? ImVec4(0.30f, 0.33f, 0.38f, 0.20f) : ImVec4(0.09f, 0.10f, 0.13f, 0.20f);
    const ImVec4 modalDimBg = lightTheme ? ImVec4(0.22f, 0.24f, 0.28f, 0.55f) : ImVec4(0.04f, 0.04f, 0.07f, 0.55f);

    // MenuBGColor: only background/surface tint.
    auto BgTint = [&](const ImVec4& base, float strength = 1.0f, float alpha = 1.0f)
    {
        float t = lightTheme ? (0.180f * strength) : (0.120f * strength);
        return Mix(base, bgAccent, t, alpha);
    };

    // MenuAccentColor: all visible interactive accent colors.
    auto AccentSoft = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.14f, alpha) : Mix(bgDark, accent, 0.32f, alpha); };

    auto AccentMed = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha); };

    auto AccentStrong = [&](float alpha = 1.0f) { return ImVec4(accent.x, accent.y, accent.z, alpha); };

    const ImVec4 bgTitle = AccentSoft();

    auto SurfaceHover = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.12f, alpha) : Mix(bgLight, accent, 0.18f, alpha); };

    auto SurfaceActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.20f, alpha) : Mix(bgLight, accent, 0.28f, alpha); };

    auto TitleActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgTitle, accent, 0.18f, alpha) : Mix(bgTitle, accent, 0.16f, alpha); };

    auto PlotAccent = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            // Darken slightly for contrast on light bg — no channel floors
            return Mix(accent, ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.20f, alpha);
        }

        // Brighten slightly for visibility on dark bg — no channel floors
        return Mix(accent, ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.35f, alpha);
    };

    auto PlotAccentHovered = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            return Mix(PlotAccent(alpha), ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.15f, alpha);
        }

        return Mix(PlotAccent(alpha), ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.25f, alpha);
    };

    auto AccentReadable = [&](float alpha = 1.0f)
    {
        // Apply saturation boost and luminance correction only here,
        // so AccentStrong / AccentMed / AccentSoft stay true to the user's pick.
        ImVec4 a = Saturate(accent, lightTheme ? 1.35f : 1.25f);
        float lum = Luminance(a);

        if (lightTheme && lum > 0.72f)
            a = Mix(a, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f, 1.0f);

        if (!lightTheme && lum < 0.25f)
            a = Mix(a, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.30f, 1.0f);

        return ImVec4(a.x, a.y, a.z, alpha);
    };

    ImVec4* c = ImGui::GetStyle().Colors;

    float minAlpha = Config::Instance()->MenuBGColorA.value_or_default() >= 0.5f
                         ? Config::Instance()->MenuBGColorA.value_or_default()
                         : 0.5f;

    c[ImGuiCol_Text] = textPrimary;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_TextLink] = AccentReadable();

    // MenuBGColor only.
    c[ImGuiCol_WindowBg] = BgTint(bgDark, 1.00f, Config::Instance()->MenuBGColorA.value_or_default());
    c[ImGuiCol_ChildBg] = BgTint(bgMid, 1.10f, minAlpha + 0.1f);
    c[ImGuiCol_PopupBg] =
        lightTheme ? BgTint(bgLight, 0.90f) : BgTint(ImVec4(0.09f, 0.10f, 0.13f, 0.97f), 0.90f, 0.97f);
    c[ImGuiCol_MenuBarBg] = BgTint(bgDark, 0.85f);
    c[ImGuiCol_DockingEmptyBg] = BgTint(bgDark, 0.75f);

    c[ImGuiCol_Border] = borderCol;
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Neutral background, not MenuBGColor.
    c[ImGuiCol_FrameBg] = BgTint(bgLight, 0.50f, minAlpha + 0.15f);
    c[ImGuiCol_FrameBgHovered] = SurfaceHover();
    c[ImGuiCol_FrameBgActive] = SurfaceActive();

    c[ImGuiCol_TitleBg] = BgTint(bgTitle, 0.40f);
    c[ImGuiCol_TitleBgActive] = TitleActive();
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(bgTitle.x, bgTitle.y, bgTitle.z, 0.75f);

    c[ImGuiCol_ScrollbarBg] = BgTint(bgDark, 0.60f, minAlpha + 0.2f);
    c[ImGuiCol_ScrollbarGrab] = AccentSoft();
    c[ImGuiCol_ScrollbarGrabHovered] = AccentMed();
    c[ImGuiCol_ScrollbarGrabActive] = AccentStrong();

    c[ImGuiCol_CheckMark] = AccentReadable();
    c[ImGuiCol_SliderGrab] = AccentMed();
    c[ImGuiCol_SliderGrabActive] = AccentReadable();
    c[ImGuiCol_InputTextCursor] = AccentReadable();

    c[ImGuiCol_Button] = AccentSoft();
    c[ImGuiCol_ButtonHovered] = AccentMed();
    c[ImGuiCol_ButtonActive] = AccentStrong();

    c[ImGuiCol_Header] = AccentSoft(0.90f);
    c[ImGuiCol_HeaderHovered] = AccentMed(0.95f);
    c[ImGuiCol_HeaderActive] = AccentStrong();

    c[ImGuiCol_Separator] = borderCol;
    c[ImGuiCol_SeparatorHovered] = AccentMed(0.85f);
    c[ImGuiCol_SeparatorActive] = AccentStrong();

    c[ImGuiCol_ResizeGrip] = AccentSoft(0.30f);
    c[ImGuiCol_ResizeGripHovered] = AccentStrong(0.70f);
    c[ImGuiCol_ResizeGripActive] = AccentStrong(0.95f);

    c[ImGuiCol_Tab] = AccentSoft();
    c[ImGuiCol_TabHovered] = AccentMed();
    c[ImGuiCol_TabSelected] = AccentSoft();
    c[ImGuiCol_TabSelectedOverline] = AccentStrong();
    c[ImGuiCol_TabDimmed] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabDimmedSelected] = AccentSoft(0.75f);
    c[ImGuiCol_TabDimmedSelectedOverline] = borderCol;

    c[ImGuiCol_DockingPreview] = AccentStrong(0.70f);

    c[ImGuiCol_PlotLines] = PlotAccent();
    c[ImGuiCol_PlotLinesHovered] = PlotAccentHovered();
    c[ImGuiCol_PlotHistogram] = PlotAccent(0.85f);
    c[ImGuiCol_PlotHistogramHovered] = PlotAccentHovered();

    c[ImGuiCol_TableHeaderBg] = BgTint(bgMid, 0.80f, minAlpha + 0.25f);
    c[ImGuiCol_TableBorderStrong] = borderCol;
    c[ImGuiCol_TableBorderLight] = lightTheme ? ImVec4(0.68f, 0.72f, 0.80f, 1.00f) : AccentSoft();
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = lightTheme ? ImVec4(0.00f, 0.00f, 0.00f, 0.045f) : ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    c[ImGuiCol_TreeLines] = borderCol;
    c[ImGuiCol_TextSelectedBg] = AccentMed(0.38f);
    c[ImGuiCol_DragDropTarget] = AccentStrong(0.90f);
    c[ImGuiCol_NavCursor] = AccentReadable();
    c[ImGuiCol_NavWindowingHighlight] = AccentStrong(0.70f);
    c[ImGuiCol_NavWindowingDimBg] = dimBg;
    c[ImGuiCol_ModalWindowDimBg] = modalDimBg;

    _hdrTonemapApplied = false;
    MenuHdrCheck(ImGui::GetIO());
}

static double lastTime = 0.0;
static double lastFrameTime = 0.0;
static UINT64 uwpTargetFrame = 0;

void MenuCommon::Present()
{
    _frameCount++;

    auto now = Util::MillisecondsNow();

    if (lastTime > 0.0)
        lastFrameTime = now - lastTime;

    lastTime = now;

    if (_handle != nullptr)
        UpdateManualInput(_handle);
}

struct VersionCheckStatus
{
    bool completed = false;
    bool updateAvailable = false;
    std::string latestTag;
    std::string latestUrl;
    std::string error;
};

struct MenuCommon::RenderMenuContext
{
    State& state;
    decltype(Config::Instance()) config;
    ImGuiIO& io;
    IFeature* currentFeature = nullptr;

    double now = 0.0;
    double frameTime = 0.0;
    double frameRate = 0.0;
    float menuResScale = 1.0f;
    float fpsScale = 1.0f;
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    bool frameTimesCalculated = false;
    bool newFrame = false;

    VersionCheckStatus versionStatus;
    std::string currentVersionText;

    // Cached when the menu is visible and shared by RenderMainMenuWindow section helpers.
    std::unique_ptr<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>> primaryGpu;
};

static std::string splashMessage;

void MenuCommon::UpdateRenderTiming(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (config->OverlayMenu.value_or_default())
    {
        _frameCount++;

        // FPS & frame time calculation
        if (lastTime > 0.0)
        {
            frameTime = now - lastTime;
            frameRate = 1000.0 / frameTime;
        }

        lastTime = now;

        if (_handle != nullptr)
            UpdateManualInput(_handle);
    }
    else
    {
        if (state.activeFgInput == FGInput::NoFG || state.activeFgOutput == FGOutput::NoFG)
            MenuCommon::Present();

        frameTime = lastFrameTime;
        frameRate = 1000.0 / frameTime;
    }

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);
}

void MenuCommon::UpdateMenuInputMode(RenderMenuContext& ctx)
{
    auto& io = ctx.io;

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        capturingKey = false;
        hasGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }
}

void MenuCommon::HandleMenuShortcuts(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    // Handle Inputs
    {
        if (inputFG)
        {
            inputFG = false;

            if (state.activeFgInput != FGInput::NoFG && state.activeFgOutput != FGOutput::NoFG &&
                (state.currentFGSwapchain != nullptr || state.activeFgInput == FGInput::NvngxFG))
            {
                config->FGEnabled = !config->FGEnabled.value_or_default();
                LOG_DEBUG("FG toggle key pressed, setting FGEnabled to {}", config->FGEnabled.value_or_default());

                if (config->FGEnabled.value_or_default())
                    state.fgChanged = true;
            }
        }

        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputDlssNr)
        {
            inputDlssNr = false;
            config->DlssNrEnabled = !config->DlssNrEnabled.value_or_default();
            LOG_DEBUG("Neural Rendering toggle key pressed, setting DlssNrEnabled to {}",
                      config->DlssNrEnabled.value_or_default());

            ImGuiToast toast { ImGuiToastType::Info, 2000 };
            toast.setTitle("DLSS Neural Rendering");
            toast.setContent(config->DlssNrEnabled.value_or_default() ? "On" : "Off");
            ImGui::InsertNotification(toast);
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();

                OptiInput::ResetMenuInputTransientState();

                ApplyThemeStyle();

                refreshRate = Util::GetActiveRefreshRate(_handle);

                auto optiPath = std::filesystem::path(Config::Instance()->MainDllPath.value());
                state.artursFgFileAvailable = enablerExists.Get(optiPath / L"dlss-enabler-headless.dll");
                state.nukemsFgFileAvailable = nukemsExists.Get(optiPath / L"dlssg_to_fsr3_amd_is_better.dll");

                if (State::Instance().currentFeature != nullptr)
                {
                    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
                        comboPreset = config->DLSSDRenderPresetForAll.value_or_default();
                    else if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSS)
                        comboPreset = config->RenderPresetForAll.value_or_default();
                }
            }
            else
            {
                ImGui::CloseCurrentPopup();

                _showMipmapCalcWindow = false;
                _showHudlessWindow = false;
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }
}

void MenuCommon::UpdateVersionAndStartupNotifications(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& versionStatus = ctx.versionStatus;

    constexpr double splashTime = 7000.0;
    constexpr int updateNoticeTime = 10000;

    // Version check state is copied while locked, then consumed by the UI render pass.
    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    ctx.currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle("OptiScaler Update available");
                updateNotification.setContent(
                    "Press %s for more info",
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    // One-shot startup warning notifications.
    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Late Streamline hook detected");
            notification.setContent(
                "Consider renaming OptiScaler from %s to other supported name.\nYou may experience issues otherwise.",
                filename.c_str());
            ImGui::InsertNotification(notification);
        }

        if (state.postCodes & PostCode::TryingFsr4Fp8OnUnsupported)
        {
            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Silly goose detected");
            notification.setContent("FSR 4 FP8 only works on AMD");
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }

    // Initialize splash timing and select the splash text once per process.
    if (splashLimit < 1.0f)
    {
        splashStart = now + 100.0;
        splashLimit = splashStart + splashTime;

        std::srand(static_cast<unsigned>(std::time(nullptr)));
        splashMessage = splashText[std::rand() % splashText.size()];
    }
}

void MenuCommon::BeginMenuFrameIfNeeded(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;
    auto& newFrame = ctx.newFrame;

    // New frame check
    // The lamp is drawn while the menu is closed, which is the whole point of it. Tied to its own
    // setting and nothing else: an overlay that appears because a scan is running, rather than
    // because someone asked for it, is an overlay nobody asked for.
    const bool scanIndicator = config->DlssNrScanMeter.value_or_default() &&
                               DlssNr::ExposureScan::Where() != DlssNr::ExposureScan::Verdict::Off;

    if ((!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit) ||
        config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0 || scanIndicator ||
        (config->DlssNrCompare.value_or_default() != 0 && config->DlssNrCompareTags.value_or_default()))
    {
        if (!_isUWP)
        {
            ImGui_ImplWin32_NewFrame();
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        OptiInput::FeedImGui(_isVisible);

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }
}

void MenuCommon::RenderSplashWindow(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;

    constexpr double fadeTime = 1000.0;

    // Splash screen
    if (!config->DisableSplash.value_or_default())
    {
        if (now > splashStart && now < splashLimit)
        {

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

            if (!config->OverlaysUseTheme.value_or_default())
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            }

            if (ImGui::Begin("Splash", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav))
            {
                float splashScale = 1.0f;
                float baseScaleHeight = 720.0f;

                if (io.DisplaySize.y > baseScaleHeight)
                    splashScale = io.DisplaySize.y / baseScaleHeight;

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text("OptiScaler - %s for menu",
                            Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), splashMessage.c_str());

                splashSize = ImGui::GetWindowSize();

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            if (!config->OverlaysUseTheme.value_or_default())
                ImGui::PopStyleColor(4);
            else
                ImGui::PopStyleColor(2);

            ImGui::PopStyleVar(2);
        }
    }
}

void MenuCommon::RenderNotifications(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Notifications
    bool tonemapRequired =
        (State::Instance().hdrOutputActive && State::Instance().swapchainEncoding != ColorEncoding::SDR) ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr());

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void MenuCommon::UpdateFrameTimeAverages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // FPS Overlay font
    fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    averageFrameTime = 0.0f;
    averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();
    }
}

// Labels for the comparison views.
//
// Drawn straight onto the foreground draw list, not as ImGui windows -- the last attempt made them
// draggable windows and the clamping fought the split. Here each label is clipped to its own side of
// the comparison, so in the wipe the moving split reveals and hides it exactly as it does the
// pictures, and there is nothing to drag. Both wipe labels sit in the same top-left corner, each
// clipped to its side, so whichever picture currently owns that corner is the one whose label shows.
void MenuCommon::RenderNrCompareTags()
{
    auto* config = Config::Instance();

    const uint32_t mode = config->DlssNrCompare.value_or_default();

    if (mode == 0 || !config->DlssNrCompareTags.value_or_default())
        return;

    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    if (screen.x < 1.0f || screen.y < 1.0f)
        return;

    const bool swap = config->DlssNrCompareSwap.value_or_default();
    const float split = mode == 1 ? 0.5f
                                  : std::clamp(config->DlssNrCompareSplit.value_or_default(), 0.0f, 1.0f);
    const float splitX = split * screen.x;

    const float scale = std::clamp(config->DlssNrTagScale.value_or_default(), 0.5f, 5.0f);

    // The left side is the untouched frame unless swapped -- matching the shader's
    // showOriginal = (uv.x < split) != swap.
    const char* leftText = swap ? "DLSS NR : ON" : "DLSS NR : OFF";
    const char* rightText = swap ? "DLSS NR : OFF" : "DLSS NR : ON";

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const float margin = 10.0f * scale;

    // Both labels flank the divider along the top: the left picture's label is right-aligned just
    // left of the split, the right picture's is left-aligned just right of it. Each is clipped to its
    // own side, so in the wipe the split reveals and hides them along with the images.
    auto drawTag = [&](const char* text, float x, ImVec2 clipMin, ImVec2 clipMax)
    {
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);

        // Never let a label run off the visible frame as it grows.
        x = std::min(std::max(x, 0.0f), screen.x - size.x);
        float y = std::min(margin, screen.y - size.y - margin);
        y = std::max(y, 0.0f);

        dl->PushClipRect(clipMin, clipMax, true);
        dl->AddText(font, fontSize, ImVec2(x + 2.0f, y + 2.0f), IM_COL32(0, 0, 0, 210), text);
        dl->AddText(font, fontSize, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text);
        dl->PopClipRect();
    };

    const ImVec2 leftSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, leftText);

    // Left picture's label: right edge a margin in from the split. Right picture's: left edge a margin
    // out from the split.
    drawTag(leftText, splitX - margin - leftSize.x, ImVec2(0.0f, 0.0f), ImVec2(splitX, screen.y));
    drawTag(rightText, splitX + margin, ImVec2(splitX, 0.0f), ImVec2(screen.x, screen.y));
}

void MenuCommon::RenderPerformanceOverlay(RenderMenuContext& ctx)
{
    RenderNrCompareTags();


    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green));
        }

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (IdentifyGpu::getPrimaryGpu().usesDxvk && state.api == DX11)
            {
                if (state.swapchainInteropApi == SwapchainInteropApi::None)
                    api = "DXVK";
                else
                    api = "DXVK w/Dx12";
            }
            else if (IdentifyGpu::getPrimaryGpu().usesVkd3dProton && state.api == DX12)
            {
                api = "VKD3D";
            }
            else
            {
                switch (state.swapchainApi)
                {
                case Vulkan:
                    api = "VLK";
                    break;

                case DX11:
                    api = "D3D11";
                    break;

                case DX12:
                    if (state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12)
                        api = "D3D11 w/DX12";
                    else
                        api = "D3D12";

                    break;

                default:
                    switch (state.api)
                    {
                    case Vulkan:
                        api = "VLK";
                        break;

                    case DX11:
                        api = "D3D11";
                        break;

                    case DX12:
                        api = "D3D12";
                        break;

                    default:
                        api = "???";
                        break;
                    }

                    break;
                }
            }

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            auto fg = state.currentFG;
            auto fgText = (fg != nullptr && fg->IsActive() && !fg->IsPaused()) ? (" (" + std::string(fg->Name()) + ")")
                                                                               : std::string();

            const int fakeFramesCount = state.dlssgDetectedInterpolationCount;
            auto formatFg = [&](std::string_view name, int maxFakeFrames)
            {
                if (fakeFramesCount > maxFakeFrames)
                    return std::format(" ({} Doesn't support more than {}x)", name, maxFakeFrames);

                else if (fakeFramesCount == 0)
                    return std::format(" ({} off)", name);

                return std::format(" ({} x{})", name, fakeFramesCount + 1);
            };

            const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
            if (activeNvngxFg == FGNvngxReplacement::Arturs)
            {
                fgText = formatFg("Enabler", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                fgText = formatFg("Nukems", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::FFX)
            {
                fgText = formatFg("FFX", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Combo)
            {
                fgText = formatFg("Combo", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (state.activeFgOutput == FGOutput::DLSSG && fg)
            {
                fgText = formatFg("DLSSG", fg->GetMaxInterpolationCount());
            }

            const auto overlayType = config->FpsOverlayType.value_or_default();
            const bool hasFeature = currentFeature && !currentFeature->IsFrozen();

            // Prepare Line 1
            std::string featurePart;
            std::string fpsPart;

            if (hasFeature)
            {
                const bool usesDx12CompatLayer = currentFeature->IsWithDx12();

                featurePart = StrFmt(" | %s -> %s %u.%u.%u%s", ApiUpscalerInputName(state.currentInputApiName).c_str(),
                                     currentFeature->ShortName().c_str(), currentFeature->Version().major,
                                     currentFeature->Version().minor, currentFeature->Version().patch,
                                     usesDx12CompatLayer ? " w/Dx12" : "");
            }

            if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
            {
                const double baseFps = frameRate / (double) (fg->GetInterpolatedFrameCount() + 1);

                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f/%5.1f ", frameRate, baseFps);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, %7.2f ms", frameRate, baseFps, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, Avg: %6.1f", frameRate, baseFps, 1000.0f / averageFrameTime);
                    break;
                }
            }
            else
            {
                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f ", frameRate);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f, %7.2f ms", frameRate, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f, Avg: %6.1f", frameRate, 1000.0f / averageFrameTime);
                    break;
                }
            }

            if (overlayType == FpsOverlay_JustFPS)
                firstLine = StrFmt("%s", fpsPart.c_str());
            else
                firstLine = StrFmt("%s | %s%s%s", api.c_str(), fpsPart.c_str(), fgText.c_str(), featurePart.c_str());

            // Prepare Line 2
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt("Frame Time: %7.2f ms, Avg: %7.2f ms", state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                thirdLine =
                    StrFmt("Upscaler Time: %7.2f ms, Avg: %7.2f ms", state.upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            // Draw the overlay
            ImGui::Text(firstLine.c_str());

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_DetailedGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_FullGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_ReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;

#ifdef LOW_LATENCY_INPUTS
                static TimingData timingData {};

                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = InputCommon::get_timing_data(timingData);
                    previousPoll = now;
                }

                if (gotData && timingData.timeRange.has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData.timeRange.value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Low latency timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](const auto& timingOpt, const char* desc, ImVec4 color)
                    {
                        if (!timingOpt.has_value())
                            return;

                        auto toneMappedColor = State::Instance().isHdrActive ? toneMapColor(color) : color;

                        const auto& timing = timingOpt.value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);

                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);

                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;

                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);

                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);

                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);

                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(timingData.simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(timingData.renderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(timingData.present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(timingData.driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(timingData.osRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(timingData.gpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#else
                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Reflex timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = toneMapColor(color);

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);
                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#endif
            }
        }

        // Restore the style
        if (!config->OverlaysUseTheme.value_or_default())
            ImGui::PopStyleColor(5);
        else
            ImGui::PopStyleColor(2);

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_BottomLeft)
        {
            overlayPosition.x = 0;
        }
        else
        {
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;
        }

        // Top / Bottom
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopRight)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }
}

void MenuCommon::RenderMainMenuHeaderMessages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& versionStatus = ctx.versionStatus;
    auto& currentVersionText = ctx.currentVersionText;
    auto& primaryGpu = *ctx.primaryGpu;

    if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        ImGui::SetWindowFocus();

    if (config->MenuScale.has_value())
    {
        _selectedScale = ((int) (menuResScale * 10.0f)) - 4;
    }
    else
    {
        _selectedScale = 0;
    }

    if (versionStatus.completed)
    {
        if (versionStatus.updateAvailable && !versionStatus.latestTag.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Update available: %s (current %s)",
                               versionStatus.latestTag.c_str(), currentVersionText.c_str());

            if (!versionStatus.latestUrl.empty())
            {
                ImGui::SameLine();
                ImGui::TextLinkOpenURL("Open release page", versionStatus.latestUrl.c_str());
            }

            ImGui::Spacing();
        }
        else if (!versionStatus.error.empty())
        {
            LOG_ERROR("Version check failed: {0}", versionStatus.error);
            versionStatus.error.clear();
        }
        // Disabled error message
        // else if (!versionStatus.error.empty())
        //{
        //    ImGui::Spacing();
        //    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.4f, 0.f, 1.f)), "%s", versionStatus.error.c_str());
        //    ImGui::Spacing();
        //}
    }

    // No active upscaler message
    if (currentFeature == nullptr || !currentFeature->IsInited())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 2.5f));
        else
            ImGui::SetWindowFontScale(menuResScale * 2.5f);

        if (state.nvngxExists || state.nvngxReplacement.has_value() ||
            (state.libxessExists || XeSSProxy::Module() != nullptr))
        {
            ImGui::Spacing();

            std::vector<std::string> upscalers;

            if (state.fsrHooks)
                upscalers.push_back("FSR");

            if (state.nvngxExists || state.nvngxReplacement.has_value() || primaryGpu.dlssCapable)
                upscalers.push_back("DLSS");

            if (state.libxessExists || XeSSProxy::Module() != nullptr)
                upscalers.push_back("XeSS");

            auto joined = upscalers | std::views::join_with(std::string { " or " });

            std::string joinedUpscalers(joined.begin(), joined.end());

            ImGui::Text("Please select %s as upscaler from game\noptions and load a save game "
                        "to enable Opti settings.\nUpscalers don't always work in menus.",
                        joinedUpscalers.c_str());

            if (config->UseHQFont.value_or_default())
                ImGui::PopFontSize();
            else
                ImGui::SetWindowFontScale(menuResScale);

            ImGui::Spacing();

            if (primaryGpu.dlssCapable)
            {
                ImGui::Text("nvngx_dlss : %s", state.NVNGX_DLSS_Path.has_value() ? "Exists" : "Doesn't Exist");
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::Text("nvngx_dlssd : %s", state.NVNGX_DLSSD_Path.has_value() ? "Exists" : "Doesn't Exist");
            }
            else
            {
                ImGui::Text("nvngx.dll: %s", state.nvngxExists ? "Exists" : "Doesn't Exist");
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::Text("nvngx replacement: %s", state.nvngxReplacement.has_value() ? "Exists" : "Doesn't Exist");
            }

            ImGui::Text("libxess: %s",
                        (state.libxessExists || XeSSProxy::Module() != nullptr) ? "Exists" : "Doesn't Exist");

            ImGui::Text("FSR Hooks: %s", state.fsrHooks ? "Exist" : "Don't Exist");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("FSR 3.1: %s", FfxApiProxy::Dx12Module() != nullptr ? "Exists" : "Doesn't Exist");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("FSR 3.1 SR: %s", FfxApiProxy::Dx12Module_SR() != nullptr ? "Exists" : "Doesn't Exist");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("FSR 3.1 FG: %s", FfxApiProxy::Dx12Module_FG() != nullptr ? "Exists" : "Doesn't Exist");

            ImGui::Spacing();
        }
        else
        {
            ImGui::Spacing();
            ImGui::Text("Can't find nvngx.dll and libxess.dll and FSR inputs\nUpscaling support will NOT work.");
            ImGui::Spacing();

            if (config->UseHQFont.value_or_default())
                ImGui::PopFont();
            else
                ImGui::SetWindowFontScale(menuResScale);
        }
    }
    else if (currentFeature->IsFrozen())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 3.0f));
        else
            ImGui::SetWindowFontScale(menuResScale * 3.0f);

        ImGui::Text("%s is active, but not currently used by the game\nPlease enter the game",
                    currentFeature->Name().c_str());

        if (config->UseHQFont.value_or_default())
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(menuResScale);
    }
}

void MenuCommon::RenderActiveUpscalerSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // UPSCALERS -----------------------------
        ImGui::SeparatorText("Upscalers");
        ShowTooltip("Which copium do you choose?");

        GetCurrentBackendInfo(state.api, currentBackend, &currentBackendName);

        std::string spoofingText;

        ImGui::PushItemWidth(180.0f * menuResScale);

        const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
        const bool usesDx12CompatLayer = currentFeature->IsWithDx12();

        switch (state.api)
        {
        case DX11:
            ImGui::Text(primaryGpu.name.c_str());

            ImGui::Text("D3D11 %s| %s %d.%d.%d%s", primaryGpu.usesDxvk ? "(DXVK) " : "",
                        currentFeature->ShortName().c_str(), currentFeature->Version().major,
                        currentFeature->Version().minor, currentFeature->Version().patch,
                        usesDx12CompatLayer ? " w/Dx12" : "");
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text("| Input: %s", ApiUpscalerInputName(state.currentInputApiName).c_str());

            ImGui::SameLine(0.0f, 6.0f);
            spoofingText = config->DxgiSpoofing.value_or_default() ? "On" : "Off";
            ImGui::Text("| Spoof: %s", spoofingText.c_str());

            if (!usesDlssd)
                AddDx11Backends(currentBackend);

            break;

        case DX12:
            ImGui::Text(primaryGpu.name.c_str());

            ImGui::Text("D3D12 %s| %s %d.%d.%d", primaryGpu.usesDxvk ? "(DXVK) " : "",
                        currentFeature->ShortName().c_str(), currentFeature->Version().major,
                        currentFeature->Version().minor, currentFeature->Version().patch);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text("| Input: %s", ApiUpscalerInputName(state.currentInputApiName).c_str());

            ImGui::SameLine(0.0f, 6.0f);
            spoofingText = config->DxgiSpoofing.value_or_default() ? "On" : "Off";
            ImGui::Text("| Spoof: %s", spoofingText.c_str());

            if (!usesDlssd)
                AddDx12Backends(currentBackend);

            break;

        default:
            ImGui::Text(primaryGpu.name.c_str());

            ImGui::Text("Vulkan %s| %s %d.%d.%d%s", primaryGpu.usesDxvk ? "(DXVK) " : "",
                        currentFeature->ShortName().c_str(), currentFeature->Version().major,
                        currentFeature->Version().minor, currentFeature->Version().patch,
                        usesDx12CompatLayer ? " w/Dx12" : "");
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text("| Input: %s", ApiUpscalerInputName(state.currentInputApiName).c_str());

            auto vlkSpoof = config->VulkanSpoofing.value_or_default();
            auto vlkExtSpoof = config->VulkanExtensionSpoofing.value_or_default();

            if (vlkSpoof && vlkExtSpoof)
                spoofingText = "On + Ext";
            else if (vlkSpoof)
                spoofingText = "On";
            else if (vlkExtSpoof)
                spoofingText = "Just Ext";
            else
                spoofingText = "Off";

            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text("| Spoof: %s", spoofingText.c_str());

            if (!usesDlssd)
                AddVulkanBackends(currentBackend);
        }

        ImGui::PopItemWidth();

        if (!usesDlssd)
        {
            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::Button("Change Upscaler##2") && state.newBackend != Upscaler::Reset &&
                state.newBackend != currentBackend)
            {
                if (state.newBackend == Upscaler::XeSS)
                {
                    // Reseting them for xess
                    config->DisableReactiveMask.reset();
                    config->DlssReactiveMaskBias.reset();
                }

                MARK_ALL_BACKENDS_CHANGED();
            }
        }

        if (currentFeature->AccessToReactiveMask())
        {
            ImGui::BeginDisabled(config->DisableReactiveMask.value_or(false));

            auto useAsTransparency = config->FsrUseMaskForTransparency.value_or_default();
            if (ImGui::Checkbox("Use Reactive Mask as Transparency Mask", &useAsTransparency))
                config->FsrUseMaskForTransparency = useAsTransparency;

            ImGui::EndDisabled();
        }

        if (primaryGpu.dlssCapable && !state.NVNGX_DLSS_Path.has_value())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "nvngx_dlss.dll not found, DLSS disabled!");
        }
    }

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;

        // Dx11 with Dx12
        if (state.api == DX11 && currentFeature->IsWithDx12())
        {
            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Dx11 with Dx12 Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (bool dontUseNTShared = config->DontUseNTShared.value_or_default();
                    ImGui::Checkbox("Don't Use NTShared", &dontUseNTShared))
                    config->DontUseNTShared = dontUseNTShared;

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        if (state.api == Vulkan && currentFeature->IsWithDx12())
        {
            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Vulkan with Dx12 Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (bool inputsUseCopy = config->VulkanUseCopyForInputs.value_or_default();
                    ImGui::Checkbox("Use CopyResource for Inputs", &inputsUseCopy))
                    config->VulkanUseCopyForInputs = inputsUseCopy;

                if (bool outputUseCopy = config->VulkanUseCopyForOutput.value_or_default();
                    ImGui::Checkbox("Use CopyResource for Output", &outputUseCopy))
                    config->VulkanUseCopyForOutput = outputUseCopy;

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        // UPSCALER SPECIFIC -----------------------------

        // XeSS -----------------------------
        if (currentBackend == Upscaler::XeSS && !usesDlssd)
        {
            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("XeSS Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                const char* models[] = { "KPSS", "SPLAT", "MODEL_3", "MODEL_4", "MODEL_5", "MODEL_6" };
                auto configModes = config->NetworkModel.value_or_default();

                if (configModes < 0 || configModes > 5)
                    configModes = 0;

                const char* selectedModel = models[configModes];

                if (ImGui::BeginCombo("Network Models", selectedModel))
                {
                    for (int n = 0; n < 6; n++)
                    {
                        if (ImGui::Selectable(models[n], (config->NetworkModel.value_or_default() == n)))
                        {
                            config->NetworkModel = n;
                            state.newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }

                    ImGui::EndCombo();
                }
                ShowHelpMarker("큰 효과는 없을 가능성이 높습니다");

                if (bool dbg = state.xessDebug; ImGui::Checkbox("Dump (Shift+Del)", &dbg))
                    state.xessDebug = dbg;

                ImGui::SameLine(0.0f, 6.0f);
                int dbgCount = state.xessDebugFrames;

                ImGui::PushItemWidth(95.0f * menuResScale);
                if (ImGui::InputInt("frames", &dbgCount))
                {
                    if (dbgCount < 4)
                        dbgCount = 4;
                    else if (dbgCount > 999)
                        dbgCount = 999;

                    state.xessDebugFrames = dbgCount;
                }

                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        // FFX -----------------
        if (!usesDlssd && (currentBackend == Upscaler::FFX || currentBackend == Upscaler::FFX_on12))
        {
            ImGui::SeparatorText("FFX Settings");

            if (_ffxUpscalerIndex < 0)
                _ffxUpscalerIndex = config->FfxUpscalerIndex.value_or_default();

            if (currentBackend == Upscaler::FFX ||
                currentBackend == Upscaler::FFX_on12 && state.ffxUpscalerVersionNames.size() > 0)
            {
                ImGui::PushItemWidth(135.0f * menuResScale);

                auto currentName = StrFmt("FSR %s", state.ffxUpscalerVersionNames[_ffxUpscalerIndex]);
                if (ImGui::BeginCombo("FFX Upscaler", currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxUpscalerVersionIds.size(); n++)
                    {
                        auto name = StrFmt("FSR %s##%d", state.ffxUpscalerVersionNames[n], n);
                        if (ImGui::Selectable(name.c_str(), config->FfxUpscalerIndex.value_or_default() == n))
                            _ffxUpscalerIndex = n;
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowHelpMarker("FFX SDK가 보고한 업스케일러 목록");

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Change Upscaler") &&
                    _ffxUpscalerIndex != config->FfxUpscalerIndex.value_or_default())
                {
                    config->FfxUpscalerIndex = _ffxUpscalerIndex;
                    state.newBackend = currentBackend;
                    MARK_ALL_BACKENDS_CHANGED();
                }

                auto majorFsrVersion = currentFeature->Version().major;

                if (majorFsrVersion >= 4)
                {
                    ImGui::Spacing();

                    // Colorspaces
                    const char* colorSpaces[] = { "Linear (Default)", "Non-Linear", "Non-Linear sRGB",
                                                  "Non-Linear PQ" };
                    int currentColorSpace = 0;
                    if (config->FsrNonLinearPQ.value_or_default())
                        currentColorSpace = 3;
                    else if (config->FsrNonLinearSRGB.value_or_default())
                        currentColorSpace = 2;
                    else if (config->FsrNonLinearColorSpace.value_or_default())
                        currentColorSpace = 1;

                    ImGui::SetNextItemWidth(150.0f * menuResScale);
                    if (ImGui::Combo("Input Color Space", &currentColorSpace, colorSpaces, IM_ARRAYSIZE(colorSpaces)))
                    {
                        bool isSrgb = (currentColorSpace == 2);
                        bool isPq = (currentColorSpace == 3);

                        config->FsrNonLinearSRGB = isSrgb;
                        config->FsrNonLinearPQ = isPq;

                        if (isSrgb || isPq)
                        {
                            config->FsrNonLinearColorSpace.set_volatile_value(true);
                        }
                        else if (currentColorSpace == 1) // Just non-Linear
                        {
                            config->FsrNonLinearColorSpace = true;
                        }
                        else // Linear
                        {
                            config->FsrNonLinearColorSpace = false;
                        }

                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                    ShowHelpMarker("게임이 사용하는 입력 색 공간을 선택합니다."
                                   ""
                                   "Non-Linear / sRGB: FSR4 업스케일링 품질이 좋아질 수 있지만 고스팅이 늘어날 수 있습니다.\n\nPQ: 가장 드물며, 고스팅이 늘어나고 조명이 깨질 수 있습니다.");

                    // FSR 4 Presets
                    const char* presets[] = { "Default",  "Preset 0", "Preset 1", "Preset 2",
                                              "Preset 3", "Preset 4", "Preset 5" };
                    int currentPresetIdx = config->Fsr4Preset.has_value() ? config->Fsr4Preset.value() + 1 : 0;

                    if (currentPresetIdx < 0 || currentPresetIdx >= IM_ARRAYSIZE(presets))
                        currentPresetIdx = 0;

                    ImGui::SetNextItemWidth(150.0f * menuResScale);
                    if (ImGui::Combo("FSR4 Preset", &currentPresetIdx, presets, IM_ARRAYSIZE(presets)))
                    {
                        if (currentPresetIdx == 0)
                            config->Fsr4Preset.reset();
                        else
                            config->Fsr4Preset = currentPresetIdx - 1;

                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                    ShowHelpMarker("각 FSR4 내부 프리셋은 특정 해상도에 맞춰 조정되어 있습니다."
                                   ""
                                   "FSR4 프리셋을 선택한다고 게임 내"
                                   "업스케일러 프리셋이 바뀌지는 않습니다!!!"
                                   ""
                                   ""
                                   "프리셋 0: FSR Native AA용"
                                   "\n프리셋 1: Quality/Ultra Quality용\n\n프리셋 2: Balanced용\n\n프리셋 3: Performance용\n\n프리셋 4: DRS용\n\n프리셋 5: Ultra Performance용");

                    // Display the active preset right next to the combo box instead of using a table
                    ImGui::SameLine();
                    if (state.currentFsr4Preset.has_value())
                        ImGui::TextDisabled("(Active: %d)", state.currentFsr4Preset.value());
                    else if (FSR4ModelSelection::IsInt8FsrHooked())
                        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "(Potential FSR3 fallback)");
                    else
                        ImGui::TextDisabled("(Failed to hook)");
                }

                if (majorFsrVersion >= 3)
                {
                    ImGui::Spacing();

                    bool debugView = config->FsrDebugView.value_or_default();
                    if (ImGui::Checkbox("Upscaler Debug View", &debugView))
                    {
                        config->FsrDebugView = debugView;

                        // FSR 4's debug view requires backend reinit
                        if (majorFsrVersion > 3)
                        {
                            state.newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }

                    if (majorFsrVersion > 3)
                    {
                        ShowHelpMarker("왼쪽 위: 팽창된 모션 벡터(Dilated Motion Vectors)"
                                       "\n오른쪽 위: 예측 블렌드 계수(Predicted Blend Factor)");
                    }
                    else
                    {
                        ShowHelpMarker("왼쪽 위: 팽창된 모션 벡터(Dilated Motion Vectors)"
                                       ""
                                       "가운데 위: 보호 영역(Protected Areas)"
                                       ""
                                       "오른쪽 위: 팽창된 깊이(Dilated Depth)"
                                       ""
                                       "가운데: 업스케일된 프레임\n\n왼쪽 아래: 디스클루전 마스크(Disocclusion mask)\n\n가운데 아래: 반응성(Reactiveness)\n\n오른쪽 아래: 디테일 보호 해제(Detail Protection Takedown)");
                    }

                    if (majorFsrVersion > 3)
                    {
                        ImGui::SameLine(0.0f, 20.0f * menuResScale);
                        bool fsr4wm = config->Fsr4EnableWatermark.value_or_default();
                        if (ImGui::Checkbox("Watermark", &fsr4wm))
                        {
                            LOG_DEBUG("FSR4 Watermark set to {}", fsr4wm);
                            config->Fsr4EnableWatermark = fsr4wm;
                        }

                        ShowHelpMarker("이 옵션을 변경한 후에는 설정 저장을 해 주세요."
                                       "\n다음 실행 때 적용됩니다.");
                    }
                }

                if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
                    currentFeature->Version() < feature_version { 4, 0, 0 })
                {
                    ImGui::Spacing();

                    if (currentFeature != nullptr)
                    {
                        ImGui::Text("FSR 3.1 Presets:");

                        ImGui::SameLine(0.0f, 6.0f);

                        // This will be applied by default
                        if (ImGui::Button("Stability"))
                        {
                            auto const scaleRatioX =
                                (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                            auto const scaleRatioY =
                                (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                            auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                            config->FsrVelocity = 0.5f;
                            config->FsrReactiveScale = 0.25f;

                            config->FsrShadingScale.reset();
                            config->FsrAccAddPerFrame.reset();
                            config->FsrMinDisOccAcc.reset();
                            config->FsrShadingScale.set_volatile_value(0.5f / scaleRatio);
                            config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                            config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                        }

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("Motion"))
                        {
                            auto const scaleRatioX =
                                (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                            auto const scaleRatioY =
                                (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                            auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                            config->FsrVelocity = 1.0f;
                            config->FsrReactiveScale = 0.5f;

                            config->FsrShadingScale.reset();
                            config->FsrAccAddPerFrame.reset();
                            config->FsrMinDisOccAcc.reset();
                            config->FsrShadingScale.set_volatile_value(1.0f / scaleRatio);
                            config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                            config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                        }

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("Default"))
                        {
                            config->FsrVelocity = 1.0f;
                            config->FsrReactiveScale = 1.0f;
                            config->FsrShadingScale = 1.0f;
                            config->FsrAccAddPerFrame = 0.333f;
                            config->FsrMinDisOccAcc = -0.333f;
                        }
                    }

                    ImGui::Spacing();

                    if (auto ch = ScopedCollapsingHeader("FSR 3 Upscaler Manual Tuning"); ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        ImGui::Spacing();

                        ImGui::PushItemWidth(220.0f * menuResScale);

                        float velocity = config->FsrVelocity.value_or_default();
                        if (ImGui::SliderFloat("Velocity Factor", &velocity, 0.00f, 1.0f, "%.2f"))
                            config->FsrVelocity = velocity;

                        ShowHelpMarker("0.0f 값은 밝은 픽셀의 시간적 안정성을 개선할 수 있습니다"
                                       ""
                                       "낮은 값은 더 안정적이지만 고스팅이 생깁니다\n\n높은 값은 픽셀 느낌이 강해지지만 고스팅은 줄어듭니다");

                        if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                        {
                            // Reactive Scale
                            float reactiveScale = config->FsrReactiveScale.value_or_default();
                            if (ImGui::SliderFloat("Reactive Scale", &reactiveScale, 0.0f, 1.0f, "%.3f"))
                                config->FsrReactiveScale = reactiveScale;

                            ShowHelpMarker("개발용 옵션입니다. 리액티브 마스크에 더 큰 값을 쓰면"
                                           "\n고스팅이 줄어드는지 확인해 볼 수 있습니다.");

                            // Shading Scale
                            float shadingScale = config->FsrShadingScale.value_or_default();
                            if (ImGui::SliderFloat("Shading Scale", &shadingScale, 0.0f, 1.0f, "%.3f"))
                                config->FsrShadingScale = shadingScale;

                            ShowHelpMarker("이 값을 올리면 FSR3.1이 계산한 셰이딩이 확대됩니다"
                                           "\n읽기 시점에 값을 변경하면 반응성이 더 높아집니다.");

                            // Accumulation Added Per Frame
                            float accAddPerFrame = config->FsrAccAddPerFrame.value_or_default();
                            if (ImGui::SliderFloat("Acc. Added Per Frame", &accAddPerFrame, 0.0f, 1.0f, "%.3f"))
                                config->FsrAccAddPerFrame = accAddPerFrame;

                            ShowHelpMarker("프레임마다 추가되는 누적량에 해당합니다. 디스오클루전이 발생했거나"
                                           ""
                                           "리액티브 마스크 값이 0.0f보다 큰 픽셀 좌표에 적용됩니다. 이 값을 낮추고"
                                           ""
                                           "고스팅되는 개체(모션 벡터 없음)를 1.0f에 가까운 값으로"
                                           "\n리액티브 마스크에 그리면 시간적 고스팅을 줄일 수 있습니다\n\n이 값을 낮추면 얇은 특징 픽셀이\n\n더 많이 깜빡일 수 있습니다.");

                            // Min Disocclusion Accumulation
                            float minDisOccAcc = config->FsrMinDisOccAcc.value_or_default();
                            if (ImGui::SliderFloat("Min. Disocclusion Acc.", &minDisOccAcc, -1.0f, 1.0f, "%.3f"))
                                config->FsrMinDisOccAcc = minDisOccAcc;

                            ShowHelpMarker("이 값을 올리면 서로 자주 가려지는 흔들리는 얇은 개체 주변에서"
                                           ""
                                           "흰색 픽셀이 깜빡이는 현상을 줄일 수 있습니다. 너무 높은 값은 고스팅을\n\n키울 수 있습니다.");
                        }

                        ImGui::PopItemWidth();

                        ImGui::Spacing();
                        ImGui::Spacing();
                    }
                }
            }
        }

        // DLSS -----------------
        if ((config->DLSSEnabled.value_or_default() && currentBackend == Upscaler::DLSS &&
             currentFeature->Version().major > 2) ||
            usesDlssd)
        {

            if (usesDlssd)
                ImGui::SeparatorText("DLSSD Settings");
            else
                ImGui::SeparatorText("DLSS Settings");

            auto overridden =
                usesDlssd ? state.dlssdPresetsOverriddenExternally : state.dlssPresetsOverriddenExternally;

            if (overridden)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Presets are overridden externally");
                ShowHelpMarker("보통 Nvidia App이나 Nvidia Inspector 같은 도구를"
                               "\n사용할 때 발생합니다");
                // ImGui::Text("Selecting setting below will disable that external override\n"
                //             "but you need to Save Settings and restart the game");

                ImGui::Spacing();
            }

            if (usesDlssd)
            {
                if (bool pOverride = config->DLSSDRenderPresetOverride.value_or_default();
                    ImGui::Checkbox("Render Presets Override", &pOverride))
                    config->DLSSDRenderPresetOverride = pOverride;

                ShowHelpMarker("각 렌더 프리셋에는 장점과 단점이 있습니다"
                               ""
                               "이미지 품질을 개선하려면 오버라이드를 사용해 보세요\n\n활성화/비활성화 후 적용을 눌러 주세요");

                /*
                auto currentPresetIndex = GetPresetIndex(currentFeature, true);

                if (currentPresetIndex == 0)
                    ImGui::Text("Current Preset: Default");
                else
                    ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
                */

                ImGui::BeginDisabled(!config->DLSSDRenderPresetOverride.value_or_default() /*|| overridden*/);
                ImGui::PushItemWidth(135.0f * menuResScale);

                AddDLSSDRenderPreset("Override Preset", &comboPreset);

                ImGui::PopItemWidth();
                ImGui::EndDisabled();
            }
            else
            {
                if (bool pOverride = config->RenderPresetOverride.value_or_default();
                    ImGui::Checkbox("Render Presets Override", &pOverride))
                    config->RenderPresetOverride = pOverride;

                ShowHelpMarker("각 렌더 프리셋에는 장점과 단점이 있습니다"
                               ""
                               "이미지 품질을 개선하려면 오버라이드를 사용해 보세요\n\n활성화/비활성화 후 적용(Apply)을 눌러 주세요");

                /*
                auto currentPresetIndex = GetPresetIndex(currentFeature, false);

                if (currentPresetIndex == 0)
                    ImGui::Text("Current Preset: Default");
                else
                    ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
                */

                ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() /*|| overridden*/);

                ImGui::PushItemWidth(135.0f * menuResScale);

                AddDLSSRenderPreset("Override Preset", &comboPreset);

                ImGui::PopItemWidth();
                ImGui::EndDisabled();
            }

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::Button("Apply Changes"))
            {
                LOG_DEBUG("Applying DLSS/DLSSD preset override changes, preset index: {}",
                          comboPreset.value_or_default());

                if (usesDlssd)
                {
                    config->DLSSDRenderPresetForAll = comboPreset.value_or_default();
                    state.newBackend = Upscaler::DLSSD;
                }
                else
                {
                    config->RenderPresetForAll = comboPreset.value_or_default();
                    state.newBackend = currentBackend;
                }

                MARK_ALL_BACKENDS_CHANGED();
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(usesDlssd ? "Advanced DLSSD Settings" : "Advanced DLSS Settings");
                ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                bool appIdOverride = config->UseGenericAppIdWithDlss.value_or_default();
                if (ImGui::Checkbox("Use Generic App Id with DLSS", &appIdOverride))
                    config->UseGenericAppIdWithDlss = appIdOverride;

                ShowHelpMarker("NGX에 일반 appid를 사용합니다"
                               ""
                               "특정 게임에서 OptiScaler 프리셋 오버라이드가 작동하지 않는 문제를 해결합니다\n\n게임 재시작이 필요합니다");

                ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() || overridden);
                ImGui::Spacing();
                ImGui::PushItemWidth(135.0f * menuResScale);

                if (usesDlssd)
                {
                    AddDLSSDRenderPreset("DLAA Preset", &config->DLSSDRenderPresetDLAA);
                    AddDLSSDRenderPreset("UltraQ Preset", &config->DLSSDRenderPresetUltraQuality);
                    AddDLSSDRenderPreset("Quality Preset", &config->DLSSDRenderPresetQuality);
                    AddDLSSDRenderPreset("Balanced Preset", &config->DLSSDRenderPresetBalanced);
                    AddDLSSDRenderPreset("Perf Preset", &config->DLSSDRenderPresetPerformance);
                    AddDLSSDRenderPreset("UltraP Preset", &config->DLSSDRenderPresetUltraPerformance);
                }
                else
                {
                    AddDLSSRenderPreset("DLAA Preset", &config->RenderPresetDLAA);
                    AddDLSSRenderPreset("UltraQ Preset", &config->RenderPresetUltraQuality);
                    AddDLSSRenderPreset("Quality Preset", &config->RenderPresetQuality);
                    AddDLSSRenderPreset("Balanced Preset", &config->RenderPresetBalanced);
                    AddDLSSRenderPreset("Perf Preset", &config->RenderPresetPerformance);
                    AddDLSSRenderPreset("UltraP Preset", &config->RenderPresetUltraPerformance);
                }
                ImGui::PopItemWidth();
                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderFrameGenerationSelection(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    /// FG INPUTS

    static std::vector<MenuOption<FGInput>> inputOptions;
    inputOptions.clear();

    // clang-format off

    inputOptions = {
        { FGInput::NoFG, "None" },
        { FGInput::Upscaler, "OptiFG (Upscaler)",
            "업스케일러가 켜져 있어야 함\n\n모든 FG 출력과 사용 가능, 일부는 불완전할 수 있음\nUI 깨짐 방지용 HUDfix 필요" },
        { FGInput::DLSSG, "DLSSG via Streamline",
            "모든 FG 출력과 사용 가능\n\n게임 설정에서 DLSS-FG를 켜야 함\n기본 HUDless 지원\n\nStreamline 사용 게임 전용" },
        { FGInput::NvngxFG, "DLSSG via Nvngx",
            "FSR FG 계열 전용\n\n게임 설정에서 DLSS-FG를 켜야 함\n기본 HUDless 지원\nStreamline 스왑체인으로 페이싱" },
        { FGInput::FSRFG, "FSR 3.1 FG",
            "모든 FG 출력과 사용 가능\n\n게임 설정에서 FSR-FG를 켜야 함\n기본 HUDless 지원" },
        { FGInput::FSRFG30, "FSR 3.0 FG",
            "모든 FG 출력과 사용 가능\n\n게임 설정에서 FSR-FG를 켜야 함\n기본 HUDless 지원" },
        { FGInput::XeFG, "XeFG" }
    };

    // clang-format on

    auto constexpr nvngxInputIndex = (uint32_t) FGInput::NvngxFG;

    // XeFG input requirements
    auto constexpr xefgInputIndex = (uint32_t) FGInput::XeFG;
    inputOptions[xefgInputIndex].set_disabled(true, "Support not implemented, they meant FG Output");

    // OptiFG requirements
    auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
    inputOptions[optiFgIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady() &&
        !ffxInitTried)
    {
        ffxInitTried = true;
        FfxApiProxy::InitFfxDx12();
        inputOptions[optiFgIndex].set_disabled(!FfxApiProxy::IsFGReady(), "amd_fidelityfx_dx12.dll is missing");
    }
    else if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::XeFG && !xefgInitTried &&
             XeFGProxy::Module() == nullptr)
    {
        xefgInitTried = true;
        XeFGProxy::InitXeFG();
        inputOptions[optiFgIndex].set_disabled(XeFGProxy::Module() == nullptr, "libxess_fg.dll is missing");
    }

    // DLSSG inputs requirements
    auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
    // inputOptions[dlssgInputIndex].set_disabled(state.streamlineVersion.major == 0, "Game doesn't use streamline");
    inputOptions[dlssgInputIndex].set_disabled(state.swapchainApi == API::DX11, "Unsupported API");

    // FSRFG inputs requirements
    auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
    inputOptions[fsrfgInputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    // FSRFG30 inputs requirements
    auto constexpr fsrfg30InputIndex = (uint32_t) FGInput::FSRFG30;
    inputOptions[fsrfg30InputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    if (!config->FGInput.has_value())
        config->FGInput = config->FGInput.value_or_default(); // need to have a value before combo

    /// FG OUTPUTS

    static std::vector<MenuOption<FGOutput>> outputOptions;
    outputOptions.clear();

    // clang-format off

    outputOptions = {
        { FGOutput::NoFG, "None" },
        { FGOutput::FSRFG, "FSR FG", "FSR3/4-FG, RDNA4는 자동으로 FSR4-FG\n\nFSR4-FG가 XeFG보다 낫거나 못할 때가 있음" },
        { FGOutput::DLSSG, "DLSSG", "DLSSG 출력\nNukem 등과 함께 사용 가능" },
        { FGOutput::XeFG, "XeFG", "XeFG - 가장 무겁지만 가장 범용인 FG\n\nXeFG 3은 HUD 처리가 전반적으로 가장 좋음\n\nHUD 잔상 있으면 UI Composition 켜기" },
    };

    // clang-format on

    // DLSSG output requirements
    auto constexpr dlssgOutputIndex = (uint32_t) FGOutput::DLSSG;
    const bool supportsDlssg = primaryGpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_AD100;
    const bool hasDlssgReplacement =
        state.nukemsFgFileAvailable || state.artursFgFileAvailable || FfxApiProxy::IsFGReady(false);

    if (!supportsDlssg && hasDlssgReplacement)
    {
        outputOptions[dlssgOutputIndex].tooltip =
            "진짜 DLSSG 없음, 하드웨어 미지원\nNvngx FG 교체 백엔드만 사용 가능";
    }

    outputOptions[dlssgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    outputOptions[dlssgOutputIndex].set_disabled(!supportsDlssg && !hasDlssgReplacement,
                                                 "Unsupported hardware and no replacements");

    // For that one case of DX11 DLSSG
    const auto streamlineVersion = state.streamlineVersion;
    const bool nukemsUnsupportedApi =
        state.swapchainApi == API::DX11 &&
        (streamlineVersion == feature_version { 0, 0, 0 } || streamlineVersion > feature_version { 2, 0, 1 });
    inputOptions[nvngxInputIndex].set_disabled(nukemsUnsupportedApi, "Unsupported API");

    // FSR FG output requirements
    auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
    outputOptions[fsrfgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    // XeFG output requirements
    auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
    outputOptions[xefgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    // Unsupported FG input selected
    const auto currentInputIndex = (uint32_t) state.activeFgInput;
    if (config->FGInput != FGInput::NoFG && inputOptions.size() > currentInputIndex &&
        inputOptions[currentInputIndex].disabled && state.activeFgInput == config->FGInput)
    {
        LOG_WARN("Resetting FGInput to NoFG: {}", inputOptions[currentInputIndex].label);
        config->FGInput = FGInput::NoFG;

        // Changing active can be dangerous but we are talking about an unsupported mode
        // which shouldn't even actually have taken affect
        state.activeFgInput = FGInput::NoFG;
    }

    // Unsupported FG output selected
    const auto currentOutputIndex = (uint32_t) state.activeFgOutput;
    if (config->FGOutput != FGOutput::NoFG && outputOptions.size() > currentOutputIndex &&
        outputOptions[currentOutputIndex].disabled && state.activeFgOutput == config->FGOutput)
    {
        LOG_WARN("Resetting FGOutput to NoFG: {}", outputOptions[currentOutputIndex].label);
        config->FGOutput = FGOutput::NoFG;
        state.activeFgOutput = FGOutput::NoFG;
    }

    if (!config->FGOutput.has_value())
        config->FGOutput = config->FGOutput.value_or_default(); // need to have a value before combo

    /// FG NVNGX REPLACEMENT

    static std::vector<MenuOption<FGNvngxReplacement>> nvngxOptions;
    nvngxOptions.clear();

    // clang-format off

    nvngxOptions = {
        { FGNvngxReplacement::None, "None (Real DLSSG)", "진짜 DLSSG, RTX 40xx 이상"},
        { FGNvngxReplacement::Nukems, "Nukem's", "FSR 3 FG" },
        { FGNvngxReplacement::Arturs, "Enabler", "FSR 3 MFG" },
        { FGNvngxReplacement::FFX, "FSR 3/4 FG", "FFX 쓰는 FSR 3/4 FG" },
        { FGNvngxReplacement::Combo, "FFX + Enabler", "중간 가짜 프레임은 FFX, 나머지는 Enabler\n\n"
                                                      "2x - FFX\n3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler" },
    };

    // clang-format on

    bool replaceFgOutputWithNvngx = false;
    bool showNvngxFgDowndown = false;

    if (config->FGInput == FGInput::NvngxFG)
    {
        config->FGOutput = FGOutput::NoFG;
        replaceFgOutputWithNvngx = true;
    }
    else if (config->FGOutput == FGOutput::DLSSG)
    {
        showNvngxFgDowndown = true;
    }

    auto constexpr fgNvngxNoneIndex = (uint32_t) FGNvngxReplacement::None;
    nvngxOptions[fgNvngxNoneIndex].set_disabled(!supportsDlssg, "Unsupported hardware");

    if (replaceFgOutputWithNvngx)
    {
        nvngxOptions[fgNvngxNoneIndex].label = "None";
        nvngxOptions[fgNvngxNoneIndex].set_hidden(true);
    }

    auto constexpr fgNvngxNukemsIndex = (uint32_t) FGNvngxReplacement::Nukems;
    nvngxOptions[fgNvngxNukemsIndex].set_disabled(!state.nukemsFgFileAvailable,
                                                  "Missing dlssg_to_fsr3_amd_is_better.dll");

    auto constexpr fgNvngxArtursIndex = (uint32_t) FGNvngxReplacement::Arturs;
    nvngxOptions[fgNvngxArtursIndex].set_disabled(!state.artursFgFileAvailable, "Missing dlss-enabler-headless.dll");

    auto constexpr fgNvngxFfxIndex = (uint32_t) FGNvngxReplacement::FFX;
    nvngxOptions[fgNvngxFfxIndex].set_disabled(!FfxApiProxy::IsFGReady(false),
                                               "Missing amd_fidelityfx_framegeneration_dx12.dll");

    auto constexpr fgNvngxComboIndex = (uint32_t) FGNvngxReplacement::Combo;
    nvngxOptions[fgNvngxComboIndex].set_disabled(
        !FfxApiProxy::IsFGReady(false) || !state.artursFgFileAvailable,
        "Missing amd_fidelityfx_framegeneration_dx12.dll\nor missing dlss-enabler-headless.dll");

    // TODO: Automatically switch to any other option

    if (!config->FGNvngxReplacement.has_value())
        config->FGNvngxReplacement = config->FGNvngxReplacement.value_or_default(); // need to have a value before combo

    if (state.activeFgInput != FGInput::ForceXeLL)
    {
        ImGui::SeparatorText("Frame Generation");

        if (ImGui::BeginTable("fgSelection", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();

            PopulateCombo("FG Input", config->FGInput, inputOptions);
            ShowTooltip("FG에 쓸 데이터 소스\n"
                        "게임이 원래 지원하는 네이티브 FG");

            ImGui::TableNextColumn();

            if (replaceFgOutputWithNvngx)
            {
                // Disable None?
                PopulateCombo("FG Nvngx", config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip("진짜 DLSSG 대신 쓸 백엔드");
            }
            else
            {
                PopulateCombo("FG Output", config->FGOutput, outputOptions);
                ShowTooltip("실제로 쓰게 될 FG");
            }

            ImGui::EndTable();
        }

        // Should be on a new line
        if (showNvngxFgDowndown)
        {
            PopulateCombo("FG Nvngx Replacement", config->FGNvngxReplacement, nvngxOptions);
            ShowTooltip("진짜 DLSSG 대신 쓸 백엔드");
        }

        // Try to avoid having None selected when the gpu doesn't support DLSSG + some fallbacks
        if (!supportsDlssg && (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
            config->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::None)
        {
            if (state.nukemsFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Nukems);

            else if (state.artursFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Arturs);

            else if (FfxApiProxy::IsFGReady(false))
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::FFX);
        }

        const bool nvngxFgChanged = (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
                                    state.activeFgNvngx != config->FGNvngxReplacement.value_or_default();
        state.fgSettingsChanged = state.activeFgOutput != config->FGOutput.value_or_default() ||
                                  state.activeFgInput != config->FGInput.value_or_default() || nvngxFgChanged;

        if (state.fgSettingsChanged)
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)),
                               "Save Settings and restart to apply the changes");
            ImGui::Spacing();
        }

        const bool dlssgInputOrOutput =
            state.activeFgOutput == FGOutput::DLSSG || state.activeFgInput == FGInput::DLSSG;

        ImGui::BeginDisabled(state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default());
        if (state.dlssgMfgMax.has_value() && state.dlssgMfgMax.value() >= 1 && !dlssgInputOrOutput)
        {
            auto maxInterpolationCount = state.dlssgMfgMax.value();

            if (maxInterpolationCount >= 1)
            {
                const char* intModes[] = { "Default", "Off", "2X", "3X", "4X", "5X", "6X" };

                // Map config value to UI index
                int currentSet = 0;
                if (config->FGDLSSGOverrideInterpolationCount.has_value())
                {
                    currentSet = config->FGDLSSGOverrideInterpolationCount.value() + 1;
                }

                const char* currentIntCount = intModes[currentSet];

                ImGui::PushItemWidth(95.0f * menuResScale);

                if (ImGui::BeginCombo("Override DLSSG Ratio", currentIntCount))
                {
                    for (int i = 0; i <= maxInterpolationCount + 1; i++)
                    {
                        if (ImGui::Selectable(intModes[i], (currentSet == i)))
                        {
                            if (i == 0)
                            {
                                // Default, no override
                                config->FGDLSSGOverrideInterpolationCount.reset();
                            }
                            else
                            {
                                // UI index, store value
                                int framesToGenerate = i - 1;

                                LOG_DEBUG("DLSSG Interpolation Count set to: {}", framesToGenerate);
                                config->FGDLSSGOverrideInterpolationCount = framesToGenerate;
                            }

                            StreamlineHooks::updateDlssgOptions();
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::PopItemWidth();
            }
        }

        ImGui::EndDisabled();

        if (!dlssgInputOrOutput)
        {
            if (state.dlssgGameDMFGSupported)
            {
                ImGui::SameLine(0.0f, 16.0f);

                if (bool dynamicMFG = config->FGDLSSGOverrideForceDMFG.value_or_default();
                    ImGui::Checkbox("Force Dynamic MFG", &dynamicMFG))
                {
                    config->FGDLSSGOverrideForceDMFG = dynamicMFG;
                    StreamlineHooks::updateDlssgOptions();
                }
            }

            SeparatorWithHelpMarker("MFG Unlock (RTX",
                                      "40) nvngx_dlssg.dll이 허용하는 생성 프레임 최대치와 Streamline이 보고하는 개수를"
                                      "올려서, 위의 비율 옵션으로 Blackwell 이전 카드에서도 최대 6X까지 사용할"
                                      "수 있습니다. 메모리에서 패치되며 디스크의 파일은 건드리지 않습니다. 다음 게임"
                                      "시작부터 적용됩니다. 공식 문서화되지 않았으며 NVIDIA가 지원하지 않습니다.");

            bool adaUnlock = config->FGDLSSGAdaMfgUnlock.value_or_default();

            if (ImGui::Checkbox("Unlock MFG on RTX 40", &adaUnlock))
                config->FGDLSSGAdaMfgUnlock = adaUnlock;

            // The patch is applied once, as nvngx_dlssg.dll loads, so the box moving does nothing
            // this session. Say so beside it rather than only in the tooltip.
            if (adaUnlock != (state.dlssgMfgMax.value_or(1) > 1))
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "(restart to apply)");
            }

            // What the last attempt found.
            //
            // The signatures carry the shape of the code they patch, so a module nobody has looked at
            // is not recognised -- the expected outcome on an unexamined version, not a fault. Saying
            // which version that was is the difference between a report that can be acted on and "it\n// does not work".
            if (adaUnlock)
            {
                const auto& mfg = MfgUnlock::LastStatus();

                const ImVec4 good(0.4f, 0.9f, 0.5f, 1.f);
                const ImVec4 bad(1.f, 0.55f, 0.4f, 1.f);

                if (!mfg.ModuleFound)
                {
                    ImGui::TextColored(bad, "nvngx_dlssg.dll is not loaded -- this game is not running "
                                            "DLSS frame generation.");
                }
                else
                {
                    const char* version = mfg.SnippetVersion.empty() ? "version unknown" : mfg.SnippetVersion.c_str();

                    if (mfg.CopiesComplete > 0)
                    {
                        // Any fully patched copy means MFG is up; copies with unknown
                        // signatures (e.g. the driver store) are a log-line matter, not
                        // something the overlay should cry about.
                        if (mfg.CopiesSeen > 1)
                            ImGui::TextColored(good, "nvngx_dlssg %s: MFG active (%u copies patched).",
                                               version, mfg.CopiesComplete);
                        else
                            ImGui::TextColored(good, "nvngx_dlssg %s: both gates patched.", version);
                    }
                    else
                        ImGui::TextColored(bad,
                                           "nvngx_dlssg %s: not recognised (advertise %s, validate %s)."
                                           " Report this version.",
                                           version, mfg.AdvertiseMatched ? "ok" : "no",
                                           mfg.ValidateMatched ? "ok" : "no");

                    if (mfg.KernelsRewritten > 0)
                        ImGui::TextColored(good, "%u kernel containers run the Blackwell image.", mfg.KernelsRewritten);
                }
            }
        }

        if (!dlssgInputOrOutput && state.dlssgGameDMFGSupported)
        {
            ImGui::BeginDisabled(state.dlssgLastSetMode != sl::DLSSGMode::eDynamic);
            static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
            ImGui::SliderFloat("DMFG FPS Target", &fpsTarget, 0, 200, "%.0f");

            ShowHelpMarker("활성 상태의 제한값이 0이면 디스플레이 주사율을 자동으로 감지합니다");

            if (ImGui::Button("Apply Target"))
            {
                config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                StreamlineHooks::updateDlssgOptions();
            }

            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Reset Target"))
            {
                fpsTarget = 0.0f;
                config->FGDLSSGFramerateTargetDMFG.reset();
            }

            ImGui::EndDisabled();
        }

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (((state.activeFgOutput == FGOutput::FSRFG || state.activeFgOutput == FGOutput::XeFG ||
              state.activeFgOutput == FGOutput::DLSSG) &&
             state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::NvngxFG) &&
            fgOutput)
        {
            ImGui::Checkbox("Show Detected UI", &state.fgHudlessCompare);
            ShowHelpMarker("최종 이미지와 비교하려면 HUDless 텍스처가 필요합니다"
                           "\nUI 요소, 그리고 UI 요소만 분홍빛으로 표시되어야 합니다!");

            const auto isUsingUIAny = fgOutput->IsUsingUIAny();

            ImGui::BeginDisabled(!isUsingUIAny);

            if (bool drawUIOverFG = config->FGDrawUIOverFG.value_or_default();
                ImGui::Checkbox("Draw UI over", &drawUIOverFG))
            {
                config->FGDrawUIOverFG = drawUIOverFG;
            }
            ShowHelpMarker("UI 리소스를 최종 이미지 위에 그립니다"
                           "\nUI가 보이지 않으면 이 옵션을 활성화하세요!");

            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!isUsingUIAny || !config->FGDrawUIOverFG.value_or_default());

            if (bool uiPremultipliedAlpha = config->FGUIPremultipliedAlpha.value_or_default();
                ImGui::Checkbox("UI Premult. alpha", &uiPremultipliedAlpha))
            {
                config->FGUIPremultipliedAlpha = uiPremultipliedAlpha;
            }
            ShowHelpMarker("UI가 너무 흐릿하면 이 옵션을 비활성화하세요");

            ImGui::EndDisabled();
        }

        const bool showOutputSpecificFGSettings = state.activeFgInput == FGInput::DLSSG ||
                                                  state.activeFgInput == FGInput::FSRFG ||
                                                  state.activeFgInput == FGInput::FSRFG30;

        const bool showHudCutoff = state.activeFgInput == FGInput::NvngxFG || state.activeFgOutput == FGOutput::FSRFG;

        if (showOutputSpecificFGSettings || showHudCutoff)
        {
            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Advanced FG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (showOutputSpecificFGSettings)
                {
                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (fgOutput)
                    {
                        ImGui::BeginDisabled(!fgOutput->IsActive());

                        const auto isUsingUIAny = fgOutput->IsUsingUIAny();
                        const auto isUsingHudlessAny = fgOutput->IsUsingHudlessAny();

                        bool disableUI = config->FGDisableUI.value_or_default();
                        ImGui::BeginDisabled(!isUsingUIAny && !disableUI);

                        if (ImGui::Checkbox("Disable UI texture", &disableUI))
                        {
                            config->FGDisableUI = disableUI;
                            fgOutput->UpdateTarget();
                        }

                        ShowHelpMarker("게임이 UI 텍스처를 보내지만 이를 비활성화하고 싶을 때 사용합니다");

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 16.0f);

                        bool disableHudless = config->FGDisableHudless.value_or_default();
                        ImGui::BeginDisabled(!isUsingHudlessAny && !disableHudless);

                        if (ImGui::Checkbox("Disable HUDless", &disableHudless))
                        {
                            config->FGDisableHudless = disableHudless;
                        }

                        ShowHelpMarker("게임이 HUDless를 보내지만 이를 비활성화하고 싶을 때 사용합니다");

                        ImGui::EndDisabled();

                        bool depthValidNow = config->FGDepthValidNow.value_or_default();
                        if (ImGui::Checkbox("Depth as ValidNow", &depthValidNow))
                            config->FGDepthValidNow = depthValidNow;

                        ShowHelpMarker("VRAM을 더 사용하지만 Uniscaler에는 이 설정이 필요합니다"
                                       "\n다른 게임에도 필요할 수 있습니다");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool velocityValidNow = config->FGVelocityValidNow.value_or_default();
                        if (ImGui::Checkbox("Velocity as ValidNow", &velocityValidNow))
                            config->FGVelocityValidNow = velocityValidNow;

                        ShowHelpMarker("VRAM을 더 사용하지만 Uniscaler에는 이 설정이 필요합니다"
                                       "\n다른 게임에도 필요할 수 있습니다");

                        bool hudlessValidNow = config->FGHudlessValidNow.value_or_default();
                        if (ImGui::Checkbox("HUDless as ValidNow", &hudlessValidNow))
                            config->FGHudlessValidNow = hudlessValidNow;

                        ShowHelpMarker("VRAM을 더 사용하지만 일부 게임에는 이 설정이 필요합니다");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool firstHudless = config->FGOnlyAcceptFirstHudless.value_or_default();
                        if (ImGui::Checkbox("Accept First HUDless", &firstHudless))
                            config->FGOnlyAcceptFirstHudless = firstHudless;

                        ShowHelpMarker("소스가 여러 개의 HUDless를 태그하면 첫 번째 것만 사용합니다");

                        if (bool skipReset = config->FGSkipReset.value_or_default();
                            ImGui::Checkbox("Skip Reset", &skipReset))
                        {
                            config->FGSkipReset = skipReset;
                        }

                        ShowHelpMarker("FG Inputs의 리셋 신호를 사용하지 않습니다");

                        ImGui::EndDisabled();

                        ImGui::PushItemWidth(80.0f * menuResScale);

                        auto frameAhead = config->FGAllowedFrameAhead.value_or_default();
                        if (ImGui::InputInt("Frame Ahead", &frameAhead, 1, 1) && frameAhead > 0 && frameAhead < 4)
                        {
                            config->FGAllowedFrameAhead = frameAhead;
                        }

                        ShowHelpMarker("FG가 게임보다 앞서갈 수 있는 프레임 수입니다"
                                       "\nFG 켜기/끄기 전환을 막을 수 있지만, 문제가 생길 수도 있습니다");

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 16.0f);

                        const char* ftSources[] = { "Input", "Opti", "Zero" };
                        const char* ftSourceInfos[] = { "Uses frametimes provided by\nDLSSG or FSR-FG ",
                                                        "Uses frametimes calculated by Opti",
                                                        "Let XeFG to handle frametimes" };

                        auto currentSet = (int) config->FTInput.value_or_default();
                        auto currentSourceCount = state.activeFgOutput == FGOutput::XeFG ? 3 : 2;

                        ImGui::PushItemWidth(95.0f * menuResScale);

                        if (ImGui::BeginCombo("FT Input", ftSources[currentSet]))
                        {
                            for (size_t i = 0; i < currentSourceCount; i++)
                            {

                                if (ImGui::Selectable(ftSources[i], currentSet == i))
                                {
                                    LOG_DEBUG("FTInput has changed {} -> {}", ftSources[currentSet], ftSources[i]);
                                    config->FTInput = (FrameTimeSource) i;
                                }

                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    ImGui::SetTooltip(ftSourceInfos[i]);
                            }

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        ShowHelpMarker("프레임타임 소스를 선택합니다"
                                       "\n프레임 페이싱과 스타터 문제에 도움이 될 수 있습니다");
                    }
                }

                if (showHudCutoff)
                {
                    float fgHudCutoff = config->FGHudCutoff.value_or_default();
                    if (ImGui::SliderFloat("Hud Cutoff", &fgHudCutoff, 0.00f, 1.0f, "%.2f"))
                        config->FGHudCutoff = fgHudCutoff;

                    ShowHelpMarker("보간을 돕기 위해 UI의 투명도를 잘라냅니다"
                                   "\nShow Detected UI로 차이를 확인할 수 있습니다\n0.0은 자동입니다");
                }
            }
        }
    }
}

void MenuCommon::RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;
    auto fgOutput = state.currentFG;

    // FSR FG controls
    if (state.activeFgOutput == FGOutput::FSRFG && state.activeFgInput != FGInput::NoFG &&
        state.currentFGSwapchain != nullptr)
    {
        if (state.activeFgInput != FGInput::Upscaler ||
            (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::IsFGReady())
        {
            ImGui::SeparatorText("Frame Generation (FSR FG)");

            if (_ffxFGIndex < 0)
                _ffxFGIndex = config->FfxFGIndex.value_or_default();

            if (state.ffxFGVersionNames.size() > 0)
            {
                ImGui::PushItemWidth(135.0f * menuResScale);

                auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                if (ImGui::BeginCombo("FFX FG", currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                    {
                        auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                        if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                            _ffxFGIndex = n;
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowHelpMarker("FFX SDK가 보고한 FG 목록입니다");

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Change FG") && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                {
                    config->FfxFGIndex = _ffxFGIndex;
                    state.fgChanged = true;
                    state.scChanged = true;
                }
            }

            bool fgActive = config->FGEnabled.value_or_default();
            if (ImGui::Checkbox("Active##2", &fgActive))
            {
                config->FGEnabled = fgActive;
                LOG_DEBUG("FGEnabled set FGEnabled: {}", fgActive);

                if (config->FGEnabled.value_or_default())
                    state.fgChanged = true;
            }
            ShowHelpMarker("프레임 생성을 활성화합니다");

            bool fgAsync = config->FGAsync.value_or_default();
            if (ImGui::Checkbox("Allow Async", &fgAsync))
            {
                config->FGAsync = fgAsync;

                if (config->FGEnabled.value_or_default())
                {
                    state.fgChanged = true;
                    state.scChanged = true;
                    LOG_DEBUG("Async set FGChanged");
                }
            }
            ShowHelpMarker("더 나은 FG 성능을 위해 Async를 활성화합니다\n특히 HUD Fix와 함께 사용하면 크래시가 발생할 수 있습니다!");

            ImGui::SameLine(0.0f, 16.0f);

            bool fgDV = config->FGDebugView.value_or_default();
            if (ImGui::Checkbox("Debug View##2", &fgDV))
            {
                config->FGDebugView = fgDV;

                if (config->FGEnabled.value_or_default())
                {
                    state.fgChanged = true;
                    LOG_DEBUG("DebugView set FGChanged");
                }
            }
            ShowHelpMarker("FSR3.1-FG Debug view를 활성화합니다"
                           ""
                           ""
                           "좌상단: Game Motion Vectors"
                           ""
                           "상단 중앙: GMV Depth"
                           ""
                           "우상단: Optical Flow MV\n\n중앙: 보간된 프레임만 표시\n\n좌하단: Disocclusion mask\n\n하단 중앙: 보간 소스(UI 제외)\n\n우하단: HUDless 리소스");

            ImGui::SameLine(0.0f, 16.0f);

            if (state.currentFG && state.currentFG->Version().major > 3)
            {
                if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                    ImGui::Checkbox("Enable Watermark", &fgwm))
                {
                    LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                    config->FSRFGEnableWatermark = fgwm;
                }

                ShowHelpMarker("이 옵션을 변경한 후에는 Save Settings를 눌러 주세요"
                               "\n다음 실행 때 적용됩니다.");
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Extended FSR FG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::Checkbox("FG Only Generated", &state.fgOnlyGenerated);
                ShowHelpMarker("FSR 3.1로 생성된 프레임만 표시합니다");

                ImGui::SameLine(0.0f, 16.0f);
                auto debugResetLines = config->FGDebugResetLines.value_or_default();
                if (ImGui::Checkbox("Debug Reset Lines", &debugResetLines))
                {
                    config->FGDebugResetLines = debugResetLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                }
                ShowHelpMarker("보간 스킵 라인 그리기를 활성화합니다");

                auto debugTearLines = config->FGDebugTearLines.value_or_default();
                if (ImGui::Checkbox("Debug Tear Lines", &debugTearLines))
                {
                    config->FGDebugTearLines = debugTearLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                }
                ShowHelpMarker("Tear 라인과 보간 스킵 라인 그리기를 활성화합니다");

                ImGui::SameLine(0.0f, 16.0f);
                auto debugPacingLines = config->FGDebugPacingLines.value_or_default();
                if (ImGui::Checkbox("Debug Pacing Lines", &debugPacingLines))
                {
                    config->FGDebugPacingLines = debugPacingLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                }
                ShowHelpMarker("페이싱 라인 그리기를 활성화합니다");

                ImGui::Spacing();
                if (ImGui::TreeNode("FG Rectangle Settings"))
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    int rectLeft = config->FGRectLeft.value_or(0);
                    if (ImGui::InputInt("Rect Left", &rectLeft))
                        config->FGRectLeft = rectLeft;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectTop = config->FGRectTop.value_or(0);
                    if (ImGui::InputInt("Rect Top", &rectTop))
                        config->FGRectTop = rectTop;

                    int rectWidth = config->FGRectWidth.value_or(0);
                    if (ImGui::InputInt("Rect Width", &rectWidth))
                        config->FGRectWidth = rectWidth;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectHeight = config->FGRectHeight.value_or(0);
                    if (ImGui::InputInt("Rect Height", &rectHeight))
                        config->FGRectHeight = rectHeight;

                    ImGui::PopItemWidth();
                    ShowHelpMarker("프레임 생성 사각형입니다. 레터박스 콘텐츠에 맞게 조정하세요");

                    ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                         !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                    if (ImGui::Button("Reset FG Rect"))
                    {
                        config->FGRectLeft.reset();
                        config->FGRectTop.reset();
                        config->FGRectWidth.reset();
                        config->FGRectHeight.reset();
                    }

                    ShowHelpMarker("프레임 생성 사각형을 초기화합니다");

                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }

                auto fg = state.currentFG;
                if (fg != nullptr && strcmp(fg->Name(), "FSR-FG") == 0 &&
                    FfxApiProxy::VersionDx12_FG() >= feature_version { 3, 1, 3 })
                {
                    ImGui::Spacing();

                    if (ImGui::TreeNode("Frame Pacing Tuning"))
                    {
                        auto fptEnabled = config->FGFramePacingTuning.value_or_default();
                        if (ImGui::Checkbox("Enable Tuning", &fptEnabled))
                        {
                            config->FGFramePacingTuning = fptEnabled;
                            state.fsrfgFramePaceTuningChanged = true;
                        }

                        ImGui::BeginDisabled(!config->FGFramePacingTuning.value_or_default());

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptSafetyMargin = config->FGFPTSafetyMarginInMs.value_or_default();
                        if (ImGui::InputFloat("Safety Margins in ms", &fptSafetyMargin, 0.01f, 0.1f, "%.2f"))
                            config->FGFPTSafetyMarginInMs = fptSafetyMargin;
                        ShowHelpMarker("안전 여유 시간(밀리초)입니다"
                                       ""
                                       "FSR 기본값: 0.1ms\n\nOpti 기본값: 0.01ms");

                        auto fptVarianceFactor = config->FGFPTVarianceFactor.value_or_default();
                        if (ImGui::SliderFloat("Variance Factor", &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                            config->FGFPTVarianceFactor = fptVarianceFactor;
                        ShowHelpMarker("Variance factor입니다"
                                       ""
                                       "FSR 기본값: 0.1\n\nOpti 기본값: 0.3");
                        ImGui::PopItemWidth();

                        auto fpHybridSpin = config->FGFPTAllowHybridSpin.value_or_default();
                        if (ImGui::Checkbox("Enable Hybrid Spin", &fpHybridSpin))
                            config->FGFPTAllowHybridSpin = fpHybridSpin;
                        ShowHelpMarker("페이싱 스핀락이 잠들 수 있게 해서 CPU 사용량을 줄여 줍니다"
                                       "\nFPS가 천천히 오를 수 있습니다");

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptHybridSpinTime = config->FGFPTHybridSpinTime.value_or_default();
                        if (ImGui::SliderInt("Hybrid Spin Time", &fptHybridSpinTime, 0, 100))
                            config->FGFPTHybridSpinTime = fptHybridSpinTime;
                        ShowHelpMarker("FPTHybridSpin이 true일 때 스핀하는 시간입니다. 타이머 해상도 단위로 측정합니다."
                                       ""
                                       "2 미만은 권장하지 않습니다. 잦은 오버슈트가 발생합니다");
                        ImGui::PopItemWidth();

                        auto fpWaitForSingleObjectOnFence =
                            config->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                        if (ImGui::Checkbox("Enable WaitForSingleObjectOnFence", &fpWaitForSingleObjectOnFence))
                        {
                            config->FGFPTAllowWaitForSingleObjectOnFence = fpWaitForSingleObjectOnFence;
                        }
                        ShowHelpMarker("펜스 값을 기다릴 때 스핀 대신 WaitForSingleObject를 사용합니다");

                        if (ImGui::Button("Apply Timing Changes"))
                            state.fsrfgFramePaceTuningChanged = true;

                        ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    // XeFG controls
    if (state.activeFgOutput == FGOutput::XeFG && state.activeFgInput != FGInput::NoFG &&
        state.activeFgInput != FGInput::ForceXeLL && state.currentFGSwapchain != nullptr && XeFGProxy::InitXeFG() &&
        fgOutput)
    {
        ImGui::SeparatorText("Frame Generation (XeFG)");

        bool ignoreChecks = config->FGXeFGIgnoreInitChecks.value_or_default();

        bool nativeAA = false;
        if (state.activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
            nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

        const bool correctMVs = fgOutput->IsLowResMV() || nativeAA ||
                                (State::Instance().gameQuirks & GameQuirk::ForceFGRenderSizeMVs) || ignoreChecks;

        if (!correctMVs || state.realExclusiveFullscreen)
        {
            config->FGEnabled.reset();
            config->FGXeFGDebugView.reset();
        }

        const bool restartNeeded = config->FGXeFGDepthInverted.value_or_default() != fgOutput->IsInvertedDepth() ||
                                   config->FGXeFGJitteredMV.value_or_default() != fgOutput->IsJitteredMVs() ||
                                   config->FGXeFGHighResMV.value_or_default() == fgOutput->IsLowResMV();

        bool cantActivate = false;
        if (restartNeeded)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               "Restart the game to apply correct XeFG settings!");
        }
        else
        {
            if (!correctMVs)
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Requires disabling dilated motion vectors");

            if (!ignoreChecks && state.realExclusiveFullscreen)
            {
                cantActivate = true;
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Borderless display mode required!");
            }

            if (!ignoreChecks && (state.hdrOutputActive && state.swapchainEncoding != ColorEncoding::SDR))
            {
                if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                    state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
                {
                    cantActivate = true;
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), "XeFG only supports HDR10");
                }
            }
        }

        if (!correctMVs || cantActivate || ignoreChecks)
        {
            if (ImGui::Checkbox("Ignore Init Checks", &ignoreChecks))
                config->FGXeFGIgnoreInitChecks = ignoreChecks;

            ShowHelpMarker("XeFG의 모든 사전 검사를 건너뜁니다"
                           ""
                           "UE 게임에서 MV 크기 경고를 건너뛰려고 이 옵션을 사용하지 마세요!\n\n크래시와 이미지 품질 저하가 발생할 수 있습니다!");
        }

        ImGui::BeginDisabled(!correctMVs || cantActivate);

        bool fgActive = config->FGEnabled.value_or_default();
        if (ImGui::Checkbox("Active##3", &fgActive))
        {
            config->FGEnabled = fgActive;
            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

            if (config->FGEnabled.value_or_default())
                state.fgChanged = true;
        }

        ShowHelpMarker("프레임 생성을 활성화합니다");

        auto maxInterpolationCount = fgOutput->GetMaxInterpolationCount();

        if (maxInterpolationCount > 1)
        {
            ImGui::SameLine(0.0f, 16.0f);

            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
            auto currentSet = fgOutput->GetInterpolatedFrameCount() - 1;
            auto currentIntCount = intModes[currentSet];

            ImGui::PushItemWidth(95.0f * menuResScale);

            if (ImGui::BeginCombo("MFG", currentIntCount))
            {
                for (int i = 0; i < maxInterpolationCount; i++)
                {
                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                    {
                        LOG_DEBUG("XeFG Interpolation Count set to: {}", i + 1);
                        state.fgChanged = true;
                        config->FGXeFGInterpolationCount = i + 1;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowHelpMarker("XeFG 보간 배수를 설정합니다");
        }

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::BeginDisabled(!fgOutput->IsUsingHudlessAny() || XeFGProxy::SetUiCompositionState() == nullptr);
        bool fgCompositeUI = config->FGXeFGUIComposition.value_or_default();
        if (ImGui::Checkbox("UI Composition", &fgCompositeUI))
            config->FGXeFGUIComposition = fgCompositeUI;

        ShowHelpMarker("HUD/UI 보간을 비활성화합니다"
                       ""
                       "이전 XeFG 2 동작으로 되돌아갑니다\n\n\n투명한 HUD/UI의 깨짐 현상을 해결합니다");
        ImGui::EndDisabled();

        bool fgDV = config->FGXeFGDebugView.value_or_default();
        if (ImGui::Checkbox("Debug View##2", &fgDV))
        {
            config->FGXeFGDebugView = fgDV;

            if (config->FGXeFGDebugView.value_or_default())
            {
                state.fgChanged = true;
                LOG_DEBUG("DebugView set FGChanged");
            }
        }
        ShowHelpMarker("XeFG Debug view를 활성화합니다");

        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 16.0f);
        bool fgBorderless = config->FGXeFGForceBorderless.value_or_default();
        if (ImGui::Checkbox("Force Borderless", &fgBorderless))
            config->FGXeFGForceBorderless = fgBorderless;

        ShowHelpMarker("Borderless 디스플레이 모드를 강제합니다"
                       ""
                       ""
                       "가장 좋은 결과를 위해서는 전체 화면 해상도를"
                       "\n디스플레이 해상도와 같게 설정하세요\n\n일부 불안정 문제가 발생할 수 있습니다\n\n\n적용하려면 게임 재시작이 필요합니다!");

        // Disable this for now
        // ImGui::SameLine(0.0f, 16.0f);
        // ImGui::Checkbox("Only Generated##2", &state.fgOnlyGenerated);
        // ShowHelpMarker("XeFG로 생성된 프레임만 표시합니다");

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Extended XeFG Settings"); ch.IsHeaderOpen())
        {
            ImGui::Spacing();
            if (ImGui::TreeNode("Rectangle Settings"))
            {
                ImGui::PushItemWidth(95.0f * menuResScale);
                int rectLeft = config->FGRectLeft.value_or(0);
                if (ImGui::InputInt("Rect Left##2", &rectLeft))
                    config->FGRectLeft = rectLeft;

                ImGui::SameLine(0.0f, 16.0f);
                int rectTop = config->FGRectTop.value_or(0);
                if (ImGui::InputInt("Rect Top##2", &rectTop))
                    config->FGRectTop = rectTop;

                int rectWidth = config->FGRectWidth.value_or(0);
                if (ImGui::InputInt("Rect Width##2", &rectWidth))
                    config->FGRectWidth = rectWidth;

                ImGui::SameLine(0.0f, 16.0f);
                int rectHeight = config->FGRectHeight.value_or(0);
                if (ImGui::InputInt("Rect Height##2", &rectHeight))
                    config->FGRectHeight = rectHeight;

                ImGui::PopItemWidth();
                ShowHelpMarker("프레임 생성 사각형입니다. 레터박스 콘텐츠에 맞게 조정하세요##2");

                ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                     !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                if (ImGui::Button("Reset FG Rect##2"))
                {
                    config->FGRectLeft.reset();
                    config->FGRectTop.reset();
                    config->FGRectWidth.reset();
                    config->FGRectHeight.reset();
                }

                ShowHelpMarker("프레임 생성 사각형을 초기화합니다##2");

                ImGui::EndDisabled();
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // DLSSG controls
    if (state.activeFgOutput == FGOutput::DLSSG && state.activeFgInput != FGInput::NoFG &&
        state.currentFGSwapchain != nullptr && StreamlineProxy::LoadStreamline() && fgOutput)
    {
        ImGui::SeparatorText("Frame Generation (DLSSG)");

        if (state.activeFgNvngx == FGNvngxReplacement::None &&
            (state.hdrOutputActive && state.swapchainEncoding != ColorEncoding::SDR))
        {
            if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), "DLSSG only supports HDR10");
            }
        }

        ImGui::Text("Current DLSSG state:");
        ImGui::SameLine();
        if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
        {
            ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), std::format("ON {}x", count + 1).c_str());
        }
        else
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
        }

        bool fgActive = config->FGEnabled.value_or_default();
        if (ImGui::Checkbox("Active##4", &fgActive))
        {
            config->FGEnabled = fgActive;
            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

            if (config->FGEnabled.value_or_default())
                state.fgChanged = true;
        }

        ShowHelpMarker("프레임 생성을 활성화합니다");

        auto maxInterpolationCount = fgOutput->GetMaxInterpolationCount();

        if (maxInterpolationCount > 1)
        {
            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(config->FGDLSSGForceDMFG.value_or_default());

            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
            auto currentSet = fgOutput->GetInterpolatedFrameCount() - 1;
            auto currentIntCount = intModes[currentSet];

            ImGui::PushItemWidth(95.0f * menuResScale);

            if (ImGui::BeginCombo("MFG", currentIntCount))
            {
                for (int i = 0; i < maxInterpolationCount; i++)
                {
                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                    {
                        LOG_DEBUG("DLSSG Interpolation Count set to: {}", i + 1);
                        config->FGDLSSGInterpolationCount = i + 1;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowHelpMarker("DLSSG 보간 프레임 수를 설정합니다");

            ImGui::EndDisabled();

            if (fgOutput->GetDMFGSupport())
            {
                ImGui::SameLine(0.0f, 16.0f);

                if (bool dynamicMFG = config->FGDLSSGForceDMFG.value_or_default();
                    ImGui::Checkbox("Force Dynamic MFG", &dynamicMFG))
                {
                    config->FGDLSSGForceDMFG = dynamicMFG;
                }

                ImGui::BeginDisabled(!config->FGDLSSGForceDMFG.value_or_default());
                static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
                ImGui::SliderFloat("DMFG FPS Target", &fpsTarget, 0, 200, "%.0f");

                ShowHelpMarker("활성화된 제한값이 0이면 디스플레이 재생 빈도를 자동으로 감지합니다");

                if (ImGui::Button("Apply Target"))
                {
                    config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                }

                ImGui::SameLine(0.0f, 16.0f);

                if (ImGui::Button("Reset Target"))
                {
                    fpsTarget = 0.0f;
                    config->FGDLSSGFramerateTargetDMFG.reset();
                }

                ImGui::EndDisabled();
            }
        }

        bool useGamesMarkers = config->FGDLSSGUseGamesReflexMarkers.value_or_default();
        ImGui::BeginDisabled(!ReflexHooks::gameIsSendingMarkers());
        if (ImGui::Checkbox("Use Game's Reflex Markers", &useGamesMarkers))
        {
            config->FGDLSSGUseGamesReflexMarkers = useGamesMarkers;
            LOG_DEBUG("Changed set FGDLSSGUseGamesReflexMarkers: {}", useGamesMarkers);
        }
        ImGui::EndDisabled();
    }

    // OptiFG
    if (state.api != API::Vulkan && state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::Upscaler)
    {
        SeparatorWithHelpMarker("프레임 생성 (OptiFG)", "업스케일러 데이터를 FG에 사용합니다");

        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            ((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
             (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
             (state.activeFgOutput == FGOutput::DLSSG && StreamlineProxy::Module() != nullptr)))
        {
            if (!Config::Instance()->FGDisableHUDFix.value_or_default() &&
                state.swapchainInteropApi == SwapchainInteropApi::None)
            {
                bool fgHudfix = config->FGHUDFix.value_or_default();

                if (ImGui::Checkbox("HUDFix", &fgHudfix))
                {
                    config->FGHUDFix = fgHudfix;
                    LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                    state.clearCapturedHudlesses = true;
                    state.fgChanged = true;
                }

                ShowHelpMarker("HUD 안정성 수정을 활성화합니다. 충돌이 발생할 수 있습니다!");

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                ImGui::SameLine(0.0f, 16.0f);
                ImGui::PushItemWidth(95.0f * menuResScale);
                int hudFixLimit = config->FGHUDLimit.value_or_default();
                if (ImGui::InputInt("Limit", &hudFixLimit))
                {
                    if (hudFixLimit < 1)
                        hudFixLimit = 1;
                    else if (hudFixLimit > 999)
                        hudFixLimit = 999;

                    config->FGHUDLimit = hudFixLimit;
                    LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                }
                ShowHelpMarker("HUDless 캡처를 지연시킵니다. 값이 높으면 충돌이 발생할 수 있습니다!");

                ImGui::SameLine(0.0f, 16.0f);
                if (ImGui::Button("Res##2"))
                    _showHudlessWindow = !_showHudlessWindow;

                ImGui::EndDisabled();

                auto hudExtended = config->FGHUDFixExtended.value_or_default();
                if (ImGui::Checkbox("Extended", &hudExtended))
                {
                    LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                    config->FGHUDFixExtended = hudExtended;
                }
                ShowHelpMarker("HUDless일 가능성이 있는 리소스를 위한 확장 형식 검사입니다.\n충돌과 성능 저하를 일으킬 수 있습니다!");
                ImGui::SameLine(0.0f, 16.0f);

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                auto immediate = config->FGImmediateCapture.value_or_default();
                if (ImGui::Checkbox("Immediate Capture", &immediate))
                {
                    LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                    config->FGImmediateCapture = immediate;
                }
                ShowHelpMarker("셰이더 실행 전에 리소스를 캡처합니다."
                               "HUDless 캡처 가능성은 높아지지만, 불필요한 리소스까지 캡처될 수 있습니다.");

                ImGui::PopItemWidth();

                ImGui::EndDisabled();
            }

            bool depthScale = config->FGEnableDepthScale.value_or_default();
            if (ImGui::Checkbox("Scale Depth to fix DLSS RR", &depthScale))
                config->FGEnableDepthScale = depthScale;
            ShowHelpMarker("DLSS-D가 잘못된 깊이 입력을 받는 문제에 대한 수정입니다");

            bool resourceFlip = config->FGResourceFlip.value_or_default();
            if (ImGui::Checkbox("Flip (Unity)", &resourceFlip))
                config->FGResourceFlip = resourceFlip;
            ShowHelpMarker("Unity 게임의 Velocity와 Depth 리소스를 서로 뒤바꿉니다");

            ImGui::SameLine(0.0f, 16.0f);

            bool resourceFlipOffset = config->FGResourceFlipOffset.value_or_default();
            if (ImGui::Checkbox("Flip Use Offset", &resourceFlipOffset))
                config->FGResourceFlipOffset = resourceFlipOffset;
            ShowHelpMarker("높이 차이를 오프셋으로 사용합니다");

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Advanced OptiFG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};

                if (!Config::Instance()->FGDisableHUDFix.value_or_default() &&
                    state.swapchainInteropApi == SwapchainInteropApi::None)
                {
                    ImGui::Spacing();

                    auto rb = config->FGResourceBlocking.value_or_default();
                    if (ImGui::Checkbox("Resource Blocking", &rb))
                    {
                        config->FGResourceBlocking = rb;
                        LOG_DEBUG("Enabled set FGResourceBlocking: {}", rb);
                    }
                    ShowHelpMarker("깜빡임 등의 문제를 막기 위해 자주 사용되지 않는 리소스가 HUDless로 쓰이지 않도록 차단합니다."
                                   ""
                                   "HUDfix를 켜거나 끄면 차단 목록이 초기화됩니다!");

                    ImGui::SameLine(0.0f, 16.0f);

                    auto rrc = config->FGRelaxedResolutionCheck.value_or_default();
                    if (ImGui::Checkbox("Relaxed Resource Check", &rrc))
                    {
                        config->FGRelaxedResolutionCheck = rrc;
                        LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                    }
                    ShowHelpMarker("일부 해상도와 화면 비율에서 검은 테두리를"
                                   "쓰는 게임(예: Witcher 3)에 도움이 되도록"
                                   "HUDless 해상도 검사를 32픽셀 만큼 완화합니다");

                    ImGui::BeginDisabled(state.fgResetCapturedResources);
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    if (ImGui::Checkbox("FG Create List", &state.fgCaptureResources))
                    {
                        if (!state.fgCaptureResources)
                            config->FGHUDLimit = 1;
                        else
                            state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::SameLine(0.0f, 16.0f);
                    if (ImGui::Checkbox("FG Use List", &state.fgOnlyUseCapturedResources))
                    {
                        if (state.fgCaptureResources)
                        {
                            state.fgCaptureResources = false;
                            config->FGHUDLimit = 1;
                        }
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("(%d)", state.fgCapturedResourceCount);

                    ImGui::PopItemWidth();

                    ImGui::SameLine(0.0f, 16.0f);

                    if (ImGui::Button("Reset List"))
                    {
                        LOG_DEBUG("Resetting captured resource list");

                        state.fgResetCapturedResources = true;
                        state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::EndDisabled();

                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (ImGui::TreeNode("Tracking Settings"))
                    {
                        auto ath = config->FGAlwaysTrackHeaps.value_or_default();
                        if (ImGui::Checkbox("Always Track Heaps", &ath))
                        {
                            config->FGAlwaysTrackHeaps = ath;
                            LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                        }
                        ShowHelpMarker("리소스를 항상 추적합니다. 성능 문제가 생길 수 있지만, HUDFix 관련"
                                       "충돌을 고칠 수도 있습니다!");

                        auto disableRTV = config->FGHudfixDisableRTV.value_or_default();
                        if (ImGui::Checkbox("Disable RTV Tracking", &disableRTV))
                            config->FGHudfixDisableRTV = disableRTV;
                        ShowHelpMarker("CreateRenderTargetView 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSRV = config->FGHudfixDisableSRV.value_or_default();
                        if (ImGui::Checkbox("Disable SRV Tracking", &disableSRV))
                            config->FGHudfixDisableSRV = disableSRV;
                        ShowHelpMarker("CreateShaderResourceView 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        auto disableUAV = config->FGHudfixDisableUAV.value_or_default();
                        if (ImGui::Checkbox("Disable UAV Tracking", &disableUAV))
                            config->FGHudfixDisableUAV = disableUAV;
                        ShowHelpMarker("CreateUnorderedAccessView 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableOM = config->FGHudfixDisableOM.value_or_default();
                        if (ImGui::Checkbox("Disable OM Tracking", &disableOM))
                            config->FGHudfixDisableOM = disableOM;
                        ShowHelpMarker("OMSetRenderTargets 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        auto disableSCR = config->FGHudfixDisableSCR.value_or_default();
                        if (ImGui::Checkbox("Disable SCR Tracking", &disableSCR))
                            config->FGHudfixDisableSCR = disableSCR;
                        ShowHelpMarker("SetComputeRootDescriptorTable 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSGR = config->FGHudfixDisableSGR.value_or_default();
                        if (ImGui::Checkbox("Disable SGR Tracking", &disableSGR))
                            config->FGHudfixDisableSGR = disableSGR;
                        ShowHelpMarker("SetGraphicsRootDescriptorTable 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        ImGui::Spacing();

                        auto disableDI = config->FGHudfixDisableDI.value_or_default();
                        if (ImGui::Checkbox("Disable DI Tracking", &disableDI))
                            config->FGHudfixDisableDI = disableDI;
                        ShowHelpMarker("DrawInstanced 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableDII = config->FGHudfixDisableDII.value_or_default();
                        if (ImGui::Checkbox("Disable DII Tracking", &disableDII))
                            config->FGHudfixDisableDII = disableDII;
                        ShowHelpMarker("DrawIndexedInstanced 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        auto disableDispatch = config->FGHudfixDisableDispatch.value_or_default();
                        if (ImGui::Checkbox("Disable Dispatch Tracking", &disableDispatch))
                            config->FGHudfixDisableDispatch = disableDispatch;
                        ShowHelpMarker("Dispatch 추적을 비활성화합니다."
                                       "잘못된 HUDless 리소스를 걸러내는 데 도움이 될 수 있습니다");

                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                if (ImGui::TreeNode("Resource Settings"))
                {
                    bool makeMVCopies = config->FGMakeMVCopy.value_or_default();
                    if (ImGui::Checkbox("FG Make MV Copies", &makeMVCopies))
                        config->FGMakeMVCopy = makeMVCopies;
                    ShowHelpMarker("OptiFG에서 사용할 모션 벡터 복사본을 만듭니다."
                                   "발생할 수 있는 화면 깨짐을 방지하기 위해서입니다");

                    bool makeDepthCopies = config->FGMakeDepthCopy.value_or_default();
                    if (ImGui::Checkbox("FG Make Depth Copies", &makeDepthCopies))
                        config->FGMakeDepthCopy = makeDepthCopies;
                    ShowHelpMarker("OptiFG에서 사용할 깊이 복사본을 만듭니다."
                                   "발생할 수 있는 화면 깨짐을 방지하기 위해서입니다");

                    ImGui::PushItemWidth(115.0f * menuResScale);
                    float depthScaleMax = config->FGDepthScaleMax.value_or_default();
                    if (ImGui::InputFloat("FG Scale Depth Max", &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                        config->FGDepthScaleMax = depthScaleMax;
                    ShowHelpMarker("깊이 값이 이 값으로 나뉩니다");
                    ImGui::PopItemWidth();

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                if (ImGui::TreeNode("Syncing Settings"))
                {
                    bool useMutexForPresent = config->FGUseMutexForSwapchain.value_or_default();
                    if (ImGui::Checkbox("FG Use Mutex for Present", &useMutexForPresent))
                        config->FGUseMutexForSwapchain = useMutexForPresent;
                    ShowHelpMarker("뮤텍스를 사용해 FG 비동기화와 충돌을 막습니다."
                                   "끄면 성능은 나아질 수 있지만 안정성이 떨어집니다");

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
        else if (currentFeature == nullptr || currentFeature->IsFrozen())
        {
            ImGui::Text("Upscaler is not active"); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady())
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               "amd_fidelityfx_dx12.dll is missing!"); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() == nullptr)
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               "libxess_fg.dll is missing!"); // Probably never will be visible
        }
    }

    const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
    if (activeNvngxFg != FGNvngxReplacement::None)
    {
        if (activeNvngxFg == FGNvngxReplacement::Nukems)
        {
            SeparatorWithHelpMarker("프레임 생성 (Nukem의 DLSSG를 통한 FSR3-FG)",
                                    "Nukem의 dlssg_to_fsr3 dll이 필요합니다");

            if (!state.nukemsFgFileAvailable)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Please put dlssg_to_fsr3_amd_is_better.dll into OptiScaler folder");
            }
        }
        else if (activeNvngxFg == FGNvngxReplacement::Arturs)
        {
            SeparatorWithHelpMarker("프레임 생성 (DLSS Enabler를 통한 FSR3-MFG)",
                                    "dlss-enabler-headless.dll 형태의 DLSS Enabler를 사용합니다");

            if (!state.artursFgFileAvailable)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Please put dlss-enabler-headless.dll into OptiScaler folder");
            }

            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               "Using a subset of features from DLSS Enabler");
        }
        else if (activeNvngxFg == FGNvngxReplacement::FFX)
        {
            SeparatorWithHelpMarker("프레임 생성 (FFX를 통한 FSRFG)", "DLSSG 스왑체인으로 FFX를 사용합니다");
        }
        else if (activeNvngxFg == FGNvngxReplacement::Combo)
        {
            SeparatorWithHelpMarker("프레임 생성 (Enabler + FFX)",
                                    "중간 가상 프레임은 FFX가, 나머지는 Enabler가 맡습니다"
                                    "\n2x - FFX\n3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler");
        }

        if (state.activeFgInput == FGInput::NvngxFG)
        {

            bool dmfgActive = state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default();

            if (!ReflexHooks::isReflexHooked())
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Reflex not hooked");
                ImGui::Text("If you are using an AMD/Intel GPU, then make sure you have Fakenvapi");
            }
            else if (ReflexHooks::dlssgFrameCountToGenerate() == 0 && !dmfgActive)
            {
                ImGui::Text("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first");
            }

            if (state.swapchainApi == DX12)
            {
                ImGui::Text("Current DLSSG state:");
                ImGui::SameLine();
                if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
                {
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)),
                                       std::format("ON {}x", count + 1).c_str());
                }
                else
                {
                    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                }

                // Issue mostly shows up on AMD on Windows on pre-RDNA3 in some non-UE games
                // Hide to reduce confusion, config is still read
                const bool isUnrealEngine = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                                            State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine;
                const bool isDllProxyNvngxType =
                    activeNvngxFg == FGNvngxReplacement::Nukems || activeNvngxFg == FGNvngxReplacement::Arturs;
                if (isDllProxyNvngxType && !primaryGpu.dlssCapable && primaryGpu.fsr4Support == FSR4Support::None &&
                    !primaryGpu.usesVkd3dProton && !isUnrealEngine)
                {
                    if (bool makeDepthCopy = config->NvngxFGMakeDepthCopy.value_or_default();
                        ImGui::Checkbox("Fix broken visuals", &makeDepthCopy))
                    {
                        config->NvngxFGMakeDepthCopy = makeDepthCopy;
                    }
                    ShowHelpMarker("깊이 버퍼의 복사본을 만듭니다."
                                   "Windows의 AMD GPU에서 일부 게임의 깨진 화면을 고칠 수 있습니다.\n스터터를 일으킬 수 있으니 필요할 때만 쓰는 것이 좋습니다");
                }
            }
            else if (state.swapchainApi == Vulkan)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   "DLSSG is purposefully disabled when this menu is visible");
                ImGui::Spacing();
            }
        }

        bool isLoaded = false;
        if (state.swapchainApi == Vulkan)
            isLoaded = Nvngx_FG::isVulkanAvailable();
        if (state.swapchainApi == DX12)
            isLoaded = Nvngx_FG::isDx12Available();

        if (isLoaded)
        {
            if (activeNvngxFg == FGNvngxReplacement::Arturs || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                auto featureVer = Nvngx_FG::version();
                auto antighostingVer = Nvngx_FG::extraVersion();
                ImGui::Text("DE Ver: %d.%d.%d.%d   GB Ver: %d.%d", featureVer.major, featureVer.minor, featureVer.patch,
                            featureVer.reserved, antighostingVer.major, antighostingVer.minor);

                static std::vector<FlagDefinition> common_flags = {
                    { "Antighosting (GB)", 0x00100000, "Enable anti-ghosting correction" },
                    { "Temporal HUD pin", 0x04000000, "Enable temporal HUD pinning (present-backbuffer stability)" }
                };

                static std::vector<FlagDefinition> uncommon_flags = {
                    //{ "Hudless UI mask", 0x02000000, "Use HUD-less as UI mask (DL2 inverted semantics)" },
                    { "HUD interpolation", 0x08000000, "HUD OF interpolation (0=legacy pin-present, 1=OF warp)" },
                    { "Ignore UI texture", 0x10000000, "Ignore dedicated DLSSG.UI texture (force legacy HUD path)" },
                    //{ "Dp4a active", 0x20000000, "OF pipeline using dp4a-accelerated SSD (SM 6.4+)" },
                    { "Pin backbuffer", 0x40000000, "Pin DLSSG.Backbuffer to subframe-1 snapshot across MFG frame" }
                };

                static std::vector<FlagDefinition> debug_flags = {
                    { "Antighosting red tint", 0x00200000, "Debug: red tint on corrected pixels" },
                    { "Antighosting split screen", 0x00400000, "Debug: split screen comparison" },
                    { "Frame index line", 0x00010000, "" },
                    { "HUD detection", 0x00020000, "" },
                    { "Disocclusion tint", 0x00040000, "" },
                    { "Artifacts detection", 0x00080000, "" },
                    { "Camera MV debug", 0x00800000, "Debug: blue tint where camera MV fallback is used" },
                    { "Generic visualization", 0x01000000, "Debug: trapezoid zone visualization" }
                };

                uint32_t temp_flags = config->NvngxFGDispatchFlags.value_or_default();
                bool changed = false;

                ImGui::Text("Raw DispatchFlags:");
                changed |= ImGui::InputScalar("##RawFlags", ImGuiDataType_U32, &temp_flags, NULL, NULL, "%08X",
                                              ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                if (bool showDebug = config->NvngxFGShowDebug.value_or_default();
                    ImGui::Checkbox("Show Debug", &showDebug))
                {
                    config->NvngxFGShowDebug = showDebug;
                }
                ShowHelpMarker("Debug 플래그가 올바르게 작동하는 데 필요합니다");

                ImGui::Spacing();

                if (auto ch = ScopedCollapsingHeader("Active DispatchFlags"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};

                    auto render_flags = [&](const std::vector<FlagDefinition>& flags)
                    {
                        for (const auto& flag : flags)
                        {
                            changed |= ImGui::CheckboxFlags(flag.name.c_str(), &temp_flags, flag.mask);

                            if (ImGui::IsItemHovered() && !flag.description.empty())
                            {
                                ImGui::SetTooltip("%s", flag.description.c_str());
                            }
                        }
                    };

                    ImGui::TextDisabled("Common");
                    render_flags(common_flags);

                    ImGui::Spacing();
                    ImGui::TextDisabled("Uncommon");
                    render_flags(uncommon_flags);

                    if (config->NvngxFGShowDebug.value_or_default())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Debug");
                        render_flags(debug_flags);
                    }
                }

                if (changed)
                {
                    config->NvngxFGDispatchFlags = temp_flags;
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                if (ImGui::Checkbox("Enable Debug View", &state.dlssgDebugView))
                {
                    Nvngx_FG::setDebugView(state.dlssgDebugView);
                }
                if (ImGui::Checkbox("Interpolated frames only", &state.dlssgInterpolatedOnly))
                {
                    Nvngx_FG::setInterpolatedOnly(state.dlssgInterpolatedOnly);
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::FFX || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                if (_ffxFGIndex < 0)
                    _ffxFGIndex = config->FfxFGIndex.value_or_default();

                if (state.ffxFGVersionNames.size() > 0)
                {
                    ImGui::PushItemWidth(135.0f * menuResScale);

                    auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                    if (ImGui::BeginCombo("FFX FG", currentName.c_str()))
                    {
                        for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                        {
                            auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                            if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                                _ffxFGIndex = n;
                        }

                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ShowHelpMarker("FFX SDK가 보고한 FG 목록입니다");

                    ImGui::SameLine(0.0f, 6.0f);

                    if (ImGui::Button("Change FG") && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                    {
                        config->FfxFGIndex = _ffxFGIndex;
                        state.fgChanged = true;
                    }
                }

                bool fgAsync = config->FGAsync.value_or_default();
                if (ImGui::Checkbox("Allow Async##2", &fgAsync))
                {
                    config->FGAsync = fgAsync;

                    if (config->FGEnabled.value_or_default())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("Async set FGChanged");
                    }
                }
                ShowHelpMarker("FG 성능을 높이려면 Async를 활성화합니다.\n특히 HUD Fix와 함께 쓸 때 충돌이 발생할 수 있습니다!");

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                bool fgDV = config->FGDebugView.value_or_default();
                if (ImGui::Checkbox("Debug View##3", &fgDV))
                {
                    config->FGDebugView = fgDV;

                    if (config->FGEnabled.value_or_default())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("DebugView set FGChanged");
                    }
                }
                ShowHelpMarker("FSR3.1-FG 디버그 뷰를 활성화합니다"
                               ""
                               "좌상단: 게임 모션 벡터"
                               "중앙 상단: GMV Depth"
                               "우상단: Optical Flow MV"
                               "중앙: 보간된 프레임만 표시"
                               "좌하단: Disocclusion 마스크"
                               "중앙 하단: 보간 소스(UI 제외)\n우하단: HUDless 리소스");

                if (Nvngx_FG::version().major > 3)
                {
                    ImGui::SameLine(0.0f, 20.0f * menuResScale);
                    if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                        ImGui::Checkbox("Enable Watermark", &fgwm))
                    {
                        LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                        config->FSRFGEnableWatermark = fgwm;
                    }

                    ShowHelpMarker("이 옵션을 변경한 후에는 설정을 저장해 주세요."
                                   "다음 실행 때 적용됩니다.");
                }
            }

            if (bool disableHudless = config->NvngxFGDisableHudless.value_or_default();
                ImGui::Checkbox("Disable HUDless", &disableHudless))
            {
                config->NvngxFGDisableHudless = disableHudless;
            }
            ShowHelpMarker("일부 DispatchFlags 조합에서는 필요할 수 있습니다");
        }
    }

    // FSR-FG Inputs
    if (state.currentFGSwapchain != nullptr &&
        (state.activeFgInput == FGInput::FSRFG || state.activeFgInput == FGInput::FSRFG30))
    {
        SeparatorWithHelpMarker("프레임 생성 (FSR-FG Inputs)", "게임 내에서 FSR-FG를 선택합니다");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (fgOutput != nullptr)
        {
            ImGui::Text("Current FSR-FG state:");
            ImGui::SameLine();
            if (state.fsrfgInputActive)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), "ON");
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), "ACTIVATE FG");
            }
            else
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                ImGui::Text("Please select FSR Frame Generation in the game options\n"
                            "You might need to select FSR first");
            }
        }

        bool skipConfig = config->FSRFGSkipConfigForHudless.value_or_default();
        if (ImGui::Checkbox("Skip Config for HUDless", &skipConfig))
            config->FSRFGSkipConfigForHudless = skipConfig;

        ShowHelpMarker("ffxConfig에서 HUDless 세트를 사용하지 않습니다");

        ImGui::SameLine(0.0f, 6.0f);

        bool skipDispatch = config->FSRFGSkipDispatchForHudless.value_or_default();
        if (ImGui::Checkbox("Skip Dispatch for HUDless", &skipDispatch))
            config->FSRFGSkipDispatchForHudless = skipDispatch;

        ShowHelpMarker("ffxDispatch에서 HUDless 세트를 사용하지 않습니다");
    }

    // Streamline FG Inputs
    if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
    {
        SeparatorWithHelpMarker("프레임 생성 (Streamline FG Inputs)", "게임 내에서 DLSS-FG를 선택합니다");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

        if (!ReflexHooks::isReflexHooked())
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Reflex not hooked");
            ImGui::Text("If you are using an AMD/Intel GPU, then make sure you have fakenvapi");
        }
        else if (fgOutput != nullptr)
        {
            ImGui::Text("Current Streamline FG state:");
            ImGui::SameLine();
            if ((state.fgLastFrame - state.dlssgLastFrame) < 3)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), "ON");
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), "ACTIVATE FG");
            }
            else
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                ImGui::Text("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first");
            }
        }
    }
}

void MenuCommon::RenderFsrCommonSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // FSR Common -----------------
        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            (state.activeFgOutput == FGOutput::FSRFG || IsFsr(currentBackend)))
        {
            SeparatorWithHelpMarker("FSR 공통 설정", "FSR-FG와 업스케일러 모두에 적용됩니다");

            bool useFsrVales = config->FsrUseFsrInputValues.value_or_default();
            if (ImGui::Checkbox("Use FSR Input Values", &useFsrVales))
                config->FsrUseFsrInputValues = useFsrVales;

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("FoV & Camera Values"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                bool useVFov = config->FsrVerticalFov.has_value() || !config->FsrHorizontalFov.has_value();

                float vfov = config->FsrVerticalFov.value_or_default();
                float hfov = config->FsrHorizontalFov.value_or(90.0f);

                if (useVFov && !config->FsrVerticalFov.has_value())
                    config->FsrVerticalFov = vfov;
                else if (!useVFov && !config->FsrHorizontalFov.has_value())
                    config->FsrHorizontalFov = hfov;

                if (ImGui::RadioButton("Use Vert. Fov", useVFov))
                {
                    config->FsrHorizontalFov.reset();
                    config->FsrVerticalFov = vfov;
                    useVFov = true;
                }

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::RadioButton("Use Horz. Fov", !useVFov))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov = hfov;
                    useVFov = false;
                }

                if (useVFov)
                {
                    if (ImGui::SliderFloat("Vert. FOV", &vfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrVerticalFov = vfov;

                    ShowHelpMarker("더 나은 이미지 품질을 얻는 데 도움이 될 수 있습니다");
                }
                else
                {
                    if (ImGui::SliderFloat("Horz. FOV", &hfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrHorizontalFov = hfov;

                    ShowHelpMarker("더 나은 이미지 품질을 얻는 데 도움이 될 수 있습니다");
                }

                float cameraNear;
                float cameraFar;

                cameraNear = config->FsrCameraNear.value_or_default();
                cameraFar = config->FsrCameraFar.value_or_default();

                if (ImGui::SliderFloat("Camera Near", &cameraNear, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraNear = cameraNear;
                ShowHelpMarker("더 나은 이미지 품질을 얻는 데 도움이 될 수 있습니다."
                               "고스팅도 줄어들 수 있습니다.");

                if (ImGui::SliderFloat("Camera Far", &cameraFar, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraFar = cameraFar;
                ShowHelpMarker("더 나은 이미지 품질을 얻는 데 도움이 될 수 있습니다."
                               "고스팅도 줄어들 수 있습니다.");

                if (ImGui::Button("Reset Camera Values"))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov.reset();
                    config->FsrCameraNear.reset();
                    config->FsrCameraFar.reset();
                }

                ImGui::SameLine(0.0f, 6.0f);
                ImGui::Text("Near: %.1f Far: %.1f",
                            state.lastFsrCameraNear < 500000.0f ? state.lastFsrCameraNear : 500000.0f,
                            state.lastFsrCameraFar < 500000.0f ? state.lastFsrCameraFar : 500000.0f);

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderFramerateSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    // Framerate ---------------------
    if (state.reflexLimitsFps || config->OverlayMenu.value_or_default())
    {
        SeparatorWithHelpMarker(
            "프레임레이트", "가능하면 Reflex를 사용합니다.\nAMD/Intel 카드에서는 Fakenvapi로 Reflex를 대체할 수 있습니다");

        static std::string currentMethod {};
        LowLatencyMode fakenvapiMode = {};
        if (state.reflexLimitsFps)
        {
            fakenvapiMode = fakenvapi::getCurrentMode();

            if (fakenvapiMode == LowLatencyMode::AntiLag2)
                currentMethod = "FSR Anti-Lag 2.0";
            else if (fakenvapiMode == LowLatencyMode::LatencyFlex)
                currentMethod = "LatencyFlex";
            else if (fakenvapiMode == LowLatencyMode::XeLL)
                currentMethod = "XeLL";
            else if (fakenvapiMode == LowLatencyMode::AntiLagVk)
                currentMethod = "Vulkan AntiLag";
            else if (fakenvapiMode == LowLatencyMode::None)
            {
                if (fakenvapi::isUsingAsMainNvapi())
                    currentMethod = "None";
                else
                    currentMethod = "Reflex";
            }

            if (state.rtssReflexInjection && fakenvapiMode == LowLatencyMode::AntiLag2 &&
                config->FGOutput.value_or_default() == FGOutput::FSRFG)
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   "Using RTSS Reflex injection with FSR Anti-Lag 2.0 and FSR FG "
                                   "might cause issues");
        }
        else
        {
            if (XellHooks::canLimit())
                currentMethod = "Game's XeLL";
            else
                currentMethod = "Fallback";
        }

        if (state.rtssReflexInjection)
            currentMethod.append(" (RTSS)");

        const bool fakenvapiInactive = (fakenvapi::isUsingAsMainNvapi() || fakenvapiMode == LowLatencyMode::XeLL) &&
                                       !fakenvapi::isLowLatencyActive() && state.reflexLimitsFps;

        if (fakenvapiInactive)
            currentMethod.append(" (inactive)");

        ImGui::Text("Current method: %s", currentMethod.c_str());

        if (fakenvapiMode == LowLatencyMode::AntiLag2)
            ShowHelpMarker("FSR Anti-Lag 2.0은 AntiLag 2의 새 이름입니다.\n왜 이름을 바꿨는지는 저도 모릅니다");

        if (state.reflexShowWarning)
        {
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                               "Using Reflex's limit with FSR FG has performance overhead");

            ImGui::Spacing();
        }

        // set initial value
        if (std::isinf(_limitFps))
            _limitFps = config->FramerateLimit.value_or_default();

        ImGui::SliderFloat("FPS Limit", &_limitFps, 0, 200, "%.0f");

        if (ImGui::Button("Apply Limit"))
        {
            config->FramerateLimit = _limitFps;
        }

        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Button("Reset Limit"))
        {
            _limitFps = 0.0f;
            config->FramerateLimit = _limitFps;
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("VRR Frame Cap Calculator"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            ImGui::PushItemWidth(105.0f * menuResScale);
            ImGui::InputInt("Refresh Rate", &refreshRate, 1, 1, ImGuiInputTextFlags_None);
            ImGui::PopItemWidth();

            float refreshRateF = static_cast<float>(refreshRate);
            // it's fine to use with real reflex, we only care about antilag
            auto fpsLimitTech = fakenvapi::getCurrentMode();
            constexpr float margin = 0.3f; // in ms
            float frameCap = std::round(10000.f / (1000.f / refreshRateF + margin)) / 10.f;

            if (fpsLimitTech == LowLatencyMode::AntiLag2 || fpsLimitTech == LowLatencyMode::AntiLagVk)
                frameCap = std::round(frameCap);

            ImGui::Text("Calculated Cap: %.1f", frameCap);

            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Set as FPS Limit"))
            {
                _limitFps = frameCap;
                config->FramerateLimit = _limitFps;
            }
        }
    }
}

void MenuCommon::RenderFakenvapiSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // FAKENVAPI ---------------------------
    ImGui::SeparatorText("fakenvapi");

    // Using state.reflexLimitsFps as a detection for Reflex being used on Nvidia
    bool showLatencyFlex =
        fakenvapi::isUsingAsMainNvapi() || (state.activeFgOutput == FGOutput::XeFG && state.reflexLimitsFps);

    if (showLatencyFlex)
    {
        ImGui::BeginDisabled(state.activeFgOutput == FGOutput::XeFG || state.activeFgInput == FGInput::ForceXeLL);
        if (bool forceLFX = config->FN_ForceLatencyFlex.value_or_default();
            ImGui::Checkbox("Force LatencyFlex", &forceLFX))
        {
            config->FN_ForceLatencyFlex = forceLFX;
        }
        ShowHelpMarker("기본적으로는 사용 가능한 경우 FSR Anti-Lag 2.0/XeLL을 사용합니다."
                       "이 설정으로 LatencyFlex를 대신 사용하도록 강제할 수 있습니다");
        ImGui::EndDisabled();

        // Keep Force XeLL on the same line if LatencyFlex is visible
        ImGui::SameLine(0.0f, 16.0f);
    }

    // Force XeLL is always visible
    bool forceXell = config->ForceXeLL.value_or_default();
    static bool activeForceXeLL = forceXell;

    if (ImGui::Checkbox("Force XeLL", &forceXell))
    {
        config->ForceXeLL = forceXell;
    }
    ShowHelpMarker("Intel이 아닌 카드에서 FG 없이 XeLL이 작동하도록 허용합니다."
                   "\nFG 옵션을 비활성화합니다.\n\n재시작이 필요합니다");

    if (activeForceXeLL != forceXell)
    {
        ImGui::Spacing();
        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)), "Save INI and restart to apply the changes");
        ImGui::Spacing();
    }

    if (showLatencyFlex)
    {
        // clang-format off
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative",
                "The safest, but might not reduce latency well" },
            { LFXMode::Aggressive, "Aggressive",
                "Improves latency, but in some cases will lower FPS more than expected" },
            { LFXMode::ReflexIDs, "Reflex ID",
                "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
                "and will fallback to Aggressive" }
        };

        bool usingLFX = fakenvapi::getCurrentMode() == LowLatencyMode::LatencyFlex;

        ImGui::BeginDisabled(!usingLFX);
        PopulateCombo("LatencyFlex mode", config->FN_LatencyFlexMode, lfx_modes);
        ImGui::EndDisabled();

        static std::vector<MenuOption<ForceReflex>> reflex_modes = { { ForceReflex::InGame, "Follow in-game" },
                                                                { ForceReflex::ForceDisable, "Force Disable" },
                                                                { ForceReflex::ForceEnable, "Force Enable" } };

        PopulateCombo("Force Reflex", config->FN_ForceReflex, reflex_modes);
        // clang-format on
    }
}

template <typename T> std::string GetMenuOptionLabel(const std::vector<MenuOption<T>>& options, T targetValue)
{
    auto it = std::find_if(options.begin(), options.end(),
                           [targetValue](const MenuOption<T>& option) { return option.value == targetValue; });

    if (it != options.end())
    {
        return it->label;
    }

    return "Unknown";
}

void MenuCommon::RenderLowLatencySettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Low Latency ---------------------------
    ImGui::SeparatorText("Low Latency");

    static std::vector<MenuOption<LowLatencyInput>> lowLatencyInput = {
        { LowLatencyInput::None, "None (Off)" },    { LowLatencyInput::Auto, "Auto" },
        { LowLatencyInput::AntiLag2, "AntiLag 2" }, { LowLatencyInput::Reflex, "Reflex" },
        { LowLatencyInput::XeLL, "XeLL" },          { LowLatencyInput::UeLowLatency, "UeLowLatency" },
    };

    static std::vector<MenuOption<LowLatencyMode>> lowLatencyOutput = {
        { LowLatencyMode::None, "None (Off)" },
        { LowLatencyMode::Auto, "Auto" },
        { LowLatencyMode::LatencyFlex, "LatencyFlex" },
        { LowLatencyMode::AntiLag2, "AntiLag 2" },
        { LowLatencyMode::XeLL, "XeLL" },
        { LowLatencyMode::AntiLagVk, "AntiLag Vk" },
        { LowLatencyMode::Reflex, "Reflex" },
    };

    LowLatencyInput activeInput {};
    LowLatencyMode activeOutput {};

    if (ImGui::BeginTable("lowLatencyActive", 2, ImGuiTableFlags_SizingStretchSame))
    {
        InputCommon::get_currently_active(activeInput, activeOutput);

        ImGui::TableNextColumn();

        ImGui::Text("Active input: %s", GetMenuOptionLabel(lowLatencyInput, activeInput).c_str());

        ImGui::TableNextColumn();

        ImGui::Text("Active output: %s", GetMenuOptionLabel(lowLatencyOutput, activeOutput).c_str());

        ImGui::EndTable();
    }

    if (ImGui::BeginTable("lowLatencySelection", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        auto avalibleInputs = InputCommon::get_avaliable_inputs();

        lowLatencyInput[(uint32_t) LowLatencyInput::AntiLag2].set_disabled(!avalibleInputs[LowLatencyInput::AntiLag2]);
        lowLatencyInput[(uint32_t) LowLatencyInput::Reflex].set_disabled(!avalibleInputs[LowLatencyInput::Reflex]);
        lowLatencyInput[(uint32_t) LowLatencyInput::XeLL].set_disabled(!avalibleInputs[LowLatencyInput::XeLL]);
        lowLatencyInput[(uint32_t) LowLatencyInput::UeLowLatency].set_disabled(
            !avalibleInputs[LowLatencyInput::UeLowLatency]);

        // need to have a value before combo
        if (!config->LowLatencyInput.has_value())
            config->LowLatencyInput = config->LowLatencyInput.value_or_default();

        PopulateCombo("Input", config->LowLatencyInput, lowLatencyInput);

        ImGui::TableNextColumn();

        lowLatencyOutput[(uint32_t) LowLatencyMode::AntiLagVk].set_disabled(true, "No support");
        lowLatencyOutput[(uint32_t) LowLatencyMode::Reflex].set_disabled(true, "No support");

        // need to have a value before combo
        if (!config->LowLatencyOutput.has_value())
            config->LowLatencyOutput = config->LowLatencyOutput.value_or_default();

        PopulateCombo("Output", config->LowLatencyOutput, lowLatencyOutput);

        ImGui::EndTable();
    }

    if (activeOutput == LowLatencyMode::LatencyFlex)
    {
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative", "The safest, but might not reduce latency well" },
            { LFXMode::Aggressive, "Aggressive",
              "Improves latency, but in some cases will lower FPS more than expected" },
            { LFXMode::ReflexIDs, "Reflex ID",
              "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
              "and will fallback to Aggressive" }
        };

        PopulateCombo("LatencyFlex mode", config->FN_LatencyFlexMode, lfx_modes);
    }

    static std::vector<MenuOption<ForceReflex>> lowlatency_states = { { ForceReflex::InGame, "Follow in-game" },
                                                                      { ForceReflex::ForceDisable, "Force Disable" },
                                                                      { ForceReflex::ForceEnable, "Force Enable" } };

    ImGui::SetNextItemWidth(150.0f * ctx.menuResScale);
    PopulateCombo("Force State", config->FN_ForceReflex, lowlatency_states);
}

void MenuCommon::RenderActiveImageSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    bool rcasEnabled = false;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // SHARPNESS -----------------------------
        ImGui::SeparatorText("Sharpness");

        if (bool overrideSharpness = config->OverrideSharpness.value_or_default();
            ImGui::Checkbox("Override", &overrideSharpness))
        {
            config->OverrideSharpness = overrideSharpness;

            if (currentBackend == Upscaler::DLSS && currentFeature->Version().major < 3)
            {
                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
        }
        ShowHelpMarker("게임이 전달하는 값을 무시하고"
                       "아래에서 설정한 값을 사용합니다");

        ImGui::SameLine(0.0f, 16.0f * menuResScale);

        float featuresCurrentSharpness = currentFeature->Sharpness();
        if (featuresCurrentSharpness > 0.0f)
            ImGui::TextDisabled("(현재 선명도: %.3f)", featuresCurrentSharpness);
        else
            ImGui::TextDisabled("(현재 선명도: 비활성화)");

        ImGui::BeginDisabled(!config->OverrideSharpness.value_or_default());

        float sharpness = config->Sharpness.value_or_default();

        if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f))
            config->Sharpness = sharpness;

        ImGui::EndDisabled();

        // RCAS
        // if (state.api == DX12 || state.api == DX11)
        {
            // xess or dlss version >= 2.5.1
            constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
            rcasEnabled = (currentBackend == Upscaler::XeSS ||
                           (currentBackend == Upscaler::DLSS && currentFeature->Version() >= requiredDlssVersion));

            ImGui::Spacing();
            ImGui::Spacing();

            if (bool rcas = config->RcasEnabled.value_or(rcasEnabled); ImGui::Checkbox("Enable RCAS/DA", &rcas))
                config->RcasEnabled = rcas;

            ShowHelpMarker("OptiScaler의 샤프닝 필터를 활성화합니다"
                           "기본값으로는 게임이 제공하는 선명도 값을 사용합니다"
                           "변경하려면 'Sharpness' 아래에서 'Override'를 선택하고"
                           "슬라이더를 조정하세요"
                           ""
                           "일부 업스케일러에는 자체 샤프닝 필터가 있으므로\n이 옵션이 항상 필요한 것은 아닙니다");

            ImGui::BeginDisabled(!config->RcasEnabled.value_or(rcasEnabled));

            auto sharpnessShader = (int32_t) Config::Instance()->SharpnessShader.value_or_default();

            if (ImGui::RadioButton("RCAS", &sharpnessShader, (int32_t) SharpenShader::RCAS))
            {
                Config::Instance()->SharpnessShader = SharpenShader::RCAS;
            }

            ShowHelpMarker("AMD의 RCAS를 사용합니다"
                           "Contrast 매개변수와"
                           "MAS 지원을 추가하도록 수정된 버전입니다");

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::RadioButton("Depth Aware (RCAS)", &sharpnessShader, (int32_t) SharpenShader::DepthAware))
            {
                Config::Instance()->SharpnessShader = SharpenShader::DepthAware;
            }

            ShowHelpMarker("Depth Aware Sharpening(RCAS)을 사용합니다"
                           "아티팩트가 적은 더 똑똑한 샤프닝이지만"
                           "부하도 더 큽니다"
                           ""
                           "객체가 멀수록\n더 많은 샤프닝이 적용됩니다");

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::RadioButton("Depth Aware (DAS)", &sharpnessShader,
                                   (int32_t) SharpenShader::LocalContrastDepthAware))
            {
                Config::Instance()->SharpnessShader = SharpenShader::LocalContrastDepthAware;
            }

            ShowHelpMarker("Depth Aware Sharpening(DAS)을 사용합니다"
                           "깊이를 인식하는 방향성 적응형 루마 샤프너입니다"
                           "아티팩트가 적은 더 똑똑한 샤프닝이지만"
                           "부하도 더 큽니다"
                           ""
                           "객체가 멀수록\n더 많은 샤프닝이 적용됩니다");

            ImGui::Spacing();

            if (bool overrideMotionSharpness = config->MotionSharpnessEnabled.value_or_default();
                ImGui::Checkbox("Enable Motion Adaptive Sharpness", &overrideMotionSharpness))
                config->MotionSharpnessEnabled = overrideMotionSharpness;
            ShowHelpMarker("움직임에 따라 선명도를 조정합니다");

            if (Config::Instance()->SharpnessShader.value_or_default() != SharpenShader::RCAS)
            {
                if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                    ImGui::Checkbox("DA + MAS Debug", &overrideMSDebug))
                    config->MotionSharpnessDebug = overrideMSDebug;

                ShowHelpMarker("DA + MAS 디버그 뷰를 활성화합니다"
                               "DA가 감지한 가장자리에는 파란색 틴트가 표시됩니다"
                               ""
                               "붉은 영역일수록 더 많은 샤프닝이 적용되고\n녹색 영역은 샤프닝이 줄어듭니다");

                if (auto ch = ScopedCollapsingHeader("Advanced DA Parameters"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (bool clamp = config->DAClampOutput.value_or(false); ImGui::Checkbox("Clamp Output", &clamp))
                    {
                        if (clamp)
                            config->DAClampOutput = true;
                        else
                            config->DAClampOutput.reset();
                    }

                    ShowHelpMarker("최종 이미지를 [0, 1] 범위로 제한합니다."
                                   ""
                                   "밝은 헤일로나 음수 색상 같은 오버슈트 아티팩트를 방지합니다."
                                   "LDR 파이프라인에서는 권장되며, HDR에서는 톤매핑에 따라 선택적으로 사용합니다.\n\n설정하지 않으면 OptiScaler가 업스케일러의 HDR 플래그로 이를 제어합니다");

                    if (currentFeature->DepthLinear())
                    {
                        float depthBias = config->DADepthBias.value_or(0.0015f);
                        if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.005f, 0.03f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowHelpMarker("가장자리 감지 전에 작은 깊이 차이를 무시합니다.\n\n값이 높으면"
                                       "사소한 깊이 변화로 인한 깜빡임과 노이즈가 줄어들지만 실제 지오메트리 가장자리가"
                                       "부드러워질 수 있습니다.\n값이 낮으면"
                                       "미세한 디테일이 유지되지만 가장자리 감지가 불안정해지거나 노이즈가 생길 수"
                                       "있습니다.");

                        float depthScale = config->DADepthScale.value_or(250.0f);
                        if (ImGui::SliderFloat("Depth Scale", &depthScale, 100.0f, 600.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowHelpMarker("깊이 가장자리를 가로지르는 샤프닝 감소 정도를 제어합니다.\n\n값이"
                                       "높으면 객체 경계를 넘는 샤프닝을 더 적극적으로 막습니다(헤일로"
                                       "감소).\n값이 낮으면"
                                       "가장자리를 넘는 샤프닝이 더 많이 허용됩니다(더 선명하지만 위험도도"
                                       "높아집니다).");
                    }
                    else
                    {
                        float depthBias = config->DADepthBias.value_or(0.001f);
                        if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.0001f, 0.003f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowHelpMarker("가장자리 감지 전에 작은 깊이 차이를 무시합니다.\n\n값이 높으면"
                                       "사소한 깊이 변화로 인한 깜빡임과 노이즈가 줄어들지만 실제 지오메트리 가장자리가"
                                       "부드러워질 수 있습니다.\n값이 낮으면"
                                       "미세한 디테일이 유지되지만 가장자리 감지가 불안정해지거나 노이즈가 생길 수"
                                       "있습니다.");

                        float depthScale = config->DADepthScale.value_or(35.0f);
                        if (ImGui::SliderFloat("Depth Scale", &depthScale, 25.0f, 400.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowHelpMarker("깊이 가장자리를 가로지르는 샤프닝 감소 정도를 제어합니다.\n\n값이"
                                       "높으면 객체 경계를 넘는 샤프닝을 더 적극적으로 막습니다(헤일로"
                                       "감소).\n값이 낮으면"
                                       "가장자리를 넘는 샤프닝이 더 많이 허용됩니다(더 선명하지만 위험도도"
                                       "높아집니다).");
                    }

                    if (ImGui::Button("Reset Depth Values"))
                    {
                        config->DADepthBias.reset();
                        config->DADepthScale.reset();
                    }
                }
            }
            else
            {
                if (bool contrastEnabled = config->ContrastEnabled.value_or_default();
                    ImGui::Checkbox("Contrast Enabled", &contrastEnabled))
                    config->ContrastEnabled = contrastEnabled;

                ShowHelpMarker("고대비 영역의 선명도를 제어합니다.");

                ImGui::BeginDisabled(!config->ContrastEnabled.value_or_default());

                float contrast = config->Contrast.value_or_default();
                if (ImGui::SliderFloat("Contrast", &contrast, -2.0f, 2.0f, "%.2f"))
                    config->Contrast = contrast;

                ShowHelpMarker("양수 값은 고대비 영역의 선명도를 낮춥니다."
                               "음수 값은 고대비 영역의 선명도를 높입니다.");

                ImGui::EndDisabled();
            }

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Motion Adaptive Sharpness##2"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::BeginDisabled(!config->MotionSharpnessEnabled.value_or_default());

                if (Config::Instance()->SharpnessShader.value_or_default() == SharpenShader::RCAS)
                {
                    if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                        ImGui::Checkbox("MAS Debug", &overrideMSDebug))
                        config->MotionSharpnessDebug = overrideMSDebug;
                    ShowHelpMarker("더 붉은 영역일수록 더 많은 샤프닝이 적용됩니다"
                                   "녹색 영역은 샤프닝이 줄어듭니다");
                }

                float motionSharpness = config->MotionSharpness.value_or_default();
                ImGui::SliderFloat("MotionSharpness", &motionSharpness, -1.0f, 1.0f, "%.3f");
                config->MotionSharpness = motionSharpness;

                ShowHelpMarker("움직임이 더하거나 뺄 수 있는 선명도의 최대치입니다."
                               ""
                               "음수 값은 움직임 중 샤프닝을 줄입니다(권장)."
                               "양수 값은 움직임 중 샤프닝을 높입니다.\n\n최종 조정은 움직임에 비례하며 이 값이 상한입니다.");

                float motionThreshod = config->MotionThreshold.value_or_default();
                ImGui::SliderFloat("MotionThreshod", &motionThreshod, 0.0f, 100.0f, "%.2f");
                config->MotionThreshold = motionThreshod;

                ShowHelpMarker("움직임 기반 선명도 조정이 시작되기 위해 필요한 최소 움직임입니다."
                               ""
                               "값이 높으면 작은 움직임을 무시합니다(더 안정적).\n값이 낮으면 미세한 움직임에도 반응합니다(더 민감).");

                float motionScale = config->MotionScaleLimit.value_or_default();
                ImGui::SliderFloat("MotionRange", &motionScale, 0.01f, 100.0f, "%.2f");
                config->MotionScaleLimit = motionScale;

                ShowHelpMarker("효과가 0에서 최대치까지 올라가는 움직임 범위를 정의합니다."
                               ""
                               "임계값을 넘은 움직임은 이 범위에 매핑됩니다."
                               "값이 크면 반응이 더 부드럽고 완만해집니다.\n값이 작으면 효과가 더 빠르고 강하게 반응합니다.");

                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }

            ImGui::EndDisabled();
        }

        // UPSCALE RATIO OVERRIDE -----------------

        auto minSliderLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
        auto maxSliderLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

        ImGui::SeparatorText("Upscale Ratio Override");

        if (bool upOverride = config->UpscaleRatioOverrideEnabled.value_or_default();
            ImGui::Checkbox("Override all", &upOverride))
        {
            config->UpscaleRatioOverrideEnabled = upOverride;

            if (upOverride)
                config->QualityRatioOverrideEnabled = false;
        }
        ShowHelpMarker("설정한 값으로 모든 업스케일러 프리셋을 덮어씁니다"
                       ""
                       "1080p 화면에서 1.5x는 내부 해상도 720p를 의미합니다\n1080 / 1.5 = 720");

        if (bool qOverride = config->QualityRatioOverrideEnabled.value_or_default();
            ImGui::Checkbox("Override per quality preset", &qOverride))
        {
            config->QualityRatioOverrideEnabled = qOverride;

            if (qOverride)
                config->UpscaleRatioOverrideEnabled = false;
        }

        ShowHelpMarker("각 프리셋의 배율을 개별적으로 덮어쓸 수 있습니다"
                       "모든 게임이 모든 품질 프리셋을 지원하는 것은 아닙니다"
                       ""
                       "1080p 화면에서 1.5x는 내부 해상도 720p를 의미합니다\n1080 / 1.5 = 720");

        if (config->UpscaleRatioOverrideEnabled.value_or_default())
        {
            float urOverride = config->UpscaleRatioOverrideValue.value_or_default();
            ImGui::SliderFloat("All Ratios", &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
            config->UpscaleRatioOverrideValue = urOverride;
        }

        if (config->QualityRatioOverrideEnabled.value_or_default())
        {
            float qDlaa = config->QualityRatio_DLAA.value_or_default();
            if (ImGui::SliderFloat("DLAA", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_DLAA = qDlaa;

            float qUq = config->QualityRatio_UltraQuality.value_or_default();
            if (ImGui::SliderFloat("Ultra Quality", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_UltraQuality = qUq;

            float qQ = config->QualityRatio_Quality.value_or_default();
            if (ImGui::SliderFloat("Quality", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_Quality = qQ;

            float qB = config->QualityRatio_Balanced.value_or_default();
            if (ImGui::SliderFloat("Balanced", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_Balanced = qB;

            float qP = config->QualityRatio_Performance.value_or_default();
            if (ImGui::SliderFloat("Performance", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_Performance = qP;

            float qUp = config->QualityRatio_UltraPerformance.value_or_default();
            if (ImGui::SliderFloat("Ultra Performance", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_UltraPerformance = qUp;
        }

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            // OUTPUT SCALING -----------------------------
            // if (state.api == DX12 || state.api == DX11)
            {
                // if motion vectors are not display size
                ImGui::BeginDisabled(!currentFeature->LowResMV() &&
                                     currentFeature->RenderWidth() != currentFeature->DisplayWidth());

                ImGui::SeparatorText("Output Scaling");

                float defaultRatio = 1.5f;

                if (_ssRatio == 0.0f)
                {
                    _ssRatio = config->OutputScalingMultiplier.value_or(defaultRatio);
                    _ssEnabled = config->OutputScalingEnabled.value_or_default();
                    _ssDownsampler = config->OutputScalingDownscaler.value_or_default();
                }

                ImGui::BeginDisabled((currentBackend == Upscaler::XeSS || currentBackend == Upscaler::DLSS) &&
                                     currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                ImGui::Checkbox("Enable", &_ssEnabled);
                ImGui::EndDisabled();

                ShowHelpMarker("이미지를 내부적으로 더 높은 출력 해상도로 업스케일한 뒤"
                               "다시 디스플레이 해상도로 다운스케일합니다"
                               ""
                               "1.0 미만 값은 업스케일러 부담을 줄입니다"
                               "1.0 초과 값은 성능을 대가로 이미지를 더 선명하게 만듭니다"
                               "\n회색으로 비활성화되어 있다면 Git Wiki - Unreal Engine tweaks 문서를 확인하세요\n\n하단의 Target res와 총 배율을 참고하세요 (총 배율 최대 3.0!)");

                ImGui::SameLine(0.0f, 6.0f);

                ImGui::BeginDisabled(!_ssEnabled);
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);

                    // clang-format off
                    std::vector<MenuOption<Scaler>> ds_options = {
                        { Scaler::FSR1, "FSR1",
                            "Default option.\nGood enough image quality and very fast." },
                        { Scaler::Bicubic, "Bicubic",
                            "Fastest traditional option.\nProduces a very soft/blurry image, but might be okay for downscaling." },
                        { Scaler::CatmullRom, "Catmull-Rom",
                            "Designed primarily for downscaling.\nRetains good contrast with minimal artefacts, but softer than Lanczos." },
                        { Scaler::Lanczos2, "Lanczos2",
                            "Lighter and faster than Lanczos3.\nLess prone to ringing artefacts, but slightly blurrier." },
                        { Scaler::Lanczos3, "Lanczos3",
                            "Heavier version of Lanczos2.\nOffers the sharpest image, but is the most prone to ringing.\nConsidered the best along with Kaiser3." },
                        { Scaler::Kaiser2, "Kaiser2",
                            "Similar to Lanczos2.\nSmoother and less prone to artefacts than Lanczos, but slightly blurrier." },
                        { Scaler::Kaiser3, "Kaiser3",
                            "Similar to Lanczos3.\nFar less prone to artefacting than Lanczos3, but much heavier on the GPU.\nConsidered the best along with Lanczos3." },
                        { Scaler::Magic, "MAGIC",
                            "Specialised to prevent artifacts.\nEliminates harsh halos for a natural look, but can appear slightly soft." }
                    };
                    // clang-format on

                    const bool isUpsampleRatio = _ssRatio < 1.0f;
                    const std::string disabledReason = "Only FSR1 and Bicubic are supported when Ratio is below 1.0.";

                    for (auto& opt : ds_options)
                    {
                        if (isUpsampleRatio && opt.value > Scaler::Bicubic)
                            opt.set_disabled(true, opt.tooltip + "\n\n" + disabledReason);
                    }

                    if (isUpsampleRatio && _ssDownsampler > Scaler::Bicubic)
                        _ssDownsampler = Scaler::FSR1;

                    PopulateCombo("Downscaler", _ssDownsampler, ds_options);

                    ImGui::PopItemWidth();
                }
                ImGui::EndDisabled();

                bool applyEnabled = _ssEnabled != config->OutputScalingEnabled.value_or_default() ||
                                    _ssRatio != config->OutputScalingMultiplier.value_or(defaultRatio) ||
                                    _ssDownsampler != config->OutputScalingDownscaler.value_or_default();

                ImGui::BeginDisabled(!applyEnabled);
                if (ImGui::Button("Apply Change"))
                {
                    config->OutputScalingEnabled = _ssEnabled;
                    config->OutputScalingMultiplier = _ssRatio;

                    if (_ssRatio < 1.0f && _ssDownsampler > Scaler::Bicubic)
                        _ssDownsampler = Scaler::FSR1;

                    config->OutputScalingDownscaler = _ssDownsampler;

                    const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
                    if (usesDlssd)
                        state.newBackend = Upscaler::DLSSD;
                    else
                        state.newBackend = currentBackend;

                    MARK_ALL_BACKENDS_CHANGED();
                }
                ImGui::EndDisabled();

                ImGui::BeginDisabled(!_ssEnabled || currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                ImGui::SliderFloat("Ratio", &_ssRatio, 0.5f, 3.0f, "%.2f");
                ImGui::EndDisabled();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    ImGui::Text("Output Scaling is %s, Target Res: %dx%d (%.2f)\nJitter Count: %d",
                                config->OutputScalingEnabled.value_or_default() ? "ENABLED" : "DISABLED",
                                (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                                (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                                ((float) currentFeature->DisplayWidth() * _ssRatio) /
                                    (float) currentFeature->RenderWidth(),
                                currentFeature->JitterCount());
                }

                ImGui::EndDisabled();
            }
        }

        // INIT -----------------------------
        ImGui::SeparatorText("Init Flags");
        if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();

            // AutoExposure is always enabled for XeSS with native Dx11
            bool autoExposureDisabled = state.api == API::DX11 && currentBackend == Upscaler::XeSS;
            ImGui::BeginDisabled(autoExposureDisabled);

            if (bool autoExposure = currentFeature->AutoExposure(); ImGui::Checkbox("Auto Exposure", &autoExposure))
            {
                config->AutoExposure = autoExposure;
                ReInitUpscaler();
            }
            ShowResetButton(&config->AutoExposure, "R");
            ShowHelpMarker("일부 Unreal Engine 게임에서 필요합니다"
                           ""
                           "색상이 깜빡이거나\n객체에 고스팅 잔상이 남는다면 사용해 보세요");

            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            auto accessToReactiveMask = currentFeature->AccessToReactiveMask();
            ImGui::BeginDisabled(!accessToReactiveMask);

            bool canUseReactiveMask =
                accessToReactiveMask && currentBackend != Upscaler::DLSS &&
                (currentBackend != Upscaler::XeSS || currentFeature->Version() >= feature_version { 2, 0, 1 });

            bool disableReactiveMask = config->DisableReactiveMask.value_or(!canUseReactiveMask);

            if (ImGui::Checkbox("Disable Reactive Mask", &disableReactiveMask))
            {
                config->DisableReactiveMask = disableReactiveMask;

                if (currentBackend == Upscaler::XeSS)
                {
                    state.newBackend = currentBackend;
                    MARK_ALL_BACKENDS_CHANGED();
                }
            }

            ImGui::EndDisabled();

            if (accessToReactiveMask)
                ShowHelpMarker("Reactive mask 사용을 허용합니다"
                               "DLSS에 전달되는 Reactive mask는"
                               "FSR/XeSS와 조합하면 좋은 이미지를 만들지 못한다는 점에 유의하세요");
            else
                ShowHelpMarker("게임이 Reactive mask를 제공하지 않아 옵션이 비활성화되었습니다");

            ImGui::EndTable();

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Advanced Init Flags"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (ImGui::BeginTable("init2", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextColumn();
                    if (bool depth = currentFeature->DepthInverted(); ImGui::Checkbox("Depth Inverted", &depth))
                    {
                        config->DepthInverted = depth;
                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->DepthInverted, "R##2");
                    ShowHelpMarker("변경할 필요는 없을 것입니다");

                    ImGui::TableNextColumn();
                    if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox("HDR", &hdr))
                    {
                        config->HDR = hdr;
                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->HDR, "R##1");
                    ShowHelpMarker("일부 게임의 보라색 색조 문제에 도움이 될 수 있습니다");

                    ImGui::TableNextColumn();
                    if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox("Display Res. MV", &mv))
                    {
                        config->DisplayResolution = mv;

                        // Disable output scaling when
                        // Display res MV is active
                        if (mv)
                        {
                            config->OutputScalingEnabled = false;
                            _ssEnabled = false;
                        }

                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->DisplayResolution, "R##4");
                    ShowHelpMarker("주로 Unreal Engine 게임용 수정 사항입니다"
                                   "화면 왼쪽 위 부분이 흐릿해집니다");

                    ImGui::TableNextColumn();

                    if (bool jitter = currentFeature->JitteredMV(); ImGui::Checkbox("Jitter Cancellation", &jitter))
                    {
                        config->JitterCancellation = jitter;
                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->JitterCancellation, "R##3");
                    ShowHelpMarker("지터가 미리 적용된 모션 데이터를 보내는 게임용 수정 사항입니다");

                    ImGui::TableNextColumn();
                    ImGui::EndTable();
                }

                if (currentFeature->AccessToReactiveMask() && currentBackend != Upscaler::DLSS)
                {
                    ImGui::BeginDisabled(config->DisableReactiveMask.value_or(currentBackend == Upscaler::XeSS));

                    bool binaryMask = state.api == Vulkan || currentBackend == Upscaler::XeSS;
                    auto defaultBias = binaryMask ? 0.0f : 0.45f;
                    auto maskBias = config->DlssReactiveMaskBias.value_or(defaultBias);

                    if (!binaryMask)
                    {
                        if (ImGui::SliderFloat("React. Mask Bias", &maskBias, 0.0f, 0.9f, "%.2f"))
                            config->DlssReactiveMaskBias = maskBias;

                        ShowHelpMarker("0보다 큰 값은 Reactive mask 사용을 활성화합니다");
                    }
                    else
                    {
                        bool useRM = maskBias > 0.0f;
                        if (ImGui::Checkbox("Use Binary Reactive Mask", &useRM))
                        {
                            if (useRM)
                                config->DlssReactiveMaskBias = 0.45f;
                            else
                                config->DlssReactiveMaskBias.reset();
                        }
                    }

                    ImGui::EndDisabled();
                }
            }
        }
    }
}

void MenuCommon::RenderMagnifierSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Magnifier -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Magnifier"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool magnifierEnabled = config->MagnifierEnabled.value_or_default();
        if (ImGui::Checkbox("Enable Magnifier", &magnifierEnabled))
            config->MagnifierEnabled = magnifierEnabled;

        ImGui::BeginDisabled(!magnifierEnabled);

        float magnifierSize = config->MagnifierSize.value_or_default();
        if (ImGui::SliderFloat("Size", &magnifierSize, 5.0f, 50.0f, "%.1f%% of screen"))
            config->MagnifierSize = magnifierSize;

        int zoomFactor = config->MagnifierZoomFactor.value_or_default();
        if (ImGui::SliderInt("Zoom Factor", &zoomFactor, 2, 20, "%dx"))
            config->MagnifierZoomFactor = zoomFactor;

        float borderSize = config->MagnifierBorderSize.value_or_default();
        if (ImGui::SliderFloat("Border Size", &borderSize, 0.0f, 2.0f, "%.2f%% of screen"))
            config->MagnifierBorderSize = borderSize;

        ImGui::Separator();
        ImGui::Text("Positioning");

        bool staticMode = config->MagnifierStaticPosX.has_value() && config->MagnifierStaticPosY.has_value();
        if (staticMode)
        {
            float staticX = config->MagnifierStaticPosX.value();
            if (ImGui::SliderFloat("Static Pos X", &staticX, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosX = staticX;

            float staticY = config->MagnifierStaticPosY.value();
            if (ImGui::SliderFloat("Static Pos Y", &staticY, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosY = staticY;

            if (ImGui::Button("Reset Static Position (Follow Cursor)"))
            {
                config->MagnifierStaticPosX.reset();
                config->MagnifierStaticPosY.reset();
            }
        }
        else
        {
            // Button to initialize static position mode
            if (ImGui::Button("Set Static Position"))
            {
                config->MagnifierStaticPosX = 50.0f;
                config->MagnifierStaticPosY = 50.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(현재 커서를 따라갑니다)");

            float offsetX = config->MagnifierCursorOffsetX.value_or_default();
            if (ImGui::SliderFloat("Cursor Offset X", &offsetX, -300.0f, 300.0f, "%.0f px"))
                config->MagnifierCursorOffsetX = offsetX;

            float offsetY = config->MagnifierCursorOffsetY.value_or_default();
            if (ImGui::SliderFloat("Cursor Offset Y", &offsetY, -300.0f, 300.0f, "%.0f px"))
                config->MagnifierCursorOffsetY = offsetY;
        }

        ImGui::EndDisabled();
        ImGui::Spacing();
    }
}
void MenuCommon::RenderQuirksSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;

    // QUIRKS -----------------------------
    if (state.detectedQuirks.size() > 0)
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Active Quirks"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            for (const auto& quirk : state.detectedQuirks)
            {
                ImGui::TextWrapped("%s", quirk.c_str());
            }
        }
    }
}

void MenuCommon::RenderAdvancedSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    // ADVANCED SETTINGS -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Advanced Settings"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            bool extendedLimits = config->ExtendedLimits.value_or_default();
            if (ImGui::Checkbox("Enable Extended Limits", &extendedLimits))
                config->ExtendedLimits = extendedLimits;

            ShowHelpMarker("품질 프리셋의 슬라이더 한계를 확장합니다"
                           ""
                           "이 옵션을 사용하면 해상도 감지 로직이 변경되며\n문제나 크래시가 발생할 수 있습니다!");
        }

        bool pcShaders = config->UsePrecompiledShaders.value_or_default();
        if (ImGui::Checkbox("Use Precompiled Shaders", &pcShaders))
        {
            config->UsePrecompiledShaders = pcShaders;
            state.newBackend = currentBackend;
            MARK_ALL_BACKENDS_CHANGED();
        }

        // DRS
        ImGui::SeparatorText("DRS (Dynamic Resolution Scaling)");
        if (ImGui::BeginTable("drs", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();
            if (bool drsMin = config->DrsMinOverrideEnabled.value_or_default();
                ImGui::Checkbox("Override Minimum", &drsMin))
                config->DrsMinOverrideEnabled = drsMin;
            ShowHelpMarker("공식 DRS 한계를 무시하는 게임용 수정 사항입니다");

            ImGui::TableNextColumn();
            if (bool drsMax = config->DrsMaxOverrideEnabled.value_or_default();
                ImGui::Checkbox("Override Maximum", &drsMax))
                config->DrsMaxOverrideEnabled = drsMax;
            ShowHelpMarker("공식 DRS 한계를 무시하는 게임용 수정 사항입니다");

            ImGui::EndTable();
        }

        // Non-DLSS hotfixes -----------------------------
        if (currentFeature != nullptr && !currentFeature->IsFrozen() && currentBackend != Upscaler::DLSS)
        {
            // BARRIERS -----------------------------
            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Resource Barriers"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                AddResourceBarrier("Color", &config->ColorResourceBarrier);
                AddResourceBarrier("Depth", &config->DepthResourceBarrier);
                AddResourceBarrier("Motion", &config->MVResourceBarrier);
                AddResourceBarrier("Exposure", &config->ExposureResourceBarrier);
                AddResourceBarrier("Mask", &config->MaskResourceBarrier);
                AddResourceBarrier("Output", &config->OutputResourceBarrier);
            }

            // HOTFIXES -----------------------------
            if (state.api == DX12)
            {
                ImGui::Spacing();
                if (auto ch = ScopedCollapsingHeader("Root Signatures"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (bool crs = config->RestoreComputeSignature.value_or_default();
                        ImGui::Checkbox("Restore Compute Root Signature", &crs))
                        config->RestoreComputeSignature = crs;

                    if (bool grs = config->RestoreGraphicSignature.value_or_default();
                        ImGui::Checkbox("Restore Graphic Root Signature", &grs))
                        config->RestoreGraphicSignature = grs;
                }
            }
        }
    }
}

void MenuCommon::RenderLoggingSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // LOGGING -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Logging"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        if (config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
            config->LogToNGX.value_or_default())
            spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
        else
            spdlog::default_logger()->set_level(spdlog::level::off);

        if (bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox("To File", &toFile))
        {
            config->LogToFile = toFile;
            PrepareLogger();
        }

        ImGui::SameLine(0.0f, 6.0f);
        if (bool toConsole = config->LogToConsole.value_or_default(); ImGui::Checkbox("To Console", &toConsole))
        {
            config->LogToConsole = toConsole;
            PrepareLogger();
        }

        const char* logLevels[] = { "Trace", "Debug", "Information", "Warning", "Error" };
        const char* selectedLevel = logLevels[config->LogLevel.value_or_default()];

        if (ImGui::BeginCombo("Log Level", selectedLevel))
        {
            for (int n = 0; n < 5; n++)
            {
                if (ImGui::Selectable(logLevels[n], (config->LogLevel.value_or_default() == n)))
                {
                    config->LogLevel = n;
                    spdlog::default_logger()->set_level(
                        (spdlog::level::level_enum) config->LogLevel.value_or_default());
                }
            }

            ImGui::EndCombo();
        }
    }
}

void MenuCommon::RenderThemeSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // THEME -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Menu Theme and Color"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool lightTheme = config->LightTheme.value_or_default();

        const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
        const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

        auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
        { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

        auto AccentSoft = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.24f, alpha) : Mix(bgDark, accent, 0.32f, alpha)); };

        auto AccentMed = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha)); };

        auto AccentStrong = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(ImVec4(accent.x, accent.y, accent.z, alpha)); };

        if (ImGui::Checkbox("Light Theme", &lightTheme))
        {
            config->LightTheme = lightTheme;
            ApplyThemeStyle();
        }

        ImGui::SeparatorText("Accent Colour");

        ImGui::Text("Presets:");
        ImGui::SameLine(0.0f, 6.0f);

        ImVec4 colorBlue = { 0.00f, 0.40f, 0.77f, 1.0f };
        ImVec4 colorTeal = { 0.00f, 1.00f, 0.91f, 1.0f };
        ImVec4 colorGray = { 0.54f, 0.54f, 0.54f, 1.0f };
        ImVec4 colorYellow = { 1.00f, 0.89f, 0.00f, 1.0f };
        ImVec4 colorGreen = { 0.25f, 1.00f, 0.00f, 1.0f };
        ImVec4 colorRed = { 1.00f, 0.00f, 0.00f, 1.0f };
        ImVec4 colorOrange = { 1.00f, 0.52f, 0.00f, 1.0f };
        ImVec4 colorPurple = { 0.576f, 0.00f, 1.00f, 1.0f };

        ImVec4 color = {};

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Blue"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Teal"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Gray"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Yellow"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Green"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Red"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Orange"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Purple"))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float accentColor[3] = { config->MenuAccentColorR.value_or_default(),
                                 config->MenuAccentColorG.value_or_default(),
                                 config->MenuAccentColorB.value_or_default() };

        if (ImGui::ColorEdit3("Custom Accent Color", accentColor))
        {
            config->MenuAccentColorR = accentColor[0];
            config->MenuAccentColorG = accentColor[1];
            config->MenuAccentColorB = accentColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        if (ImGui::Button("Reset Accent Color"))
        {
            config->MenuAccentColorR.reset();
            config->MenuAccentColorG.reset();
            config->MenuAccentColorB.reset();
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        ImGui::SeparatorText("Background Colour");

        ImGui::Text("Presets:");
        ImGui::SameLine(0.0f, 6.0f);

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Blue##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Teal##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Gray##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Yellow##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Green##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Red##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Orange##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (ImGui::Button("Purple##2"))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float bgColor[3] = { config->MenuBGColorR.value_or_default(), config->MenuBGColorG.value_or_default(),
                             config->MenuBGColorB.value_or_default() };

        if (ImGui::ColorEdit3("Custom BG Colour", bgColor))
        {
            config->MenuBGColorR = bgColor[0];
            config->MenuBGColorG = bgColor[1];
            config->MenuBGColorB = bgColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        auto alpha = config->MenuBGColorA.value_or_default();
        if (ImGui::SliderFloat("Background Alpha", &alpha, 0.0f, 1.0f))
        {
            config->MenuBGColorA = alpha;
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        if (ImGui::Button("Reset BG Colour"))
        {
            config->MenuBGColorR.reset();
            config->MenuBGColorG.reset();
            config->MenuBGColorB.reset();
            config->MenuBGColorA.reset();
            ApplyThemeStyle();
        }

        ImGui::Spacing();
    }
}

void MenuCommon::RenderFpsOverlaySettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // FPS OVERLAY -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("FPS Overlay"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool fpsEnabled = config->ShowFps.value_or_default();
        if (ImGui::Checkbox("FPS Overlay Enabled", &fpsEnabled))
            config->ShowFps = fpsEnabled;

        ImGui::SameLine(0.0f, 6.0f);

        bool fpsHorizontal = config->FpsOverlayHorizontal.value_or_default();
        if (ImGui::Checkbox("Horizontal", &fpsHorizontal))
            config->FpsOverlayHorizontal = fpsHorizontal;

        const char* fpsPosition[] = { "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
        const char* selectedPosition = fpsPosition[config->FpsOverlayPosition.value_or_default()];

        if (ImGui::BeginCombo("Overlay Position", selectedPosition))
        {
            for (int n = 0; n < std::size(fpsPosition); n++)
            {
                if (ImGui::Selectable(fpsPosition[n], (config->FpsOverlayPosition.value_or_default() == n)))
                    config->FpsOverlayPosition = (FpsOverlayPos) n;
            }

            ImGui::EndCombo();
        }

        const char* fpsType[] = { "Just FPS", "Simple",       "Detailed",      "Detailed + Graph",
                                  "Full",     "Full + Graph", "Reflex timings" };
        const char* selectedType = fpsType[config->FpsOverlayType.value_or_default()];

        if (ImGui::BeginCombo("Overlay Type", selectedType))
        {
            for (int n = 0; n < std::size(fpsType); n++)
            {
                if (ImGui::Selectable(fpsType[n], (config->FpsOverlayType.value_or_default() == n)))
                    config->FpsOverlayType = (FpsOverlay) n;
            }

            ImGui::EndCombo();
        }

        float fpsAlpha = config->FpsOverlayAlpha.value_or_default();
        if (ImGui::SliderFloat("Background Alpha", &fpsAlpha, 0.0f, 1.0f, "%.2f"))
            config->FpsOverlayAlpha = fpsAlpha;

        const char* options[] = { "Same as menu", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                  "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
        int currentIndex = std::max(((int) (config->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
        float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                           1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

        if (ImGui::SliderInt("Scale", &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
                             ImGuiSliderFlags_ClampOnInput))
        {
            if (currentIndex == 0)
                config->FpsScale.reset();
            else
                config->FpsScale = values[currentIndex];
        }

        bool useTheme = config->OverlaysUseTheme.value_or_default();
        if (ImGui::Checkbox("Use Theme Colors", &useTheme))
            config->OverlaysUseTheme = useTheme;
    }
}

void MenuCommon::RenderUpscalerInputsSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    // UPSCALER INPUTS -----------------------------
    ImGui::Spacing();
    auto uiStateOpen = currentFeature == nullptr || currentFeature->IsFrozen();
    if (auto ch = ScopedCollapsingHeader("Upscaler Inputs", uiStateOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        if (config->EnableFsr2Inputs.value_or_default())
        {
            bool fsr2Inputs = config->UseFsr2Inputs.value_or_default();
            bool fsr2Pattern = config->Fsr2Pattern.value_or_default();

            if (ImGui::Checkbox("Use Fsr2 Inputs", &fsr2Inputs))
                config->UseFsr2Inputs = fsr2Inputs;

            if (ImGui::Checkbox("Use Fsr2 Pattern Matching", &fsr2Pattern))
                config->Fsr2Pattern = fsr2Pattern;
            ShowTooltip("This setting will become active on next boot!");
        }

        if (config->EnableFsr3Inputs.value_or_default())
        {
            bool fsr3Inputs = config->UseFsr3Inputs.value_or_default();
            bool fsr3Pattern = config->Fsr3Pattern.value_or_default();

            if (ImGui::Checkbox("Use Fsr3 Inputs", &fsr3Inputs))
                config->UseFsr3Inputs = fsr3Inputs;

            if (ImGui::Checkbox("Use Fsr3 Pattern Matching", &fsr3Pattern))
                config->Fsr3Pattern = fsr3Pattern;
            ShowTooltip("This setting will become active on next boot!");
        }

        if (config->EnableFfxInputs.value_or_default())
        {
            bool ffxInputs = config->UseFfxInputs.value_or_default();

            if (ImGui::Checkbox("Use Ffx Inputs", &ffxInputs))
                config->UseFfxInputs = ffxInputs;
        }
    }
}

void MenuCommon::RenderApiAndTextureSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // DX11 & DX12 -----------------------------
    if (state.swapchainApi != Vulkan)
    {
        // V-SYNC -----------------------------
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("V-Sync Settings"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            auto forceVsyncOn = config->ForceVsync.has_value() && config->ForceVsync.value();
            auto forceVsyncOff = config->ForceVsync.has_value() && !config->ForceVsync.value();
            bool vsyncChanged = false;

            if (ImGui::Checkbox("V-Sync On", &forceVsyncOn))
            {
                if (forceVsyncOn)
                {
                    config->ForceVsync = true;
                    vsyncChanged = true;
                }
                else
                {
                    config->ForceVsync.reset();
                    vsyncChanged = true;
                }
            }
            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Checkbox("V-Sync Off", &forceVsyncOff))
            {
                if (forceVsyncOff)
                {
                    config->ForceVsync = false;
                    vsyncChanged = true;
                }
                else
                {
                    config->ForceVsync.reset();
                    vsyncChanged = true;
                }
            }
            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!forceVsyncOn);

            ImGui::PushItemWidth(50.0f * menuResScale);

            auto vsyncBuf = StrFmt("%d", config->VsyncInterval.value_or_default());
            if (ImGui::BeginCombo("Sync Int.", vsyncBuf.c_str()))
            {
                if (ImGui::Selectable("0", config->VsyncInterval.value_or_default() == 0))
                {
                    config->VsyncInterval = 0;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("1", config->VsyncInterval.value_or_default() == 1))
                {
                    config->VsyncInterval = 1;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("2", config->VsyncInterval.value_or_default() == 2))
                {
                    config->VsyncInterval = 2;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("3", config->VsyncInterval.value_or_default() == 3))
                {
                    config->VsyncInterval = 3;
                    vsyncChanged = true;
                }

                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ShowHelpMarker("DXGI Present 동기화 간격을 제어합니다. 이 값은"
                           "스왑 체인이 수직 리프레시를 기다리는 방식을 결정합니다."
                           ""
                           "0  = 즉시 Present, VSync 대기 없음."
                           "1  = 모든 리프레시에 동기화, 일반 VSync."
                           "2 이상 = N번의 리프레시마다 Present, 유효 프레임 레이트 감소."
                           "\n값이 높으면 테어링은 줄지만 지연 시간이 늘어나고 FPS가 제한될 수 있습니다.\n대부분의 게임에서는 최저 지연 시간을 위해 0을, 일반 VSync를 위해 1을 사용하세요.");

            ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Reset##10"))
            {
                config->ForceVsync.reset();
                vsyncChanged = true;
            }

            ShowHelpMarker("Force V-Sync On/Off와 Sync Interval 옵션을 초기화합니다");

            if (vsyncChanged && state.activeFgOutput == FGOutput::XeFG && state.currentFG != nullptr)
            {
                // To prevent XeLL issues
                LOG_DEBUG("V-Sync change detected, forcing XeFG reset");
                state.WAR_xefgRequestFGToggle = true;
            }
        }

        // MIPMAP BIAS & Anisotropy -----------------------------
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Mipmap Bias", (currentFeature == nullptr || currentFeature->IsFrozen())
                                                                ? ImGuiTreeNodeFlags_DefaultOpen
                                                                : 0);
            ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();
            if (config->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
                _mipBias = config->MipmapBiasOverride.value();

            ImGui::SliderFloat("Mipmap Bias##2", &_mipBias, -15.0f, 15.0f, "%.6f");
            ShowHelpMarker("고장 난 게임의 흐릿한 텍스처에 도움이 될 수 있습니다"
                           "음수 값은 텍스처를 더 선명하게 만듭니다"
                           "양수 값은 텍스처를 더 흐릿하게 만듭니다"
                           "\n성능에는 미미한 영향만 줍니다");

            ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
            {
                ImGui::BeginDisabled(config->MipmapBiasScaleOverride.has_value() &&
                                     config->MipmapBiasScaleOverride.value());
                {
                    bool mbFixed = config->MipmapBiasFixedOverride.value_or_default();
                    if (ImGui::Checkbox("MB Fixed Override", &mbFixed))
                    {
                        config->MipmapBiasScaleOverride.reset();
                        config->MipmapBiasFixedOverride = mbFixed;
                    }

                    ShowHelpMarker("모든 텍스처에 동일한 오버라이드 값을 적용합니다");
                }
                ImGui::EndDisabled();

                ImGui::SameLine(0.0f, 6.0f);

                ImGui::BeginDisabled(config->MipmapBiasFixedOverride.has_value() &&
                                     config->MipmapBiasFixedOverride.value());
                {
                    bool mbScale = config->MipmapBiasScaleOverride.value_or_default();
                    if (ImGui::Checkbox("MB Scale Override", &mbScale))
                    {
                        config->MipmapBiasFixedOverride.reset();
                        config->MipmapBiasScaleOverride = mbScale;
                    }

                    ShowHelpMarker("오버라이드 값을 배율 승수로 적용합니다"
                                   "스케일 모드를 사용할 때는 선명도를 높이려면"
                                   "양수 오버라이드 값을 사용하세요!");
                }
                ImGui::EndDisabled();

                bool mbAll = config->MipmapBiasOverrideAll.value_or_default();
                if (ImGui::Checkbox("MB Override All Textures", &mbAll))
                    config->MipmapBiasOverrideAll = mbAll;

                ShowHelpMarker("모든 텍스처의 밉맵 값을 덮어씁니다"
                               "평소에는 OptiScaler가 0 미만의"
                               "밉맵 값만 덮어씁니다!");
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(config->MipmapBiasOverride.has_value() &&
                                 config->MipmapBiasOverride.value() == _mipBias);
            {
                if (ImGui::Button("Set"))
                {
                    config->MipmapBiasOverride = _mipBias;
                    state.lastMipBias = 100.0f;
                    state.lastMipBiasMax = -100.0f;
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 6.0f);

            ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
            {
                if (ImGui::Button("Reset"))
                {
                    config->MipmapBiasOverride.reset();
                    _mipBias = 0.0f;
                    state.lastMipBias = 100.0f;
                    state.lastMipBiasMax = -100.0f;
                }
            }
            ImGui::EndDisabled();

            if (currentFeature != nullptr && !currentFeature->IsFrozen())
            {
                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Calculate Mipmap Bias"))
                    _showMipmapCalcWindow = true;
            }

            if (config->MipmapBiasOverride.has_value())
            {
                if (config->MipmapBiasFixedOverride.value_or_default())
                {
                    ImGui::Text("Current : %.3f / %.3f, Target: %.3f", state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
                else if (config->MipmapBiasScaleOverride.value_or_default())
                {
                    ImGui::Text("Current : %.3f / %.3f, Target: Base * %.3f", state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
                else
                {
                    ImGui::Text("Current : %.3f / %.3f, Target: Base + %.3f", state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
            }
            else
            {
                ImGui::Text("Current : %.3f / %.3f", state.lastMipBias, state.lastMipBiasMax);
            }

            ImGui::Text("Will be applied after RESOLUTION/PRESET change !!!");
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(
                "Anisotropic Filtering",
                (currentFeature == nullptr || currentFeature->IsFrozen()) ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();
            ImGui::PushItemWidth(65.0f * menuResScale);

            auto selectedAF =
                config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : "Auto";
            if (ImGui::BeginCombo("Force Anisotropic Filtering", selectedAF.c_str()))
            {
                if (ImGui::Selectable("Auto", !config->AnisotropyOverride.has_value()))
                    config->AnisotropyOverride.reset();

                if (ImGui::Selectable("1", config->AnisotropyOverride.value_or(0) == 1))
                    config->AnisotropyOverride = 1;

                if (ImGui::Selectable("2", config->AnisotropyOverride.value_or(0) == 2))
                    config->AnisotropyOverride = 2;

                if (ImGui::Selectable("4", config->AnisotropyOverride.value_or(0) == 4))
                    config->AnisotropyOverride = 4;

                if (ImGui::Selectable("8", config->AnisotropyOverride.value_or(0) == 8))
                    config->AnisotropyOverride = 8;

                if (ImGui::Selectable("16", config->AnisotropyOverride.value_or(0) == 16))
                    config->AnisotropyOverride = 16;

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            bool afComp = config->AnisotropyModifyComp.value_or_default();
            if (ImGui::Checkbox("Modify Compare", &afComp))
                config->AnisotropyModifyComp = afComp;

            ShowHelpMarker("비교 필터를 업데이트합니다");

            ImGui::SameLine(0.0f, 6.0f);

            bool afMinMax = config->AnisotropyModifyMinMax.value_or_default();
            if (ImGui::Checkbox("Modify Min/Max", &afMinMax))
                config->AnisotropyModifyMinMax = afMinMax;

            ShowHelpMarker("min/max 필터를 업데이트합니다");

            bool afSkipPoint = config->AnisotropySkipPointFilter.value_or_default();
            if (ImGui::Checkbox("Skip Point Filters", &afSkipPoint))
                config->AnisotropySkipPointFilter = afSkipPoint;

            ShowHelpMarker("포인트 필터 업데이트를 건너뜁니다");

            ImGui::Text("Will might be applied after RESOLUTION/PRESET change !!!");
        }
    }
}

void MenuCommon::RenderKeybindSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Keybinds"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        ImGui::Text("Key combinations are currently NOT supported!");
        ImGui::Text("Escape: 취소, Backspace 또는 해제 버튼: 단축키 없음");
        ImGui::Spacing();

        static auto menu = Keybind("Menu", 10);
        static auto fpsOverlay = Keybind("FPS Overlay", 11);
        static auto fpsOverlayCycle = Keybind("FPS Overlay Cycle", 12);
        static auto fgEnable = Keybind("Frame Generation", 13);
        static auto dlssNrToggle = Keybind("Neural Rendering", 14);

        menu.Render(config->ShortcutKey);
        fpsOverlay.Render(config->FpsShortcutKey);
        fpsOverlayCycle.Render(config->FpsCycleShortcutKey);
        fgEnable.Render(config->FGShortcutKey);
        dlssNrToggle.Render(config->DlssNrToggleKey);
    }
}

void MenuCommon::RenderMainMenuTable(RenderMenuContext& ctx)
{
    if (ImGui::BeginTable("main", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        // Left column: active upscaler state, frame generation, FSR common, latency and fakenvapi controls.
        RenderActiveUpscalerSettings(ctx);
        RenderFrameGenerationSelection(ctx);
        RenderFrameGenerationRuntimeSettings(ctx);
        RenderFsrCommonSettings(ctx);
        RenderFramerateSettings(ctx);
#ifdef LOW_LATENCY_INPUTS
        RenderLowLatencySettings(ctx);
#else
        RenderFakenvapiSettings(ctx);
#endif

        ImGui::TableNextColumn();

        // Right column: image quality, initialization, advanced options, appearance, overlay and input settings.
        RenderActiveImageSettings(ctx);
        DlssNr::RenderMenu(ctx.config, ctx.menuResScale);
        RenderMagnifierSettings(ctx);
        RenderQuirksSettings(ctx);
        RenderAdvancedSettings(ctx);
        RenderLoggingSettings(ctx);
        RenderThemeSettings(ctx);
        RenderFpsOverlaySettings(ctx);
        RenderUpscalerInputsSettings(ctx);
        RenderApiAndTextureSettings(ctx);
        RenderKeybindSettings(ctx);

        ImGui::EndTable();
    }
}

void MenuCommon::RenderMainMenuGraphs(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("plots", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        ImGui::Text("FrameTime");
        auto ft = StrFmt("%7.2f ms / %6.1f fps", frameTime, frameRate);
        ImGui::PlotLines(
            ft.c_str(), [](void* rb, int idx) -> float
            { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gFrameTimes, plotWidth);

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            ImGui::TableNextColumn();
            ImGui::Text("Upscaler");

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !state.detailedGpuTimes.empty())
            {
                ImGui::BeginTooltip();

                ImGui::TextDisabled("셰이더별 내역:");
                if (ImGui::BeginTable("ShaderTimes", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    bool hasExtra = false;

                    for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
                    {
                        if (!includedInUpscalerTime)
                        {
                            hasExtra = true;
                            continue;
                        }

                        auto formattedTime = StrFmt("%7.2f ms", time);

                        ImGui::TableNextColumn();
                        ImGui::Text(name.c_str());

                        ImGui::TableNextColumn();
                        ImGui::Text(formattedTime.c_str());
                    }

                    std::optional<double> nrTime {};
                    nrTime = DlssNr::LastGpuTime();
                    if (hasExtra || nrTime.has_value())
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("Extra shaders:");
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("");
                        for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
                        {
                            if (includedInUpscalerTime)
                                continue;

                            auto formattedTime = StrFmt("%7.2f ms", time);

                            ImGui::TableNextColumn();
                            ImGui::Text(name.c_str());

                            ImGui::TableNextColumn();
                            ImGui::Text(formattedTime.c_str());
                        }

                        if (nrTime.has_value())
                        {
                            ImGui::TableNextColumn();
                            ImGui::Text("Neural Rendering");
                            ImGui::TableNextColumn();
                            ImGui::Text(StrFmt("%.2f ms", nrTime.value()).c_str());
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::EndTooltip();
            }

            auto ups = StrFmt("%7.2f ms", state.upscaleTimes.back());
            ImGui::PlotLines(
                ups.c_str(), [](void* rb, int idx) -> float
                { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gUpscalerTimes, plotWidth);
        }

        ImGui::EndTable();
    }
}

void MenuCommon::RenderMainMenuBottomBar(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // BOTTOM LINE ---------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        ImGui::Text("%dx%d -> %dx%d (%.1f) [%dx%d (%.1f)]", currentFeature->RenderWidth(),
                    currentFeature->RenderHeight(), currentFeature->TargetWidth(), currentFeature->TargetHeight(),
                    (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth(),
                    currentFeature->DisplayWidth(), currentFeature->DisplayHeight(),
                    (float) currentFeature->DisplayWidth() / (float) currentFeature->RenderWidth());

        ImGui::SameLine(0.0f, 4.0f);

        ImGui::Text("%d", currentFeature->FrameCount());

        ImGui::SameLine(0.0f, 10.0f);
    }

    ImGui::PushItemWidth(100.0f * menuResScale);

    auto autoText = config->MenuScale.has_value() ? "Auto" : StrFmt("Auto (%3.1f)", menuResScale);
    // clang-format off
    const char* uiScales[] = { autoText.c_str(), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1",
                               "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
    // clang-format on

    const char* selectedScaleName = uiScales[_selectedScale];

    if (ImGui::BeginCombo("Menu Scale", selectedScaleName))
    {
        for (int n = 0; n < std::size(uiScales); n++)
        {
            if (ImGui::Selectable(uiScales[n], (_selectedScale == n)))
            {
                _selectedScale = n;

                if (n == 0)
                    config->MenuScale.reset();
                else
                    config->MenuScale = 0.4f + (float) n / 10.0f;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 15.0f);

    if (ImGui::Button("Save Settings"))
        config->SaveIni();

    ImGui::SameLine(0.0f, 6.0f);

    if (ImGui::Button("Close"))
    {
        _isVisible = false;
        hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
        io.BackendFlags &= 30;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

        _showMipmapCalcWindow = false;
        _showHudlessWindow = false;
        io.MouseDrawCursor = false;
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
    }

    auto winSize = ImGui::GetWindowSize();
    auto winPos = ImGui::GetWindowPos();

    ImGui::SameLine();

    auto textSize = ImGui::CalcTextSize("Open Wiki (?)");
    auto& style = ImGui::GetStyle();
    textSize.x += style.FramePadding.x * 2.0f;
    textSize.x += style.ItemSpacing.x;

    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

    // Make button text underline
    if (ImGui::Button("Open Wiki"))
    {
        auto pIO = &ImGui::GetPlatformIO();
        auto ctx = ImGui::GetCurrentContext();
        pIO->Platform_OpenInShellFn(ctx, "https://github.com/optiscaler/OptiScaler/wiki");
    }
    ShowHelpMarker("클릭하면 기본 브라우저에서"
                   "OptiScaler Wiki 페이지를 엽니다"
                   "\n알려진 게임 문제와 우회 방법을 정리한 호환성 목록,\nFG 옵션 설명 및 기타 유용한 정보가 있습니다");

    ImGui::Spacing();
    ImGui::Separator();

    if (state.nvngxIniDetected)
    {
        ImGui::Spacing();
        ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                           "nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config");
        ImGui::Spacing();
    }

    if (lastPosition.x < -900.0f || (lastPosition.x >= winPos.x - 1.0f && lastPosition.y >= winPos.y - 1.0f &&
                                     lastPosition.x <= winPos.x + 1.0f && lastPosition.y <= winPos.y + 1.0f))
    {
        float posX;
        float posY;

        posX = ((float) io.DisplaySize.x - winSize.x) / 2.0f;
        posY = ((float) io.DisplaySize.y - winSize.y) / 2.0f;

        // don't position menu outside of screen
        if (posX < 0.0 || posY < 0.0)
        {
            posX = 50;
            posY = 50;
        }

        ImGui::SetWindowPos(ImVec2 { posX, posY });
        lastPosition.x = posX;
        lastPosition.y = posY;
    }
}

void MenuCommon::RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;

    // Metrics window (for debug)
    // ImGui::ShowMetricsWindow();

    // Mipmap calculation window
    if (_showMipmapCalcWindow && currentFeature != nullptr && !currentFeature->IsFrozen() && currentFeature->IsInited())
    {
        auto posX = (io.DisplaySize.x - 450.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 200.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 450.0f, 200.0f }, ImGuiCond_FirstUseEver);

        if (_displayWidth == 0)
        {
            if (config->OutputScalingEnabled.value_or_default())
            {
                _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                      config->OutputScalingMultiplier.value_or_default());
            }
            else
            {
                _displayWidth = currentFeature->DisplayWidth();
            }

            _renderWidth = static_cast<uint32_t>(_displayWidth / 3.0f);
            _mipmapUpscalerQuality = 0;
            _mipmapUpscalerRatio = 3.0f;
            _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
        }

        if (ImGui::Begin("Mipmap Bias", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            if (ImGui::InputScalar("Display Width", ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
            {
                if (_displayWidth <= 0)
                {
                    if (config->OutputScalingEnabled.value_or_default())
                    {
                        _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                              config->OutputScalingMultiplier.value_or_default());
                    }
                    else
                    {
                        _displayWidth = currentFeature->DisplayWidth();
                    }
                }

                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            const char* q[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Ultra Quality", "DLAA" };
            float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
            auto configQ = _mipmapUpscalerQuality;

            const char* selectedQ = q[configQ];

            ImGui::BeginDisabled(config->UpscaleRatioOverrideEnabled.value_or_default());

            if (ImGui::BeginCombo("Upscaler Quality", selectedQ))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                    {
                        _mipmapUpscalerQuality = n;

                        float ov = -1.0f;

                        if (config->QualityRatioOverrideEnabled.value_or_default())
                        {
                            switch (n)
                            {
                            case 0:
                                ov = config->QualityRatio_UltraPerformance.value_or(-1.0f);
                                break;

                            case 1:
                                ov = config->QualityRatio_Performance.value_or(-1.0f);
                                break;

                            case 2:
                                ov = config->QualityRatio_Balanced.value_or(-1.0f);
                                break;

                            case 3:
                                ov = config->QualityRatio_Quality.value_or(-1.0f);
                                break;

                            case 4:
                                ov = config->QualityRatio_UltraQuality.value_or(-1.0f);
                                break;
                            }
                        }

                        if (ov > 0.0f)
                            _mipmapUpscalerRatio = ov;
                        else
                            _mipmapUpscalerRatio = fr[n];

                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();

            auto minLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
            auto maxLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
            if (ImGui::SliderFloat("Upscaler Ratio", &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
            {
                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            if (ImGui::InputScalar("Render Width", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

            ImGui::SliderFloat("Mipmap Bias", &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

            // BOTTOM LINE
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SameLine();
            ImGui::Spacing();

            constexpr float spacing = 6.0f;
            auto textSize = ImGui::CalcTextSize("Use Value");
            textSize += ImGui::CalcTextSize("Close");
            textSize.x += ImGui::GetStyle().FramePadding.x * 5.0f + spacing; // 2 sides * 2 buttons + 1

            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

            if (ImGui::Button("Use Value"))
            {
                _mipBias = _mipBiasCalculated;
                _showMipmapCalcWindow = false;
            }

            ImGui::SameLine(0.0f, spacing);

            if (ImGui::Button("Close"))
                _showMipmapCalcWindow = false;

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::End();
        }
    }
}

void MenuCommon::RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    auto fg = state.currentFG;
    if (_showHudlessWindow && config->FGHUDFix.value_or_default() && fg != nullptr && fg->IsActive())
    {
        auto posX = (io.DisplaySize.x - 400.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 300.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 400.0f, 300.0f });

        if (ImGui::Begin("HUDless Resources", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            int btnCount = 100;

            if (ImGui::BeginTable("HUDlessTable", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                for (it = state.capturedHudlesses.begin(); it != state.capturedHudlesses.end(); it++)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::Text("%08x, %s->%s, Count: %llu, %s", (size_t) it->first,
                                GetSourceString(it->second.captureInfo & 0xFF).c_str(),
                                GetDispatchString(it->second.captureInfo & 0xFF00).c_str(), it->second.usageCount,
                                it->second.enabled ? "Active" : "Passive");

                    ImGui::TableSetColumnIndex(1);

                    btnCount++;
                    std::string text;

                    if (it->second.enabled)
                        text = StrFmt("Disable##%d", btnCount);
                    else
                        text = StrFmt("Enable##%d", btnCount);

                    if (ImGui::Button(text.c_str()))
                    {
                        LOG_DEBUG("HUDless {:X}: {}", (size_t) it->first,
                                  it->second.enabled ? "Disabling" : "Enabling");
                        it->second.enabled = !it->second.enabled;
                    }
                }

                ImGui::EndTable();
            }

            if (ImGui::Button("Clear##4"))
            {
                LOG_DEBUG("Clearing captured HUDless resources");
                state.clearCapturedHudlesses = true;
            }

            ImGui::SameLine(0.0f, 8.0f);

            if (ImGui::Button("Close##4"))
                _showHudlessWindow = false;

            ImGui::End();
        }
    }
}

void MenuCommon::RenderMainMenuWindow(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;

    if (!_isVisible)
        return;

    // Check for GPU support once and reuse the result in all menu sections.
    // DXVK might call Vulkan device creation, which would destroy our objects.
    State::Instance().vulkanSkipHooks = true;
    ctx.primaryGpu =
        std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>(IdentifyGpu::getPrimaryGpu());
    State::Instance().vulkanSkipHooks = false;

    // Overlay font
    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(menuResScale * fontSize));

    // If overlay is not visible frame needs to be inited
    if (!frameTimesCalculated)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
    }

    ImGuiWindowFlags flags = 0;
    flags |= ImGuiWindowFlags_NoSavedSettings;
    flags |= ImGuiWindowFlags_NoCollapse;
    flags |= ImGuiWindowFlags_AlwaysAutoResize;

    if (lastMenuScale != menuResScale)
    {
        lastMenuScale = menuResScale;

        // if UI scale is changed rescale the style
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiStyle styleold = style; // Backup colors
        style = ImGuiStyle();        // IMPORTANT: ScaleAllSizes will change the original size,
                                     // so we should reset all style config

        ApplyThemeStyle();

        style.ScaleAllSizes(menuResScale);
        style.MouseCursorScale = 1.0f;
        CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors

        ImGui::SetNextWindowSize({ 1.0f, 1.0f });
    }

    // Main menu window
    if (windowTitle.empty())
    {
        windowTitle = StrFmt("%s - %s %s %s %s", VER_PRODUCT_NAME, state.gameExe.c_str(),
                             state.gameName.empty() ? "" : StrFmt("- %s", state.gameName.c_str()).c_str(),
                             (state.detectedQuirks.size() > 0) ? "(Q)" : "", state.isOptiPatcherSucceed ? "(OP)" : "");
    }

    if (ImGui::Begin(windowTitle.c_str(), NULL, flags))
    {
        // Header/status messages shown above the two-column settings table.
        RenderMainMenuHeaderMessages(ctx);

        // Main two-column settings content.
        RenderMainMenuTable(ctx);

        // Diagnostics and footer actions below the settings table.
        RenderMainMenuGraphs(ctx);
        RenderMainMenuBottomBar(ctx);

        ImGui::End();
    }

    // Detached utility windows owned by the main menu.
    RenderMipmapBiasWindow(ctx, flags);
    RenderHudlessResourcesWindow(ctx, flags);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void KeyUp(UINT vKey)
{
    inputMenu = vKey == Config::Instance()->ShortcutKey.value_or_default();
    inputFps = vKey == Config::Instance()->FpsShortcutKey.value_or_default();
    inputFG = vKey == Config::Instance()->FGShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

// The lamp, and only the lamp.
//
// Red for dark, green for full light, with its reading beside it. No status sentence: the whole
// point of a light meter is that it is read at a glance while playing, and a paragraph in the corner
// of somebody's game is not that. Everything wordy lives in the menu, which is where someone has
// already decided to stop and read.
//
// Drawn only when its own setting is on. An overlay that appears because a scan happens to be
// running is an overlay nobody asked for.
void RenderExposureScanIndicator(float alpha)
{
    using DlssNr::ExposureScan::Verdict;

    if (!Config::Instance()->DlssNrScanMeter.value_or_default())
        return;

    if (DlssNr::ExposureScan::Where() == Verdict::Off)
        return;

    int which = 0;
    float low = 0.0f, high = 0.0f;
    const float now = DlssNr::ExposureScan::BestValue(&which, &low, &high);

    // Nothing found yet, or no range to place it in: a dim lamp, which says "watching, no reading"
    // without saying it in words.
    const bool reading = now > 0.0f && high > low;

    float lit = 0.0f;

    if (reading)
    {
        // An exposure falls as the scene brightens, so the value reads backwards unless the buffer
        // holds the reciprocal -- the same question the anchor asks, answered from the same setting,
        // because a lamp contradicting the picture would be worse than no lamp.
        lit = (high - now) / (high - low);

        if (Config::Instance()->DlssNrScanInverted.value_or_default())
            lit = 1.0f - lit;

        lit = lit < 0.0f ? 0.0f : (lit > 1.0f ? 1.0f : lit);
    }

    // Red to amber to green. A straight red-to-green fade passes through a muddy brown at the
    // midpoint, and the midpoint is where most of a session is spent.
    const ImVec4 dark(0.90f, 0.22f, 0.20f, 1.0f);
    const ImVec4 mid(0.95f, 0.75f, 0.20f, 1.0f);
    const ImVec4 bright(0.35f, 0.88f, 0.38f, 1.0f);
    const ImVec4 idle(0.45f, 0.45f, 0.45f, 1.0f);

    ImVec4 lamp = idle;

    if (reading)
    {
        const float t = lit < 0.5f ? lit * 2.0f : (lit - 0.5f) * 2.0f;
        const ImVec4& a = lit < 0.5f ? dark : mid;
        const ImVec4& b = lit < 0.5f ? mid : bright;
        lamp = ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 12.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(alpha);

    if (ImGui::Begin("DlssNrExposureScan", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove))
    {
        const float r = ImGui::GetFontSize() * 0.38f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 centre(at.x + r, at.y + ImGui::GetTextLineHeight() * 0.5f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(centre, r, ImGui::GetColorU32(lamp), 20);
        draw->AddCircle(centre, r, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f)), 20, 1.5f);

        ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine();

        if (reading)
            ImGui::TextColored(lamp, "%3.0f%%  %.5f", lit * 100.0f, now);
        else
            ImGui::TextColored(idle, "--");
    }

    ImGui::End();
}

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    ctx.now = Util::MillisecondsNow();
    ctx.currentFeature = ctx.state.currentFeature;

    // 1) Collect timing and input state before any ImGui drawing.
    UpdateRenderTiming(ctx);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);

    // 2) Prepare one-shot notifications and start a new ImGui frame only when needed.
    UpdateVersionAndStartupNotifications(ctx);
    BeginMenuFrameIfNeeded(ctx);
    OptiInput::EndFrame(_isVisible);

    // 3) Draw lightweight overlay windows first, preserving the original order.
    ctx.menuResScale = MenuResolutionScale(ctx.io);
    RenderSplashWindow(ctx);
    RenderNotifications(ctx);
    UpdateFrameTimeAverages(ctx);
    RenderPerformanceOverlay(ctx);
    RenderExposureScanIndicator(ctx.config->FpsOverlayAlpha.value_or_default());

    // 4) Draw the full settings menu last so popups and child windows keep their existing behavior.
    RenderMainMenuWindow(ctx);

    if (ctx.newFrame)
        ImGui::EndFrame();

    return ctx.newFrame;
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    // Reset shutdown flag in case of re-init
    State::Instance().isShuttingDown = false;

    HWND oldHandle = nullptr;

    if (_handle != nullptr)
    {
        oldHandle = _handle;
        LOG_DEBUG("Old Handle: {:X}, ImGui Handle: {:X}", (size_t) oldHandle,
                  (size_t) ImGui::GetMainViewport()->PlatformHandleRaw);
    }

    _handle = InHwnd;
    _isVisible = false;
    _isUWP = isUWP;
    lastPosition = { -1000.0f, -1000.0f };

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // In case d3d12 wasn't yet used up to this point, try to update GPU info late here
    IdentifyGpu::updateD3d12Capabilities();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->FontSize.has_value())
            fontSize = Config::Instance()->FontSize.value();

        // Korean glyph ranges (Latin + Hangul Jamo + Hangul Syllables + Fullwidth)
        static const ImWchar koRanges[] = { 0x0020, 0x00FF, 0x3131, 0x3163, 0xAC00, 0xD7A3, 0xFF00, 0xFFEF, 0 };
        std::string ttfPath = Config::Instance()->TTFFontPath.has_value()
                                  ? wstring_to_string(Config::Instance()->TTFFontPath.value())
                                  : "C:\\Windows\\Fonts\\malgun.ttf";
        ImFont* koFont = atlas->AddFontFromFileTTF(ttfPath.c_str(), fontSize, &fontConfig, koRanges);
        if (koFont != nullptr)
        {
            io.FontDefault = koFont;
        }
        else
        {
            io.FontDefault = atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,
                                                                         fontSize, &fontConfig);
        }
    }

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _hdrTonemapApplied = false;
    }

    DWORD hwndPid = 0;
    DWORD hwndTid = GetWindowThreadProcessId(_handle, &hwndPid);

    LOG_DEBUG("HWND: {:X}, IsWindow: {}, HWND PID: {}, Current PID: {}, HWND TID: {}, Current TID: {}",
              (ULONG64) _handle, IsWindow(_handle), hwndPid, GetCurrentProcessId(), hwndTid, GetCurrentThreadId());

    OptiInput::Initialize(_handle, isUWP);

    ApplyThemeStyle();
    _isInited = true;
}

void MenuCommon::Shutdown()
{
    if (!MenuCommon::_isInited)
        return;

    // if (_oWndProc != nullptr)
    //{
    //     auto handle = (HWND) ImGui::GetMainViewport()->PlatformHandleRaw;
    //     SetLastError(0);
    //     auto restoreResult = SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
    //     auto error = GetLastError();

    //    if (restoreResult == 0 && error != 0)
    //    {
    //        LOG_ERROR("Failed to restore old WndProc. Error: {:X}", error);
    //    }

    //    _oWndProc = nullptr;
    //}

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    ImGui::DestroyContext();

    _handle = nullptr;
    _isInited = false;
    _isVisible = false;
}

void MenuCommon::HideMenu()
{
    if (!_isVisible)
        return;

    _isVisible = false;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    _showMipmapCalcWindow = false;
    _showHudlessWindow = false;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}
