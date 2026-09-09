#include "pch.h"

#include "MfgUnlock.h"

#include <Config.h>
#include <Util.h>
#include <scanner/scanner.h>
#include <misc/IdentifyGpu.h>

#include <atomic>
#include <map>
#include <mutex>


namespace
{
// mov ebx,1 / mov r8d,3 / cmp edi,0x1b0 / cmovl r8d,ebx. The two counts and the architecture
// constant together are unique in the module; the wildcards cover nothing, they are here only to
// keep the shape readable.
constexpr std::string_view kAdvertisePattern = "BB 01 00 00 00 41 B8 03 00 00 00 81 FF B0 01 00 00 44 0F 4C C3";

// cmp eax,0x1b0 / jl / cmp ebx,3 / jbe. The only comparison against the architecture constant that
// is followed by a signed branch and a count test.
constexpr std::string_view kValidatePattern = "3D B0 01 00 00 7C ? 83 FB 03 76";

// Five generated frames, the count both patched sites carry.
constexpr uint8_t kMaxGeneratedFrames = 5;

// 310.9 restructured both gates. The count is no longer an immediate next to the comparison: the
// Blackwell branch starts at five and reads a configured value, and anything below Blackwell is sent
// to a branch that publishes one.
//     cmp ebp, 0x1b0
//     jl  ada          <- neutralised, so every card takes the Blackwell branch
//     mov edi, 0x5
constexpr std::string_view kAdvertisePattern309 = "81 FD B0 01 00 00 0F 8C ? ? ? ? BF 05 00 00 00";

// The capability flag in the same build is a setae rather than a branch.
//     cmp   eax, 0x1b0
//     setae al
constexpr std::string_view kValidatePattern309 = "3D B0 01 00 00 0F 93 C0";



// scanner::GetAddress only walks sections marked executable. Fatbins are data, so they need their own
// search. Returns 0 unless exactly one non-executable section holds the sequence, once.
uintptr_t FindDataBytes(HMODULE module, const uint8_t* needle, size_t length)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    uintptr_t found = 0;
    size_t hits = 0;

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = section[i];

        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;

        uint8_t* start = base + s.VirtualAddress;
        uint8_t* end = start + s.Misc.VirtualSize;

        for (uint8_t* p = std::search(start, end, needle, needle + length); p != end;
             p = std::search(p + 1, end, needle, needle + length))
        {
            found = reinterpret_cast<uintptr_t>(p);

            if (++hits > 1)
                return 0;
        }
    }

    return hits == 1 ? found : 0;
}

// Aggregate state lives with the patch map below (g_aggregate).

// The module's own file version, for the report. A signature that does not match is expected on a
// version nobody has looked at, and the version is the one thing that makes such a report actionable.
std::string ModuleVersion(HMODULE module)
{
    wchar_t path[MAX_PATH] {};

    if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
        return {};

    version_t file {};
    version_t product {};

    if (!Util::GetFileVersion(path, &file, &product))
        return {};

    return std::format("{}.{}.{}", file.major, file.minor, file.patch);
}

bool WriteBytes(uintptr_t address, const uint8_t* bytes, size_t count)
{
    DWORD oldProtect = 0;

    if (!VirtualProtect((LPVOID) address, count, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LOG_WARN("VirtualProtect failed at {:X}", address);
        return false;
    }

    std::memcpy((void*) address, bytes, count);

    DWORD ignored = 0;
    VirtualProtect((LPVOID) address, count, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID) address, count);

    return true;
}

std::string Hex(const uint8_t* bytes, size_t count)
{
    std::string out;

    for (size_t i = 0; i < count; ++i)
        out += std::format("{}{:02X}", i == 0 ? "" : " ", bytes[i]);

    return out;
}

