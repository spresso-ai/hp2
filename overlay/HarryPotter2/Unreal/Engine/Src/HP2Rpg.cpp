/*=============================================================================
	HP2Rpg.cpp: Native RPG layer for Harry Potter 2.

	Adds experience and levels, skills, spell mastery perks, vendors with
	their own rotating stock, cosmetics with stats (outfits, costumes, hats,
	glasses, held items, pets, auras), a bestiary, Wizard Card collection
	rewards, trophies, a trouble/detention system, student interaction and
	live house point feedback on top of the retail game, without touching
	retail packages.

	Game state is reached by name on the running HGame objects. Script calls
	and state changes are observed through the VM hooks in HP2ScriptWatch.h.
	RPG progress lives in memory and is written to <SaveSlotPath>/SaveN.rpg
	whenever the game writes SaveN.usa, and read back when slot N loads.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnSkeletalMesh.h"
#include "HP2Rpg.h"
#include "HP2ScriptWatch.h"
#include "HP2RpgUi.h"
#include "HP2RpgData.h"

namespace
{

/*-----------------------------------------------------------------------------
	Small RNG (kept separate from appRand so game RNG streams are untouched).
-----------------------------------------------------------------------------*/

static DWORD GRpgRandSeed = 0x2468ace1;
static INT RpgRand( INT Range )
{
	GRpgRandSeed = GRpgRandSeed*1664525u + 1013904223u;
	return Range>0 ? (INT)((GRpgRandSeed>>8) % (DWORD)Range) : 0;
}

static DWORD HashMix( DWORD A, DWORD B )
{
	DWORD H = A*2654435761u ^ (B+0x9e3779b9u+(A<<6)+(A>>2));
	H ^= H>>15; H *= 2246822519u; H ^= H>>13;
	return H;
}

static DWORD HashString( const TCHAR* S )
{
	DWORD H = 2166136261u;
	for( ; S && *S; S++ )
		H = (H ^ (DWORD)appToLower(*S)) * 16777619u;
	return H;
}

/*-----------------------------------------------------------------------------
	Diagnostics: <SavePath>/HP2Rpg.log.
-----------------------------------------------------------------------------*/

static void VARARGS RpgLog( const TCHAR* Fmt, ... )
{
	TCHAR Buffer[1024];
	GET_VARARGS( Buffer, ARRAY_COUNT(Buffer), Fmt );
	debugf( NAME_Log, TEXT("HP2Rpg: %s"), Buffer );
	static FString LogPath;
	static INT Lines = 0;
	if( !LogPath.Len() )
	{
		LogPath = GSys->SavePath + PATH_SEPARATOR TEXT("HP2Rpg.log");
		appSaveStringToFile( FString(TEXT("HP2Rpg log\r\n")), *LogPath );
	}
	if( Lines++ > 3000 )
		return;
	FString Old;
	appLoadFileToString( Old, *LogPath );
	appSaveStringToFile( Old + FString(Buffer) + TEXT("\r\n"), *LogPath );
}

/*-----------------------------------------------------------------------------
	Reflection helpers.
-----------------------------------------------------------------------------*/

static UClass* FindClass( const TCHAR* Name )
{
	return FindObject<UClass>( ANY_PACKAGE, Name );
}

static UBOOL ClassChainIs( UObject* Obj, const TCHAR* Name )
{
	if( !Obj || !Name )
		return 0;
	for( UClass* C=Obj->GetClass(); C; C=C->GetSuperClass() )
		if( appStricmp(C->GetName(),Name)==0 )
			return 1;
	return 0;
}

// Matches a '|' separated list of class names along the class chain.
static UBOOL ClassChainIsAny( UObject* Obj, const TCHAR* List )
{
	if( !Obj || !List )
		return 0;
	TCHAR Token[64];
	INT N = 0;
	for( const TCHAR* P=List; ; P++ )
	{
		if( *P=='|' || *P==0 )
		{
			Token[N] = 0;
			if( N && ClassChainIs(Obj,Token) )
				return 1;
			N = 0;
			if( *P==0 )
				break;
		}
		else if( N < 63 )
			Token[N++] = *P;
	}
	return 0;
}

static UBOOL NameMatchesFilter( const TCHAR* Name, const TCHAR* Filter )
{
	if( !Filter )
		return 1;
	TCHAR Token[64];
	INT N = 0;
	for( const TCHAR* P=Filter; ; P++ )
	{
		if( *P=='|' || *P==0 )
		{
			Token[N] = 0;
			if( N && appStrstr(Name,Token) )
				return 1;
			N = 0;
			if( *P==0 )
				break;
		}
		else if( N < 63 )
			Token[N++] = *P;
	}
	return 0;
}

static INT GetIntProp( UObject* Obj, const TCHAR* Name, INT Default=0 )
{
	UIntProperty* P = Obj ? FindField<UIntProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? *(INT*)((BYTE*)Obj + P->Offset) : Default;
}

static UBOOL SetIntProp( UObject* Obj, const TCHAR* Name, INT Value )
{
	UIntProperty* P = Obj ? FindField<UIntProperty>( Obj->GetClass(), Name ) : NULL;
	if( !P )
		return 0;
	*(INT*)((BYTE*)Obj + P->Offset) = Value;
	return 1;
}

static FLOAT GetFloatProp( UObject* Obj, const TCHAR* Name, FLOAT Default=0.f )
{
	UFloatProperty* P = Obj ? FindField<UFloatProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? *(FLOAT*)((BYTE*)Obj + P->Offset) : Default;
}

static UBOOL SetFloatProp( UObject* Obj, const TCHAR* Name, FLOAT Value )
{
	UFloatProperty* P = Obj ? FindField<UFloatProperty>( Obj->GetClass(), Name ) : NULL;
	if( !P )
		return 0;
	*(FLOAT*)((BYTE*)Obj + P->Offset) = Value;
	return 1;
}

static UBOOL GetBoolProp( UObject* Obj, const TCHAR* Name )
{
	UBoolProperty* P = Obj ? FindField<UBoolProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? ((*(BITFIELD*)((BYTE*)Obj + P->Offset) & P->BitMask)!=0) : 0;
}

static UBOOL SetBoolProp( UObject* Obj, const TCHAR* Name, UBOOL Value )
{
	UBoolProperty* P = Obj ? FindField<UBoolProperty>( Obj->GetClass(), Name ) : NULL;
	if( !P )
		return 0;
	BITFIELD& Bits = *(BITFIELD*)((BYTE*)Obj + P->Offset);
	if( Value )
		Bits |= P->BitMask;
	else
		Bits &= ~P->BitMask;
	return 1;
}

static INT GetByteProp( UObject* Obj, const TCHAR* Name, INT Default=0 )
{
	UByteProperty* P = Obj ? FindField<UByteProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? *((BYTE*)Obj + P->Offset) : Default;
}

static UObject* GetObjProp( UObject* Obj, const TCHAR* Name )
{
	UObjectProperty* P = Obj ? FindField<UObjectProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? *(UObject**)((BYTE*)Obj + P->Offset) : NULL;
}

static FName GetStateName( UObject* Obj )
{
	if( Obj && Obj->GetStateFrame() && Obj->GetStateFrame()->StateNode )
		return Obj->GetStateFrame()->StateNode->GetFName();
	return NAME_None;
}

// Parameter access inside a watched function's frame.
static UProperty* FindParm( UFunction* Function, INT Index )
{
	INT I = 0;
	for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
	{
		if( It->PropertyFlags & CPF_ReturnParm )
			continue;
		if( I++==Index )
			return *It;
	}
	return NULL;
}

static UObject* ParmObj( UFunction* F, BYTE* Locals, INT Index )
{
	UObjectProperty* P = Cast<UObjectProperty>( FindParm(F,Index) );
	return P ? *(UObject**)(Locals + P->Offset) : NULL;
}

static INT* ParmIntPtr( UFunction* F, BYTE* Locals, INT Index )
{
	UIntProperty* P = Cast<UIntProperty>( FindParm(F,Index) );
	return P ? (INT*)(Locals + P->Offset) : NULL;
}

static FLOAT* ParmFloatPtr( UFunction* F, BYTE* Locals, INT Index )
{
	UFloatProperty* P = Cast<UFloatProperty>( FindParm(F,Index) );
	return P ? (FLOAT*)(Locals + P->Offset) : NULL;
}

static UBOOL ParmBool( UFunction* F, BYTE* Locals, INT Index )
{
	UBoolProperty* P = Cast<UBoolProperty>( FindParm(F,Index) );
	return P ? ((*(BITFIELD*)(Locals + P->Offset) & P->BitMask)!=0) : 0;
}

static FName ParmName( UFunction* F, BYTE* Locals, INT Index )
{
	UNameProperty* P = Cast<UNameProperty>( FindParm(F,Index) );
	return P ? *(FName*)(Locals + P->Offset) : NAME_None;
}

static UProperty* ParmByName( UFunction* F, const TCHAR* Name )
{
	UProperty* P = FindField<UProperty>( F, Name );
	return P && (P->PropertyFlags & CPF_Parm) ? P : NULL;
}

static const FString* ParmStr( UFunction* F, BYTE* Locals, INT Index )
{
	UStrProperty* P = Cast<UStrProperty>( FindParm(F,Index) );
	return P ? (const FString*)(Locals + P->Offset) : NULL;
}

struct FRpgArg
{
	INT Int;
	FLOAT Float;
	UObject* Obj;
	FName Name;
	FVector Vec;
	FRpgArg() : Int(0), Float(0.f), Obj(NULL), Name(NAME_None), Vec(0,0,0) {}
	static FRpgArg I( INT V ) { FRpgArg A; A.Int=V; A.Float=(FLOAT)V; return A; }
	static FRpgArg F( FLOAT V ) { FRpgArg A; A.Float=V; return A; }
	static FRpgArg O( UObject* V ) { FRpgArg A; A.Obj=V; return A; }
	static FRpgArg N( FName V ) { FRpgArg A; A.Name=V; return A; }
	static FRpgArg V( FVector V ) { FRpgArg A; A.Vec=V; return A; }
};

static void WriteArg( UProperty* P, BYTE* Addr, const FRpgArg& A )
{
	if( Cast<UIntProperty>(P) )         *(INT*)Addr = A.Int;
	else if( Cast<UFloatProperty>(P) )  *(FLOAT*)Addr = A.Float;
	else if( Cast<UByteProperty>(P) )   *(BYTE*)Addr = (BYTE)A.Int;
	else if( Cast<UBoolProperty>(P) )
	{
		if( A.Int )
			*(BITFIELD*)Addr |= ((UBoolProperty*)P)->BitMask;
	}
	else if( Cast<UNameProperty>(P) )   *(FName*)Addr = A.Name;
	else if( Cast<UObjectProperty>(P) ) *(UObject**)Addr = A.Obj;
	else if( Cast<UStructProperty>(P) && P->ElementSize==sizeof(FVector) ) *(FVector*)Addr = A.Vec;
}

static void ReadArg( UProperty* P, BYTE* Addr, FRpgArg& A )
{
	if( Cast<UIntProperty>(P) )         A.Int = *(INT*)Addr;
	else if( Cast<UFloatProperty>(P) )  A.Float = *(FLOAT*)Addr;
	else if( Cast<UByteProperty>(P) )   A.Int = *(BYTE*)Addr;
	else if( Cast<UBoolProperty>(P) )   A.Int = (*(BITFIELD*)Addr & ((UBoolProperty*)P)->BitMask)!=0;
	else if( Cast<UNameProperty>(P) )   A.Name = *(FName*)Addr;
	else if( Cast<UObjectProperty>(P) ) A.Obj = *(UObject**)Addr;
}

// Calls a script function by name. String parameters are left empty.
static UBOOL CallScript( UObject* Obj, const TCHAR* FuncName, const FRpgArg* Args=NULL, INT NumArgs=0, FRpgArg* Ret=NULL )
{
	if( !Obj )
		return 0;
	FName Name( FuncName, FNAME_Find );
	if( Name==NAME_None )
		return 0;
	UFunction* Function = Obj->FindFunction( Name );
	if( !Function || Function->iNative )
		return 0; // ProcessEvent silently ignores final native functions

	BYTE Parms[512];
	if( Function->ParmsSize > sizeof(Parms) )
		return 0;
	appMemzero( Parms, sizeof(Parms) );

	INT ArgIndex = 0;
	for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
	{
		if( It->PropertyFlags & CPF_ReturnParm )
			continue;
		if( Cast<UStrProperty>(*It) && ArgIndex < NumArgs )
			return 0;
		if( ArgIndex < NumArgs )
			WriteArg( *It, Parms + It->Offset, Args[ArgIndex] );
		ArgIndex++;
	}

	Obj->ProcessEvent( Function, Parms );

	if( Ret )
		for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
			if( It->PropertyFlags & CPF_ReturnParm )
				ReadArg( *It, Parms + It->Offset, *Ret );
	return 1;
}

static INT CallInt( UObject* Obj, const TCHAR* FuncName, INT Default=0 )
{
	FRpgArg Ret;
	Ret.Int = Default;
	CallScript( Obj, FuncName, NULL, 0, &Ret );
	return Ret.Int;
}

static UMesh* LoadMesh( const TCHAR* Path )
{
	return Path ? (UMesh*)FRpgUi::LoadRooted( UMesh::StaticClass(), Path ) : NULL;
}

static UTexture* LoadTex( const TCHAR* Path )
{
	return Path ? FRpgUi::LoadTexture( Path ) : NULL;
}

static UClass* LoadParticleClass( const TCHAR* Name )
{
	if( !Name )
		return NULL;
	UClass* Class = FindClass( Name );
	if( !Class )
		Class = (UClass*)FRpgUi::LoadRooted( UClass::StaticClass(), *FString::Printf(TEXT("HPParticle.%s"), Name) );
	return Class;
}

/*-----------------------------------------------------------------------------
	Persistent RPG state.
-----------------------------------------------------------------------------*/

enum ERpgSkill { SKILL_Health, SKILL_Speed, SKILL_Jump, SKILL_Luck, SKILL_MAX };
enum { SKILL_MAX_PIPS = 5, MAX_TROPHIES = 32, MAX_MASTERED = 16 };

static const TCHAR* GSkillNames[SKILL_MAX] = { TEXT("Health"), TEXT("Speed"), TEXT("Jump"), TEXT("Luck") };
static const TCHAR* GSkillDescs[SKILL_MAX] =
{
	TEXT("More maximum health: one extra health bar per point."),
	TEXT("Harry runs faster: +8% per point."),
	TEXT("Harry jumps higher and further: +10% per point."),
	TEXT("Bonus beans on pickup and better prices."),
};

struct FRpgState
{
	INT Xp;
	INT Level;
	INT SkillPoints;
	INT Skill[SKILL_MAX];
	INT AppliedHealthBonus;
	INT Owned[MAX_COSMETICS/32];
	INT Equip[RSLOT_MAX];
	INT Perks;
	INT Kills[CR_MAX];
	INT Counters[CNT_MAX];
	INT TrophyTier[MAX_TROPHIES];
	INT CardRewards;
	INT Trouble;
	INT DetentionSeconds;
	INT Day;
	INT FelixSeconds;
	INT Mastered[MAX_MASTERED];
	INT StockDay;
	INT StockUsed[VEN_MAX*CON_MAX];
	INT TaskDay;
	INT Tasks[TASKS_PER_DAY*3]; // id, goal, baseline per task
	INT TasksDone;
	INT Rep[4];
	INT LetterDay;
	INT Inbox[MAX_INBOX];   // letter id + 1, newest first
	INT LettersRead;        // bit per inbox slot

	void Reset()
	{
		appMemzero( this, sizeof(*this) );
		Level = 1;
		Day = 1;
		StockDay = 1;
	}
};

static INT XpForNextLevel( INT Level )
{
	return 100 + 50*(Level-1);
}

/*-----------------------------------------------------------------------------
	Misc types.
-----------------------------------------------------------------------------*/

enum EToast { TOAST_Info, TOAST_Xp, TOAST_Level, TOAST_House, TOAST_Bean, TOAST_Trophy, TOAST_Trouble, TOAST_Reward };

