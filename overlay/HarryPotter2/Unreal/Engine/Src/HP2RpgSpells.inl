/*=============================================================================
	HP2RpgSpells.inl: Extra spells of FHP2Rpg. Pick one with the mouse wheel (or keys 4-0),
	hold the right mouse button to aim and click the left button to cast.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	Accio, Revelio, Protego, Wingardium Leviosa, Glacius and Arresto Momentum
	are built from existing game pieces: HP1-era particle effects still in
	HPParticle.u, the game's own pickup/spell handlers, and per-actor tick
	control (an actor whose bTicked equals the level's Ticked flag skips its
	next tick).
=============================================================================*/

	enum ESpell2 { SP2_Accio, SP2_Revelio, SP2_Protego, SP2_Leviosa, SP2_Glacius, SP2_Arresto, SP2_Transfig, SP2_MAX };

	struct FSpell2Def
	{
		const TCHAR* Name;
		const TCHAR* Desc;
		INT UnlockLevel;
		FLOAT Cooldown;
		const TCHAR* Icon;
		FColor Color;
	};

	static const FSpell2Def& Spell2( INT i )
	{
		static const FSpell2Def Defs[SP2_MAX] =
		{
			{ TEXT("Accio"), TEXT("Summon beans, cards, stars and ingredients nearby."), 2, 8.f, TEXT("SpellShapes.Shapes.SPSalohomora"), FColor(255,214,100,255) },
			{ TEXT("Revelio"), TEXT("Reveal secrets, chests and collectibles for 8 seconds."), 3, 12.f, TEXT("SpellShapes.Shapes.SPSlumos"), FColor(150,220,255,255) },
			{ TEXT("Protego"), TEXT("A shield that blocks all damage for 4 seconds."), 4, 25.f, TEXT("SpellShapes.Shapes.SPSexpelliarmus"), FColor(140,200,255,255) },
			{ TEXT("Wingardium Leviosa"), TEXT("Lift a creature, student or vase and steer it with the mouse. Release to drop, click to throw."), 6, 10.f, TEXT("SpellShapes.Shapes.SPSspongify"), FColor(220,240,160,255) },
			{ TEXT("Glacius"), TEXT("Freeze a creature solid for 5 seconds."), 8, 12.f, TEXT("SpellShapes.Shapes.SPSmimble"), FColor(170,230,255,255) },
			{ TEXT("Arresto Momentum"), TEXT("Slow down every enemy and their attacks for 6 seconds."), 10, 40.f, TEXT("SpellShapes.Shapes.SPSskurge"), FColor(200,170,255,255) },
			{ TEXT("Transfiguration"), TEXT("Turn a student into an animal for 20 seconds. Teach troublemakers a lesson!"), 9, 15.f, TEXT("SpellShapes.Shapes.SPSdiffindo"), FColor(255,170,220,255) },
		};
		return Defs[i];
	}

	struct FPullEntry   { AActor* Actor; BYTE OldPhysics; FLOAT Age; };
	struct FRevealEntry { AActor* Actor; FVector Location; INT Kind; };
	struct FLiftEntry   { AActor* Actor; BYTE OldRole; BYTE OldPhysics; FVector Base; FLOAT Age; UBOOL bDropped; UBOOL bProp; UBOOL bControlled; UBOOL bThrown; FLOAT HoldDist; };
	struct FFreezeEntry { AActor* Actor; FLOAT Left; UBOOL OldEnviro; UTexture* OldTexture; BYTE OldFatness; FLOAT OldScaleGlow; };

	FLOAT SpellCooldown[SP2_MAX];
	TArray<FPullEntry> Pulls;
	TArray<FRevealEntry> Reveals;
	TArray<FLiftEntry> Lifts;
	TArray<FFreezeEntry> Freezes;
	FLOAT RevealLeft;
	FLOAT ShieldLeft;
	FLOAT ArrestoLeft;
	AActor* ShieldFx;
	INT SlowFrame;
	INT SpellsKnownNotified;

	// Mouse aiming.
	INT SelSpell;            // spell picked with the wheel / number keys
	UBOOL bSelOurs;          // 0: the wand (the game's own spells) is picked
	FLOAT SelFlash;          // seconds the picked spell's name stays shown
	UBOOL bAiming;           // right mouse held for our spell
	AActor* AimTarget;
	FVector CamPos, CamDir;  // last drawn camera
	UBOOL bCamValid;
	FLOAT LastCastClock;
	UBOOL bShowAllHud;       // Tab held: show the whole HUD

	UBOOL SpellUnlocked( INT i )
	{
		return State.Level >= Spell2(i).UnlockLevel;
	}

	/*-------------------------------------------------------------------------
		Small helpers.
	-------------------------------------------------------------------------*/

	AActor* SpawnFx( UGameEngine* Engine, const TCHAR* ParticleName, FVector Location, FLOAT Life )
	{
		UClass* Class = LoadParticleClass( ParticleName );
		if( !Class || (Class->ClassFlags & CLASS_Abstract) )
			return NULL;
		AActor* A = Engine->GLevel->SpawnActor( Class, NAME_None, NULL, NULL, Location, FRotator(0,0,0), NULL, 1 );
		if( A )
		{
			A->LifeSpan = Life;
			A->Tag = FName( TEXT("HP2RpgActor") );
		}
		return A;
	}

	void PlaySfx( const TCHAR* Path )
	{
		USound* Sound = (USound*)FRpgUi::LoadRooted( USound::StaticClass(), Path );
		APlayerPawn* Harry = CurrentHarry();
		if( Sound && Harry && Ui.Engine && Ui.Engine->Audio )
			Ui.Engine->Audio->PlaySound( Harry, (INT)Harry->GetIndex()*16 + SLOT_Interface*2 + 1, Sound, Harry->Location, 0.9f, GAudioDefaultRadius, 1.f, SF_No3D, 0.f );
	}

	FVector WandTip( APlayerPawn* Harry )
	{
		if( !Harry->WeaponLoc.IsZero() )
			return Harry->WeaponLoc;
		return Harry->Location + FVector( 0, 0, Harry->BaseEyeHeight*0.6f );
	}

	void PlayCastAnim( APlayerPawn* Harry )
	{
		UObject* Channel = GetObjProp( Harry, TEXT("HarryAnimChannel") );
		if( Channel )
			Channel->GotoState( FName(TEXT("stateCast")) );
	}

	// Only the states where a creature has hold of Harry (not stunned ones).
	UBOOL IsHoldingHarry( AActor* A )
	{
		static const TCHAR* States[] = { TEXT("stateBiteHarry"), TEXT("stateBeingCarried"), TEXT("AttachToHarry"), TEXT("stateWasHoldingByHarry") };
		const FName S = GetStateName( A );
		for( INT i=0; i<ARRAY_COUNT(States); i++ )
		{
			const FName N( States[i], FNAME_Find );
			if( N!=NAME_None && S==N )
				return 1;
		}
		return 0;
	}

	UBOOL IsGrabbingHarry( AActor* A )
	{
		static const TCHAR* States[] = { TEXT("stateBiteHarry"), TEXT("stateBeingCarried"), TEXT("stateBeingThrown"), TEXT("AttachToHarry"), TEXT("stateWasHoldingByHarry") };
		const FName S = GetStateName( A );
		for( INT i=0; i<ARRAY_COUNT(States); i++ )
		{
			const FName N( States[i], FNAME_Find );
			if( N!=NAME_None && S==N )
				return 1;
		}
		for( INT i=0; i<NumDeathStates; i++ )
			if( S==DeathStates[i] )
				return 1;
		return 0;
	}

	// Best target in front of Harry: creatures (and optionally students/props).
	AActor* PickTarget( UGameEngine* Engine, APlayerPawn* Harry, UBOOL bStudents, UBOOL bProps, FLOAT Range )
	{
		const FVector Fwd = Harry->Rotation.Vector();
		AActor* Best = NULL;
		FLOAT BestScore = 1e9f;
		AActor* Carry = (AActor*)GetObjProp( Harry, TEXT("CarryingActor") );
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A==Harry || A==Carry || A->bDeleteMe || A->bHidden || A->bStatic || !A->bMovable )
				continue;
			const UBOOL bCreature = CreatureType(A)>=0 && CreatureType(A)!=CR_Boss;
			const UBOOL bStudent = bStudents && IsStudent(A) && !IsHolding(A);
			const UBOOL bProp = bProps && ClassChainIsAny( A, TEXT("HFlipendo|chestbronze|bronzecauldron|GenericSpawner") );
			if( !bCreature && !bStudent && !bProp )
				continue;
			if( bCreature && IsGrabbingHarry(A) )
				continue;
			if( !Harry->LineOfSightTo(A) )
				continue;
			FVector D = A->Location - Harry->Location;
			const FLOAT Dist = D.Size();
			if( Dist>Range || Dist<1.f )
				continue;
			const FLOAT Dot = (D/Dist) | Fwd;
			if( Dot < 0.82f )   // ~35 degrees
				continue;
			const FLOAT Score = Dist*(2.f-Dot);
			if( Score<BestScore )
			{
				BestScore = Score;
				Best = A;
			}
		}
		return Best;
	}

	/*-------------------------------------------------------------------------
		Casting.
	-------------------------------------------------------------------------*/

	// Which spells need something to aim at.
	static UBOOL SpellNeedsTarget( INT i )
	{
		return i==SP2_Leviosa || i==SP2_Glacius || i==SP2_Transfig;
	}

	void CastSpell2( UGameEngine* Engine, APlayerPawn* Harry, INT i, AActor* Target )
	{
		const FSpell2Def& D = Spell2( i );
		if( !SpellUnlocked(i) )
		{
			AddFeed( FString::Printf(TEXT("%s is locked"), D.Name), FColor(180,180,200,255), TOAST_Info, *FString::Printf(TEXT("Learn it at level %i"), D.UnlockLevel) );
			return;
		}
		if( SpellCooldown[i]>0.f )
		{
			AddFeed( FString::Printf(TEXT("%s is recharging"), D.Name), FColor(180,180,200,255), TOAST_Info, *FString::Printf(TEXT("Ready in %i s"), appCeil(SpellCooldown[i])) );
			return;
		}
		if( SpellNeedsTarget(i) && !Target )
		{
			PlaySfx( TEXT("HPSounds.Magic_sfx.spell_dud") );
			AddFeed( FString::Printf(TEXT("%s needs a target"), D.Name), FColor(180,180,200,255), TOAST_Info, TEXT("Aim until the spell sign appears") );
			return;
		}
		UBOOL bCast = 0;
		switch( i )
		{
			case SP2_Accio:    bCast = CastAccio( Engine, Harry ); break;
			case SP2_Revelio:  bCast = CastRevelio( Engine, Harry ); break;
			case SP2_Protego:  bCast = CastProtego( Engine, Harry ); break;
			case SP2_Leviosa:  bCast = CastLeviosa( Engine, Harry, Target ); break;
			case SP2_Glacius:  bCast = CastGlacius( Engine, Harry, Target ); break;
			case SP2_Arresto:  bCast = CastArresto( Engine, Harry ); break;
			case SP2_Transfig: bCast = CastTransfig( Engine, Harry, Target ); break;
		}
		if( !bCast )
		{
			PlaySfx( TEXT("HPSounds.Magic_sfx.spell_dud") );
			AddFeed( FString::Printf(TEXT("%s found no target"), D.Name), FColor(180,180,200,255), TOAST_Info, TEXT("Get closer and try again") );
			RpgLog( TEXT("cast %s: no target"), D.Name );
			return;
		}
		SpellCooldown[i] = D.Cooldown;
		LastCastClock = RpgClock;
		State.Counters[CNT_WheelCasts]++;
		if( i==SP2_Leviosa ) State.Counters[CNT_Levitations]++;
		if( i==SP2_Transfig ) State.Counters[CNT_Transfigs]++;
		PlayCastAnim( Harry );
		RpgLog( TEXT("cast %s"), D.Name );
	}

	UBOOL CastAccio( UGameEngine* Engine, APlayerPawn* Harry )
	{
		INT Count = 0;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden || A->Owner )
				continue;
			if( !ClassChainIsAny( A, TEXT("Jellybean|WizardCardIcon|ChallengeStar|PotionIngredients|WiggenWell") ) )
				continue;
			static FName PickupProp( TEXT("PickupProp") );
			if( GetStateName(A)==PickupProp || !GetBoolProp(A,TEXT("bPickupOnTouch")) )
				continue;
			if( (A->Location - Harry->Location).SizeSquared() > 700.f*700.f || !Harry->LineOfSightTo(A) )
				continue;
			UBOOL bAlready = 0;
			for( INT p=0; p<Pulls.Num(); p++ )
				if( Pulls(p).Actor==A )
					bAlready = 1;
			if( bAlready )
				continue;
			FPullEntry* E = new(Pulls) FPullEntry;
			E->Actor = A;
			E->OldPhysics = A->Physics;
			E->Age = 0.f;
			A->setPhysics( PHYS_None );
			SpawnFx( Engine, TEXT("Spawn_flash_1"), A->Location, 1.f );
			Count++;
		}
		SpawnFx( Engine, TEXT("Wing_fly"), WandTip(Harry), 0.8f );
		PlaySfx( TEXT("HPSounds.Magic_sfx.Dueling_EXP_swoosh") );
		if( !Count )
			AddFeed( TEXT("Accio"), Spell2(SP2_Accio).Color, TOAST_Info, TEXT("Nothing to summon nearby") );
		return 1;
	}

	void UpdateAccio( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		for( INT p=Pulls.Num()-1; p>=0; p-- )
		{
			FPullEntry& E = Pulls(p);
			if( !IsInLevel(E.Actor) )
			{
				Pulls.Remove( p );
				continue;
			}
			static FName PickupProp( TEXT("PickupProp") );
			if( E.Actor->bHidden || GetStateName(E.Actor)==PickupProp )
			{
				Pulls.Remove( p ); // the game is already collecting it
				continue;
			}
			E.Age += DeltaSeconds;
			const FVector Target = Harry->Location + FVector( 0, 0, 20 );
			FVector D = Target - E.Actor->Location;
			const FLOAT Dist = D.Size();
			const FLOAT Reach = Harry->CollisionRadius + E.Actor->CollisionRadius + 12.f;
			if( Dist <= Reach || E.Age > 3.f )
			{
				if( Dist <= Reach*2.f )
				{
					FRpgArg Arg = FRpgArg::O( Harry );
					CallScript( E.Actor, TEXT("Touch"), &Arg, 1 );
				}
				if( IsInLevel(E.Actor) && GetStateName(E.Actor)!=FName(TEXT("PickupProp")) )
					E.Actor->setPhysics( E.OldPhysics );
				Pulls.Remove( p );
				continue;
			}
			const FLOAT Step = Min( Dist, 650.f*DeltaSeconds );
			Engine->GLevel->FarMoveActor( E.Actor, E.Actor->Location + D/Dist*Step, 0, 1 );
		}
	}

	UBOOL CastRevelio( UGameEngine* Engine, APlayerPawn* Harry )
	{
		Reveals.Empty();
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || (A->Location - Harry->Location).SizeSquared() > 2600.f*2600.f )
				continue;
			INT Kind = -1;
			const FName S = GetStateName( A );
			if( ClassChainIs(A,TEXT("SecretAreaMarker")) && !GetBoolProp(A,TEXT("bFound")) )
				Kind = 0;
			else if( ClassChainIsAny(A,TEXT("chestbronze|bronzecauldron")) && !GetBoolProp(A,TEXT("bOpened")) )
				Kind = 1;
			else if( ClassChainIs(A,TEXT("HFlipendo")) && S==FName(TEXT("waitforspell")) )
				Kind = 1;
			else if( ClassChainIs(A,TEXT("WizardCardIcon")) && !A->Owner && !A->bHidden && S!=FName(TEXT("PickupProp")) )
				Kind = 2;
			else if( ClassChainIsAny(A,TEXT("ChallengeStar|PotionIngredients|WiggenWell")) && !A->Owner && !A->bHidden && S!=FName(TEXT("PickupProp")) )
				Kind = 3;
			if( Kind<0 )
				continue;
			FRevealEntry* E = new(Reveals) FRevealEntry;
			E->Actor = A;
			E->Location = A->Location;
			E->Kind = Kind;
			static const TCHAR* Fx[4] = { TEXT("secretdust"), TEXT("QuestionFX"), TEXT("GoldSparkle01"), TEXT("Goldstar01") };
			SpawnFx( Engine, Fx[Kind], A->Location, 8.f );
		}
		RevealPages( Engine, Harry );
		RevealLeft = 8.f;
		PlaySfx( TEXT("HPSounds.Magic_sfx.Lumos_pass_through") );
		AddFeed( TEXT("Revelio"), Spell2(SP2_Revelio).Color, TOAST_Info, *FString::Printf(TEXT("%i hidden thing%s revealed"), Reveals.Num(), Reveals.Num()==1 ? TEXT("") : TEXT("s")) );
		return 1;
	}

	UBOOL CastProtego( UGameEngine* Engine, APlayerPawn* Harry )
	{
		ShieldLeft = 4.f;
		if( ShieldFx && IsInLevel(ShieldFx) )
			Engine->GLevel->DestroyActor( ShieldFx );
		ShieldFx = SpawnFx( Engine, TEXT("Exep_Shield"), Harry->Location, 4.2f );
		PlaySfx( TEXT("HPSounds.Magic_sfx.Dueling_switch2MIM") );
		return 1;
	}

	UBOOL CastLeviosa( UGameEngine* Engine, APlayerPawn* Harry, AActor* T )
	{
		if( !T )
			return 0;
		for( INT l=0; l<Lifts.Num(); l++ )
			if( Lifts(l).Actor==T || Lifts(l).bControlled )
				return 0;
		FLiftEntry* E = new(Lifts) FLiftEntry;
		E->Actor = T;
		E->OldRole = T->Role;
		E->OldPhysics = T->Physics;
		E->Base = T->Location;
		E->Age = 0.f;
		E->bDropped = 0;
		E->bProp = !T->IsA(APawn::StaticClass());
		E->bControlled = 1;
		E->bThrown = 0;
		E->HoldDist = Clamp( (T->Location - Harry->Location).Size2D(), 160.f, 520.f );
		SpawnFx( Engine, TEXT("Levitate_hold"), T->Location, 2.f );
		T->Role = ROLE_DumbProxy;
		T->setPhysics( PHYS_None );
		T->Velocity = FVector( 0, 0, 0 );
		SpawnFx( Engine, TEXT("Levitate_wand"), WandTip(Harry), 0.8f );
		SpawnFx( Engine, TEXT("levitate_hit"), T->Location, 1.f );
		PlaySfx( TEXT("HPSounds.Magic_sfx.Spell_aim") );
		if( IsStudent(T) )
			StudentHitBySpell( T, 5, -3, TEXT("Levitating a student") );
		return 1;
	}

	FLiftEntry* ControlledLift()
	{
		for( INT l=0; l<Lifts.Num(); l++ )
			if( Lifts(l).bControlled && IsInLevel(Lifts(l).Actor) )
				return &Lifts(l);
		return NULL;
	}

	// Lets go of the levitated target: a drop, or a throw along the aim.
	void ReleaseLift( UGameEngine* Engine, FLiftEntry& E, UBOOL bThrow )
	{
		AActor* T = E.Actor;
		E.bControlled = 0;
		E.bDropped = 1;
		E.Age = 0.f;
		T->setPhysics( PHYS_Falling );
		SpawnFx( Engine, TEXT("Wing_hit"), T->Location, 1.f );
		if( bThrow && bCamValid )
		{
			E.bThrown = 1;
			T->Velocity = CamDir*1100.f + FVector( 0, 0, 260.f );
			PlaySfx( TEXT("HPSounds.Magic_sfx.Dueling_EXP_swoosh") );
			RpgLog( TEXT("leviosa: threw %s"), T->GetName() );
		}
		else
		{
			T->Velocity = FVector( 0, 0, -100.f );
			PlaySfx( TEXT("HPSounds.Magic_sfx.SPN_bounce_on") );
		}
	}

	void UpdateLeviosa( UGameEngine* Engine, FLOAT DeltaSeconds )
	{
		for( INT l=Lifts.Num()-1; l>=0; l-- )
		{
			FLiftEntry& E = Lifts(l);
			if( !IsInLevel(E.Actor) )
			{
				Lifts.Remove( l );
				continue;
			}
			AActor* T = E.Actor;
			E.Age += DeltaSeconds;
			if( E.bControlled )
			{
				APlayerPawn* Harry = CurrentHarry();
				if( !Harry || !bAiming || !bCamValid || E.Age>15.f )
				{
					ReleaseLift( Engine, E, 0 );
					continue;
				}
				// Float where Harry aims, at the held distance.
				FVector Flat( CamDir.X, CamDir.Y, 0.f );
				Flat = Flat.SafeNormal();
				FVector Want = Harry->Location + Flat*E.HoldDist;
				Want.Z = Clamp( Harry->Location.Z + 70.f + CamDir.Z*E.HoldDist + (FLOAT)appSin(E.Age*4.f)*6.f, Harry->Location.Z - 30.f, Harry->Location.Z + 420.f );
				FCheckResult Hit;
				Engine->GLevel->MoveActor( T, (Want - T->Location)*Min(1.f, DeltaSeconds*8.f), T->Rotation + FRotator(0,appRound(6000.f*DeltaSeconds),0), Hit );
				T->Velocity = FVector( 0, 0, 0 );
				continue;
			}
			if( !E.bDropped )
			{
				if( E.Age < 3.5f )
				{
					const FLOAT Rise = Min( 1.f, E.Age/0.5f )*90.f + (FLOAT)appSin(E.Age*4.f)*6.f;
					FCheckResult Hit;
					const FVector Want = E.Base + FVector( 0, 0, Rise );
					Engine->GLevel->MoveActor( T, Want - T->Location, T->Rotation + FRotator(0,appRound(9000.f*DeltaSeconds),0), Hit );
					continue;
				}
				// Drop it.
				E.bDropped = 1;
				T->setPhysics( PHYS_Falling );
				T->Velocity = FVector( 0, 0, -100.f );
				SpawnFx( Engine, TEXT("Wing_hit"), T->Location, 1.f );
				PlaySfx( TEXT("HPSounds.Magic_sfx.SPN_bounce_on") );
				continue;
			}
			if( T->Physics==PHYS_Falling && E.Age < 8.f )
				continue;
			// Landed: hand control back, and let props react like a Flipendo hit.
			T->Role = E.OldRole;
			if( E.bProp )
			{
				T->Velocity = FVector( 0, 0, 0 );
				T->setPhysics( E.OldPhysics );
			}
			else if( E.OldPhysics!=PHYS_Walking && E.OldPhysics!=PHYS_None && E.OldPhysics!=PHYS_Falling )
				T->setPhysics( E.OldPhysics );
			const INT Vuln = GetByteProp( T, TEXT("eVulnerableToSpell"), 0 );
			if( E.bProp && Vuln==13 )
				QueueRedispatch( T, Vuln ); // only props a Flipendo would open
			else if( CreatureType(T)>=0 )
				QueueRedispatch( T, 13 );
			Lifts.Remove( l );
		}
	}

	UBOOL CastGlacius( UGameEngine* Engine, APlayerPawn* Harry, AActor* T )
	{
		if( !T || CreatureType(T)<0 )
			return 0;
		for( INT f=0; f<Freezes.Num(); f++ )
			if( Freezes(f).Actor==T )
			{
				Freezes(f).Left = 5.f;
				return 1;
			}
		static UTexture* Ice = NULL;
		if( !Ice )
			Ice = LoadTex( TEXT("HPParticle.hp_fx.General.EctoIce") );
		FFreezeEntry* E = new(Freezes) FFreezeEntry;
		E->Actor = T;
		E->Left = 5.f;
		E->OldEnviro = T->bMeshEnviroMap;
		E->OldTexture = T->Texture;
		E->OldFatness = T->Fatness;
		E->OldScaleGlow = T->ScaleGlow;
		if( Ice )
		{
			T->bMeshEnviroMap = 1;
			T->Texture = Ice;
		}
		T->Fatness = 136;
		T->ScaleGlow = 1.6f;
		T->Velocity = FVector( 0, 0, 0 );
		SpawnFx( Engine, TEXT("Incend_hit"), T->Location, 1.5f );
		PlaySfx( TEXT("HPSounds.Magic_sfx.spell_hit") );
		return 1;
	}

	void Thaw( UGameEngine* Engine, FFreezeEntry& E, UBOOL bEffects )
	{
		AActor* T = E.Actor;
		T->bMeshEnviroMap = E.OldEnviro;
		T->Texture = E.OldTexture;
		T->Fatness = E.OldFatness;
		T->ScaleGlow = E.OldScaleGlow;
		if( bEffects )
		{
			SpawnFx( Engine, TEXT("Incend_hit"), T->Location, 1.f );
			PlaySfx( TEXT("HPSounds.General.ss_Jar_Smash_0001") );
		}
	}

	UBOOL IsFrozen( UObject* Obj )
	{
		for( INT f=0; f<Freezes.Num(); f++ )
			if( Freezes(f).Actor==Obj )
				return 1;
		return 0;
	}

	UBOOL CastArresto( UGameEngine* Engine, APlayerPawn* Harry )
	{
		ArrestoLeft = 6.f;
		SpawnFx( Engine, TEXT("levitate_hit"), Harry->Location, 1.f );
		PlaySfx( TEXT("HPSounds.Magic_sfx.Dueling_MIM_buildup") );
		return 1;
	}

	struct FClassType { UClass* Class; INT Type; };
	TArray<FClassType> ClassTypes;

	INT CreatureTypeCached( AActor* A )
	{
		UClass* C = A->GetClass();
		for( INT i=0; i<ClassTypes.Num(); i++ )
			if( ClassTypes(i).Class==C )
				return ClassTypes(i).Type;
		FClassType* E = new(ClassTypes) FClassType;
		E->Class = C;
		E->Type = CreatureType( A );
		return E->Type;
	}

	// Runs right after the level tick: anything whose bTicked matches the
	// level's flag skips its next tick.
	void HoldActorTicks( UGameEngine* Engine, APlayerPawn* Harry )
	{
		const UBOOL Flag = Engine->GLevel->Ticked;
		for( INT f=0; f<Freezes.Num(); f++ )
			if( IsInLevel(Freezes(f).Actor) )
				Freezes(f).Actor->bTicked = Flag;
		if( ArrestoLeft<=0.f )
			return;
		// Slow enemies: they only tick on two frames out of five.
		SlowFrame = (SlowFrame+1) % 5;
		if( SlowFrame>=2 )
		{
			static UClass* Spell = NULL;
			if( !Spell )
				Spell = FindClass( TEXT("baseSpell") );
			for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
			{
				AActor* A = Engine->GLevel->Actors(i);
				if( !A || A==Harry || A->bDeleteMe )
					continue;
				if( A==(AActor*)GetObjProp(Harry,TEXT("CarryingActor")) )
					continue;
				const INT Type = CreatureTypeCached( A );
				const UBOOL bEnemy = Type>=0 && Type!=CR_Boss;
				const UBOOL bHostileSpell = Spell && A->IsA(Spell) && !IsHarrysSpell(A,Harry);
				const UBOOL bHazard = ClassChainIsAny( A, TEXT("Fireball|FireballLarge|ThrownSpike|ThrownPoisonCloud|Bludger") );
				if( bEnemy || bHostileSpell || bHazard )
					A->bTicked = Flag;
			}
		}
	}

	INT CountKnownSpells()
	{
		INT Known = 0;
		for( INT i=0; i<SP2_MAX; i++ )
			if( SpellUnlocked(i) )
				Known = i+1;
		return Known;
	}

	void UpdateSpells( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( Engine->GLevel->GetLevelInfo()->Pauser.Len() )
		{
			HoldActorTicks( Engine, Harry ); // keep frozen actors in step
			return;
		}
		for( INT i=0; i<SP2_MAX; i++ )
			SpellCooldown[i] = Max( 0.f, SpellCooldown[i]-DeltaSeconds );
		RevealLeft = Max( 0.f, RevealLeft-DeltaSeconds );
		ArrestoLeft = Max( 0.f, ArrestoLeft-DeltaSeconds );
		if( ShieldLeft>0.f )
		{
			ShieldLeft = Max( 0.f, ShieldLeft-DeltaSeconds );
			if( ShieldFx && IsInLevel(ShieldFx) )
				Engine->GLevel->FarMoveActor( ShieldFx, Harry->Location, 0, 1 );
			else
				ShieldFx = NULL;
		}
		UpdateAccio( Engine, Harry, DeltaSeconds );
		UpdateAim( Engine, Harry, DeltaSeconds );
		UpdateLeviosa( Engine, DeltaSeconds );
		UpdateTransfigs( Engine, DeltaSeconds );
		for( INT f=Freezes.Num()-1; f>=0; f-- )
		{
			FFreezeEntry& E = Freezes(f);
			if( !IsInLevel(E.Actor) )
			{
				Freezes.Remove( f );
				continue;
			}
			E.Left -= DeltaSeconds;
			if( E.Left<=0.f )
			{
				Thaw( Engine, E, 1 );
				Freezes.Remove( f );
			}
		}
		HoldActorTicks( Engine, Harry );

		// Announce spells unlocked by levelling up (SpellsKnownNotified is
		// synced silently whenever a save is loaded).
		// (SpellsKnownNotified holds the level that was last announced.)
		if( SpellsKnownNotified<0 )
			SpellsKnownNotified = State.Level;
		for( INT i=0; i<SP2_MAX; i++ )
			if( Spell2(i).UnlockLevel>SpellsKnownNotified && Spell2(i).UnlockLevel<=State.Level )
				AddFeed( FString::Printf(TEXT("New spell: %s"), Spell2(i).Name), Spell2(i).Color, TOAST_Reward, TEXT("Scroll to it, hold right mouse to aim") );
		SpellsKnownNotified = State.Level;
	}

	// Ends every running spell and restores the actors it changed (before a
	// save, a level change or closing).
	void EndAllSpells( UGameEngine* Engine )
	{
		for( INT f=0; f<Freezes.Num(); f++ )
			if( IsInLevel(Freezes(f).Actor) )
				Thaw( Engine, Freezes(f), 0 );
		Freezes.Empty();
		for( INT l=0; l<Lifts.Num(); l++ )
			if( IsInLevel(Lifts(l).Actor) )
			{
				AActor* T = Lifts(l).Actor;
				T->Role = Lifts(l).OldRole;
				const BYTE OldP = Lifts(l).OldPhysics;
				T->setPhysics( (OldP==PHYS_Walking || OldP==PHYS_None) ? PHYS_Falling : OldP );
			}
		Lifts.Empty();
		for( INT t=0; t<Transfigs.Num(); t++ )
			if( IsInLevel(Transfigs(t).Actor) )
				Untransfigure( Engine, Transfigs(t), 0 );
		Transfigs.Empty();
		bAiming = 0;
		AimTarget = NULL;
		for( INT p=0; p<Pulls.Num(); p++ )
			if( IsInLevel(Pulls(p).Actor) )
				Pulls(p).Actor->setPhysics( Pulls(p).OldPhysics );
		Pulls.Empty();
		Reveals.Empty();
		DestroyActorSafe( Engine, ShieldFx );
		ArrestoLeft = RevealLeft = ShieldLeft = 0.f;
	}

	void ForgetSpells()
	{
		Freezes.Empty();
		Lifts.Empty();
		Transfigs.Empty();
		bAiming = 0;
		AimTarget = NULL;
		bCamValid = 0;
		Pulls.Empty();
		Reveals.Empty();
		ShieldFx = NULL;
		ArrestoLeft = RevealLeft = ShieldLeft = 0.f;
	}


	/*-------------------------------------------------------------------------
		Transfiguration: a student becomes a cat or an owl for a while.
	-------------------------------------------------------------------------*/

	struct FTransfigEntry
	{
		AActor* Actor;
		UMesh* OldMesh;
		UTexture* OldSkin;
		UTexture* OldMulti[8];
		UAnimation* OldAnim;
		FLOAT OldScale;
		BYTE OldRole;
		FLOAT Left;
	};
	TArray<FTransfigEntry> Transfigs;

	UBOOL IsTransfigured( AActor* A )
	{
		for( INT t=0; t<Transfigs.Num(); t++ )
			if( Transfigs(t).Actor==A )
				return 1;
		return 0;
	}

	UBOOL CastTransfig( UGameEngine* Engine, APlayerPawn* Harry, AActor* T )
	{
		if( !T || !(IsStudent(T) || IsTroublemaker(T)) || IsTransfigured(T) )
			return 0;
		// Mesh, animation set, idle sequence, height in units, name.
		static const TCHAR* Forms[][5] =
		{
			{ TEXT("HPModels.skMrsNorrisMesh"),   TEXT("HPModels.skMrsNorrisAnims"),   TEXT("Idle"),    TEXT("40"), TEXT("a cat") },
			{ TEXT("HProps.skChocolateFrogMesh"), TEXT("HProps.skChocolateFrogAnims"), TEXT("croak"),   TEXT("26"), TEXT("a frog") },
			{ TEXT("HPModels.skPigMesh"),         TEXT("HPModels.skPigAnims"),         TEXT("Idle"),    TEXT("45"), TEXT("a pig") },
			{ TEXT("HPModels.skowlbarnMesh"),     TEXT("HPModels.skowlbarnAnims"),     TEXT("Breathe"), TEXT("40"), TEXT("an owl") },
		};
		INT F = RpgRand( (INT)ARRAY_COUNT(Forms) );
		UMesh* Mesh = LoadMesh( Forms[F][0] );
		if( !Mesh && F==2 )
			Mesh = LoadMesh( TEXT("HProps.skPigMesh") );
		if( !Mesh )
		{
			RpgLog( TEXT("transfiguration: form %s missing, using a cat"), Forms[F][0] );
			F = 0;
			Mesh = LoadMesh( Forms[0][0] );
		}
		UAnimation* Anim = (UAnimation*)FRpgUi::LoadRooted( UAnimation::StaticClass(), Forms[F][1] );
		if( !Anim && F==2 )
			Anim = (UAnimation*)FRpgUi::LoadRooted( UAnimation::StaticClass(), TEXT("HProps.skPigAnims") );
		if( !Mesh )
		{
			RpgLog( TEXT("transfiguration: form %s missing"), Forms[F][0] );
			return 0;
		}
		FTransfigEntry* E = new(Transfigs) FTransfigEntry;
		E->Actor = T;
		E->OldMesh = T->Mesh;
		E->OldSkin = T->Skin;
		for( INT i=0; i<8; i++ )
			E->OldMulti[i] = T->MultiSkins[i];
		E->OldAnim = T->SkelAnim;
		E->OldScale = T->DrawScale;
		E->OldRole = T->Role;
		E->Left = 20.f;
		const FVector Feet = T->Location - FVector( 0, 0, T->CollisionHeight );
		T->Mesh = Mesh;
		T->Skin = NULL;
		for( INT i=0; i<8; i++ )
			T->MultiSkins[i] = NULL;
		T->SkelAnim = Anim ? Anim : (Mesh->IsA(USkeletalMesh::StaticClass()) ? ((USkeletalMesh*)Mesh)->DefaultAnimation : NULL);
		T->Role = ROLE_DumbProxy;
		T->Velocity = FVector( 0, 0, 0 );
		T->Acceleration = FVector( 0, 0, 0 );
		// Animal sized: about 45 units tall.
		T->DrawScale = 1.f;
		{
			const FCoords Box = T->GetRenderBoundingBox( 1 );
			const FLOAT H = Max( Box.ZAxis.Size(), 1.f );
			T->DrawScale = Clamp( appAtof(Forms[F][3])/H, 0.1f, 3.f );
		}
		if( T->SkelAnim )
		{
			FName Idle( Forms[F][2], FNAME_Find );
			if( Idle==NAME_None || !T->SkelAnim->GetAnimSeq(Idle) )
				Idle = T->SkelAnim->AnimSeqs.Num() ? T->SkelAnim->AnimSeqs(0).Name : NAME_None;
			if( Idle!=NAME_None )
				T->PlayAnim( Idle, true, 1.f, 0.f );
		}
		SpawnFx( Engine, TEXT("SmokeExplo_01"), T->Location, 1.5f );
		SpawnFx( Engine, TEXT("WizCrackSparkle"), Feet, 1.5f );
		AddFeed( TEXT("Transfiguration"), Spell2(SP2_Transfig).Color, TOAST_Info, *FString::Printf(TEXT("%s is now %s!"), *NpcName(T), Forms[F][4]) );
		PlaySfx( TEXT("HPSounds.Magic_sfx.spell_hit") );
		RpgLog( TEXT("transfigured %s into %s"), T->GetName(), Mesh->GetName() );
		StudentHitBySpell( T, 15, -10, TEXT("Transfiguring a student") );
		return 1;
	}

	void Untransfigure( UGameEngine* Engine, FTransfigEntry& E, UBOOL bEffects )
	{
		AActor* T = E.Actor;
		T->Mesh = E.OldMesh;
		T->Skin = E.OldSkin;
		for( INT i=0; i<8; i++ )
			T->MultiSkins[i] = E.OldMulti[i];
		T->SkelAnim = E.OldAnim;
		T->DrawScale = E.OldScale;
		T->Role = E.OldRole;
		T->AnimSequence = NAME_None;
		if( bEffects )
		{
			SpawnFx( Engine, TEXT("SmokeExplo_02"), T->Location, 1.5f );
			PlaySfx( TEXT("HPSounds.Magic_sfx.spell_hit") );
		}
	}

	void UpdateTransfigs( UGameEngine* Engine, FLOAT DeltaSeconds )
	{
		for( INT t=Transfigs.Num()-1; t>=0; t-- )
		{
			FTransfigEntry& E = Transfigs(t);
			if( !IsInLevel(E.Actor) )
			{
				Transfigs.Remove( t );
				continue;
			}
			E.Left -= DeltaSeconds;
			E.Actor->Velocity = FVector( 0, 0, 0 );
			if( E.Left<=0.f )
			{
				Untransfigure( Engine, E, 1 );
				Transfigs.Remove( t );
			}
		}
	}

	/*-------------------------------------------------------------------------
		Mouse aiming.
	-------------------------------------------------------------------------*/

	UBOOL IsPickup( AActor* A )
	{
		static FName PickupProp( TEXT("PickupProp") );
		return ClassChainIsAny( A, TEXT("Jellybean|WizardCardIcon|ChallengeStar|PotionIngredients|WiggenWell") ) && !A->Owner && GetStateName(A)!=PickupProp;
	}

	// Loose things and animals: any small game prop or animal that moves,
	// but nothing the story or the level needs to stay put.
	UBOOL IsLevitatable( AActor* A )
	{
		if( !A->Mesh || A->Tag==FName(TEXT("HP2RpgActor")) || A->bNoDelete || IsHolding(A) || IsStaff(A) )
			return 0;
		if( A->IsA(AMover::StaticClass()) || A->IsA(ATriggers::StaticClass()) || A->IsA(AInventory::StaticClass()) )
			return 0;
		// Puzzle and story props (chests, spell targets, spawners, save books,
		// brewing stations, stars) keep working only if they stay where they are.
		if( ClassChainIsAny( A, TEXT("chestbronze|bronzecauldron|CauldronMixing|GenericSpawner|SpongifyTarget|SavePoint|BooksSaveBook|ChallengeStar|GnomeHome|Quaffle|WizDuelKiosk") ) )
			return 0;
		if( A->Event!=NAME_None || GetBoolProp(A,TEXT("bPersistent")) || GetByteProp(A,TEXT("eVulnerableToSpell"),0)!=0 )
			return 0;
		if( ClassChainIsAny( A, TEXT("Pig|GNOME|HProp") ) )
			return A->CollisionRadius<=60.f && A->CollisionHeight<=90.f;
		return 0;
	}

	// What the spell can be aimed at.
	UBOOL AimAccepts( INT Spell, AActor* A )
	{
		const INT Type = CreatureTypeCached( A );
		const UBOOL bCreature = Type>=0 && Type!=CR_Boss && (Spell==SP2_Leviosa ? !IsHoldingHarry(A) : !IsGrabbingHarry(A));
		const UBOOL bStudent = (IsStudent(A) || IsTroublemaker(A)) && !IsHolding(A);
		switch( Spell )
		{
			case SP2_Leviosa:  return bCreature || (bStudent && !IsTransfigured(A)) || IsLevitatable(A);
			case SP2_Glacius:  return bCreature;
			case SP2_Transfig: return bStudent && !IsTransfigured(A);
			default:           return 0;
		}
	}

	// The actor closest to the centre of the aim (the camera's view line).
	AActor* PickAimTarget( UGameEngine* Engine, APlayerPawn* Harry, INT Spell )
	{
		if( !bCamValid || !SpellNeedsTarget(Spell) )
			return NULL;
		AActor* Best = NULL;
		FLOAT BestScore = 1e9f;
		AActor* Carry = (AActor*)GetObjProp( Harry, TEXT("CarryingActor") );
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A==Harry || A==Carry || A->bDeleteMe || A->bHidden || A->bStatic || !A->bMovable )
				continue;
			const FLOAT HarryDist = (A->Location - Harry->Location).Size();
			if( HarryDist>950.f || !AimAccepts(Spell,A) )
				continue;
			const FVector V = A->Location - CamPos;
			const FLOAT Along = V | CamDir;
			if( Along<40.f )
				continue;
			const FLOAT Off = (V - CamDir*Along).Size();
			const FLOAT Allow = Max( Along*0.17f, A->CollisionRadius + 30.f );
			if( Off>Allow )
				continue;
			if( !Harry->LineOfSightTo(A) )
				continue;
			const FLOAT Score = Off/Along + HarryDist*0.0002f;
			if( Score<BestScore )
			{
				BestScore = Score;
				Best = A;
			}
		}
		return Best;
	}

	void SelectSpell( INT i )
	{
		SelSpell = Clamp( i, 0, SP2_MAX-1 );
		bSelOurs = 1;
		SelFlash = 1.6f;
		AimTarget = NULL;
		if( !SpellUnlocked(SelSpell) )
			AddFeed( FString::Printf(TEXT("%s is locked"), Spell2(SelSpell).Name), FColor(180,180,200,255), TOAST_Info, *FString::Printf(TEXT("Learn it at level %i"), Spell2(SelSpell).UnlockLevel) );
		else
			Ui.Play( RSND_Tab, CurrentHarry() );
	}

	void SelectWand()
	{
		bSelOurs = 0;
		SelFlash = 1.6f;
		AimTarget = NULL;
		Ui.Play( RSND_Tab, CurrentHarry() );
	}

	// Next/previous slot: the wand, then every unlocked spell.
	void CycleSpell( INT Dir )
	{
		TArray<INT> Slots;
		Slots.AddItem( -1 );
		for( INT i=0; i<SP2_MAX; i++ )
			if( SpellUnlocked(i) )
				Slots.AddItem( i );
		INT Cur = 0;
		for( INT k=0; k<Slots.Num(); k++ )
			if( bSelOurs && Slots(k)==SelSpell )
				Cur = k;
		const INT Next = Slots( (Cur + Dir + Slots.Num()*4) % Slots.Num() );
		if( Next<0 )
			SelectWand();
		else
			SelectSpell( Next );
	}

	void StopAiming( UGameEngine* Engine )
	{
		bAiming = 0;
		AimTarget = NULL;
		FLiftEntry* L = ControlledLift();
		if( L )
			ReleaseLift( Engine, *L, 0 );
	}

	// Mouse input during gameplay: returns 1 when the event was ours.
	UBOOL AimInput( UGameEngine* Engine, APlayerPawn* Harry, EInputKey Key, EInputAction Action )
	{
		if( Key==IK_RightMouse )
		{
			if( Action==IST_Press && !bAiming && (!bSelOurs || SpellUnlocked(SelSpell)) && CanCastNow(Engine,Harry) && !GetBoolProp(Harry,TEXT("bInDuelingMode")) )
			{
				bAiming = 1;
				AimTarget = bSelOurs ? PickAimTarget( Engine, Harry, SelSpell ) : NULL;
				return 1;
			}
			if( Action==IST_Release && bAiming )
			{
				StopAiming( Engine );
				return 1;
			}
			return 0;
		}
		// With the wand picked, the game casts its own spells as always.
		if( Key==IK_LeftMouse && bAiming && bSelOurs )
		{
			if( Action==IST_Press )
			{
				FLiftEntry* L = ControlledLift();
				if( L )
					ReleaseLift( Engine, *L, 1 );
				else
					CastSpell2( Engine, Harry, SelSpell, AimTarget );
			}
			return 1;
		}
		if( (Key==IK_MouseWheelUp || Key==IK_MouseWheelDown) && Action==IST_Press && CanCastNow(Engine,Harry) )
		{
			const INT Dir = Key==IK_MouseWheelUp ? -1 : 1;
			FLiftEntry* L = ControlledLift();
			if( L )
				L->HoldDist = Clamp( L->HoldDist - Dir*45.f, 120.f, 700.f );
			else
				CycleSpell( Dir );
			return 1;
		}
		if( Key==IK_Tab && (Action==IST_Press || Action==IST_Release) )
		{
			bShowAllHud = Action==IST_Press;
			return 0;
		}
		return 0;
	}

	void UpdateAim( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		SelFlash = Max( 0.f, SelFlash-DeltaSeconds );
		if( !bAiming )
			return;
		if( !CanCastNow(Engine,Harry) )
		{
			StopAiming( Engine );
			return;
		}
		if( !ControlledLift() )
			AimTarget = bSelOurs ? PickAimTarget( Engine, Harry, SelSpell ) : NULL;
	}

	FString AimLabel( AActor* A )
	{
		if( IsTroublemaker(A) )
			return FString::Printf( TEXT("%s (troublemaker)"), *NpcName(A) );
		const INT Type = CreatureTypeCached( A );
		if( Type>=0 )
			return GCreatures[Type].Name;
		if( IsStudent(A) )
			return NpcName( A );
		return TEXT("");
	}

	// Screen box around an actor (its collision cylinder).
	UBOOL ActorScreenBox( AActor* A, FRpgRect& Out )
	{
		FLOAT X1, Y1, X2, Y2;
		if( !WorldToScreen(A->Location + FVector(0,0,A->CollisionHeight), X1, Y1) || !WorldToScreen(A->Location - FVector(0,0,A->CollisionHeight), X2, Y2) )
			return 0;
		const FLOAT H = Max( Ui.P(48.f), Abs(Y2-Y1) );
		const FLOAT W = Max( Ui.P(48.f), H*Clamp(A->CollisionRadius/Max(1.f,A->CollisionHeight), 0.5f, 1.4f) );
		const FLOAT CX = (X1+X2)*0.5f, CY = (Y1+Y2)*0.5f;
		Out = FRpgRect( CX-W*0.5f, CY-H*0.5f, W, H );
		return 1;
	}

	void DrawAim( APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		const FColor Text1( 240,238,230,255 ), Dim( 110,116,146,255 );
		const FLOAT T = Max( 1.f, Ui.P(2.f) );
		const FLOAT PromptY = C->ClipY - Ui.P(160.f);
		if( !bSelOurs )
		{
			const TCHAR* Keys[1] = { TEXT("LMB") };
			const TCHAR* Verbs[1] = { TEXT("Cast your wand spell") };
			Ui.Prompt( C->ClipX*0.5f, PromptY, Keys, Verbs, 1 );
			return;
		}
		const FSpell2Def& D = Spell2( SelSpell );
		UTexture* Icon = LoadTex( D.Icon );
		FLiftEntry* L = ControlledLift();
		if( L )
		{
			FRpgRect Box;
			if( ActorScreenBox(L->Actor, Box) )
			{
				Ui.Brackets( Box, Ui.P(12.f), T, D.Color );
				if( Icon )
					Ui.Icon( Icon, FRpgRect(Box.X+Box.W*0.5f-Ui.P(14.f), Box.Y-Ui.P(36.f), Ui.P(28.f), Ui.P(28.f)) );
				// Distance gauge.
				const FRpgRect Gauge( Box.X+Box.W+Ui.P(8.f), Box.Y+Box.H*0.5f-Ui.P(40.f), Max(2.f,Ui.P(4.f)), Ui.P(80.f) );
				Ui.Scrim( Gauge, 0.4f );
				const FLOAT Pct = Clamp( (L->HoldDist-120.f)/580.f, 0.f, 1.f );
				Ui.Rect( FRpgRect(Gauge.X, Gauge.Y+Gauge.H*(1.f-Pct), Gauge.W, Gauge.H*Pct), D.Color );
			}
			const TCHAR* Keys[4] = { TEXT("Mouse"), TEXT("Wheel"), TEXT("LMB"), TEXT("Release RMB") };
			const TCHAR* Verbs[4] = { TEXT("Steer"), TEXT("Distance"), TEXT("Throw"), TEXT("Drop") };
			Ui.Prompt( C->ClipX*0.5f, PromptY, Keys, Verbs, 4 );
			return;
		}
		if( !SpellNeedsTarget(SelSpell) )
		{
			const TCHAR* Keys[1] = { TEXT("LMB") };
			const FString Verb = FString::Printf( TEXT("Cast %s"), D.Name );
			const TCHAR* Verbs[1] = { *Verb };
			Ui.Prompt( C->ClipX*0.5f, C->ClipY*0.62f, Keys, Verbs, 1 );
			return;
		}
		FRpgRect Box;
		if( AimTarget && IsInLevel(AimTarget) && ActorScreenBox(AimTarget, Box) )
		{
			Ui.Brackets( Box, Ui.P(12.f), T, D.Color );
			if( Icon )
				Ui.Icon( Icon, FRpgRect(Box.X+Box.W*0.5f-Ui.P(14.f), Box.Y-Ui.P(36.f), Ui.P(28.f), Ui.P(28.f)) );
			const FString Label = AimLabel( AimTarget );
			const TCHAR* Keys[1] = { TEXT("LMB") };
			const TCHAR* Verbs[1] = { TEXT("Cast") };
			Ui.Prompt( Box.X+Box.W*0.5f, Box.Y+Box.H+Ui.P(8.f), Keys, Verbs, 1, Label.Len() ? *Label : NULL );
		}
		else
		{
			// Idle reticle at the view centre (where the aim ray points).
			const FLOAT R = Ui.P(18.f);
			const FRpgRect Ret( C->ClipX*0.5f-R, C->ClipY*0.5f-R, R*2, R*2 );
			Ui.Brackets( FRpgRect(Ret.X+1, Ret.Y+1, Ret.W, Ret.H), Ui.P(10.f), T, FColor(0,0,0,255) );
			Ui.Brackets( Ret, Ui.P(10.f), T, Text1 );
			Ui.Rect( FRpgRect(C->ClipX*0.5f-T, C->ClipY*0.5f-T, T*2, T*2), Text1 );
			Ui.Text( Ui.FontCaption, C->ClipX*0.5f, Ret.Y+Ret.H+Ui.P(8.f), Dim, TEXT("No target"), RALIGN_Center );
		}
	}

	/*-------------------------------------------------------------------------
		Drawing: hotbar, Revelio markers, Arresto tint.
	-------------------------------------------------------------------------*/

	UBOOL WorldToScreen( const FVector& P, FLOAT& SX, FLOAT& SY )
	{
		FSceneNode* F = Ui.C ? Ui.C->Frame : NULL;
		if( !F )
			return 0;
		const FVector V = P - F->Coords.Origin;
		const FLOAT Z = V | F->Coords.ZAxis;
		if( Z<10.f )
			return 0;
		SX = F->FX2 + (V | F->Coords.XAxis)*F->Proj.Z/Z;
		SY = F->FY2 + (V | F->Coords.YAxis)*F->Proj.Z/Z;
		return 1;
	}

	void DrawSpellHud( APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		const FLOAT S = Ui.S;
		if( C->Frame )
		{
			CamPos = C->Frame->Coords.Origin;
			CamDir = C->Frame->Coords.ZAxis;
			bCamValid = 1;
		}

		// Arresto: blue wash over the world.
		if( ArrestoLeft>0.f && Ui.White )
			C->DrawTile( Ui.White, 0, 0, C->ClipX, C->ClipY, 0, 0, 1, 1, NULL, 1.f, FPlane(0.f,0.03f,0.12f,1.f), FPlane(0,0,0,0), PF_Translucent );

		// Revelio markers.
		if( RevealLeft>0.f )
		{
			static UTexture* Star = NULL;
			if( !Star )
				Star = LoadTex( TEXT("HP2_Menu.Icons.HP2StarCounter") );
			static const TCHAR* Labels[5] = { TEXT("Secret"), TEXT("Treasure"), TEXT("Card"), TEXT("Item"), TEXT("Page") };
			for( INT r=0; r<Reveals.Num(); r++ )
			{
				const FRevealEntry& E = Reveals(r);
				if( E.Actor && (!IsInLevel(E.Actor) || (E.Kind>=2 && (E.Actor->bHidden || GetStateName(E.Actor)==FName(TEXT("PickupProp"))))) )
					continue;
				FLOAT SX, SY;
				if( !WorldToScreen( E.Location + FVector(0,0,30), SX, SY ) )
					continue;
				SX = Clamp( SX, 20*S, C->ClipX-20*S );
				SY = Clamp( SY, 20*S, C->ClipY-20*S );
				const FLOAT Sz = 18*S;
				if( Star )
					Ui.Icon( Star, FRpgRect(SX-Sz*0.5f, SY-Sz*0.5f, Sz, Sz) );
				const INT Meters = appRound( (E.Location - Harry->Location).Size()/52.5f );
				Ui.Text( Ui.FontModern, SX, SY+Sz*0.6f, Spell2(SP2_Revelio).Color, *FString::Printf(TEXT("%s %im"), Labels[E.Kind], Meters), RALIGN_Center );
			}
		}

		// Hotbar, bottom centre; hidden while someone speaks (the subtitle sits there).
		if( IsTalking() )
			return;
		{
			const FColor Line( 46,56,96,255 ), GoldHi( 255,222,130,255 ), Text2( 182,186,206,255 );
			const FLOAT Slot = Ui.P(46.f), Gap = Ui.P(6.f), Div = Ui.P(12.f);
			UBOOL bCooling = 0;
			for( INT i=0; i<SP2_MAX; i++ )
				if( SpellCooldown[i]>0.f )
					bCooling = 1;
			// Quiet by default: the bar shows while you pick, aim or wait for
			// a spell, for a moment after casting, or on Tab.
			if( !(bAiming || SelFlash>0.f || bCooling || RpgClock-LastCastClock<4.f || HintTimer<90.f || bShowAllHud || ShieldLeft>0.f || ArrestoLeft>0.f) )
				return;
			// Only spells you know are shown; the bar grows as you learn.
			INT Shown[SP2_MAX], NumShown = 0;
			for( INT i=0; i<SP2_MAX; i++ )
				if( SpellUnlocked(i) )
					Shown[NumShown++] = i;
			const FLOAT W = Slot + (NumShown ? Div + NumShown*Slot + (NumShown-1)*Gap : 0.f);
			const FLOAT X0 = (C->ClipX-W)*0.5f, Y0 = C->ClipY - Slot - Ui.P(24.f);
			Ui.Glass( FRpgRect(X0-Ui.P(10.f), Y0-Ui.P(10.f), W+Ui.P(20.f), Slot+Ui.P(20.f)) );
			for( INT k=-1; k<NumShown; k++ )
			{
				const UBOOL bWand = k<0;
				const INT i = bWand ? -1 : Shown[k];
				const UBOOL bSel = bWand ? !bSelOurs : (bSelOurs && SelSpell==i);
				FLOAT X = bWand ? X0 : X0 + Slot + Div + k*(Slot+Gap);
				FRpgRect R( X, Y0 - (bSel ? Ui.P(6.f) : 0.f), Slot, Slot );
				if( bSel && Ui.GlowGold )
					Ui.Glow( Ui.GlowGold, FRpgRect(R.X-Slot*0.25f, R.Y-Slot*0.25f, Slot*1.5f, Slot*1.5f), 0.35f );
				Ui.Chamfer( R, Ui.P(3.f), bSel ? FColor(32,42,82,255) : FColor(12,16,34,255) );
				Ui.Border( R, bSel ? Max(1.f,Ui.P(2.f)) : Max(1.f,Ui.P(1.f)), bSel ? GoldHi : Line );
				if( bSel )
					Ui.Brackets( FRpgRect(R.X-Ui.P(4.f), R.Y-Ui.P(4.f), R.W+Ui.P(8.f), R.H+Ui.P(8.f)), Ui.P(8.f), Max(1.f,Ui.P(2.f)), GoldHi );
				UTexture* Icon = LoadTex( bWand ? TEXT("SpellShapes.Shapes.SPSflipendo") : Spell2(i).Icon );
				const FLOAT Cool = bWand ? 0.f : SpellCooldown[i];
				if( Icon )
					Ui.Icon( Icon, FRpgRect(R.X+(Slot-Ui.P(32.f))*0.5f, R.Y+(Slot-Ui.P(32.f))*0.5f, Ui.P(32.f), Ui.P(32.f)), Cool>0.f ? 0.45f : 1.f );
				// Colour strip along the bottom edge.
				Ui.Rect( FRpgRect(R.X+Ui.P(3.f), R.Y+R.H-Max(1.f,Ui.P(3.f)), R.W-Ui.P(6.f), Max(1.f,Ui.P(3.f))), bWand ? FColor(201,162,74,255) : Spell2(i).Color );
				if( Cool>0.f )
				{
					// Dark sweep from the top, seconds in the middle.
					const FLOAT Pct = Clamp( Cool/Spell2(i).Cooldown, 0.f, 1.f );
					Ui.Scrim( FRpgRect(R.X+1, R.Y+1, R.W-2, (R.H-2)*Pct), 0.35f );
					Ui.Text( Ui.FontNum, R.X+R.W*0.5f, R.Y+(R.H-Ui.TextHeight(Ui.FontNum))*0.5f, FColor(240,238,230,255), *FString::Printf(TEXT("%i"), appCeil(Cool)), RALIGN_Center );
				}
				// Key in a small corner keycap, outside the icon.
				if( !bWand )
				{
					const FString Key = FString::Printf( TEXT("%i"), (i+4)%10 );
					UFont* F = Ui.FontCaption;
					const FLOAT KS = Ui.P(18.f);
					const FRpgRect KR( R.X+R.W-KS+Ui.P(4.f), R.Y+R.H-KS+Ui.P(4.f), KS, KS );
					Ui.Chamfer( KR, Ui.P(2.f), Line );
					Ui.Chamfer( KR.Inset(Max(1.f,Ui.P(1.f))), Ui.P(2.f), FColor(20,27,54,255) );
					Ui.Text( F, KR.X+KS*0.5f, KR.Y+(KS-Ui.TextHeight(F))*0.5f, Text2, *Key, RALIGN_Center, 0 );
				}
				if( bSel && SelFlash>0.f )
					Ui.Text( Ui.FontModernBold, R.X+R.W*0.5f, R.Y-Ui.TextHeight(Ui.FontModernBold)-Ui.P(10.f), GoldHi, bWand ? TEXT("Wand") : Spell2(i).Name, RALIGN_Center );
			}
			// Controls hint: first minute and a half, or when picking a spell.
			if( !bAiming && (HintTimer<90.f || SelFlash>0.f) && NumShown && !NearNpc && ShieldLeft<=0.f && ArrestoLeft<=0.f )
			{
				const TCHAR* Keys[3] = { TEXT("Wheel"), TEXT("RMB"), TEXT("LMB") };
				const TCHAR* Verbs[3] = { TEXT("Pick"), TEXT("Aim"), TEXT("Cast") };
				Ui.Prompt( C->ClipX*0.5f, Y0-Ui.P(96.f), Keys, Verbs, 3 );
			}
			if( ShieldLeft>0.f || ArrestoLeft>0.f )
			{
				const UBOOL bShield = ShieldLeft>0.f;
				const FString T = FString::Printf( TEXT("%s %.1f"), bShield ? TEXT("Protego") : TEXT("Arresto Momentum"), bShield ? ShieldLeft : ArrestoLeft );
				Ui.Text( Ui.FontModernBold, C->ClipX*0.5f, Y0-Ui.P(46.f), Spell2(bShield ? SP2_Protego : SP2_Arresto).Color, *T, RALIGN_Center );
			}
		}
		if( bAiming )
			DrawAim( Harry );
	}
