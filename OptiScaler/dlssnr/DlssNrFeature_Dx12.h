#pragma once

#include <d3d12.h>

#include <array>
#include <optional>
#include <string>

#include <shaders/dlssnr/DlssNr_Common.h>
#include <nvsdk_ngx.h>

// DLSS 5 Neural Rendering, run over the upscaler's output.
//
// Neural Rendering is a post-process, not an upscaler and not a denoiser: it takes a finished frame plus
// depth and motion vectors and synthesises detail. NVIDIA ships no public integration for it, so it is
// driven directly through nvngx_dlssnr.dll as feature 18.
//
// OptiScaler is the right host for it because of one thing it knows that an external hook cannot: which
// NGX evaluate belongs to the upscaler and which to frame generation. Both are handed depth and motion
// vectors, so anything guessing from the parameter block alone attaches to both and runs the model twice
// per rendered frame. Here it is a lookup on the feature handle.
class Config;

namespace DlssNr
{
// The ceiling on how many times the model runs over one frame. The array of extra features, the
// pass-side clamp, the slider's bounds and the slider's own clamp all read this one number.
// What the arrays are sized for, and the ceiling the unlocked slider reaches.
constexpr unsigned int kMaxPasses = 30;

// What the slider offers unless the ceiling is lifted. Cost is exactly linear and the model is nearly
// all of it, so five is already several times the frame budget of the pass at one.
constexpr unsigned int kDefaultMaxPasses = 5;

// Per-pass model settings, sparse: a field with no value follows the global setting. Serialised as
// "2:intensity=0.5,style=1;3:intensity=0.3" -- one-based, so "1" is the first pass.
struct PassTuning
{
    std::optional<float> Intensity;
    std::optional<float> LocalStructure;
    std::optional<float> LocalTone;
    std::optional<float> SkinStructure;
    std::optional<uint32_t> Style;
    std::optional<uint32_t> Preset;
    std::optional<bool> AutoMask;
};

std::array<PassTuning, kMaxPasses> ParsePassOverridesForMenu(const std::string& text);
std::string SerializePassOverrides(const std::array<PassTuning, kMaxPasses>& passes);

// The model runs immediately after the game's upscaler, before the interface is drawn. It is shown a
// display-referred proxy of that frame -- the sort of picture it was trained on -- and its answer is
// composed back over the untouched original.
// Runs the model over Output on the same command list, immediately after the upscaler has written it.
// Called only for upscaler evaluates -- never for frame generation, which is the whole point.
//
// Safe to call every frame; it builds what it needs on first use and disables itself for the session if
// anything fails, rather than retrying into a crash.
// timingQueue is the queue this command list will be executed on, when the caller knows it.
// State::currentCommandQueue only exists once a D3D12 swapchain has been created, which a Vulkan
// game never does -- so without this the pass runs and never reports what it cost.
void EvaluateAfterUpscale(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params,
                          ID3D12CommandQueue* timingQueue = nullptr);

// The same pass, run on the frame the upscaler is about to read rather than on the one it wrote.
//
// Experimental. The model is shown the game's render-resolution colour buffer, so it costs what that
// resolution costs rather than what the display resolution costs, and it sees rendered samples
// instead of the upscaler's reconstruction. Against that: colour arriving here is jittered per frame
// and the model takes no jitter offset, so its history reprojects against an offset it cannot see.
//
// The edit lands on a surface of ours. The caller substitutes it for the upscale and puts the game's
// own buffer back afterwards.
void EvaluateBeforeUpscale(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params,
                           ID3D12CommandQueue* timingQueue = nullptr);

// The surface EvaluateBeforeUpscale wrote, or null when this frame's pass did not run.
ID3D12Resource* PreUpscaleResult();

// The pass as one stage of an upscaler's own pipeline, on two frames the caller already holds.
//
// Everything the model needs beyond the two frames -- depth, motion vectors, the create flags, the
// reset -- still comes from the parameter block, because those are the game's and unchanged by where
// the stage sits. Answers whether the edit reached dest; false leaves dest untouched.
bool EvaluateStage(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params, ID3D12Resource* source,
                   ID3D12Resource* dest, ID3D12CommandQueue* timingQueue = nullptr);

// The surface the stage before this one should write, matched to the frame this one will write.
// Rebuilt when that frame changes size or format. Owned here, so the caller holds a borrowed pointer.
ID3D12Resource* StageInputSurface(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* like);

// Whether the model is being carried by an upscaler's own pipeline: the arrangement is switched on
// and has been seen to work. EvaluateAfterUpscale asks this and declines when it answers yes.
//
// Both halves matter. Asking only the setting made the model silent whenever the split did not apply;
// asking only what happened would keep declining after the setting was turned off.
bool StageCarriesTheModel();

// Frame generation titles tag their UI layer through Streamline; a copy of it makes the HUD mask
// exact at the finished frame. Called at tag time.




// The settings panel, drawn inside OptiScaler's menu.
void RenderMenu(::Config* config, float menuResScale);

// Clears the session failure latch, so a failure caused by transient thrash does not cost a restart.
void RetryAfterFailure();


// Asks the model whether it will work on Direct3D 11 at all, once, and logs the answer.
//
// The bridge exists because of a claim nobody tested: "the model refuses on DX11, it answers
// FeatureNotSupported". Nothing in this project has ever called the snippet's own D3D11 entry points
// -- it exports ten of them, implemented in ngx_d3d11.cpp and sharing CreateFeatureCommon and
// EvaluateFeatureCommon with the D3D12 path. Nothing is created and nothing changes; it resolves the
// entry points and initialises on the game's own device, which is where a refusal would appear.
void ProbeD3D11(void* d3d11Device);

// What scale this game's buffer is on, measured from the untouched copy of each frame.
//
// A suggestion only. Nothing applies it: the menu shows it and the user takes it or does not, which
// keeps the number visible and adjustable rather than a value that moved on its own. Confidence is
// how settled recent readings are -- 1 means they agree, 0 means the scene is changing under the
// measurement and no single value would serve.
struct CalibrationReading
{
    float suggestion = 0.0f;

