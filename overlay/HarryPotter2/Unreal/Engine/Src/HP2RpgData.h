/*=============================================================================
	HP2RpgData.h: Static content tables for the RPG layer: cosmetics, shop
	items, vendors, spell perks, creatures, card collections and trophies.
	Included only by HP2Rpg.cpp.
=============================================================================*/

#ifndef HP2RPGDATA_H
#define HP2RPGDATA_H

/*-----------------------------------------------------------------------------
	Stats granted by gear, cards and bestiary tiers.
-----------------------------------------------------------------------------*/

enum ERpgStat
{
	STAT_None,
	STAT_BeanFind,      // % chance per bean of a bonus bean
	STAT_XpGain,        // % more XP
	STAT_DamageReduce,  // % less damage taken
	STAT_SpellPower,    // % stronger perk effects (pushes, stuns)
	STAT_Speed,         // % run speed
	STAT_Jump,          // % jump height
	STAT_Discount,      // % off shop prices
	STAT_MAX
};

static const TCHAR* GStatFormat[STAT_MAX] =
{
	TEXT(""),
	TEXT("+%i%% bean find"),
	TEXT("+%i%% XP"),
	TEXT("-%i%% damage taken"),
	TEXT("+%i%% spell power"),
	TEXT("+%i%% speed"),
	TEXT("+%i%% jump"),
	TEXT("-%i%% shop prices"),
};

enum ERpgRarity { RAR_Common, RAR_Rare, RAR_Epic, RAR_Legendary, RAR_MAX };
static const TCHAR* GRarityNames[RAR_MAX] = { TEXT("Common"), TEXT("Rare"), TEXT("Epic"), TEXT("Legendary") };
static const FColor GRarityColors[RAR_MAX] =
{
	FColor(120,126,160,255), FColor(80,150,255,255), FColor(190,110,255,255), FColor(255,190,60,255)
};

/*-----------------------------------------------------------------------------
	Cosmetics.
-----------------------------------------------------------------------------*/

enum ERpgSlot { RSLOT_Outfit, RSLOT_Hat, RSLOT_Glasses, RSLOT_Held, RSLOT_Pet, RSLOT_Aura, RSLOT_MAX };
static const TCHAR* GSlotNames[RSLOT_MAX] = { TEXT("Outfit"), TEXT("Hat"), TEXT("Glasses"), TEXT("Held"), TEXT("Pet"), TEXT("Aura") };

enum EOutfitKind { OK_Default, OK_Palette, OK_Mesh };

// Palette variants applied to Harry's own textures.
enum EPaletteVariant { PV_None, PV_Slytherin, PV_Ravenclaw, PV_Hufflepuff, PV_Memory };

struct FCosmetic
{
	INT Slot;
	const TCHAR* Name;
	const TCHAR* Desc;
	INT Price;            // 0 = owned from the start
	INT Rarity;
	INT Stat;
	INT StatValue;
	// Visuals.
	INT OutfitKind;       // outfits only
	INT Palette;          // outfits (OK_Palette) and glasses (frame colour index)
	const TCHAR* Mesh;    // costume, hat, held, pet mesh, or aura particle class
	FLOAT Scale;          // DrawScale (relative to Harry for costumes, ThirdPersonScale factor for held)
	FLOAT Up;             // attachment offsets in bone space
	FLOAT Forward;
	FLOAT Side;
	const TCHAR* Skin0;   // optional MultiSkins for costumes
	const TCHAR* Skin1;
};

