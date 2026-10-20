/*=============================================================================
	HP2Rpg.cpp: Native RPG layer for Harry Potter 2.

	Adds experience/levels, skill points (Health, Speed, Jump, Luck), a bean
	shop, a wardrobe (outfits, hats, glasses, held item) and live house point
	feedback on top of the retail game, without touching retail packages.

	Everything is reached by name on the running HGame objects:
	  Harry.managerStatus (StatusManager) for beans, house points, cards,
	  potions and health potential; Harry's movement properties for skills;
	  HPModels textures/meshes for the wardrobe.

	RPG progress lives in memory and is written to <SaveSlotPath>/SaveN.rpg
	whenever the game writes SaveN.usa, and read back when slot N loads.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnSkeletalMesh.h"
#include "HP2Rpg.h"
#include "HP2ScriptWatch.h"

namespace
{

/*-----------------------------------------------------------------------------
	Catalog.
-----------------------------------------------------------------------------*/

enum ERpgSkill { SKILL_Health, SKILL_Speed, SKILL_Jump, SKILL_Luck, SKILL_MAX };
enum { SKILL_MAX_PIPS = 5 };

static const TCHAR* GSkillNames[SKILL_MAX] = { TEXT("Health"), TEXT("Speed"), TEXT("Jump"), TEXT("Luck") };
static const TCHAR* GSkillDescs[SKILL_MAX] =
{
	TEXT("More maximum health (+1 health bar per point)"),
	TEXT("Run faster (+8% per point)"),
	TEXT("Jump higher and farther (+10% per point)"),
	TEXT("Chance of a bonus bean when you pick up beans"),
};

enum ERpgSlot { RSLOT_Outfit, RSLOT_Hat, RSLOT_Glasses, RSLOT_Held, RSLOT_MAX };
static const TCHAR* GSlotNames[RSLOT_MAX] = { TEXT("Outfit"), TEXT("Hat"), TEXT("Glasses"), TEXT("Held item") };

enum EOutfit { OUTFIT_Default, OUTFIT_Quidditch, OUTFIT_Slytherin, OUTFIT_Ravenclaw, OUTFIT_Hufflepuff, OUTFIT_Memory, OUTFIT_MAX };
enum EHat { HAT_None, HAT_SortingHat, HAT_Helmet, HAT_Cauldron, HAT_Teapot, HAT_MAX };
enum EGlasses { GLASSES_Default, GLASSES_None, GLASSES_Gold, GLASSES_Red, GLASSES_Green, GLASSES_MAX };
enum EHeld { HELD_Wand, HELD_Sword, HELD_Broom, HELD_MAX };

static const INT GSlotCounts[RSLOT_MAX] = { OUTFIT_MAX, HAT_MAX, GLASSES_MAX, HELD_MAX };

struct FWardrobeOption
{
	const TCHAR* Name;
	const TCHAR* Desc;
	INT Price;     // 0 = owned from the start
	INT OwnBit;    // -1 = always owned
};

static const FWardrobeOption GOutfits[OUTFIT_MAX] =
{
	{ TEXT("Gryffindor Uniform"), TEXT("Your own school robes."), 0, -1 },
	{ TEXT("Quidditch Robes"), TEXT("Scarlet and gold, straight from the pitch."), 250, 0 },
	{ TEXT("Slytherin Uniform"), TEXT("Green and silver. Draco would approve."), 200, 1 },
	{ TEXT("Ravenclaw Uniform"), TEXT("Blue and bronze, for the wise."), 200, 2 },
	{ TEXT("Hufflepuff Uniform"), TEXT("Yellow and black, loyal to the end."), 200, 3 },
	{ TEXT("Memory Robes"), TEXT("Look like a page from Tom Riddle's diary."), 150, 4 },
};
static const FWardrobeOption GHats[HAT_MAX] =
{
	{ TEXT("No Hat"), TEXT("Nothing on your head."), 0, -1 },
	{ TEXT("Sorting Hat"), TEXT("It still has opinions about you."), 200, 5 },
	{ TEXT("Knight's Helmet"), TEXT("Borrowed from a suit of armour."), 300, 6 },
	{ TEXT("Cauldron"), TEXT("Pewter, standard size 2."), 120, 7 },
	{ TEXT("Hagrid's Teapot"), TEXT("Nobody will dare ask."), 80, 8 },
};
static const FWardrobeOption GGlasses[GLASSES_MAX] =
{
	{ TEXT("Harry's Glasses"), TEXT("Round and famous."), 0, -1 },
	{ TEXT("No Glasses"), TEXT("You squint a bit."), 50, 9 },
	{ TEXT("Gold Frames"), TEXT("Very Lockhart."), 100, 10 },
	{ TEXT("Gryffindor Red Frames"), TEXT("House pride on your nose."), 100, 11 },
	{ TEXT("Slytherin Green Frames"), TEXT("Sneaky."), 100, 12 },
};
static const FWardrobeOption GHeld[HELD_MAX] =
{
	{ TEXT("Wand"), TEXT("Holly and phoenix feather."), 0, -1 },
	{ TEXT("Sword of Gryffindor"), TEXT("Looks the part. Spells still come from your wand."), 500, 13 },
	{ TEXT("Quidditch Broom"), TEXT("Carry it around like a seeker."), 250, 14 },
};

static const FWardrobeOption* GetSlotOptions( INT Slot )
{
	switch( Slot )
	{
		case RSLOT_Outfit:  return GOutfits;
		case RSLOT_Hat:     return GHats;
		case RSLOT_Glasses: return GGlasses;
		default:           return GHeld;
	}
}

enum EShopKind { SHOP_Potion, SHOP_FMucus, SHOP_WBark, SHOP_BronzeCard, SHOP_SilverCard, SHOP_SkillPoint, SHOP_Wardrobe };

struct FShopItem
{
	INT Category;
	INT Kind;
	INT Count;      // stack size for consumables
	INT Price;
	INT Slot;       // SHOP_Wardrobe only
	INT Option;     // SHOP_Wardrobe only
	const TCHAR* Name;
	const TCHAR* Desc;
};

enum EShopCategory { CAT_Potions, CAT_Ingredients, CAT_Cards, CAT_Clothes, CAT_Hats, CAT_Glasses, CAT_Gear, CAT_Training, CAT_MAX };
static const TCHAR* GShopCategoryNames[CAT_MAX] =
{
	TEXT("Potions"), TEXT("Ingredients"), TEXT("Cards"), TEXT("Clothes"), TEXT("Hats"), TEXT("Glasses"), TEXT("Gear"), TEXT("Training")
};

static const FShopItem GShopItems[] =
{
	{ CAT_Potions, SHOP_Potion, 1, 40, 0, 0, TEXT("Wiggenweld Potion"), TEXT("Restores your health when it runs out.") },
	{ CAT_Potions, SHOP_Potion, 3, 110, 0, 0, TEXT("Wiggenweld Potion x3"), TEXT("A small stock for the road.") },
	{ CAT_Potions, SHOP_Potion, 5, 175, 0, 0, TEXT("Wiggenweld Potion x5"), TEXT("Best value.") },
	{ CAT_Ingredients, SHOP_FMucus, 1, 15, 0, 0, TEXT("Flobberworm Mucus"), TEXT("Ingredient for the Wiggenweld Potion.") },
	{ CAT_Ingredients, SHOP_WBark, 1, 15, 0, 0, TEXT("Wiggentree Bark"), TEXT("Ingredient for the Wiggenweld Potion.") },
	{ CAT_Ingredients, SHOP_FMucus, 5, 65, 0, 0, TEXT("Flobberworm Mucus x5"), TEXT("A jar full.") },
	{ CAT_Ingredients, SHOP_WBark, 5, 65, 0, 0, TEXT("Wiggentree Bark x5"), TEXT("A bundle.") },
	{ CAT_Cards, SHOP_BronzeCard, 1, 60, 0, 0, TEXT("Bronze Wizard Card"), TEXT("A random bronze card you don't own yet.") },
	{ CAT_Cards, SHOP_SilverCard, 1, 150, 0, 0, TEXT("Silver Wizard Card"), TEXT("A random silver card you don't own yet.") },
	{ CAT_Clothes, SHOP_Wardrobe, 1, 0, RSLOT_Outfit, OUTFIT_Quidditch, NULL, NULL },
	{ CAT_Clothes, SHOP_Wardrobe, 1, 0, RSLOT_Outfit, OUTFIT_Slytherin, NULL, NULL },
	{ CAT_Clothes, SHOP_Wardrobe, 1, 0, RSLOT_Outfit, OUTFIT_Ravenclaw, NULL, NULL },
	{ CAT_Clothes, SHOP_Wardrobe, 1, 0, RSLOT_Outfit, OUTFIT_Hufflepuff, NULL, NULL },
	{ CAT_Clothes, SHOP_Wardrobe, 1, 0, RSLOT_Outfit, OUTFIT_Memory, NULL, NULL },
	{ CAT_Hats, SHOP_Wardrobe, 1, 0, RSLOT_Hat, HAT_SortingHat, NULL, NULL },
	{ CAT_Hats, SHOP_Wardrobe, 1, 0, RSLOT_Hat, HAT_Helmet, NULL, NULL },
	{ CAT_Hats, SHOP_Wardrobe, 1, 0, RSLOT_Hat, HAT_Cauldron, NULL, NULL },
	{ CAT_Hats, SHOP_Wardrobe, 1, 0, RSLOT_Hat, HAT_Teapot, NULL, NULL },
	{ CAT_Glasses, SHOP_Wardrobe, 1, 0, RSLOT_Glasses, GLASSES_None, NULL, NULL },
	{ CAT_Glasses, SHOP_Wardrobe, 1, 0, RSLOT_Glasses, GLASSES_Gold, NULL, NULL },
	{ CAT_Glasses, SHOP_Wardrobe, 1, 0, RSLOT_Glasses, GLASSES_Red, NULL, NULL },
	{ CAT_Glasses, SHOP_Wardrobe, 1, 0, RSLOT_Glasses, GLASSES_Green, NULL, NULL },
	{ CAT_Gear, SHOP_Wardrobe, 1, 0, RSLOT_Held, HELD_Sword, NULL, NULL },
	{ CAT_Gear, SHOP_Wardrobe, 1, 0, RSLOT_Held, HELD_Broom, NULL, NULL },
	{ CAT_Training, SHOP_SkillPoint, 1, 400, 0, 0, TEXT("Extra Lesson"), TEXT("A private lesson. Gives 1 skill point.") },
};
static const INT GNumShopItems = (INT)ARRAY_COUNT(GShopItems);