struct FRpgFeed
{
	FString Text;
	FString Sub;
	FColor Color;
	FLOAT Age;
	INT Kind;
	INT Amount;
};

enum EDeferredKind { DEF_SpawnBeans, DEF_RedispatchSpell };

struct FRpgDeferred
{
	INT Kind;
	AActor* Actor;
	INT Value;
	FVector Location;
};

struct FActorFlags
{
	AActor* Actor;
	FName Name;
	INT Flags;
};

enum EActorFlag
{
	AF_Pushed    = 1<<0,
	AF_Stunned   = 1<<1,
	AF_SpiderStun= 1<<2,
	AF_Plant     = 1<<3,
	AF_Ecto      = 1<<4,
	AF_Peeves    = 1<<5,
	AF_Rewarded  = 1<<6,
	AF_Talked    = 1<<7,
	AF_ImpStun   = 1<<8,
};

enum EMenu { MENU_None, MENU_Shop, MENU_Book };
enum EBookTab { BOOK_Skills, BOOK_Spells, BOOK_Wardrobe, BOOK_Collection, BOOK_Trophies, BOOK_HouseCup, BOOK_Letters, BOOK_Status, BOOK_MAX };
static const TCHAR* GBookTabNames[BOOK_MAX] = { TEXT("Skills"), TEXT("Spells"), TEXT("Wardrobe"), TEXT("Collection"), TEXT("Trophies"), TEXT("House"), TEXT("Post"), TEXT("Status") };

enum EHouse { HOUSE_Gryffindor, HOUSE_Slytherin, HOUSE_Ravenclaw, HOUSE_Hufflepuff, HOUSE_MAX };
static const TCHAR* GHouseNames[HOUSE_MAX] = { TEXT("Gryffindor"), TEXT("Slytherin"), TEXT("Ravenclaw"), TEXT("Hufflepuff") };
static const TCHAR* GHouseGetters[HOUSE_MAX] = { TEXT("GetHPointsG"), TEXT("GetHPointsS"), TEXT("GetHPointsR"), TEXT("GetHPointsH") };
static const TCHAR* GHouseItems[HOUSE_MAX] = { TEXT("StatusItemGryffindorPts"), TEXT("StatusItemSlytherinPts"), TEXT("StatusItemRavenclawPts"), TEXT("StatusItemHufflepuffPts") };
static const TCHAR* GHouseCrests[HOUSE_MAX] =
{
	TEXT("HP2_Menu.Icons.HP2GriffindorCrestSM"), TEXT("HP2_Menu.Icons.HP2SlytherinCrestSM"),
	TEXT("HP2_Menu.Icons.HP2RavenclawCrestSM"), TEXT("HP2_Menu.Icons.HP2HufflepuffCrestSM")
};
static const FColor GHouseColors[HOUSE_MAX] = { FColor(236,72,52,255), FColor(64,200,96,255), FColor(80,140,255,255), FColor(255,214,64,255) };

// One entry in a vendor's offer list for the current day.
enum EOfferKind { OFFER_Consumable, OFFER_Cosmetic, OFFER_Sell };
struct FOffer
{
	INT Kind;
	INT Id;
	INT Tab;
};

// Preview framing for 3D tiles.
enum EPreviewMode { PREV_Body, PREV_Head, PREV_Shoulder, PREV_Item };

/*-----------------------------------------------------------------------------
	The RPG layer.
-----------------------------------------------------------------------------*/

class FHP2Rpg
{
public:
	UBOOL bInitialized;
	UBOOL bEnabled;
	FRpgState State;
	FRpgUi Ui;
	INT BeanMultiplier;

	// Level tracking.
	ULevel* LastLevel;
	APlayerPawn* LastHarry;
	UAnimation* HarryAnim;   // Harry's own animation set, kept for costumes
	FLOAT HarrySince;
	UBOOL bLoadedFromSlot;
	UBOOL bLevelTransition;
	FLOAT LastDayClock;
	INT HealthTargetTried;
	INT LevelSerial;
	FLOAT RpgClock;
	FLOAT SecondTimer;
	FLOAT TroubleTimer;

	// Baselines for diff-based rewards.
	UBOOL bBaselineValid;
	INT LastHouse[HOUSE_MAX];
	INT LastCards;
	INT LastStars;
	INT LastSpells;
	INT PendingHouse[HOUSE_MAX];
	FLOAT PendingHouseTimer[HOUSE_MAX];
	FLOAT PollTimer;
	FLOAT HouseHudTimer;
	INT QuidditchCountedLevel;
	INT LuckyFeedBeans;
	FLOAT LuckyFeedTimer;

	// Per-level bookkeeping.
	TArray<FActorFlags> ActorFlags;
	TArray<FRpgDeferred> Deferred;
	AActor* NearNpc;
	INT NearVendor;
	AActor* LastHitStudent;
	FLOAT LastHitTime;
	FLOAT LastCaughtTime;
	FLOAT LastTalkTime;

	// Feed.
	TArray<FRpgFeed> Feed;
	FString BannerText;
	FString BannerSub;
	FLOAT BannerAge;

	// Menu state.
	INT Menu;
	INT BookTab;
	INT Sel;
	INT Scroll;
	INT ShopTab;
	INT WardrobeSlot;
	INT ConfirmIndex;
	FLOAT PreviewYaw;
	FString Speech;
	FString Notice;
	FLOAT NoticeTime;
	UBOOL bWePaused;
	AActor* ShopVendor;
	INT ShopVendorDef;
	TArray<FOffer> Offers;

	// Watched names.
	FName N_OnSpellHit, N_HandleFlipendo, N_HandleRictusempra, N_HandleDiffindo, N_HandleSkurge;
	FName N_TakeDamage, N_PickupItem, N_ThrowCarrying, N_OnFound, N_PotionEnd, N_ChallengeScores;
	FName N_DuelRanks, N_ComputeScore, N_DeltaHealth, N_BossDefeated, N_DestroyActor, N_LoseHead, N_KillMaim;
	FName DeathStates[10];
	INT NumDeathStates;
	FName N_CaughtHarry;

	// Wardrobe.
	UTexture* HarryTex[4];
	UPalette* OriginalPalette[4];
	UMesh* HarryMesh;
	FLOAT BaseDrawScale;
	UMesh* CostumeApplied;
	TArray<UPalette*> CustomPalettes;
	INT CustomPaletteKeys[64];
	TArray<BYTE> GlassesBackup[8];
	UBOOL bGlassesHidden;
	INT AppliedPalette[4];
	AActor* HatActor;
	INT HatActorKind;
	AActor* PetActor;
	INT PetActorKind;
	AActor* AuraActor;
	INT AuraActorKind;
	AActor* HeldWeapon;
	UMesh* HeldOriginalMesh;
	FLOAT HeldOriginalScale;
	INT FailedSpawn[RSLOT_MAX];
	FVector HatTweak;
	FLOAT HatScaleTweak;
	FLOAT LastRunSpeed, LastAirSpeed, LastJump;

	// Preview actors (hidden, render-only).
	AActor* PreviewBody;
	AActor* PreviewAttach;
	AActor* PreviewItem;

	// UI textures.
	UTexture* Crests[HOUSE_MAX];
	UTexture* BeanIcon;

	FHP2Rpg()
	{
		appMemzero( this, sizeof(*this) );
	}

	/*-------------------------------------------------------------------------
		Lifetime.
	-------------------------------------------------------------------------*/

	void Init()
	{
		if( bInitialized )
			return;
		bInitialized = 1;
		bEnabled = 1;
		UBOOL Value = 1;
		if( GConfig->GetBool( TEXT("HP2Rpg"), TEXT("Enabled"), Value ) )
			bEnabled = Value;
		BeanMultiplier = 1;
		INT ConfigMultiplier = 0;
		if( GConfig->GetInt( TEXT("HP2Rpg"), TEXT("BeanMultiplier"), ConfigMultiplier ) )
			BeanMultiplier = Clamp( ConfigMultiplier, 1, 5 );
		State.Reset();
		Sanitize();
		for( INT i=0; i<4; i++ )
			AppliedPalette[i] = -1;
		for( INT i=0; i<RSLOT_MAX; i++ )
			FailedSpawn[i] = -1;
		HatScaleTweak = 1.f;
		ConfirmIndex = -1;
		SpellsKnownNotified = -1;
		QuidditchCountedLevel = -1;
		HatActorKind = PetActorKind = AuraActorKind = -1;

		N_OnSpellHit = FName(TEXT("OnSpellHit"));
		N_HandleFlipendo = FName(TEXT("HandleSpellFlipendo"));
		N_HandleRictusempra = FName(TEXT("HandleSpellRictusempra"));
		N_HandleDiffindo = FName(TEXT("HandleSpellDiffindo"));
		N_HandleSkurge = FName(TEXT("HandleSpellSkurge"));
		N_TakeDamage = FName(TEXT("TakeDamage"));
		N_PickupItem = FName(TEXT("PickupItem"));
		N_ThrowCarrying = FName(TEXT("ThrowCarryingActor"));
		N_OnFound = FName(TEXT("OnFound"));
		N_PotionEnd = FName(TEXT("DoPotionMixingEnd"));
		N_ChallengeScores = FName(TEXT("UpdateChallengeScores"));
		N_DuelRanks = FName(TEXT("UpdateDuelingRanks"));
		N_ComputeScore = FName(TEXT("ComputeScore"));
		N_DeltaHealth = FName(TEXT("DeltaHealth"));
		N_BossDefeated = FName(TEXT("SendDefeatedTrigger"));
		N_DestroyActor = FName(TEXT("DestroyActor"));
		N_LoseHead = FName(TEXT("LoseHead"));
		N_KillMaim = FName(TEXT("KillMaimDestroy"));
		const FName Watched[] =
		{
			N_OnSpellHit, N_HandleFlipendo, N_HandleRictusempra, N_HandleDiffindo, N_HandleSkurge, N_TakeDamage,
			N_PickupItem, N_ThrowCarrying, N_OnFound, N_PotionEnd, N_ChallengeScores, N_DuelRanks, N_ComputeScore,
			N_DeltaHealth, N_BossDefeated, N_DestroyActor, N_LoseHead, N_KillMaim
		};
		for( INT i=0; i<ARRAY_COUNT(Watched); i++ )
			HP2WatchScriptName( Watched[i].GetIndex() );

		static const TCHAR* Deaths[] =
		{
			TEXT("stateDied"), TEXT("BlowUpAndDie"), TEXT("DeadSpider"), TEXT("OutForTheCount"), TEXT("KillDoxie"),
			TEXT("stayFlipped"), TEXT("stateCrushSpider"), TEXT("FallOverLedge"), TEXT("Wilted"), TEXT("stateHiding")
		};
		NumDeathStates = ARRAY_COUNT(Deaths);
		for( INT i=0; i<NumDeathStates; i++ )
			DeathStates[i] = FName( Deaths[i] );
		N_CaughtHarry = FName( TEXT("CaughtHarry") );

		GHP2ScriptWatch = bEnabled ? &FHP2Rpg::StaticOnScriptCall : NULL;
		GHP2StateWatch = bEnabled ? &FHP2Rpg::StaticOnStateChange : NULL;
		RpgLog( TEXT("enabled=%i beans x%i"), bEnabled, BeanMultiplier );
	}

	static void StaticOnScriptCall( UObject* Self, UFunction* Function, BYTE* Locals, UObject* Caller );
	static void StaticOnStateChange( UObject* Self, FName OldState, FName NewState );

	APlayerPawn* GetHarry( UGameEngine* Engine )
	{
		if( !Engine->GLevel || !Engine->Client || !Engine->Client->Viewports.Num() )
			return NULL;
		APlayerPawn* Pawn = Engine->Client->Viewports(0)->Actor;
		if( !Pawn || Pawn->bDeleteMe )
			return NULL;
		static UClass* HarryClass = NULL;
		if( !HarryClass )
			HarryClass = FindClass( TEXT("harry") );
		if( !HarryClass || !Pawn->IsA(HarryClass) )
			return NULL;
		return Pawn;
	}

	APlayerPawn* CurrentHarry()
	{
		return LastHarry && !LastHarry->bDeleteMe ? LastHarry : NULL;
	}

	UObject* GetStatus( APlayerPawn* Harry )
	{
		return GetObjProp( Harry, TEXT("managerStatus") );
	}

	/*-------------------------------------------------------------------------
		Actor flags (per level).
	-------------------------------------------------------------------------*/

	INT& FlagsFor( AActor* Actor )
	{
		// Freed actors' addresses get reused, so match the object name too.
		const FName Name = Actor->GetFName();
		for( INT i=0; i<ActorFlags.Num(); i++ )
			if( ActorFlags(i).Actor==Actor )
			{
				if( ActorFlags(i).Name==Name )
					return ActorFlags(i).Flags;
				ActorFlags(i).Name = Name;
				ActorFlags(i).Flags = 0;
				return ActorFlags(i).Flags;
			}
		FActorFlags* Entry = new(ActorFlags) FActorFlags;
		Entry->Actor = Actor;
		Entry->Name = Name;
		Entry->Flags = 0;
		return Entry->Flags;
	}

	UBOOL TestAndSet( AActor* Actor, INT Flag )
	{
		INT& F = FlagsFor( Actor );
		if( F & Flag )
			return 1;
		F |= Flag;
		return 0;
	}

	/*-------------------------------------------------------------------------
		Persistence.
	-------------------------------------------------------------------------*/

	FString SidecarPath( INT Slot )
	{
		return FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Save%i.rpg"), *GSys->SaveSlotPath, Slot );
	}

	static FString IntList( const INT* Values, INT Num )
	{
		FString Out;
		for( INT i=0; i<Num; i++ )
			Out += FString::Printf( TEXT("%s%i"), i ? TEXT(",") : TEXT(""), Values[i] );
		return Out;
	}

	static void ParseIntList( const TCHAR* Stream, const TCHAR* Key, INT* Values, INT Num )
	{
		const TCHAR* P = appStrfind( Stream, Key );
		if( !P )
			return;
		P += appStrlen( Key );
		for( INT i=0; i<Num; i++ )
		{
			Values[i] = appAtoi( P );
			while( *P && *P!=',' && *P!=' ' && *P!='\r' && *P!='\n' )
				P++;
			if( *P!=',' )
				break;
			P++;
		}
	}

	void SaveSlot( INT Slot )
	{
		FString Text = FString::Printf
		(
			TEXT("HP2RPG Version=2 Xp=%i Level=%i SkillPoints=%i HealthBonus=%i Perks=%i CardRewards=%i Trouble=%i Detention=%i Day=%i Felix=%i StockDay=%i\r\n"),
			State.Xp, State.Level, State.SkillPoints, State.AppliedHealthBonus, State.Perks, State.CardRewards,
			State.Trouble, State.DetentionSeconds, State.Day, State.FelixSeconds, State.StockDay
		);
		Text += FString(TEXT("Skills=")) + IntList(State.Skill,SKILL_MAX) + TEXT("\r\n");
		Text += FString(TEXT("Owned=")) + IntList(State.Owned,MAX_COSMETICS/32) + TEXT("\r\n");
		Text += FString(TEXT("Equip=")) + IntList(State.Equip,RSLOT_MAX) + TEXT("\r\n");
		Text += FString(TEXT("Kills=")) + IntList(State.Kills,CR_MAX) + TEXT("\r\n");
		Text += FString(TEXT("Counters=")) + IntList(State.Counters,CNT_MAX) + TEXT("\r\n");
		Text += FString(TEXT("Trophies=")) + IntList(State.TrophyTier,MAX_TROPHIES) + TEXT("\r\n");
		Text += FString(TEXT("Mastered=")) + IntList(State.Mastered,MAX_MASTERED) + TEXT("\r\n");
		Text += FString(TEXT("Stock=")) + IntList(State.StockUsed,VEN_MAX*CON_MAX) + TEXT("\r\n");
		Text += FString::Printf(TEXT("TaskDay=%i TasksDone=%i\r\n"), State.TaskDay, State.TasksDone);
		Text += FString(TEXT("Tasks=")) + IntList(State.Tasks,TASKS_PER_DAY*3) + TEXT("\r\n");
		Text += FString(TEXT("Rep=")) + IntList(State.Rep,4) + TEXT("\r\n");
		Text += FString::Printf(TEXT("LetterDay=%i LettersRead=%i\r\n"), State.LetterDay, State.LettersRead);
		Text += FString(TEXT("Inbox=")) + IntList(State.Inbox,MAX_INBOX) + TEXT("\r\n");
		const FString Path = SidecarPath( Slot );
		if( !appSaveStringToFile( Text, *Path ) )
			RpgLog( TEXT("could not write %s"), *Path );
	}

