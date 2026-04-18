# dhewm3 Architecture

## Overview

dhewm3 is a source port of id Tech 4 (Doom 3). The engine executable loads game logic as shared libraries at runtime, enabling modding without recompiling the engine.

**Build outputs:**
- `dhewm3` — main engine executable
- `dhewm3ded` — dedicated server (no GL/OpenAL)
- `base.so/.dylib/.dll` — base game logic
- `d3xp.so/.dylib/.dll` — Resurrection of Evil expansion

---

## Top-Level Subsystem Map

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            dhewm3 executable                                │
│                                                                             │
│  ┌────────────┐  ┌──────────────┐  ┌──────────┐  ┌──────────┐  ┌───────┐  │
│  │ framework/ │  │  renderer/   │  │  sound/  │  │   ui/    │  │  cm/  │  │
│  │ idCommon   │  │ idRenderSystem│  │idSoundSys│  │  idUI    │  │idColl │  │
│  │ idCmdSystem│  │ idRenderWorld │  │idSoundWld│  │ Manager  │  │Model  │  │
│  │ idFileSystem│ │ idRenderModel│  │idSoundEmit│ │          │  │Manager│  │
│  │ idDeclMgr  │  │ idMaterial   │  │idSoundShdr│ │          │  │       │  │
│  └─────┬──────┘  └──────────────┘  └──────────┘  └──────────┘  └───────┘  │
│        │                                                                    │
│  ┌─────▼──────────────────────────────────────────────────────────────────┐ │
│  │                        idlib/ (core utilities)                         │ │
│  │  Math (idVec3, idMat3, …)  Containers (idList, idHashTable, …)        │ │
│  │  Geometry (idPlane, idBounds, …)  idStr  Hashing  SIMD                │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                     sys/ (platform abstraction)                      │   │
│  │  linux/  win32/  osx/  posix/  aros/  stub/                         │   │
│  │  Sys_DLL_Load/Unload  Sys_PollInput  Sys_Milliseconds  Sys_ListFiles │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────┬────────────────────────────────────────────┘
                                 │ Sys_DLL_Load("base") / Sys_DLL_Load("d3xp")
                                 │ GetGameAPI(gameImport_t*)
                    ┌────────────▼───────────────────┐
                    │    base.so  /  d3xp.so          │
                    │                                 │
                    │  game/ (or d3xp/)               │
                    │  ┌──────────┐  ┌────────────┐  │
                    │  │ gamesys/ │  │ physics/   │  │
                    │  │ idClass  │  │ idPhysics  │  │
                    │  │ idEvent  │  │ idPhysicsRB│  │
                    │  │ idTypeInfo│ │ idPhysicsAF│  │
                    │  └──────────┘  └────────────┘  │
                    │  ┌──────────┐  ┌────────────┐  │
                    │  │  ai/     │  │  anim/     │  │
                    │  │ idAI     │  │ idAnim     │  │
                    │  │ idAAS    │  │ idAnimBlend│  │
                    │  └──────────┘  └────────────┘  │
                    │  ┌──────────┐  ┌────────────┐  │
                    │  │ script/  │  │ entities   │  │
                    │  │ idScript │  │ idEntity   │  │
                    │  │ idProgram│  │ idPlayer   │  │
                    │  │ idThread │  │ idActor/AI │  │
                    │  └──────────┘  └────────────┘  │
                    └─────────────────────────────────┘
```

---

## Engine ↔ Game DLL Interface

The DLL boundary is defined in `framework/Game.h`. The engine passes services in, and the DLL returns its implementation.

```
Engine                                      Game DLL (base.so / d3xp.so)
──────                                      ─────────────────────────────
                     gameImport_t*
Sys_DLL_Load()  ─────────────────────────►  GetGameAPI(import)
                                                │
                ◄────────────────────────────   returns gameExport_t
                     gameExport_t               {
                     .game                        .game    → idGameLocal*
                     .gameEdit                    .gameEdit → idGameEditLocal*
                                                }

