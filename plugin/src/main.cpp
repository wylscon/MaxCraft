#include "Camera.h"
#include "Characters.h"
#include "Collision.h"
#include "CrashLog.h"
#include "Config.h"
#include "Engine.h"
#include "Hook.h"
#include "Input.h"
#include "Launcher.h"
#include "Link.h"
#include "Log.h"
#include "Render.h"
#include "Runtime.h"

#include <MinHook.h>
#include <Windows.h>

// plugin/CMakeLists.txt has the one copy of this and passes it in; the fallback keeps a plain
// compiler command honest. The log's first line is how the user tells which build is installed.
#ifndef MCRAFT_VERSION
#define MCRAFT_VERSION "0.1.1"
#endif

// The commit the binary was built from, so a log always names the code it came from. CMake passes it
// (CMakeLists.txt reads it out of git); a plain compiler command keeps a literal fallback rather than
// failing, the same way MCRAFT_VERSION above does.
#ifndef MCRAFT_GIT_COMMIT
#define MCRAFT_GIT_COMMIT "unknown"
#endif

namespace
{
    // Defined below, after Startup, which starts it. Declared here so Startup can name it.
    DWORD WINAPI MinecraftWatch(LPVOID);

    DWORD WINAPI Startup(LPVOID)
    {
        mclog::Init();
        crashlog::Install();
        mclog::Info("MaxCraft {} loaded (protocol v{}, built {} {})", MCRAFT_VERSION, proto::kVersion, __DATE__, __TIME__);
        // Which binary is this, in the log, before anything else can go wrong.
        //
        // Two builds of this mod differ by a couple of kilobytes and a diagnostic, and an A/B run is
        // worthless if the .asi in the game folder is not the one the result is about. That has already
        // happened once: a download turned out to be the previous run's build, and the only reason it
        // showed up was a size difference. A commit and a size in the first three lines end that class
        // of mistake. MCRAFT_GIT_COMMIT is passed by the build; the fallback keeps a plain compiler
        // command working.
        mclog::Info("[BUILD] commit={} version={} built {} {} diagnostic=1", MCRAFT_GIT_COMMIT, MCRAFT_VERSION, __DATE__, __TIME__);

        if (!mp2::Resolve()) {
            mclog::Info("unsupported Max Payne 2 version: MaxCraft stays inactive");
            return 0;
        }
        // The shared memory (~120 MB of views) is mapped at MP2's first frame, not here: taking that
        // much of a 32-bit address space while MP2 makes its own big startup allocations made one of
        // them fail (crash reading address 0 at maxpayne2.exe+0x1b80, on some launches).
        if (MH_Initialize() != MH_OK) {
            mclog::Info("MinHook failed to initialize");
            return 0;
        }

        // One batch for all of them: MinHook suspends every thread per MH_EnableHook (~44 ms here),
        // so seventeen installs cost ~0.75 s during which MP2's own window procedure can run into a
        // half-patched hook table - the leading suspect for maxpayne2.exe+0x1b80 in docs/HANDOFF.md
        // §8.1. One flush does the same work with a single suspension. This narrows the window; it
        // does not prove anything, and the user has to say whether the crash is gone.
        hooks::BeginBatch();
        bool ok = true;
        ok &= collision::Install();  // first: rooms are built while the first level loads
        ok &= characters::Install();
        ok &= camera::Install();
        ok &= input::Install();
        ok &= render::Install();
        ok &= hooks::FlushHooks();
        if (ok)
            mclog::Info("MaxCraft ready");
        else
            mclog::Info("MaxCraft running with missing parts (see above)");

        // Detached, and it must stay that way: see the DllMain comment below about unloading.
        CreateThread(nullptr, 0, MinecraftWatch, nullptr, 0, nullptr);
        return 0;
    }

