#pragma once

// Lock-free record of what the render path was doing when the game died.
//
// The run that started this exists because the log could not answer that. It stopped at 08:25:42.916
// and the fault was at 08:25:43.119 - 203 ms later, in the same frame - and Windows' own report says
// only "unknown module, 0x001afaca". Nothing in the log said what had been entered. Everything the
// render path does per frame is far too much to write to disk, so instead each hook writes one word
// into a ring here, and the crash handler prints the tail of it.
//
// No mutex and no allocation, deliberately: Mark() runs on the render thread between Present and
// EndScene, and the reader runs inside a vectored exception handler where a lock could be held by the
// very thread that faulted. A diagnostic that can deadlock the crash is worse than no diagnostic.
//
// Header-only on purpose - it is included by both Render.cpp and CrashLog.cpp and adds no source file
// to the build.

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdio>

namespace diag
{
    // Every point worth naming. The numbers are written into the crash report, so they are appended
    // only and never reused: a ring that held an old meaning would make a report unreadable.
    enum Event : unsigned
    {
        kNone = 0,
        // scene lifecycle
        kDeviceCreated,
        kBeginSceneEnter,
        kBeginSceneExit,
        kEndSceneEnter,
        kEndSceneExit,
        kClearEnter,
        kClearExit,
        kPresentEnter,
        kDepthStencilLogged,   // the last thing the log itself printed before the crash
        kCheckSurfaceBackBuffer,
        kCheckSurfaceDepth,
        kCheckTexture,
        kPresentMcInWorld,
        kStateBlockCapture,
        kSetViewport,
        kOurBeginScene,
        kDrawWorldEnter2,
        kDrawOverlayEnter2,
        kOurEndScene,
        kApplyStateBlock,
        kPresentStatusLog,
        kPresentExit,
        kResetEnter,
        kResetExit,
        // matrix callbacks
        kSetTransformEnter,
        kSetTransformProjection,
        kSetTransformView,
        kSetTransformExit,
        kSetVsConstantEnter,
        kSetVsConstantFixed,
        kSetVsConstantExit,
        kSetRenderStateEnter,
        kSetRenderStateExit,
        // custom draw
        kDrawWorldEnter,
        kDrawWorldExit,
        kDrawOverlayEnter,
        kDrawOverlayExit,
        // state the crash report quotes
        kCameraValid,
        kCameraInvalid,
        kWorldReady,
    };

    inline const char* Name(unsigned event)
    {
        switch (event) {
        case kNone: return "NONE";
        case kDeviceCreated: return "DEVICE_CREATED";
        case kBeginSceneEnter: return "BEGINSCENE_ENTER";
        case kBeginSceneExit: return "BEGINSCENE_EXIT";
        case kEndSceneEnter: return "ENDSCENE_ENTER";
        case kEndSceneExit: return "ENDSCENE_EXIT";
        case kClearEnter: return "CLEAR_ENTER";
        case kClearExit: return "CLEAR_EXIT";
        case kPresentEnter: return "PRESENT_ENTER";
        case kDepthStencilLogged: return "DEPTH_STENCIL_LOGGED";
        case kCheckSurfaceBackBuffer: return "CHECK_SURFACE_BACKBUFFER";
        case kCheckSurfaceDepth: return "CHECK_SURFACE_DEPTH";
        case kCheckTexture: return "CHECK_TEXTURE";
        case kPresentMcInWorld: return "PRESENT_MC_IN_WORLD";
        case kStateBlockCapture: return "STATE_BLOCK_CAPTURE";
        case kSetViewport: return "SET_VIEWPORT_FULL";
        case kOurBeginScene: return "OUR_BEGIN_SCENE";
        case kDrawWorldEnter2: return "OUR_DRAW_WORLD";
        case kDrawOverlayEnter2: return "OUR_DRAW_OVERLAY";
        case kOurEndScene: return "OUR_END_SCENE";
        case kApplyStateBlock: return "APPLY_STATE_BLOCK";
        case kPresentStatusLog: return "PRESENT_STATUS_LOG";
        case kPresentExit: return "PRESENT_EXIT";
        case kResetEnter: return "RESET_ENTER";
        case kResetExit: return "RESET_EXIT";
        case kSetTransformEnter: return "SETTRANSFORM_ENTER";
        case kSetTransformProjection: return "SETTRANSFORM_PROJECTION";
        case kSetTransformView: return "SETTRANSFORM_VIEW";
        case kSetTransformExit: return "SETTRANSFORM_EXIT";
        case kSetVsConstantEnter: return "SETVS_ENTER";
        case kSetVsConstantFixed: return "SETVS_FIXED";
        case kSetVsConstantExit: return "SETVS_EXIT";
        case kSetRenderStateEnter: return "SETRENDERSTATE_ENTER";
        case kSetRenderStateExit: return "SETRENDERSTATE_EXIT";
        case kDrawWorldEnter: return "DRAW_WORLD_ENTER";
        case kDrawWorldExit: return "DRAW_WORLD_EXIT";
        case kDrawOverlayEnter: return "DRAW_OVERLAY_ENTER";
        case kDrawOverlayExit: return "DRAW_OVERLAY_EXIT";
        case kCameraValid: return "CAMERA_VALID";
        case kCameraInvalid: return "CAMERA_INVALID";
        case kWorldReady: return "WORLD_READY";
        default: return "?";
        }
    }