	void LoadSlot( INT Slot )
	{
		State.Reset();
		FString Text;
		const FString Path = SidecarPath( Slot );
		if( appLoadFileToString( Text, *Path ) )
		{
			const TCHAR* S = *Text;
			INT Version = 1;
			Parse( S, TEXT("Version="), Version );
			Parse( S, TEXT("Xp="), State.Xp );
			Parse( S, TEXT("Level="), State.Level );
			Parse( S, TEXT("SkillPoints="), State.SkillPoints );
			Parse( S, TEXT("HealthBonus="), State.AppliedHealthBonus );
			if( Version<2 )
			{
				// Version 1 stored skills by name and used other cosmetic ids.
				Parse( S, TEXT("Health="), State.Skill[SKILL_Health] );
				Parse( S, TEXT("Speed="), State.Skill[SKILL_Speed] );
				Parse( S, TEXT("Jump="), State.Skill[SKILL_Jump] );
				Parse( S, TEXT("Luck="), State.Skill[SKILL_Luck] );
				INT OldOwned = 0, OldHat = 0;
				Parse( S, TEXT("Owned="), OldOwned );
				Parse( S, TEXT("Hat="), OldHat );
				// v1 bit 5 was the Sorting Hat; carry it over.
				if( OldOwned & (1<<5) )
					SetOwned( FindCosmetic(TEXT("Sorting Hat")) );
				if( OldHat==1 )
					State.Equip[RSLOT_Hat] = FindCosmetic( TEXT("Sorting Hat") );
			}
			else
			{
				Parse( S, TEXT("Perks="), State.Perks );
				Parse( S, TEXT("CardRewards="), State.CardRewards );
				Parse( S, TEXT("Trouble="), State.Trouble );
				Parse( S, TEXT("Detention="), State.DetentionSeconds );
				Parse( S, TEXT("Day="), State.Day );
				Parse( S, TEXT("Felix="), State.FelixSeconds );
				Parse( S, TEXT("StockDay="), State.StockDay );
				ParseIntList( S, TEXT("Skills="), State.Skill, SKILL_MAX );
				ParseIntList( S, TEXT("Owned="), State.Owned, MAX_COSMETICS/32 );
				ParseIntList( S, TEXT("Equip="), State.Equip, RSLOT_MAX );
				ParseIntList( S, TEXT("Kills="), State.Kills, CR_MAX );
				ParseIntList( S, TEXT("Counters="), State.Counters, CNT_MAX );
				ParseIntList( S, TEXT("Trophies="), State.TrophyTier, MAX_TROPHIES );
				ParseIntList( S, TEXT("Mastered="), State.Mastered, MAX_MASTERED );
				ParseIntList( S, TEXT("Stock="), State.StockUsed, VEN_MAX*CON_MAX );
				Parse( S, TEXT("TaskDay="), State.TaskDay );
				Parse( S, TEXT("TasksDone="), State.TasksDone );
				ParseIntList( S, TEXT("Tasks="), State.Tasks, TASKS_PER_DAY*3 );
				ParseIntList( S, TEXT("Rep="), State.Rep, 4 );
				Parse( S, TEXT("LetterDay="), State.LetterDay );
				Parse( S, TEXT("LettersRead="), State.LettersRead );
				ParseIntList( S, TEXT("Inbox="), State.Inbox, MAX_INBOX );
			}
		}
		Sanitize();
		MarkWardrobeDirty();
		SpellsKnownNotified = -1;
		RpgLog( TEXT("loaded slot %i: level %i xp %i points %i"), Slot, State.Level, State.Xp, State.SkillPoints );
	}

	void Sanitize()
	{
		State.Level = Clamp( State.Level, 1, 99 );
		State.Xp = Max( State.Xp, 0 );
		State.SkillPoints = Clamp( State.SkillPoints, 0, 199 );
		State.Day = Max( State.Day, 1 );
		State.Trouble = Clamp( State.Trouble, 0, 100 );
		for( INT i=0; i<SKILL_MAX; i++ )
			State.Skill[i] = Clamp( State.Skill[i], 0, (INT)SKILL_MAX_PIPS );
		for( INT i=0; i<MAX_TROPHIES; i++ )
			State.TrophyTier[i] = Clamp( State.TrophyTier[i], 0, 3 );
		for( INT i=0; i<CR_MAX; i++ )
			State.Kills[i] = Max( State.Kills[i], 0 );
		for( INT i=0; i<CNT_MAX; i++ )
			State.Counters[i] = Max( State.Counters[i], 0 );
		State.DetentionSeconds = Clamp( State.DetentionSeconds, 0, 3600 );
		State.FelixSeconds = Clamp( State.FelixSeconds, 0, 3600 );
		for( INT t=0; t<TASKS_PER_DAY; t++ )
			if( State.Tasks[t*3]<0 || State.Tasks[t*3]>=GNumTasks )
				State.TaskDay = 0; // re-roll
		for( INT h=0; h<4; h++ )
			State.Rep[h] = Clamp( State.Rep[h], -100, 100 );
		INT Kept = 0;
		for( INT i=0; i<MAX_INBOX; i++ )
			if( State.Inbox[i]>0 && State.Inbox[i]<=GNumLetters )
				State.Inbox[Kept++] = State.Inbox[i];
		for( INT i=Kept; i<MAX_INBOX; i++ )
			State.Inbox[i] = 0;
		for( INT s=0; s<RSLOT_MAX; s++ )
		{
			const INT Id = State.Equip[s];
			if( Id<0 || Id>=GNumCosmetics || GCosmetics[Id].Slot!=s || !IsOwned(Id) )
				State.Equip[s] = DefaultCosmetic( s );
		}
	}

	/*-------------------------------------------------------------------------
		Cosmetics bookkeeping.
	-------------------------------------------------------------------------*/

	static INT FindCosmetic( const TCHAR* Name )
	{
		for( INT i=0; i<GNumCosmetics; i++ )
			if( appStricmp(GCosmetics[i].Name,Name)==0 )
				return i;
		return 0;
	}

	static INT DefaultCosmetic( INT Slot )
	{
		for( INT i=0; i<GNumCosmetics; i++ )
			if( GCosmetics[i].Slot==Slot && GCosmetics[i].Price==0 )
				return i;
		return 0;
	}

	UBOOL IsOwned( INT Id )
	{
		if( Id<0 || Id>=GNumCosmetics )
			return 0;
		return GCosmetics[Id].Price==0 || (State.Owned[Id>>5] & (1<<(Id&31)))!=0;
	}

	void SetOwned( INT Id )
	{
		if( Id>=0 && Id<GNumCosmetics )
			State.Owned[Id>>5] |= (1<<(Id&31));
	}

	INT CountOwnedCosmetics()
	{
		INT N = 0;
		for( INT i=0; i<GNumCosmetics; i++ )
			if( GCosmetics[i].Price>0 && IsOwned(i) )
				N++;
		return N;
	}

	/*-------------------------------------------------------------------------
		Stats.
	-------------------------------------------------------------------------*/

	INT GetStat( INT Stat )
	{
		INT Total = 0;
		for( INT s=0; s<RSLOT_MAX; s++ )
		{
			const FCosmetic& C = GCosmetics[State.Equip[s]];
			if( C.Stat==Stat )
				Total += C.StatValue;
		}
		for( INT r=0; r<GNumCardRewards; r++ )
			if( (State.CardRewards & (1<<r)) && GCardRewards[r].Stat==Stat )
				Total += GCardRewards[r].StatValue;
		if( Stat==STAT_Discount )
			Total += 2*State.Skill[SKILL_Luck];
		if( Stat==STAT_BeanFind )
			Total += 8*State.Skill[SKILL_Luck];
		if( State.FelixSeconds>0 && (Stat==STAT_BeanFind || Stat==STAT_XpGain) )
			Total += 100;
		return Total;
	}

	FLOAT SpellPower()
	{
		return 1.f + GetStat(STAT_SpellPower)/100.f;
	}

	UBOOL HasPerk( INT Perk )
	{
		return (State.Perks & (1<<Perk))!=0;
	}

	INT CountPerks()
	{
		INT N = 0;
		for( INT p=0; p<PERK_MAX; p++ )
			if( HasPerk(p) )
				N++;
		return N;
	}

	/*-------------------------------------------------------------------------
		Feed.
	-------------------------------------------------------------------------*/

	void AddFeed( const FString& Text, FColor Color, INT Kind=TOAST_Info, const TCHAR* Sub=NULL )
	{
		if( Kind==TOAST_Level )
		{
			BannerText = Text;
			BannerSub = Sub ? Sub : TEXT("");
			BannerAge = 0.f;
			return;
		}
		// Repeats refresh the existing card instead of stacking.
		for( INT i=0; i<Feed.Num(); i++ )
			if( Feed(i).Kind==Kind && Feed(i).Text==Text && Kind!=TOAST_House )
			{
				Feed(i).Age = Min( Feed(i).Age, 0.2f );
				return;
			}
		if( Feed.Num()>=3 )
			Feed.Remove( 0 );
		FRpgFeed* Entry = new(Feed) FRpgFeed;
		Entry->Text = Text;
		Entry->Sub = Sub ? Sub : TEXT("");
		Entry->Color = Color;
		Entry->Age = 0.f;
		Entry->Kind = Kind;
		Entry->Amount = 0;
	}

	// XP gained in quick succession is merged into one card.
	void AddXpToast( INT Amount, const TCHAR* Reason )
	{
		if( Feed.Num() )
		{
			FRpgFeed& Last = Feed(Feed.Num()-1);
			if( Last.Kind==TOAST_Xp && Last.Age<1.5f )
			{
				Last.Amount += Amount;
				Last.Text = FString::Printf( TEXT("+%i XP"), Last.Amount );
				Last.Sub = Reason;
				Last.Age = Min( Last.Age, 0.3f );
				return;
			}
		}
		AddFeed( FString::Printf(TEXT("+%i XP"), Amount), FColor(120,190,255,255), TOAST_Xp, Reason );
		Feed(Feed.Num()-1).Amount = Amount;
	}

	/*-------------------------------------------------------------------------
		Experience, beans, house points, trouble.
	-------------------------------------------------------------------------*/

	void GiveXp( INT Amount, const TCHAR* Reason )
	{
		if( Amount<=0 )
			return;
		Amount = Amount*(100 + GetStat(STAT_XpGain))/100;
		State.Xp += Amount;
		AddXpToast( Amount, Reason );
		while( State.Xp >= XpForNextLevel(State.Level) && State.Level<99 )
		{
			State.Xp -= XpForNextLevel( State.Level );
			State.Level++;
			State.SkillPoints++;
			AddFeed( FString::Printf(TEXT("Level %i"), State.Level), FColor(255,220,90,255), TOAST_Level, TEXT("+1 skill point  -  press K") );
			Ui.Play( RSND_Reward, CurrentHarry() );
			AddHousePoints( CurrentHarry(), HOUSE_Gryffindor, 5 );
		}
	}

	void GiveBeans( APlayerPawn* Harry, INT Amount )
	{
		UObject* Status = GetStatus( Harry );
		if( !Status || !Amount )
			return;
		FRpgArg Arg = FRpgArg::I( Amount );
		CallScript( Status, TEXT("AddBeans"), &Arg, 1 );
	}

	INT GetBeans( APlayerPawn* Harry )
	{
		return CallInt( GetStatus(Harry), TEXT("GetBeanCount") );
	}

	void AddHousePoints( APlayerPawn* Harry, INT House, INT Points )
	{
		UObject* Status = GetStatus( Harry );
		UClass* Group = FindClass( TEXT("StatusGroupHousePoints") );
		UClass* Item = FindClass( GHouseItems[House] );
		if( !Status || !Group || !Item )
			return;
		FRpgArg Args[3] = { FRpgArg::O(Group), FRpgArg::O(Item), FRpgArg::I(Points) };
		CallScript( Status, TEXT("IncrementCount"), Args, 3 );
	}

	void AddTrouble( INT Amount, const TCHAR* Reason )
	{
		const INT Before = State.Trouble;
		State.Trouble = Clamp( State.Trouble + Amount, 0, 100 );
		if( Amount>0 )
			AddFeed( FString::Printf(TEXT("Trouble +%i"), Amount), FColor(255,150,90,255), TOAST_Trouble, Reason );
		if( Before<70 && State.Trouble>=70 && State.Trouble<100 )
			AddFeed( TEXT("Careful!"), FColor(255,110,80,255), TOAST_Trouble, TEXT("A little more and it's detention.") );
		if( State.Trouble>=100 )
			GiveDetention();
	}

	void GiveDetention()
	{
		APlayerPawn* Harry = CurrentHarry();
		State.Trouble = 40;
		State.DetentionSeconds = 600;
		State.Counters[CNT_Detentions]++;
		AddHousePoints( Harry, HOUSE_Gryffindor, -20 );
		const INT Fine = Min( 50, GetBeans(Harry) );
		GiveBeans( Harry, -Fine );
		AddFeed( TEXT("Detention!"), FColor(255,90,70,255), TOAST_Trouble, *FString::Printf(TEXT("-20 points, -%i beans, no trading for 10 min"), Fine) );
		Ui.Play( RSND_Deny, Harry );
		RpgLog( TEXT("detention #%i"), State.Counters[CNT_Detentions] );
	}

	/*-------------------------------------------------------------------------
		Polling rewards (house points, cards, stars, spells).
	-------------------------------------------------------------------------*/

	INT GetStatusCount( UObject* Status, const TCHAR* GroupName, const TCHAR* ItemName )
	{
		UClass* Group = FindClass( GroupName );
		UClass* Item = FindClass( ItemName );
		if( !Status || !Group || !Item )
			return 0;
		FRpgArg Args[2] = { FRpgArg::O(Group), FRpgArg::O(Item) };
		FRpgArg Ret;
		if( !CallScript( Status, TEXT("GetStatusItem"), Args, 2, &Ret ) || !Ret.Obj )
			return 0;
		return GetIntProp( Ret.Obj, TEXT("nCount") );
	}

	INT CountCards( UObject* Status, INT Kind )
	{
		static const TCHAR* Items[3] = { TEXT("StatusItemBronzeCards"), TEXT("StatusItemSilverCards"), TEXT("StatusItemGoldCards") };
		return GetStatusCount( Status, TEXT("StatusGroupWizardCards"), Items[Kind] );
	}

	INT CountSpells( APlayerPawn* Harry )
	{
		UObjectProperty* P = FindField<UObjectProperty>( Harry->GetClass(), TEXT("SpellBook") );
		if( !P )
			return 0;
		INT Count = 0;
		for( INT i=0; i<P->ArrayDim; i++ )
			if( *(UObject**)((BYTE*)Harry + P->Offset + i*P->ElementSize) )
				Count++;
		return Count;
	}

	void PollRewards( APlayerPawn* Harry )
	{
		UObject* Status = GetStatus( Harry );
		if( !Status )
			return;

		INT House[HOUSE_MAX];
		for( INT i=0; i<HOUSE_MAX; i++ )
			House[i] = CallInt( Status, GHouseGetters[i] );
		const INT Cards = CountCards(Status,CARD_Bronze) + CountCards(Status,CARD_Silver) + CountCards(Status,CARD_Gold);
		const INT Stars = GetStatusCount( Status, TEXT("StatusGroupStars"), TEXT("StatusItemStars") );
		const INT Spells = CountSpells( Harry );

		if( !bBaselineValid )
		{
			bBaselineValid = 1;
			for( INT i=0; i<HOUSE_MAX; i++ )
			{
				LastHouse[i] = House[i];
				PendingHouse[i] = 0;
			}
			LastCards = Cards;
			LastStars = Stars;
			LastSpells = Spells;
			return;
		}

		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			const INT Diff = House[i] - LastHouse[i];
			if( Diff )
			{
				PendingHouse[i] += Diff;
				PendingHouseTimer[i] = 0.f;
				HouseHudTimer = 6.f;
				if( i==HOUSE_Gryffindor && Diff>0 )
				{
					State.Counters[CNT_HousePoints] += Diff;
					GiveXp( Min( Diff/2, 25 ), TEXT("house points") );
				}
			}
			LastHouse[i] = House[i];
		}

