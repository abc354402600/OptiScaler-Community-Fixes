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
        ImGui::TextDisabled("global");
    }

    return changed;
}

void RenderMenu(Config* config, float menuResScale)
{

    // DLSS Neural Rendering -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("DLSS Neural Rendering"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool enabled = config->DlssNrEnabled.value_or_default();
        if (ImGui::Checkbox("Enable Neural Rendering", &enabled))
            config->DlssNrEnabled = enabled;

        HelpMarker("프레임 생성이 확인하기 전에, 업스케일러 출력에 디테일을 합성합니다."
                       ""
                       ""
                       "OptiScaler 옆에 이름이 한 글자 차이인 두 파일이 필요합니다:"
                       "\n  nvngx_dlssnr.dll       NVIDIA 모델(약 165 MB) -- 사용자가 준비\n\n  nvngx.dll_dlssnr.dll   포워더(약 13 KB) -- 이 패키지에 포함\n\n문서화되지 않고 직접 구동되는 방식이라, 공식적으로 지원되는 부분은 없습니다.");

        // The toggle can be bound to a key, and nobody would think to look for it under Keybinds
        // unless told. Dimmed, because it is a note rather than a setting.
        ImGui::TextDisabled("키로 켜고 끌 수 있습니다. Keybinds의 \"Neural Rendering\"에 바인딩하세요.");

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
            ImGui::TextDisabled("꺼짐. 모델은 로드된 상태로 유지되므로 다시 켜면 즉시 적용됩니다.");
        }
        else if (!DlssNr::IsRunning() && !vulkan)
        {
            const char* reason = DlssNr::FailureReason();

            if (reason[0] != 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Off for this session: %s.", reason);
                ImGui::SameLine();

                if (ImGui::SmallButton("Retry"))
                    DlssNr::RetryAfterFailure();
            }
            // The model is D3D12 and Vulkan only. A native D3D11 upscaler creates no D3D12 device,
            // so nothing ever arrives and the wait below would never end.
            else if (auto feature = State::Instance().currentFeature;
                     feature != nullptr && feature->Api() == API::DX11 && !feature->IsWithDx12())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                   "%s runs natively on D3D11, which the model has no path for.",
                                   feature->Name().c_str());
                ImGui::TextDisabled("위에서 w/Dx12 표시가 있는 업스케일러를 선택한 뒤 게임을 재시작하세요.");
            }
            else if (enabled)
                ImGui::TextUnformatted("Waiting for the upscaler to run.");
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
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Running%s - %.2f ms per frame",
                                   vulkan ? " natively on Vulkan" : "", ms.value());
            else if (vulkan)
                // Measured but not yet read: the first few frames are still in the query ring.
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Running natively on Vulkan - %llu frames",
                                   DlssNr::FramesVk());
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Running.");

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("The whole pass: the staging copies and the resolve as well as the"
                                  "\nmodel. Timing only the model would flatter the number."
                                  "\n\nCompare it against the frame time at the bottom of this window to"
                                  "\nsee what it is costing you.");
        }

        ImGui::Spacing();
        ImGui::PushItemWidth(220.0f * menuResScale);

        ImGui::SeparatorText("Cost");

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

            if (ImGui::SliderInt("Passes", &passes, 1, passLimit,
                                 passes == 1 ? "%d (native)" : "%dx model cost"))
                pendingPasses = passes;

            ImGui::PopStyleColor(2);

            if (ImGui::IsItemDeactivatedAfterEdit() && pendingPasses >= 0)
            {
                config->DlssNrPasses = (uint32_t) std::clamp(pendingPasses, 1, passLimit);
                pendingPasses = -1;
            }

            if (bool lift = unlocked; ImGui::Checkbox("Lift the pass limit", &lift))
            {
                config->DlssNrUnlockPasses = lift;

                // Dropping the ceiling under a larger count would leave the file asking for passes
                // the slider can no longer show.
                if (!lift && config->DlssNrPasses.value_or_default() > DlssNr::kDefaultMaxPasses)
                    config->DlssNrPasses = DlssNr::kDefaultMaxPasses;
            }

            const std::string liftTip =
                "Raises the slider above to " + std::to_string(DlssNr::kMaxPasses) +
                ", which is far past what this pass"
                "\nwas built for. Expect the frame time to scale with it and the game to stop being"
                "\nplayable well before the top."
                "\n\nCost is exactly linear and the model is nearly all of it, so ten passes is ten"
                "\nmodel runs in one frame. Each also holds an NGX feature with its own history,"
                "\nsized by the driver, and they are built one at a time with a settle between --"
                "\nreaching a large count takes a while and the frames spent building show nothing."
                "\n\nThe ratio guard under Colour has to rise with the count or the extra passes"
                "\nspend their contribution against the clamp.";

            HelpMarker(liftTip.c_str());

            // The tooltip is not enough for a slider that now reaches thirty. Say the cost on screen,
            // and keep saying it while the count is past what the slider offers by default.
            if (unlocked)
            {
                const int live = (int) config->DlssNrPasses.value_or_default();

                if (live > (int) DlssNr::kDefaultMaxPasses)
                    ImGui::TextColored(ImVec4(1.00f, 0.25f, 0.85f, 1.0f),
                                       "%d passes: %dx the model's cost, every frame.", live, live);
                else
                    ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f),
                                       "Unlocked. Each pass past this point is another whole model run.");
            }

            // Per-pass settings, one node each, only for the passes that are running.
            //
            // Written back as the sparse "2:intensity=0.5;3:style=1" the pass reads. A pass whose
            // controls all sit at the global value contributes nothing, so the string stays empty
            // until something is actually different and the default costs nothing to carry.
            const auto liveCount = (int) config->DlssNrPasses.value_or_default();

            if (liveCount > 1)
            {
                if (ImGui::TreeNode("Per pass"))
                {
                    auto overrides = DlssNr::ParsePassOverridesForMenu(
                        config->DlssNrPassOverrides.value_or_default());

                    bool edited = false;

                    for (int pass = 0; pass < liveCount; ++pass)
                    {
                        const std::string label = "Pass " + std::to_string(pass + 1);

                        if (!ImGui::TreeNode(label.c_str()))
                            continue;

                        auto& own = overrides[pass];

                        edited |= PassOverrideSlider("Intensity", &own.Intensity,
                                                     config->DlssNrIntensity.value_or_default(),
                                                     0.0f, 4.0f, pass);
                        edited |= PassOverrideSlider("Detail strength", &own.LocalStructure,
                                                     config->DlssNrLocalStructure.value_or_default(),
                                                     0.0f, 4.0f, pass);
                        edited |= PassOverrideSlider("Local tone", &own.LocalTone,
                                                     config->DlssNrLocalTone.value_or_default(),
                                                     0.0f, 4.0f, pass);
                        edited |= PassOverrideSlider("Skin structure", &own.SkinStructure,
                                                     config->DlssNrSkinStructure.value_or_default(),
                                                     -1.0f, 4.0f, pass);

                        ImGui::TreePop();
                    }

                    if (edited)
                        config->DlssNrPassOverrides = DlssNr::SerializePassOverrides(overrides);

                    HelpMarker(
                        "각 패스에 전달되는 내용으로, 위의 값과 달라져야 하는 부분입니다."
                        ""
                        ""
                        "\"global\"로 남아 있는 항목은 바로 위의 설정을 따르므로,"
                        ""
                        "건드리지 않은 패스는 이 기능이 없던 때와 똑같이 동작합니다.\n\n\n패스는 누적됩니다. 뒤의 패스는 앞의 패스가 만들어 낸 결과를 봅니다.\n\n강도를 체인을 따라 점점 낮추면 마지막 패스들이 다시 증폭하는 대신\n\n이미 있는 것을 다듬게 됩니다.");

                    ImGui::TreePop();
                }
            }

            HelpMarker("모델이 프레임 위를 몇 번 도는지이며, 각 패스에는 이전 패스의"
                       ""
                       "결과가 보여집니다."
                       ""
                       ""
                       "여기서 가장 비용이 큰 항목입니다. 비용은 정확히 선형입니다. 패스 5개는"
                       ""
                       "모델 실행 5회이며, 이 패스 비용의 거의 전부가 모델입니다."
                       ""
                       ""
                       "다른 어떤 것으로도 얻을 수 없는 것은 모델이 디테일의 위치와"
                       ""
                       "색조, 채도를 다시 판단한다는 점입니다. Detail strength는 첫 패스가 그린"
                       ""
                       "맵을 증폭할 뿐, 다시 그릴 수는 없습니다."
                       "\n\n얻을 수 없는 것은 순수한 크기입니다. 그건 Detail strength와\n\nIntensity가 공짜로 해 줍니다. 이것보다 먼저 둘 다 시도하고, Model resolution을 올려 보세요.\n\n\n3을 넘기면 Colour 아래의 비율 가드도 함께 올리세요. 패스들이 휘도 비율을\n\n누적하고 가드가 이를 잘라 내므로, 한도를 넘는 분의 추가 실행은\n\n비용만 지불하고 버려집니다.\n\n\n각 패스는 자체 메모리를 가진 별도의 모델이며 자체 프레임 위에서\n\n구축되므로, 이 값을 올리면 적용까지 몇 초가 걸리고 VRAM 사용량도 함께 늘어납니다.\n\n\n네이티브 Vulkan이나 프록시 경로를 켠 상태에서는 효과가 없습니다.");
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

        if (ImGui::SliderInt("Model resolution", &scalePercent, 25, 200, "%d%%"))
            pendingScale = scalePercent;

        if (ImGui::IsItemDeactivatedAfterEdit() && pendingScale >= 0)
        {
            config->DlssNrWorkingScale = std::clamp(pendingScale, 25, 200) / 100.0f;
            pendingScale = -1;
        }

        HelpMarker("프레임 대비 모델의 래스터 크기로, 25%에서 200%까지입니다."
                       ""
                       "원본 프레임은 풀 디테일로 유지되고, 모델의 작업만 리샘플링됩니다."
                       ""
                       ""
                       "100% 미만에서는 모델이 더 작은 그림으로 작업하고 그 결과가 확대됩니다."
                       "\n절반 해상도는 모델 픽셀이 1/4이지만 미세 디테일은 부드러워집니다.\n\n\n100% 초과에서는 모델이 더 큰 래스터로 작업하고 결과가\n\n프레임 크기로 돌아옵니다. 200%는 각 축 2배, 모델 픽셀 4배이며 VRAM 비용도\n\n더 듭니다. 게임 지오메트리 샘플이 추가되지는 않습니다.");

        {
            bool dual = config->DlssNrDualFeature.value_or_default();

            if (ImGui::Checkbox("Run inside the upscaler", &dual))
                config->DlssNrDualFeature = dual;

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), "(experimental, restart)");

            HelpMarker("업스케일러를 둘로 나누고 그 사이에 모델을 넣습니다. 업스케일러는"
                       ""
                       "렌더 해상도로 기록하고, 모델이 그 위에서 실행되며, 디스플레이 해상도로의"
                       ""
                       "확대는 그 다음에 일어납니다."
                       ""
                       ""
                       "ray reconstruction을 사용하면 첫 번째 절반이 디노이저 이외 아무것도"
                       ""
                       "아니게 되는데, 이것이 가장 쓸 만한 배치입니다. 모델이 보는 프레임은"
                       ""
                       "깨끗하고, Performance에서는 픽셀 수가 1/4입니다."
                       "\n\n'Run before the upscaler'와 달리, 여기서의 프레임은 이미\n\n템포럴 누적을 거쳤으므로 모델에 알려 줄 방법이 없는 서브픽셀 지터는\n\n모델이 보기 전에 해소되어 있습니다.\n\n\n확대는 출력 스케일러이지 업스케일러 자체가 아닙니다. 업스케일러가\n\n다음에 빌드될 때 적용되므로, 체크한 뒤 게임을 재시작하거나 품질을 바꾸세요.\n\n\n렌더 해상도가 이미 디스플레이 해상도와 같으면 아무 일도 하지 않습니다.\n\nDLAA에서는 더 작은 프레임이 없어 실행할 대상이 없습니다.");

            if (dual)
            {
                // The same names the upscaler list uses, resolved through the same provider, so a
                // machine without DLSS is handed FSR here exactly as it is anywhere else.
                static const char* enlargerNames[] = { "Spatial (no motion vectors)", "DLSS", "FSR 2.2", "FSR 3.1",
                                                       "XeSS" };
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

                if (ImGui::Combo("Enlarged by", &index, enlargerNames, IM_ARRAYSIZE(enlargerNames)))
                {
                    if (enlargerValues[index].has_value())
                        config->DlssNrDualEnlarger = enlargerValues[index].value();
                    else
                        config->DlssNrDualEnlarger.reset();
                }

                HelpMarker("모델이 프레임을 편집한 뒤 프레임을 확대하는 주체입니다."
                           ""
                           ""
                           "Spatial은 모션 벡터도, 깊이도, 지터도 필요 없으므로 그 어느 것으로도"
                           ""
                           "틀릴 수 없습니다. 다만 시간적으로 근거할 것이 없어"
                           ""
                           "가장 부드러운 결과를 냅니다.\n\n\n업스케일러들은 더 선명하고 게임 자체의 프레임별 데이터를 사용합니다.\n\n이 머신이 실행할 수 없는 업스케일러는 실행 가능한 것으로 교체되며,\n\n메인 업스케일러 목록과 같은 방식입니다.\n\n\n업스케일러가 다음에 빌드될 때 적용됩니다.");
            }

            bool preUpscale = config->DlssNrPreUpscale.value_or_default();

            if (dual)
                ImGui::BeginDisabled();

            if (ImGui::Checkbox("Run before the upscaler", &preUpscale))
                config->DlssNrPreUpscale = preUpscale;

            if (dual)
                ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), "(experimental)");

            HelpMarker("업스케일러가 기록한 프레임 대신, 업스케일러가 읽기 직전의 프레임을"
                       ""
                       "모델에 보여 줍니다. 모델은 렌더 해상도로 실행되므로 Performance에서는"
                       ""
                       "업스케일러 이후 실행할 때의 약 1/4 비용이 들고, 재구성된 픽셀이 아니라"
                       ""
                       "렌더링된 픽셀을 봅니다."
                       ""
                       ""
                       "Model resolution과 달리 모델이 돌려주는 것을 부드럽게 하지 않습니다. 모델은"
                       "\n큰 프레임을 줄여 도는 것이 아니라 작은 프레임에 1:1로 실행되고,\n\n업스케일러가 다른 모든 것과 함께 모델의 작업을 확대합니다.\n\n\n시험되지 않은 영역입니다. 이 지점의 Colour는 매 프레임 다른 서브픽셀\n\n오프셋으로 지터가 걸리는데 모델에는 그 사실을 알릴 방법이 없으므로, 모델의\n\n히스토리가 볼 수 없는 오프셋을 향해 리프로젝트할 수 있습니다. 카메라가\n\n움직일 때 미세 디테일의 반짝임과 떨림을 살펴 보세요.");
        }

        // Only meaningful below 100%: at the same rate the residual collapses to the model's own
        // picture and the two modes are identical, so the control says so by going grey.
        {
            const bool reduced = config->DlssNrWorkingScale.value_or_default() < 0.999f;

            if (!reduced)
                ImGui::BeginDisabled();

            static const char* enlargeNames[] = { "Classic", "Matched residual" };
            int enlarge = config->DlssNrTransfer.value_or_default() == 1 ? 1 : 0;

            if (ImGui::Combo("Enlargement", &enlarge, enlargeNames, IM_ARRAYSIZE(enlargeNames)))
                config->DlssNrTransfer = (uint32_t) enlarge;

            if (!reduced)
                ImGui::EndDisabled();

            HelpMarker("모델이 프레임보다 작게 실행되었을 때 그 작업을 되돌려 올리는 방식입니다."
                       ""
                       ""
                       "Classic은 모델의 작은 그림을 풀사이즈 프레임에 직접 합성합니다."
                       ""
                       "두 그림은 축소의 흐림만큼이나 모델의 편집만큼 어긋나는데, 합성은"
                       ""
                       "이 둘을 구분하지 못하고 그 흐림을 프레임에 있고 모델은 본 적 없는"
                       ""
                       "밝기로 읽습니다. Model resolution이 낮을수록 그 오차는 커지며,"
                       "\n50%에서 드러나는 것은 색 시프트입니다.\n\n\nMatched residual은 모델의 차이만 끌어 올려 프레임 자체의\n\n프록시 위에 얹으므로, 비교되는 두 그림은 모두 풀사이즈이고 작은\n\n래스터에서 온 것은 편집 그 자체뿐입니다.\n\n\n100%에서는 효과가 없습니다. 옮길 잔차가 없고 둘은 동일합니다.\n\n\n이 포크에서 hhkbble의 멀티 패스 작업에서 비롯되었습니다.");
        }

        ImGui::SeparatorText("How much of it lands");

        float transfer = config->DlssNrTransferStrength.value_or_default();
        if (ImGui::SliderFloat("Detail strength", &transfer, 0.0f, 2.0f, "%.2f"))
            config->DlssNrTransferStrength = transfer;

        HelpMarker("프레임이 모델의 그림 쪽으로 얼마나 이동하는지입니다."
                       ""
                       ""
                       "모델의 결과는 프레임에 더해지는 것이 아니라, 그 자체로 완결된"
                       ""
                       "한 장의 그림이며, 휘도가 원본이 지시하는 위치에 앉도록 리스케일되어"
                       ""
                       "있습니다. 이 항목은 둘 사이를 블렌드하므로 양쪽 끝도 실제 그림이고"
                       ""
                       "그 사이의 모든 것도 그렇습니다.\n\n\n0은 업스케일러가 만든 것을 그대로 돌려 줍니다. 1은 모델의 그림입니다.\n\n\n1을 넘으면 같은 방향으로 그 너머까지 나아가는데, 이는 모델이\n\n요청한 것이 아닙니다. 무엇을 하는지 보려면 쓰고 다시 내려오세요.\n\n효과를 더 원하면 이 항목을 올리는 것이 맞습니다. Intensity는 모델의\n\n것이라 그 쓰임은 모델이 결정합니다.");

        float colour = config->DlssNrColourStrength.value_or_default();
        if (ImGui::SliderFloat("Colour strength", &colour, 0.0f, 1.0f, "%.2f"))
            config->DlssNrColourStrength = colour;

        HelpMarker("모델의 색이 빛과 함께 도착하는지 여부입니다."
                       ""
                       ""
                       "0은 게임 고유의 색조를 정확히 유지합니다. 모든 픽셀은 원본 색이고"
                       ""
                       "밝기만 모델의 판단을 전달합니다. 디테일과 함께 게임 정확한 색입니다."
                       ""
                       "1은 모델의 색도 자체 색조로 가져오되, AP1로 클램프하여\n\n도달할 수 없는 것을 요구하지 않게 합니다.\n\n\n이것만으로는 색조를 이동할 수 없습니다. 하나에 색 차이를 더하는 대신\n\n완성된 두 그림 사이를 보간하며, 색 차이를 더하던 방식은\n\n따뜻한 피사체를 초록으로 돌려 보내곤 했습니다.");

        ImGui::SeparatorText("Model");

        ImGui::TextUnformatted("Read when the model is built, so a change rebuilds it after a moment.");

        static const char* nrPresetNames[] = { "Default", "Preset 1", "Preset 2", "Preset 3" };
        int preset = (int) config->DlssNrPreset.value_or_default();
        if (ImGui::Combo("Model preset", &preset, nrPresetNames, IM_ARRAYSIZE(nrPresetNames)))
            config->DlssNrPreset = (uint32_t) preset;

        HelpMarker("Default는 선택을 모델에게 맡깁니다."
                       ""
                       "\nsuper resolution이나 ray reconstruction 프리셋과는 스케일이 다르며,\n\n같은 숫자가 여기서는 다른 의미를 가집니다.");

        static const char* nrStyleNames[] = { "Default (standard)", "Natural", "Cinematic" };
        int style = (int) config->DlssNrStyle.value_or_default();

        if (style > 2)
            style = 2;

        if (ImGui::Combo("Style", &style, nrStyleNames, IM_ARRAYSIZE(nrStyleNames)))
            config->DlssNrStyle = (uint32_t) style;

        HelpMarker("모델 자체의 처리 프로파일입니다."
                   ""
                   ""
                   "Default (standard): 가장 강합니다. 로컬 대비를 높이고 조명을 깊게"
                   ""
                   "하며, 과채도나 스타일리시한 느낌이 될 수 있습니다. '모델이"
                   ""
                   "게임의 느낌을 바꿨다'고 읽히는 대부분은 이 프로파일입니다."
                   "\n\nNatural: 같은 디테일 작업을 더 부드럽게 합니다. 피부 톤과\n\n톤 밸런스를 게임이 렌더링한 것에 더 가깝게 유지합니다.\n\n\nCinematic: 빛번짐과 과잉 처리를 낮춰 필름 같은 느낌을 냅니다.\n\n\n모델이 빌드될 때 읽히므로, 바꾸면 잠시 후 모델이 다시 빌드됩니다.\n\n이름들은 커뮤니티 테스트에서 나왔으며, NVIDIA는 바이너리에 이름을 담지 않습니다.");

        DeferredSlider("Intensity", &config->DlssNrIntensity, 0.0f, 2.0f);

        HelpMarker("모델 자체의 강도 항목으로, 모델 내부에서 적용됩니다. 그 결과를"
                       "\n나중에 배율 조정하는 위의 Detail strength와는 별개입니다.");

        DeferredSlider("Local structure", &config->DlssNrLocalStructure, 0.0f, 2.0f);

        DeferredSlider("Local tone", &config->DlssNrLocalTone, 0.0f, 2.0f);


        DeferredSlider("Skin structure", &config->DlssNrSkinStructure, -1.0f, 2.0f);

        HelpMarker("-1은 로컬 구조를 따른다는 뜻이며 모델 자체의 기본값입니다."
                       "\n강도 0이 아닙니다. 0 이상은 프레임의 나머지와 무관하게 피부를 설정합니다.");

        bool autoMask = config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox("Auto skin mask", &autoMask))
            config->DlssNrAutoMask = autoMask;

        HelpMarker("모델이 프레임을 균일하게 다루는 대신 피부를 스스로 찾도록 합니다.");

        ImGui::SeparatorText("Colour");

        ImGui::TextDisabled("모델은 완성된, sRGB 인코딩 프레임으로 학습되었습니다. 업스케일러의"
                            ""
                            "출력은 그렇지 않습니다. 선형이고 상한이 없습니다. 이 항목들은 그것을"
                            "\n모델이 인식하는 형태로 매핑하는 방법을 결정합니다. 게임이 이미\n\n톤 매핑된 프레임이라고 보고하면 그대로 통과시키며 이 항목들은 적용되지 않습니다.");

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

            static const char* sourceNames[] = { "Paper white only", "The game's own exposure",
                                                 "A buffer the scan found" };

            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 2)
                source = 0;

            if (ImGui::Combo("White point from", &source, sourceNames, IM_ARRAYSIZE(sourceNames)))
            {
                config->DlssNrWhitePointSource = (uint32_t) source;

                // Nothing else to set. The scan asks the source whether it is wanted, so choosing
                // it here is the whole of switching it on -- there is no second flag to keep in
                // step, and so no way for the two to disagree.
            }

            HelpMarker("프레임을 나누는 숫자가 어디서 오는지입니다."
                           ""
                           ""
                           "Paper white만 -- 아래 슬라이더뿐이고 그 외에는 없습니다. 노출이"
                           ""
                           "움직이지 않는 게임에는 맞지만, 움직이는 순간 틀립니다. 하나의"
                           ""
                           "상수로 동굴과 들판을 모두 책임질 수는 없습니다."
                           ""
                           ""
                           "게임 자체의 노출 -- 게임이 업스케일러에 건네는"
                           ""
                           "텍스처에서 읽습니다. 가장 좋은 원천입니다. 업스트림에서\n\n결정되고 이 패스가 무엇을 하든 움직일 수 없기 때문입니다.\n\n모든 게임이 제공하는 것은 아닙니다.\n\n\n스캔이 찾아 낸 버퍼 -- 노출을 계산만 하고 넘기지 않는\n\n게임용입니다. 어림짐작입니다. 후보는 모양으로 매칭되며, GTA V에서\n\n가장 좋은 후보는 실제 노출을 따라가지만 자체 스케일을 가지고 있어,\n\nanchor의 비율이 그것을 상쇄합니다. 한 번의 anchoring이 필요하고,\n\n그 뒤 확인이 필요합니다.");

            // Availability, in colour, for the option currently chosen.
            if (source == 1)
            {
                if (!vk && ex.seenFrames == 0)
                    ImGui::TextDisabled("프레임 대기 중...");
                else if (!haveExposure)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       "This game supplies no exposure -- paper white is in use. Try "
                                       "the scan instead.");
                else if (vk)
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "This game supplies an exposure and it is being read.");
                else if (ex.exposure > 1e-6f)
                {
                    const float trim =
                        std::clamp(config->DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Game exposure %.4f  ->  white point %.2f%s", ex.exposure,
                                       ex.preExposure / ex.exposure * trim,
                                       ex.offeredNow ? "" : "  (held: absent this frame)");
                }
                else
                    ImGui::TextDisabled("노출을 읽는 중...");
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
                                           "Nothing in this game is shaped like an exposure.");
                    else
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                           "Watching %u, none moving yet -- go between light and shade.",
                                           watching);
                }
                else if (!haveAnchor)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       "Found one. Set paper white below until the picture looks "
                                       "right, then press Anchor here.");
                else
                {
                    const float w = DlssNr::ExposureScan::AnchoredWhitePoint(
                        anchorNow, config->DlssNrScanInverted.value_or_default(),
                        config->DlssNrScanTrim.value_or_default());
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Anchored. Scan %.5f  ->  white point %.2f", anchorNow, w);
                }
            }
            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   "This game supplies an exposure -- the option above would use it.");
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
        // silently destroyed the number found in the other, and there was no way back to "just take\n// the game's answer" short of knowing that the number for it was 1. Separate values fix both.
        // Switching modes is now non-destructive in both directions.
        // The trim belongs to both automatic sources, since both end in "the game's number times a\n// little". Only the manual source gets the absolute slider.
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

            char lbl[48];
            if (editingRow)
                snprintf(lbl, sizeof(lbl), "Paper white (editing point %d)", selectedAnchor + 1);
            else
                snprintf(lbl, sizeof(lbl), "Paper white");

            if (ImGui::SliderFloat(lbl, &pw, 0.25f, 2000.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
            {
                if (editingRow)
                {
                    DlssNr::ExposureScan::AnchorSetWhite(selectedAnchor, pw);
                    config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                }
                else
                    config->DlssNrWhitePointScale = pw;
            }

            HelpMarker("선택한 캘리브레이션 지점의 화이트 포인트이며, 선택된 행이"
                           ""
                           "없을 때는 다음 Anchor 누름이 캡처할 값입니다."
                           ""
                           ""
                           "여기서 그림이 맞아 보일 때까지 맞춘 뒤 Anchor 하세요. 아주 다른\n\n조명으로 옮겨 가서 다시 하세요. 두 지점이 버퍼의 실제\n\n관계를 확정하며 화이트 포인트는 그 사이에서 유지됩니다. 아래 행을\n\n클릭하면 돌아가서 그 지점을 조정하고, 다시 클릭하면 놓아 줍니다.");

            // A global multiplier on the interpolated result, kept for parity with the other
            // sources. The points themselves are the real control here, so this stays near 1.
            float trim = config->DlssNrScanTrim.value_or_default();

            if (ImGui::SliderFloat("Trim (x the scan)", &trim, 0.25f, 4.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);

            ImGui::SameLine();

            if (ImGui::SmallButton("Reset##scantrim"))
                config->DlssNrScanTrim = 1.0f;
        }
        else if (wpSource == 1)
        {
            const bool ofScan = false;

            float trim = ofScan ? config->DlssNrScanTrim.value_or_default()
                                : config->DlssNrWhitePointTrim.value_or_default();

            if (ImGui::SliderFloat(ofScan ? "Trim (x the scan)" : "Trim (x the game's exposure)", &trim,
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
            if (ImGui::SmallButton("Reset##wptrim"))
            {
                if (ofScan)
                    config->DlssNrScanTrim = 1.0f;
                else
                    config->DlssNrWhitePointTrim = 1.0f;
            }

            HelpMarker("게임이 준 노출에 대한 배수입니다. 1.00x는 그 숫자를"
                           ""
                           "그대로 쓰며, 여기서는 그것이 옳은 답입니다."
                           ""
                           ""
                           "이것은 대충 맞추는 보정값이 아닙니다. 어떤 게임이 맞아 보이려면 trim이"
                           ""
                           "1에서 멀리 떨어져야 한다면, 그것은 이 게임에서 읽히는 노출이"
                           "\n잘못되었다는 증거이지 게임이 trim을 원한다는 뜻이 아닙니다. 대략\n\n0.8에서 1.25 사이는 정직한 튜닝입니다. 4에 손을 뻗는다는 것은\n\n업스트림 어딘가가 고장 났고 trim이 그것을 숨기고 있다는 뜻입니다.\n\n\n수동 Paper white는 별도로 보관되며, 위의 옵션을\n\n끄면 그대로 돌아옵니다.");
        }
        else
        {
            // Logarithmic, because the useful range is not linear. A quarter to 2000: the low end
            // because a frame the game already tone mapped wants roughly 1, the high end because
            // there is no principled ceiling -- this is a divisor on an open-ended linear buffer, and
            // how far up a given game needs to go is a property of that game's exposure rather than
            // of anything that can be bounded here. One tester was still improving at 100.
            float wpScale = config->DlssNrWhitePointScale.value_or_default();

            if (ImGui::SliderFloat("Paper white", &wpScale, 0.25f, 2000.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointScale = wpScale;

        HelpMarker("모델이 보기 전에 프레임을 무엇으로 나누는지입니다. 다른 화이트"
                       ""
                       "포인트는 없습니다. 이것이 전부입니다."
                       ""
                       ""
                       "모델은 white가 1에 있는 완성된 프레임으로 학습되었습니다."
                       ""
                       "업스케일러의 출력은 선형이고 상한이 없으므로, white가 어디에 있는지를"
                       ""
                       "말해 줄 무언가가 필요합니다. 게임의 DLSS 버퍼가 선형 HDR이라면"
                       ""
                       "그 숫자는 1에서 아주 멉니다. Monster Hunter Wilds에서 측정하면"
                       ""
                       "모델의 디테일이 프레임에 닿기 시작하기까지 16 이상이 걸렸고,"
                       ""
                       "그늘진 캠프에 맞는 값은 같은 게임의 한낮 야외에는 여전히 너무 작았습니다."
                       ""
                       ""
                       "너무 낮으면 거의 모든 픽셀이 소프트 니에 걸립니다. 모델에는"
                       "\n거의 흰 납작한 그림이 보이고, 그 결과는 스케일이 날아가며,\n\n색조만 남습니다. 그것은 잃어버린 디테일이 아니라 색 틴트로 읽힙니다. 너무\n\n높으면 노출 부족의 그림이 보이고, 결과는 열화되며, 이 같은\n\n숫자가 나가는 길에 그 오차를 다시 곱합니다.\n\n\n그림이 더 이상 좋아지지 않을 때까지 올리세요. 그 지점을 넘기면 정체되지 않고 반대 방향으로 나빠집니다.\n\n\n이 값은 원래 측정된 화이트 포인트에 대한 배율이었습니다. 그 측정은 사라졌습니다. 장면 밝기를 white 위치 대신 읽어서 모델에는 3배나 어두운 그림을 넘기고, 하이라이트 경로에는 되돌려 줄 것을 남기지 않았습니다.\n\n\n강도가 0이면 이 값이 무엇이든 프레임은 비트 단위로 동일합니다.");
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
                    ImGui::Checkbox("Show the light meter on screen", &meter))
                    config->DlssNrScanMeter = meter;

                HelpMarker("구석의 램프입니다. 어두우면 빨강, 충분히 밝으면 초록,"
                               ""
                               "그 사이는 단계별 색으로 표시되며 옆에는 수치가 함께 보입니다."
                               ""
                               ""
                               "스캔이 단순히 실행 중인 것이 아니라 TRACKING(추적) 중임을"
                               "\n한눈에 알려 줍니다. 그늘로 들어가면 빨강 쪽으로 기울고,\n\n밝은 곳으로 나오면 초록이 되어야 합니다. 반대로 움직인다면\n\n위의 설정이 그 문제를 해결해 줍니다.\n\n\n순수한 표시용이며 아무것도 변경하지 않습니다.");

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

                if (ImGui::Button("Anchor here"))
                {
                    if (DlssNr::ExposureScan::AnchorAdd(
                            live, std::max(0.01f, config->DlssNrWhitePointScale.value_or_default())))
                    {
                        config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                        selectedAnchor = -1;
                    }
                }

                ImGui::EndDisabled();

                HelpMarker("위에서 종이 흰색을 화면이 자연스러워 보일 때까지 맞춘 뒤 이 항목을 누릅니다."
                               ""
                               ""
                               "첫 누름으로 한 지점을 보정합니다. 이후 흰색 포인트는"
                               ""
                               "이전과 같이 스캔 값에 비율로 따라갑니다. 조도가 크게"
                               ""
                               "다른 환경에서 종이 흰색을 다시 맞추고 다시 누르면\n\n두 번째 지점이 버퍼의 실제 곡선을 확정하며\n\n두 지점 사이 전체가 정확해집니다. 최대 8개까지 가능합니다.\n\n\n이 표는 게임별로 저장되며 공유할 수 있습니다. 한 사람이 게임을 보정하면\n\n프로필을 가져오는 모든 사람에게 같은 수치가 적용됩니다.");

                if (!isSource)
                    ImGui::TextDisabled("(스캔은 관찰만 하며, 위의 흰색 포인트는"
                                        "다른 곳에서 가져옵니다)");

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
                        char row[96];
                        snprintf(row, sizeof(row), "%s scan %.4f  ->  white %.2f%s",
                                 ((int) i == active && isSource) ? ">" : "  ", anchors[i].scan,
                                 anchors[i].white, sel ? "   [editing]" : "");

                        // Click selects the row (slider edits it); click again deselects (slider
                        // returns to the live unanchored point).
                        if (ImGui::Selectable(row, sel))
                            selectedAnchor = sel ? -1 : (int) i;

                        ImGui::PopID();
                    }

                    ImGui::TextDisabled("행을 클릭하면 위 슬라이더로 편집하고, 다시 클릭하면"
                                        " 현재 적용 지점을 조절합니다. > 표시가 지금 사용 중인 지점입니다.");
                }

                // The direction flag only means anything with a single point; with two or more the
                // direction the white point moves is already fixed by the data.
                if (anchors.size() == 1)
                {
                    bool inverted = config->DlssNrScanInverted.value_or_default();
                    if (ImGui::Checkbox("The number runs the other way", &inverted))
                        config->DlssNrScanInverted = inverted;

                    HelpMarker("올바른 방향으로는 나아져야 할 화면이 오히려 나빠진다면 이 항목을"
                                   ""
                                   "뒤집으세요. 대부분의 엔진은 장면이 밝아질수록 값이 작아지는"
                                   ""
                                   "노출을 저장하고, 일부 엔진은 그 역수를 저장합니다. 형태로 찾은\n\n버퍼만으로는 어느 쪽인지 알 수 없습니다. 조도가 다른 곳에서\n\n두 번째 기준점을 추가하면 자동으로 판별되어 이 항목이 사라집니다.");
                }

                if (isSource && live > 0.0f && !anchors.empty())
                {
                    const float w = DlssNr::ExposureScan::AnchoredWhitePoint(
                        live, config->DlssNrScanInverted.value_or_default(),
                        config->DlssNrScanTrim.value_or_default());

                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Scan %.5f  ->  white point %.2f   (%u point%s)", live, w,
                                       (unsigned) anchors.size(), anchors.size() == 1 ? "" : "s");
                }

                // Everything below is read-out rather than control: what the scan is looking at and
                // how to tell whether it found the right thing. Folded away because the two decisions
                // that matter -- anchor, and which way the number runs -- are above it.
                if (ImGui::TreeNode("Advanced"))
                {

                    const auto found = DlssNr::ExposureScan::Report();
                    const char* why = DlssNr::ExposureScan::Status();

                    if (found.empty())
                    {
                        ImGui::TextDisabled("%s", why != nullptr && why[0] != 0
                                                      ? why
                                                      : "아직 일치하는 항목이 없습니다.");
                    }
                    else
                    {
                        for (size_t i = 0; i < found.size(); ++i)
                        {
                            const auto& c = found[i];

                            if (c.reads == 0)
                            {
                                ImGui::TextDisabled("%zu. %s -- 아직 읽지 않음", i + 1, c.shape.c_str());
                                continue;
                            }

                            // Moving is the whole signal, so it is the thing that is coloured.
                            ImGui::TextColored(c.moves ? ImVec4(0.45f, 0.8f, 0.45f, 1.0f)
                                                       : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                               "%zu. %s = %.5f  (seen %.5f..%.5f) %s", i + 1,
                                               c.shape.c_str(), c.latest, c.lowest, c.highest,
                                               c.moves ? "MOVES" : "flat so far");
                        }

                        ImGui::TextDisabled("그늘에서 햇빛 아래로 이동해 보세요. 실제 노출이라면 값이 움직입니다.");
                        ImGui::TextDisabled("끝없이 올라가기만 하는 값은 노출이 아니라 카운터입니다.");
                    }

                    ImGui::TreePop();
                }
            }
        }

        // Reaches as far as Passes does. The guard is applied once to the finished composition while
        // the passes compound the ratio it bounds, so a count the slider above can reach needs a guard
        // that can follow it.
        float maxRatio = config->DlssNrMaxRatio.value_or_default();
        if (ImGui::SliderFloat("Highlight guard", &maxRatio, 1.0f, (float) DlssNr::kMaxPasses, "%.1fx"))
            config->DlssNrMaxRatio = maxRatio;

        HelpMarker("패스가 픽셀을 옮길 수 있는 최대 폭입니다. 기존 값의 배수로, 양쪽"
                       ""
                       "방향 모두에 적용됩니다. 픽셀은 이 한계를 넘어 밝아질 수 없고, 그"
                       ""
                       "역수를 넘어 어두워질 수도 없습니다."
                       ""
                       ""
                       "밝은 영역은 모델이 가장 말할 수 없는 부분이자, 답을 프레임에 맞춰"
                       ""
                       "재조정할 때 피해가 가장 큰 부분입니다. 초기 버전은 장면의 모든"
                       ""
                       "스트립 조명을 색이 들어간 셀들로 만들어 버렸습니다. 2x는 디테일을"
                       ""
                       "지키면서 이런 실패를 막아 줍니다. 밝은 부분이 잘려 보일 때만 올리세요."
                       ""
                       ""
                       "이 가드는 최종 합성 결과에 한 번 적용되는 반면, 패스는 그 가드가\n\n묶는 비율을 계속 곱합니다. 패스 수에 가까운 값을 쓰면 각 패스가\n\n얻는 여유가 대략 일정해집니다. 1 패스에 1x, 2 패스에 2x, 3 패스에 3x.\n\n그대로 두면 세 번째 패스의 기여 대부분이 클램프에 막힙니다.\n\n\n어둡게 하는 쪽은 한때 제한 없이 두었고, 가드 자체는\n\n컬러 강도 0인 끝만 묶었습니다. 그래서 기본 강도에서는 아무것도 묶지 않았습니다. Nioh 3 때문에 둘 다 고쳤습니다. 소프트 니가 전혀 작동하지 않을 만큼 어두운 장면에서는 합성이 모델 자체 그림으로 축소되는데, 이때 프레임의 빨강이 프레임마다 절반 넘게 무너지면서, 도달 불가능한 분기의 밝아짐 전용 가드는 그걸 보고만 있었습니다.");

        }

        ImGui::SeparatorText("Inspect");

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
        if (ImGui::Button(holding ? "Resume" : holdPending ? "Cancel hold" : "Hold frame"))
        {
            if (holding || holdPending)
                DlssNr::ReleaseInspectionHold();
            else
                DlssNr::RequestInspectionHold();
        }
        ImGui::EndDisabled();
        HelpMarker("D3D12 점검 전용입니다. 다음 성공 프레임의 프록시, 모델 결과, 원본을"
                   ""
                   "함께 고정해 둡니다. 비교와 디버그는 계속 조작할 수 있으며, 모델은"
                   ""
                   "재개(Resume) 전까지 다시 실행되지 않습니다. 게임은 일시정지되지 않습니다."
                   "\n\n모델과 색 보정 변경은 재개 후 적용됩니다. 창 크기를 바꾸면 고정이 풀립니다.\n\n캡처는 먼저 실시간 렌더링을 재개하고, 고정은 캡처가 끝날 때까지 기다립니다.\n\n네이티브 Vulkan이나 실험용 프록시 경로에서는 사용할 수 없습니다.");
        ImGui::SameLine();

        if (capturing)
        {
            ImGui::TextDisabled("Capturing...");
        }
        else if (ImGui::Button("Capture 8 frames"))
        {
            DlssNr::RequestCapture(8);
        }

        HelpMarker("연속된 여덟 프레임을 두 번 기록합니다. 업스케일러가 만든 그대로 한 번,"
                       ""
                       "모델 편집을 적용한 뒤 한 번입니다."
                       ""
                       ""
                       "같은 프레임, 같은 실행, 변수는 하나. 영상 캡처 두 개를 비교하는 것으로는"
                       "\n이런 비교가 절대 불가능합니다. 카메라 경로가 다르고, 사이에 끼인 코덱이\n\n바로 그 문제가 되는 미세한 시간 정보를 버리기 때문입니다.\n\n\nOptiScaler 옆의 dlssnr-capture 폴더에 원본 그대로 저장합니다. 여덟 프레임으로\n\n제한되며, 실행할 때마다 이전 기록을 덮어씁니다.");

        static const char* compareNames[] = { "Off", "Side by side", "Wipe" };
        int compare = (int) config->DlssNrCompare.value_or_default();
        if (ImGui::Combo("Compare", &compare, compareNames, IM_ARRAYSIZE(compareNames)))
            config->DlssNrCompare = (uint32_t) compare;

        HelpMarker("패스 전후를 나란히 보여 줍니다. 전환해 가며 기억할 필요 없이"
                       ""
                       "두 상태를 한 번에 볼 수 있습니다."
                       ""
                       ""
                       "나란히 보기는 각 절반에 프레임 전체를 넣습니다. 왼쪽은 그대로, 오른쪽은"
                       ""
                       "편집된 상태입니다. 두 절반 모두 맞추기 위해 가로로 눌려 있으므로"
                       "\n플레이용이 아니라 확인용입니다.\n\n\n와이프는 분할 지점에서 프레임 하나를 가르고 리샘플링하지 않으므로\n\n화면 비율이 올바르고 평소처럼 플레이할 수 있습니다. 아래에서 분할선을\n\n끌어 조절합니다. 저장되는 설정이므로 메뉴를 닫아도 그대로 유지됩니다.\n\n\n둘 다 메뉴를 열어 두지 않아도 계속 동작합니다. 경계는 가는 선으로 표시됩니다.");

        if (compare != 0)
        {
            bool swap = config->DlssNrCompareSwap.value_or_default();
            if (ImGui::Checkbox("Swap sides", &swap))
                config->DlssNrCompareSwap = swap;

            bool tags = config->DlssNrCompareTags.value_or_default();
            if (ImGui::Checkbox("Label the sides", &tags))
                config->DlssNrCompareTags = tags;

            HelpMarker("어느 쪽이 무엇인지 프레임 자체에 새겨 넣으므로, 스크린샷이 이 컴퓨터를"
                           ""
                           "벗어나고 나서도 구분을 알 수 있습니다. 화면 자체 평면에 그려지므로"
                           ""
                           "와이프에서는 분할선이 이미지를 드러내고 가리는 것과 똑같이 라벨도\n\n드러나고 가려지며, 끌어 옮길 대상은 없습니다. 옆 바꾸기를 하면\n\n라벨이 각자의 이미지와 함께 움직입니다.");

            if (tags)
            {
                float tagScale = config->DlssNrTagScale.value_or_default();
                if (ImGui::SliderFloat("Label size", &tagScale, 0.5f, 5.0f, "%.1fx"))
                    config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
            }

            HelpMarker("편집된 프레임을 반대쪽으로 옮깁니다."
                           ""
                           ""
                           "어느 쪽이 낫다고 판단한 뒤 한 번 해 볼 가치가 있습니다. 눈은 왼쪽과"
                           "\n오른쪽을 공평하게 보지 않아서, 차이가 위치만으로 개선처럼\n\n느껴질 수 있습니다. 자리를 바꾼 뒤에도 같은 쪽이 낫다면 그것은\n\n배치가 아니라 패스 자체의 차이입니다.");
        }

        if (compare == 1)
        {
            float zoom = config->DlssNrCompareZoom.value_or_default();
            if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 2.0f, "%.2f"))
                config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);

            HelpMarker("각 절반이 프레임의 얼마만큼을 보여 줄지 정합니다."
                           ""
                           ""
                           "절반 영역은 프레임의 절반 폭에 높이는 같으므로, 프레임은"
                           ""
                           "비율을 지키면서 이 영역을 채울 수 없습니다.\n\n\n1이면 프레임 전체가 올바른 비율로 표시되고 위아래에 여백이 생깁니다.\n\n2이면 절반 영역을 가득 채우고 대신 양옆을 잘라 냅니다. 그 사이 값은\n\n둘 사이의 절충입니다.");
        }

        if (compare == 2)
        {
            float split = config->DlssNrCompareSplit.value_or_default();
            if (ImGui::SliderFloat("Split", &split, 0.0f, 1.0f, "%.2f"))
                config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);

            HelpMarker("와이프 분할선의 위치입니다. 왼쪽은 업스케일러가 만든 프레임,"
                           "\n오른쪽은 모델이 편집한 프레임입니다.");
        }

        static const char* debugNames[] = { "Off", "Proxy (what the model sees)", "Model output (raw)",
                                            "Difference (amplified)" };
        int debugView = (int) config->DlssNrDebugView.value_or_default();
        if (ImGui::Combo("Debug view", &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
            config->DlssNrDebugView = (uint32_t) debugView;

        HelpMarker("프록시는 모델에 전달되는 이미지입니다. 이것이 이상해 보인다면 흰색"
                       ""
                       "포인트가 틀린 것이고, 그 아래 단계는 아무것도 판단할 수 없습니다."
                       "\n\n차이는 모델이 실제로 바꾼 부분을 20배 확대하고 회색 기준으로\n\n중심을 맞춰 보여 줍니다. 여기가 단색 회색이라면 아무것도 하지 않는다는 뜻입니다.");

        ImGui::PopItemWidth();
    }
}

} // namespace DlssNr

