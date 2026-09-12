#include "pch.h"
#include "DlssNrFeature_Vk.h"

#include "DlssNr.h"
#include "DlssNr_ExposureScan.h"


#include <Config.h>
#include <menu/menu_common.h>

#include <imgui/imgui.h>

#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace DlssNr
{

// Aurora CN: convert wide string literals to UTF-8 for Dear ImGui.
// Using wide literals keeps the Chinese UI independent of the compiler's narrow execution code page.
static std::string AuroraUtf8(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0')
        return {};

    return wstring_to_string(std::wstring(text));
}

#define AURORA_CN(x) AuroraUtf8(L##x).c_str()

// The "(?)" marker every control carries, matching the rest of the menu.
static void HelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A slider that only writes its value when the handle is released.
//
// Some controls -- intensity, the structure and tone strengths -- are read by the model once, when
// the feature is built, so changing one rebuilds the whole feature. Writing on every pixel of a drag
// meant a rebuild per frame, felt as the picture hitching while you scrub. The slider still tracks
// live under the cursor; only the commit that triggers the rebuild waits for release. Cheap controls
// that are just shader constants (detail, colour, paper white) do not use this -- they can afford to
// apply live.
static bool DeferredSlider(const char* label, CustomOptional<float>* opt, float mn, float mx,
                           const char* fmt = "%.2f")
{
    static std::unordered_map<std::string, float> pending;

    auto it = pending.find(label);
    float value = it != pending.end() ? it->second : opt->value_or_default();

    if (ImGui::SliderFloat(label, &value, mn, mx, fmt))
        pending[label] = value;

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        auto committed = pending.find(label);

        if (committed != pending.end())
        {
            *opt = std::clamp(committed->second, mn, mx);
            pending.erase(committed);
            return true;
        }
    }

    return false;
}

// One per-pass control: a checkbox that decides whether this pass has an opinion, and the slider it
// enables. Unchecked follows the global setting, which is what an untouched pass does.
static bool PassOverrideSlider(const char* label, std::optional<float>* own, float global, float mn,
                               float mx, int pass)
{
    bool changed = false;
    bool has = own->has_value();

    const std::string useId = std::string("##use") + label + std::to_string(pass);

    if (ImGui::Checkbox(useId.c_str(), &has))
    {
        if (has)
            *own = global;
        else
            own->reset();

        changed = true;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!has);

    float value = own->value_or(global);
    const std::string sliderId = std::string(label) + "##" + std::to_string(pass);

    if (ImGui::SliderFloat(sliderId.c_str(), &value, mn, mx, "%.2f") && has)
    {
        *own = value;
        changed = true;
    }

    ImGui::EndDisabled();

    if (!has)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(AURORA_CN("跟随全局"));
    }

    return changed;
}

