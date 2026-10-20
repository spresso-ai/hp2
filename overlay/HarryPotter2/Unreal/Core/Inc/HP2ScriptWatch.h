/*=============================================================================
	HP2ScriptWatch.h: Optional observers for script calls and state changes.

	UObject::CallFunction reports script-to-script calls of watched functions
	(selected by FName index) after their parameters are evaluated and before
	the body runs, so an observer may adjust value parameters in Locals.
	UObject::GotoState reports state transitions. Both are unbound (NULL) by
	default, so the VM behaves exactly as before.
=============================================================================*/

#ifndef HP2SCRIPTWATCH_H
#define HP2SCRIPTWATCH_H

class UObject;
class UFunction;

typedef void (*HP2ScriptWatchFunc)( UObject* Self, UFunction* Function, BYTE* Locals, UObject* Caller );
typedef void (*HP2StateWatchFunc)( UObject* Self, FName OldState, FName NewState );

enum { HP2_SCRIPT_WATCH_BITS = 65536 };

CORE_API extern HP2ScriptWatchFunc GHP2ScriptWatch;
CORE_API extern HP2StateWatchFunc GHP2StateWatch;
CORE_API extern BYTE GHP2ScriptWatchNames[HP2_SCRIPT_WATCH_BITS/8];

inline UBOOL HP2IsScriptWatched( INT NameIndex )
{
	return NameIndex>=0 && NameIndex<HP2_SCRIPT_WATCH_BITS && (GHP2ScriptWatchNames[NameIndex>>3] & (1<<(NameIndex&7)));
}

inline void HP2WatchScriptName( INT NameIndex )
{
	if( NameIndex>=0 && NameIndex<HP2_SCRIPT_WATCH_BITS )
		GHP2ScriptWatchNames[NameIndex>>3] |= (BYTE)(1<<(NameIndex&7));
}

#endif
