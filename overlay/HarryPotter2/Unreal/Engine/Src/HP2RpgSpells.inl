/*=============================================================================
	HP2RpgSpells.inl: Extra spells of FHP2Rpg, cast from a hotbar (keys 4-9).
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	Accio, Revelio, Protego, Wingardium Leviosa, Glacius and Arresto Momentum
	are built from existing game pieces: HP1-era particle effects still in
	HPParticle.u, the game's own pickup/spell handlers, and per-actor tick
	control (an actor whose bTicked equals the level's Ticked flag skips its
	next tick).
=============================================================================*/

	enum ESpell2 { SP2_Accio, SP2_Revelio, SP2_Protego, SP2_Leviosa, SP2_Glacius, SP2_Arresto, SP2_MAX };

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
			{ TEXT("Revelio"), TEXT("Reveal secrets, chests and collectibles for 8 seconds."), 3, 20.f, TEXT("SpellShapes.Shapes.SPSlumos"), FColor(150,220,255,255) },
			{ TEXT("Protego"), TEXT("A shield that blocks all damage for 4 seconds."), 4, 25.f, TEXT("SpellShapes.Shapes.SPSexpelliarmus"), FColor(140,200,255,255) },
			{ TEXT("Wingardium Leviosa"), TEXT("Lift a creature, student or vase into the air, then drop it."), 6, 10.f, TEXT("SpellShapes.Shapes.SPSspongify"), FColor(220,240,160,255) },
			{ TEXT("Glacius"), TEXT("Freeze a creature solid for 5 seconds."), 8, 12.f, TEXT("SpellShapes.Shapes.SPSmimble"), FColor(170,230,255,255) },
			{ TEXT("Arresto Momentum"), TEXT("Slow down every enemy and their attacks for 6 seconds."), 10, 40.f, TEXT("SpellShapes.Shapes.SPSskurge"), FColor(200,170,255,255) },
		};
		return Defs[i];
	}

	struct FPullEntry   { AActor* Actor; BYTE OldPhysics; FLOAT Age; };
	struct FRevealEntry { AActor* Actor; FVector Location; INT Kind; };
	struct FLiftEntry   { AActor* Actor; BYTE OldRole; BYTE OldPhysics; FVector Base; FLOAT Age; UBOOL bDropped; UBOOL bProp; };
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

	UBOOL IsGrabbingHarry( AActor* A )
	{
		static const TCHAR* States[] = { TEXT("stateBiteHarry"), TEXT("stateBeingCarried"), TEXT("stateBeingThrown"), TEXT("AttachToHarry"), TEXT("stateWasHoldingByHarry") };
		const FName S = GetStateName( A );
		for( INT i=0; i<ARRAY_COUNT(States); i++ )
			if( S==FName(States[i],FNAME_Find) )
				return 1;
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

	void CastSpell2( UGameEngine* Engine, APlayerPawn* Harry, INT i )
	{
		const FSpell2Def& D = Spell2( i );
		if( !SpellUnlocked(i) )
		{
			AddFeed( FString::Printf(TEXT("%s is locked"), D.Name), FColor(180,180,200,255), TOAST_Info, *FString::Printf(TEXT("Learn it at level %i"), D.UnlockLevel) );
			return;
		}
		if( SpellCooldown[i]>0.f )
			return;
		UBOOL bCast = 0;
		switch( i )
		{
			case SP2_Accio:   bCast = CastAccio( Engine, Harry ); break;
			case SP2_Revelio: bCast = CastRevelio( Engine, Harry ); break;
			case SP2_Protego: bCast = CastProtego( Engine, Harry ); break;
			case SP2_Leviosa: bCast = CastLeviosa( Engine, Harry ); break;
			case SP2_Glacius: bCast = CastGlacius( Engine, Harry ); break;
			case SP2_Arresto: bCast = CastArresto( Engine, Harry ); break;
		}
		if( !bCast )
		{
			PlaySfx( TEXT("HPSounds.Magic_sfx.spell_dud") );
			return;
		}
		SpellCooldown[i] = D.Cooldown;
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
			if( (A->Location - Harry->Location).SizeSquared() > 700.f*700.f )
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
			else if( ClassChainIs(A,TEXT("HFlipendo")) && S==FName(TEXT("waitforspell"),FNAME_Find) )
				Kind = 1;
			else if( ClassChainIs(A,TEXT("WizardCardIcon")) && !A->Owner && S!=FName(TEXT("PickupProp"),FNAME_Find) )
				Kind = 2;
			else if( ClassChainIsAny(A,TEXT("ChallengeStar|PotionIngredients|WiggenWell")) && !A->Owner )
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

	UBOOL CastLeviosa( UGameEngine* Engine, APlayerPawn* Harry )
	{
		AActor* T = PickTarget( Engine, Harry, 1, 1, 650.f );
		if( !T )
		{
			AddFeed( TEXT("Wingardium Leviosa"), Spell2(SP2_Leviosa).Color, TOAST_Info, TEXT("No target in front of you") );
			return 0;
		}
		for( INT l=0; l<Lifts.Num(); l++ )
			if( Lifts(l).Actor==T )
				return 0;
		FLiftEntry* E = new(Lifts) FLiftEntry;
		E->Actor = T;
		E->OldRole = T->Role;
		E->OldPhysics = T->Physics;
		E->Base = T->Location;
		E->Age = 0.f;
		E->bDropped = 0;
		E->bProp = !T->IsA(APawn::StaticClass());
		T->Role = ROLE_DumbProxy;
		T->setPhysics( PHYS_None );
		T->Velocity = FVector( 0, 0, 0 );
		SpawnFx( Engine, TEXT("Levitate_wand"), WandTip(Harry), 0.8f );
		SpawnFx( Engine, TEXT("levitate_hit"), T->Location, 1.f );
		SpawnFx( Engine, TEXT("Levitate_hold"), T->Location, 3.5f );
		PlaySfx( TEXT("HPSounds.Magic_sfx.Spell_aim") );
		if( IsStudent(T) )
		{
			AddTrouble( 5, TEXT("Levitating a student") );
			AddRep( StudentHouse(T), -3 );
		}
		return 1;
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
			if( E.OldPhysics==PHYS_Flying )
				T->setPhysics( PHYS_Flying );
			const INT Vuln = GetByteProp( T, TEXT("eVulnerableToSpell"), 0 );
			if( E.bProp && Vuln )
				QueueRedispatch( T, Vuln );
			else if( CreatureType(T)>=0 )
				QueueRedispatch( T, 13 );
			Lifts.Remove( l );
		}
	}

	UBOOL CastGlacius( UGameEngine* Engine, APlayerPawn* Harry )
	{
		AActor* T = PickTarget( Engine, Harry, 0, 0, 700.f );
		if( !T )
		{
			AddFeed( TEXT("Glacius"), Spell2(SP2_Glacius).Color, TOAST_Info, TEXT("No creature in front of you") );
			return 0;
		}
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
				const UBOOL bEnemy = CreatureType(A)>=0 && !IsStudent(A);
				const UBOOL bHostileSpell = Spell && A->IsA(Spell) && !IsHarrysSpell(A,Harry);
				const UBOOL bHazard = ClassChainIsAny( A, TEXT("Fireball|FireballLarge|ThrownSpike|ThrownPoisonCloud|Bludger") );
				if( bEnemy || bHostileSpell || bHazard )
					A->bTicked = Flag;
			}
		}
	}

	void UpdateSpells( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
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
		UpdateLeviosa( Engine, DeltaSeconds );
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

		// Announce newly unlocked spells.
		INT Known = 0;
		for( INT i=0; i<SP2_MAX; i++ )
			if( SpellUnlocked(i) )
				Known = i+1;
		if( Known>SpellsKnownNotified )
		{
			if( SpellsKnownNotified>0 || Known>0 )
				for( INT i=SpellsKnownNotified; i<Known; i++ )
					AddFeed( FString::Printf(TEXT("New spell: %s"), Spell2(i).Name), Spell2(i).Color, TOAST_Reward, *FString::Printf(TEXT("Press %i to cast"), i+4) );
			SpellsKnownNotified = Known;
		}
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
				T->setPhysics( Lifts(l).OldPhysics==PHYS_Flying ? PHYS_Flying : PHYS_Falling );
			}
		Lifts.Empty();
		for( INT p=0; p<Pulls.Num(); p++ )
			if( IsInLevel(Pulls(p).Actor) )
				Pulls(p).Actor->setPhysics( Pulls(p).OldPhysics );
		Pulls.Empty();
		Reveals.Empty();
		ShieldFx = NULL;
		ArrestoLeft = RevealLeft = 0.f;
	}

	void ForgetSpells()
	{
		Freezes.Empty();
		Lifts.Empty();
		Pulls.Empty();
		Reveals.Empty();
		ShieldFx = NULL;
		ArrestoLeft = RevealLeft = ShieldLeft = 0.f;
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

		// Arresto: blue wash over the world.
		if( ArrestoLeft>0.f && Ui.White )
			C->DrawTile( Ui.White, 0, 0, C->ClipX, C->ClipY, 0, 0, 1, 1, NULL, 1.f, FPlane(0.f,0.03f,0.12f,1.f), FPlane(0,0,0,0), PF_Translucent );

		// Revelio markers.
		if( RevealLeft>0.f )
		{
			static UTexture* Star = NULL;
			if( !Star )
				Star = LoadTex( TEXT("HP2_Menu.Icons.HP2StarCounter") );
			static const TCHAR* Labels[4] = { TEXT("Secret"), TEXT("Treasure"), TEXT("Card"), TEXT("Item") };
			for( INT r=0; r<Reveals.Num(); r++ )
			{
				const FRevealEntry& E = Reveals(r);
				if( E.Actor && !IsInLevel(E.Actor) )
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

		// Hotbar, bottom centre.
		const FLOAT Slot = 40*S, Gap = 6*S;
		const FLOAT W = SP2_MAX*Slot + (SP2_MAX-1)*Gap;
		const FLOAT X0 = (C->ClipX-W)*0.5f, Y0 = C->ClipY - Slot - 12*S;
		for( INT i=0; i<SP2_MAX; i++ )
		{
			const FSpell2Def& D = Spell2( i );
			const FRpgRect R( X0 + i*(Slot+Gap), Y0, Slot, Slot );
			const UBOOL bOn = SpellUnlocked( i );
			Ui.Rect( R, FColor(12,16,36,255) );
			Ui.Border( R, Max(1.f,S), bOn ? (SpellCooldown[i]>0.f ? FColor(80,84,110,255) : D.Color) : FColor(50,52,70,255) );
			UTexture* Icon = LoadTex( D.Icon );
			if( Icon )
				Ui.Icon( Icon, R.Inset(5*S), bOn ? (SpellCooldown[i]>0.f ? 0.35f : 1.f) : 0.15f );
			if( bOn && SpellCooldown[i]>0.f )
			{
				const FLOAT Pct = SpellCooldown[i]/D.Cooldown;
				Ui.Rect( FRpgRect(R.X+2*S, R.Y+R.H-2*S-(R.H-4*S)*Pct, R.W-4*S, (R.H-4*S)*Pct), FColor(30,34,60,255) );
				Ui.Text( Ui.FontModernBold, R.X+R.W*0.5f, R.Y+(R.H-Ui.TextHeight(Ui.FontModernBold))*0.5f, FColor(230,230,240,255), *FString::Printf(TEXT("%i"), appCeil(SpellCooldown[i])), RALIGN_Center );
			}
			Ui.Text( Ui.FontModern, R.X+3*S, R.Y+1*S, bOn ? FColor(230,230,240,255) : RpgMuted, *FString::Printf(TEXT("%i"), i+4), RALIGN_Left );
			if( !bOn )
				Ui.Text( Ui.FontModern, R.X+R.W*0.5f, R.Y+R.H-Ui.TextHeight(Ui.FontModern)-1*S, RpgMuted, *FString::Printf(TEXT("Lv%i"), D.UnlockLevel), RALIGN_Center );
		}
		if( ShieldLeft>0.f )
			Ui.Text( Ui.FontModernBold, C->ClipX*0.5f, Y0-Ui.TextHeight(Ui.FontModernBold)-4*S, Spell2(SP2_Protego).Color, *FString::Printf(TEXT("Protego %.1f"), ShieldLeft), RALIGN_Center );
		else if( ArrestoLeft>0.f )
			Ui.Text( Ui.FontModernBold, C->ClipX*0.5f, Y0-Ui.TextHeight(Ui.FontModernBold)-4*S, Spell2(SP2_Arresto).Color, *FString::Printf(TEXT("Arresto Momentum %.1f"), ArrestoLeft), RALIGN_Center );
	}