    // How much recent readings agree. This is steadiness, not correctness: a frozen frame agrees with
    // itself perfectly, so a loading screen scores full marks for a number that means nothing. Read it
    // together with usable.
    float steadiness = 0.0f;

    unsigned long long samples = 0;

    // Whether the scene is worth measuring at all. False when the frame is already tone mapped -- the
    // divisor does nothing there and the reading would be a meaningless 0.9 -- or when too little of
    // the picture is lit to say where the top of the range is. A dark cave gives a small number very
    // steadily, which is the trap this exists to close.
    bool usable = false;
    const char* why = "";
};

CalibrationReading Calibration();

// Whether the model is loaded and running, for the overlay.
bool IsRunning();

// Why it is not, if it is not. Empty while it is running or has not been tried yet.
const char* FailureReason();

// What the game offers by way of exposure. Observed every frame whether or not the setting is on, so
// the menu can say whether turning it on would do anything here.
struct ExposureStatus
{
    unsigned long long seenFrames = 0;   // evaluates observed; 0 means nothing has run yet
    bool offeredNow = false;             // a texture on the most recent frame
    bool everOffered = false;            // a texture on any frame so far
    float exposure = 0.0f;               // last value read back, 0 if none
    float preExposure = 1.0f;
};

ExposureStatus GameExposureStatus();

// The white point the exposure meter has settled on, or 0 if it has not taken a reading yet. For the
// overlay, so the number in use is visible rather than inferred.

// What the pass last cost on the GPU, in milliseconds, or nothing if it has not been measured yet.
std::optional<double> LastGpuTime();

// What the white point meter last settled on, or 0 when it is not running. For the menu.


// Writes a run of consecutive frames, each as the upscaler produced it and again after the model's edit.
// The pair is a control: same frames, same run, one variable.
void RequestCapture(unsigned int frames);
bool CaptureInProgress();

// Session-only D3D12 inspection. Pending takes a matched snapshot on the next successful resolve;
// Held runs only that resolve with live Compare/Debug controls, never NGX with stale guides.
// Capture takes priority and resumes live rendering. Resume invalidates every model history.
enum class InspectionHoldState { Unavailable, Live, Pending, Held };
InspectionHoldState GetInspectionHoldState();
void RequestInspectionHold();
void ReleaseInspectionHold();

void Shutdown();
} // namespace DlssNr