// Rewrites count and neutralises the architecture clamp, so MultiFrameCountMax is published as five.
bool PatchAdvertise(HMODULE module)
{
    if (const auto at309 = scanner::GetAddress(module, kAdvertisePattern309); at309 != 0)
    {
        // The jl is a rel32, six bytes.
        const auto branchAt = at309 + 6;
        const uint8_t nop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x90 };

        LOG_INFO("MFG unlock: advertise (310.9) at {:X}, jl {} -> {}", at309,
                 Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)));

        return WriteBytes(branchAt, nop, sizeof(nop));
    }

    const auto address = scanner::GetAddress(module, kAdvertisePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the advertise signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the r8d immediate, and the cmovl.
    const auto countAt = address + 7;
    const auto cmovAt = address + 17;

    const uint8_t count[] = { kMaxGeneratedFrames };
    const uint8_t nop[] = { 0x0F, 0x1F, 0x40, 0x00 };

    LOG_INFO("MFG unlock: advertise at {:X}, count {} -> {}, cmovl {} -> {}", address,
             *(const uint8_t*) countAt, kMaxGeneratedFrames, Hex((const uint8_t*) cmovAt, sizeof(nop)),
             Hex(nop, sizeof(nop)));

    return WriteBytes(countAt, count, sizeof(count)) && WriteBytes(cmovAt, nop, sizeof(nop));
}

// Drops the Ada branch and raises the accepted count, so a request for five is not rejected.
bool PatchValidate(HMODULE module)
{
    if (const auto at309 = scanner::GetAddress(module, kValidatePattern309); at309 != 0)
    {
        // setae al -> mov al, 1, so the flag is set whatever the architecture reports.
        const auto setAt = at309 + 5;
        const uint8_t always[] = { 0xB0, 0x01, 0x90 };

        LOG_INFO("MFG unlock: validate (310.9) at {:X}, setae {} -> {}", at309,
                 Hex((const uint8_t*) setAt, sizeof(always)), Hex(always, sizeof(always)));

        return WriteBytes(setAt, always, sizeof(always));
    }

    const auto address = scanner::GetAddress(module, kValidatePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the validate signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the jl, and the immediate of the count test behind it.
    const auto branchAt = address + 5;
    const auto countAt = address + 9;

    const uint8_t nop[] = { 0x90, 0x90 };
    const uint8_t count[] = { kMaxGeneratedFrames };

    LOG_INFO("MFG unlock: validate at {:X}, jl {} -> {}, count {} -> {}", address,
             Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)), *(const uint8_t*) countAt,
             kMaxGeneratedFrames);

    return WriteBytes(branchAt, nop, sizeof(nop)) && WriteBytes(countAt, count, sizeof(count));
}


// Gives Ada the Blackwell kernels the module already carries.
//
// nvngx_dlssg.dll ships two builds of the interpolation kernels. Kernel_EstimateIntermMvecsScatter
// reads three f32 fields of its parameter block on sm_120 and one on sm_89, so on Ada every generated
// frame is placed at the same point between the two real ones: the world does not advance between
// them while the interface, composited once per present, does. At 2X there is one frame and nothing
// to distinguish; above it that is the whole symptom.
//
// The sm_120 module uses no instruction Ada lacks. So per container: the Blackwell PTX image is
// relabelled sm_89, its .target directive is rewritten in place (".target sm_120" and
// ".target sm_89 " are both fourteen bytes, and the directive sits in the literal run at the head of
// the LZ4 stream), and the images that were sm_89 -- the Ada PTX and its SASS -- are relabelled to an
// architecture that does not exist so the driver cannot select them. The driver then JITs Blackwell's
// kernel when it asks for Ada's.
//
// Nothing is copied in and no payload changes length. A container without both images is left alone.
constexpr uint32_t kArchAda = 89;
constexpr uint32_t kArchBlackwell = 120;

// No such shader model. Parks an image where nothing will ask for it.
constexpr uint32_t kArchParked = 122;

// Offsets inside a fatbin image header: payload length, and the architecture the image answers for.
constexpr size_t kImagePayloadSize = 8;
constexpr size_t kImageArch = 28;