gameImport_t contains engine services:        gameExport_t exposes:
  .sys               → idSys*                  .game        → idGame*
  .common            → idCommon*               .gameEdit    → idGameEdit*
  .cmdSystem         → idCmdSystem*
  .cvarSystem        → idCVarSystem*
  .fileSystem        → idFileSystem*
  .renderSystem      → idRenderSystem*
  .soundSystem       → idSoundSystem*
  .renderModelManager→ idRenderModelManager*
  .uiManager         → idUserInterfaceManager*
  .declManager       → idDeclManager*
  .collisionModel    → idCollisionModelManager*
  .AASFileManager    → idAASFileManager*
  .version           → GAME_API_VERSION (9)
```

---

## Framework — Main Loop

```
main()
  └─ common->Init(argc, argv)
       ├─ idLib::Init()          — math, string system
       ├─ idFileSystem::Init()   — pak/zip file mounting
       ├─ idDeclManager::Init()  — material/sound declarations
       ├─ idRenderSystem::Init() — OpenGL context
       ├─ idSoundSystem::Init()  — OpenAL context
       └─ Sys_DLL_Load(game)     — load game DLL

  └─ common->Frame()  [called every display frame]
       ├─ Input processing          (Sys_PollInput → SE_KEY / SE_MOUSE / …)
       ├─ if com_ticNumber changed:
       │    game->RunFrame(usercmds) — game logic tick
       │      ├─ idEntity::Think() for all entities
       │      ├─ Physics::Evaluate()
       │      ├─ Script threads
       │      └─ AI decision / pathfinding
       ├─ game->Draw(clientNum)      — populate render world
       │    └─ idEntity::Present() → renderWorld->UpdateEntityDef()
       ├─ renderSystem->EndFrame()   — issue GPU commands
       └─ soundSystem update (if not async)

  └─ Async thread  [60 Hz]
       ├─ soundSystem->AsyncUpdate()   — audio mixing
       ├─ Network tick
       └─ com_ticNumber++
```

---

## Renderer Architecture

```
                  idRenderSystem (RenderSystem.h)
                  ┌──────────────────────────────────┐
                  │  Init / Shutdown                  │
                  │  InitOpenGL / ShutdownOpenGL       │
                  │  BeginFrame / EndFrame            │
                  │  AllocRenderWorld()               │
                  │  2D drawing (text, images, GUI)   │
                  └──────────────┬───────────────────┘
                                 │ per scene
                  ┌──────────────▼───────────────────┐
                  │  idRenderWorld (RenderWorld.h)    │
                  │  AddEntityDef(renderEntity_s*)    │
                  │  UpdateEntityDef / FreeDef        │
                  │  AddLightDef(renderLight_s*)      │
                  │  RenderScene(renderView_s*)       │
                  └──────────────────────────────────┘

renderEntity_s ─────────────────────────────────────────────────┐
  .hModel         → idRenderModel*                              │
  .origin/axis    → position & rotation                         │
  .customShader   → idMaterial* override                        │
  .shaderParms[]  → per-entity shader parameters                │
  .joints[]       → idJointMat* for skeletal animation          │
  .gui[]          → idUserInterface* overlays                   │
                                                                │
idRenderModel hierarchy:                                        │
  idRenderModel (Model.h)                                       │
  └── idRenderModelStatic    — BSP/static geometry              │
      ├── idRenderModelMD5   — skeletal animation (MD5Mesh/Anim)│
      ├── idRenderModelMD3   — Quake3-style vertex anim         │
      ├── idRenderModelLiquid— water/fluid surfaces             │
      ├── idRenderModelPrt   — particle systems                 │
      ├── idRenderModelBeam  — beam / ribbon rendering          │
      └── idRenderModelSprite— billboarded sprites              │
                                                                │
idMaterial (Material.h) ────────────────────────────────────────┘
  Parsed from .mtr declaration files
  Multiple stages: diffuse, bump, specular, custom fragment programs

Render pipeline (per frame):
  ┌───────────────────────────┐     ┌───────────────────────────────┐
  │      Frontend             │     │       Backend (OpenGL)        │
  │  Portal/PVS culling       │────►│  Depth pre-pass               │
  │  Collect visible entities │     │  Stencil shadow volumes       │
  │  Build draw surf lists    │     │  Opaque geometry              │
  │  Light interaction lists  │     │  Transparent surfaces         │
  └───────────────────────────┘     │  2D GUI overlay               │
                                    └───────────────────────────────┘