		if( Cards > LastCards )
			GiveXp( 25*(Cards-LastCards), TEXT("wizard card") );
		LastCards = Cards;

		if( Stars > LastStars )
			GiveXp( 5*(Stars-LastStars), TEXT("challenge star") );
		LastStars = Stars;

		if( Spells > LastSpells )
			GiveXp( 100*(Spells-LastSpells), TEXT("new spell learned") );
		LastSpells = Spells;
	}

	void FlushHouseFeed( FLOAT DeltaSeconds )
	{
		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			if( !PendingHouse[i] )
				continue;
			PendingHouseTimer[i] += DeltaSeconds;
			if( PendingHouseTimer[i] < 0.8f )
				continue;
			const INT P = PendingHouse[i];
			const FString Text = P>0
				? FString::Printf( TEXT("+%i %s"), P, GHouseNames[i] )
				: FString::Printf( TEXT("%s loses %i point%s"), GHouseNames[i], -P, P==-1 ? TEXT("") : TEXT("s") );
			AddFeed( Text, GHouseColors[i], TOAST_House, TEXT("House points") );
			Feed(Feed.Num()-1).Amount = i;
			PendingHouse[i] = 0;
		}
	}

	/*-------------------------------------------------------------------------
		Collections and trophies (checked once a second).
	-------------------------------------------------------------------------*/

	void CheckCardRewards( APlayerPawn* Harry )
	{
		UObject* Status = GetStatus( Harry );
		if( !Status )
			return;
		INT Owned[3];
		for( INT k=0; k<3; k++ )
			Owned[k] = CountCards( Status, k );
		for( INT r=0; r<GNumCardRewards; r++ )
		{
			const FCardReward& R = GCardRewards[r];
			if( (State.CardRewards & (1<<r)) || Owned[R.Kind] < R.Count )
				continue;
			State.CardRewards |= (1<<r);
			State.SkillPoints += R.SkillPoints;
			FString Bonus = R.Stat!=STAT_None ? FString::Printf( GStatFormat[R.Stat], R.StatValue ) : FString();
			if( R.SkillPoints )
				Bonus += FString::Printf( TEXT("%s+%i skill point%s"), Bonus.Len() ? TEXT(", ") : TEXT(""), R.SkillPoints, R.SkillPoints>1 ? TEXT("s") : TEXT("") );
			AddFeed( FString::Printf(TEXT("%s"), R.Name), FColor(255,214,120,255), TOAST_Reward, *(FString(TEXT("Card collection  -  ")) + Bonus) );
			Ui.Play( RSND_Reward, Harry );
		}
	}

	INT CounterValue( APlayerPawn* Harry, INT Counter )
	{
		switch( Counter )
		{
			case CNT_Level: return State.Level;
			case CNT_Cosmetics: return CountOwnedCosmetics();
			case CNT_Perks: return CountPerks();
			case CNT_Cards:
			{
				UObject* Status = GetStatus( Harry );
				return CountCards(Status,CARD_Bronze) + CountCards(Status,CARD_Silver) + CountCards(Status,CARD_Gold);
			}
			default: return State.Counters[Counter];
		}
	}

	/*-------------------------------------------------------------------------
		Daily tasks and the rival houses.
	-------------------------------------------------------------------------*/

	void RollTasks( APlayerPawn* Harry )
	{
		const INT Tier = Min( 2, (State.Level-1)/6 );
		INT Picked = 0;
		for( INT Try=0; Picked<TASKS_PER_DAY && Try<64; Try++ )
		{
			const INT Id = (INT)(HashMix( (DWORD)State.Day*977u, (DWORD)Try ) % (DWORD)GNumTasks);
			UBOOL bDup = 0;
			for( INT p=0; p<Picked; p++ )
				if( State.Tasks[p*3]==Id )
					bDup = 1;
			if( bDup )
				continue;
			State.Tasks[Picked*3+0] = Id;
			State.Tasks[Picked*3+1] = GTasks[Id].Goals[Tier];
			State.Tasks[Picked*3+2] = CounterValue( Harry, GTasks[Id].Counter );
			Picked++;
		}
		State.TasksDone = 0;
		State.TaskDay = State.Day;
	}

	void SimulateRivals( APlayerPawn* Harry )
	{
		const INT Gains[3] = { 15 + RpgRand(31), 10 + RpgRand(26), 8 + RpgRand(23) };
		AddHousePoints( Harry, HOUSE_Slytherin, Gains[0] );
		AddHousePoints( Harry, HOUSE_Ravenclaw, Gains[1] );
		AddHousePoints( Harry, HOUSE_Hufflepuff, Gains[2] );
		bBaselineValid = 0; // don't announce these as separate house point cards
		AddFeed( TEXT("House Cup update"), FColor(255,214,120,255), TOAST_House,
			*FString::Printf(TEXT("Slytherin +%i, Ravenclaw +%i, Hufflepuff +%i"), Gains[0], Gains[1], Gains[2]) );
		Feed(Feed.Num()-1).Amount = HOUSE_Slytherin;
	}

	/*-------------------------------------------------------------------------
		Owl post and house reputation.
	-------------------------------------------------------------------------*/

	UBOOL HasLetter( INT Id )
	{
		for( INT i=0; i<MAX_INBOX; i++ )
			if( State.Inbox[i]==Id+1 )
				return 1;
		return 0;
	}

	void DeliverLetter( APlayerPawn* Harry )
	{
		INT Candidates[64];
		INT N = 0;
		for( INT l=0; l<GNumLetters && N<64; l++ )
			if( GLetters[l].MinLevel<=State.Level && !HasLetter(l) )
				Candidates[N++] = l;
		if( !N )
			return;
		const INT Id = Candidates[ HashMix((DWORD)State.Day,77u) % (DWORD)N ];
		for( INT i=MAX_INBOX-1; i>0; i-- )
			State.Inbox[i] = State.Inbox[i-1];
		State.Inbox[0] = Id+1;
		State.LettersRead = (State.LettersRead<<1) & ((1<<MAX_INBOX)-1);
		const FLetterDef& L = GLetters[Id];
		FString Gift;
		switch( L.Gift )
		{
			case GIFT_Beans:      GiveBeans( Harry, L.GiftAmount ); Gift = FString::Printf( TEXT("  +%i beans"), L.GiftAmount ); break;
			case GIFT_Potion:     ApplyConsumable( Harry, CON_Potion, L.GiftAmount ); Gift = TEXT("  +Wiggenweld potion"); break;
			case GIFT_ChocFrog:   ApplyConsumable( Harry, CON_ChocFrog, 1 ); Gift = TEXT("  +Chocolate Frog (health restored)"); break;
			case GIFT_BronzeCard: ApplyConsumable( Harry, CON_BronzeCard, 1 ); Gift = TEXT("  +Wizard Card"); break;
			case GIFT_SkillPoint: State.SkillPoints += L.GiftAmount; Gift = TEXT("  +1 skill point"); break;
			default: break;
		}
		AddFeed( FString::Printf(TEXT("Owl post from %s"), L.From), FColor(230,210,170,255), TOAST_Reward, *(FString(L.Subject) + Gift) );
	}

	static const TCHAR* RepTier( INT Rep )
	{
		return Rep<=-30 ? TEXT("Hostile") : (Rep<10 ? TEXT("Neutral") : (Rep<40 ? TEXT("Friendly") : TEXT("Honoured")));
	}

	// Price change in % at that house's student vendors (negative = discount).
	static INT RepPriceDelta( INT Rep )
	{
		return Rep<=-30 ? 20 : (Rep<10 ? 0 : (Rep<40 ? -5 : -12));
	}

	void AddRep( INT House, INT Amount )
	{
		const TCHAR* Before = RepTier( State.Rep[House] );
		State.Rep[House] = Clamp( State.Rep[House]+Amount, -100, 100 );
		const TCHAR* After = RepTier( State.Rep[House] );
		if( Before!=After )
		{
			AddFeed( FString::Printf(TEXT("%s: %s"), GHouseNames[House], After), GHouseColors[House], TOAST_House, TEXT("House reputation changed") );
			Feed(Feed.Num()-1).Amount = House;
		}
	}

	void UpdateTasks( APlayerPawn* Harry )
	{
		if( State.LetterDay!=State.Day )
		{
			State.LetterDay = State.Day;
			DeliverLetter( Harry );
		}
		if( State.TaskDay!=State.Day )
		{
			const UBOOL bNewDay = State.TaskDay!=0;
			RollTasks( Harry );
			if( bNewDay )
			{
				SimulateRivals( Harry );
				AddFeed( FString::Printf(TEXT("School day %i"), State.Day), FColor(255,220,90,255), TOAST_Level, TEXT("New house tasks - press K, House Cup") );
			}
			return;
		}
		for( INT t=0; t<TASKS_PER_DAY; t++ )
		{
			if( State.TasksDone & (1<<t) )
				continue;
			const FTaskDef& T = GTasks[State.Tasks[t*3]];
			if( CounterValue(Harry,T.Counter) - State.Tasks[t*3+2] < State.Tasks[t*3+1] )
				continue;
			State.TasksDone |= (1<<t);
			AddHousePoints( Harry, HOUSE_Gryffindor, T.Points );
			GiveBeans( Harry, T.Beans );
			AddFeed( TEXT("Task complete!"), FColor(120,220,140,255), TOAST_Reward,
				*FString::Printf(TEXT("+%i points, +%i beans"), T.Points, T.Beans) );
			GiveXp( T.Xp, TEXT("house task") );
			Ui.Play( RSND_Reward, Harry );
		}
	}

	void CheckTrophies( APlayerPawn* Harry )
	{
		for( INT t=0; t<GNumTrophies && t<MAX_TROPHIES; t++ )
		{
			const FTrophyDef& T = GTrophies[t];
			const INT Value = CounterValue( Harry, T.Counter );
			INT Tier = 0;
			while( Tier<3 && Value>=T.Goals[Tier] )
				Tier++;
			while( State.TrophyTier[t] < Tier )
			{
				const INT Won = State.TrophyTier[t]++;
				AddFeed( FString::Printf(TEXT("%s"), T.Name), GTrophyTierColors[Won], TOAST_Trophy, *FString::Printf(TEXT("%s trophy unlocked"), GTrophyTierNames[Won]) );
				GiveXp( GTrophyXp[Won], TEXT("trophy") );
				GiveBeans( Harry, GTrophyBeans[Won] );
				Ui.Play( RSND_Reward, Harry );
			}
		}
	}

	/*-------------------------------------------------------------------------
		Bestiary.
	-------------------------------------------------------------------------*/

	INT CreatureType( UObject* Obj )
	{
		if( !Obj )
			return -1;
		for( INT c=0; c<CR_MAX; c++ )
			if( ClassChainIsAny(Obj,GCreatures[c].Classes) )
				return c;
		return -1;
	}

	INT BestiaryTier( INT Type )
	{
		INT Tier = 0;
		while( Tier<3 && State.Kills[Type]>=GBestiaryTiers[Tier] )
			Tier++;
		return Tier;
	}

	void CreditKill( AActor* Creature, INT Type )
	{
		if( !Creature || Type<0 || TestAndSet(Creature,AF_Rewarded) )
			return;
		const INT OldTier = BestiaryTier( Type );
		State.Kills[Type]++;
		State.Counters[CNT_Creatures]++;
		const INT Tier = BestiaryTier( Type );
		INT Xp = GCreatures[Type].Xp;
		if( Tier>=1 )
			Xp += Xp/5;
		GiveXp( Xp, *FString::Printf(TEXT("%s defeated"), GCreatures[Type].Name) );
		if( Tier>OldTier )
		{
			static const TCHAR* TierText[3] =
			{
				TEXT("+20% XP from them"), TEXT("-15% damage from them"), TEXT("-30% damage, and they drop a bean")
			};
			AddFeed( FString::Printf(TEXT("%s rank %i"), GCreatures[Type].Name, Tier), FColor(160,230,140,255), TOAST_Reward, *(FString(TEXT("Bestiary  -  ")) + TierText[Tier-1]) );
		}
		if( Tier>=3 )
			QueueBeans( Creature->Location, 1 );
	}

	INT CreatureDamageReduction( UObject* Source )
	{
		if( !Source )
			return 0;
		INT Type = CreatureType( Source );
		if( Type<0 && Source->IsA(AActor::StaticClass()) )
			Type = CreatureType( ((AActor*)Source)->Owner );
		if( Type<0 )
			return 0;
		const INT Tier = BestiaryTier( Type );
		return Tier>=3 ? 30 : (Tier>=2 ? 15 : 0);
	}

	/*-------------------------------------------------------------------------
		Deferred actions (never run inside a VM hook).
	-------------------------------------------------------------------------*/

	void QueueBeans( FVector Location, INT Count )
	{
		FRpgDeferred* D = new(Deferred) FRpgDeferred;
		D->Kind = DEF_SpawnBeans;
		D->Actor = NULL;
		D->Value = Count;
		D->Location = Location;
	}

	void QueueRedispatch( AActor* Target, INT SpellType )
	{
		FRpgDeferred* D = new(Deferred) FRpgDeferred;
		D->Kind = DEF_RedispatchSpell;
		D->Actor = Target;
		D->Value = SpellType;
		D->Location = Target->Location;
	}

	// Safe membership test: destroyed actors can be freed mid-level, so never
	// read fields of a cached actor pointer before this says it is live.
	UBOOL IsInLevel( AActor* Actor )
	{
		if( !Actor || !LastLevel )
			return 0;
		for( INT i=0; i<LastLevel->Actors.Num(); i++ )
			if( LastLevel->Actors(i)==Actor )
				return !Actor->bDeleteMe;
		return 0;
	}

	UBOOL IsLiveActor( UGameEngine* Engine, AActor* Actor )
	{
		if( !Actor )
			return 0;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
			if( Engine->GLevel->Actors(i)==Actor )
				return !Actor->bDeleteMe;
		return 0;
	}

	void RunDeferred( UGameEngine* Engine )
	{
		if( !Deferred.Num() )
			return;
		TArray<FRpgDeferred> Work = Deferred;
		Deferred.Empty();
		static UClass* BeanClass = NULL;
		if( !BeanClass )
			BeanClass = FindClass( TEXT("Jellybean") );
		for( INT i=0; i<Work.Num(); i++ )
		{
			const FRpgDeferred& D = Work(i);
			if( D.Kind==DEF_SpawnBeans && BeanClass )
			{
				for( INT b=0; b<D.Value; b++ )
				{
					AActor* Bean = Engine->GLevel->SpawnActor( BeanClass, NAME_None, NULL, NULL, D.Location + FVector(0,0,25), FRotator(0,0,0), NULL, 1 );
					if( Bean )
					{
						Bean->Velocity = FVector( RpgRand(160)-80.f, RpgRand(160)-80.f, 180.f + RpgRand(80) );
						Bean->setPhysics( PHYS_Falling );
					}
				}
			}
			else if( D.Kind==DEF_RedispatchSpell && IsLiveActor(Engine,D.Actor) )
			{
				FRpgArg Args[2] = { FRpgArg::I(D.Value), FRpgArg::V(D.Location) };
				CallScript( D.Actor, TEXT("CallHandleSpellBySpellType"), Args, 2 );
			}
		}
	}

	/*-------------------------------------------------------------------------
		NPC classification.
	-------------------------------------------------------------------------*/

	UBOOL IsStudent( AActor* Actor )
	{
		return ClassChainIsAny( Actor, TEXT("GFemGry1|GOldMaleGry1|GenMale1|GenMale2") );
	}

	UBOOL IsHolding( AActor* Actor )
	{
		// Cutscene casts wait in a "PeopleRm" group; they never trade.
		const TCHAR* G = *Actor->Group;
		return appStrstr(G,TEXT("PeopleRm")) || appStrstr(G,TEXT("peoplerm")) || appStrstr(G,TEXT("PeopleRM"));
	}

	INT StudentHouse( AActor* Actor )
	{
		const TCHAR* N = Actor->GetClass()->GetName();
		if( appStrstr(N,TEXT("Sly")) ) return HOUSE_Slytherin;
		if( appStrstr(N,TEXT("Rav")) ) return HOUSE_Ravenclaw;
		if( appStrstr(N,TEXT("Huf")) ) return HOUSE_Hufflepuff;
		return HOUSE_Gryffindor;
	}

	// Which vendor (if any) this actor is, given the current story state.
	INT VendorFor( AActor* Actor )
	{
		if( !Actor || Actor->bHidden || IsHolding(Actor) )
			return -1;
		for( INT v=0; v<VEN_StudentGryffindor; v++ )
			if( GVendors[v].ClassName && ClassChainIs(Actor,GVendors[v].ClassName) )
				return v;
		if( ClassChainIs(Actor,TEXT("GeorgeWeasley")) )
			return VEN_Weasleys;
		// Only the students the game itself made traders sell anything.
		if( IsStudent(Actor) )
		{
			const INT Sells = GetByteProp( Actor, TEXT("CharacterSells"), 7 );
			if( Sells>=2 && Sells<=5 )
				return VEN_StudentGryffindor + StudentHouse( Actor );
		}
		return -1;
	}

	FString NpcName( AActor* Actor )
	{
		const INT V = VendorFor( Actor );
		if( V>=0 && V<VEN_StudentGryffindor )
			return GVendors[V].Name;
		if( IsStudent(Actor) )
			return FString::Printf( TEXT("%s student"), GHouseNames[StudentHouse(Actor)] );
		return Actor->GetClass()->GetName();
	}

	// Logs the characters around Harry (used when B finds no vendor).
	void LogNearby( UGameEngine* Engine, APlayerPawn* Harry )
	{
		static UClass* HChar = NULL;
		if( !HChar )
			HChar = FindClass( TEXT("HChar") );
		RpgLog( TEXT("B pressed: HChar class %s"), HChar ? TEXT("found") : TEXT("MISSING") );
		INT Logged = 0;
		for( INT i=0; i<Engine->GLevel->Actors.Num() && Logged<12; i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A==Harry || !A->IsA(APawn::StaticClass()) )
				continue;
			const FLOAT Dist = (A->Location - Harry->Location).Size();
			if( Dist>600.f )
				continue;
			Logged++;
			RpgLog( TEXT("  %s class=%s dist=%i hidden=%i gamestate=%i group=%s student=%i holding=%i sells=%i vendor=%i hchar=%i"),
				A->GetName(), A->GetClass()->GetName(), appRound(Dist), A->bHidden, A->bInCurrentGameState, *A->Group,
				IsStudent(A), IsHolding(A), GetByteProp(A,TEXT("CharacterSells"),-1), VendorFor(A), HChar ? A->IsA(HChar) : -1 );
		}
	}

	void PollNearby( UGameEngine* Engine, APlayerPawn* Harry )
	{
		static UClass* HChar = NULL;
		if( !HChar )
			HChar = FindClass( TEXT("HChar") );
		NearNpc = NULL;
		NearVendor = -1;
		if( !HChar )
			return;
		FLOAT Nearest = 230.f;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* Actor = Engine->GLevel->Actors(i);
			if( !Actor || Actor->bDeleteMe || Actor==Harry || Actor->bHidden || !Actor->IsA(HChar) )
				continue;
			const INT Vendor = VendorFor( Actor );
			if( Vendor<0 && !(IsStudent(Actor) && !IsHolding(Actor)) )
				continue;
			const FLOAT Dist = (Actor->Location - Harry->Location).Size();
			if( Dist < Nearest )
			{
				Nearest = Dist;
				NearNpc = Actor;
				NearVendor = Vendor;
			}
		}
	}

	/*-------------------------------------------------------------------------
		VM hooks.
	-------------------------------------------------------------------------*/

	void OnScriptCall( UObject* Self, UFunction* Function, BYTE* Locals, UObject* Caller )
	{
		if( !bEnabled || bLevelTransition )
			return;
		APlayerPawn* Harry = CurrentHarry();
		const FName Name = Function->GetFName();

		if( Name==N_HandleFlipendo || Name==N_HandleRictusempra || Name==N_HandleDiffindo || Name==N_HandleSkurge )
		{
			// Only the most-derived handler acts, so Super chains don't stack perks.
			if( !Self->IsA(AActor::StaticClass()) || Self->FindFunction(Name)!=Function )
				return;
			AActor* Spell = (AActor*)ParmObj( Function, Locals, 0 );
			if( !Spell || !Harry || !IsHarrysSpell(Spell,Harry) )
				return; // re-dispatches and other casters pass a None or foreign spell
			OnHarrySpellHit( (AActor*)Self, Name, Spell );
			return;
		}
		if( Name==N_OnSpellHit )
		{
			OnSpellHitPawn( Self, Function, Locals );
			return;
		}
		if( Name==N_TakeDamage )
		{
			// State versions call Global.TakeDamage; act only in the class one.
			if( !Harry || Self!=Harry || appStricmp(Function->GetOuter()->GetName(),TEXT("harry"))!=0 )
				return;
			INT* Damage = ParmIntPtr( Function, Locals, 0 );
			const FName Type = ParmName( Function, Locals, 4 );
			if( !Damage || *Damage<=0 )
				return;
			const TCHAR* T = *Type;
			if( appStricmp(T,TEXT("ZonePain"))==0 || appStricmp(T,TEXT("pit"))==0 || appStricmp(T,TEXT("Crushed"))==0 || appStricmp(T,TEXT("Falling"))==0 )
				return;
			// Protego blocks everything; frozen creatures can't hurt you.
			if( ShieldLeft>0.f || IsFrozen(Caller) || IsFrozen(ParmObj(Function,Locals,1)) )
			{
				*Damage = 0;
				if( ShieldLeft>0.f )
					PlaySfx( TEXT("HPSounds.Magic_sfx.Dueling_EXP_smack") );
				return;
			}
			const INT Pct = Min( 60, GetStat(STAT_DamageReduce) + CreatureDamageReduction(Caller) );
			if( Pct>0 )
				*Damage = Max( 1, appRound(*Damage*(100-Pct)/100.f) );
			return;
		}
		if( Name==N_PickupItem )
		{
			UObject* Prop = ParmObj( Function, Locals, 0 );
			if( !Prop || !ClassChainIs(Prop,TEXT("Jellybean")) )
				return;
			const INT Base = Max( 1, GetIntProp(Prop,TEXT("nPickupIncrement"),1) );
			// Special bean colours are worth more.
			INT Worth = 1;
			const TCHAR* Kind = NULL;
			AActor* BeanActor = Prop->IsA(AActor::StaticClass()) ? (AActor*)Prop : NULL;
			const TCHAR* SkinName = BeanActor && BeanActor->Skin ? BeanActor->Skin->GetName() : TEXT("");
			if( appStrstr(SkinName,TEXT("Stripe")) )            { Worth = 5; Kind = TEXT("striped"); }
			else if( appStrstr(SkinName,TEXT("Yellowy")) )      { Worth = 3; Kind = TEXT("golden"); }
			else if( appStrstr(SkinName,TEXT("Checker")) || appStrstr(SkinName,TEXT("Spot")) ) { Worth = 2; Kind = TEXT("speckled"); }
			INT Total = Base*Worth*BeanMultiplier;
			if( Kind )
				AddFeed( FString::Printf(TEXT("+%i beans"), Base*Worth*BeanMultiplier), Worth>=5 ? FColor(255,120,120,255) : (Worth>=3 ? FColor(255,220,90,255) : FColor(190,230,255,255)), TOAST_Bean, *FString::Printf(TEXT("%s bean!"), Kind) );
			const INT Chance = GetStat( STAT_BeanFind );
			INT Bonus = 0;
			for( INT i=0; i<Base; i++ )
				if( RpgRand(100) < Chance )
					Bonus += Chance>100 ? 2 : 1;
			Total += Bonus;
			SetIntProp( Prop, TEXT("nPickupIncrement"), Total );
			State.Counters[CNT_Beans] += Total;
			if( Bonus )
			{
				LuckyFeedBeans += Bonus;
				LuckyFeedTimer = 0.f;
			}
			return;
		}
		if( Name==N_ThrowCarrying )
		{
			if( Harry && Self==Harry && ClassChainIs(GetObjProp(Self,TEXT("CarryingActor")),TEXT("GNOME")) )
				State.Counters[CNT_Gnomes]++;
			return;
		}
		if( Name==N_OnFound )
		{
			if( ClassChainIs(Self,TEXT("SecretAreaMarker")) && !GetBoolProp(Self,TEXT("bFound")) )
			{
				State.Counters[CNT_Secrets]++;
				GiveXp( 30, TEXT("secret found") );
			}
			return;
		}
		if( Name==N_PotionEnd )
		{
			if( Harry && Self==Harry )
			{
				State.Counters[CNT_Potions]++;
				GiveXp( 40, TEXT("potion brewed") );
			}
			return;
		}
		if( Name==N_ChallengeScores )
		{
			UStrProperty* UrlP = Cast<UStrProperty>( ParmByName(Function,TEXT("strLevelURL")) );
			UIntProperty* HighP = Cast<UIntProperty>( ParmByName(Function,TEXT("nHighScore")) );
			UIntProperty* MaxP = Cast<UIntProperty>( ParmByName(Function,TEXT("nMaxScore")) );
			const FString* Url = UrlP ? (const FString*)(Locals + UrlP->Offset) : NULL;
			INT* High = HighP ? (INT*)(Locals + HighP->Offset) : NULL;
			INT* MaxScore = MaxP ? (INT*)(Locals + MaxP->Offset) : NULL;
			if( Url && High && MaxScore && *MaxScore>0 && *High>=*MaxScore )
			{
				const INT H = (INT)(HashString(**Url) & 0x7fffffff) | 1;
				INT Slot = -1;
				for( INT i=0; i<MAX_MASTERED; i++ )
				{
					if( State.Mastered[i]==H )
						return;
					if( !State.Mastered[i] && Slot<0 )
						Slot = i;
				}
				if( Slot>=0 )
				{
					State.Mastered[Slot] = H;
					State.Counters[CNT_Mastered]++;
					GiveXp( 150, TEXT("challenge mastered") );
				}
			}
			return;
		}
		if( Name==N_DuelRanks )
		{
			if( Harry && Self==Harry && ParmBool(Function,Locals,0) )
			{
				State.Counters[CNT_Duels]++;
				GiveXp( 75, TEXT("duel won") );
			}
			return;
		}
		if( Name==N_ComputeScore )
		{
			if( ClassChainIs(Self,TEXT("QuidditchDirector")) && GetBoolProp(Self,TEXT("bGryffWon")) && QuidditchCountedLevel!=LevelSerial )
			{
				QuidditchCountedLevel = LevelSerial;
				State.Counters[CNT_Quidditch]++;
				GiveXp( 150, TEXT("Quidditch match won") );
			}
			return;
		}
		if( Name==N_DeltaHealth )
		{
			if( HasPerk(PERK_Duelist) && ClassChainIs(Self,TEXT("Duellist")) )
			{
				FLOAT* Charge = ParmFloatPtr( Function, Locals, 2 );
				if( Charge )
					*Charge *= ParmBool(Function,Locals,0) ? 0.7f : 1.4f;
			}
			return;
		}
		if( Name==N_BossDefeated )
		{
			if( ClassChainIs(Self,TEXT("baseBoss")) && Self->IsA(AActor::StaticClass()) )
				CreditKill( (AActor*)Self, CR_Boss );
			return;
		}
		if( Name==N_DestroyActor || Name==N_LoseHead || Name==N_KillMaim )
		{
			if( !Self->IsA(AActor::StaticClass()) )
				return;
			const INT Type = CreatureType( Self );
			if( Type==CR_Bowtruckle || Type==CR_Horklump || Type==CR_SpikyPlant )
				CreditKill( (AActor*)Self, Type );
			return;
		}
	}

	void OnStateChange( UObject* Self, FName OldState, FName NewState )
	{
		if( !bEnabled || bLevelTransition || !Self->IsA(AActor::StaticClass()) )
			return;
		if( NewState==N_CaughtHarry )
		{
			if( RpgClock-LastCaughtTime > 10.f )
			{
				LastCaughtTime = RpgClock;
				AddTrouble( 30, TEXT("caught out of bounds") );
			}
			return;
		}
		for( INT i=0; i<NumDeathStates; i++ )
			if( NewState==DeathStates[i] )
			{
				const INT Type = CreatureType( Self );
				if( Type>=0 && Type!=CR_Boss )
					CreditKill( (AActor*)Self, Type );
				return;
			}
	}

	void OnSpellHitPawn( UObject* Self, UFunction* Function, BYTE* Locals )
	{
		// OnSpellHit( optional baseSpell spell, optional vector vHitLocation ) on the victim.
		if( !Self->IsA(AActor::StaticClass()) || !IsStudent((AActor*)Self) )
			return;
		AActor* Spell = (AActor*)ParmObj( Function, Locals, 0 );
		APlayerPawn* Harry = CurrentHarry();
		if( !Spell || !Harry || !IsHarrysSpell(Spell,Harry) )
			return;
		LaunchStudent( Harry, (AActor*)Self );
	}

	// Students are not reliably touched by spell projectiles, so watch Harry's
	// Flipendo spells ourselves and launch any student they reach.
	void LaunchStudent( APlayerPawn* Harry, AActor* Student )
	{
		if( Student==LastHitStudent && RpgClock-LastHitTime < 1.5f )
			return;
		LastHitStudent = Student;
		LastHitTime = RpgClock;
		const FLOAT Boost = (HasPerk(PERK_PowerPush) ? 1.5f : 1.f)*SpellPower();
		FVector Dir = Student->Location - Harry->Location;
		Dir.Z = 0.f;
		Dir = Dir.SafeNormal();
		SetBoolProp( Student, TEXT("bFlipPushable"), 1 );
		Student->setPhysics( PHYS_Falling );
		Student->Velocity = Dir*(420.f*Boost) + FVector( 0.f, 0.f, 620.f*Boost );
		State.Counters[CNT_Students]++;
		AddHousePoints( Harry, HOUSE_Gryffindor, -5 );
		AddTrouble( 12, TEXT("Hexing a student") );
		AddRep( StudentHouse(Student), -8 );
		RpgLog( TEXT("launched student %s"), Student->GetName() );
	}

	// Spells are owned by the wand (or Harry), depending on how they were cast.
	static UBOOL IsHarrysSpell( AActor* Spell, APlayerPawn* Harry )
	{
		return Spell->Owner==Harry || (Spell->Owner && Spell->Owner->Owner==Harry) || Spell->Instigator==Harry;
	}

	void PollSpellHits( UGameEngine* Engine, APlayerPawn* Harry )
	{
		static UClass* Flipendo = NULL;
		if( !Flipendo )
			Flipendo = FindClass( TEXT("spellFlipendo") );
		if( !Flipendo )
			return;
		ULevel* Level = Engine->GLevel;
		for( INT i=0; i<Level->Actors.Num(); i++ )
		{
			AActor* Spell = Level->Actors(i);
			if( !Spell || Spell->bDeleteMe || !Spell->IsA(Flipendo) || !IsHarrysSpell(Spell,Harry) )
				continue;
			if( !TestAndSet(Spell,AF_Talked) )
			{
				// Diagnostics: first sighting of each Flipendo.
				FLOAT Best = 1e9f;
				AActor* BestStudent = NULL;
				for( INT j=0; j<Level->Actors.Num(); j++ )
				{
					AActor* Other = Level->Actors(j);
					if( Other && !Other->bDeleteMe && IsStudent(Other) )
					{
						const FLOAT D = (Other->Location-Spell->Location).Size();
						if( D<Best ) { Best = D; BestStudent = Other; }
					}
				}
				RpgLog( TEXT("flipendo %s owner=%s nearest student %s at %i"), Spell->GetName(),
					Spell->Owner ? Spell->Owner->GetName() : TEXT("None"), BestStudent ? BestStudent->GetName() : TEXT("none"), appRound(Best) );
			}
			for( INT j=0; j<Level->Actors.Num(); j++ )
			{
				AActor* Other = Level->Actors(j);
				if( !Other || Other->bDeleteMe || Other->bHidden || !IsStudent(Other) || IsHolding(Other) )
					continue;
				const FVector D = Other->Location - Spell->Location;
				const FLOAT Reach = Other->CollisionRadius + 28.f;
				if( D.X*D.X + D.Y*D.Y > Reach*Reach || Abs(D.Z) > Other->CollisionHeight + 30.f )
					continue;
				if( TestAndSet(Spell,AF_Pushed) )
					break;
				LaunchStudent( Harry, Other );
				break;
			}
		}
	}

	void ScaleOnce( AActor* A, INT Flag, const TCHAR* Prop, FLOAT Factor )
	{
		if( TestAndSet(A,Flag) )
			return;
		const FLOAT V = GetFloatProp( A, Prop, -1.f );
		if( V>0.f )
			SetFloatProp( A, Prop, V*Factor );
	}

	void DecrementInt( AActor* A, const TCHAR* Prop, INT Floor )
	{
		const INT V = GetIntProp( A, Prop, -1000 );
		if( V>Floor )
			SetIntProp( A, Prop, V-1 );
	}

	void OnHarrySpellHit( AActor* Target, FName Spell, AActor* SpellActor )
	{
		const FLOAT K = SpellPower();
		const INT Type = CreatureType( Target );

		if( Spell==N_HandleFlipendo )
		{
			if( IsStudent(Target) )
			{
				// Students go flying. Bully.
				const FLOAT Boost = HasPerk(PERK_PowerPush) ? 1.5f : 1.f;
				SetBoolProp( Target, TEXT("bFlipPushable"), 1 );
				SetFloatProp( Target, TEXT("fFlipPushForceXY"), 420.f*Boost*K );
				SetFloatProp( Target, TEXT("fFlipPushForceZ"), 620.f*Boost*K );
				return;
			}
			if( Type<0 )
				return;
			if( HasPerk(PERK_PowerPush) && !TestAndSet(Target,AF_Pushed) )
			{
				const FLOAT XY = GetFloatProp( Target, TEXT("fFlipPushForceXY"), -1.f );
				const FLOAT Z = GetFloatProp( Target, TEXT("fFlipPushForceZ"), -1.f );
				if( XY>0.f ) SetFloatProp( Target, TEXT("fFlipPushForceXY"), XY*1.6f*K );
				if( Z>0.f )  SetFloatProp( Target, TEXT("fFlipPushForceZ"), Z*1.4f*K );
			}
			if( HasPerk(PERK_DoubleImpact) )
			{
				if( Type==CR_Gnome )
					DecrementInt( Target, TEXT("Lives"), 1 );
				else if( Type==CR_Doxy )
				{
					const FLOAT S = GetFloatProp( Target, TEXT("strength"), 0.f );
					if( S>1.f )
						SetFloatProp( Target, TEXT("strength"), S-1.f );
				}
				else if( Type==CR_Imp )
					ScaleOnce( Target, AF_ImpStun, TEXT("timeStunned"), 1.5f*K );
			}
			if( HasPerk(PERK_BeanShaker) && RpgRand(100) < 30 )
				QueueBeans( Target->Location, 1 + RpgRand(2) );
			return;
		}

		if( Spell==N_HandleRictusempra )
		{
			if( Type<0 )
				return;
			if( HasPerk(PERK_StunningSmile) )
			{
				if( Type==CR_Pixie || Type==CR_Imp )
					ScaleOnce( Target, AF_Stunned, TEXT("timeStunned"), 1.6f*K );
				else if( Type==CR_SpiderLarge )
					ScaleOnce( Target, AF_SpiderStun, TEXT("timeStunnedWhenHit"), 1.5f*K );
			}
			if( HasPerk(PERK_DoubleTickle) )
			{
				if( Type==CR_SpiderLarge )
					DecrementInt( Target, TEXT("numSpells"), 1 );
				else if( Type==CR_FireCrab )
					DecrementInt( Target, TEXT("iNumSpellHitsToFlip"), 1 );
			}
			if( HasPerk(PERK_Splash) && Target->XLevel )
			{
				INT Count = 0;
				for( INT i=0; i<Target->XLevel->Actors.Num() && Count<3; i++ )
				{
					AActor* Other = Target->XLevel->Actors(i);
					if( !Other || Other==Target || Other->bDeleteMe || Other->GetClass()!=Target->GetClass() )
						continue;
					if( (Other->Location-Target->Location).SizeSquared() > 230.f*230.f )
						continue;
					QueueRedispatch( Other, 22 );
					Count++;
				}
			}
			return;
		}

		if( Spell==N_HandleDiffindo )
		{
			if( HasPerk(PERK_SharpCut) )
			{
				if( Type==CR_SpikyPlant )
					ScaleOnce( Target, AF_Plant, TEXT("durationNoSpikes"), 2.f*K );
				else if( Type==CR_Tentacula )
					DecrementInt( Target, TEXT("NumLimbs"), 1 );
			}
			if( HasPerk(PERK_Harvest) && (Type==CR_Bowtruckle || Type==CR_SpikyPlant || Type==CR_Tentacula || Type==CR_Horklump) )
				QueueBeans( Target->Location, 2 );
			return;
		}

		if( Spell==N_HandleSkurge )
		{
			if( HasPerk(PERK_DeepClean) && Type==CR_Ectoplasm )
				ScaleOnce( Target, AF_Ecto, TEXT("fShrinkTime"), 2.f*K );
			if( HasPerk(PERK_Ghostbuster) && ClassChainIs(Target,TEXT("Peeves")) && !TestAndSet(Target,AF_Peeves) )
			{
				const INT D = GetIntProp( Target, TEXT("HitDamage"), 0 );
				if( D>0 )
					SetIntProp( Target, TEXT("HitDamage"), appRound(D*2.f*K) );
			}
			return;
		}
	}

	/*-------------------------------------------------------------------------
		Skills and wand perks.
	-------------------------------------------------------------------------*/

	static UBOOL Near( FLOAT A, FLOAT B ) { return Abs(A-B) < 0.01f; }

	// Applies a multiplier to a float property, but only while the game uses
	// the stock value or the value we set earlier, so game effects (webs,
	// sleepiness, boss fights) keep working.
	void ApplyScaled( APlayerPawn* Harry, UObject* Defaults, const TCHAR* Prop, FLOAT Factor, FLOAT* LastSet )
	{
		const FLOAT Base = GetFloatProp( Defaults, Prop, 0.f );
		const FLOAT Cur = GetFloatProp( Harry, Prop, 0.f );
		if( Base<=0.f )
			return;
		const FLOAT Want = Base*Factor;
		if( Near(Cur,Want) )
			return;
		if( Near(Cur,Base) || (LastSet && Near(Cur,*LastSet)) )
		{
			SetFloatProp( Harry, Prop, Want );
			if( LastSet )
				*LastSet = Want;
		}
	}

	void ApplySkills( APlayerPawn* Harry )
	{
		UObject* Defaults = Harry->GetClass()->GetDefaultObject();

		const INT DesiredBonus = 100*State.Skill[SKILL_Health];
		if( DesiredBonus!=State.AppliedHealthBonus && DesiredBonus!=HealthTargetTried && !GetBoolProp(Harry,TEXT("bInDuelingMode")) )
		{
			UObject* Status = GetStatus( Harry );
			if( Status )
			{
				// The game caps health potential, so record what really changed.
				const INT Before = CallInt( Status, TEXT("GetHealthPotentialCount") );
				FRpgArg Arg = FRpgArg::I( DesiredBonus - State.AppliedHealthBonus );
				CallScript( Status, TEXT("AddHealthPotential"), &Arg, 1 );
				State.AppliedHealthBonus += CallInt( Status, TEXT("GetHealthPotentialCount") ) - Before;
				HealthTargetTried = DesiredBonus;
			}
		}

		const FLOAT Speed = 1.f + 0.08f*State.Skill[SKILL_Speed] + GetStat(STAT_Speed)/100.f;
		const FLOAT Jump = 1.f + 0.10f*State.Skill[SKILL_Jump] + GetStat(STAT_Jump)/100.f;
		const FLOAT OldRun = Harry->GroundRunSpeed;
		ApplyScaled( Harry, Defaults, TEXT("GroundRunSpeed"), Speed, &LastRunSpeed );
		if( Harry->GroundRunSpeed!=OldRun && Near(Harry->GroundSpeed,OldRun) )
			Harry->GroundSpeed = Harry->GroundRunSpeed;
		ApplyScaled( Harry, Defaults, TEXT("GroundJumpSpeed"), Speed, &LastAirSpeed );
		ApplyScaled( Harry, Defaults, TEXT("JumpZ"), Jump, &LastJump );

		if( HasPerk(PERK_QuickWand) )
			SetBoolProp( Harry, TEXT("bCastFastSpells"), 1 );
		if( HasPerk(PERK_FarSight) )
		{
			UObject* Cursor = GetObjProp( Harry, TEXT("SpellCursor") );
			if( Cursor )
			{
				UObject* CursorDefaults = Cursor->GetClass()->GetDefaultObject();
				const FLOAT Base = GetFloatProp( CursorDefaults, TEXT("fLOS_Distance"), 0.f );
				if( Base>0.f && Near(GetFloatProp(Cursor,TEXT("fLOS_Distance")),Base) )
					SetFloatProp( Cursor, TEXT("fLOS_Distance"), Base*1.5f );
			}
		}
	}

	/*-------------------------------------------------------------------------
		Wardrobe: Harry's textures and palettes.
	-------------------------------------------------------------------------*/

	UBOOL ResolveHarryAssets( APlayerPawn* Harry )
	{
		if( !HarryMesh )
		{
			if( Harry->Mesh && appStricmp(Harry->Mesh->GetName(),TEXT("skharryMesh"))==0 )
				HarryMesh = Harry->Mesh;
			else
				HarryMesh = (UMesh*)UObject::StaticFindObject( UMesh::StaticClass(), ANY_PACKAGE, TEXT("skharryMesh") );
			if( HarryMesh )
			{
				HarryMesh->AddToRoot();
				BaseDrawScale = ((AActor*)Harry->GetClass()->GetDefaultObject())->DrawScale;
				if( BaseDrawScale<=0.f )
					BaseDrawScale = 1.f;
			}
		}
		static const TCHAR* Names[4] = { TEXT("skharryTex0"), TEXT("skharryTex1"), TEXT("skharryTex2"), TEXT("skharryTex3") };
		for( INT i=0; i<4; i++ )
			if( !HarryTex[i] )
			{
				HarryTex[i] = LoadTex( *FString::Printf(TEXT("HPModels.Skins.%s"), Names[i]) );
				if( HarryTex[i] )
				{
					OriginalPalette[i] = HarryTex[i]->Palette;
					if( OriginalPalette[i] )
						OriginalPalette[i]->AddToRoot();
				}
				else
					RpgLog( TEXT("missing texture %s"), Names[i] );
			}
		return HarryMesh && HarryTex[0] && HarryTex[1] && HarryTex[2] && HarryTex[3];
	}

	static void RgbToHsv( FLOAT R, FLOAT G, FLOAT B, FLOAT& H, FLOAT& S, FLOAT& V )
	{
		const FLOAT Mx = Max(R,Max(G,B)), Mn = Min(R,Min(G,B)), D = Mx-Mn;
		V = Mx;
		S = Mx>0.f ? D/Mx : 0.f;
		if( D<=0.f )
			H = 0.f;
		else if( Mx==R )
			H = (G-B)/D/6.f;
		else if( Mx==G )
			H = ((B-R)/D + 2.f)/6.f;
		else
			H = ((R-G)/D + 4.f)/6.f;
		if( H<0.f )
			H += 1.f;
	}

	static void HsvToRgb( FLOAT H, FLOAT S, FLOAT V, FLOAT& R, FLOAT& G, FLOAT& B )
	{
		H = H - appFloor(H);
		const FLOAT F = H*6.f;
		const INT I = appFloor(F) % 6;
		const FLOAT Fr = F - appFloor(F);
		const FLOAT P = V*(1.f-S), Q = V*(1.f-S*Fr), T = V*(1.f-S*(1.f-Fr));
		switch( I )
		{
			case 0:  R=V; G=T; B=P; break;
			case 1:  R=Q; G=V; B=P; break;
			case 2:  R=P; G=V; B=T; break;
			case 3:  R=P; G=Q; B=V; break;
			case 4:  R=T; G=P; B=V; break;
			default: R=V; G=P; B=Q; break;
		}
	}

	static BYTE ToByte( FLOAT F )
	{
		return (BYTE)Clamp( appRound(F*255.f), 0, 255 );
	}

	// Variant keys: PV_* for robes/body/face, 10+n for glasses frame colours.
	UPalette* GetPalette( INT TexIndex, INT Variant )
	{
		if( Variant<=0 )
			return OriginalPalette[TexIndex];
		const INT Key = TexIndex*100 + Variant;
		for( INT i=0; i<CustomPalettes.Num(); i++ )
			if( CustomPaletteKeys[i]==Key )
				return CustomPalettes(i);
		UPalette* Source = OriginalPalette[TexIndex];
		if( !Source || CustomPalettes.Num()>=ARRAY_COUNT(CustomPaletteKeys) )
			return Source;

		UPalette* Palette = ConstructObject<UPalette>( UPalette::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Transient );
		Palette->AddToRoot();
		Palette->Colors = Source->Colors;
		static const FColor Frames[6] =
		{
			FColor(0,0,0,255), FColor(0,0,0,255), FColor(212,170,60,255), FColor(170,24,24,255), FColor(20,120,50,255), FColor(40,80,190,255)
		};
		for( INT i=0; i<Palette->Colors.Num(); i++ )
		{
			FColor& C = Palette->Colors(i);
			if( TexIndex==3 )
			{
				if( i==1 && Variant>=12 && Variant<=15 )
					C = FColor( Frames[Variant-10].R, Frames[Variant-10].G, Frames[Variant-10].B, C.A );
				continue;
			}
			const FLOAT R = C.R/255.f, G = C.G/255.f, B = C.B/255.f;
			if( Variant==PV_Memory )
			{
				const FLOAT L = 0.30f*R + 0.59f*G + 0.11f*B;
				C = FColor( ToByte(L*1.07f), ToByte(L*0.92f), ToByte(L*0.72f), C.A );
				continue;
			}
			FLOAT H, S, V;
			RgbToHsv( R, G, B, H, S, V );
			const UBOOL bRed = (H<0.06f || H>0.94f) && S>0.45f && V>0.12f;
			const UBOOL bGold = H>=0.08f && H<=0.18f && S>0.40f && V>0.35f;
			if( !bRed && !bGold )
				continue;
			FLOAT NR=R, NG=G, NB=B;
			switch( Variant )
			{
				case PV_Slytherin:
					if( bRed ) HsvToRgb( 0.36f, S*0.85f, V*0.85f, NR, NG, NB );
					else       HsvToRgb( 0.f, 0.05f, Min(1.f,V*1.05f), NR, NG, NB );
					break;
				case PV_Ravenclaw:
					if( bRed ) HsvToRgb( 0.62f, S*0.9f, V, NR, NG, NB );
					else       HsvToRgb( 0.07f, S*0.75f, V*0.8f, NR, NG, NB );
					break;
				case PV_Hufflepuff:
					if( bRed ) HsvToRgb( 0.13f, Min(1.f,S*1.05f), Min(1.f,V*1.35f), NR, NG, NB );
					else       HsvToRgb( 0.f, 0.f, V*0.18f, NR, NG, NB );
					break;
				default:
					break;
			}
			C = FColor( ToByte(NR), ToByte(NG), ToByte(NB), C.A );
		}
		CustomPaletteKeys[CustomPalettes.Num()] = Key;
		CustomPalettes.AddItem( Palette );
		return Palette;
	}

	void SetTexturePalette( INT TexIndex, UPalette* Palette )
	{
		UTexture* Tex = HarryTex[TexIndex];
		if( !Tex || !Palette || Tex->Palette==Palette )
			return;
		Tex->Palette = Palette;
		Tex->bRealtimeChanged = 1;
	}

	void SetGlassesHidden( UBOOL bHide )
	{
		UTexture* Tex = HarryTex[3];
		if( !Tex || bGlassesHidden==bHide )
			return;
		const INT NumMips = Min( Tex->Mips.Num(), (INT)ARRAY_COUNT(GlassesBackup) );
		for( INT m=0; m<NumMips; m++ )
		{
			FMipmap& Mip = Tex->Mips(m);
			Mip.DataArray.Load();
			if( Mip.DataArray.Num()<=0 )
				continue;
			if( bHide )
			{
				GlassesBackup[m].Empty();
				GlassesBackup[m].Add( Mip.DataArray.Num() );
				appMemcpy( &GlassesBackup[m](0), &Mip.DataArray(0), Mip.DataArray.Num() );
				appMemzero( &Mip.DataArray(0), Mip.DataArray.Num() );
			}
			else if( GlassesBackup[m].Num()==Mip.DataArray.Num() )
				appMemcpy( &Mip.DataArray(0), &GlassesBackup[m](0), Mip.DataArray.Num() );
		}
		bGlassesHidden = bHide;
		Tex->bRealtimeChanged = 1;
	}

	// Sets Harry's four textures for an outfit + glasses combination.
	void ApplyLook( INT OutfitId, INT GlassesId )
	{
		const FCosmetic& O = GCosmetics[OutfitId];
		const INT PV = O.OutfitKind==OK_Palette ? O.Palette : PV_None;
		INT Want[4];
		Want[0] = PV==PV_Memory ? PV_Memory : 0;
		Want[1] = PV;
		Want[2] = PV==PV_Memory ? PV_Memory : 0;
		const INT Frame = GCosmetics[GlassesId].Palette;
		Want[3] = Frame>=2 ? 10+Frame : 0;
		for( INT t=0; t<4; t++ )
			if( Want[t]!=AppliedPalette[t] )
			{
				SetTexturePalette( t, GetPalette(t,Want[t]) );
				AppliedPalette[t] = Want[t];
			}
		SetGlassesHidden( Frame==1 );
	}

	void MarkWardrobeDirty()
	{
		for( INT t=0; t<4; t++ )
			AppliedPalette[t] = -1;
		for( INT s=0; s<RSLOT_MAX; s++ )
			FailedSpawn[s] = -1;
	}

	/*-------------------------------------------------------------------------
		Wardrobe: costumes (mesh swaps on Harry).
	-------------------------------------------------------------------------*/

	void SetCostume( AActor* Body, INT OutfitId, UBOOL bIsHarry )
	{
		const FCosmetic& O = GCosmetics[OutfitId];
		if( O.OutfitKind==OK_Mesh )
		{
			UMesh* Mesh = LoadMesh( O.Mesh );
			if( !Mesh )
			{
				if( bIsHarry && FailedSpawn[RSLOT_Outfit]!=OutfitId )
				{
					RpgLog( TEXT("costume mesh missing: %s"), O.Mesh );
					FailedSpawn[RSLOT_Outfit] = OutfitId;
				}
				return;
			}
			UTexture* S0 = LoadTex( O.Skin0 );
			UTexture* S1 = LoadTex( O.Skin1 );
			if( Body->Mesh!=Mesh || Body->MultiSkins[0]!=S0 || Body->MultiSkins[1]!=S1 )
			{
				// A fresh pawn has no animation linked yet; the costume mesh would
				// link its own default set (without Harry's moves) and T-pose.
				if( !Body->SkelAnim )
					Body->SkelAnim = HarryAnimSet();
				if( !Body->SkelAnim )
					return;
				Body->Mesh = Mesh;
				for( INT i=0; i<8; i++ )
					Body->MultiSkins[i] = NULL;
				Body->MultiSkins[0] = S0;
				Body->MultiSkins[1] = S1;
				Body->DrawScale = BaseDrawScale*O.Scale;
				if( bIsHarry )
					RpgLog( TEXT("costume on: %s"), Mesh->GetPathName() );
			}
			if( bIsHarry )
				CostumeApplied = Mesh;
		}
		else if( Body->Mesh!=HarryMesh )
		{
			Body->Mesh = HarryMesh;
			for( INT i=0; i<8; i++ )
				Body->MultiSkins[i] = NULL;
			Body->DrawScale = BaseDrawScale;
			if( bIsHarry )
				CostumeApplied = NULL;
		}
	}

	void UpdateCostume( APlayerPawn* Harry )
	{
		// The game swaps Harry's mesh for cutscenes and Polyjuice; never fight it.
		if( Harry->Mesh!=HarryMesh && (!CostumeApplied || Harry->Mesh!=CostumeApplied) )
			return;
		// Let a new pawn settle (and link its animation) first.
		if( RpgClock-HarrySince < 1.f )
			return;
		if( Harry->Mesh==HarryMesh && Harry->SkelAnim )
			HarryAnim = Harry->SkelAnim;
		SetCostume( Harry, State.Equip[RSLOT_Outfit], 1 );
		// Keep Harry's own moves on the costume: if the linked set lacks the
		// sequence being played the mesh falls back to its bind (T) pose.
		if( CostumeApplied && Harry->Mesh==CostumeApplied && Harry->AnimSequence!=NAME_None )
		{
			UAnimation* Own = HarryAnimSet();
			if( Own && Harry->SkelAnim!=Own && (!Harry->SkelAnim || !Harry->SkelAnim->GetAnimSeq(Harry->AnimSequence)) && Own->GetAnimSeq(Harry->AnimSequence) )
			{
				RpgLog( TEXT("costume: relinked Harry's animations (%s)"), *Harry->AnimSequence );
				Harry->SkelAnim = Own;
			}
		}
	}

	UAnimation* HarryAnimSet()
	{
		if( HarryAnim )
			return HarryAnim;
		return HarryMesh && HarryMesh->IsA(USkeletalMesh::StaticClass()) ? ((USkeletalMesh*)HarryMesh)->DefaultAnimation : NULL;
	}

	void RestoreCostume( APlayerPawn* Harry )
	{
		if( Harry && CostumeApplied && Harry->Mesh==CostumeApplied && HarryMesh )
		{
			Harry->Mesh = HarryMesh;
			for( INT i=0; i<8; i++ )
				Harry->MultiSkins[i] = NULL;
			Harry->DrawScale = BaseDrawScale;
		}
		CostumeApplied = NULL;
	}

	/*-------------------------------------------------------------------------
		Wardrobe: attachments (hat, pet, aura) and held item.
	-------------------------------------------------------------------------*/

	// Plain actors for hats, pets, owls and menu previews. The first class
	// that spawns cleanly in this game is remembered; each refusal is logged
	// once with its reason.
	UClass* RenderClass;
	UBOOL bRenderClassLogged;

	AActor* TrySpawn( UGameEngine* Engine, UClass* Class, FVector Location, UBOOL bLog )
	{
		if( !Class )
			return NULL;
		AActor* Def = Class->GetDefaultActor();
		if( (Class->ClassFlags & CLASS_Abstract) || !Def || Def->bStatic || Def->bNoDelete )
		{
			if( bLog )
				RpgLog( TEXT("render class %s refused: abstract=%i static=%i nodelete=%i"), Class->GetName(),
					(Class->ClassFlags & CLASS_Abstract)!=0, Def ? Def->bStatic : -1, Def ? Def->bNoDelete : -1 );
			return NULL;
		}
		AActor* A = Engine->GLevel->SpawnActor( Class, NAME_None, NULL, NULL, Location, FRotator(0,0,0), NULL, 1 );
		if( (!A || A->bDeleteMe) && bLog )
			RpgLog( TEXT("render class %s refused: spawn returned %s"), Class->GetName(), A ? TEXT("deleted actor") : TEXT("nothing") );
		return A && !A->bDeleteMe ? A : NULL;
	}

	AActor* SpawnRenderActor( UGameEngine* Engine, UClass* Class, FVector Location )
	{
		AActor* A = NULL;
		if( Class )
			A = TrySpawn( Engine, Class, Location, 0 );
		else if( RenderClass )
			A = TrySpawn( Engine, RenderClass, Location, 0 );
		else
		{
			static const TCHAR* Candidates[] = { TEXT("Effects"), TEXT("ScaledSprite"), TEXT("Fragment"), TEXT("Projectile"), TEXT("Decoration"), TEXT("TimedCue") };
			const UBOOL bLog = !bRenderClassLogged;
			bRenderClassLogged = 1;
			for( INT c=0; c<ARRAY_COUNT(Candidates) && !A; c++ )
			{
				UClass* Cand = FindClass( Candidates[c] );
				if( !Cand && bLog )
					RpgLog( TEXT("render class %s not found"), Candidates[c] );
				A = TrySpawn( Engine, Cand, Location, bLog );
				if( A )
				{
					RenderClass = Cand;
					RpgLog( TEXT("render class: %s"), Cand->GetName() );
				}
			}
		}
		if( !A )
			return NULL;
		A->bHidden = 0;
		A->LifeSpan = 0.f;
		A->Velocity = FVector( 0, 0, 0 );
		A->Acceleration = FVector( 0, 0, 0 );
		A->SetCollision( 0, 0, 0 );
		A->bCollideWorld = 0;
		A->Physics = PHYS_None;
		A->Tag = FName( TEXT("HP2RpgActor") );
		return A;
	}

	void DestroyActorSafe( UGameEngine* Engine, AActor*& A )
	{
		if( A && Engine->GLevel && Engine->GLevel==LastLevel && IsInLevel(A) )
			Engine->GLevel->DestroyActor( A );
		A = NULL;
	}

	static INT HeadBone( AActor* Body, USkeletalMesh*& Skel )
	{
		Skel = Cast<USkeletalMesh>( Body->Mesh );
		if( !Skel )
			return -1;
		static FName HeadNub( TEXT("Bip01 HeadNub") );
		static FName Head( TEXT("Bip01 Head") );
		INT Bone = Skel->BoneIndex( HeadNub );
		if( Bone<0 )
			Bone = Skel->BoneIndex( Head );
		return Bone;
	}

	static INT ShoulderBone( AActor* Body, USkeletalMesh*& Skel )
	{
		Skel = Cast<USkeletalMesh>( Body->Mesh );
		if( !Skel )
			return -1;
		static FName Clavicle( TEXT("Bip01 L Clavicle") );
		static FName Neck( TEXT("Bip01 Neck") );
		INT Bone = Skel->BoneIndex( Clavicle );
		if( Bone<0 )
			Bone = Skel->BoneIndex( Neck );
		return Bone;
	}

	// Positions a hat actor on Body's head. Returns false if there is no head bone.
	UBOOL PlaceHat( AActor* Body, AActor* Hat, const FCosmetic& Spec, ULevel* WorldLevel )
	{
		USkeletalMesh* Skel = NULL;
		const INT Bone = HeadBone( Body, Skel );
		if( Bone<0 )
			return 0;
		const FCoords C = Skel->GetBoneCoords( Body, Bone );
		FCoords HatCoords = C;
		HatCoords.XAxis = C.YAxis;
		HatCoords.ZAxis = C.XAxis;
		HatCoords.YAxis = HatCoords.ZAxis ^ HatCoords.XAxis;
		const FLOAT BodyScale = Body->DrawScale/Max(0.01f,BaseDrawScale);
		const FVector Loc = C.Origin
			+ C.XAxis.SafeNormal()*(Spec.Up + HatTweak.X)*BodyScale
			+ C.YAxis.SafeNormal()*(Spec.Forward + HatTweak.Y)*BodyScale
			+ C.ZAxis.SafeNormal()*(Spec.Side + HatTweak.Z)*BodyScale;
		if( WorldLevel )
			WorldLevel->FarMoveActor( Hat, Loc, 0, 1 );
		else
			Hat->Location = Loc;
		Hat->Rotation = HatCoords.OrthoRotation();
		Hat->DrawScale = Spec.Scale*HatScaleTweak*BodyScale;
		return 1;
	}

	UBOOL PlacePet( AActor* Body, AActor* Pet, const FCosmetic& Spec, ULevel* WorldLevel )
	{
		USkeletalMesh* Skel = NULL;
		const INT Bone = ShoulderBone( Body, Skel );
		if( Bone<0 )
			return 0;
		const FCoords C = Skel->GetBoneCoords( Body, Bone );
		const FLOAT Yaw = Body->Rotation.Yaw*(2.f*PI/65536.f);
		const FVector Right( -(FLOAT)appSin(Yaw), (FLOAT)appCos(Yaw), 0.f );
		const FLOAT BodyScale = Body->DrawScale/Max(0.01f,BaseDrawScale);
		const FVector Loc = C.Origin + FVector(0,0,Spec.Up*BodyScale) + Right*(Spec.Side*BodyScale);
		if( WorldLevel )
			WorldLevel->FarMoveActor( Pet, Loc, 0, 1 );
		else
			Pet->Location = Loc;
		Pet->Rotation = FRotator( 0, Body->Rotation.Yaw, 0 );
		Pet->DrawScale = Spec.Scale*Body->DrawScale/Max(0.01f,BaseDrawScale);
		return 1;
	}

	void UpdateAttachment( UGameEngine* Engine, APlayerPawn* Harry, INT Slot, AActor*& Actor, INT& Kind, UBOOL bVisible )
	{
		const INT Id = State.Equip[Slot];
		const FCosmetic& Spec = GCosmetics[Id];
		if( !Spec.Mesh || !bVisible )
		{
			DestroyActorSafe( Engine, Actor );
			Kind = -1;
			return;
		}
		if( Actor && !IsInLevel(Actor) )
			Actor = NULL; // destroyed by the game (particles can end themselves)
		if( Actor && Kind!=Id )
			DestroyActorSafe( Engine, Actor );
		if( !Actor )
		{
			if( FailedSpawn[Slot]==Id )
				return;
			if( Slot==RSLOT_Aura )
			{
				UClass* Particle = LoadParticleClass( Spec.Mesh );
				Actor = Particle ? SpawnRenderActor( Engine, Particle, Harry->Location ) : NULL;
				if( Actor )
					RpgLog( TEXT("aura %s spawned"), Spec.Mesh );
			}
			else
			{
				UMesh* Mesh = LoadMesh( Spec.Mesh );
				Actor = Mesh ? SpawnRenderActor( Engine, NULL, Harry->Location ) : NULL;
				if( Actor )
				{
					Actor->DrawType = DT_Mesh;
					Actor->Mesh = Mesh;
					Actor->AmbientGlow = Harry->AmbientGlow;
					RpgLog( TEXT("%s %s spawned with %s"), GSlotNames[Slot], Spec.Name, Mesh->GetPathName() );
				}
			}
			if( !Actor )
			{
				RpgLog( TEXT("%s %s could not spawn (%s)"), GSlotNames[Slot], Spec.Name, Spec.Mesh );
				FailedSpawn[Slot] = Id;
				return;
			}
			Kind = Id;
		}
		UBOOL bPlaced = 1;
		if( Slot==RSLOT_Hat )
			bPlaced = PlaceHat( Harry, Actor, Spec, Engine->GLevel );
		else if( Slot==RSLOT_Pet )
			bPlaced = PlacePet( Harry, Actor, Spec, Engine->GLevel );
		else
			Engine->GLevel->FarMoveActor( Actor, Harry->Location + FVector(0,0,Spec.Up), 0, 1 );
		Actor->bHidden = Harry->bHidden || !bPlaced;
		Actor->ScaleGlow = Harry->ScaleGlow;
	}

	void RestoreHeld()
	{
		if( HeldWeapon && HeldOriginalMesh && IsInLevel(HeldWeapon) )
		{
			AInventory* Inv = (AInventory*)HeldWeapon;
			Inv->ThirdPersonMesh = HeldOriginalMesh;
			Inv->ThirdPersonScale = HeldOriginalScale;
		}
		HeldWeapon = NULL;
		HeldOriginalMesh = NULL;
	}

	void UpdateHeld( APlayerPawn* Harry )
	{
		const INT Id = State.Equip[RSLOT_Held];
		const FCosmetic& Spec = GCosmetics[Id];
		AWeapon* Weapon = Harry->Weapon;
		if( HeldWeapon && HeldWeapon!=Weapon )
			RestoreHeld();
		if( !Weapon )
			return;
		static UMesh* SwordMesh = NULL;
		if( !SwordMesh )
			SwordMesh = LoadMesh( TEXT("HPModels.skGryf_SwordMesh") );
		if( SwordMesh && Weapon->Mesh==SwordMesh )
		{
			// The game is using the real sword (Basilisk fight): show it.
			if( HeldWeapon )
			{
				Weapon->ThirdPersonMesh = SwordMesh;
				Weapon->ThirdPersonScale = HeldOriginalScale;
				HeldWeapon = NULL;
				HeldOriginalMesh = NULL;
			}
			return;
		}
		if( !Spec.Mesh )
		{
			RestoreHeld();
			return;
		}
		UMesh* Mesh = LoadMesh( Spec.Mesh );
		if( !Mesh )
			return;
		if( !HeldWeapon )
		{
			HeldWeapon = Weapon;
			HeldOriginalMesh = Weapon->ThirdPersonMesh;
			HeldOriginalScale = Weapon->ThirdPersonScale;
		}
		if( Weapon->ThirdPersonMesh!=Mesh )
		{
			Weapon->ThirdPersonMesh = Mesh;
			Weapon->ThirdPersonScale = HeldOriginalScale*Spec.Scale;
		}
	}

	void UpdateWardrobe( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !ResolveHarryAssets(Harry) )
			return;
		ApplyLook( State.Equip[RSLOT_Outfit], State.Equip[RSLOT_Glasses] );
		UpdateCostume( Harry );
		const UBOOL bOurBody = Harry->Mesh==HarryMesh || (CostumeApplied && Harry->Mesh==CostumeApplied);
		UpdateAttachment( Engine, Harry, RSLOT_Hat, HatActor, HatActorKind, bOurBody );
		UpdateAttachment( Engine, Harry, RSLOT_Pet, PetActor, PetActorKind, bOurBody );
		UpdateAttachment( Engine, Harry, RSLOT_Aura, AuraActor, AuraActorKind, 1 );
		UpdateHeld( Harry );
	}

	/*-------------------------------------------------------------------------
		Gameplay gating.
	-------------------------------------------------------------------------*/

	UBOOL IsFreeGameplay( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !Harry )
			return 0;
		ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
		if( Info->Pauser.Len() || Info->LevelAction!=LEVACT_None )
			return 0;
		static FName PlayerWalking( TEXT("PlayerWalking") );
		if( GetStateName(Harry)!=PlayerWalking )
			return 0;
		if( GetBoolProp(Harry,TEXT("bIsCaptured")) || GetBoolProp(Harry,TEXT("bInDuelingMode")) )
			return 0;
		if( GetObjProp(Harry,TEXT("CurrVendorManager")) )
			return 0;
		AActor* Hud = Harry->myHUD;
		if( Hud && (GetBoolProp(Hud,TEXT("bCutSceneMode")) || GetBoolProp(Hud,TEXT("bCutPopupMode"))) )
			return 0;
		UObject* Console = Harry->Player ? Harry->Player->Console : NULL;
		UObject* Book = GetObjProp( Console, TEXT("menuBook") );
		if( Book && GetBoolProp(Book,TEXT("bIsOpen")) )
			return 0;
		if( Console && GetBoolProp(Console,TEXT("bTyping")) )
			return 0;
		return 1;
	}

	/*-------------------------------------------------------------------------
		Tick.
	-------------------------------------------------------------------------*/

	void Tick( UGameEngine* Engine, FLOAT DeltaSeconds )
	{
		Init();
		bLevelTransition = 0;
		if( !bEnabled || !Engine->GLevel || Engine->Replay.Replaying() || Engine->Replay.Recording() )
			return;
		RpgClock += DeltaSeconds;
		if( PreviewYaw > 436906.f )
			PreviewYaw -= 436906.f;

		if( Engine->GLevel!=LastLevel )
		{
			LastLevel = Engine->GLevel;
			OnLevelChanged( Engine );
		}

		for( INT d=0; d<Ui.DiagLines.Num(); d++ )
			RpgLog( TEXT("%s"), *Ui.DiagLines(d) );
		Ui.DiagLines.Empty();

		APlayerPawn* Harry = GetHarry( Engine );
		if( Harry!=LastHarry )
		{
			LastHarry = Harry;
			HarrySince = RpgClock;
			bBaselineValid = 0;
			MarkWardrobeDirty();
			HeldWeapon = NULL;
			HeldOriginalMesh = NULL;
			CostumeApplied = NULL;
			LastRunSpeed = LastAirSpeed = LastJump = 0.f;
		}
		if( !Harry )
		{
			if( Menu!=MENU_None )
				CloseMenu( Engine );
			return;
		}

		for( INT i=Feed.Num()-1; i>=0; i-- )
		{
			Feed(i).Age += DeltaSeconds;
			if( Feed(i).Age > 3.2f )
				Feed.Remove( i );
		}
		BannerAge += DeltaSeconds;
		HouseHudTimer = Max( 0.f, HouseHudTimer-DeltaSeconds );
		NoticeTime = Max( 0.f, NoticeTime-DeltaSeconds );
		if( Menu!=MENU_None )
			PreviewYaw += DeltaSeconds*2600.f;

		LuckyFeedTimer += DeltaSeconds;
		if( LuckyFeedBeans && LuckyFeedTimer>0.6f )
		{
			AddFeed( FString::Printf(TEXT("+%i bean%s"), LuckyFeedBeans, LuckyFeedBeans==1 ? TEXT("") : TEXT("s")), FColor(140,230,140,255), TOAST_Bean, TEXT("Lucky find") );
			LuckyFeedBeans = 0;
		}

		const UBOOL bPaused = Engine->GLevel->GetLevelInfo()->Pauser.Len()>0;
		if( !bPaused )
		{
			SecondTimer += DeltaSeconds;
			while( SecondTimer>=1.f )
			{
				SecondTimer -= 1.f;
				if( State.DetentionSeconds>0 && --State.DetentionSeconds==0 )
					AddFeed( TEXT("Detention is over. Vendors will trade with you again."), FColor(200,230,255,255) );
				if( State.FelixSeconds>0 && --State.FelixSeconds==0 )
					AddFeed( TEXT("The Felix Felicis wears off."), FColor(255,220,120,255) );
				TroubleTimer += 1.f;
				if( TroubleTimer>=20.f )
				{
					TroubleTimer = 0.f;
					if( State.Trouble>0 )
						State.Trouble--;
				}
				CheckCardRewards( Harry );
				CheckTrophies( Harry );
				if( bBaselineValid )
					UpdateTasks( Harry );
			}
		}

		PollTimer += DeltaSeconds;
		if( PollTimer >= 0.2f )
		{
			PollTimer = 0.f;
			PollRewards( Harry );
			PollNearby( Engine, Harry );
			ApplySkills( Harry );
		}
		FlushHouseFeed( DeltaSeconds );
		PollSpellHits( Engine, Harry );
		RunDeferred( Engine );
		UpdateWardrobe( Engine, Harry );
		UpdateSpells( Engine, Harry, DeltaSeconds );
		UpdateOwls( Engine, Harry, DeltaSeconds );
		UpdateStudents( Engine, Harry, IsFreeGameplay(Engine,Harry), DeltaSeconds );
	}

	void OnLevelChanged( UGameEngine* Engine )
	{
		LevelSerial++;
		ActorFlags.Empty();
		Deferred.Empty();
		bBaselineValid = 0;
		HatActor = PetActor = AuraActor = NULL;
		HatActorKind = PetActorKind = AuraActorKind = -1;
		PreviewBody = PreviewAttach = PreviewItem = PreviewNpc = NULL;
		HeldWeapon = NULL;
		HeldOriginalMesh = NULL;
		CostumeApplied = NULL;
		NearNpc = NULL;
		ShopVendor = NULL;
		LastHitStudent = NULL;
		if( Menu!=MENU_None )
			CloseMenu( Engine );
		bWePaused = 0;
		Menu = MENU_None;
		MarkWardrobeDirty();

		// Actors saved into .usa files by older builds are stale; drop them.
		static FName OldHatTag( TEXT("HP2RpgHat") );
		static FName ActorTag( TEXT("HP2RpgActor") );
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* Actor = Engine->GLevel->Actors(i);
			if( Actor && !Actor->bDeleteMe && (Actor->Tag==OldHatTag || Actor->Tag==ActorTag) )
				Engine->GLevel->DestroyActor( Actor );
		}

		const TCHAR* MapName = Engine->GLevel->URL.Map.Len() ? *Engine->GLevel->URL.Map : TEXT("");
		if( bLoadedFromSlot )
			bLoadedFromSlot = 0;
		else if( appStrfind(MapName,TEXT("PrivetDr")) || appStrfind(MapName,TEXT("privetdr")) )
		{
			State.Reset();
			Sanitize();
			SpellsKnownNotified = -1;
			RpgLog( TEXT("new game, RPG progress reset") );
		}
		else if( appStrstr(MapName,TEXT("_hub")) && RpgClock-LastDayClock > 300.f )
		{
			// A new school day starts when you come back to a hub after a while.
			LastDayClock = RpgClock;
			State.Day++;
		}
		if( State.StockDay!=State.Day )
		{
			State.StockDay = State.Day;
			appMemzero( State.StockUsed, sizeof(State.StockUsed) );
		}
		RpgLog( TEXT("level %s, day %i"), MapName, State.Day );
	}

	void PreSave( UGameEngine* Engine )
	{
		if( !bEnabled )
			return;
		DestroyActorSafe( Engine, HatActor );
		DestroyActorSafe( Engine, PetActor );
		DestroyActorSafe( Engine, AuraActor );
		DestroyActorSafe( Engine, PreviewBody );
		DestroyActorSafe( Engine, PreviewAttach );
		DestroyActorSafe( Engine, PreviewItem );
		DestroyActorSafe( Engine, PreviewNpc );
		HatActorKind = PetActorKind = AuraActorKind = -1;
		EndAllSpells( Engine );
		EndWorld();
		for( INT o=0; o<Owls.Num(); o++ )
			DestroyActorSafe( Engine, Owls(o).Actor );
		Owls.Empty();
		bOwlsSpawned = 0;
		RestoreHeld();
		APlayerPawn* Harry = GetHarry( Engine );
		RestoreCostume( Harry );
		NearNpc = NULL;
		if( Harry )
		{
			// Saves keep stock movement values; the next tick re-applies skills.
			UObject* Defaults = Harry->GetClass()->GetDefaultObject();
			static const TCHAR* Props[3] = { TEXT("GroundRunSpeed"), TEXT("GroundJumpSpeed"), TEXT("JumpZ") };
			for( INT i=0; i<3; i++ )
			{
				const FLOAT Cur = GetFloatProp( Harry, Props[i], 0.f );
				const FLOAT Base = GetFloatProp( Defaults, Props[i], 0.f );
				if( Base>0.f && !Near(Cur,Base) )
				{
					if( i==0 && Near(Harry->GroundSpeed,Cur) )
						Harry->GroundSpeed = Base;
					SetFloatProp( Harry, Props[i], Base );
				}
			}
			LastRunSpeed = LastAirSpeed = LastJump = 0.f;
			SetBoolProp( Harry, TEXT("bCastFastSpells"), GetBoolProp(Defaults,TEXT("bCastFastSpells")) );
			UObject* Cursor = GetObjProp( Harry, TEXT("SpellCursor") );
			if( Cursor )
				SetFloatProp( Cursor, TEXT("fLOS_Distance"), GetFloatProp(Cursor->GetClass()->GetDefaultObject(),TEXT("fLOS_Distance"),0.f) );
		}
	}

	// Drops every pointer into the current level (before it is unloaded).
	void ForgetLevel()
	{
		LastLevel = NULL;
		LastHarry = NULL;
		HarryAnim = NULL;
		HatActor = PetActor = AuraActor = NULL;
		HatActorKind = PetActorKind = AuraActorKind = -1;
		PreviewBody = PreviewAttach = PreviewItem = PreviewNpc = NULL;
		HeldWeapon = NULL;
		HeldOriginalMesh = NULL;
		CostumeApplied = NULL;
		NearNpc = NULL;
		ShopVendor = NULL;
		LastHitStudent = NULL;
		ActorFlags.Empty();
		Deferred.Empty();
		ForgetSpells();
		ForgetWorld();
	}

	/*-------------------------------------------------------------------------
		Menus, HUD drawing, input and console commands.
	-------------------------------------------------------------------------*/

	// ApplyConsumable lives in the menus file; declared there.
	#include "HP2RpgMenus.inl"
	#include "HP2RpgSpells.inl"
	#include "HP2RpgWorld.inl"
};

