# Dynamic Speed Controller

Tiny SKSE plugin that adjusts the player's SpeedMult dynamically (drawn/sneak/jog/sprint/combat) without an ESP and without MCM.
All tuning is done via a simple JSON file.

## Features
- Per-state speed deltas (Default, Jogging, Drawn, Sneak, Sprint)
- Optional "no reduction while in combat"
- Toggle between two out-of-combat modes (e.g., Walk vs. Jog) via hotkey or user event
- Location-based modifiers, set different reductions or increases for specific locations or location types
- No ESP, no scripts, no MCM, just a DLL + JSON

## Install
1. Drop the DLL and *SpeedController.json* into your Data\SKSE\Plugins\ folder.

2. Launch the game. The plugin auto-loads and applies settings.

## Usage
- **Default Speed**: The plugin applies a base speed reduction when the player is out of combat with weapons sheathed.
- **Jogging Mode**: Press the toggle key or trigger the user event to switch to jogging mode, which applies a different speed reduction.
- **Drawn Weapons**: When the player draws a weapon, the plugin applies a speed reduction (This is useful for combat scenarios, it is coupled, so if you don't draw a weapon in combat, you will not get the speed boost/reduction).
- **Sneaking**: When the player is sneaking, a different speed reduction is applied.

### Configuration
Open *(Data/SKSE/Plugins/SpeedController.json)*

```json
{
  "kReduceOutOfCombat": 45.0,
  "kReduceJoggingOutOfCombat": 25.0,
  "kReduceDrawn": 15.0,
  "kReduceSneak": 20.0,
  "kIncreaseSprinting": 25.0,
  "kNoReductionInCombat": true,

  "kToggleSpeedKey": 269,
  "kToggleSpeedEvent": "Shout",
  "kSprintEventName": "Sprint",

  "kReduceInLocationSpecific": [],
  "kReduceInLocationType": []
}
```

Example for an added location:
```json
{
	"kIncreaseSprinting": 25.0,
	"kLocationAffects": "default",
	"kLocationMode": "replace",
	"kNoReductionInCombat": true,
	"kReduceDrawn": 15.0,
	"kReduceInLocationSpecific": [
		{
			"form": "cceejsse001-hstead.esm|0x000F1E",
			"value": 58.0
		}
	],
	"kReduceInLocationType": [],
	"kReduceJoggingOutOfCombat": 25.0,
	"kReduceOutOfCombat": 45.0,
	"kReduceSneak": 61.0,
	"kSprintEventName": "Sprint",
	"kToggleSpeedEvent": "Shout",
	"kToggleSpeedKey": 269
	}
```


#### What the settings do
- kReduceOutOfCombat: Base reduction when out of combat (weapon sheathed).
- kReduceJoggingOutOfCombat: Alternate out-of-combat reduction when you toggle "jogging mode."
- kReduceDrawn: Reduction while weapons are drawn.
- kReduceSneak: Reduction while sneaking.
- kIncreaseSprinting: Extra increase applied while sprinting.
- kNoReductionInCombat: If true, disables reductions during combat (useful for responsiveness).
- kToggleSpeedKey: Keyboard scancode for toggling jogging mode (e.g., 269 = dpadright (https://www.nexusmods.com/skyrimspecialedition/articles/7704%5D) / depends on layout). Set 0 to disable (Or better, don't use it :D).
- kToggleSpeedEvent: Game user event name that also toggles jogging mode (default "Shout"). Set "" to disable.
- kSprintEventName: Input event used to latch sprint (default "Sprint"). If you use custom control maps, set the matching event name here.
- kReduceInLocationSpecific: LocationRules.Specific List of `Plugin|0xFormID` + value pairs for exact locations (e.g. "Skyrim.esm|0x0001A26F" : 30.0). You can also simple go into the cell, open the menu and press the button "Use Current Location".
- kReduceInLocationType: LocationRules.Types. Same, but for location keywords (e.g. "Skyrim.esm|0x00013793" for LocTypeDungeon).

**Hint**: Use kToggleSpeedEvent, because the key is not always recognized by the game (e.g., when using a controller).

## Notes / Compatibility
- Works alongside movement/anim mods, values are additive via ModActorValue(SpeedMult).

- The plugin nudges a secondary AV internally to force a safe speed refresh, no ESP, no animations required.

- If your sprint mod renames the sprint event, update kSprintEventName.

## Uninstall
Remove the DLL and the JSON. No save bloat, no scripts left behind.

## Credits
SKSE team & CommonLibSSE/NG community.