```

---

## Sound Architecture

```
idSoundSystem (sound/sound.h)
  Init / Shutdown / InitHW / ShutdownHW
  AsyncUpdate()           ← called 60 Hz from async thread
  AllocSoundWorld()

  └─ idSoundWorld (per level)
       PlaceListener(origin, axis)   ← called each game frame
       PlayShaderDirectly()          ← background music
       FadeSoundClasses()            ← fade dialog/effects/etc.

       └─ idSoundEmitter (per entity)
            UpdateEmitter(origin, listenerId, parms)
            StartSound(shader, channel, parms)
            StopSound(channel)
            FadeSound(channel, targetVol, time)
            ModifySound(channel, parms)

            └─ idSoundChannel (per playing sound)
                 idSoundShader* shader   — .sndshd declaration
                 idSoundSample* sample   — decoded PCM buffer

soundShaderParms_t:
  minDistance / maxDistance   — attenuation range
  volume (dB)
  shakes                      — controller rumble amount
  soundClass (0–3)            — for global class fading
  flags: SSF_LOOPING, SSF_GLOBAL, SSF_OMNIDIRECTIONAL,
         SSF_PRIVATE_SOUND, SSF_NO_OCCLUSION, SSF_PLAY_ONCE
```

---

## Game Entity Hierarchy

```
idClass  (game/gamesys/Class.h)
  — RTTI via idTypeInfo, event dispatch via idEvent
  │
  └─ idEntity  (game/Entity.h)
       .entityNumber   .name   .spawnArgs (idDict)
       .health   .thinkFlags   .scriptObject
       .flags: notarget, takedamage, hidden, …
       │
       ├─ idPlayer         — player character, user input, inventory
       │
       ├─ idActor          — base for all animate characters
       │   ├─ idAI         — NPC with AAS pathfinding, attack states
       │   └─ idCombatNode
       │
       ├─ idWeapon         — weapon logic, fire/reload state machine
       │
       ├─ idItem           — pickups (ammo, health, armor, keys)
       │
       ├─ idMover          — moving geometry (doors, platforms, trains)
       │   ├─ idDoor
       │   └─ idPlat
       │
       ├─ idMoveable       — physics-driven prop
       │
       ├─ idLight          — dynamic light source
       │
       ├─ idCamera         — cinematic / security camera
       │
       ├─ idProjectile     — bullet, rocket, fireball
       │
       ├─ idFx             — visual effects emitter
       │
       ├─ idTrigger        — trigger volumes
       │
       ├─ idAFEntity       — articulated figure body part
       │   └─ idAF         — full ragdoll assembly
       │
       └─ idBrittleFracture— breakable geometry
```

---

## Physics System

```
idPhysics (game/physics/Physics.h) — abstract interface
  │
  ├─ idPhysics_Static          — immovable geometry
  ├─ idPhysics_StaticMulti     — multiple static bodies
  ├─ idPhysics_RigidBody       — single rigid body
  ├─ idPhysics_Parametric      — scripted movement (movers)
  ├─ idPhysics_Player          — character controller
  ├─ idPhysics_Monster         — NPC movement
  └─ idPhysics_AF              — articulated figure (ragdoll)

Each physics object per frame:
  Evaluate(timeStep)
    ├─ Apply forces / gravity
    ├─ Integrate velocity
    ├─ idCollisionModelManager::Translation() / Rotation()
    ├─ Resolve contacts
    └─ Emit idEntity::Collide() events
```

---

## Game Scripting System

```
.script files (map-embedded or game data)
  parsed by idParser → idProgram

idProgram   — compiled bytecode, type definitions, function table
idThread    — execution context (stack, program counter)
idScriptObject — per-entity script data / vtable

Execution:
  idEntity::Think()
    └─ scriptThread->Execute()
         └─ idInterpreter::Execute()
              ├─ OP_CALL   → native idEventDef callback
              └─ OP_RETURN → resume caller thread