static FHP2Rpg GRpg;

void FHP2Rpg::StaticOnScriptCall( UObject* Self, UFunction* Function, BYTE* Locals, UObject* Caller )
{
	GRpg.OnScriptCall( Self, Function, Locals, Caller );
}

void FHP2Rpg::StaticOnStateChange( UObject* Self, FName OldState, FName NewState )
{
	GRpg.OnStateChange( Self, OldState, NewState );
}

} // namespace

/*-----------------------------------------------------------------------------
	Engine entry points.
-----------------------------------------------------------------------------*/

void HP2RpgTick( UGameEngine* Engine, FLOAT DeltaSeconds )
{
	guard(HP2RpgTick);
	GRpg.Tick( Engine, DeltaSeconds );
	unguard;
}

void HP2RpgDraw( UGameEngine* Engine, UViewport* Viewport, UCanvas* Canvas )
{
	guard(HP2RpgDraw);
	GRpg.Draw( Engine, Viewport, Canvas );
	unguard;
}

UBOOL HP2RpgInputEvent( UGameEngine* Engine, UViewport* Viewport, EInputKey Key, EInputAction Action, FLOAT Delta )
{
	guard(HP2RpgInputEvent);
	return GRpg.InputEvent( Engine, Viewport, Key, Action, Delta );
	unguard;
}