    // Creates the shared mapping and starts Minecraft, on its own thread, waiting for a level first.
    //
    // This used to live inside Present, and that was a mistake worth naming. The shared mapping is what
    // Minecraft opens (SkyLink.poll retries it once a second forever) and Minecraft was started from the
    // same place, so both depended on the Present detour having installed. They are now the two things
    // being measured: a game whose render path is under suspicion could not be tested at all, because
    // the test needs Minecraft connected and Minecraft needed the very hook that was in doubt. The
    // symptom was a session where nothing was wrong - "the mod isn't coming" - with the real fault
    // nowhere in sight.
    //
    // Waiting for a level is deliberate and is the only thing this thread has to be careful about: the
    // mapping is ~191 MB of a 32-bit address space, and taking it while MP2 is still making its own
    // large startup allocations is what the comment in Startup() above warns about. The fallback is
    // wall-clock, not tied to any frame, so a game that never reaches a level still gets Minecraft.
    DWORD WINAPI MinecraftWatch(LPVOID)
    {
        const DWORD startedAt = GetTickCount();
        bool       mapped     = false;
        bool       startAsked = false;
        DWORD      nextStart  = 0;

        // Ten minutes of one-second polls: long enough for a slow first launch, short enough that the
        // thread is not still there asking questions long after the game is over.
        for (int tick = 0; tick < 600; ++tick) {
            const bool levelRunning = State().levelRunning.load(std::memory_order_relaxed);
            const DWORD elapsed     = GetTickCount() - startedAt;

            // The mapping, once a level is running - or after 40 s whether or not one ever starts.
            //
            // The fallback is not optional. It was left out of the first version of this thread,
            // with the reasoning that the mapping must wait for a level because it is 191 MB of a
            // 32-bit address space. That reasoning is right about the *timing* and wrong about the
            // *consequence*: a player who sits at the menu never gets a level, so the mapping never
            // appeared, and Minecraft - which had been started 45 s earlier - spent the whole
            // session failing to open a name that did not exist (Windows error 2, logged once).
            // Forty seconds is past MP2's loading allocations and still inside the window where a
            // player is normally pressing Play, so it costs nothing and removes the deadlock.
            if (!mapped && (levelRunning || elapsed > 40000)) {
                if (Link::Get().Create())
                    mapped = true;
                else
                    mclog::Info("Minecraft: the shared mapping could not be created; it will be tried again");
            }

            if (Config::Get().startWithGame) {
                // Only once the mapping exists. The order matters and was wrong the first time:
                // Minecraft was started while the name it opens did not exist, so it spent its
                // first poll failing and, being a one-shot starter, never asked again.
                if (!startAsked && mapped && (levelRunning || elapsed > 45000)) {
                    startAsked = true;
                    launcher::StartMinecraft();
                    nextStart = GetTickCount() + 20000;
                }
                // StartMinecraft is one shot and can legitimately do nothing: the bundle may still be
                // unpacking, the launcher may already be up without SkyCraft loaded, or the previous
                // attempt may have raced. Ask the other end instead of assuming, and keep asking until
                // SkyCraft's mod is actually holding its mutex. The signed difference keeps the
                // comparison right across GetTickCount's wrap.
                if (startAsked && static_cast<LONG>(GetTickCount() - nextStart) > 0 && !launcher::MinecraftUp()) {
                    mclog::Info("Minecraft: not up yet, trying again");
                    launcher::StartMinecraft();
                    nextStart = GetTickCount() + 20000;
                }
                if (startAsked && elapsed > 60000 && launcher::MinecraftUp())
                    break;  // up and connected-or-connecting: nothing left to do
            }
            Sleep(1000);
        }
        return 0;
    }

    // What DLL_PROCESS_DETACH can and cannot do, in one place. Both paths run under the loader lock,
    // where joining a thread, taking a lock and suspending anything are all off the table: a thread
    // suspended by MinHook while it waits for the loader lock would never be resumed.
    //
    // lpReserved != nullptr: the process is terminating. ExitProcess has already stopped every thread
    // but this one, so no producer is inside a view and this is the one unload path where the
    // teardown order can actually be right. Unmapping the shared memory is not what makes the exit
    // clean - the address space is discarded whole - it is here because Link::Shutdown() is
    // idempotent and this exercises the same path a clean shutdown would.
    //
    // lpReserved == nullptr: FreeLibrary. Deliberately nothing. Two detached threads of ours are
    // still running code that is about to be unmapped - Collision.cpp's worker and Launcher's - and
    // nothing legal under the loader lock can stop them. Unlinking the shared memory here would only
    // decide where the inevitable access violation lands, so the honest answer is that MaxCraft.asi
    // cannot be unloaded while the game runs; the game has to exit.
    void OnDetach(LPVOID reserved)
    {
        if (!reserved)
            return;
        // Stop the producer before the mapping goes: the collision worker is the one thread that can
        // be inside Link::WriteCollision() holding a pointer into the views, and unmapViewOfFile
        // under it would fault in a thread we are already tearing down. Safe here and only here,
        // because process termination has stopped every other thread already - the join returns at
        // once instead of waiting for a worker that is not running any more.
        collision::StopWorker();
        Link::Shutdown();
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // Engine DLLs are static imports of maxpayne2.exe, so they are mapped already; the hooks go
        // in from a thread because DllMain runs under the loader lock.
        if (HANDLE thread = CreateThread(nullptr, 0, Startup, nullptr, 0, nullptr))
            CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        OnDetach(reserved);
    }
    return TRUE;
}