    inline constexpr int kRing = 512;

    struct Record
    {
        unsigned       event;
        DWORD          threadId;
        DWORD          tick;
        unsigned long long seq;  // absolute, so a wrapped ring still reads in order
    };

    // One slot per writer position. Only the writer that owns the slot touches it, and the reader may
    // see a torn record if it reads one the writer is in the middle of - so seq is written last and
    // the reader drops any record whose seq is not the one it expects. That is the whole trick: no
    // lock, and a stale or half-written word reads as "skip this one" rather than as a wrong answer.
    inline std::atomic<unsigned>       g_head{ 0 };
    inline std::atomic<unsigned long long> g_seq{ 0 };
    inline Record                      g_ring[kRing]{};

    inline void Mark(unsigned event)
    {
        const unsigned long long      seq = g_seq.fetch_add(1, std::memory_order_relaxed) + 1;
        const unsigned                slot = static_cast<unsigned>(seq % kRing);
        Record&                       r = g_ring[slot];
        r.event                       = event;
        r.threadId                    = GetCurrentThreadId();
        r.tick                        = GetTickCount();
        r.seq                         = seq;  // published last: the reader treats a record as valid only
                                           // once this word is the one it expects
    }

    // Copies the newest `max` records into `out`, oldest first. Returns how many were valid. Safe to
    // call from a fault: no allocation, no lock, no syscall that can block.
    inline int Snapshot(Record* out, int max)
    {
        const unsigned long long newest = g_seq.load(std::memory_order_relaxed);
        const int count = (newest < static_cast<unsigned long long>(kRing)) ? static_cast<int>(newest) : kRing;
        const int take = (count < max) ? count : max;
        for (int i = 0; i < take; ++i) {
            const unsigned long long want = newest - static_cast<unsigned long long>(take) + 1 + static_cast<unsigned long long>(i);
            const unsigned                slot = static_cast<unsigned>(want % kRing);
            const Record&                 r = g_ring[slot];
            if (r.seq == want)  // else the writer is mid-write: skip rather than report half of it
                out[i] = r;
            else
                out[i] = Record{ kNone, 0, 0, want };
        }
        return take;
    }

    // The single most recent event, or kNone if the ring has never been written. This is the line the
    // crash report leads with: it names the callback the game was inside.
    //
    // Defined in terms of Snapshot rather than by indexing the ring again. The first version did index
    // it directly and the two disagreed on a real crash - Last() said RESET_EXIT while the snapshot's
    // window ended with SETRENDERSTATE_EXIT - so a report could name a callback that was not the last
    // one. Two readers of one window cannot disagree.
    inline Record Last()
    {
        Record  one[1]{};
        const int n = Snapshot(one, 1);
        if (n < 1)
            return Record{};
        return one[0];
    }

    // True when `address` is a plausible function pointer: mapped and executable. Checked on every
    // install, because a vtable slot that is null or points at data is how a detour ends up in front
    // of something that was never the function it replaced.
    inline bool IsExecutable(const void* address)
    {
        if (!address)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            return false;
        return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    // Which module owns `address`, asked of Windows rather than worked out from VirtualQuery.
    //
    // Worked out is wrong: VirtualQuery's AllocationBase is the base of one committed region, and a PE
    // image's sections are committed inside the loader's single reservation, so the two do not reliably
    // agree - the first version of this check compared them and reported all eight D3D8 slots as "not
    // inside d3d8.dll", which would have read as eight wrong vtable indices and none of them were.
    // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS is the query that answers the question directly, and
    // UNCHANGED_REFCOUNT keeps it from pinning a module for the rest of the session.
    inline HMODULE ModuleOf(const void* address)
    {
        if (!address)
            return nullptr;
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(reinterpret_cast<ULONG_PTR>(const_cast<void*>(address))), &module))
            return nullptr;
        return module;
    }

    // "maxpayne2.exe+0x001b70", for the log. Writes into a caller-supplied buffer: the crash logger
    // cannot allocate, and this is shared with it.
    inline void DescribeAddress(const void* address, char* out, std::size_t outSize)
    {
        if (!out || !outSize)
            return;
        out[0] = '\0';
        if (!address)
            return;
        HMODULE module = ModuleOf(address);
        wchar_t  path[MAX_PATH]{};
        if (!module || !GetModuleFileNameW(module, path, MAX_PATH)) {
            std::snprintf(out, outSize, "%p", address);
            return;
        }
        const wchar_t* name = path;
        for (std::size_t i = MAX_PATH; i > 0; --i) {
            if (path[i - 1] == L'\\' || path[i - 1] == L'/') {
                name = path + i;
                break;
            }
        }
        const auto offset = static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(const_cast<void*>(address)) -
                                               reinterpret_cast<ULONG_PTR>(module));
        std::snprintf(out, outSize, "%ls+0x%08lx", name, offset);
    }
}