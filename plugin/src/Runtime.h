#pragma once

#include "Engine.h"
#include "Link.h"

#include <atomic>

// State shared between the game-thread hooks, the input hooks and the Present hook.
// MaxFX updates and renders on one thread, so most of this is only touched there.
struct Runtime
{
    // Minecraft is connected, in its world, has acknowledged our teleport: it drives Max.
    std::atomic<bool> puppeting{ false };
    // Puppeting, or waiting for Minecraft to arrive after a teleport: MP2's controls don't move Max.
    std::atomic<bool> minecraftOwnsPlayer{ false };
    std::atomic<bool> mcScreenOpen{ false };
    std::atomic<bool> mcInWorld{ false };
    // MP2 is paused, in a menu, loading or playing a cut-scene: it gets all input.
    std::atomic<bool> gameOwnsInput{ true };

    // Look direction in Minecraft degrees, integrated from the mouse (zero added latency).
    float yaw{ 0.0f };
    float pitch{ 0.0f };
    bool  lookInitialized{ false };
    float sensitivity{ 0.5f };

    // Mouse deltas collected by the input hooks since the last game frame.
    std::atomic<int> lookDx{ 0 };
    std::atomic<int> lookDy{ 0 };

    // Minecraft cursor while one of its screens is open (overlay pixels).
    std::atomic<int> cursorX{ 0 };
    std::atomic<int> cursorY{ 0 };
    std::atomic<int> viewportW{ 1280 };
    std::atomic<int> viewportH{ 720 };

    // The player switched to MP2's own controls (Config::gameControlsKey): Minecraft lets go of Max
    // and MP2 plays as it would without the mod, until the key is pressed again. Set by the input
    // thread, read by the game thread.
    std::atomic<bool> gameControls{ false };

    // Latest Minecraft state (game thread) and the camera built from it this frame.
    proto::McState mc{};
    bool           haveMc{ false };
    double         eyeX{ 0 }, eyeY{ 0 }, eyeZ{ 0 };  // Minecraft coords, interpolated
    double         feetX{ 0 }, feetY{ 0 }, feetZ{ 0 };
    bool           cameraValid{ false };
    mp2::Matrix4x3 camera{};  // MP2 camera-to-world for this frame
    float          fovDeg{ 70.0f };

    mp2::X_Character* player{ nullptr };
    std::uint32_t     epoch{ 1 };    // collision epoch: bumps on level change / recalibration
    std::uint32_t     levelId{ 1 };  // SkyState::worldId
    std::uint64_t     lastPlayerFrameMs{ 0 };
// True while MP2 is actually simulating a level. Set by the game thread, read by the thread that
// starts Minecraft.
//
// This exists instead of that thread reading `player` and `lastPlayerFrameMs` directly, because those
// are plain members written by the game thread and `lastPlayerFrameMs` is 64 bits wide: a reader on
// another thread can catch it mid-write. The flag is atomic and carries no such question, and the two
// members stay exactly as they were for the readers that are on the game thread.
std::atomic<bool> levelRunning{ false };
};

Runtime& State();