// Cosmetic ids are their index in this table; ownership is a bit per id.
static const FCosmetic GCosmetics[] =
{
	// --- Outfits ------------------------------------------------------------
	{ RSLOT_Outfit, TEXT("Gryffindor Uniform"), TEXT("Your own school robes."), 0, RAR_Common, STAT_None, 0, OK_Default, PV_None, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Quidditch Robes"), TEXT("Gryffindor's scarlet and gold team robes."), 250, RAR_Rare, STAT_Speed, 6, OK_Mesh, PV_None, TEXT("HPModels.skharryquidMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Slytherin Robes"), TEXT("Green and silver. Draco would approve."), 200, RAR_Common, STAT_BeanFind, 5, OK_Palette, PV_Slytherin, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Ravenclaw Robes"), TEXT("Blue and bronze, for the wise."), 200, RAR_Common, STAT_XpGain, 5, OK_Palette, PV_Ravenclaw, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Hufflepuff Robes"), TEXT("Yellow and black, loyal to the end."), 200, RAR_Common, STAT_DamageReduce, 5, OK_Palette, PV_Hufflepuff, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Memory Robes"), TEXT("Look like a page from Tom Riddle's diary."), 300, RAR_Rare, STAT_SpellPower, 8, OK_Palette, PV_Memory, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Ron's Look"), TEXT("Polyjuice-free disguise as your best mate."), 350, RAR_Rare, STAT_Jump, 6, OK_Mesh, PV_None, TEXT("HPModels.skronMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Hermione's Look"), TEXT("Brightest witch of her age, apparently."), 350, RAR_Rare, STAT_XpGain, 8, OK_Mesh, PV_None, TEXT("HPModels.skhermioneMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Ginny's Look"), TEXT("Youngest Weasley, fiercest bat-bogey hex."), 350, RAR_Rare, STAT_SpellPower, 6, OK_Mesh, PV_None, TEXT("HPModels.skGinnyMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Draco's Look"), TEXT("Sneer included."), 400, RAR_Epic, STAT_Discount, 10, OK_Mesh, PV_None, TEXT("HPModels.skDracoMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Fred's Look"), TEXT("Or is it George?"), 450, RAR_Epic, STAT_BeanFind, 12, OK_Mesh, PV_None, TEXT("HPModels.skFredWeasleyMesh"), 0.95f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("George's Look"), TEXT("Or is it Fred?"), 450, RAR_Epic, STAT_BeanFind, 12, OK_Mesh, PV_None, TEXT("HPModels.skGeorgeWeasleyMesh"), 0.95f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Oliver Wood's Look"), TEXT("Captain. Keeper. Obsessed."), 500, RAR_Epic, STAT_Speed, 10, OK_Mesh, PV_None, TEXT("HPModels.skOliverWoodMesh"), 0.95f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Polyjuice: Goyle"), TEXT("Smells faintly of Goyle."), 600, RAR_Epic, STAT_DamageReduce, 12, OK_Mesh, PV_None, TEXT("HPModels.skGoyleMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Tom Riddle's Look"), TEXT("A memory preserved for fifty years."), 900, RAR_Legendary, STAT_SpellPower, 15, OK_Mesh, PV_None, TEXT("HPModels.skTomRiddleMesh"), 0.92f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Slytherin Student"), TEXT("Blend in at the dungeon entrance."), 300, RAR_Rare, STAT_Discount, 6, OK_Mesh, PV_None, TEXT("HPModels.skhp2_genmale1Mesh"), 1.f, 0,0,0, TEXT("HPModels.Skins.skhp2_genmale1_2Tex0"), TEXT("HPModels.Skins.skhp2_genmale1_2Tex1") },
	{ RSLOT_Outfit, TEXT("Ravenclaw Student"), TEXT("Wit beyond measure is man's greatest treasure."), 300, RAR_Rare, STAT_XpGain, 6, OK_Mesh, PV_None, TEXT("HPModels.skhp2_genmale1Mesh"), 1.f, 0,0,0, TEXT("HPModels.Skins.skhp2_genmale1_6Tex0"), TEXT("HPModels.Skins.skhp2_genmale1_6Tex1") },
	{ RSLOT_Outfit, TEXT("Hufflepuff Student"), TEXT("Just and loyal."), 300, RAR_Rare, STAT_DamageReduce, 6, OK_Mesh, PV_None, TEXT("HPModels.skhp2_genmale1Mesh"), 1.f, 0,0,0, TEXT("HPModels.Skins.skhp2_genmale1_4Tex0"), TEXT("HPModels.Skins.skhp2_genmale1_4Tex1") },
	{ RSLOT_Outfit, TEXT("Moaning Myrtle"), TEXT("Haunt the girls' bathroom in style."), 700, RAR_Legendary, STAT_Jump, 12, OK_Mesh, PV_None, TEXT("HPModels.skmoaningmyrtleMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Outfit, TEXT("Dobby"), TEXT("Dobby is a free elf! Well, almost."), 800, RAR_Legendary, STAT_Speed, 12, OK_Mesh, PV_None, TEXT("HPModels.skdobbyMesh"), 0.85f, 0,0,0, NULL, NULL },

	// --- Hats ----------------------------------------------------------------
	{ RSLOT_Hat, TEXT("No Hat"), TEXT("Nothing on your head."), 0, RAR_Common, STAT_None, 0, 0, 0, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Hat, TEXT("Sorting Hat"), TEXT("It still has opinions about you."), 300, RAR_Epic, STAT_XpGain, 10, 0, 0, TEXT("HPModels.skSortingHatMesh"), 0.45f, 3.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Knight's Helmet"), TEXT("Borrowed from a suit of armour."), 350, RAR_Epic, STAT_DamageReduce, 10, 0, 0, TEXT("HProps.skArmorHelmetMesh"), 0.78f, -6.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Cauldron"), TEXT("Pewter, standard size 2."), 120, RAR_Common, STAT_DamageReduce, 3, 0, 0, TEXT("HProps.skCauldronSmallMesh"), 0.6f, 2.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Big Cauldron"), TEXT("For serious brewers only."), 180, RAR_Rare, STAT_SpellPower, 5, 0, 0, TEXT("HProps.skCauldronStudentMesh"), 0.45f, 2.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Bucket"), TEXT("Filch will want it back."), 60, RAR_Common, STAT_DamageReduce, 2, 0, 0, TEXT("HPModels.skbucketMesh"), 0.4f, 2.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Horklump Cap"), TEXT("A mushroom that grew on you."), 150, RAR_Rare, STAT_BeanFind, 6, 0, 0, TEXT("HPModels.skhorklumpsHeadMesh"), 0.75f, 2.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Dragon Head"), TEXT("Charlie Weasley would be proud."), 650, RAR_Legendary, STAT_SpellPower, 12, 0, 0, TEXT("HProps.skDragonHeadMesh"), 0.8f, 0.f, 2.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Hagrid's Teapot"), TEXT("Nobody will dare ask."), 80, RAR_Common, STAT_XpGain, 2, 0, 0, TEXT("HProps.skDishesHagridTeaPotMesh"), 0.55f, 2.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Flower Pot"), TEXT("Professor Sprout's spare."), 70, RAR_Common, STAT_Jump, 3, 0, 0, TEXT("HPModels.skemptyplantpotMesh"), 1.3f, 1.f, 0.f, 0.f, NULL, NULL },
	{ RSLOT_Hat, TEXT("Challenge Star"), TEXT("A golden star floats above your head."), 500, RAR_Legendary, STAT_XpGain, 12, 0, 0, TEXT("HProps.skChallengeStarMesh"), 0.35f, 16.f, 0.f, 0.f, NULL, NULL },

	// --- Glasses (Palette = frame colour index) ------------------------------
	{ RSLOT_Glasses, TEXT("Harry's Glasses"), TEXT("Round and famous."), 0, RAR_Common, STAT_None, 0, 0, 0, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Glasses, TEXT("No Glasses"), TEXT("You squint a bit."), 50, RAR_Common, STAT_None, 0, 0, 1, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Glasses, TEXT("Gold Frames"), TEXT("Very Lockhart."), 150, RAR_Rare, STAT_Discount, 5, 0, 2, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Glasses, TEXT("Gryffindor Red Frames"), TEXT("House pride on your nose."), 100, RAR_Common, STAT_SpellPower, 4, 0, 3, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Glasses, TEXT("Slytherin Green Frames"), TEXT("Sneaky."), 100, RAR_Common, STAT_BeanFind, 4, 0, 4, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Glasses, TEXT("Ravenclaw Blue Frames"), TEXT("Read faster."), 100, RAR_Common, STAT_XpGain, 4, 0, 5, NULL, 1.f, 0,0,0, NULL, NULL },

	// --- Held items (cosmetic; spells still come from your wand) -------------
	{ RSLOT_Held, TEXT("Wand"), TEXT("Holly and phoenix feather."), 0, RAR_Common, STAT_None, 0, 0, 0, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Sword of Gryffindor"), TEXT("Presented only to a true Gryffindor."), 800, RAR_Legendary, STAT_SpellPower, 15, 0, 0, TEXT("HPModels.skGryf_SwordMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Quidditch Broom"), TEXT("Carry it around like a Seeker."), 300, RAR_Rare, STAT_Speed, 6, 0, 0, TEXT("HProps.skBroomQudditchMesh"), 0.7f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Riddle's Diary"), TEXT("It writes back."), 500, RAR_Epic, STAT_XpGain, 10, 0, 0, TEXT("HProps.skRiddlesDiaryMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Filch's Lantern"), TEXT("Lights the way after curfew."), 220, RAR_Rare, STAT_BeanFind, 6, 0, 0, TEXT("HProps.skNorrisTorchMesh"), 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Golden Goblet"), TEXT("Pumpkin juice not included."), 160, RAR_Common, STAT_Discount, 4, 0, 0, TEXT("HProps.skDishesGobletMesh"), 0.6f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Quaffle"), TEXT("Ten points for Gryffindor!"), 200, RAR_Rare, STAT_Jump, 6, 0, 0, TEXT("HProps.skQuidditchQuaffleMesh"), 0.5f, 0,0,0, NULL, NULL },
	{ RSLOT_Held, TEXT("Chicken Leg"), TEXT("A snack from the Great Hall feast."), 40, RAR_Common, STAT_DamageReduce, 2, 0, 0, TEXT("HPModels.skChickenLegMesh"), 0.6f, 0,0,0, NULL, NULL },

	// --- Pets (sit on the left shoulder) --------------------------------------
	{ RSLOT_Pet, TEXT("No Pet"), TEXT("Just you."), 0, RAR_Common, STAT_None, 0, 0, 0, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Pet, TEXT("Hedwig"), TEXT("Your snowy owl, fresh from the Owlery."), 600, RAR_Epic, STAT_BeanFind, 10, 0, 0, TEXT("HPModels.skowlbarnMesh"), 0.3f, 6.f, 0.f, -4.f, NULL, NULL },
	{ RSLOT_Pet, TEXT("Fawkes"), TEXT("Dumbledore's phoenix. Help will always be given."), 1500, RAR_Legendary, STAT_DamageReduce, 15, 0, 0, TEXT("HPModels.skFawkesMesh"), 0.22f, 8.f, 0.f, -4.f, NULL, NULL },
	{ RSLOT_Pet, TEXT("Cornish Pixie"), TEXT("Mostly well behaved. Mostly."), 250, RAR_Rare, STAT_Speed, 5, 0, 0, TEXT("HPModels.skcornishpixieMesh"), 0.55f, 6.f, 0.f, -4.f, NULL, NULL },
	{ RSLOT_Pet, TEXT("Garden Gnome"), TEXT("Rescued from a de-gnoming."), 200, RAR_Rare, STAT_BeanFind, 6, 0, 0, TEXT("HPModels.skgnomeMesh"), 0.35f, 4.f, 0.f, -4.f, NULL, NULL },
	{ RSLOT_Pet, TEXT("Mrs Norris"), TEXT("Filch will be looking for her."), 450, RAR_Epic, STAT_Discount, 8, 0, 0, TEXT("HPModels.skMrsNorrisMesh"), 0.45f, 4.f, 0.f, -4.f, NULL, NULL },
	{ RSLOT_Pet, TEXT("Pet Spider"), TEXT("Ron will not be visiting."), 120, RAR_Common, STAT_Jump, 4, 0, 0, TEXT("HPModels.skSpiderSmallMesh"), 0.9f, 3.f, 0.f, -4.f, NULL, NULL },
	{ RSLOT_Pet, TEXT("Golden Snitch"), TEXT("Caught it. Kept it."), 900, RAR_Legendary, STAT_Speed, 12, 0, 0, TEXT("HPModels.skSnitchMesh"), 0.35f, 10.f, 0.f, -8.f, NULL, NULL },

	// --- Auras (particle effects following Harry) ------------------------------
	{ RSLOT_Aura, TEXT("No Aura"), TEXT("No sparkle."), 0, RAR_Common, STAT_None, 0, 0, 0, NULL, 1.f, 0,0,0, NULL, NULL },
	{ RSLOT_Aura, TEXT("Fireflies"), TEXT("A soft cloud of fireflies."), 300, RAR_Rare, STAT_XpGain, 5, 0, 0, TEXT("FireFlies"), 1.f, 20.f, 0,0, NULL, NULL },
	{ RSLOT_Aura, TEXT("Golden Sparkle"), TEXT("Shine like a gold Wizard Card."), 500, RAR_Epic, STAT_BeanFind, 8, 0, 0, TEXT("GoldSparkle01"), 1.f, 10.f, 0,0, NULL, NULL },
	{ RSLOT_Aura, TEXT("Silver Sparkle"), TEXT("Subtle, but magical."), 350, RAR_Rare, STAT_SpellPower, 5, 0, 0, TEXT("SilverSparkle01"), 1.f, 10.f, 0,0, NULL, NULL },
	{ RSLOT_Aura, TEXT("Personal Snowfall"), TEXT("It's always Christmas somewhere."), 400, RAR_Epic, STAT_DamageReduce, 6, 0, 0, TEXT("Snow"), 1.f, 70.f, 0,0, NULL, NULL },
	{ RSLOT_Aura, TEXT("Dizzy Stars"), TEXT("Like after a Bludger to the head."), 250, RAR_Rare, STAT_Jump, 5, 0, 0, TEXT("DizzyStars"), 1.f, 48.f, 0,0, NULL, NULL },
	{ RSLOT_Aura, TEXT("Ghost Trail"), TEXT("Nearly Headless Nick's favourite."), 700, RAR_Legendary, STAT_Speed, 10, 0, 0, TEXT("GhostTrail"), 1.f, 20.f, 0,0, NULL, NULL },
};
static const INT GNumCosmetics = (INT)ARRAY_COUNT(GCosmetics);
enum { MAX_COSMETICS = 128 };

