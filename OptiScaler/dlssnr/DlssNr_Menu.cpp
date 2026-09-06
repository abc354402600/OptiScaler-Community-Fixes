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

        HelpMarker("Synthesises detail in the upscaler's output, before frame generation sees it."
                       "\n\nNeeds two similarly named files beside OptiScaler, one character apart:"
                       "\n  nvngx_dlssnr.dll       NVIDIA's model (~165 MB) -- you supply it"
                       "\n  nvngx.dll_dlssnr.dll   the forwarder (~13 KB) -- ships in this package"
                       "\nUndocumented and driven directly, so none of this is officially supported.");

        // The toggle can be bound to a key, and nobody would think to look for it under Keybinds
        // unless told. Dimmed, because it is a note rather than a setting.
        ImGui::TextDisabled("Can be toggled with a key -- bind it under Keybinds, \"Neural Rendering\".");

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
            ImGui::TextDisabled("Off. The model stays loaded, so turning this back on is immediate.");
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
                ImGui::TextDisabled("Pick an upscaler marked w/Dx12 above, then restart the game.");
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
                        "What each pass is told, where it should differ from the values above."
                        "\n\nA control left on \"global\" follows the setting above it, so a pass you"
                        "\nhave not touched behaves exactly as it did before this existed."
                        "\n\nThe passes compound: a later pass sees what the one before it produced."
                        "\nEasing intensity down the chain keeps the last passes refining rather than"
                        "\nre-amplifying what is already there.");

                    ImGui::TreePop();
                }
            }

            HelpMarker("How many times the model runs over the frame, each pass shown the last one's"
                       "\nanswer."
                       "\n\nThe most expensive control here. Cost is exactly linear: five passes is"
                       "\nfive model runs, and the model is nearly all of what this pass costs."
                       "\n\nWhat it buys that nothing else can is the model re-deciding where detail"
                       "\ngoes, what hue it is, and how saturated. Detail strength amplifies the map"
                       "\nthe first pass drew; it cannot redraw it."
                       "\n\nWhat it does not buy is raw magnitude. Detail strength and Intensity are"
                       "\nfree and do that. Try both, and raise Model resolution, before this."
                       "\n\nPast 3, raise the ratio guard under Colour with it. The passes compound"
                       "\nthe luminance ratio and the guard clamps it, so beyond its limit the extra"
                       "\nruns are paid for and thrown away."
                       "\n\nEach pass is its own model, with its own memory, built on a frame of its"
                       "\nown -- so raising this takes a couple of seconds to arrive, and VRAM grows"
                       "\nwith it."
                       "\n\nNo effect on native Vulkan, or with the proxy path switched on.");
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

        HelpMarker("The model's raster as a percentage of the frame, from 25% to 200%."
                       "\nThe original frame stays at full detail; only the model's work is resampled."
                       "\n\nBelow 100%, the model works on a smaller picture and its answer is enlarged."
                       "\nHalf resolution is a quarter of the model pixels, but fine detail softens."
                       "\n\nAbove 100%, the model works on a larger raster and its answer returns to"
                       "\nframe size. 200% is twice each axis, four times the model pixels and higher"
                       "\nVRAM cost; it does not add game geometry samples.");

        {
            bool dual = config->DlssNrDualFeature.value_or_default();

            if (ImGui::Checkbox("Run inside the upscaler", &dual))
                config->DlssNrDualFeature = dual;

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), "(experimental, restart)");

            HelpMarker("Splits the upscaler in two and puts the model between the halves. The upscaler"
                       "\nwrites at render resolution, the model runs on that, and the enlargement to"
                       "\ndisplay resolution happens afterwards."
                       "\n\nWith ray reconstruction this makes the first half a denoiser and nothing else,"
                       "\nwhich is the arrangement worth having: the frame the model sees is clean, and it"
                       "\nis a quarter of the pixels at Performance."
                       "\n\nUnlike 'Run before the upscaler', the frame here has already been through"
                       "\ntemporal accumulation, so the subpixel jitter the model cannot be told about is"
                       "\nresolved before it sees anything."
                       "\n\nThe enlargement is the output scaler, not the upscaler's own. Takes effect when"
                       "\nthe upscaler is next built, so restart or change quality after ticking it."
                       "\n\nDoes nothing when render resolution already equals display resolution -- at"
                       "\nDLAA there is no smaller frame to run on.");

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

                HelpMarker("What enlarges the frame once the model has edited it."
                           "\n\nSpatial needs no motion vectors, no depth and no jitter, so it cannot be"
                           "\nwrong about any of them -- and it is the softest, having nothing temporal to"
                           "\nwork from."
                           "\n\nThe upscalers are sharper and use the game's own per-frame data. An"
                           "\nupscaler this machine cannot run is replaced with one it can, the same way"
                           "\nthe main upscaler list behaves."
                           "\n\nTakes effect when the upscaler is next built.");
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

            HelpMarker("Shows the model the frame the upscaler is about to read, instead of the one it"
                       "\nwrote. The model runs at render resolution, so at Performance it costs about a"
                       "\nquarter of what it costs after the upscaler, and it sees rendered pixels rather"
                       "\nthan reconstructed ones."
                       "\n\nUnlike Model resolution this does not soften what the model returns: the model"
                       "\nruns 1:1 on a smaller frame rather than small on a large one, and the upscaler"
                       "\nenlarges its work along with everything else."
                       "\n\nUntested territory. Colour at this point is jittered by a different subpixel"
                       "\noffset every frame and the model is given no way to know that, so its history"
                       "\nmay reproject against an offset it cannot see. Look for shimmer and swimming on"
                       "\nfine detail while the camera moves.");
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

            HelpMarker("How the model's work is brought back up when it ran below the frame's size."
                       "\n\nClassic composes the model's small picture directly against the full-size"
                       "\nframe. Those two disagree by the shrink's blur as well as by the model's edit,"
                       "\nand the composition cannot tell them apart -- it reads the blur as brightness"
                       "\nthe frame has and the model never saw. The lower the model resolution the"
                       "\nlarger that error, and it is the colour shift that shows up at 50%."
                       "\n\nMatched residual carries up only the model's difference and lays it on the"
                       "\nframe's own proxy, so both pictures being compared are full size and the only"
                       "\nthing that came from the small raster is the edit itself."
                       "\n\nNo effect at 100%: there is no residual to carry and the two are identical."
                       "\n\nFrom hhkbble's multi-pass work on this fork.");
        }

        ImGui::SeparatorText("How much of it lands");

        float transfer = config->DlssNrTransferStrength.value_or_default();
        if (ImGui::SliderFloat("Detail strength", &transfer, 0.0f, 2.0f, "%.2f"))
            config->DlssNrTransferStrength = transfer;

        HelpMarker("How far the frame moves toward the model's picture."
                       "\n\nThe model's answer is not added to the frame -- it is a complete picture of its"
                       "\nown, rescaled so its luminance sits where the original says it should. This"
                       "\nblends between the two, so both ends are real pictures and everything between"
                       "\nthem is one too."
                       "\n\n0 gives back exactly what the upscaler produced. 1 is the model's picture."
                       "\n\nAbove 1 carries on past it in the same direction, which is not something the"
                       "\nmodel asked for -- use it to see what it is doing, then come back down. This"
                       "\nis the control to push if you want more effect: Intensity belongs to the model"
                       "\nand it decides what to do with it.");

        float colour = config->DlssNrColourStrength.value_or_default();
        if (ImGui::SliderFloat("Colour strength", &colour, 0.0f, 1.0f, "%.2f"))
            config->DlssNrColourStrength = colour;

        HelpMarker("Whether the model's colour arrives with its light."
                       "\n\n0 keeps the game's own hue exactly -- every pixel is the original colour with"
                       "\nonly its brightness carrying the model's verdict. Game-accurate colour, with"
                       "\nthe detail. 1 brings the model's colour as well, in its own hue, clamped into"
                       "\nAP1 so nothing unreachable is asked for."
                       "\n\nThis cannot shift hue on its own: it interpolates between two finished"
                       "\npictures rather than adding a colour difference to one, which is what used to"
                       "\nlet a warm subject come back green.");

        ImGui::SeparatorText("Model");

        ImGui::TextUnformatted("Read when the model is built, so a change rebuilds it after a moment.");

        static const char* nrPresetNames[] = { "Default", "Preset 1", "Preset 2", "Preset 3" };
        int preset = (int) config->DlssNrPreset.value_or_default();
        if (ImGui::Combo("Model preset", &preset, nrPresetNames, IM_ARRAYSIZE(nrPresetNames)))
            config->DlssNrPreset = (uint32_t) preset;

        HelpMarker("Default leaves the choice to the model."
                       "\n\nNot the same scale as the super resolution or ray reconstruction presets --"
                       "\nthe same number means something different here.");

        static const char* nrStyleNames[] = { "Default (standard)", "Natural", "Cinematic" };
        int style = (int) config->DlssNrStyle.value_or_default();

        if (style > 2)
            style = 2;

        if (ImGui::Combo("Style", &style, nrStyleNames, IM_ARRAYSIZE(nrStyleNames)))
            config->DlssNrStyle = (uint32_t) style;

        HelpMarker("The model's own processing profiles."
                   "\n\nDefault (standard): the strongest. Boosts local contrast and deepens"
                   "\nlighting, and can oversaturate or look stylised -- most of what reads as"
                   "\n'the model changed my game's look' is this profile."
                   "\n\nNatural: the same detail work with a gentler hand. Keeps skin tones and"
                   "\ntonal balance closer to what the game rendered."
                   "\n\nCinematic: tones down the shine and over-processing for a film-like look."
                   "\n\nRead when the model is built, so a change rebuilds it after a moment. The"
                   "\nnames come from community testing; NVIDIA ships no names in the binaries.");

        DeferredSlider("Intensity", &config->DlssNrIntensity, 0.0f, 2.0f);

        HelpMarker("The model's own strength control, applied inside it. Distinct from detail"
                       "\nstrength above, which scales the result afterwards.");

        DeferredSlider("Local structure", &config->DlssNrLocalStructure, 0.0f, 2.0f);

        DeferredSlider("Local tone", &config->DlssNrLocalTone, 0.0f, 2.0f);


        DeferredSlider("Skin structure", &config->DlssNrSkinStructure, -1.0f, 2.0f);

        HelpMarker("-1 means follow local structure, and is the model's own default -- it is not a"
                       "\nstrength of zero. 0 and above set skin independently of the rest of the frame.");

        bool autoMask = config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox("Auto skin mask", &autoMask))
            config->DlssNrAutoMask = autoMask;

        HelpMarker("Lets the model find skin itself rather than treating the frame uniformly.");

        ImGui::SeparatorText("Colour");

        ImGui::TextDisabled("The model was trained on finished, sRGB-encoded frames. The upscaler's\n"
                            "output is not one: it is linear and open-ended. These decide how it is\n"
                            "mapped into something the model recognises. A frame the game reports as\n"
                            "already tone-mapped is passed over untouched and none of this applies.");

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

            HelpMarker("Where the number that divides the frame comes from."
                           "\n\nPaper white only -- the slider below and nothing else. Right for a"
                           "\ngame whose exposure never moves, wrong the moment it does: one"
                           "\nconstant cannot serve a cave and a field."
                           "\n\nThe game's own exposure -- read from the texture the game hands"
                           "\nthe upscaler. The best source there is, because it is decided"
                           "\nupstream and nothing this pass does can move it. Not every game"
                           "\nsupplies one."
                           "\n\nA buffer the scan found -- for games that compute an exposure and"
                           "\nnever pass it on. A GUESS: candidates are matched by shape, and in"
                           "\nGTA V the best one tracks the real exposure but at its own scale,"
                           "\nwhich the anchor's ratio cancels. Needs anchoring once, and checking"
                           "\nafterwards.");

            // Availability, in colour, for the option currently chosen.
            if (source == 1)
            {
                if (!vk && ex.seenFrames == 0)
                    ImGui::TextDisabled("Waiting for a frame...");
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
                    ImGui::TextDisabled("Reading the exposure...");
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

            HelpMarker("The white point for the selected calibration point, or -- with no row"
                           "\nselected -- the value the next Anchor press captures."
                           "\n\nSet it until the picture looks right here, then Anchor. Move to very"
                           "\ndifferent light and do it again: two points fix the buffer's real"
                           "\nrelationship and the white point holds between them. Click a row below"
                           "\nto come back and adjust that point; click it again to let go.");

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

            HelpMarker("A multiplier on the exposure the game supplied. 1.00x takes its number"
                           "\nexactly, and that is the right answer here."
                           "\n\nThis is not a fudge factor. If a game needs the trim far from 1 to look"
                           "\nright, that is evidence the exposure being read is wrong for that game,"
                           "\nnot that the game wants trimming. Somewhere around 0.8 to 1.25 is honest"
                           "\ntuning; reaching for 4 means something upstream is broken and the trim is"
                           "\nhiding it."
                           "\n\nYour manual paper white is kept separately and comes back untouched if"
                           "\nyou switch the option above off.");
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

        HelpMarker("What the frame is divided by before the model sees it. There is no other white"
                       "\npoint; this is the whole of it."
                       "\n\nThe model was trained on finished frames where white sits at 1. The"
                       "\nupscaler's output is linear and open-ended, so something has to say where"
                       "\nwhite is -- and where the game's DLSS buffer is linear HDR, that number is"
                       "\nrarely anywhere near 1. Measured in Monster Hunter Wilds it takes 16 or more"
                       "\nbefore the model's detail reaches the frame at all, and the value that suits"
                       "\na shaded camp is still too small for the same game out in daylight."
                       "\n\nToo low and almost every pixel trips the soft knee: the model is shown a"
                       "\nflat near-white picture, its answer is scaled away, and only its hue"
                       "\nsurvives -- which reads as a colour cast rather than as lost detail. Too"
                       "\nhigh and it is shown an underexposed one, its answer degrades, and this same"
                       "\nnumber multiplies that error on the way out."
                       "\n\nRaise it until the picture stops improving. Past that point it does not"
                       "\nplateau, it gets worse in the other direction."
                       "\n\nThis was once a multiplier on a measured white point. The measurement is"
                       "\ngone: it read scene brightness rather than where white belongs, handed the"
                       "\nmodel a picture three times too dark, and left the highlight path nothing to"
                       "\ngive back."
                       "\n\nAt strength zero the frame is still bit-identical whatever this says.");
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

                HelpMarker("A lamp in the corner: red for dark, green for full light, and the"
                               "\nshades between, with the reading beside it."
                               "\n\nIt is how you see at a glance that the scan is TRACKING rather"
                               "\nthan merely running. Walk into shade and it should slide toward"
                               "\nred; step out and it should go green. If it moves the wrong way,"
                               "\nthat is what the setting above is for."
                               "\n\nPurely a readout. It changes nothing.");

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

                HelpMarker("Set the paper white above until the picture looks right, then press this."
                               "\n\nThe first press calibrates one point -- the white point then"
                               "\nfollows the scan by ratio from there, as before. Walk into very"
                               "\ndifferent light, set paper white again, and press it again: the"
                               "\nsecond point pins down the buffer's real curve and everything"
                               "\nbetween the two is right, not just near one anchor. Up to eight."
                               "\n\nThe table is per game and shareable: one person calibrates a game"
                               "\nand the numbers are the same for everyone who takes the profile.");

                if (!isSource)
                    ImGui::TextDisabled("(the scan is only watching -- the white point above comes "
                                        "from somewhere else)");

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

                    ImGui::TextDisabled("Click a row to edit it with the slider above; click it again"
                                        " to control the live point. > is the point in use now.");
                }

                // The direction flag only means anything with a single point; with two or more the
                // direction the white point moves is already fixed by the data.
                if (anchors.size() == 1)
                {
                    bool inverted = config->DlssNrScanInverted.value_or_default();
                    if (ImGui::Checkbox("The number runs the other way", &inverted))
                        config->DlssNrScanInverted = inverted;

                    HelpMarker("Flip this if the picture gets worse in the direction it should be"
                                   "\ngetting better. Most engines store an exposure that falls as"
                                   "\nthe scene brightens; some store its reciprocal, and a buffer"
                                   "\nfound by shape does not say which. Add a second anchor point in"
                                   "\ndifferent light and this is decided for you, so it disappears.");
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
                                                      : "nothing matched yet.");
                    }
                    else
                    {
                        for (size_t i = 0; i < found.size(); ++i)
                        {
                            const auto& c = found[i];

                            if (c.reads == 0)
                            {
                                ImGui::TextDisabled("%zu. %s -- not read yet", i + 1, c.shape.c_str());
                                continue;
                            }

                            // Moving is the whole signal, so it is the thing that is coloured.
                            ImGui::TextColored(c.moves ? ImVec4(0.45f, 0.8f, 0.45f, 1.0f)
                                                       : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                               "%zu. %s = %.5f  (seen %.5f..%.5f) %s", i + 1,
                                               c.shape.c_str(), c.latest, c.lowest, c.highest,
                                               c.moves ? "MOVES" : "flat so far");
                        }

                        ImGui::TextDisabled("Walk from shade into daylight. A real exposure moves.");
                        ImGui::TextDisabled("One that only ever climbs is a counter, not an exposure.");
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

        HelpMarker("The most the pass may move any pixel, as a multiple of what it already was --"
                       "\nin both directions. A pixel may not be brightened past this, nor darkened"
                       "\npast its reciprocal."
                       "\n\nLights are where the model has least to say and where rescaling its answer"
                       "\ninto the frame does the most damage: an early version turned every strip light"
                       "\nin the scene into a string of coloured cells. 2x leaves detail intact while"
                       "\nmaking that failure impossible. Raise it only if bright areas look clipped."
                       "\n\nThe guard is applied once, to the finished composition, while Passes"
                       "\ncompounds the ratio it bounds. A value near the pass count keeps the headroom"
                       "\neach pass gets roughly constant -- 1 pass at 1x, 2 at 2x, 3 at 3x. Left where"
                       "\nit is, the third pass spends most of its contribution against the clamp."
                       "\n\nDarkening was once left uncapped, and the guard itself only bound the"
                       "\ncolour-strength-zero end of the blend -- so at the default strength it bound"
                       "\nnothing at all. Nioh 3 is why both are fixed: in a scene dark enough that the"
                       "\nsoft knee never fires, the composition reduces to the model's own picture,"
                       "\nand it collapsed the frame's red by more than half, once per frame, while an"
                       "\nupward-only guard on an unreachable branch watched it happen.");

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
        HelpMarker("D3D12 inspection only: keeps the next successful frame's proxy, model answer"
                   "\nand original together. Compare and Debug remain interactive; the model does"
                   "\nnot run again until Resume. This does not pause the game."
                   "\n\nModel and colour tuning apply after Resume. A resize releases the hold."
                   "\nCapture resumes live rendering first; Hold waits until capture has finished."
                   "\nUnavailable on native Vulkan or the experimental proxy path.");
        ImGui::SameLine();

        if (capturing)
        {
            ImGui::TextDisabled("Capturing...");
        }
        else if (ImGui::Button("Capture 8 frames"))
        {
            DlssNr::RequestCapture(8);
        }

        HelpMarker("Writes eight consecutive frames twice: as the upscaler produced them, and again"
                       "\nonce the model's edit was applied."
                       "\n\nSame frames, same run, one variable -- which is what comparing two video"
                       "\ncaptures can never be, since they have different camera paths and a codec in"
                       "\nbetween that discards exactly the fine temporal detail in question."
                       "\n\nRaw, into a dlssnr-capture folder beside OptiScaler. Bounded to eight frames,"
                       "\nand each run overwrites the last.");

        static const char* compareNames[] = { "Off", "Side by side", "Wipe" };
        int compare = (int) config->DlssNrCompare.value_or_default();
        if (ImGui::Combo("Compare", &compare, compareNames, IM_ARRAYSIZE(compareNames)))
            config->DlssNrCompare = (uint32_t) compare;

        HelpMarker("Shows the pass against itself, so the two can be seen at once rather than"
                       "\ntoggled and remembered."
                       "\n\nSide by side puts the whole frame in each half, untouched on the left and"
                       "\nedited on the right. Both halves are squeezed horizontally to fit, so it is"
                       "\nfor looking at rather than playing in."
                       "\n\nWipe cuts a single frame at the split and resamples nothing, so the picture"
                       "\nis the right shape and can be played normally. Drag the split below; it is a"
                       "\nstored setting and stays put once the menu is closed."
                       "\n\nNeither needs the menu open to keep working. A hairline marks the join.");

        if (compare != 0)
        {
            bool swap = config->DlssNrCompareSwap.value_or_default();
            if (ImGui::Checkbox("Swap sides", &swap))
                config->DlssNrCompareSwap = swap;

            bool tags = config->DlssNrCompareTags.value_or_default();
            if (ImGui::Checkbox("Label the sides", &tags))
                config->DlssNrCompareTags = tags;

            HelpMarker("Writes which side is which onto the frame itself, so a screenshot still"
                           "\nsays so after it has left this machine. Drawn into the picture's own"
                           "\nplane: in the wipe the split reveals and hides the label exactly as it"
                           "\ndoes the images, and there is nothing to drag. Swap sides moves the"
                           "\nlabels with their pictures.");

            if (tags)
            {
                float tagScale = config->DlssNrTagScale.value_or_default();
                if (ImGui::SliderFloat("Label size", &tagScale, 0.5f, 5.0f, "%.1fx"))
                    config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
            }

            HelpMarker("Puts the edited frame on the other side."
                           "\n\nWorth doing once you have decided which you prefer: the eye is not"
                           "\neven-handed about left and right, and a difference can read as an"
                           "\nimprovement purely from where it sits. If the same side still wins after"
                           "\nswapping, it is the pass you are seeing and not the placement.");
        }

        if (compare == 1)
        {
            float zoom = config->DlssNrCompareZoom.value_or_default();
            if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 2.0f, "%.2f"))
                config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);

            HelpMarker("How much of the frame each half shows."
                           "\n\nA half is half as wide as the frame and just as tall, so the frame"
                           "\ncannot fill it and keep its shape."
                           "\n\nAt 1 the whole frame is there at its right proportions, with bars above"
                           "\nand below. At 2 the half is filled and the sides are cropped away"
                           "\ninstead. Anything between trades one for the other.");
        }

        if (compare == 2)
        {
            float split = config->DlssNrCompareSplit.value_or_default();
            if (ImGui::SliderFloat("Split", &split, 0.0f, 1.0f, "%.2f"))
                config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);

            HelpMarker("Where the wipe cuts. Left of it is the frame as the upscaler produced it,"
                           "\nright of it is the frame the model edited.");
        }

        static const char* debugNames[] = { "Off", "Proxy (what the model sees)", "Model output (raw)",
                                            "Difference (amplified)" };
        int debugView = (int) config->DlssNrDebugView.value_or_default();
        if (ImGui::Combo("Debug view", &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
            config->DlssNrDebugView = (uint32_t) debugView;

        HelpMarker("Proxy is the picture handed to the model -- if that looks wrong, the white point"
                       "\nis wrong and nothing downstream can be judged."
                       "\n\nDifference shows what the model actually changed, amplified twenty times and"
                       "\ncentred on grey. A flat grey frame there means it is doing nothing.");

        ImGui::PopItemWidth();
    }
}

} // namespace DlssNr