static const TCHAR* ShopItemName( const FShopItem& Item )
{
	return Item.Kind==SHOP_Wardrobe ? GetSlotOptions(Item.Slot)[Item.Option].Name : Item.Name;
}
static const TCHAR* ShopItemDesc( const FShopItem& Item )
{
	return Item.Kind==SHOP_Wardrobe ? GetSlotOptions(Item.Slot)[Item.Option].Desc : Item.Desc;
}
static INT ShopItemPrice( const FShopItem& Item )
{
	return Item.Kind==SHOP_Wardrobe ? GetSlotOptions(Item.Slot)[Item.Option].Price : Item.Price;
}

/*-----------------------------------------------------------------------------
	Persistent RPG state.
-----------------------------------------------------------------------------*/

struct FRpgState
{
	INT Xp;
	INT Level;
	INT SkillPoints;
	INT Skill[SKILL_MAX];
	INT AppliedHealthBonus;
	INT Owned;
	INT Equip[RSLOT_MAX];
	INT StudentsHit;

	void Reset()
	{
		appMemzero( this, sizeof(*this) );
		Level = 1;
	}
};

static INT XpForNextLevel( INT Level )
{
	return 100 + 50*(Level-1);
}

/*-----------------------------------------------------------------------------
	Feed messages (floating text).
-----------------------------------------------------------------------------*/

struct FRpgFeed
{
	FString Text;
	FColor Color;
	FLOAT Age;
};

enum EMenu { MENU_None, MENU_Shop, MENU_Character };
enum ECharTab { TAB_Skills, TAB_Wardrobe, TAB_MAX };

enum EHouse { HOUSE_Gryffindor, HOUSE_Slytherin, HOUSE_Ravenclaw, HOUSE_Hufflepuff, HOUSE_MAX };
static const TCHAR* GHouseNames[HOUSE_MAX] = { TEXT("Gryffindor"), TEXT("Slytherin"), TEXT("Ravenclaw"), TEXT("Hufflepuff") };
static const TCHAR* GHouseGetters[HOUSE_MAX] = { TEXT("GetHPointsG"), TEXT("GetHPointsS"), TEXT("GetHPointsR"), TEXT("GetHPointsH") };
static const TCHAR* GHouseCrests[HOUSE_MAX] = { TEXT("HP2GriffindorCrestSM"), TEXT("HP2SlytherinCrestSM"), TEXT("HP2RavenclawCrestSM"), TEXT("HP2HufflepuffCrestSM") };
static const FColor GHouseColors[HOUSE_MAX] = { FColor(236,72,52,255), FColor(64,200,96,255), FColor(80,140,255,255), FColor(255,214,64,255) };

struct FHatSpec
{
	const TCHAR* MeshPaths[3];
	FLOAT Scale;
	FLOAT Up;
	FLOAT Forward;
};
static FHatSpec GHatSpecs[HAT_MAX] =
{
	{ { NULL, NULL, NULL }, 1.f, 0.f, 0.f },
	{ { TEXT("HPModels.skSortingHatMesh"), TEXT("skSortingHatMesh"), NULL }, 0.45f, 3.f, 0.f },
	{ { TEXT("HProps.skArmorHelmetMesh"), TEXT("skArmorHelmetMesh"), NULL }, 0.78f, -6.f, 0.f },
	{ { TEXT("HProps.skCauldronSmallMesh"), TEXT("skCauldronSmallMesh"), NULL }, 0.6f, 2.f, 0.f },
	{ { TEXT("HProps.skDishesHagridTeaPotMesh"), TEXT("skDishesHagridTeaPotMesh"), NULL }, 0.55f, 2.f, 0.f },
};

static const TCHAR* GHeldMeshPaths[HELD_MAX][2] =
{
	{ NULL, NULL },
	{ TEXT("HPModels.skGryf_SwordMesh"), TEXT("skGryf_SwordMesh") },
	{ TEXT("HProps.skBroomQudditchMesh"), TEXT("skBroomQudditchMesh") },
};

/*-----------------------------------------------------------------------------
	Small RNG (kept separate from appRand so game RNG streams are untouched).
-----------------------------------------------------------------------------*/

static DWORD GRpgRandSeed = 0x2468ace1;
static INT RpgRand( INT Range )
{
	GRpgRandSeed = GRpgRandSeed*1664525u + 1013904223u;
	return Range>0 ? (INT)((GRpgRandSeed>>8) % (DWORD)Range) : 0;
}

/*-----------------------------------------------------------------------------
	Reflection helpers.
-----------------------------------------------------------------------------*/

static UClass* FindClass( const TCHAR* Name )
{
	return FindObject<UClass>( ANY_PACKAGE, Name );
}