/*-----------------------------------------------------------------------------
	Consumables.
-----------------------------------------------------------------------------*/

enum EConsumable
{
	CON_Potion, CON_FMucus, CON_WBark, CON_Bicorn, CON_Boomslang,
	CON_BronzeCard, CON_SilverCard, CON_Lesson, CON_Respec, CON_ChocFrog,
	CON_Felix, CON_Pardon, CON_MAX
};

struct FConsumable
{
	const TCHAR* Name;
	const TCHAR* Desc;
	INT Price;
	INT Count;        // amount granted
	INT DailyStock;   // per vendor per day
	const TCHAR* Icon;
	const TCHAR* Mesh;
	FLOAT MeshScale;
};

static const FConsumable GConsumables[CON_MAX] =
{
	{ TEXT("Wiggenweld Potion"), TEXT("Restores your health when it runs out."), 40, 1, 5, TEXT("HP2_Menu.Icons.HP2WiggenwellPotion"), TEXT("HProps.skBottlePotionGreen1Mesh"), 1.f },
	{ TEXT("Flobberworm Mucus"), TEXT("Ingredient for the Wiggenweld Potion."), 15, 1, 8, TEXT("HP2_Menu.Icons.HP2FlobberwormMucus"), TEXT("HProps.skJarFlobberwormMucusMesh"), 1.f },
	{ TEXT("Wiggentree Bark"), TEXT("Ingredient for the Wiggenweld Potion."), 15, 1, 8, TEXT("HP2_Menu.Icons.HP2WiggentreeBark"), TEXT("HProps.skWiggentreeBarkMesh"), 1.f },
	{ TEXT("Bicorn Horn"), TEXT("Polyjuice ingredient. Rare and smelly."), 60, 1, 2, TEXT("HP2_Menu.Icons.HP2Bicorn"), TEXT("HProps.skBicornMesh"), 1.f },
	{ TEXT("Boomslang Skin"), TEXT("Polyjuice ingredient. Shed, not stolen."), 60, 1, 2, TEXT("HP2_Menu.Icons.HP2Boomslang"), TEXT("HProps.skBoomSlangMesh"), 1.f },
	{ TEXT("Bronze Wizard Card"), TEXT("A random bronze card you don't own yet."), 60, 1, 3, TEXT("HP2_Menu.Icons.HP2VendorBronzeCard"), TEXT("HProps.skWizardCardIconMesh"), 1.f },
	{ TEXT("Silver Wizard Card"), TEXT("A random silver card you don't own yet."), 160, 1, 2, TEXT("HP2_Menu.Icons.HP2VendorSilverCard"), TEXT("HProps.skWizardCardIconMesh"), 1.f },
	{ TEXT("Extra Lesson"), TEXT("A private lesson. Gives 1 skill point."), 450, 1, 1, TEXT("HP2_Menu.Icons.HP2MenuChallenges"), TEXT("HProps.skBooksSmallBookMesh"), 1.f },
	{ TEXT("Forgetfulness Potion"), TEXT("Forget your skills and perks and get every point back to spend again."), 250, 1, 1, TEXT("HP2_Menu.Icons.HP2MenuWPotion"), TEXT("HProps.skBottlePotionPurpleMesh"), 1.f },
	{ TEXT("Chocolate Frog"), TEXT("Restores all your health right now."), 30, 1, 4, TEXT("HP2_Menu.Icons.HP2MenuJellybeans"), TEXT("HProps.skChocolateFrogMesh"), 0.25f },
	{ TEXT("Felix Felicis"), TEXT("Liquid luck: double bean find and XP for 5 minutes."), 300, 1, 1, TEXT("HP2_Menu.Icons.HP2WiggenwellPotion"), TEXT("HProps.skBottlePotionOrangeMesh"), 1.f },
	{ TEXT("Detention Pardon"), TEXT("A word from a prefect. Clears 60 trouble."), 120, 1, 1, TEXT("HP2_Menu.Icons.HP2SilverCardKey"), TEXT("HProps.skBooksSmallBookMesh"), 1.f },
};

