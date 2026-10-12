#pragma once

namespace launcher
{
    // Starts Minecraft with the game (per MaxCraft.ini) unless one with SkyCraft's mod is running.
    //
    // Default: the Minecraft SkyCraft ships (portable Prism Launcher + a ready "SkyCraft" instance),
    // from MaxCraft\SkyCraft-Minecraft.zip in the game folder, unpacked to %LOCALAPPDATA%\SkyCraft the
    // same way SkyCraft's own plugin does it (so a player who has both shares one install).
    void StartMinecraft();
    // True while a Minecraft with SkyCraft's mod is running: it holds Local\SkyCraft_v1_minecraft for
    // as long as it lives (SkyLink.announceRunning).
    //
    // Exposed because StartMinecraft() is one shot and can lose a race - a launcher still unpacking,
    // an instance already booting, a start that quietly did nothing - and the only way to know whether
    // the auto-start worked is to ask the other end rather than to assume it did.
    bool MinecraftUp();
}
