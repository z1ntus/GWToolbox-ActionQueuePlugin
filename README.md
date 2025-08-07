# GWToolbox-ActionQueuePlugin
A GWToolbox++ plugin for saving and replaying farming methods via .bot scripts. Designed to help players document, repeat and teach optimized farms. ~ Not as an unattended bot! ~ Includes repeat option, and support for various in-game actions. Use responsibly.

---------------------------------------------------------------------------------------------------------------------------------------------
🛠️ Building the ActionQueue Plugin

To build the ActionQueuePlugin as part of GWToolbox++, follow these steps:

1. Clone the GWToolbox++ Repository

Follow the official build instructions for GWToolbox++ here:
👉 https://github.com/HasKha/GWToolboxpp#building

This sets up the environment and dependencies for building the toolbox.

2. Add the Plugin Source

Copy or clone this plugin repository's ActionQueuePlugin/ folder into the /GWToolboxpp/plugins/ folder in your local GWToolbox++ repository:

GWToolboxpp/
├── plugins/
│   ├── ActionQueuePlugin/
│   │   ├── ActionQueuePlugin.cpp
│   │   └── ActionQueuePlugin.h

3. Register the Plugin in CMake

Edit the file: 
/GWToolboxpp/cmake/gwtoolboxdll_plugins.cmake

Add the following line at the bottom with the other add_tb_plugin entries:
add_tb_plugin(ActionQueuePlugin)

4. Rebuild GWToolbox++

delete /build/ folder and run:
cmake --preset=vcpkg

5. Open build and edit

cmake --open build

6. Build ActionQueuePlugin

Ctrl + B

7. Install the Plugin

Copy the compiled plugin .dll file (ActionQueuePlugin.dll) into your GWToolbox++ plugins folder: 
C:\Users\<YourUsername>\Documents\GWToolboxpp\<YourComputerName>\plugins

8. Run
---------------------------------------------------------------------------------------------------------------------------------------------
Scripting Actions (.bot Files)

You can save and replay farming routines using .bot script files placed in:
C:\Users\<YourUsername>\Documents\GWToolboxpp\scripts
Each line in a .bot file represents a command. The following commands are supported:

    EnterPortal x y
    Enters a portal in front of position (x and y need to be behind th portal) and waits until map is loaded.

    Move x y
    Moves to coordinates (x, y).

    UseSkill skill_id [target_id]
    Uses the specified skill. target_id is optional (0 = no target).

    ApproachNearestEnemy dist
    Approaches the nearest enemy within a given distance.

    Wait ms
    Waits for the specified number of milliseconds.

    InteractNearestItemWithinDistance dist
    Interacts (picks up) with the nearest item (drop) within the specified distance.

    ReturnToOutpost
    Returns to the outpost and waits until map is loaded.

    EnterChallenge
    Enters the challenge mission (e.g., Mission) and waits until map is loaded.

    DropItem
    Drops the item (flag, item spell, etc.).

    Interact agent_id
    Interacts/attacks with/the the agent (e.g., NPC or object) with the given ID.

    TargetNearestEnemyWithinDistance dist
    Targets the nearest enemy within the specified distance.

    TargetStrongestEnemyWithinDistance dist
    Targets the strongest enemy (highest HP) within the specified distance.