/*-----------------------------------------------------------------------------
	Vendors.
-----------------------------------------------------------------------------*/

enum EShopTab { TAB_Potions, TAB_Cards, TAB_Ingredients, TAB_Clothes, TAB_Hats, TAB_Glasses, TAB_Gear, TAB_Pets, TAB_Auras, TAB_Magic, TAB_Sell, TAB_MAX };
static const TCHAR* GShopTabNames[TAB_MAX] =
{
	TEXT("Potions"), TEXT("Cards"), TEXT("Ingredients"), TEXT("Clothes"), TEXT("Hats"), TEXT("Glasses"),
	TEXT("Gear"), TEXT("Pets"), TEXT("Auras"), TEXT("Magic"), TEXT("Sell")
};

enum EVendor
{
	VEN_Weasleys, VEN_Pomfrey, VEN_Hagrid, VEN_Percy, VEN_SortingHat, VEN_Myrtle, VEN_Friar,
	VEN_StudentGryffindor, VEN_StudentSlytherin, VEN_StudentRavenclaw, VEN_StudentHufflepuff, VEN_MAX
};

struct FVendorDef
{
	const TCHAR* Name;
	const TCHAR* Greeting;
	const TCHAR* ClassName;     // matched along the class chain; NULL for students
	DWORD Tabs;                 // bit per EShopTab
	DWORD Consumables;          // bit per EConsumable
	const TCHAR* CosmeticFilter;// substring list of cosmetic names this vendor may stock; NULL = slot rules
	INT DailyCosmetics;         // how many cosmetics are on offer each day
	INT PriceMarkup;            // % on top of base price
};

#define TABBIT(t) (1u<<(t))
#define CONBIT(c) (1u<<(c))