idEventDef  — named event with typed args  (e.g. EV_Touch, EV_Use)
idEventFunc — C++ function bound to an event on a class
idEvent     — runtime event instance (queued or immediate)
```

---

## Platform / sys Layer

```
sys/sys_public.h — OS-agnostic declarations
sys/sys_local.h  — Local implementation interface

Implementations:
  sys/posix/   — POSIX common (threads, mutexes, sockets)
  sys/linux/   — Linux-specific (X11/Wayland, inotify)
  sys/osx/     — macOS-specific (NSApplication, CoreFoundation)
  sys/win32/   — Windows (Win32 API, WGL)
  sys/aros/    — AROS OS
  sys/stub/    — Stubs for dedicated server (no GL/OpenAL)

Key function groups:
  Timing:   Sys_Milliseconds()  Sys_MillisecondsPrecise()  Sys_Sleep()
  Input:    Sys_PollInput()  → sysEvent_t {SE_KEY, SE_CHAR, SE_MOUSE, …}
  Files:    Sys_ListFiles()  Sys_GetPath()
  DLLs:     Sys_DLL_Load()  Sys_DLL_GetProcAddress()  Sys_DLL_Unload()
  CPU:      Sys_GetProcessorId() → CPUID_MMX | CPUID_SSE | CPUID_SSE2 | …
  FPU:      Sys_FPU_SetPrecision()  Sys_FPU_SetFTZ()  Sys_FPU_SetDAZ()
  Memory:   Sys_GetSystemRam()  Sys_LockMemory()
```

---

## CMake Build Graph

```
CMakeLists.txt (neo/)
  │
  ├─ dhewm3  [executable]
  │    sources: framework/  renderer/  sound/  ui/  cm/  idlib/  sys/
  │    links:   OpenAL  OpenGL  SDL2 (or SDL3)  [optional: Curl, ImGui]
  │    defines: GAME_DLL (dlopen path), D3_ARCH, D3_OSTYPE
  │
  ├─ dhewm3ded  [executable]  (if DEDICATED=ON)
  │    like dhewm3 but sources from sys/stub/ replace renderer/sound
  │
  ├─ base  [shared library]  (if BASE=ON and HARDLINK_GAME=OFF)
  │    sources: game/
  │    exports: GetGameAPI()
  │
  └─ d3xp  [shared library]  (if D3XP=ON and HARDLINK_GAME=OFF)
       sources: d3xp/
       exports: GetGameAPI()

Options that change the graph:
  HARDLINK_GAME=ON  → game/ compiled into dhewm3 directly (no DLL)
  SDL3=ON           → links SDL3 instead of SDL2
  IMGUI=ON          → adds libs/imgui/ to dhewm3 sources (requires C++11)
  TRACY=ON          → adds Tracy profiler sources
  ASAN/UBSAN=ON     → adds sanitizer flags (GCC/Clang only)
```

---

## Key Global Singletons

| Symbol | Type | Header | Purpose |
|--------|------|--------|---------|
| `common` | `idCommon*` | `framework/Common.h` | Main engine |
| `renderSystem` | `idRenderSystem*` | `renderer/RenderSystem.h` | Renderer |
| `soundSystem` | `idSoundSystem*` | `sound/sound.h` | Audio |
| `fileSystem` | `idFileSystem*` | `framework/FileSystem.h` | File I/O |
| `cmdSystem` | `idCmdSystem*` | `framework/CmdSystem.h` | Console cmds |
| `cvarSystem` | `idCVarSystem*` | `framework/CVarSystem.h` | Console vars |
| `declManager` | `idDeclManager*` | `framework/DeclManager.h` | Declarations |
| `uiManager` | `idUserInterfaceManager*` | `ui/UserInterface.h` | GUI |
| `collisionModelManager` | `idCollisionModelManager*` | `cm/CollisionModel.h` | Collision |
| `game` | `idGame*` | `framework/Game.h` | Game DLL instance |
| `gameEdit` | `idGameEdit*` | `framework/Game.h` | In-editor game API |
| `com_frameTime` | `int` | `framework/Common.h` | Frame delta (ms) |
| `com_ticNumber` | `volatile int` | `framework/Common.h` | 60 Hz tick counter |