unsigned int RewriteBlackwellKernels(HMODULE module)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    const uint8_t magic[] = { 0x50, 0xED, 0x55, 0xBA };
    unsigned int rewritten = 0;

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = section[i];

        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;

        uint8_t* start = base + s.VirtualAddress;
        uint8_t* end = start + s.Misc.VirtualSize;

        for (uint8_t* c = std::search(start, end, magic, magic + sizeof(magic)); c < end;
             c = std::search(c + 1, end, magic, magic + sizeof(magic)))
        {
            if (c + 16 > end)
                break;

            const auto headerSize = *reinterpret_cast<const uint16_t*>(c + 6);
            const auto fatSize = *reinterpret_cast<const uint64_t*>(c + 8);

            if (headerSize != 0x10 || fatSize == 0 || c + 16 + fatSize > end)
                continue;

            uint8_t* blackwell = nullptr;
            size_t blackwellHeader = 0;
            size_t blackwellPayload = 0;
            std::vector<uint8_t*> ada;

            for (uint8_t* image = c + 16; image < c + 16 + fatSize;)
            {
                const auto kind = *reinterpret_cast<const uint16_t*>(image);
                const auto imageHeader = *reinterpret_cast<const uint32_t*>(image + 4);
                const auto payload = *reinterpret_cast<const uint64_t*>(image + kImagePayloadSize);
                const auto arch = *reinterpret_cast<const uint32_t*>(image + kImageArch);

                if (imageHeader == 0 || payload == 0)
                    break;

                // kind 1 is PTX, 2 is a cubin. Only the PTX can be retargeted; the cubin is parked.
                if (kind == 1 && arch == kArchBlackwell)
                {
                    blackwell = image;
                    blackwellHeader = imageHeader;
                    blackwellPayload = payload;
                }
                else if (arch == kArchAda)
                {
                    ada.push_back(image);
                }

                image += imageHeader + payload;
            }

            if (blackwell == nullptr || ada.empty())
                continue;

            const char from[] = ".target sm_120";
            const char to[] = ".target sm_89 ";
            static_assert(sizeof(from) == sizeof(to), "the directive rewrite must not change length");

            uint8_t* body = blackwell + blackwellHeader;
            uint8_t* bodyEnd = body + blackwellPayload;
            auto at = std::search(body, bodyEnd, from, from + sizeof(from) - 1);

            if (at == bodyEnd)
                continue;

            if (!WriteBytes(reinterpret_cast<uintptr_t>(at), reinterpret_cast<const uint8_t*>(to),
                            sizeof(to) - 1))
                continue;

            const uint32_t ada89 = kArchAda;
            const uint32_t parked = kArchParked;

            WriteBytes(reinterpret_cast<uintptr_t>(blackwell + kImageArch),
                       reinterpret_cast<const uint8_t*>(&ada89), sizeof(ada89));

            for (uint8_t* image : ada)
                WriteBytes(reinterpret_cast<uintptr_t>(image + kImageArch),
                           reinterpret_cast<const uint8_t*>(&parked), sizeof(parked));

            ++rewritten;
        }
    }

    LOG_INFO("MFG unlock: {} kernel containers answer Ada with the Blackwell image", rewritten);

    return rewritten;
}
} // namespace

void MfgUnlock::TryApply()
{
    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default())
        return;

    if (auto module = GetModuleHandleW(L"nvngx_dlssg.dll"); module != nullptr)
        TryApply(module);
}

static std::mutex g_patchMutex;
static std::map<HMODULE, MfgUnlock::Status> g_patched; // completed attempts only
static MfgUnlock::Status g_aggregate {};
static std::atomic<unsigned int> g_publishedMax { 0 };
static std::atomic<bool> g_anyModule { false };

static std::wstring ModulePath(HMODULE module)
{
    wchar_t path[MAX_PATH] {};
    return GetModuleFileNameW(module, path, MAX_PATH) != 0 ? std::wstring(path) : std::wstring(L"?");
}