static const FVendorDef GVendors[VEN_MAX] =
{
	{ TEXT("Fred & George"), TEXT("Psst! Best prices in Hogwarts. Probably."), TEXT("FredWeasley"),
		TABBIT(TAB_Cards)|TABBIT(TAB_Clothes)|TABBIT(TAB_Hats)|TABBIT(TAB_Gear)|TABBIT(TAB_Pets)|TABBIT(TAB_Magic)|TABBIT(TAB_Sell),
		CONBIT(CON_BronzeCard)|CONBIT(CON_SilverCard)|CONBIT(CON_ChocFrog)|CONBIT(CON_Felix), NULL, 6, 0 },
	{ TEXT("Madam Pomfrey"), TEXT("Sit down, Potter. What hurts this time?"), TEXT("MadamePomfrey"),
		TABBIT(TAB_Potions)|TABBIT(TAB_Ingredients)|TABBIT(TAB_Magic)|TABBIT(TAB_Sell),
		CONBIT(CON_Potion)|CONBIT(CON_FMucus)|CONBIT(CON_WBark)|CONBIT(CON_ChocFrog)|CONBIT(CON_Respec), NULL, 0, 0 },
	{ TEXT("Hagrid"), TEXT("Yer a wizard, Harry. An' yeh need a pet."), TEXT("Hagrid"),
		TABBIT(TAB_Pets)|TABBIT(TAB_Ingredients)|TABBIT(TAB_Hats)|TABBIT(TAB_Sell),
		CONBIT(CON_WBark)|CONBIT(CON_Bicorn)|CONBIT(CON_Boomslang), TEXT("Pet|Hedwig|Fawkes|Pixie|Gnome|Norris|Spider|Teapot|Horklump|Dragon"), 5, 0 },
	{ TEXT("Percy Weasley"), TEXT("Prefect business. Make it quick."), TEXT("Percy"),
		TABBIT(TAB_Clothes)|TABBIT(TAB_Glasses)|TABBIT(TAB_Magic),
		CONBIT(CON_Lesson)|CONBIT(CON_Pardon), TEXT("Robes|Uniform|Student|Frames|Glasses"), 6, 10 },
	{ TEXT("The Sorting Hat"), TEXT("Hmm, difficult. Very difficult. Which hat for you?"), TEXT("SortingHat"),
		TABBIT(TAB_Hats)|TABBIT(TAB_Auras),
		0, NULL, 6, 0 },
	{ TEXT("Moaning Myrtle"), TEXT("Nobody ever buys anything from me..."), TEXT("MoaningMyrtle"),
		TABBIT(TAB_Clothes)|TABBIT(TAB_Auras)|TABBIT(TAB_Glasses),
		0, TEXT("Look|Myrtle|Dobby|Ghost|Snow|Dizzy|Fireflies|Frames"), 5, 0 },
	{ TEXT("The Fat Friar"), TEXT("Hungry, young Potter? Have a snack!"), TEXT("fatfriar"),
		TABBIT(TAB_Potions)|TABBIT(TAB_Gear)|TABBIT(TAB_Magic),
		CONBIT(CON_ChocFrog)|CONBIT(CON_Potion)|CONBIT(CON_Felix), TEXT("Chicken|Goblet|Lantern"), 3, 0 },
	{ TEXT("Gryffindor Student"), TEXT("Hey Harry! Want to trade?"), NULL,
		TABBIT(TAB_Cards)|TABBIT(TAB_Ingredients)|TABBIT(TAB_Gear)|TABBIT(TAB_Sell),
		CONBIT(CON_BronzeCard)|CONBIT(CON_FMucus)|CONBIT(CON_WBark), TEXT("Quidditch|Quaffle|Broom|Red"), 3, 0 },
	{ TEXT("Slytherin Student"), TEXT("Keep your voice down, Potter. Black market rules."), NULL,
		TABBIT(TAB_Cards)|TABBIT(TAB_Clothes)|TABBIT(TAB_Ingredients)|TABBIT(TAB_Magic)|TABBIT(TAB_Sell),
		CONBIT(CON_SilverCard)|CONBIT(CON_Bicorn)|CONBIT(CON_Boomslang)|CONBIT(CON_Felix), TEXT("Slytherin|Draco|Green|Goyle|Riddle"), 4, 25 },
	{ TEXT("Ravenclaw Student"), TEXT("I've got something clever for you."), NULL,
		TABBIT(TAB_Cards)|TABBIT(TAB_Clothes)|TABBIT(TAB_Glasses)|TABBIT(TAB_Magic),
		CONBIT(CON_BronzeCard)|CONBIT(CON_SilverCard)|CONBIT(CON_Lesson), TEXT("Ravenclaw|Blue|Diary|Hermione"), 4, 0 },
	{ TEXT("Hufflepuff Student"), TEXT("Fancy a swap? I've got snacks!"), NULL,
		TABBIT(TAB_Cards)|TABBIT(TAB_Clothes)|TABBIT(TAB_Potions)|TABBIT(TAB_Ingredients),
		CONBIT(CON_BronzeCard)|CONBIT(CON_ChocFrog)|CONBIT(CON_FMucus)|CONBIT(CON_WBark)|CONBIT(CON_Potion), TEXT("Hufflepuff|Pot|Bucket|Chicken"), 4, 0 },
};

static const INT GCosmeticTab[RSLOT_MAX] = { TAB_Clothes, TAB_Hats, TAB_Glasses, TAB_Gear, TAB_Pets, TAB_Auras };

// Sell prices (beans per item) for the Sell tab.
static const INT GSellPotion = 15;
static const INT GSellIngredient = 6;

/*-----------------------------------------------------------------------------
	Spell mastery perks.
-----------------------------------------------------------------------------*/

enum ERpgSpell { SP_Flipendo, SP_Rictusempra, SP_Diffindo, SP_Skurge, SP_Wand, SP_MAX };
static const TCHAR* GSpellNames[SP_MAX] = { TEXT("Flipendo"), TEXT("Rictusempra"), TEXT("Diffindo"), TEXT("Skurge"), TEXT("Wandwork") };
static const TCHAR* GSpellIcons[SP_MAX] =
{
	TEXT("SpellShapes.Shapes.SPSflipendo"), TEXT("SpellShapes.Shapes.SPSrictusempra"), TEXT("SpellShapes.Shapes.SPSdiffindo"),
	TEXT("SpellShapes.Shapes.SPSskurge"), TEXT("SpellShapes.Shapes.SPSlumos")
};

