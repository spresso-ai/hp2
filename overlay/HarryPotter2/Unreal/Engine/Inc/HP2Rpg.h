/*=============================================================================
	HP2Rpg.h: Native RPG layer (experience, skills, shop, wardrobe and house
	point feedback) driven from UGameEngine.

	All game state it touches is reached by name (FindField / FindFunction
	on HGame classes); retail packages are never modified. RPG progress is
	stored in a SaveN.rpg sidecar next to each SaveN.usa. Disable with
	[HP2Rpg] Enabled=False in the game ini.
=============================================================================*/

#ifndef HP2RPG_H
#define HP2RPG_H

class UGameEngine;
class UViewport;
class UCanvas;

void HP2RpgTick( UGameEngine* Engine, FLOAT DeltaSeconds );
void HP2RpgDraw( UGameEngine* Engine, UViewport* Viewport, UCanvas* Canvas );
UBOOL HP2RpgInputEvent( UGameEngine* Engine, UViewport* Viewport, EInputKey Key, EInputAction Action, FLOAT Delta );
UBOOL HP2RpgExec( UGameEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar );
void HP2RpgPreSave( UGameEngine* Engine );
void HP2RpgPostSave( UGameEngine* Engine, INT Slot );
void HP2RpgOnLevelLoaded( UGameEngine* Engine );
void HP2RpgOnLoadedSlot( UGameEngine* Engine, INT Slot );

#endif