static INT GetIntProp( UObject* Obj, const TCHAR* Name, INT Default=0 )
{
	UIntProperty* P = Obj ? FindField<UIntProperty>( Obj->GetClass(), Name ) : NULL;
	return P ? *(INT*)((BYTE*)Obj + P->Offset) : Default;
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

struct FRpgArg
{
	INT Int;
	FLOAT Float;
	UObject* Obj;
	FRpgArg() : Int(0), Float(0.f), Obj(NULL) {}
	static FRpgArg I( INT V ) { FRpgArg A; A.Int=V; return A; }
	static FRpgArg O( UObject* V ) { FRpgArg A; A.Obj=V; return A; }
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
	else if( Cast<UObjectProperty>(P) ) *(UObject**)Addr = A.Obj;
}

static void ReadArg( UProperty* P, BYTE* Addr, FRpgArg& A )
{
	if( Cast<UIntProperty>(P) )         A.Int = *(INT*)Addr;
	else if( Cast<UFloatProperty>(P) )  A.Float = *(FLOAT*)Addr;
	else if( Cast<UByteProperty>(P) )   A.Int = *(BYTE*)Addr;
	else if( Cast<UBoolProperty>(P) )   A.Int = (*(BITFIELD*)Addr & ((UBoolProperty*)P)->BitMask)!=0;
	else if( Cast<UObjectProperty>(P) ) A.Obj = *(UObject**)Addr;
}

// Calls a script function by name with int/float/bool/object arguments.
static UBOOL CallScript( UObject* Obj, const TCHAR* FuncName, const FRpgArg* Args=NULL, INT NumArgs=0, FRpgArg* Ret=NULL )
{
	if( !Obj )
		return 0;
	FName Name( FuncName, FNAME_Find );
	if( Name==NAME_None )
		return 0;
	UFunction* Function = Obj->FindFunction( Name );
	if( !Function )
		return 0;

	BYTE Parms[512];
	if( Function->ParmsSize > sizeof(Parms) )
		return 0;
	appMemzero( Parms, sizeof(Parms) );

	INT ArgIndex = 0;
	for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
	{
		if( It->PropertyFlags & CPF_ReturnParm )
			continue;
		if( Cast<UStrProperty>(*It) )
			return 0; // String parameters are not supported here.
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

static UObject* LoadAny( UClass* Class, const TCHAR* const* Paths, INT NumPaths )
{
	for( INT i=0; i<NumPaths; i++ )
	{
		if( !Paths[i] )
			continue;
		UObject* Obj = NULL;
		if( appStrfind(Paths[i],TEXT(".")) )
			Obj = UObject::StaticLoadObject( Class, NULL, Paths[i], NULL, LOAD_NoWarn|LOAD_Quiet, NULL );
		else
			Obj = UObject::StaticFindObject( Class, ANY_PACKAGE, Paths[i] );
		if( Obj )
		{
			// Callers cache these across level loads.
			Obj->AddToRoot();
			return Obj;
		}
	}
	return NULL;
}

static UTexture* FindTexture( const TCHAR* Package, const TCHAR* Name )
{
	UTexture* Tex = FindObject<UTexture>( ANY_PACKAGE, Name );
	if( !Tex && Package )
	{
		UObject::LoadPackage( NULL, Package, LOAD_NoWarn|LOAD_Quiet );
		Tex = FindObject<UTexture>( ANY_PACKAGE, Name );
	}
	return Tex;
}

/*-----------------------------------------------------------------------------
	The RPG layer.
-----------------------------------------------------------------------------*/

class FHP2Rpg
{
public:
	UBOOL bInitialized;
	UBOOL bEnabled;
	FRpgState State;

	// Level tracking.
	ULevel* LastLevel;
	APlayerPawn* LastHarry;
	UBOOL bLoadedFromSlot;

	// Baselines for diff-based rewards (reset on level/pawn change).
	UBOOL bBaselineValid;
	INT LastHouse[HOUSE_MAX];
	INT LastCards;
	INT LastStars;
	INT LastSpells;
	INT LastBeans;
	INT PendingHouse[HOUSE_MAX];
	FLOAT PendingHouseTimer[HOUSE_MAX];
	FLOAT PollTimer;
	TArray<AActor*> RewardedCreatures;
	FLOAT HouseHudTimer;

	// Feed.
	TArray<FRpgFeed> Feed;

	// Menu.
	INT Menu;
	INT CharTab;
	INT SkillRow;
	INT WardrobeRow;
	INT ShopCategory;
	INT ShopRow;
	FString MenuNotice;
	FLOAT MenuNoticeTime;
	UBOOL bWePaused;
	AActor* NearStudent;

	// Student-hit bookkeeping.
	AActor* LastHitStudent;
	FLOAT LastHitTime;
	FLOAT RpgClock;

	// Wardrobe.
	UTexture* HarryTex[4];
	UPalette* OriginalPalette[4];
	UMesh* HarryMesh;
	TArray<UPalette*> CustomPalettes;
	INT CustomPaletteKeys[64];
	TArray<BYTE> GlassesBackup[8];
	UBOOL bGlassesHidden;
	INT AppliedOutfit;
	INT AppliedGlasses;
	AActor* HatActor;
	INT HatActorKind;
	AActor* HeldWeapon;
	UMesh* HeldOriginalMesh;
	FLOAT HeldOriginalScale;
	UBOOL bWardrobeDirty;
	FVector HatTweak;  // up, forward, side offsets added on top of the hat spec
	FLOAT HatScaleTweak;

	// UI.
	UTexture* WhiteTex;
	UTexture* Crests[HOUSE_MAX];
	UBOOL bUiResolved;

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
		State.Reset();
		AppliedOutfit = -1;
		AppliedGlasses = -1;
		HatScaleTweak = 1.f;
		Menu = MENU_None;
		GHP2ScriptWatchNameIndex = FName(TEXT("OnSpellHit")).GetIndex();
		GHP2ScriptWatch = bEnabled ? &FHP2Rpg::StaticOnScriptCall : NULL;
		debugf( NAME_Log, TEXT("HP2Rpg: %s"), bEnabled ? TEXT("enabled") : TEXT("disabled") );
	}

	static void StaticOnScriptCall( UObject* Self, UFunction* Function, BYTE* Locals );

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

	UObject* GetStatus( APlayerPawn* Harry )
	{
		return GetObjProp( Harry, TEXT("managerStatus") );
	}

	/*-------------------------------------------------------------------------
		Persistence.
	-------------------------------------------------------------------------*/

	FString SidecarPath( INT Slot )
	{
		return FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Save%i.rpg"), *GSys->SaveSlotPath, Slot );
	}

	void SaveSlot( INT Slot )
	{
		FString Text = FString::Printf
		(
			TEXT("HP2RPG Version=1 Xp=%i Level=%i SkillPoints=%i Health=%i Speed=%i Jump=%i Luck=%i HealthBonus=%i Owned=%i Outfit=%i Hat=%i Glasses=%i Held=%i StudentsHit=%i\r\n"),
			State.Xp, State.Level, State.SkillPoints,
			State.Skill[SKILL_Health], State.Skill[SKILL_Speed], State.Skill[SKILL_Jump], State.Skill[SKILL_Luck],
			State.AppliedHealthBonus, State.Owned,
			State.Equip[RSLOT_Outfit], State.Equip[RSLOT_Hat], State.Equip[RSLOT_Glasses], State.Equip[RSLOT_Held],
			State.StudentsHit
		);
		const FString Path = SidecarPath( Slot );
		if( !appSaveStringToFile( Text, *Path ) )
			debugf( NAME_Warning, TEXT("HP2Rpg: could not write %s"), *Path );
	}

	void LoadSlot( INT Slot )
	{
		State.Reset();
		FString Text;
		const FString Path = SidecarPath( Slot );
		if( appLoadFileToString( Text, *Path ) )
		{
			const TCHAR* S = *Text;
			Parse( S, TEXT("Xp="), State.Xp );
			Parse( S, TEXT("Level="), State.Level );
			Parse( S, TEXT("SkillPoints="), State.SkillPoints );
			Parse( S, TEXT("Health="), State.Skill[SKILL_Health] );
			Parse( S, TEXT("Speed="), State.Skill[SKILL_Speed] );
			Parse( S, TEXT("Jump="), State.Skill[SKILL_Jump] );
			Parse( S, TEXT("Luck="), State.Skill[SKILL_Luck] );
			Parse( S, TEXT("HealthBonus="), State.AppliedHealthBonus );
			Parse( S, TEXT("Owned="), State.Owned );
			Parse( S, TEXT("Outfit="), State.Equip[RSLOT_Outfit] );
			Parse( S, TEXT("Hat="), State.Equip[RSLOT_Hat] );
			Parse( S, TEXT("Glasses="), State.Equip[RSLOT_Glasses] );
			Parse( S, TEXT("Held="), State.Equip[RSLOT_Held] );
			Parse( S, TEXT("StudentsHit="), State.StudentsHit );
		}
		Sanitize();
		bWardrobeDirty = 1;
	}

	void Sanitize()
	{
		State.Level = Clamp( State.Level, 1, 99 );
		State.Xp = Max( State.Xp, 0 );
		State.SkillPoints = Clamp( State.SkillPoints, 0, 99 );
		for( INT i=0; i<SKILL_MAX; i++ )
			State.Skill[i] = Clamp( State.Skill[i], 0, (INT)SKILL_MAX_PIPS );
		for( INT i=0; i<RSLOT_MAX; i++ )
			if( State.Equip[i]<0 || State.Equip[i]>=GSlotCounts[i] || !IsOwned(i,State.Equip[i]) )
				State.Equip[i] = 0;
	}

	UBOOL IsOwned( INT Slot, INT Option )
	{
		const INT Bit = GetSlotOptions(Slot)[Option].OwnBit;
		return Bit<0 || (State.Owned & (1<<Bit))!=0;
	}

	/*-------------------------------------------------------------------------
		Feed.
	-------------------------------------------------------------------------*/

	void AddFeed( const FString& Text, FColor Color )
	{
		if( Feed.Num()>=6 )
			Feed.Remove( 0 );
		FRpgFeed* Entry = new(Feed) FRpgFeed;
		Entry->Text = Text;
		Entry->Color = Color;
		Entry->Age = 0.f;
	}

	/*-------------------------------------------------------------------------
		Experience.
	-------------------------------------------------------------------------*/

	void GiveXp( INT Amount, const TCHAR* Reason, APlayerPawn* Harry )
	{
		if( Amount<=0 )
			return;
		State.Xp += Amount;
		AddFeed( FString::Printf(TEXT("+%i XP  %s"), Amount, Reason), FColor(170,220,255,255) );
		while( State.Xp >= XpForNextLevel(State.Level) && State.Level<99 )
		{
			State.Xp -= XpForNextLevel( State.Level );
			State.Level++;
			State.SkillPoints++;
			AddFeed( FString::Printf(TEXT("Level %i!  +1 skill point (press K)"), State.Level), FColor(255,220,90,255) );
			// A level up earns Gryffindor a few points, like a good lesson would.
			AddHousePoints( Harry, HOUSE_Gryffindor, 5 );
		}
	}

	void AddHousePoints( APlayerPawn* Harry, INT House, INT Points )
	{
		static const TCHAR* Items[HOUSE_MAX] =
		{
			TEXT("StatusItemGryffindorPts"), TEXT("StatusItemSlytherinPts"), TEXT("StatusItemRavenclawPts"), TEXT("StatusItemHufflepuffPts")
		};
		UObject* Status = GetStatus( Harry );
		UClass* Group = FindClass( TEXT("StatusGroupHousePoints") );
		UClass* Item = FindClass( Items[House] );
		if( !Status || !Group || !Item )
			return;
		FRpgArg Args[3] = { FRpgArg::O(Group), FRpgArg::O(Item), FRpgArg::I(Points) };
		CallScript( Status, TEXT("IncrementCount"), Args, 3 );
	}

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

	INT CountCards( UObject* Status )
	{
		return GetStatusCount( Status, TEXT("StatusGroupWizardCards"), TEXT("StatusItemBronzeCards") )
			+ GetStatusCount( Status, TEXT("StatusGroupWizardCards"), TEXT("StatusItemSilverCards") )
			+ GetStatusCount( Status, TEXT("StatusGroupWizardCards"), TEXT("StatusItemGoldCards") );
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

	void PollRewards( APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		UObject* Status = GetStatus( Harry );
		if( !Status )
			return;

		INT House[HOUSE_MAX];
		for( INT i=0; i<HOUSE_MAX; i++ )
			House[i] = CallInt( Status, GHouseGetters[i] );
		const INT Cards = CountCards( Status );
		const INT Stars = GetStatusCount( Status, TEXT("StatusGroupStars"), TEXT("StatusItemStars") );
		const INT Spells = CountSpells( Harry );
		const INT Beans = CallInt( Status, TEXT("GetBeanCount") );

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
			LastBeans = Beans;
			return;
		}

		// House points: merge bursts (challenge tallies) into one message.
		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			const INT Diff = House[i] - LastHouse[i];
			if( Diff )
			{
				PendingHouse[i] += Diff;
				PendingHouseTimer[i] = 0.f;
				HouseHudTimer = 6.f;
				if( i==HOUSE_Gryffindor && Diff>0 )
					GiveXp( Diff/2, TEXT("house points"), Harry );
			}
			LastHouse[i] = House[i];
		}

		if( Cards > LastCards )
			GiveXp( 25*(Cards-LastCards), TEXT("wizard card"), Harry );
		LastCards = Cards;

		if( Stars > LastStars )
			GiveXp( 5*(Stars-LastStars), TEXT("challenge star"), Harry );
		LastStars = Stars;

		if( Spells > LastSpells )
			GiveXp( 100*(Spells-LastSpells), TEXT("new spell learned"), Harry );
		LastSpells = Spells;

		// Luck: bonus beans on pickup.
		if( Beans > LastBeans && State.Skill[SKILL_Luck]>0 )
		{
			INT Bonus = 0;
			for( INT i=0; i<Min(Beans-LastBeans,50); i++ )
				if( RpgRand(100) < 8*State.Skill[SKILL_Luck] )
					Bonus++;
			if( Bonus )
			{
				FRpgArg Arg = FRpgArg::I( Bonus );
				CallScript( Status, TEXT("AddBeans"), &Arg, 1 );
				AddFeed( FString::Printf(TEXT("+%i lucky bean%s"), Bonus, Bonus==1 ? TEXT("") : TEXT("s")), FColor(140,255,140,255) );
				LastBeans = CallInt( Status, TEXT("GetBeanCount") );
			}
			else
				LastBeans = Beans;
		}
		else
			LastBeans = Beans;
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
			if( P>0 )
				AddFeed( FString::Printf(TEXT("%s gains %i point%s"), GHouseNames[i], P, P==1 ? TEXT("") : TEXT("s")), GHouseColors[i] );
			else
				AddFeed( FString::Printf(TEXT("%s loses %i point%s"), GHouseNames[i], -P, P==-1 ? TEXT("") : TEXT("s")), GHouseColors[i] );
			PendingHouse[i] = 0;
		}
	}

	UBOOL IsDeathState( FName StateName )
	{
		static FName Names[6];
		static UBOOL bInit = 0;
		if( !bInit )
		{
			bInit = 1;
			Names[0] = FName( TEXT("stateDied") );
			Names[1] = FName( TEXT("BlowUpAndDie") );
			Names[2] = FName( TEXT("DeadSpider") );
			Names[3] = FName( TEXT("OutForTheCount") );
			Names[4] = FName( TEXT("KillDoxie") );
			Names[5] = FName( TEXT("stayFlipped") );
		}
		for( INT i=0; i<ARRAY_COUNT(Names); i++ )
			if( StateName==Names[i] )
				return 1;
		return 0;
	}

	void PollCreatures( UGameEngine* Engine, APlayerPawn* Harry )
	{
		static UClass* HChar = NULL;
		static UClass* Firecrab = NULL;
		static UClass* SpiderLarge = NULL;
		if( !HChar )
		{
			HChar = FindClass( TEXT("HChar") );
			Firecrab = FindClass( TEXT("firecrab") );
			SpiderLarge = FindClass( TEXT("SpiderLarge") );
		}
		if( !HChar )
			return;
		NearStudent = NULL;
		FLOAT NearestDist = 170.f;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* Actor = Engine->GLevel->Actors(i);
			if( !Actor || Actor->bDeleteMe || Actor==Harry || !Actor->IsA(HChar) )
				continue;
			if( IsStudent(Actor) || IsVendor(Actor) )
			{
				if( !Actor->bHidden )
				{
					const FLOAT Dist = (Actor->Location - Harry->Location).Size();
					if( Dist < NearestDist )
					{
						NearestDist = Dist;
						NearStudent = Actor;
					}
				}
				continue;
			}
			if( !IsDeathState(GetStateName(Actor)) || RewardedCreatures.FindItemIndex(Actor)!=INDEX_NONE )
				continue;
			RewardedCreatures.AddItem( Actor );
			if( Firecrab && Actor->IsA(Firecrab) )
				GiveXp( 10, TEXT("fire crab flipped"), Harry );
			else if( SpiderLarge && Actor->IsA(SpiderLarge) )
				GiveXp( 40, TEXT("creature defeated"), Harry );
			else
				GiveXp( 20, TEXT("creature defeated"), Harry );
		}
	}

	UBOOL IsStudent( AActor* Actor )
	{
		static UClass* Classes[4];
		static UBOOL bInit = 0;
		if( !bInit )
		{
			bInit = 1;
			Classes[0] = FindClass( TEXT("GFemGry1") );
			Classes[1] = FindClass( TEXT("GOldMaleGry1") );
			Classes[2] = FindClass( TEXT("GenMale1") );
			Classes[3] = FindClass( TEXT("GenMale2") );
		}
		for( INT i=0; i<ARRAY_COUNT(Classes); i++ )
			if( Classes[i] && Actor->IsA(Classes[i]) )
				return 1;
		return 0;
	}

	UBOOL IsVendor( AActor* Actor )
	{
		static UClass* Classes[2];
		static UBOOL bInit = 0;
		if( !bInit )
		{
			bInit = 1;
			Classes[0] = FindClass( TEXT("FredWeasley") );
			Classes[1] = FindClass( TEXT("GeorgeWeasley") );
		}
		for( INT i=0; i<ARRAY_COUNT(Classes); i++ )
			if( Classes[i] && Actor->IsA(Classes[i]) )
				return 1;
		return 0;
	}

	void OnScriptCall( UObject* Self, UFunction* Function, BYTE* Locals )
	{
		// OnSpellHit( optional baseSpell spell, optional vector vHitLocation ) on the victim.
		if( !bEnabled || !Self->IsA(AActor::StaticClass()) || !IsStudent((AActor*)Self) )
			return;
		UObjectProperty* SpellParm = NULL;
		for( TFieldIterator<UProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It )
		{
			SpellParm = Cast<UObjectProperty>( *It );
			break;
		}
		if( !SpellParm )
			return;
		AActor* Spell = *(AActor**)(Locals + SpellParm->Offset);
		AActor* Caster = Spell ? Spell->Owner : NULL;
		if( !Caster || !Caster->IsA(APlayerPawn::StaticClass()) )
			return;
		if( Self==LastHitStudent && RpgClock-LastHitTime < 1.5f )
			return;
		LastHitStudent = (AActor*)Self;
		LastHitTime = RpgClock;
		State.StudentsHit++;
		AddHousePoints( (APlayerPawn*)Caster, HOUSE_Gryffindor, -5 );
	}

	/*-------------------------------------------------------------------------
		Skills.
	-------------------------------------------------------------------------*/

	void ApplySkills( APlayerPawn* Harry )
	{
		APawn* Defaults = (APawn*)Harry->GetClass()->GetDefaultObject();

		// Health: +100 health potential (one HUD bar) per point.
		const INT DesiredBonus = 100*State.Skill[SKILL_Health];
		if( DesiredBonus!=State.AppliedHealthBonus && !GetBoolProp(Harry,TEXT("bInDuelingMode")) )
		{
			UObject* Status = GetStatus( Harry );
			if( Status )
			{
				FRpgArg Arg = FRpgArg::I( DesiredBonus - State.AppliedHealthBonus );
				if( CallScript( Status, TEXT("AddHealthPotential"), &Arg, 1 ) )
					State.AppliedHealthBonus = DesiredBonus;
			}
		}

		// Speed: scale run speed only while the game is using the stock value,
		// so web/sleepy/sword effects keep working.
		const FLOAT BaseRun = Defaults->GroundRunSpeed;
		const FLOAT WantRun = BaseRun*(1.f + 0.08f*State.Skill[SKILL_Speed]);
		if( Harry->GroundRunSpeed!=WantRun && (Harry->GroundRunSpeed==BaseRun || IsScaled(Harry->GroundRunSpeed,BaseRun)) )
		{
			if( Harry->GroundSpeed==Harry->GroundRunSpeed )
				Harry->GroundSpeed = WantRun;
			Harry->GroundRunSpeed = WantRun;
		}
		const FLOAT BaseAir = GetFloatProp( Defaults, TEXT("GroundJumpSpeed"), 0.f );
		const FLOAT CurAir = GetFloatProp( Harry, TEXT("GroundJumpSpeed"), 0.f );
		const FLOAT WantAir = BaseAir*(1.f + 0.08f*State.Skill[SKILL_Speed]);
		if( BaseAir>0.f && CurAir!=WantAir && (CurAir==BaseAir || IsScaled(CurAir,BaseAir)) )
			SetFloatProp( Harry, TEXT("GroundJumpSpeed"), WantAir );

		// Jump.
		const FLOAT BaseJump = Defaults->JumpZ;
		const FLOAT WantJump = BaseJump*(1.f + 0.10f*State.Skill[SKILL_Jump]);
		if( Harry->JumpZ!=WantJump && (Harry->JumpZ==BaseJump || IsScaled(Harry->JumpZ,BaseJump)) )
			Harry->JumpZ = WantJump;
	}

	// True when Value is Base scaled by one of our own skill multipliers.
	static UBOOL IsScaled( FLOAT Value, FLOAT Base )
	{
		for( INT Pips=1; Pips<=SKILL_MAX_PIPS; Pips++ )
		{
			if( Abs(Value - Base*(1.f+0.08f*Pips)) < 0.01f )
				return 1;
			if( Abs(Value - Base*(1.f+0.10f*Pips)) < 0.01f )
				return 1;
		}
		return 0;
	}

	/*-------------------------------------------------------------------------
		Wardrobe.
	-------------------------------------------------------------------------*/

	UBOOL ResolveHarryAssets( APlayerPawn* Harry )
	{
		if( !HarryMesh )
		{
			if( Harry->Mesh && appStricmp(Harry->Mesh->GetName(),TEXT("skharryMesh"))==0 )
				HarryMesh = Harry->Mesh;
			else
				HarryMesh = (UMesh*)UObject::StaticFindObject( UMesh::StaticClass(), ANY_PACKAGE, TEXT("skharryMesh") );
		}
		static const TCHAR* Names[4] = { TEXT("skharryTex0"), TEXT("skharryTex1"), TEXT("skharryTex2"), TEXT("skharryTex3") };
		for( INT i=0; i<4; i++ )
			if( !HarryTex[i] )
			{
				HarryTex[i] = FindTexture( TEXT("HPModels"), Names[i] );
				if( HarryTex[i] )
				{
					// Keep everything we hold across LoadMap's garbage collection.
					HarryTex[i]->AddToRoot();
					OriginalPalette[i] = HarryTex[i]->Palette;
					if( OriginalPalette[i] )
						OriginalPalette[i]->AddToRoot();
				}
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

	// Variant keys: 1..4 house recolors, 5 sepia, 10+n glasses frame colors.
	UPalette* GetPalette( INT TexIndex, INT Variant )
	{
		const INT Key = TexIndex*100 + Variant;
		for( INT i=0; i<CustomPalettes.Num(); i++ )
			if( CustomPaletteKeys[i]==Key )
				return CustomPalettes(i);
		UPalette* Source = OriginalPalette[TexIndex];
		if( !Source || CustomPalettes.Num()>=ARRAY_COUNT(CustomPaletteKeys) )
			return NULL;

		UPalette* Palette = ConstructObject<UPalette>( UPalette::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Transient );
		Palette->AddToRoot();
		Palette->Colors = Source->Colors;
		for( INT i=0; i<Palette->Colors.Num(); i++ )
		{
			FColor& C = Palette->Colors(i);
			if( TexIndex==3 )
			{
				// Glasses: entry 0 is the mask, entry 1 the frame.
				if( i==1 )
				{
					if( Variant==10+GLASSES_Gold )       C = FColor(212,170,60,C.A);
					else if( Variant==10+GLASSES_Red )   C = FColor(170,24,24,C.A);
					else if( Variant==10+GLASSES_Green ) C = FColor(20,120,50,C.A);
				}
				continue;
			}
			const FLOAT R = C.R/255.f, G = C.G/255.f, B = C.B/255.f;
			if( Variant==OUTFIT_Memory )
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
				case OUTFIT_Slytherin:
					if( bRed ) HsvToRgb( 0.36f, S*0.85f, V*0.85f, NR, NG, NB );
					else       HsvToRgb( 0.f, 0.05f, Min(1.f,V*1.05f), NR, NG, NB );
					break;
				case OUTFIT_Ravenclaw:
					if( bRed ) HsvToRgb( 0.62f, S*0.9f, V, NR, NG, NB );
					else       HsvToRgb( 0.07f, S*0.75f, V*0.8f, NR, NG, NB );
					break;
				case OUTFIT_Hufflepuff:
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

	void ApplyTextures()
	{
		const INT Outfit = State.Equip[RSLOT_Outfit];
		const INT Glasses = State.Equip[RSLOT_Glasses];
		if( Outfit==AppliedOutfit && Glasses==AppliedGlasses )
			return;

		// Robe (tex1), body (tex0) and face (tex2).
		for( INT t=0; t<3; t++ )
		{
			UPalette* Palette = OriginalPalette[t];
			if( Outfit==OUTFIT_Memory )
				Palette = GetPalette( t, OUTFIT_Memory );
			else if( t==1 && Outfit>=OUTFIT_Slytherin && Outfit<=OUTFIT_Hufflepuff )
				Palette = GetPalette( t, Outfit );
			SetTexturePalette( t, Palette ? Palette : OriginalPalette[t] );
		}

		// Glasses (tex3).
		SetGlassesHidden( Glasses==GLASSES_None );
		if( Glasses==GLASSES_Gold || Glasses==GLASSES_Red || Glasses==GLASSES_Green )
			SetTexturePalette( 3, GetPalette(3,10+Glasses) );
		else
			SetTexturePalette( 3, OriginalPalette[3] );

		AppliedOutfit = Outfit;
		AppliedGlasses = Glasses;
	}

	void ApplyQuidditchSkins( APlayerPawn* Harry, UBOOL bOn )
	{
		static UTexture* Quid[3] = { NULL, NULL, NULL };
		static const TCHAR* Names[3] = { TEXT("skharryquidTex0"), TEXT("skharryquidTex1"), TEXT("skharryquidTex2") };
		for( INT i=0; i<3; i++ )
		{
			if( !Quid[i] )
			{
				Quid[i] = FindTexture( TEXT("HPModels"), Names[i] );
				if( Quid[i] )
					Quid[i]->AddToRoot();
			}
			if( !Quid[i] )
				return;
		}
		for( INT i=0; i<3; i++ )
		{
			if( bOn )
				Harry->MultiSkins[i] = Quid[i];
			else if( Harry->MultiSkins[i]==Quid[i] )
				Harry->MultiSkins[i] = NULL;
		}
	}

	void DestroyHat( UGameEngine* Engine )
	{
		if( HatActor && Engine->GLevel && !HatActor->bDeleteMe )
			Engine->GLevel->DestroyActor( HatActor );
		HatActor = NULL;
		HatActorKind = HAT_None;
	}

	void UpdateHat( UGameEngine* Engine, APlayerPawn* Harry, UBOOL bWearingHarryMesh )
	{
		const INT Hat = State.Equip[RSLOT_Hat];
		if( Hat==HAT_None || !bWearingHarryMesh )
		{
			DestroyHat( Engine );
			return;
		}
		if( HatActor && (HatActor->bDeleteMe || HatActor->XLevel!=Engine->GLevel || HatActorKind!=Hat) )
			DestroyHat( Engine );
		if( !HatActor )
		{
			UMesh* Mesh = (UMesh*)LoadAny( UMesh::StaticClass(), GHatSpecs[Hat].MeshPaths, 3 );
			UClass* EffectsClass = FindClass( TEXT("Effects") );
			if( !Mesh || !EffectsClass || (EffectsClass->ClassFlags & CLASS_Abstract) )
				return;
			HatActor = Engine->GLevel->SpawnActor( EffectsClass, NAME_None, NULL, NULL, Harry->Location, Harry->Rotation, NULL, 1 );
			if( !HatActor )
				return;
			HatActor->SetCollision( 0, 0, 0 );
			HatActor->bCollideWorld = 0;
			HatActor->Physics = PHYS_None;
			HatActor->DrawType = DT_Mesh;
			HatActor->Mesh = Mesh;
			HatActor->AmbientGlow = Harry->AmbientGlow;
			HatActor->Tag = FName( TEXT("HP2RpgHat") );
			HatActorKind = Hat;
		}

		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>( Harry->Mesh );
		static FName HeadNub( TEXT("Bip01 HeadNub") );
		const INT Bone = SkelMesh ? SkelMesh->BoneIndex( HeadNub ) : -1;
		if( Bone<0 )
		{
			HatActor->bHidden = 1;
			return;
		}
		const FCoords Bonec = SkelMesh->GetBoneCoords( Harry, Bone );
		const FHatSpec& Spec = GHatSpecs[Hat];
		// Head X points up the head, Y forward, Z sideways; hats are Z-up.
		FCoords HatCoords = Bonec;
		HatCoords.XAxis = Bonec.YAxis;
		HatCoords.ZAxis = Bonec.XAxis;
		HatCoords.YAxis = HatCoords.ZAxis ^ HatCoords.XAxis;
		HatActor->Location = Bonec.Origin
			+ Bonec.XAxis.SafeNormal()*(Spec.Up + HatTweak.X)
			+ Bonec.YAxis.SafeNormal()*(Spec.Forward + HatTweak.Y)
			+ Bonec.ZAxis.SafeNormal()*HatTweak.Z;
		HatActor->Rotation = HatCoords.OrthoRotation();
		HatActor->DrawScale = Spec.Scale*HatScaleTweak*Harry->DrawScale;
		HatActor->bHidden = Harry->bHidden;
		HatActor->ScaleGlow = Harry->ScaleGlow;
	}

	void RestoreHeld()
	{
		if( HeldWeapon && !HeldWeapon->bDeleteMe && HeldOriginalMesh )
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
		const INT Held = State.Equip[RSLOT_Held];
		AWeapon* Weapon = Harry->Weapon;
		if( HeldWeapon && HeldWeapon!=Weapon )
			RestoreHeld();
		if( Held==HELD_Wand || !Weapon )
		{
			RestoreHeld();
			return;
		}
		UMesh* Mesh = (UMesh*)LoadAny( UMesh::StaticClass(), GHeldMeshPaths[Held], 2 );
		if( !Mesh )
			return;
		static UMesh* SwordMesh = NULL;
		if( !SwordMesh )
			SwordMesh = (UMesh*)LoadAny( UMesh::StaticClass(), GHeldMeshPaths[HELD_Sword], 2 );
		if( Weapon->ThirdPersonMesh==SwordMesh && HeldWeapon==NULL )
			return; // The game itself is using the sword right now.
		if( !HeldWeapon )
		{
			HeldWeapon = Weapon;
			HeldOriginalMesh = Weapon->ThirdPersonMesh;
			HeldOriginalScale = Weapon->ThirdPersonScale;
		}
		if( Weapon->ThirdPersonMesh!=Mesh )
		{
			Weapon->ThirdPersonMesh = Mesh;
			Weapon->ThirdPersonScale = Held==HELD_Broom ? HeldOriginalScale*0.7f : HeldOriginalScale;
		}
	}

	void UpdateWardrobe( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !ResolveHarryAssets(Harry) )
			return;
		const UBOOL bHarryMesh = Harry->Mesh==HarryMesh;
		if( bWardrobeDirty )
		{
			AppliedOutfit = -1;
			AppliedGlasses = -1;
			bWardrobeDirty = 0;
		}
		ApplyTextures();
		ApplyQuidditchSkins( Harry, bHarryMesh && State.Equip[RSLOT_Outfit]==OUTFIT_Quidditch );
		UpdateHat( Engine, Harry, bHarryMesh );
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
		if( !bEnabled || !Engine->GLevel || Engine->Replay.Replaying() || Engine->Replay.Recording() )
			return;
		RpgClock += DeltaSeconds;

		if( Engine->GLevel!=LastLevel )
		{
			LastLevel = Engine->GLevel;
			OnLevelChanged( Engine );
		}

		APlayerPawn* Harry = GetHarry( Engine );
		if( Harry!=LastHarry )
		{
			LastHarry = Harry;
			bBaselineValid = 0;
			bWardrobeDirty = 1;
			HeldWeapon = NULL;
			HeldOriginalMesh = NULL;
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
			if( Feed(i).Age > 4.5f )
				Feed.Remove( i );
		}
		HouseHudTimer = Max( 0.f, HouseHudTimer-DeltaSeconds );
		MenuNoticeTime = Max( 0.f, MenuNoticeTime-DeltaSeconds );

		PollTimer += DeltaSeconds;
		if( PollTimer >= 0.2f )
		{
			PollTimer = 0.f;
			PollRewards( Harry, DeltaSeconds );
			PollCreatures( Engine, Harry );
			ApplySkills( Harry );
		}
		FlushHouseFeed( DeltaSeconds );
		UpdateWardrobe( Engine, Harry );
	}

	void OnLevelChanged( UGameEngine* Engine )
	{
		RewardedCreatures.Empty();
		bBaselineValid = 0;
		HatActor = NULL;
		HatActorKind = HAT_None;
		HeldWeapon = NULL;
		HeldOriginalMesh = NULL;
		NearStudent = NULL;
		LastHitStudent = NULL;
		bWePaused = 0;
		Menu = MENU_None;
		bWardrobeDirty = 1;

		// Hats saved into older .usa files are stale; drop them.
		static FName HatTag( TEXT("HP2RpgHat") );
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* Actor = Engine->GLevel->Actors(i);
			if( Actor && !Actor->bDeleteMe && Actor->Tag==HatTag )
				Engine->GLevel->DestroyActor( Actor );
		}

		const TCHAR* MapName = Engine->GLevel->URL.Map.Len() ? *Engine->GLevel->URL.Map : TEXT("");
		if( bLoadedFromSlot )
			bLoadedFromSlot = 0;
		else if( appStrfind(MapName,TEXT("PrivetDr")) || appStrfind(MapName,TEXT("privetdr")) )
		{
			// A fresh game starts in Privet Drive.
			State.Reset();
			debugf( NAME_Log, TEXT("HP2Rpg: new game, RPG progress reset") );
		}
	}

	void PreSave( UGameEngine* Engine )
	{
		if( !bEnabled )
			return;
		DestroyHat( Engine );
		RestoreHeld();
		APlayerPawn* Harry = GetHarry( Engine );
		if( Harry )
			ApplyQuidditchSkins( Harry, 0 );
	}

	/*-------------------------------------------------------------------------
		Menus.
	-------------------------------------------------------------------------*/

	void OpenMenu( UGameEngine* Engine, INT NewMenu )
	{
		Menu = NewMenu;
		MenuNotice = TEXT("");
		ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
		if( !Info->Pauser.Len() )
		{
			Info->Pauser = TEXT("HP2Rpg");
			bWePaused = 1;
		}
	}

	void CloseMenu( UGameEngine* Engine )
	{
		Menu = MENU_None;
		if( bWePaused && Engine->GLevel )
		{
			ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
			if( Info->Pauser==TEXT("HP2Rpg") )
				Info->Pauser = TEXT("");
		}
		bWePaused = 0;
	}

	void Notice( const FString& Text )
	{
		MenuNotice = Text;
		MenuNoticeTime = 2.5f;
	}

	INT ShopItemsInCategory( INT Category, INT* Indices )
	{
		INT Count = 0;
		for( INT i=0; i<GNumShopItems; i++ )
			if( GShopItems[i].Category==Category )
				Indices[Count++] = i;
		return Count;
	}

	UBOOL GiveRandomCard( UObject* Status, UBOOL bSilver )
	{
		static const INT Bronze[] = { 1,2,4,5,6,7,9,10,12,18,19,21,24,25,26,28,29,31,32,33,37,44,46,47,49,50,51,53,57,58,61,63,64,66,67,75,76,77,78,80,81,83,84,86,88,91,93,94,95,96 };
		static const INT Gold[] = { 11,15,40,41,48,69,72,74,82,100,101 };
		INT Candidates[128];
		INT Num = 0;
		for( INT Id=1; Id<=99; Id++ )
		{
			UBOOL bIsBronze = 0, bIsGold = 0;
			for( INT i=0; i<ARRAY_COUNT(Bronze); i++ ) if( Bronze[i]==Id ) bIsBronze = 1;
			for( INT i=0; i<ARRAY_COUNT(Gold); i++ ) if( Gold[i]==Id ) bIsGold = 1;
			if( bIsGold || (bSilver ? bIsBronze : !bIsBronze) )
				continue;
			FRpgArg Arg = FRpgArg::I( Id );
			FRpgArg Ret;
			if( !CallScript( Status, TEXT("IsOwnedByHarry"), &Arg, 1, &Ret ) )
				return 0;
			if( !Ret.Int )
				Candidates[Num++] = Id;
		}
		if( !Num )
			return 0;
		FRpgArg Arg = FRpgArg::I( Candidates[RpgRand(Num)] );
		return CallScript( Status, TEXT("GiveCardToHarry"), &Arg, 1 );
	}

	void Buy( APlayerPawn* Harry, const FShopItem& Item )
	{
		UObject* Status = GetStatus( Harry );
		if( !Status )
			return;
		const INT Price = ShopItemPrice( Item );
		if( Item.Kind==SHOP_Wardrobe && IsOwned(Item.Slot,Item.Option) )
		{
			State.Equip[Item.Slot] = Item.Option;
			Notice( TEXT("Already yours. Equipped!") );
			return;
		}
		const INT Beans = CallInt( Status, TEXT("GetBeanCount") );
		if( Beans < Price )
		{
			Notice( TEXT("Not enough beans.") );
			return;
		}

		UBOOL bOk = 1;
		FRpgArg Count = FRpgArg::I( Item.Count );
		switch( Item.Kind )
		{
			case SHOP_Potion:     bOk = CallScript( Status, TEXT("AddPotions"), &Count, 1 ); break;
			case SHOP_FMucus:     bOk = CallScript( Status, TEXT("AddFMucus"), &Count, 1 ); break;
			case SHOP_WBark:      bOk = CallScript( Status, TEXT("AddWBark"), &Count, 1 ); break;
			case SHOP_BronzeCard: bOk = GiveRandomCard( Status, 0 ); break;
			case SHOP_SilverCard: bOk = GiveRandomCard( Status, 1 ); break;
			case SHOP_SkillPoint: State.SkillPoints++; break;
			case SHOP_Wardrobe:
				State.Owned |= 1 << GetSlotOptions(Item.Slot)[Item.Option].OwnBit;
				State.Equip[Item.Slot] = Item.Option;
				break;
		}
		if( !bOk )
		{
			Notice( Item.Kind==SHOP_BronzeCard || Item.Kind==SHOP_SilverCard ? TEXT("You already own all of those cards!") : TEXT("The vendor can't sell that right now.") );
			return;
		}
		FRpgArg Pay = FRpgArg::I( -Price );
		CallScript( Status, TEXT("AddBeans"), &Pay, 1 );
		LastBeans = CallInt( Status, TEXT("GetBeanCount") );
		Notice( FString::Printf(TEXT("Bought %s!"), ShopItemName(Item)) );
	}

	UBOOL InputEvent( UGameEngine* Engine, UViewport* Viewport, EInputKey Key, EInputAction Action, FLOAT Delta )
	{
		if( !bInitialized || !bEnabled || !Engine->GLevel || Engine->Replay.Replaying() || Engine->Replay.Recording() )
			return 0;
		APlayerPawn* Harry = GetHarry( Engine );

		if( Menu==MENU_None )
		{
			if( Action!=IST_Press || !Harry )
				return 0;
			if( Key==IK_K && IsFreeGameplay(Engine,Harry) )
			{
				OpenMenu( Engine, MENU_Character );
				return 1;
			}
			if( Key==IK_B && IsFreeGameplay(Engine,Harry) )
			{
				if( NearStudent )
					OpenMenu( Engine, MENU_Shop );
				else
					AddFeed( TEXT("Find a student or the Weasley twins to trade with."), FColor(220,220,220,255) );
				return 1;
			}
			return 0;
		}

		// A menu is open: it owns all input, except releases, so keys held
		// when the menu opened are not left stuck down.
		if( Action!=IST_Press )
			return Action==IST_Release ? 0 : 1;
		if( !Harry )
		{
			CloseMenu( Engine );
			return 1;
		}
		const UBOOL bUp = Key==IK_Up || Key==IK_W;
		const UBOOL bDown = Key==IK_Down || Key==IK_S;
		const UBOOL bLeft = Key==IK_Left || Key==IK_A;
		const UBOOL bRight = Key==IK_Right || Key==IK_D;
		const UBOOL bAccept = Key==IK_Enter || Key==IK_Space;
		const UBOOL bClose = Key==IK_Escape || Key==IK_Backspace || (Menu==MENU_Shop && Key==IK_B) || (Menu==MENU_Character && Key==IK_K);
		if( bClose )
		{
			CloseMenu( Engine );
			return 1;
		}

		if( Menu==MENU_Shop )
		{
			INT Indices[64];
			if( bLeft )  { ShopCategory = (ShopCategory+CAT_MAX-1)%CAT_MAX; ShopRow = 0; }
			if( bRight ) { ShopCategory = (ShopCategory+1)%CAT_MAX; ShopRow = 0; }
			const INT Count = ShopItemsInCategory( ShopCategory, Indices );
			if( Count>0 )
			{
				if( bUp )   ShopRow = (ShopRow+Count-1)%Count;
				if( bDown ) ShopRow = (ShopRow+1)%Count;
				ShopRow = Clamp( ShopRow, 0, Count-1 );
				if( bAccept )
					Buy( Harry, GShopItems[Indices[ShopRow]] );
			}
			return 1;
		}

		// Character menu.
		if( Key==IK_Tab || Key==IK_Q || Key==IK_E )
		{
			CharTab = (CharTab+1)%TAB_MAX;
			return 1;
		}
		if( CharTab==TAB_Skills )
		{
			if( bUp )   SkillRow = (SkillRow+SKILL_MAX-1)%SKILL_MAX;
			if( bDown ) SkillRow = (SkillRow+1)%SKILL_MAX;
			if( bAccept || bRight )
			{
				if( State.SkillPoints<=0 )
					Notice( TEXT("No skill points. Level up or buy an Extra Lesson.") );
				else if( State.Skill[SkillRow]>=SKILL_MAX_PIPS )
					Notice( TEXT("That skill is maxed out.") );
				else
				{
					State.SkillPoints--;
					State.Skill[SkillRow]++;
					ApplySkills( Harry );
					Notice( FString::Printf(TEXT("%s improved!"), GSkillNames[SkillRow]) );
				}
			}
		}
		else
		{
			if( bUp )   WardrobeRow = (WardrobeRow+RSLOT_MAX-1)%RSLOT_MAX;
			if( bDown ) WardrobeRow = (WardrobeRow+1)%RSLOT_MAX;
			if( bLeft || bRight || bAccept )
			{
				// Cycle through owned options only.
				const INT Num = GSlotCounts[WardrobeRow];
				INT Option = State.Equip[WardrobeRow];
				for( INT Step=0; Step<Num; Step++ )
				{
					Option = (Option + (bLeft ? Num-1 : 1)) % Num;
					if( IsOwned(WardrobeRow,Option) )
						break;
				}
				if( Option==State.Equip[WardrobeRow] )
					Notice( TEXT("Buy more items from a student vendor (press B near one).") );
				State.Equip[WardrobeRow] = Option;
			}
		}
		return 1;
	}

	/*-------------------------------------------------------------------------
		Drawing.
	-------------------------------------------------------------------------*/

	void ResolveUi()
	{
		if( bUiResolved )
			return;
		bUiResolved = 1;
		WhiteTex = FindTexture( TEXT("UWindow"), TEXT("WhiteTexture") );
		if( !WhiteTex )
			WhiteTex = FindTexture( NULL, TEXT("DefaultTexture") );
		if( WhiteTex )
			WhiteTex->AddToRoot();
		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			Crests[i] = FindTexture( TEXT("HP2_Menu"), GHouseCrests[i] );
			if( Crests[i] )
				Crests[i]->AddToRoot();
		}
	}

	void Rect( UCanvas* C, FLOAT X, FLOAT Y, FLOAT W, FLOAT H, FColor Color )
	{
		if( !WhiteTex )
			return;
		C->DrawTile( WhiteTex, X, Y, W, H, 0, 0, 1, 1, NULL, 1.f, FPlane(Color.R/255.f,Color.G/255.f,Color.B/255.f,1.f), FPlane(0,0,0,0), PF_NoSmooth );
	}

	void Frame( UCanvas* C, FLOAT X, FLOAT Y, FLOAT W, FLOAT H, FLOAT S, FColor Fill, FColor Border )
	{
		Rect( C, X, Y, W, H, Border );
		Rect( C, X+2*S, Y+2*S, W-4*S, H-4*S, Fill );
	}

	void Icon( UCanvas* C, UTexture* Tex, FLOAT X, FLOAT Y, FLOAT W, FLOAT H, FLOAT Bright=1.f )
	{
		if( !Tex )
			return;
		C->DrawTile( Tex, X, Y, W, H, 0, 0, Tex->USize, Tex->VSize, NULL, 1.f, FPlane(Bright,Bright,Bright,1.f), FPlane(0,0,0,0), PF_Masked );
	}

	FLOAT TextWidth( UCanvas* C, UFont* Font, const TCHAR* Text )
	{
		INT XL=0, YL=0;
		C->CurX = 0.f;
		C->CurY = 0.f;
		C->WrappedStrLenf( Font, XL, YL, TEXT("%s"), Text );
		return XL;
	}

	FLOAT TextHeight( UCanvas* C, UFont* Font )
	{
		INT XL=0, YL=0;
		C->CurX = 0.f;
		C->CurY = 0.f;
		C->WrappedStrLenf( Font, XL, YL, TEXT("Ag") );
		return YL>0 ? YL : 12;
	}

	void Text( UCanvas* C, UFont* Font, FLOAT X, FLOAT Y, FColor Color, const TCHAR* Str, INT Align=0 )
	{
		if( !Font )
			return;
		if( Align==1 )
			X -= TextWidth( C, Font, Str )*0.5f;
		else if( Align==2 )
			X -= TextWidth( C, Font, Str );
		C->CurX = X;
		C->CurY = Y;
		C->Color = Color;
		C->WrappedPrintf( Font, 0, TEXT("%s"), Str );
	}

	void Draw( UGameEngine* Engine, UViewport* Viewport, UCanvas* C )
	{
		if( !bInitialized || !bEnabled || !Engine->GLevel )
			return;
		APlayerPawn* Harry = GetHarry( Engine );
		if( !Harry )
			return;
		ResolveUi();

		const FLOAT SavedOrgX = C->OrgX, SavedOrgY = C->OrgY, SavedClipX = C->ClipX, SavedClipY = C->ClipY;
		const FLOAT SavedCurX = C->CurX, SavedCurY = C->CurY;
		const FColor SavedColor = C->Color;
		const BYTE SavedStyle = C->Style;
		C->OrgX = 0.f;
		C->OrgY = 0.f;
		C->ClipX = C->X;
		C->ClipY = C->Y;
		C->Style = STY_Normal;

		const FLOAT S = Max( 1.f, C->ClipY/480.f );
		UFont* Small = C->SmallFont ? C->SmallFont : C->MedFont;
		UFont* Med = C->MedFont ? C->MedFont : Small;
		UFont* Big = C->BigFont ? C->BigFont : Med;

		const UBOOL bGameplay = IsFreeGameplay( Engine, Harry );
		if( Menu==MENU_None && (bGameplay || HouseHudTimer>0.f) )
			DrawStatusHud( C, Harry, S, Small, bGameplay );
		DrawFeed( C, S, Med );
		if( Menu==MENU_Shop )
			DrawShop( C, Harry, S, Small, Med, Big );
		else if( Menu==MENU_Character )
			DrawCharacter( C, Harry, S, Small, Med, Big );

		C->OrgX = SavedOrgX;
		C->OrgY = SavedOrgY;
		C->ClipX = SavedClipX;
		C->ClipY = SavedClipY;
		C->CurX = SavedCurX;
		C->CurY = SavedCurY;
		C->Color = SavedColor;
		C->Style = SavedStyle;
	}

	void DrawStatusHud( UCanvas* C, APlayerPawn* Harry, FLOAT S, UFont* Small, UBOOL bGameplay )
	{
		// House crests with totals, top centre.
		const FLOAT Crest = 18*S;
		const FLOAT Gap = 50*S;
		const FLOAT X0 = C->ClipX*0.5f - Gap*1.5f;
		const FLOAT Y0 = 6*S;
		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			const FLOAT X = X0 + Gap*i;
			const FLOAT Bright = (PendingHouse[i] || i==HOUSE_Gryffindor) ? 1.f : 0.7f;
			if( Crests[i] )
				Icon( C, Crests[i], X-Crest*0.5f, Y0, Crest, Crest, Bright );
			Text( C, Small, X, Y0+Crest+1*S, GHouseColors[i], *FString::Printf(TEXT("%i"), bBaselineValid ? LastHouse[i] : 0), 1 );
		}

		// Level and XP bar under the crests.
		const FLOAT BarW = 120*S, BarH = 4*S;
		const FLOAT BarX = C->ClipX*0.5f - BarW*0.5f;
		const FLOAT BarY = Y0 + Crest + 14*S;
		Rect( C, BarX, BarY, BarW, BarH, FColor(30,30,40,255) );
		const FLOAT Pct = Clamp( (FLOAT)State.Xp/XpForNextLevel(State.Level), 0.f, 1.f );
		Rect( C, BarX, BarY, BarW*Pct, BarH, FColor(120,190,255,255) );
		Text( C, Small, BarX-4*S, BarY-4*S, FColor(255,230,140,255), *FString::Printf(TEXT("Lv %i"), State.Level), 2 );
		if( State.SkillPoints>0 )
			Text( C, Small, BarX+BarW+4*S, BarY-4*S, FColor(255,230,140,255), *FString::Printf(TEXT("+%i [K]"), State.SkillPoints), 0 );

		if( bGameplay && NearStudent )
			Text( C, Small, C->ClipX*0.5f, C->ClipY*0.78f, FColor(255,240,200,255), TEXT("[B] Trade"), 1 );
	}

	void DrawFeed( UCanvas* C, FLOAT S, UFont* Font )
	{
		const FLOAT LineH = TextHeight( C, Font ) + 2*S;
		FLOAT Y = C->ClipY*0.30f;
		for( INT i=0; i<Feed.Num(); i++ )
		{
			const FRpgFeed& Entry = Feed(i);
			const FLOAT Fade = Entry.Age<3.5f ? 1.f : Max( 0.f, 1.f-(Entry.Age-3.5f) );
			const FLOAT Rise = Entry.Age*6*S;
			FColor Color( (BYTE)(Entry.Color.R*Fade), (BYTE)(Entry.Color.G*Fade), (BYTE)(Entry.Color.B*Fade), 255 );
			Text( C, Font, C->ClipX*0.5f+1*S, Y-Rise+1*S, FColor(0,0,0,(BYTE)(255*Fade)), *Entry.Text, 1 );
			Text( C, Font, C->ClipX*0.5f, Y-Rise, Color, *Entry.Text, 1 );
			Y += LineH;
		}
	}

	void DrawPanel( UCanvas* C, FLOAT S, FLOAT& X, FLOAT& Y, FLOAT& W, FLOAT& H )
	{
		W = Min( C->ClipX-20*S, 560*S );
		H = Min( C->ClipY-20*S, 380*S );
		X = (C->ClipX-W)*0.5f;
		Y = (C->ClipY-H)*0.5f;
		Frame( C, X, Y, W, H, S, FColor(16,20,44,255), FColor(196,160,64,255) );
	}

	void DrawBeans( UCanvas* C, APlayerPawn* Harry, FLOAT X, FLOAT Y, UFont* Font )
	{
		const INT Beans = CallInt( GetStatus(Harry), TEXT("GetBeanCount") );
		Text( C, Font, X, Y, FColor(255,220,120,255), *FString::Printf(TEXT("Your beans: %i"), Beans) );
	}

	void DrawNotice( UCanvas* C, FLOAT X, FLOAT Y, UFont* Font )
	{
		if( MenuNoticeTime>0.f && MenuNotice.Len() )
			Text( C, Font, X, Y, FColor(255,255,255,255), *MenuNotice, 1 );
	}

	void DrawShop( UCanvas* C, APlayerPawn* Harry, FLOAT S, UFont* Small, UFont* Med, UFont* Big )
	{
		FLOAT X, Y, W, H;
		DrawPanel( C, S, X, Y, W, H );
		const FLOAT Pad = 12*S;
		Text( C, Big, X+W*0.5f, Y+Pad, FColor(230,190,90,255), TEXT("Shop"), 1 );

		// Category tabs.
		FLOAT TabY = Y + Pad + TextHeight(C,Big) + 6*S;
		const FLOAT TabW = (W-2*Pad)/CAT_MAX;
		for( INT i=0; i<CAT_MAX; i++ )
		{
			const FLOAT TX = X + Pad + TabW*i;
			if( i==ShopCategory )
				Rect( C, TX+1*S, TabY-2*S, TabW-2*S, TextHeight(C,Small)+4*S, FColor(70,60,30,255) );
			Text( C, Small, TX+TabW*0.5f, TabY, i==ShopCategory ? FColor(255,230,140,255) : FColor(160,160,190,255), GShopCategoryNames[i], 1 );
		}

		// Items.
		const INT Beans = CallInt( GetStatus(Harry), TEXT("GetBeanCount") );
		INT Indices[64];
		const INT Count = ShopItemsInCategory( ShopCategory, Indices );
		FLOAT RowY = TabY + TextHeight(C,Small) + 14*S;
		const FLOAT RowH = TextHeight(C,Med) + 8*S;
		for( INT r=0; r<Count; r++ )
		{
			const FShopItem& Item = GShopItems[Indices[r]];
			const UBOOL bSel = r==ShopRow;
			const UBOOL bOwned = Item.Kind==SHOP_Wardrobe && IsOwned(Item.Slot,Item.Option);
			const INT Price = ShopItemPrice( Item );
			if( bSel )
				Rect( C, X+Pad, RowY-3*S, W-2*Pad, RowH, FColor(44,52,96,255) );
			Text( C, Med, X+Pad+8*S, RowY, bSel ? FColor(255,255,255,255) : FColor(200,200,220,255), ShopItemName(Item) );
			if( bOwned )
				Text( C, Med, X+W-Pad-8*S, RowY, FColor(120,230,120,255), TEXT("Owned"), 2 );
			else
				Text( C, Med, X+W-Pad-8*S, RowY, Price<=Beans ? FColor(255,220,120,255) : FColor(230,70,70,255), *FString::Printf(TEXT("%i beans"), Price), 2 );
			RowY += RowH;
		}
		if( Count>0 )
		{
			const FShopItem& Sel = GShopItems[Indices[Clamp(ShopRow,0,Count-1)]];
			Text( C, Small, X+Pad+8*S, Y+H-Pad-TextHeight(C,Small)*3-10*S, FColor(210,210,230,255), ShopItemDesc(Sel) );
			const UBOOL bOwned = Sel.Kind==SHOP_Wardrobe && IsOwned(Sel.Slot,Sel.Option);
			const TCHAR* Action = bOwned ? TEXT("[Enter] Equip") : (ShopItemPrice(Sel)<=Beans ? TEXT("[Enter] Confirm") : TEXT("Too expensive"));
			Text( C, Med, X+W-Pad-8*S, Y+H-Pad-TextHeight(C,Med)-TextHeight(C,Small)-6*S, FColor(255,230,140,255), Action, 2 );
		}
		DrawBeans( C, Harry, X+Pad+8*S, Y+H-Pad-TextHeight(C,Small)*2-4*S, Med );
		Text( C, Small, X+W*0.5f, Y+H-Pad-TextHeight(C,Small), FColor(150,150,180,255), TEXT("Left/Right: category   Up/Down: choose   Enter: buy   B/Esc: close"), 1 );
		DrawNotice( C, X+W*0.5f, Y+H*0.5f+60*S, Med );
	}

	void DrawCharacter( UCanvas* C, APlayerPawn* Harry, FLOAT S, UFont* Small, UFont* Med, UFont* Big )
	{
		FLOAT X, Y, W, H;
		DrawPanel( C, S, X, Y, W, H );
		const FLOAT Pad = 12*S;

		// Tabs.
		static const TCHAR* Tabs[TAB_MAX] = { TEXT("Skills"), TEXT("Wardrobe") };
		for( INT i=0; i<TAB_MAX; i++ )
		{
			const FLOAT TX = X + W*0.5f + (i==0 ? -70*S : 70*S);
			Text( C, Big, TX, Y+Pad, i==CharTab ? FColor(230,190,90,255) : FColor(120,120,150,255), Tabs[i], 1 );
		}
		FLOAT RowY = Y + Pad + TextHeight(C,Big) + 12*S;

		// Level line.
		Text( C, Med, X+Pad+8*S, RowY, FColor(255,230,140,255),
			*FString::Printf(TEXT("Level %i   XP %i / %i   Skill points: %i"), State.Level, State.Xp, XpForNextLevel(State.Level), State.SkillPoints) );
		RowY += TextHeight(C,Med) + 14*S;

		const FLOAT RowH = TextHeight(C,Med) + TextHeight(C,Small) + 12*S;
		if( CharTab==TAB_Skills )
		{
			for( INT i=0; i<SKILL_MAX; i++ )
			{
				const UBOOL bSel = i==SkillRow;
				if( bSel )
					Rect( C, X+Pad, RowY-3*S, W-2*Pad, RowH-4*S, FColor(44,52,96,255) );
				Text( C, Med, X+Pad+8*S, RowY, bSel ? FColor(255,255,255,255) : FColor(200,200,220,255), GSkillNames[i] );
				Text( C, Small, X+Pad+8*S, RowY+TextHeight(C,Med)+2*S, FColor(160,160,190,255), GSkillDescs[i] );
				const FLOAT Pip = 14*S;
				for( INT p=0; p<SKILL_MAX_PIPS; p++ )
				{
					const FLOAT PX = X+W-Pad-8*S-(SKILL_MAX_PIPS-p)*(Pip+4*S);
					Frame( C, PX, RowY+2*S, Pip, Pip, S*0.5f, p<State.Skill[i] ? FColor(230,190,70,255) : FColor(30,34,60,255), FColor(150,130,70,255) );
				}
				RowY += RowH;
			}
			Text( C, Small, X+W*0.5f, Y+H-Pad-TextHeight(C,Small), FColor(150,150,180,255), TEXT("Up/Down: choose   Enter: spend point   Tab/Q/E: wardrobe   K/Esc: close"), 1 );
		}
		else
		{
			for( INT i=0; i<RSLOT_MAX; i++ )
			{
				const UBOOL bSel = i==WardrobeRow;
				const FWardrobeOption& Opt = GetSlotOptions(i)[State.Equip[i]];
				if( bSel )
					Rect( C, X+Pad, RowY-3*S, W-2*Pad, RowH-4*S, FColor(44,52,96,255) );
				Text( C, Med, X+Pad+8*S, RowY, bSel ? FColor(255,255,255,255) : FColor(200,200,220,255), GSlotNames[i] );
				Text( C, Med, X+W-Pad-8*S, RowY, FColor(255,230,140,255), *FString::Printf(TEXT("< %s >"), Opt.Name), 2 );
				Text( C, Small, X+Pad+8*S, RowY+TextHeight(C,Med)+2*S, FColor(160,160,190,255), Opt.Desc );
				RowY += RowH;
			}
			// Locked items in the selected slot.
			FString Locked;
			const FWardrobeOption* Opts = GetSlotOptions( WardrobeRow );
			for( INT o=0; o<GSlotCounts[WardrobeRow]; o++ )
				if( !IsOwned(WardrobeRow,o) )
					Locked += FString::Printf( TEXT("%s%s (%i)"), Locked.Len() ? TEXT(", ") : TEXT(""), Opts[o].Name, Opts[o].Price );
			if( Locked.Len() )
				Text( C, Small, X+Pad+8*S, RowY+4*S, FColor(200,120,120,255), *(FString(TEXT("Locked, buy from a vendor: ")) + Locked) );
			Text( C, Small, X+W*0.5f, Y+H-Pad-TextHeight(C,Small), FColor(150,150,180,255), TEXT("Up/Down: choose   Left/Right: change   Tab/Q/E: skills   K/Esc: close"), 1 );
		}
		DrawNotice( C, X+W*0.5f, Y+H-Pad-TextHeight(C,Small)*2-10*S, Med );
	}

	/*-------------------------------------------------------------------------
		Console commands.
	-------------------------------------------------------------------------*/

	UBOOL Exec( UGameEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar )
	{
		Init();
		if( !ParseCommand(&Cmd,TEXT("RPG")) )
			return 0;
		APlayerPawn* Harry = GetHarry( Engine );
		if( ParseCommand(&Cmd,TEXT("STATUS")) )
		{
			Ar.Logf( TEXT("HP2Rpg %s: Level %i Xp %i SkillPoints %i Health %i Speed %i Jump %i Luck %i Owned %i"),
				bEnabled ? TEXT("on") : TEXT("off"), State.Level, State.Xp, State.SkillPoints,
				State.Skill[0], State.Skill[1], State.Skill[2], State.Skill[3], State.Owned );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("XP")) )
		{
			if( Harry )
				GiveXp( appAtoi(Cmd), TEXT("console"), Harry );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("BEANS")) )
		{
			FRpgArg Arg = FRpgArg::I( appAtoi(Cmd) );
			if( Harry )
				CallScript( GetStatus(Harry), TEXT("AddBeans"), &Arg, 1 );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("HAT")) )
		{
			// RPG HAT <up> <forward> <side> <scale>: tune hat placement live.
			FLOAT Values[4] = { 0.f, 0.f, 0.f, 1.f };
			INT N = 0;
			FString Token;
			while( N<4 && ParseToken(Cmd,Token,0) )
				Values[N++] = appAtof( *Token );
			HatTweak = FVector( Values[0], Values[1], Values[2] );
			HatScaleTweak = Values[3]>0.f ? Values[3] : 1.f;
			Ar.Logf( TEXT("HP2Rpg hat tweak up=%.1f fwd=%.1f side=%.1f scale=%.2f"), HatTweak.X, HatTweak.Y, HatTweak.Z, HatScaleTweak );
			return 1;
		}
		Ar.Logf( TEXT("Usage: RPG STATUS | RPG XP <n> | RPG BEANS <n> | RPG HAT <up> <forward> <side> <scale>") );
		return 1;
	}
};

static FHP2Rpg GRpg;

void FHP2Rpg::StaticOnScriptCall( UObject* Self, UFunction* Function, BYTE* Locals )
{
	GRpg.OnScriptCall( Self, Function, Locals );
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

void HP2RpgOnLevelLoaded( UGameEngine* Engine )
{
	guard(HP2RpgOnLevelLoaded);
	// The old level and its actors are gone; forget every pointer into it
	// even if the new ULevel happens to reuse the old address.
	GRpg.LastLevel = NULL;
	GRpg.LastHarry = NULL;
	GRpg.HatActor = NULL;
	GRpg.HatActorKind = HAT_None;
	GRpg.HeldWeapon = NULL;
	GRpg.HeldOriginalMesh = NULL;
	GRpg.NearStudent = NULL;
	GRpg.LastHitStudent = NULL;
	GRpg.RewardedCreatures.Empty();
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
		// LoadMap already replaced GLevel before we got here; treat the
		// next level-change check as coming from this load.
		GRpg.LastLevel = NULL;
	}
	unguard;
}