enum ERpgPerk
{
	PERK_PowerPush, PERK_DoubleImpact, PERK_BeanShaker,
	PERK_StunningSmile, PERK_DoubleTickle, PERK_Splash,
	PERK_SharpCut, PERK_Harvest,
	PERK_DeepClean, PERK_Ghostbuster,
	PERK_QuickWand, PERK_FarSight, PERK_Duelist,
	PERK_MAX
};

struct FPerkDef
{
	INT Spell;
	const TCHAR* Name;
	const TCHAR* Desc;
	INT Cost;
	INT Requires;   // perk that must be learned first, or -1
};

static const FPerkDef GPerks[PERK_MAX] =
{
	{ SP_Flipendo, TEXT("Power Push"), TEXT("Flipendo knocks creatures and students much further."), 1, -1 },
	{ SP_Flipendo, TEXT("Double Impact"), TEXT("Gnomes, imps and doxies take an extra hit."), 2, PERK_PowerPush },
	{ SP_Flipendo, TEXT("Bean Shaker"), TEXT("Creatures hit by Flipendo may drop beans."), 2, PERK_DoubleImpact },
	{ SP_Rictusempra, TEXT("Stunning Smile"), TEXT("Pixies, imps and spiders stay stunned longer."), 1, -1 },
	{ SP_Rictusempra, TEXT("Double Tickle"), TEXT("Spiders and fire crabs take an extra hit."), 2, PERK_StunningSmile },
	{ SP_Rictusempra, TEXT("Giggle Splash"), TEXT("Hits also tickle nearby creatures of the same kind."), 3, PERK_DoubleTickle },
	{ SP_Diffindo, TEXT("Sharp Cut"), TEXT("Plants stay cut back much longer."), 1, -1 },
	{ SP_Diffindo, TEXT("Harvest"), TEXT("Cutting plants and bowtruckles drops beans."), 2, PERK_SharpCut },
	{ SP_Skurge, TEXT("Deep Clean"), TEXT("Ectoplasm stays shrunk twice as long."), 1, -1 },
	{ SP_Skurge, TEXT("Ghostbuster"), TEXT("Skurge hurts Peeves twice as much."), 2, PERK_DeepClean },
	{ SP_Wand, TEXT("Quick Wand"), TEXT("Cast spells faster."), 2, PERK_FarSight },
	{ SP_Wand, TEXT("Far Sight"), TEXT("Your spell cursor locks on from further away."), 1, -1 },
	{ SP_Wand, TEXT("Duellist"), TEXT("Your duel spells hit harder, and opponents' hit softer."), 2, PERK_QuickWand },
};

/*-----------------------------------------------------------------------------
	Bestiary.
-----------------------------------------------------------------------------*/

enum ECreature
{
	CR_Gnome, CR_Imp, CR_Pixie, CR_Doxy, CR_SpiderSmall, CR_SpiderLarge, CR_FireCrab,
	CR_Bowtruckle, CR_Ectoplasm, CR_Horklump, CR_SpikyPlant, CR_Tentacula, CR_Boss, CR_MAX
};

struct FCreatureDef
{
	const TCHAR* Name;
	const TCHAR* Classes;   // '|' separated class names (class chain match)
	const TCHAR* Lore;
	INT Xp;
	const TCHAR* Mesh;
};

static const FCreatureDef GCreatures[CR_MAX] =
{
	{ TEXT("Garden Gnome"), TEXT("GNOME"), TEXT("Pests of every wizarding garden. Swing them round and throw them over the hedge."), 15, TEXT("HPModels.skgnomeMesh") },
	{ TEXT("Imp"), TEXT("Imp"), TEXT("Small, mischievous and fond of biting ankles."), 20, TEXT("HPModels.skimpMesh") },
	{ TEXT("Cornish Pixie"), TEXT("CornishPixie"), TEXT("Electric blue and devilishly tricky. Lockhart's favourite disaster."), 20, TEXT("HPModels.skcornishpixieMesh") },
	{ TEXT("Doxy"), TEXT("Doxie1"), TEXT("Biting fairy with venomous teeth. Keep a Doxycide handy."), 25, NULL },
	{ TEXT("Small Spider"), TEXT("SpiderSmall"), TEXT("Acromantula hatchlings. Squishable."), 5, TEXT("HPModels.skSpiderSmallMesh") },
	{ TEXT("Large Spider"), TEXT("SpiderLarge|SpiderAttendent"), TEXT("Aragog's many children guard the Forbidden Forest."), 40, TEXT("HPModels.skSpiderLargeMesh") },
	{ TEXT("Fire Crab"), TEXT("firecrab"), TEXT("Looks like a tortoise, shoots flames from its rear."), 15, TEXT("HPModels.skfirecrabMesh") },
	{ TEXT("Bowtruckle"), TEXT("Bowtruckle"), TEXT("Tree guardian made of twigs. Bark is its treasure."), 15, TEXT("HPModels.skBowtruckleMesh") },
	{ TEXT("Ectoplasm"), TEXT("Ectoplasma"), TEXT("What ghosts leave behind. Skurge cleans it right up."), 15, TEXT("HPModels.skEctoplasmaMesh") },
	{ TEXT("Horklump"), TEXT("Horklumps"), TEXT("Fleshy pink mushroom. Gnomes love to eat them."), 10, TEXT("HPModels.skhorklumpsMesh") },
	{ TEXT("Spiky Plant"), TEXT("SpikyPlant"), TEXT("Shoots spikes at passers-by. Diffindo keeps it trimmed."), 10, TEXT("HPModels.skSpikyPlantMesh") },
	{ TEXT("Venomous Tentacula"), TEXT("Tentacula"), TEXT("A spiky dark red plant that grabs at anyone nearby."), 20, NULL },
	{ TEXT("Bosses"), TEXT("baseBoss"), TEXT("Peeves, Aragog and the Basilisk. Legends of the castle."), 250, TEXT("HPModels.skpeevesMesh") },
};

static const INT GBestiaryTiers[3] = { 5, 20, 50 };

/*-----------------------------------------------------------------------------
	Wizard card collection rewards.
-----------------------------------------------------------------------------*/

