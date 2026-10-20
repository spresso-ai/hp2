/*=============================================================================
	HP2ScriptWatch.h: Optional observer for one named script function call.

	UObject::CallFunction reports script-to-script calls of the watched
	function (matched by FName index) after its parameters are evaluated.
	Unbound (NULL) by default, so the VM behaves exactly as before.
=============================================================================*/

#ifndef HP2SCRIPTWATCH_H
#define HP2SCRIPTWATCH_H

class UObject;
class UFunction;

typedef void (*HP2ScriptWatchFunc)( UObject* Self, UFunction* Function, BYTE* Locals );

CORE_API extern HP2ScriptWatchFunc GHP2ScriptWatch;
CORE_API extern INT GHP2ScriptWatchNameIndex;

#endif