void MfgUnlock::TryApply(HMODULE module)
{
    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() || module == nullptr)
        return;

    {
        std::lock_guard<std::mutex> lock(g_patchMutex);
        if (g_patched.contains(module))
            return; // same handle already processed; concurrent duplicates converge below
    }

    // Everything below runs OUTSIDE the mutex: version/file APIs can pull in system DLLs on
    // first use, and GPU discovery touches NVAPI. Holding our lock across those while a nested
    // loader callback waits on it would invert the lock order (loader lock -> patch mutex here
    // vs patch mutex -> loader lock there). Patch writes themselves are idempotent, so a raced
    // duplicate attempt is redundant work, never corruption; the first finisher wins the insert.
    const auto pathW = ModulePath(module);
    const auto pathA = wstring_to_string(pathW);
    LOG_INFO("MFG unlock: begin module {:p} ({})", (void*) module, pathA);

    LOG_INFO("MFG unlock: reading module version...");
    const std::string version = ModuleVersion(module);
    LOG_INFO("MFG unlock: module version {}", version);

    LOG_INFO("MFG unlock: scanning advertise gate...");
    const bool advertise = PatchAdvertise(module);
    LOG_INFO("MFG unlock: advertise scan complete: {}", advertise);

    LOG_INFO("MFG unlock: scanning validate gate...");
    const bool validate = PatchValidate(module);
    LOG_INFO("MFG unlock: validate scan complete: {}", validate);

    // Default on where it applies: below Blackwell the unlock alone produces frames that do
    // not advance the picture, so the two belong together. dlssCapable is set from the same
    // field, so an architecture that never reported leaves this off.
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    const bool preBlackwell = gpu.vendorId == VendorId::Nvidia &&
                              gpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_TU100 &&
                              gpu.nvidiaArchInfo.architecture_id <= NV_GPU_ARCHITECTURE_AD100;

    unsigned int kernels = 0;
    const bool useBlackwellKernels =
        Config::Instance()->FGDLSSGAdaBlackwellKernels.value_or(preBlackwell);
    LOG_INFO("MFG unlock: AdaBlackwellKernels effective value: {}", useBlackwellKernels);
    if (useBlackwellKernels)
        kernels = RewriteBlackwellKernels(module);

    if (advertise && validate)
        LOG_INFO("MFG unlock: nvngx_dlssg.dll patched for {} generated frames ({})", kMaxGeneratedFrames,
                 pathA);
    else
        LOG_WARN("MFG unlock: nvngx_dlssg.dll incomplete, advertise {}, validate {} ({})", advertise, validate,
                 pathA);

    std::lock_guard<std::mutex> lock(g_patchMutex);
    if (g_patched.contains(module))
        return; // lost the race; the winner's results stand

    Status perModule {};
    perModule.ModuleFound = true;
    perModule.SnippetVersion = version;
    perModule.AdvertiseMatched = advertise;
    perModule.ValidateMatched = validate;
    perModule.KernelsRewritten = kernels;
    g_patched.emplace(module, perModule);

    // Fail closed: the ceiling opens only when at least one copy is seen and EVERY seen copy
    // is fully patched. A complete bin64 copy next to an unpatched DriverStore copy reports 0,
    // because the serving copy cannot be identified and a split success is not a success.
    // Known limitation: unload + address reuse is not tracked; a stale entry fails closed too.
    bool allComplete = !g_patched.empty();
    unsigned int bestKernels = 0;
    unsigned int complete = 0;
    for (const auto& [handle, st] : g_patched)
    {
        const bool whole = st.AdvertiseMatched && st.ValidateMatched;
        allComplete = allComplete && whole;
        if (whole)
            ++complete;
        else if (g_aggregate.UnmatchedVersion.empty() && !st.SnippetVersion.empty())
            g_aggregate.UnmatchedVersion = st.SnippetVersion;
        if (st.KernelsRewritten > bestKernels)
            bestKernels = st.KernelsRewritten;
        if (g_aggregate.SnippetVersion.empty() && !st.SnippetVersion.empty())
            g_aggregate.SnippetVersion = st.SnippetVersion;
    }
    g_aggregate.ModuleFound = true;
    g_aggregate.AdvertiseMatched = allComplete;
    g_aggregate.ValidateMatched = allComplete;
    g_aggregate.KernelsRewritten = bestKernels;
    g_aggregate.CopiesSeen = static_cast<unsigned int>(g_patched.size());
    g_aggregate.CopiesComplete = complete;
    g_anyModule.store(true, std::memory_order_release);
    g_publishedMax.store(allComplete ? kMaxGeneratedFrames : 0, std::memory_order_release);
}

size_t MfgUnlock::PatchedModuleCount()
{
    std::lock_guard<std::mutex> lock(g_patchMutex);
    return g_patched.size();
}

unsigned int MfgUnlock::UnlockedMax() { return g_publishedMax.load(std::memory_order_acquire); }

bool MfgUnlock::AnyModuleSeen() { return g_anyModule.load(std::memory_order_acquire); }

MfgUnlock::Status MfgUnlock::LastStatus()
{
    std::lock_guard<std::mutex> lock(g_patchMutex);
    return g_aggregate; // by value: a coherent snapshot, never a mutable shared reference
}
