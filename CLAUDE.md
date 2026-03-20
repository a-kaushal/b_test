# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SMM is a C++17 Windows DLL that implements a GOAP (Goal-Oriented Action Planning) bot for World of Warcraft. It reads game state via a kernel driver (IOCTL), makes AI decisions, and drives input automation. It is compiled as `SMM.dll` and injected into the game process.

## Build

Open `MemoryTest.sln` (one level up) in Visual Studio 2022 and build the **SMM** project. SMM depends on the **Detour** project in the same solution.

Command-line build:
```
msbuild SMM.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `x64/Release/SMM.dll` or `x64/Debug/SMM.dll`

External include dependencies (absolute paths baked into the project):
- SkyFire WoW server headers: `C:\Users\A\Downloads\SkyFire_548\...`
- Recast/Detour navigation: `C:\Users\A\Downloads\recastnavigation-1.6.0\...`
- MySQL: `C:\Program Files\MySQL\MySQL Server 9.5\include`

There are no automated tests and no linting configuration.

## Architecture

### Data Flow

```
Kernel driver IOCTL → MemoryRead.h (parse offsets) → WorldState (central state)
→ GoapSystem (priority-based action selection)
→ Movement / Combat / Input systems
→ next frame
```

### Core Systems

**Memory Layer** (`Memory.h`, `MemoryRead.h`)
Hard-coded WoW memory offsets read via kernel driver IOCTL. `MemoryRead.h` contains all entity manager traversal logic and offset definitions. Never use the Windows `ReadProcessMemory` API — all reads go through the driver interface in `Memory.h`.

**Entity System** (`Entity.h`)
`GameEntity` wraps polymorphic `std::shared_ptr<EntityInfo>` subtypes: `PlayerInfo`, `EnemyInfo`, `ObjectInfo` (gatherable nodes), `ItemInfo`. `WorldState` holds the authoritative entity list.

**World State** (`WorldState.h`)
Single global `WorldState` struct updated each tick. Contains player info, entity list, action state flags, current map ID, and profile settings. Protected by `g_EntityMutex` for cross-thread access.

**GOAP AI** (`GoapSystem.h`, `Behaviors.h`)
Each bot action extends abstract `GoapAction` with `CanExecute()` / `Execute()` / `GetPriority()`. The planner selects the highest-priority executable action each tick. Priority constants: Unstuck=10000, Respawn=1000, Combat=200, Gather=100 (lower numbers = normal background actions).

**Pathfinding** (`Pathfinding2.h`, `MovementController.h`, `FMapLoader.cpp`, `VMapLoader.cpp`)
Uses Recast/Detour NavMesh with custom FMap (floor collision) and VMap (vertical/3D collision) formats. `MovementController` drives waypoint following with stuck detection. Supports both ground and flying movement modes.

**Combat** (`CombatController.h`, `Combat.h`)
Spell rotation via chat command injection. Tracks cooldowns and Holy Power (Retribution Paladin). Target selection logic lives in `Combat.h`.

**Input Simulation** (`SimpleKeyboardClient.h`, `SimpleMouseClient.h`)
All keyboard and mouse events are sent via raw input simulation. Camera control uses mouse movement deltas. Object interaction uses click targeting.

**Camera** (`Camera.h`)
Constructs view/projection matrices from game memory to project 3D world positions to 2D screen coordinates (90° FOV). Used by the overlay and for UI interaction targeting.

**Profile System** (`Profile.h`, `ProfileLoader.h`, `ProfileInterface.h`)
Bot profiles are separate DLLs loaded dynamically at runtime. `ProfileLoader` handles DLL load/unload. `ProfileInterface` defines the plugin contract. Settings include gather toggles, vendor lists, blackspots, and player faction.

**Web Server** (`WebServer.h`, `WebServer.cpp`)
Embedded HTTP server for external monitoring and control: JSON state dumps, profile switching, log retrieval. Status heartbeat written to `C:\SMM\bot_status.txt` every ~2 seconds.

**Lua Scripting** (`LuaAnchor.h`)
Exposes bot internals to Lua scripts for custom logic injection.

### Threading Model

- **Bot thread**: reads memory, runs GOAP, executes actions
- **GUI thread**: reads `WorldState` with `g_EntityMutex`
- **Web server thread**: handles HTTP requests
- Global control flags: `g_IsRunning` (atomic), `g_BotLogicActive`, `g_ProfileActive`
- Crash dumps written to `C:\SMM\SMM_Crash.dmp`

### Logging

`g_LogFile` is a global file stream opened once at startup, shared via mutex. Use the existing `Logger.h` helpers — do not open/close the log file per-call.