enum ECardKind { CARD_Bronze, CARD_Silver, CARD_Gold };

struct FCardReward
{
	INT Kind;
	INT Count;
	const TCHAR* Name;
	INT Stat;
	INT StatValue;
	INT SkillPoints;
};

static const FCardReward GCardRewards[] =
{
	{ CARD_Bronze, 10, TEXT("Bronze Collector"), STAT_BeanFind, 5, 0 },
	{ CARD_Bronze, 25, TEXT("Bronze Hoarder"), STAT_None, 0, 1 },
	{ CARD_Bronze, 50, TEXT("Complete Bronze Set"), STAT_XpGain, 10, 1 },
	{ CARD_Silver, 10, TEXT("Silver Collector"), STAT_DamageReduce, 5, 0 },
	{ CARD_Silver, 20, TEXT("Silver Hoarder"), STAT_None, 0, 1 },
	{ CARD_Silver, 40, TEXT("Complete Silver Set"), STAT_Speed, 6, 1 },
	{ CARD_Gold, 5, TEXT("Gold Collector"), STAT_None, 0, 1 },
	{ CARD_Gold, 11, TEXT("Every Famous Wizard"), STAT_SpellPower, 10, 2 },
};
static const INT GNumCardRewards = (INT)ARRAY_COUNT(GCardRewards);

/*-----------------------------------------------------------------------------
	Trophies (achievements with bronze / silver / gold tiers).
-----------------------------------------------------------------------------*/

enum ECounter
{
	CNT_Creatures, CNT_Gnomes, CNT_Students, CNT_Secrets, CNT_Potions, CNT_Mastered,
	CNT_Duels, CNT_Quidditch, CNT_Beans, CNT_Bought, CNT_HousePoints, CNT_Detentions,
	CNT_Level, CNT_Cards, CNT_Cosmetics, CNT_Perks, CNT_Talks, CNT_MAX
};

struct FTrophyDef
{
	INT Counter;
	const TCHAR* Name;
	const TCHAR* Desc;   // %i = goal
	INT Goals[3];
};

static const FTrophyDef GTrophies[] =
{
	{ CNT_Creatures, TEXT("Creature Hunter"), TEXT("Defeat %i creatures."), { 10, 50, 150 } },
	{ CNT_Gnomes, TEXT("De-gnomer"), TEXT("Throw %i gnomes."), { 5, 20, 50 } },
	{ CNT_Students, TEXT("Bully"), TEXT("Send %i students flying. Shame on you."), { 3, 15, 50 } },
	{ CNT_Secrets, TEXT("Secret Keeper"), TEXT("Find %i secret areas."), { 3, 10, 25 } },
	{ CNT_Potions, TEXT("Potions Master"), TEXT("Brew %i Wiggenweld potions."), { 1, 5, 15 } },
	{ CNT_Mastered, TEXT("Challenge Champion"), TEXT("Master %i spell challenges."), { 1, 3, 5 } },
	{ CNT_Duels, TEXT("Duelling Club"), TEXT("Win %i duels."), { 1, 5, 10 } },
	{ CNT_Quidditch, TEXT("Seeker"), TEXT("Win %i Quidditch matches."), { 1, 3, 6 } },
	{ CNT_Beans, TEXT("Bean Baron"), TEXT("Collect %i beans."), { 500, 2000, 6000 } },
	{ CNT_Bought, TEXT("Big Spender"), TEXT("Buy %i things from vendors."), { 3, 12, 30 } },
	{ CNT_HousePoints, TEXT("House Pride"), TEXT("Earn %i points for Gryffindor."), { 50, 250, 600 } },
	{ CNT_Level, TEXT("Rising Wizard"), TEXT("Reach level %i."), { 5, 12, 25 } },
	{ CNT_Cards, TEXT("Card Collector"), TEXT("Own %i Wizard Cards."), { 20, 60, 101 } },
	{ CNT_Cosmetics, TEXT("Fashionista"), TEXT("Own %i cosmetics."), { 3, 10, 25 } },
	{ CNT_Perks, TEXT("Spell Scholar"), TEXT("Learn %i spell perks."), { 2, 6, 13 } },
	{ CNT_Talks, TEXT("Popular"), TEXT("Chat with students %i times."), { 5, 25, 75 } },
	{ CNT_Detentions, TEXT("Troublemaker"), TEXT("Get %i detentions."), { 1, 3, 6 } },
};
static const INT GNumTrophies = (INT)ARRAY_COUNT(GTrophies);
static const TCHAR* GTrophyTierNames[3] = { TEXT("Bronze"), TEXT("Silver"), TEXT("Gold") };
static const FColor GTrophyTierColors[3] = { FColor(205,127,50,255), FColor(200,205,215,255), FColor(255,205,70,255) };
static const INT GTrophyXp[3] = { 50, 150, 400 };
static const INT GTrophyBeans[3] = { 25, 75, 200 };

/*-----------------------------------------------------------------------------
	Daily house tasks.
-----------------------------------------------------------------------------*/

struct FTaskDef
{
	INT Counter;
	INT Goals[3];       // easy / medium / hard, picked by level
	const TCHAR* Desc;  // %i = goal
	INT Points;         // Gryffindor house points
	INT Beans;
	INT Xp;
};

static const FTaskDef GTasks[] =
{
	{ CNT_Creatures, { 4, 8, 12 }, TEXT("Defeat %i creatures"), 15, 30, 40 },
	{ CNT_Gnomes, { 2, 4, 6 }, TEXT("Throw %i garden gnomes"), 10, 25, 30 },
	{ CNT_Beans, { 50, 100, 180 }, TEXT("Collect %i beans"), 10, 20, 30 },
	{ CNT_Secrets, { 1, 1, 2 }, TEXT("Find %i secret area(s)"), 20, 40, 60 },
	{ CNT_Potions, { 1, 1, 2 }, TEXT("Brew %i Wiggenweld potion(s)"), 15, 30, 50 },
	{ CNT_Talks, { 3, 5, 8 }, TEXT("Chat with %i students"), 5, 15, 20 },
	{ CNT_Bought, { 1, 2, 3 }, TEXT("Buy %i item(s) from vendors"), 5, 0, 25 },
	{ CNT_HousePoints, { 15, 30, 50 }, TEXT("Earn %i points for Gryffindor"), 10, 30, 40 },
	{ CNT_Cards, { 1, 2, 3 }, TEXT("Collect %i Wizard Card(s)"), 15, 25, 40 },
	{ CNT_Duels, { 1, 1, 2 }, TEXT("Win %i duel(s)"), 20, 40, 60 },
};
static const INT GNumTasks = (INT)ARRAY_COUNT(GTasks);
enum { TASKS_PER_DAY = 3 };

