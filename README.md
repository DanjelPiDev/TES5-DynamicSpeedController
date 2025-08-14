# Dynamic Speed Controller

> [![Download Latest Release](https://img.shields.io/github/v/release/DanjelPiDev/TES5-DynamicSpeedController)](https://github.com/DanjelPiDev/TES5-DynamicSpeedController/releases/latest)

A small SKSE plugin that tweaks your SpeedMult and attack speed depending on what you're doing (drawn/sneak/jog/sprint/combat), now with optional NPC support and attack scaling by weapon weight & character size.
No ESP, no MCM, just a DLL + JSON (plus SKSE menu integration).

## Features
- Different movement speed values per state (Default, Jogging, Drawn, Sneak, Sprint)
- Optional speed scaling for NPCs
- Attack speed scaling based on weapon weight and player size
- Option to skip reductions in combat
- Toggle jogging mode via hotkey or user event (Default: "Shout" key)
- Location-based modifiers for specific locations or location types
- Lightweight, no scripts, no save bloat
- Fixes Skyrim’s diagonal movement speed boost

## Install
1. Drop the DLL and *SpeedController.json* into your Data\SKSE\Plugins\ folder.
2. Launch the game. The plugin auto-loads and applies settings.

## How to Update
1. Remember your SpeedController.json values (The values you are using), because the update reverts them.
2. Drop the DLL and SpeedController.json into Data\SKSE\Plugins\
3. Start the game, settings auto-load

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
  "kReduceInLocationType": [],
  "kAttackSpeedEnabled": true,
  "kAttackOnlyWhenDrawn": true,
  "kAttackBase": 1.00,
  "kWeightPivot": 10.0,
  "kWeightSlope": -0.03,
  "kUsePlayerScale": false,
  "kScaleSlope": 0.25,
  "kMinAttackMult": 0.60,
  "kMaxAttackMult": 1.80,
  "kEnableSpeedScalingForNPCs": false,
  "kIgnoreBeastForms": true
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
	"kToggleSpeedKey": 269,
	"kAttackSpeedEnabled": true,
	"kAttackOnlyWhenDrawn": true,
	"kAttackBase": 1.00,
	"kWeightPivot": 10.0,
	"kWeightSlope": -0.03,
	"kUsePlayerScale": false,
	"kScaleSlope": 0.25,
	"kMinAttackMult": 0.60,
	"kMaxAttackMult": 1.80,
	"kEnableSpeedScalingForNPCs": false,
	"kIgnoreBeastForms": true
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
- kIgnoreBeastForms: If true, completely disables speed & attack modifiers when the actor is in a beast form (Werewolf / Vampire Lord).
- kEnableSpeedScalingForNPCs: If true, applies all scaling rules to NPCs as well as the player.
- kAttackSpeedEnabled: Enables or disables attack speed scaling entirely. When false, weapon attack speed will not be modified at all.
- kAttackOnlyWhenDrawn: If true, attack speed scaling only applies when the actor’s weapons are drawn; if sheathed, no scaling is applied.
- kAttackBase: The base weapon speed multiplier before any weight or scale adjustments are applied (1.0 = vanilla default speed).
- kWeightPivot: The reference weapon weight (in Skyrim units) where no weight-based speed adjustment is applied. Heavier or lighter weapons are scaled relative to this pivot.
- kWeightSlope: The amount of attack speed change per unit of weapon weight difference from the pivot. Negative values make heavier weapons slower and lighter weapons faster, positive values do the opposite.
- kUsePlayerScale: If true, the actor's scale (size) is factored into attack speed calculations. Larger or smaller characters will have faster/slower attacks depending on kScaleSlope.
- kScaleSlope: The amount of attack speed change per unit of scale difference from 1.0 (normal size). Positive values make larger actors faster, negative values make them slower.
- kMinAttackMult: The minimum allowed final attack speed multiplier after all calculations. Prevents extreme slowdowns.
- kMaxAttackMult: The maximum allowed final attack speed multiplier after all calculations. Prevents extreme speed boosts.

**Hints**: 
- Use kToggleSpeedEvent, because the key is not always recognized by the game (e.g., when using a controller).
- Location rules can either replace or add to the base reductions (configurable in the SKSE Menu version).

## Notes / Compatibility
- Works alongside movement/anim mods, values are additive via ModActorValue(SpeedMult).

- The plugin nudges a secondary AV internally to force a safe speed refresh, no ESP, no animations required.

- If your sprint mod renames the sprint event, update kSprintEventName.

## Uninstall
Remove the DLL and the JSON. No save bloat, no scripts left behind.

## Credits
SKSE team & CommonLibSSE/NG community.