void RenderMenu(Config* config, float menuResScale)
{

    // DLSS Neural Rendering -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader(AURORA_CN("DLSS 神经渲染")); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool enabled = config->DlssNrEnabled.value_or_default();
        if (ImGui::Checkbox(AURORA_CN("启用神经渲染"), &enabled))
            config->DlssNrEnabled = enabled;

        HelpMarker(AuroraUtf8(L"在帧生成处理之前，对超分输出进一步合成细节。\n\n需要在 OptiScaler 旁放置两个名称非常接近的文件：\n  nvngx_dlssnr.dll       NVIDIA 模型文件（约 165 MB，需要自行提供）\n  nvngx.dll_dlssnr.dll   转发器（约 13 KB，本包自带）\n\n该功能没有 NVIDIA 官方集成文档，目前属于直接调用的实验性实现，不受官方支持。").c_str());

        // The toggle can be bound to a key, and nobody would think to look for it under Keybinds
        // unless told. Dimmed, because it is a note rather than a setting.
        ImGui::TextDisabled(AURORA_CN("可绑定快捷键切换——请在“快捷键”中设置“神经渲染”。"));

        // Either backend. The two keep separate state, and on a native Vulkan game the D3D12 side
        // is never touched -- so asking only that one reports "waiting for the upscaler" over a pass
        // that is demonstrably running.
        const bool vulkan = DlssNr::IsRunningVk();

        // Turning the pass off does not release the model, so the feature handle stays alive and
        // IsRunning keeps answering yes. Reporting a cost from that was wrong in the way that matters
        // most: the toggle is how anyone A/Bs this, so the one moment the number is read is the one
        // moment it describes the frame before last.
        if (!enabled)
        {
            ImGui::TextDisabled(AURORA_CN("已关闭。模型仍保留在显存中，再次开启可立即生效。"));
        }
        else if (!DlssNr::IsRunning() && !vulkan)
        {
            const char* reason = DlssNr::FailureReason();

            if (reason[0] != 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), AURORA_CN("本次会话已停用：%s。"), reason);
                ImGui::SameLine();

                if (ImGui::SmallButton(AURORA_CN("重试")))
                    DlssNr::RetryAfterFailure();
            }
            // The model is D3D12 and Vulkan only. A native D3D11 upscaler creates no D3D12 device,
            // so nothing ever arrives and the wait below would never end.
            else if (auto feature = State::Instance().currentFeature;
                     feature != nullptr && feature->Api() == API::DX11 && !feature->IsWithDx12())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                   AURORA_CN("%s 当前以原生 D3D11 运行，神经渲染模型不支持此路径。"),
                                   feature->Name().c_str());
                ImGui::TextDisabled(AURORA_CN("请选择上方带 w/Dx12 标记的超分器，然后重启游戏。"));
            }
            else if (enabled)
                ImGui::TextUnformatted(AURORA_CN("正在等待超分器开始运行……"));
        }
        else
        {
            // The cost belongs here rather than only in the upscaler's breakdown: that tooltip needs
            // OptiScaler's own upscaler to have run, and with native DLSS passing through there is
            // nothing in it to hang this off.
            // Either backend's timer. They measure the same thing by different means, and only one
            // of them is running.
            const auto ms = vulkan ? DlssNr::LastGpuTimeVk() : DlssNr::LastGpuTime();

            if (ms.has_value())
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), AURORA_CN("运行中%s - 每帧 %.2f ms"),
                                   vulkan ? AURORA_CN("（Vulkan 原生）") : "", ms.value());
            else if (vulkan)
                // Measured but not yet read: the first few frames are still in the query ring.
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), AURORA_CN("Vulkan 原生运行中 - %llu 帧"),
                                   DlssNr::FramesVk());
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), AURORA_CN("运行中。"));

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", AuroraUtf8(L"这里统计的是整个神经渲染流程，包括中间拷贝、Resolve 和模型本身。只统计模型会低估实际开销。\n\n可与窗口底部的帧时间对比，判断该功能实际增加了多少 GPU 开销。").c_str());
        }

        ImGui::Spacing();
        ImGui::PushItemWidth(220.0f * menuResScale);

        ImGui::SeparatorText(AURORA_CN("性能开销"));

        {
            // Coloured by what it costs, because the number alone does not say. The model is 98% of
            // this pass's expense and every run pays it again, so the scale is linear and brutal:
            // four passes is four times the model, not four percent more.
            //
            // Green at 1, what the model was trained for. Amber at 2 and 3, where it is being asked
            // to enhance its own output. Red from 4, where it usually stops looking rendered.
            //
            // Applied when the handle is let go: every distinct value is a feature to build, and the
            // build is spaced so the driver's latches survive it.
            static int pendingPasses = -1;

            int passes = pendingPasses >= 0 ? pendingPasses
                                            : (int) config->DlssNrPasses.value_or_default();

            if (passes < 1)
                passes = 1;

            const ImVec4 colour =
                passes <= 1                                  ? ImVec4(0.35f, 0.88f, 0.38f, 1.0f)
                : passes <= 3                                ? ImVec4(0.95f, 0.70f, 0.20f, 1.0f)
                : passes <= (int) DlssNr::kDefaultMaxPasses  ? ImVec4(0.92f, 0.30f, 0.25f, 1.0f)
                                                             : ImVec4(1.00f, 0.25f, 0.85f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, colour);

            const bool unlocked = config->DlssNrUnlockPasses.value_or_default();
            const int passLimit = (int) (unlocked ? DlssNr::kMaxPasses : DlssNr::kDefaultMaxPasses);

            if (ImGui::SliderInt(AURORA_CN("模型遍数"), &passes, 1, passLimit,
                                 passes == 1 ? AURORA_CN("%d（原生）") : AURORA_CN("%dx 模型开销")))
                pendingPasses = passes;

            ImGui::PopStyleColor(2);

            if (ImGui::IsItemDeactivatedAfterEdit() && pendingPasses >= 0)
            {
                config->DlssNrPasses = (uint32_t) std::clamp(pendingPasses, 1, passLimit);
                pendingPasses = -1;
            }

            if (bool lift = unlocked; ImGui::Checkbox(AURORA_CN("解除遍数限制"), &lift))
            {
                config->DlssNrUnlockPasses = lift;

                // Dropping the ceiling under a larger count would leave the file asking for passes
                // the slider can no longer show.
                if (!lift && config->DlssNrPasses.value_or_default() > DlssNr::kDefaultMaxPasses)
                    config->DlssNrPasses = DlssNr::kDefaultMaxPasses;
            }

            const std::string liftTip =
                AuroraUtf8(L"将上方模型遍数上限提升到 ") + std::to_string(DlssNr::kMaxPasses) +
                AuroraUtf8(L"。这远高于该流程的常规设计范围，帧时间会近似随遍数线性增加，通常在到达上限前游戏就已不具备可玩性。\n\n模型本身占据了绝大部分开销，因此 10 遍就近似等于每帧运行 10 次完整模型。每一遍还会持有独立的 NGX Feature 和历史数据；这些 Feature 会逐个构建，中间需要等待稳定，所以设置很高的遍数时需要一些时间。\n\n随着遍数增加，还需要同步提高“高光保护”，否则额外遍数产生的亮度变化会被保护阈值截掉，白白消耗性能。");

            HelpMarker(liftTip.c_str());

            // The tooltip is not enough for a slider that now reaches thirty. Say the cost on screen,
            // and keep saying it while the count is past what the slider offers by default.
            if (unlocked)
            {
                const int live = (int) config->DlssNrPasses.value_or_default();

                if (live > (int) DlssNr::kDefaultMaxPasses)
                    ImGui::TextColored(ImVec4(1.00f, 0.25f, 0.85f, 1.0f),
                                       AURORA_CN("%d 遍：每帧约为 %dx 模型开销。"), live, live);
                else
                    ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f),
                                       AURORA_CN("已解除限制。继续增加一遍，就会多运行一次完整模型。"));
            }

            // Per-pass settings, one node each, only for the passes that are running.
            //
            // Written back as the sparse "2:intensity=0.5;3:style=1" the pass reads. A pass whose
            // controls all sit at the global value contributes nothing, so the string stays empty
            // until something is actually different and the default costs nothing to carry.
            const auto liveCount = (int) config->DlssNrPasses.value_or_default();

            if (liveCount > 1)
            {
                if (ImGui::TreeNode(AURORA_CN("分遍设置")))
                {
                    auto overrides = DlssNr::ParsePassOverridesForMenu(
                        config->DlssNrPassOverrides.value_or_default());

                    bool edited = false;

                    for (int pass = 0; pass < liveCount; ++pass)
                    {
                        const std::string label = AuroraUtf8(L"第 ") + std::to_string(pass + 1) + AuroraUtf8(L" 遍");

                        if (!ImGui::TreeNode(label.c_str()))
                            continue;

                        auto& own = overrides[pass];

                        edited |= PassOverrideSlider(AURORA_CN("强度"), &own.Intensity,
                                                     config->DlssNrIntensity.value_or_default(),
                                                     0.0f, 4.0f, pass);
                        edited |= PassOverrideSlider(AURORA_CN("细节强度"), &own.LocalStructure,
                                                     config->DlssNrLocalStructure.value_or_default(),
                                                     0.0f, 4.0f, pass);
                        edited |= PassOverrideSlider(AURORA_CN("局部色调"), &own.LocalTone,
                                                     config->DlssNrLocalTone.value_or_default(),
                                                     0.0f, 4.0f, pass);
                        edited |= PassOverrideSlider(AURORA_CN("皮肤结构"), &own.SkinStructure,
                                                     config->DlssNrSkinStructure.value_or_default(),
                                                     -1.0f, 4.0f, pass);

                        ImGui::TreePop();
                    }

                    if (edited)
                        config->DlssNrPassOverrides = DlssNr::SerializePassOverrides(overrides);

                    HelpMarker(AuroraUtf8(L"这里可以为每一遍模型单独覆盖参数。\n\n保持“跟随全局”时，该遍会继承上方全局设置，因此未单独修改的遍数行为与默认状态完全一致。\n\n多遍处理是串联的：后一遍看到的是前一遍已经处理过的结果。逐遍适当降低强度，可以让后面的模型更偏向精修，而不是反复放大同一效果。").c_str());

                    ImGui::TreePop();
                }
            }

            HelpMarker(AuroraUtf8(L"控制模型对同一帧连续处理多少遍；每一遍都会接收上一遍的输出。\n\n这是本页开销最大的选项，性能成本几乎严格线性：5 遍就是每帧运行约 5 次模型。\n\n它真正独有的作用，是让模型重新判断细节应该出现在哪里、采用什么色相与饱和度；“细节强度”只能放大第一遍已经生成的细节图，无法重新绘制。\n\n如果只是想让效果更明显，优先尝试“细节强度”“强度”和提高“模型分辨率”，这些方式通常比直接增加遍数划算。\n\n超过 3 遍后建议同步提高“高光保护”，否则多遍叠加的亮度比例会被保护阈值截断，额外计算可能被浪费。\n\n每一遍都有独立模型状态和显存历史，因此提高遍数时会需要数秒逐步构建，显存占用也会增加。\n\n原生 Vulkan 路径或启用代理路径时，此选项可能不生效。").c_str());
        }

        // Any percentage, rather than a handful of steps somebody chose in advance. The lower bound
        // is 25%: below that the model is working on so little of the picture that its answer no
        // longer survives being enlarged onto it.
        // Applied when the handle is let go, not while it is moving.
        //
        // Every distinct value here is a different working size, and a different working size tears
        // down the scratch textures and rebuilds the model. Writing it on each pixel of a drag meant
        // dozens of rebuilds in a second, which is felt as the whole frame hitching. The slider still
        // reads live; only the commit waits.
        static int pendingScale = -1;

        int scalePercent = pendingScale >= 0
                               ? pendingScale
                               : (int) lroundf(config->DlssNrWorkingScale.value_or_default() * 100.0f);

        if (ImGui::SliderInt(AURORA_CN("模型分辨率"), &scalePercent, 25, 200, "%d%%"))
            pendingScale = scalePercent;

        if (ImGui::IsItemDeactivatedAfterEdit() && pendingScale >= 0)
        {
            config->DlssNrWorkingScale = std::clamp(pendingScale, 25, 200) / 100.0f;
            pendingScale = -1;
        }

        HelpMarker(AuroraUtf8(L"模型内部处理分辨率相对于当前帧的比例，可在 25%～200% 之间调节。原始游戏帧始终保持完整分辨率，只有模型自身的输入/输出会重采样。\n\n低于 100% 时，模型在更小的图像上运行再放大回来。50% 表示横纵尺寸各减半，模型像素数约为 1/4，性能更好但细节会变软。\n\n高于 100% 时，模型会在更大的栅格上运行后再缩回帧尺寸。200% 表示横纵各 2 倍，即约 4 倍模型像素与更高显存开销；它不会增加游戏本身的几何采样。").c_str());

        {
            bool dual = config->DlssNrDualFeature.value_or_default();

            if (ImGui::Checkbox(AURORA_CN("在超分器内部运行"), &dual))
                config->DlssNrDualFeature = dual;

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), AURORA_CN("（实验性，需重启）"));

            HelpMarker(AuroraUtf8(L"把超分流程拆成两段，并将神经渲染模型插在中间：前半段先在渲染分辨率输出，模型在该分辨率上处理，之后再由输出放大器提升到显示分辨率。\n\n配合光线重建时，前半段基本只承担降噪；模型看到的是已经清理过的图像，而且在 Performance 模式下像素量约为最终画面的 1/4，因此具有明显的性能优势。\n\n与“在超分前运行”不同，这里的图像已经经过时间累积，模型无法获知的亚像素抖动已先被处理。\n\n最终放大由单独的输出放大器完成，而不是原超分器自身。勾选后需要在超分器下次重建时才生效，最稳妥的做法是保存后重启游戏（或切换一次画质档位）。\n\n当渲染分辨率已经等于显示分辨率时不会产生效果，例如 DLAA 模式下没有更小的中间帧可供处理。").c_str());

            if (dual)
            {
                // The same names the upscaler list uses, resolved through the same provider, so a
                // machine without DLSS is handed FSR here exactly as it is anywhere else.
                static const std::string enlargerSpatial = AuroraUtf8(L"空间放大（无需运动矢量）");
                static const char* enlargerNames[] = { enlargerSpatial.c_str(), "DLSS", "FSR 2.2", "FSR 3.1", "XeSS" };
                static const std::optional<Upscaler> enlargerValues[] = { std::nullopt, Upscaler::DLSS, Upscaler::FSR22,
                                                                          Upscaler::FFX, Upscaler::XeSS };

                const auto current = config->DlssNrDualEnlarger.value_for_config();

                int index = 0;
                for (int i = 1; i < IM_ARRAYSIZE(enlargerNames); ++i)
                {
                    if (current == enlargerValues[i])
                    {
                        index = i;
                        break;
                    }
                }

                if (ImGui::Combo(AURORA_CN("放大方式"), &index, enlargerNames, IM_ARRAYSIZE(enlargerNames)))
                {
                    if (enlargerValues[index].has_value())
                        config->DlssNrDualEnlarger = enlargerValues[index].value();
                    else
                        config->DlssNrDualEnlarger.reset();
                }

                HelpMarker(AuroraUtf8(L"选择模型处理完成后，使用什么方式把图像放大到目标分辨率。\n\n“空间放大”不需要运动矢量、深度或抖动信息，因此不会因为这些输入错误而出问题，但也因为没有时间信息，画面通常最柔。\n\nDLSS / FSR / XeSS 会利用游戏每帧提供的数据，通常更锐利。如果选择了当前机器无法运行的超分器，会按主超分器列表的规则自动回退到可用方案。\n\n该设置会在超分器下次重建时生效。").c_str());
            }

            bool preUpscale = config->DlssNrPreUpscale.value_or_default();

            if (dual)
                ImGui::BeginDisabled();

            if (ImGui::Checkbox(AURORA_CN("在超分前运行"), &preUpscale))
                config->DlssNrPreUpscale = preUpscale;

            if (dual)
                ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), AURORA_CN("（实验性）"));

            HelpMarker(AuroraUtf8(L"让模型处理“即将送入超分器的渲染帧”，而不是超分器已经输出的结果。模型直接在渲染分辨率运行，因此在 Performance 模式下，开销大约只有超分后运行的 1/4，而且看到的是原始渲染像素而不是重建像素。\n\n它与降低“模型分辨率”不同：这里模型是在一张真实的小尺寸帧上 1:1 运行，之后由超分器把模型处理结果和其他内容一起放大，因此不会因为模型内部再缩放而额外变软。\n\n这仍属于实验性路径。此阶段的颜色每帧带有不同的亚像素抖动，而模型并不知道该偏移，因此历史重投影可能出现错误。移动镜头时要重点观察细节闪烁、游动或不稳定。").c_str());
        }

        // Only meaningful below 100%: at the same rate the residual collapses to the model's own
        // picture and the two modes are identical, so the control says so by going grey.
        {
            const bool reduced = config->DlssNrWorkingScale.value_or_default() < 0.999f;

            if (!reduced)
                ImGui::BeginDisabled();

            static const std::string enlargeClassic = AuroraUtf8(L"经典");
            static const std::string enlargeResidual = AuroraUtf8(L"匹配残差");
            static const char* enlargeNames[] = { enlargeClassic.c_str(), enlargeResidual.c_str() };
            int enlarge = config->DlssNrTransfer.value_or_default() == 1 ? 1 : 0;

            if (ImGui::Combo(AURORA_CN("放大合成"), &enlarge, enlargeNames, IM_ARRAYSIZE(enlargeNames)))
                config->DlssNrTransfer = (uint32_t) enlarge;

            if (!reduced)
                ImGui::EndDisabled();

            HelpMarker(AuroraUtf8(L"当模型分辨率低于当前帧时，决定如何把模型结果重新合成回完整尺寸。\n\n“经典”会直接把模型的小尺寸结果与完整帧合成。两者之间既包含模型真正做出的修改，也包含缩小/放大带来的模糊差异，合成阶段无法区分它们；模型分辨率越低，这种误差越明显，50% 时尤其容易表现为色偏。\n\n“匹配残差”只提取并放大模型真正产生的差值，再叠加到完整尺寸的代理图上。这样参与比较的两张图都处于完整分辨率，从小尺寸栅格带回来的只有模型修改本身。\n\n在 100% 模型分辨率下两种方式完全等价，因此不会产生差异。该方案源自本分支中 hhkbble 的多遍处理工作。").c_str());
        }

        ImGui::SeparatorText(AURORA_CN("效果混合"));

        float transfer = config->DlssNrTransferStrength.value_or_default();
        if (ImGui::SliderFloat(AURORA_CN("细节强度"), &transfer, 0.0f, 2.0f, "%.2f"))
            config->DlssNrTransferStrength = transfer;

        HelpMarker(AuroraUtf8(L"控制最终画面向模型结果混合多少。模型输出不是简单“加”到原画面上，而是一张完整图像；系统会先按原始画面校正其亮度，再在两张完整图像之间插值。\n\n0 = 完全保留超分器原始输出；1 = 完全采用模型结果。\n\n高于 1 会沿同一方向继续外推，这并不是模型原生设计用途，适合临时观察模型到底改变了什么，确认后通常应调回较低值。如果只是想让神经渲染更明显，优先提高这里的“细节强度”；“强度”是模型内部参数，模型会自行决定如何响应。").c_str());

        float colour = config->DlssNrColourStrength.value_or_default();
        if (ImGui::SliderFloat(AURORA_CN("色彩强度"), &colour, 0.0f, 1.0f, "%.2f"))
            config->DlssNrColourStrength = colour;

        HelpMarker(AuroraUtf8(L"控制模型的“颜色变化”是否随亮度与细节一起进入最终画面。\n\n0 会完全保留游戏原本的色相，只让亮度/细节体现模型结果；1 则同时采用模型输出的颜色，并限制在 AP1 可表示范围内。\n\n该过程是在两张完整图像之间插值，而不是把颜色差值直接叠加到原图，因此本身不会无约束地推动色相，能避免早期实现中暖色物体被推成绿色之类的问题。").c_str());

        ImGui::SeparatorText(AURORA_CN("模型设置"));

        ImGui::TextUnformatted(AURORA_CN("这些参数会在模型构建时读取；修改后会在片刻后重新构建模型。"));

        static const std::string nrPresetDefault = AuroraUtf8(L"默认");
        static const std::string nrPreset1 = AuroraUtf8(L"预设 1");
        static const std::string nrPreset2 = AuroraUtf8(L"预设 2");
        static const std::string nrPreset3 = AuroraUtf8(L"预设 3");
        static const char* nrPresetNames[] = { nrPresetDefault.c_str(), nrPreset1.c_str(), nrPreset2.c_str(), nrPreset3.c_str() };
        int preset = (int) config->DlssNrPreset.value_or_default();
        if (ImGui::Combo(AURORA_CN("模型预设"), &preset, nrPresetNames, IM_ARRAYSIZE(nrPresetNames)))
            config->DlssNrPreset = (uint32_t) preset;

        HelpMarker(AuroraUtf8(L"“默认”表示让模型自行选择。\n\n这里的预设编号与 DLSS 超分或光线重建的预设不是同一套含义；相同数字在这里代表的是不同模型设置。").c_str());

        static const std::string nrStyleDefault = AuroraUtf8(L"默认（标准）");
        static const std::string nrStyleNatural = AuroraUtf8(L"自然");
        static const std::string nrStyleCinematic = AuroraUtf8(L"电影感");
        static const char* nrStyleNames[] = { nrStyleDefault.c_str(), nrStyleNatural.c_str(), nrStyleCinematic.c_str() };
        int style = (int) config->DlssNrStyle.value_or_default();

        if (style > 2)
            style = 2;

        if (ImGui::Combo(AURORA_CN("风格"), &style, nrStyleNames, IM_ARRAYSIZE(nrStyleNames)))
            config->DlssNrStyle = (uint32_t) style;

        HelpMarker(AuroraUtf8(L"模型自身提供的处理风格。\n\n默认（标准）：效果最强，会提升局部对比、压深光照，也更容易出现过饱和或明显的风格化；很多“模型改变了游戏原本观感”的现象都来自这一档。\n\n自然：保留同类细节增强，但处理更克制，肤色与整体明暗关系更接近游戏原始渲染。\n\n电影感：减少高光发亮和过度处理，画面更偏电影式质感。\n\n这些参数在模型构建时读取，修改后会触发重新构建。名称来自社区测试，NVIDIA 的二进制中并未提供官方名称。").c_str());

        DeferredSlider(AURORA_CN("强度"), &config->DlssNrIntensity, 0.0f, 2.0f);

        HelpMarker(AuroraUtf8(L"模型内部自身的强度参数。它与上方“细节强度”不同：这里会改变模型内部处理，而“细节强度”是在模型输出之后再缩放最终效果。").c_str());

        DeferredSlider(AURORA_CN("局部结构"), &config->DlssNrLocalStructure, 0.0f, 2.0f);

        DeferredSlider(AURORA_CN("局部色调"), &config->DlssNrLocalTone, 0.0f, 2.0f);


        DeferredSlider(AURORA_CN("皮肤结构"), &config->DlssNrSkinStructure, -1.0f, 2.0f);

        HelpMarker(AuroraUtf8(L"-1 表示跟随“局部结构”，也是模型自身的默认行为，并不等于强度为 0。设置为 0 或更高数值后，皮肤结构会独立于画面其他区域进行控制。").c_str());

        bool autoMask = config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox(AURORA_CN("自动皮肤遮罩"), &autoMask))
            config->DlssNrAutoMask = autoMask;

        HelpMarker(AuroraUtf8(L"允许模型自动识别皮肤区域，而不是对整张画面一视同仁地处理。").c_str());

        ImGui::SeparatorText(AURORA_CN("色彩与曝光"));

        ImGui::TextDisabled(AuroraUtf8(L"模型训练时使用的是已经完成色调映射、采用 sRGB 编码的最终画面；而超分器输出通常是线性且没有固定上限的 HDR 数据。下面这些设置决定如何把它映射成模型能够正确识别的范围。若游戏报告该帧已经完成色调映射，则会直接跳过这套转换。").c_str());

        {
        // Logarithmic, because the useful range is not linear. A quarter to 240: the low end because
        // a frame the game already tone mapped wants roughly 1, the high end because there is no
        // principled ceiling -- this is a divisor on an open-ended linear buffer, and how far up a
        // given game needs to go is a property of that game's exposure, not of anything we can bound.
        // One tester was still improving at 100. A linear slider over that span would spend nine
        // tenths of its travel on values nobody needs and never reach the ones they do.
        // One dropdown, because there is one answer.
        //
        // This was two checkboxes that could both be on, and every attempt to stop that was a patch
        // on a shape that should not have existed. Greying deadlocked -- each disabled the other, so
        // once both were set the only way out was a button the notice never mentioned. Clearing
        // worked but silently undid a setting somebody had made. Both were ways to stop an illegal
        // state being REACHED; a single choice cannot reach it, because there is only one value to
        // be in.
        //
        // Each option also says whether it can actually do anything in THIS game, in colour, so the
        // choice is made on what is available rather than on what sounds best.
        {
            const auto ex = DlssNr::GameExposureStatus();
            const bool vk = DlssNr::IsRunningVk();
            const bool haveExposure = vk ? DlssNr::ExposureOfferedVk() : ex.everOffered;

            const float anchorNow = DlssNr::ExposureScan::BestValue();
            const bool haveAnchor = !DlssNr::ExposureScan::Anchors().empty();

            static const std::string sourcePaperWhite = AuroraUtf8(L"仅使用纸白");
            static const std::string sourceGameExposure = AuroraUtf8(L"使用游戏自身曝光");
            static const std::string sourceScanBuffer = AuroraUtf8(L"使用扫描找到的缓冲区");
            static const char* sourceNames[] = { sourcePaperWhite.c_str(), sourceGameExposure.c_str(),
                                                 sourceScanBuffer.c_str() };

            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 2)
                source = 0;

            if (ImGui::Combo(AURORA_CN("白点来源"), &source, sourceNames, IM_ARRAYSIZE(sourceNames)))
            {
                config->DlssNrWhitePointSource = (uint32_t) source;

                // Nothing else to set. The scan asks the source whether it is wanted, so choosing
                // it here is the whole of switching it on -- there is no second flag to keep in
                // step, and so no way for the two to disagree.
            }

            HelpMarker(AuroraUtf8(L"选择用于把线性 HDR 画面归一化到模型输入范围的白点来源。\n\n仅使用纸白：只使用下方固定数值。适合曝光基本不变化的游戏；如果场景会从洞穴切到阳光下，一个常数通常无法同时适配。\n\n使用游戏自身曝光：直接读取游戏交给超分器的曝光纹理。因为它在本流程之前就已确定，不会被神经渲染反向影响，因此通常是最可靠的来源；但并非所有游戏都会提供。\n\n使用扫描找到的缓冲区：用于游戏内部计算了曝光、却没有把曝光传给超分器的情况。扫描只能根据缓冲区形态猜测候选项，因此需要先做一次标定并在不同明暗环境中确认其确实跟随曝光变化。").c_str());

            // Availability, in colour, for the option currently chosen.
            if (source == 1)
            {
                if (!vk && ex.seenFrames == 0)
                    ImGui::TextDisabled(AURORA_CN("正在等待有效画面……"));
                else if (!haveExposure)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       AURORA_CN("该游戏未提供曝光数据——当前使用纸白。可以改用扫描来源。"));
                else if (vk)
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       AURORA_CN("游戏提供了曝光数据，正在读取。"));
                else if (ex.exposure > 1e-6f)
                {
                    const float trim =
                        std::clamp(config->DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       AURORA_CN("游戏曝光 %.4f  ->  白点 %.2f%s"), ex.exposure,
                                       ex.preExposure / ex.exposure * trim,
                                       ex.offeredNow ? "" : AURORA_CN("  （本帧缺失，沿用上一值）"));
                }
                else
                    ImGui::TextDisabled(AURORA_CN("正在读取曝光……"));
            }
            else if (source == 2)
            {
                // "Nothing found" and "found several, none of them moving" are different states,
                // and this said the first for both. In GTA V the log carried eight candidates while
                // the panel claimed there were none, which reads as the scan being broken when what
                // it actually needs is for the light to change.
                if (anchorNow <= 0.0f)
                {
                    const unsigned int watching = (unsigned int) DlssNr::ExposureScan::Report().size();

                    if (watching == 0)
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                           AURORA_CN("未发现形态符合曝光数据的缓冲区。"));
                    else
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                           AURORA_CN("正在监测 %u 个候选项，目前都没有变化——请在明暗环境之间移动。"),
                                           watching);
                }
                else if (!haveAnchor)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       AURORA_CN("已找到候选项。先调节下方纸白直到画面正常，再点击“在此标定”。"));
                else
                {
                    const float w = DlssNr::ExposureScan::AnchoredWhitePoint(
                        anchorNow, config->DlssNrScanInverted.value_or_default(),
                        config->DlssNrScanTrim.value_or_default());
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       AURORA_CN("已标定：扫描值 %.5f  ->  白点 %.2f"), anchorNow, w);
                }
            }
            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   AURORA_CN("该游戏提供了曝光数据——上方对应选项可以直接使用它。"));
            }
        }






        // A measured suggestion for paper white used to sit here and has been withdrawn.
        //
        // It took the 90th percentile of per-tile peak luminance from the untouched frame, which is a
        // statement about scene content rather than about the buffer's scale. In Nioh 3, where the
        // right answer is about 240, it offered 8 -- because most tiles are shadow and the percentile
        // sits wherever most tiles are. The guard meant to catch that compared each tile against the
        // frame's own brightest, which is scale-free and therefore passes on a black screen: the same
        // relative-threshold mistake the white point meter was removed for, made a second time.
        //
        // A wrong number offered confidently is worse than no number, so nothing is offered. What
        // replaces it has to be a measurement of the game's own exposure rather than of its scenery:
        // the exposure texture where a game supplies one, and otherwise the ratio between the
        // scene-referred buffer and the finished frame, which is that exposure by definition.

        // Two controls, not one control with two meanings.
        //
        // These are different quantities. The manual path wants an absolute divisor on an open-ended
        // linear buffer -- Nioh 3 needs about 240 -- and the exposure path wants a multiplier on a
        // number the game already supplied, where 1 is correct and anything far from it says the read
        // is wrong rather than that somebody prefers it.
        //
        // They used to share one stored value, narrowed to 0.25..4 when the toggle was on. That kept
        // a ruinous value unreachable but left two worse problems: moving the slider in one mode
        // silently destroyed the number found in the other, and there was no way back to "just take
        // the game's answer" short of knowing that the number for it was 1. Separate values fix both.
        // Switching modes is now non-destructive in both directions.
        // The trim belongs to both automatic sources, since both end in "the game's number times a
        // little". Only the manual source gets the absolute slider.
        // One slider per source, each remembering its own number.
        //
        // A trim on the game's exposure and a trim on a buffer the scan found are trims on different
        // things, and a value found against one means nothing against the other. Sharing them meant
        // changing source silently carried a number across, so a picture that had been tuned came
        // back wrong for a reason nothing on screen explained.
        //
        // The scan before it is anchored is the exception, and it has to be: anchoring captures an
        // absolute white point, so there must be an absolute slider to set. Showing a trim there
        // asked people to "set paper white below" next to a control that was not paper white.
        const int wpSource = (int) config->DlssNrWhitePointSource.value_or_default();

        // Which anchor row the paper-white slider edits, or -1 for the live unanchored point. Menu-
        // local and not persisted; the anchor block below sets it when a row is clicked. Declared
        // here because both the slider (this block) and the table (below) read it in the same frame.
        static int selectedAnchor = -1;
        auto anchors = DlssNr::ExposureScan::Anchors();
        if (selectedAnchor >= (int) anchors.size())
            selectedAnchor = -1;

        if (wpSource == 2)
        {
            // The scanned source is calibrated by the anchor table below. The slider here is the
            // paper white: it edits the selected row's white point, or -- with nothing selected --
            // the live value the next Anchor press will capture.
            const bool editingRow = selectedAnchor >= 0 && selectedAnchor < (int) anchors.size();

            float pw = editingRow ? anchors[selectedAnchor].white
                                  : config->DlssNrWhitePointScale.value_or_default();

            const std::string lbl = editingRow
                                        ? AuroraUtf8(L"纸白（正在编辑标定点 ") + std::to_string(selectedAnchor + 1) + AuroraUtf8(L"）")
                                        : AuroraUtf8(L"纸白");

            if (ImGui::SliderFloat(lbl.c_str(), &pw, 0.25f, 2000.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
            {
                if (editingRow)
                {
                    DlssNr::ExposureScan::AnchorSetWhite(selectedAnchor, pw);
                    config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                }
                else
                    config->DlssNrWhitePointScale = pw;
            }

            HelpMarker(AuroraUtf8(L"如果下方选中了某个标定点，这里编辑该点的白点；如果没有选择，则这里就是下一次点击“在此标定”时要记录的实时白点。\n\n先把画面调到看起来正确，再进行标定。随后移动到光照差异很大的场景，再调一次并添加第二个标定点；两个点可以更准确地描述该缓冲区与真实白点之间的关系，并在中间范围内插值。点击下方某一行可重新编辑该点，再次点击可取消选择。").c_str());

            // A global multiplier on the interpolated result, kept for parity with the other
            // sources. The points themselves are the real control here, so this stays near 1.
            float trim = config->DlssNrScanTrim.value_or_default();

            if (ImGui::SliderFloat(AURORA_CN("扫描结果微调"), &trim, 0.25f, 4.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);

            ImGui::SameLine();

            if (ImGui::SmallButton(AURORA_CN("重置##scantrim")))
                config->DlssNrScanTrim = 1.0f;
        }
        else if (wpSource == 1)
        {
            const bool ofScan = false;

            float trim = ofScan ? config->DlssNrScanTrim.value_or_default()
                                : config->DlssNrWhitePointTrim.value_or_default();

            const std::string trimLabel =
                ofScan ? AuroraUtf8(L"扫描结果微调") : AuroraUtf8(L"游戏曝光微调");
            if (ImGui::SliderFloat(trimLabel.c_str(), &trim,
                                   0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
            {
                if (ofScan)
                    config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);
                else
                    config->DlssNrWhitePointTrim = std::clamp(trim, 0.25f, 4.0f);
            }

            ImGui::SameLine();

            // Deliberately always present rather than greyed at 1. The point of it is that the safe
            // value is one click away without having to know what the safe value is.
            if (ImGui::SmallButton(AURORA_CN("重置##wptrim")))
            {
                if (ofScan)
                    config->DlssNrScanTrim = 1.0f;
                else
                    config->DlssNrWhitePointTrim = 1.0f;
            }

            HelpMarker(AuroraUtf8(L"对游戏提供的曝光值再乘一个微调系数。1.00x 表示完全采用游戏给出的数值，通常也是正确基准。\n\n这不是用来“硬救画面”的补偿项。如果必须把数值调得远离 1 才正常，更可能说明当前读取的曝光并不适用于该游戏。大约 0.8～1.25 属于合理微调；如果需要接近 4，往往是上游数据本身有问题。\n\n手动纸白使用独立数值，切换来源后不会丢失之前设置。").c_str());
        }
        else
        {
            // Logarithmic, because the useful range is not linear. A quarter to 2000: the low end
            // because a frame the game already tone mapped wants roughly 1, the high end because
            // there is no principled ceiling -- this is a divisor on an open-ended linear buffer, and
            // how far up a given game needs to go is a property of that game's exposure rather than
            // of anything that can be bounded here. One tester was still improving at 100.
            float wpScale = config->DlssNrWhitePointScale.value_or_default();

            if (ImGui::SliderFloat(AURORA_CN("纸白"), &wpScale, 0.25f, 2000.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointScale = wpScale;

        HelpMarker(AuroraUtf8(L"模型看到画面之前，会先用这里的数值除以整帧；这就是手动模式下完整的白点定义。\n\n模型训练所用的成片里“白色”大致位于 1，而游戏的 DLSS 缓冲区往往是线性、开放上限的 HDR 数据，所以必须告诉模型哪一个数值才应当视为白色，这个值通常并不接近 1。\n\n数值过低时，大量像素会过早进入高光压缩区，模型看到近乎一片发白的输入，细节结果被缩掉，最后可能只留下色相变化，看起来像色偏；数值过高则会让模型看到过暗的输入，同样导致结果劣化。\n\n实际调节方法是逐步提高，直到画面不再继续改善；超过最佳点后不会保持不变，而会向另一个方向变差。\n\n当“细节强度”为 0 时，无论这里怎么设置，最终帧仍应与未启用该效果时逐像素一致。").c_str());
        }

        // Directly under the white point, because that is the number it moves and the number the
        // anchor captures. It used to sit under Inspect, a whole section away from the slider it
        // reads, which left "Anchor here" looking like a control for something else entirely.
        {
            // No checkbox here any more.
            //
            // The dropdown above says whether the scan is the white point's source, and that is
            // the only reason anybody using this would want it running. A second control could
            // only agree with the dropdown or contradict it, and both were on offer: it began as
            // a redundant question and became a way to switch off the thing the chosen source
            // depended on.
            //
            // The ini key survives as a developer override for the one case a user has no reason
            // to want -- running the scan in a game that supplies a REAL exposure, so the log can
            // compare the two. That is validation, and validation does not need a widget.
            //
            // Worth keeping written down, since the panel no longer says it: the scan matches
            // buffers by SHAPE, and shape is a weak filter. In GTA V -- a game that supplies a
            // real exposure, so the right answer sat visible beside it -- the best candidate was
            // a 1x1 R32_FLOAT that climbed in a straight line for seventeen minutes while the
            // true exposure held still. Their ratio moved 14x. That is an accumulator, not an
            // eye adaptation.

                // Only where it means something. The lamp reads the scan, so offering it beside a
                // white point that comes from the game's own exposure is offering a control that
                // cannot light up.
                bool meter = config->DlssNrScanMeter.value_or_default();

                if (config->DlssNrWhitePointSource.value_or_default() == 2 &&
                    ImGui::Checkbox(AURORA_CN("在屏幕上显示光照表"), &meter))
                    config->DlssNrScanMeter = meter;

                HelpMarker(AuroraUtf8(L"在屏幕角落显示一个光照指示灯：暗处偏红，明亮处偏绿，并显示当前读数。\n\n它用于快速确认扫描到的候选值是否真的在“跟随曝光变化”，而不是仅仅处于运行状态。走进阴影时读数应向暗端移动，走到明处应向亮端移动；如果方向相反，可使用上方“数值变化方向相反”。\n\n该功能只是显示读数，不会改变画面。").c_str());

            // Shown when the scan is actually running, whichever way it got switched on.
            if (DlssNr::ExposureScan::Scanning())
            {
                // Anchoring: one press, then it never needs touching again.
                //
                // The absolute white point cannot come out of a buffer whose units are unknown.
                // Every value AFTER the first can: only the ratio against the anchor is used, so
                // whatever the number means, it cancels. That is why this is a button and not a
                // measurement -- the one thing a person can supply that no amount of cleverness
                // can is "this looks right to me".
                int which = 0;
                float low = 0.0f, high = 0.0f;
                const float live = DlssNr::ExposureScan::BestValue(&which, &low, &high);

                const bool isSource = config->DlssNrWhitePointSource.value_or_default() == 2;

                // Anchor captures (currentScan, currentPaperWhite) and ADDS a row -- it does not
                // replace. One row is the old single-anchor ratio law; add a second in different
                // light and the white point is interpolated between the points, so it holds across
                // the whole range instead of only near one anchor. Greyed unless the scan is the
                // chosen source and it currently has a value to capture.
                ImGui::BeginDisabled(live <= 0.0f || !isSource);

                if (ImGui::Button(AURORA_CN("在此标定")))
                {
                    if (DlssNr::ExposureScan::AnchorAdd(
                            live, std::max(0.01f, config->DlssNrWhitePointScale.value_or_default())))
                    {
                        config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                        selectedAnchor = -1;
                    }
                }

                ImGui::EndDisabled();

                HelpMarker(AuroraUtf8(L"先调节上方纸白，直到当前画面看起来正确，然后点击这里记录标定点。\n\n第一次标定会建立一个基础比例。之后走到光照明显不同的场景，再次调好纸白并添加第二个标定点，就可以更准确地拟合该缓冲区实际的曝光曲线，而不仅仅在单一标定点附近正确。最多支持 8 个标定点。\n\n标定表按游戏保存并可共享；同一游戏中，一人校准出的数值原则上可供其他使用相同配置的人复用。").c_str());

                if (!isSource)
                    ImGui::TextDisabled(AURORA_CN("（扫描当前仅用于监测——上方白点来自其他来源）"));

                if (!anchors.empty())
                {
                    // The row nearest the live scan value (in log space) is the one driving the
                    // picture right now; mark it so the user can see which calibration is in effect.
                    int active = 0;
                    float bestDist = 1e30f;
                    const float liveLog = std::log(std::max(live, 1e-6f));

                    for (size_t i = 0; i < anchors.size(); ++i)
                    {
                        const float d =
                            std::fabs(std::log(std::max(anchors[i].scan, 1e-6f)) - liveLog);
                        if (d < bestDist)
                        {
                            bestDist = d;
                            active = (int) i;
                        }
                    }

                    for (size_t i = 0; i < anchors.size(); ++i)
                    {
                        ImGui::PushID((int) i);

                        // Delete first, so its click is never swallowed by the row-wide Selectable.
                        if (ImGui::SmallButton("x"))
                        {
                            DlssNr::ExposureScan::AnchorRemove((int) i);
                            config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                            if (selectedAnchor == (int) i)
                                selectedAnchor = -1;
                            else if (selectedAnchor > (int) i)
                                --selectedAnchor;
                            ImGui::PopID();
                            continue;
                        }

                        ImGui::SameLine();

                        const bool sel = (int) i == selectedAnchor;
                        char row[160];
                        const std::string rowFormat = AuroraUtf8(L"%s 扫描 %.4f  ->  白点 %.2f%s");
                        const std::string editingText = sel ? AuroraUtf8(L"   [编辑中]") : std::string();
                        snprintf(row, sizeof(row), rowFormat.c_str(),
                                 ((int) i == active && isSource) ? ">" : "  ", anchors[i].scan,
                                 anchors[i].white, editingText.c_str());

                        // Click selects the row (slider edits it); click again deselects (slider
                        // returns to the live unanchored point).
                        if (ImGui::Selectable(row, sel))
                            selectedAnchor = sel ? -1 : (int) i;

                        ImGui::PopID();
                    }

                    ImGui::TextDisabled(AURORA_CN("点击某一行可用上方滑块编辑该标定点；再次点击即可取消选择并控制实时点。> 表示当前正在使用的标定点。"));
                }

                // The direction flag only means anything with a single point; with two or more the
                // direction the white point moves is already fixed by the data.
                if (anchors.size() == 1)
                {
                    bool inverted = config->DlssNrScanInverted.value_or_default();
                    if (ImGui::Checkbox(AURORA_CN("数值变化方向相反"), &inverted))
                        config->DlssNrScanInverted = inverted;

                    HelpMarker(AuroraUtf8(L"如果环境越亮，画面反而朝错误方向变化，请勾选此项。多数引擎存储的曝光值会随场景变亮而下降，也有引擎存储其倒数；仅凭缓冲区形态无法判断方向。添加第二个不同光照下的标定点后，系统可以由数据自动确定方向，因此该选项会不再需要。").c_str());
                }

                if (isSource && live > 0.0f && !anchors.empty())
                {
                    const float w = DlssNr::ExposureScan::AnchoredWhitePoint(
                        live, config->DlssNrScanInverted.value_or_default(),
                        config->DlssNrScanTrim.value_or_default());

                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       AURORA_CN("扫描 %.5f  ->  白点 %.2f   （%u 个标定点）"), live, w,
                                       (unsigned) anchors.size());
                }

                // Everything below is read-out rather than control: what the scan is looking at and
                // how to tell whether it found the right thing. Folded away because the two decisions
                // that matter -- anchor, and which way the number runs -- are above it.
                if (ImGui::TreeNode(AURORA_CN("高级信息")))
                {

                    const auto found = DlssNr::ExposureScan::Report();
                    const char* why = DlssNr::ExposureScan::Status();

                    if (found.empty())
                    {
                        ImGui::TextDisabled("%s", why != nullptr && why[0] != 0
                                                      ? why
                                                      : AURORA_CN("暂未匹配到候选项。"));
                    }
                    else
                    {
                        for (size_t i = 0; i < found.size(); ++i)
                        {
                            const auto& c = found[i];

                            if (c.reads == 0)
                            {
                                ImGui::TextDisabled(AURORA_CN("%zu. %s -- 尚未读取"), i + 1, c.shape.c_str());
                                continue;
                            }

                            // Moving is the whole signal, so it is the thing that is coloured.
                            ImGui::TextColored(c.moves ? ImVec4(0.45f, 0.8f, 0.45f, 1.0f)
                                                       : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                               AURORA_CN("%zu. %s = %.5f  （范围 %.5f..%.5f） %s"), i + 1,
                                               c.shape.c_str(), c.latest, c.lowest, c.highest,
                                               c.moves ? AURORA_CN("变化中") : AURORA_CN("暂时不变"));
                        }

                        ImGui::TextDisabled(AURORA_CN("从阴影走到明亮区域；真正的曝光值应该会变化。"));
                        ImGui::TextDisabled(AURORA_CN("如果数值只会单向持续上升，那通常是计数器，而不是曝光。"));
                    }

                    ImGui::TreePop();
                }
            }
        }

        // Reaches as far as Passes does. The guard is applied once to the finished composition while
        // the passes compound the ratio it bounds, so a count the slider above can reach needs a guard
        // that can follow it.
        float maxRatio = config->DlssNrMaxRatio.value_or_default();
        if (ImGui::SliderFloat(AURORA_CN("高光保护"), &maxRatio, 1.0f, (float) DlssNr::kMaxPasses, "%.1fx"))
            config->DlssNrMaxRatio = maxRatio;

        HelpMarker(AuroraUtf8(L"限制神经渲染最终最多可以把任意像素的亮度改变多少倍，并同时限制变亮和变暗两个方向。也就是说，像素既不能被提亮超过该倍率，也不能被压暗超过其倒数。\n\n高亮区域通常是模型最容易出问题、合成误差最明显的地方。较早版本曾把场景中的灯带破坏成彩色块；约 2x 的限制通常能保留细节，同时避免这种极端错误。只有当高光明显被压扁/截断时才建议提高。\n\n保护限制只在最终合成后应用一次，而多遍模型会逐次叠加亮度比例。因此遍数越高，通常也要适当提高该值；例如 1 遍约 1x、2 遍约 2x、3 遍约 3x，可让每一遍拥有近似相同的变化余量。\n\n该保护同时约束变暗路径，避免模型在极暗场景中反复把某些颜色通道压得过低。").c_str());

        }

        ImGui::SeparatorText(AURORA_CN("对比与检查"));

        // The depth and motion diagnostics used to sit here and are now ini-only:
        // ConstantDepth, FreezeDepth, FreezeMotion and MvScaleAbuse.
        //
        // They answered their question and the answer is in the notes: motion vectors are read
        // strongly -- 32x on the scale visibly degrades the picture -- and depth is read weakly.
        // What is left is four controls that can only make a game look worse, in a panel people
        // open to make it look better, next to the sliders they actually came for.
        //
        // Nothing is deleted. Anyone repeating the measurement sets the key and gets the same
        // instrument, and the reason for keeping the code is that the depth reading was taken
        // while the exaggeration slider was still at 32x and deserves a clean re-run.


        const auto hold = DlssNr::GetInspectionHoldState();
        const bool holding = hold == DlssNr::InspectionHoldState::Held;
        const bool holdPending = hold == DlssNr::InspectionHoldState::Pending;
        const bool capturing = DlssNr::CaptureInProgress();
        ImGui::BeginDisabled(!holding && !holdPending &&
                             (vulkan || capturing || hold == DlssNr::InspectionHoldState::Unavailable));
        const std::string holdButton =
            holding ? AuroraUtf8(L"恢复") : holdPending ? AuroraUtf8(L"取消锁定") : AuroraUtf8(L"锁定帧");
        if (ImGui::Button(holdButton.c_str()))
        {
            if (holding || holdPending)
                DlssNr::ReleaseInspectionHold();
            else
                DlssNr::RequestInspectionHold();
        }
        ImGui::EndDisabled();
        HelpMarker(AuroraUtf8(L"仅用于 D3D12 检查：锁定下一帧成功处理后的代理图、模型输出和原始画面，便于静态比较。锁定期间“对比”和“调试视图”仍可交互，但模型不会继续运行，直到点击“恢复”。这不会暂停游戏本身。\n\n模型参数和色彩参数的修改会在恢复后生效；改变分辨率会自动解除锁定。执行“捕获”时会先恢复实时渲染，而“锁定帧”会等待当前捕获完成。\n\n原生 Vulkan 或实验性代理路径下不可用。").c_str());
        ImGui::SameLine();

        if (capturing)
        {
            ImGui::TextDisabled(AURORA_CN("正在捕获……"));
        }
        else if (ImGui::Button(AURORA_CN("捕获 8 帧")))
        {
            DlssNr::RequestCapture(8);
        }

        HelpMarker(AuroraUtf8(L"连续捕获 8 帧，并分别保存两套结果：一套是超分器原始输出，另一套是应用神经渲染后的画面。\n\n两套图来自同一次运行、同一批帧，唯一变量就是神经渲染本身，因此比录制两段不同视频更适合做严谨对比；视频录制不仅镜头路径难以完全一致，编码器还会丢失细微的时间域信息。\n\n以原始数据写入 OptiScaler 旁的 dlssnr-capture 文件夹。每次固定保存 8 帧，并覆盖上一轮捕获。").c_str());

        static const std::string compareOff = AuroraUtf8(L"关闭");
        static const std::string compareSide = AuroraUtf8(L"并排对比");
        static const std::string compareWipe = AuroraUtf8(L"分割线对比");
        static const char* compareNames[] = { compareOff.c_str(), compareSide.c_str(), compareWipe.c_str() };
        int compare = (int) config->DlssNrCompare.value_or_default();
        if (ImGui::Combo(AURORA_CN("对比模式"), &compare, compareNames, IM_ARRAYSIZE(compareNames)))
            config->DlssNrCompare = (uint32_t) compare;

        HelpMarker(AuroraUtf8(L"把未处理画面与神经渲染结果同时显示，避免只能来回切换后凭记忆比较。\n\n“并排对比”会把完整画面分别放在左右两半：左侧为原始超分输出，右侧为神经渲染结果。为了塞进半个屏幕，图像会横向压缩，因此更适合观察差异，不适合正常游玩。\n\n“分割线对比”在同一张完整尺寸画面上直接切开左右两种结果，不会重新缩放，画面比例保持正常，适合边玩边比较。可用下方“分割位置”移动分界线，设置会被保存。\n\n两种模式都不要求菜单保持打开，分界处会显示细线。").c_str());

        if (compare != 0)
        {
            bool swap = config->DlssNrCompareSwap.value_or_default();
            if (ImGui::Checkbox(AURORA_CN("左右互换"), &swap))
                config->DlssNrCompareSwap = swap;

            bool tags = config->DlssNrCompareTags.value_or_default();
            if (ImGui::Checkbox(AURORA_CN("标注两侧"), &tags))
                config->DlssNrCompareTags = tags;

            HelpMarker(AuroraUtf8(L"把“原始/处理后”等标记直接绘制到画面中，因此即使截图离开本机也能看出哪一侧是哪一种结果。标签与图像处于同一画面平面；在分割线模式下，分界线会像切换图像一样显示/隐藏对应标签。开启“左右互换”后，标签会与各自画面一起移动。").c_str());

            if (tags)
            {
                float tagScale = config->DlssNrTagScale.value_or_default();
                if (ImGui::SliderFloat(AURORA_CN("标签大小"), &tagScale, 0.5f, 5.0f, "%.1fx"))
                    config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
            }

            HelpMarker(AuroraUtf8(L"将神经渲染后的画面换到另一侧。\n\n当你已经形成初步偏好时，建议至少交换一次左右再看。人眼对左右位置并非完全没有偏差，有时仅仅因为某一画面处在更习惯的位置，就会被误认为“更好”。交换后如果同一处理结果仍然明显胜出，判断会更可靠。").c_str());
        }

        if (compare == 1)
        {
            float zoom = config->DlssNrCompareZoom.value_or_default();
            if (ImGui::SliderFloat(AURORA_CN("缩放"), &zoom, 1.0f, 2.0f, "%.2f"))
                config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);

            HelpMarker(AuroraUtf8(L"控制并排对比时每一侧显示原画面的多少范围。\n\n每一侧只有完整画面一半的宽度、但高度不变，因此不可能既铺满区域又完全保持原始比例。\n\n1.0 会显示完整画面并保持正确比例，因此上下会出现留黑；2.0 会铺满半屏，但左右内容会被裁掉；中间数值是在“完整视野”和“填满区域”之间折中。").c_str());
        }

        if (compare == 2)
        {
            float split = config->DlssNrCompareSplit.value_or_default();
            if (ImGui::SliderFloat(AURORA_CN("分割位置"), &split, 0.0f, 1.0f, "%.2f"))
                config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);

            HelpMarker(AuroraUtf8(L"控制分割线的位置。默认情况下，分割线左侧是超分器原始输出，右侧是神经渲染后的画面；开启“左右互换”后两侧相反。").c_str());
        }

        static const std::string debugOff = AuroraUtf8(L"关闭");
        static const std::string debugProxy = AuroraUtf8(L"输入代理图（模型看到的画面）");
        static const std::string debugRaw = AuroraUtf8(L"模型原始输出");
        static const std::string debugDiff = AuroraUtf8(L"差异（放大显示）");
        static const char* debugNames[] = { debugOff.c_str(), debugProxy.c_str(), debugRaw.c_str(), debugDiff.c_str() };
        int debugView = (int) config->DlssNrDebugView.value_or_default();
        if (ImGui::Combo(AURORA_CN("调试视图"), &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
            config->DlssNrDebugView = (uint32_t) debugView;

        HelpMarker(AuroraUtf8(L"“输入代理图”就是实际送进模型的画面。如果它本身看起来就不正常，通常说明白点/曝光映射有问题，此时后续模型效果都没有比较意义。\n\n“差异”会把模型真正改动的部分放大约 20 倍，并以中性灰为中心显示；如果整张画面几乎都是均匀灰色，说明模型基本没有产生变化。").c_str());

        ImGui::PopItemWidth();
    }
}

} // namespace DlssNr