/*-----------------------------------------------------------------------------
	Owl post.
-----------------------------------------------------------------------------*/

enum EGift { GIFT_None, GIFT_Beans, GIFT_Potion, GIFT_ChocFrog, GIFT_BronzeCard, GIFT_SkillPoint };

struct FLetterDef
{
	const TCHAR* From;
	const TCHAR* Subject;
	const TCHAR* Body;
	INT MinLevel;
	INT Gift;
	INT GiftAmount;
};

static const FLetterDef GLetters[] =
{
	{ TEXT("Ron Weasley"), TEXT("Guess what"), TEXT("Harry,\nFred and George reckon they've found a way to make Bertie Bott's beans taste of treacle tart every time. They're charging five beans to tell you how. Don't pay them. I already did.\nSee you at dinner,\nRon"), 1, GIFT_Beans, 15 },
	{ TEXT("Hermione Granger"), TEXT("Revision timetable"), TEXT("Harry,\nI've drawn up a revision timetable for you. Please actually look at it this time. Practising your spells properly really does make them stronger, you know - I've seen it in the Spells tab of your notes.\nHermione"), 1, GIFT_None, 0 },
	{ TEXT("Rubeus Hagrid"), TEXT("Tea on Friday?"), TEXT("Dear Harry,\nCome down fer tea when yeh get a chance. I've made rock cakes. Fang's been missin' yeh. Watch out fer them gnomes in the garden, they're gettin' cheeky this year.\nHagrid"), 1, GIFT_ChocFrog, 1 },
	{ TEXT("Molly Weasley"), TEXT("Look after yourself"), TEXT("Harry dear,\nI hope you're eating properly. I've tucked a little something in with this owl - a Wiggenweld potion, just in case. Do keep an eye on Ron and Ginny for me.\nLove,\nMolly Weasley"), 2, GIFT_Potion, 1 },
	{ TEXT("Fred & George"), TEXT("Business proposal"), TEXT("Harrykins,\nWe've noticed you've been collecting rather a lot of beans. Should you ever wish to invest them in a certain pair of entrepreneurs, our stall is always open. Prices negotiable. Mostly upwards.\nGred and Forge"), 2, GIFT_Beans, 25 },
	{ TEXT("Oliver Wood"), TEXT("Training schedule"), TEXT("Potter,\nPractice is at six. A.M. If we're going to win the cup this year I need my Seeker sharp. Run, jump, train - every skill point counts!\nWood"), 3, GIFT_None, 0 },
	{ TEXT("Albus Dumbledore"), TEXT("A small observation"), TEXT("Dear Harry,\nIt is our choices that show what we truly are, far more than our abilities. I am told you have been kind to the students of every house. Do carry on.\nWith warm regards,\nA. Dumbledore\nP.S. I enclose a Chocolate Frog. I am rather fond of them."), 4, GIFT_ChocFrog, 1 },
	{ TEXT("Neville Longbottom"), TEXT("Thank you"), TEXT("Harry,\nThanks for helping me with my Flipendo. Gran says I'm getting better. I found this card in my trunk - I've already got three of him, so you have it.\nNeville"), 4, GIFT_BronzeCard, 1 },
	{ TEXT("Professor McGonagall"), TEXT("Your conduct"), TEXT("Mr Potter,\nIt has come to my attention that a number of students have been seen flying through the corridors. Unassisted by brooms. I trust this has nothing to do with you.\nProfessor M. McGonagall"), 5, GIFT_None, 0 },
	{ TEXT("Ginny Weasley"), TEXT("(no subject)"), TEXT("Harry,\nI just wanted to say good luck with everything. Ron says you've beaten loads of creatures already. That's brilliant.\nGinny"), 5, GIFT_Beans, 20 },
	{ TEXT("Hermione Granger"), TEXT("Something odd"), TEXT("Harry,\nI've been reading about secret passages in Hogwarts: A History. There are far more than the ones we know about. Keep your eyes open - and maybe try looking behind things.\nHermione"), 6, GIFT_None, 0 },
	{ TEXT("Professor Flitwick"), TEXT("Extra credit"), TEXT("Dear Mr Potter,\nYour wandwork has impressed me greatly this term! Please accept this small token, and do come by for an extra lesson whenever you wish.\nProfessor F. Flitwick"), 7, GIFT_SkillPoint, 1 },
	{ TEXT("Rubeus Hagrid"), TEXT("Don' tell anyone"), TEXT("Harry,\nI found an egg. Never yeh mind what sort. Point is, if anyone asks, I'm jus' keepin' it warm fer a friend.\nHagrid"), 8, GIFT_ChocFrog, 2 },
	{ TEXT("Percy Weasley"), TEXT("Prefect's notice"), TEXT("Harry,\nAs a prefect I am obliged to remind you that hexing fellow students results in detention. As your friend's brother, I am obliged to remind you twice.\nP. Weasley, Prefect"), 3, GIFT_None, 0 },
	{ TEXT("Fred & George"), TEXT("Urgent"), TEXT("Harry,\nIf a striped bean ever turns up, grab it. Worth five of the normal ones. Don't ask how we know. Definitely don't ask Ron.\nF & G"), 1, GIFT_None, 0 },
	{ TEXT("Albus Dumbledore"), TEXT("The House Cup"), TEXT("Dear Harry,\nThe race for the House Cup is closer than many realise. Every task completed brings honour to your house - and to yourself.\nA. Dumbledore"), 6, GIFT_Beans, 50 },
};
static const INT GNumLetters = (INT)ARRAY_COUNT(GLetters);
enum { MAX_INBOX = 12 };

#endif