UBOOL HP2RpgExec( UGameEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(HP2RpgExec);
	return GRpg.Exec( Engine, Cmd, Ar );
	unguard;
}

void HP2RpgPreSave( UGameEngine* Engine )
{
	guard(HP2RpgPreSave);
	GRpg.Init();
	GRpg.PreSave( Engine );
	unguard;
}

void HP2RpgPostSave( UGameEngine* Engine, INT Slot )
{
	guard(HP2RpgPostSave);
	GRpg.Init();
	if( GRpg.bEnabled )
		GRpg.SaveSlot( Slot );
	unguard;
}

void HP2RpgOnLevelUnloading( UGameEngine* Engine )
{
	guard(HP2RpgOnLevelUnloading);
	GRpg.Init();
	if( GRpg.Menu!=MENU_None && Engine->GLevel )
		GRpg.CloseMenu( Engine );
	if( Engine->GLevel && Engine->GLevel==GRpg.LastLevel )
	{
		GRpg.EndAllSpells( Engine );
		GRpg.EndWorld();
	}
	GRpg.ForgetLevel();
	GRpg.bLevelTransition = 1;
	unguard;
}

void HP2RpgOnLevelLoaded( UGameEngine* Engine )
{
	guard(HP2RpgOnLevelLoaded);
	// The old level and its actors are gone; forget every pointer into it
	// even if the new ULevel happens to reuse the old address.
	GRpg.ForgetLevel();
	unguard;
}

void HP2RpgOnLoadedSlot( UGameEngine* Engine, INT Slot )
{
	guard(HP2RpgOnLoadedSlot);
	GRpg.Init();
	if( GRpg.bEnabled )
	{
		GRpg.LoadSlot( Slot );
		GRpg.bLoadedFromSlot = 1;
		GRpg.LastLevel = NULL;
	}
	unguard;
}
