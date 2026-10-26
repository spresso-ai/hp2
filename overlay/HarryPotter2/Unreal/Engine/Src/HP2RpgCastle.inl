/*=============================================================================
	HP2RpgCastle.inl: A livelier castle, manual saving and leaving a
	challenge. Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	- Stand-ins: teachers and familiar faces who aren't in a hub at this
	  point of the story walk around it (plain animated meshes, no AI), so
	  quests and lessons can always be picked up. A character the story
	  already shows in the hub is never doubled, and stand-ins hide during
	  cutscenes.
	- Save: Harry's own SaveGame, from the book or with F5.
	- Leave challenge: fires the level's own walk-out exit back to the hub.
=============================================================================*/

	struct FStandInDef
	{
		const TCHAR* Map;       // hub map substring
		const TCHAR* Name;
		const TCHAR* Class;     // the retail class it stands in for (quests/lessons)
		const TCHAR* Mesh;
		const TCHAR* Move;      // walk (or fly) sequence
		FLOAT Speed;
		FLOAT Hover;            // ghosts float
		const TCHAR* Hello;
	};

	static const FStandInDef& StandInDef( INT i )
	{
		static const FStandInDef Defs[] =
		{
			{ TEXT("Entryhall"), TEXT("Professor McGonagall"), TEXT("ProfMcGonagall"), TEXT("HPModels.skProfMcGonagallMesh"), TEXT("Walk"), 70.f, 0.f, TEXT("Good day, Potter. Mind you're not late for class.") },
			{ TEXT("Entryhall"), TEXT("Professor Lockhart"), TEXT("ProfLockhart"), TEXT("HPModels.skProfLockhartMesh"), TEXT("Walk"), 80.f, 0.f, TEXT("Harry! Need an autograph? No, no - of course you'd rather have one of mine.") },
			{ TEXT("Entryhall"), TEXT("Nearly Headless Nick"), TEXT("NHNick"), TEXT("HPModels.skNHNickMesh"), TEXT("Fly"), 50.f, 10.f, TEXT("Lovely evening, Harry. Do mind the draught - it goes straight through me.") },
			{ TEXT("Entryhall"), TEXT("Percy Weasley"), TEXT("Percy"), TEXT("HPModels.skPercyMesh"), TEXT("Walk"), 90.f, 0.f, TEXT("Prefect on patrol. Keep your nose clean, Harry.") },
			{ TEXT("Grandstaircase"), TEXT("Professor Flitwick"), TEXT("ProfFlitwick"), TEXT("HPModels.skProfFlitwickMesh"), TEXT("Walk"), 55.f, 0.f, TEXT("Ah, Mr Potter! Swish and flick, remember - swish and flick!") },
			{ TEXT("Grandstaircase"), TEXT("Peeves"), TEXT("Peeves"), TEXT("HPModels.skpeevesMesh"), TEXT("Flying"), 70.f, 75.f, TEXT("Potty wee Potter! Wandering about, are we?") },
			{ TEXT("Grounds_hub"), TEXT("Professor Sprout"), TEXT("ProfSprout"), TEXT("HPModels.skProfSproutMesh"), TEXT("Walk"), 65.f, 0.f, TEXT("Lovely weather for the Mandrakes, Harry dear.") },
			{ TEXT("Grounds_hub"), TEXT("Oliver Wood"), TEXT("OliverWood"), TEXT("HPModels.skOliverWoodMesh"), TEXT("Walk"), 95.f, 0.f, TEXT("Potter! Practice at dawn. Don't be late.") },
			{ TEXT("Grounds_hub"), TEXT("Hagrid"), TEXT("Hagrid"), TEXT("HPModels.skHagridMesh"), TEXT("Walk"), 70.f, 0.f, TEXT("'Ello, Harry! Fancy a rock cake later?") },
			{ TEXT("Entryhall"), TEXT("Professor Snape"), TEXT("ProfSnape"), TEXT("HPModels.skProfSnapeMesh"), TEXT("Walk"), 60.f, 0.f, TEXT("Potter. Shouldn't you be somewhere... less in my way?") },
			// --- Appended (v6.2): the night grounds ---------------------------
			{ TEXT("Grounds_Night"), TEXT("Hagrid"), TEXT("Hagrid"), TEXT("HPModels.skHagridMesh"), TEXT("Walk"), 60.f, 0.f, TEXT("Harry! Out late, aren't yeh? Mind Filch doesn't catch yeh.") },
			{ TEXT("Grounds_Night"), TEXT("Argus Filch"), TEXT("filch"), TEXT("HPModels.skFilchMesh"), TEXT("Walk"), 55.f, 0.f, TEXT("Out of bed at this hour? Wait till the Headmaster hears about this...") },
			{ TEXT("Grounds_Night"), TEXT("Nearly Headless Nick"), TEXT("NHNick"), TEXT("HPModels.skNHNickMesh"), TEXT("Fly"), 45.f, 10.f, TEXT("A lovely night for a stroll, Harry. Do mind the Willow.") },
		};
		return Defs[ Clamp(i, 0, (INT)ARRAY_COUNT(Defs)-1) ];
	}
	enum { NUM_STANDINS = 13 };

	// Retail voice lines each stand-in may call out (AllDialog ids).
	static const TCHAR* StandInLines( INT i )
	{
		static const TCHAR* Lines[NUM_STANDINS] =
		{
			TEXT("PC_MCG_BUMPLINES_593|PC_MCG_BUMPLINES_589|PC_MCG_BUMPLINES_655"),
			TEXT("PC_GIL_BUMPLINES_998|PC_GIL_BUMPLINES_995|PC_GIL_BUMPLINES_1001|PC_GIL_BUMPLINES_997"),
			TEXT("PC_Nhn_GoldExplain_01|PC_Nhn_GoldExplain_03|PC_Nhn_Chal2Skurge_18"),
			TEXT("PC_PCY_BUMPLINES_336|PC_PCY_BUMPLINES_339|PC_PCY_BUMPLINES_555|PC_PCY_BUMPLINES_557|PC_PCY_BUMPLINES_653"),
			TEXT("PC_Flt_Chal2Skurge_49|PC_Flt_CharmSkurgTeach_06|PC_Flt_CharmSkurgTeach_05"),
			TEXT("PC_Pvs_Chal2Skurge_54|PC_Pvs_Chal2Skurge_51|PC_Pvs_Chal2Skurge_53|PC_Pvs_Chal2Skurge_24"),
			TEXT("PC_Spr_HerbDiffTeach_06|PC_Spr_Chal3Diff_22|PC_Spr_Chal3Diff_07"),
			TEXT("PC_Owd_GoToQuid_00|PC_Owd_GoToQuid2_04|PC_Owd_QuidTut1_27"),
			TEXT("PC_HDW_BUMPLINES_349|PC_HDW_BUMPLINES_352|PC_HDW_BUMPLINES_560|PC_HDW_BUMPLINES_355|PC_HDW_BUMPLINES_971"),
			TEXT("PC_SNP_BUMPLINES_973|PC_SNP_BUMPLINES_974|PC_SNP_BUMPLINES_1004|PC_SNP_BUMPLINES_967"),
			TEXT("PC_HDW_BUMPLINES_349|PC_HDW_BUMPLINES_354|PC_HDW_BUMPLINES_646"),
			TEXT("PC_FIL_BUMPLINES_977|PC_FIL_BUMPLINES_979"),
			TEXT("PC_Nhn_GoldExplain_01|PC_Nhn_GoldExplain_03|PC_Nhn_Chal2Skurge_18"),
		};
		return Lines[ Clamp(i, 0, (INT)NUM_STANDINS-1) ];
	}

	struct FStandIn { AActor* Actor; INT Def; FVector Target; FLOAT Wait; FVector Anchor; UBOOL bGreeted; INT LineIndex; FLOAT NextTalk; };
	TArray<FStandIn> StandIns;
	UBOOL bStandInsSpawned;
	FLOAT StandInDelay;
	FLOAT StandInCheck;

	// The retail character this stand-in replaces is out in the open now.
	UBOOL StoryCopyAbout( UGameEngine* Engine, const FStandInDef& Def )
	{
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden || IsHolding(A) || !A->IsA(APawn::StaticClass()) || A->Tag==FName(TEXT("HP2RpgActor")) )
				continue;
			const UBOOL bSame = ClassChainIs(A,Def.Class) || (appStricmp(Def.Class,TEXT("ProfSnape"))==0 && ClassChainIs(A,TEXT("Snape")));
			if( bSame && IsOutAndAbout(Engine,A) )
				return 1;
		}
		return 0;
	}

	static UBOOL ListHas( const TCHAR* List, const TCHAR* Name )
	{
		TCHAR Token[64];
		INT N = 0;
		for( const TCHAR* P=List; ; P++ )
		{
			if( *P=='|' || *P==0 )
			{
				Token[N] = 0;
				if( N && appStricmp(Token,Name)==0 )
					return 1;
				N = 0;
				if( !*P )
					return 0;
			}
			else if( N<63 )
				Token[N++] = *P;
		}
	}

	// Gives a spawned character its animation set: the mesh's default, its
	// own "...Anims" package, or the shared student set. Without one it
	// would walk around in its bind pose.
	static void LinkCharacterAnims( AActor* A, const TCHAR* MeshPath )
	{
		if( A->Mesh && A->Mesh->IsA(USkeletalMesh::StaticClass()) )
			A->SkelAnim = ((USkeletalMesh*)A->Mesh)->DefaultAnimation;
		if( A->SkelAnim && A->SkelAnim->AnimSeqs.Num() )
			return;
		FString Path = MeshPath;
		const INT Cut = Path.InStr( TEXT("Mesh"), 1 );
		if( Cut>0 )
		{
			Path = Path.Left( Cut ) + TEXT("Anims");
			A->SkelAnim = (UAnimation*)FRpgUi::LoadRooted( UAnimation::StaticClass(), *Path );
		}
		if( !A->SkelAnim || !A->SkelAnim->AnimSeqs.Num() )
			A->SkelAnim = (UAnimation*)FRpgUi::LoadRooted( UAnimation::StaticClass(), TEXT("HPModels.skGenMaleAnims") );
		RpgLog( TEXT("anims for %s: %s"), MeshPath, A->SkelAnim ? A->SkelAnim->GetName() : TEXT("none") );
	}

	FStandIn* FindStandIn( AActor* A )
	{
		for( INT i=0; i<StandIns.Num(); i++ )
			if( StandIns(i).Actor==A && A )
				return &StandIns(i);
		return NULL;
	}

	// Class match that also understands stand-ins.
	UBOOL NpcIs( AActor* A, const TCHAR* List )
	{
		if( !A || !List )
			return 0;
		FStandIn* S = FindStandIn( A );
		if( S )
			return ListHas( List, StandInDef(S->Def).Class );
		const INT m = MateIndex( A );
		if( m>=0 )
			return MateDef(m).Class && ListHas( List, MateDef(m).Class );
		return ClassChainIsAny( A, List );
	}

	void SpawnStandIns( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bStandInsSpawned = 1;
		const TCHAR* Map = *Engine->GLevel->URL.Map;
		if( !MapHas(Map,TEXT("_hub")) && !MapHas(Map,TEXT("Grounds_Night")) )
			return;
		for( INT d=0; d<NUM_STANDINS; d++ )
		{
			const FStandInDef& Def = StandInDef( d );
			if( !MapHas(Map,Def.Map) )
				continue;
			// The story already shows this character here: no double.
			// Snape has two retail classes; one parked out of reach for a
			// cutscene doesn't count, then the stand-in walks instead.
			const UBOOL bPresent = StoryCopyAbout( Engine, Def );
			if( bPresent )
				continue;
			FVector Where;
			if( !PickSpot( Engine, (DWORD)(State.Day*13 + d*7 + 101), Where ) )
				continue;
			Where.Z -= 22.f;
			AActor* A = SpawnPickup( Engine, Def.Mesh, Where + FVector(0,0,Def.Hover), 1.f );
			if( !A )
				continue;
			A->AmbientGlow = 0;
			LinkCharacterAnims( A, Def.Mesh );
			FStandIn* S = new(StandIns) FStandIn;
			S->Actor = A;
			S->Def = d;
			S->Anchor = Where;
			S->Target = Where;
			S->Wait = 2.f + RpgRand(4);
			S->bGreeted = 0;
			S->LineIndex = State.Day + d;
			S->NextTalk = 0.f;
		}
		if( StandIns.Num() )
			RpgLog( TEXT("castle: %i stand-ins in %s"), StandIns.Num(), Map );
	}

	// Next stop: a path node near the anchor that can be walked to in a line.
	FVector NextStop( UGameEngine* Engine, const FStandIn& S )
	{
		TArray<FVector> Spots;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* N = Engine->GLevel->Actors(i);
			if( !IsWalkPoint(N) || (N->Location-S.Anchor).SizeSquared() > 800.f*800.f || (N->Location-S.Actor->Location).SizeSquared() < 120.f*120.f )
				continue;
			if( Engine->GLevel->Model->FastLineCheck( N->Location, S.Actor->Location + FVector(0,0,40.f) ) )
				Spots.AddItem( N->Location );
		}
		return Spots.Num() ? Spots( RpgRand(Spots.Num()) ) : S.Anchor;
	}

	void UpdateStandIns( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( !bStandInsSpawned )
		{
			StandInDelay -= DeltaSeconds;
			if( StandInDelay<=0.f )
				SpawnStandIns( Engine, Harry );
			return;
		}
		// Now and then: step aside if the story brings its own copy out.
		StandInCheck -= DeltaSeconds;
		const UBOOL bCheck = StandInCheck<=0.f;
		if( bCheck )
			StandInCheck = 3.f;
		for( INT i=StandIns.Num()-1; i>=0; i-- )
		{
			FStandIn& S = StandIns(i);
			AActor* A = S.Actor;
			if( !IsInLevel(A) )
			{
				StandIns.Remove( i );
				continue;
			}
			if( bCheck && StoryCopyAbout( Engine, StandInDef(S.Def) ) )
			{
				DestroyActorSafe( Engine, S.Actor );
				StandIns.Remove( i );
				continue;
			}
			const FStandInDef& Def = StandInDef( S.Def );
			// Out of the way while the story plays.
			A->bHidden = bWasCutscene;
			if( bWasCutscene )
				continue;
			FName Idle = FindSeq( A->SkelAnim, TEXT("Idle") );
			const FName Move = FindSeq( A->SkelAnim, Def.Move );
			// Stop to chat while Harry stands close.
			const UBOOL bNearHarry = (A->Location-Harry->Location).SizeSquared() < 160.f*160.f;
			// Call out once per visit when Harry comes by.
			if( !S.bGreeted && (A->Location-Harry->Location).SizeSquared() < 320.f*320.f
				&& RpgClock>=MateVoiceFree && IsFreeGameplay(Engine,Harry) && !IsTalking() )
			{
				S.bGreeted = 1;
				const FLOAT Secs = SpeakLine( A, StandInLines(S.Def), S.LineIndex );
				S.NextTalk = RpgClock + Secs + 6.f;
				S.Wait = Max( S.Wait, Secs );
			}
			// No walk cycle in its set: it stays where it is rather than slide.
			if( Move==NAME_None )
				S.Wait = Max( S.Wait, 1.f );
			if( S.Wait>0.f || bNearHarry )
			{
				S.Wait = Max( 0.f, S.Wait-DeltaSeconds );
				if( Idle!=NAME_None && (A->AnimSequence!=Idle || !A->bAnimLoop) )
					A->PlayAnim( Idle, true, 1.f, 0.25f );
				if( bNearHarry )
				{
					FVector Face = Harry->Location - A->Location;
					Face.Z = 0.f;
					const INT Diff = ((Face.Rotation().Yaw - A->Rotation.Yaw + 32768) & 65535) - 32768;
					A->Rotation = FRotator( 0, A->Rotation.Yaw + appRound(Diff*Min(1.f,DeltaSeconds*5.f)), 0 );
				}
				if( S.Wait<=0.f && !bNearHarry )
					S.Target = NextStop( Engine, S );
				continue;
			}
			FVector D = S.Target - A->Location;
			D.Z = 0.f;
			const FLOAT Dist = D.Size();
			if( Dist<30.f )
			{
				S.Wait = 4.f + RpgRand(8);
				continue;
			}
			FVector Dir = D/Dist;
			FVector New = A->Location + Dir*Min( Dist, Def.Speed*DeltaSeconds );
			FCheckResult Hit;
			const FVector Top = New + FVector( 0, 0, 60.f );
			if( !Engine->GLevel->SingleLineCheck( Hit, A, Top - FVector(0,0,300.f), Top, TRACE_VisBlocking, FVector(0,0,0) ) )
				New.Z = Hit.Location.Z + Def.Hover;
			Engine->GLevel->FarMoveActor( A, New, 0, 1 );
			const INT Diff = ((Dir.Rotation().Yaw - A->Rotation.Yaw + 32768) & 65535) - 32768;
			A->Rotation = FRotator( 0, A->Rotation.Yaw + appRound(Diff*Min(1.f,DeltaSeconds*6.f)), 0 );
			if( Move!=NAME_None && (A->AnimSequence!=Move || !A->bAnimLoop) )
				A->PlayAnim( Move, true, 1.f, 0.25f );
		}
	}

	void EndStandIns( UGameEngine* Engine )
	{
		for( INT i=0; i<StandIns.Num(); i++ )
			DestroyActorSafe( Engine, StandIns(i).Actor );
		StandIns.Empty();
		bStandInsSpawned = 0;
		StandInDelay = 1.f;
	}

	void ForgetStandIns()
	{
		StandIns.Empty();
		bStandInsSpawned = 0;
		StandInDelay = 3.f;
	}

	// The stand-in Harry is closest to (for E), within Range.
	AActor* NearestStandIn( APlayerPawn* Harry, FLOAT Range )
	{
		AActor* Best = NULL;
		FLOAT BestD = Range*Range;
		for( INT i=0; i<StandIns.Num(); i++ )
		{
			AActor* A = StandIns(i).Actor;
			if( !A || A->bHidden || A->bDeleteMe )
				continue;
			const FLOAT D = (A->Location-Harry->Location).SizeSquared();
			if( D<BestD )
			{
				BestD = D;
				Best = A;
			}
		}
		return Best;
	}

	/*-------------------------------------------------------------------------
		Saving and leaving a challenge.
	-------------------------------------------------------------------------*/

	UBOOL CanSaveNow( UGameEngine* Engine, APlayerPawn* Harry )
	{
		return IsFreeGameplay( Engine, Harry ) && !bLevelTransition;
	}

	void ManualSave( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !Harry || !CanSaveNow(Engine,Harry) )
		{
			AddFeed( TEXT("Can't save right now"), FColor(232,92,80,255), TOAST_Info, TEXT("Try again when nothing is happening") );
			return;
		}
		FRpgArg Slot = FRpgArg::I( 0 );
		if( CallScript( Harry, TEXT("SaveGame"), &Slot, 1 ) )
		{
			AddFeed( TEXT("Game saved"), FColor(120,220,140,255), TOAST_Info, *FString::Printf(TEXT("%s, school day %i"), *Engine->GLevel->URL.Map, State.Day) );
			RpgLog( TEXT("manual save") );
		}
	}

	static FString PropString( UObject* Obj, const TCHAR* Name )
	{
		if( !Obj )
			return FString();
		if( UStrProperty* P = FindField<UStrProperty>( Obj->GetClass(), Name ) )
			return *(FString*)((BYTE*)Obj + P->Offset);
		if( UNameProperty* P = FindField<UNameProperty>( Obj->GetClass(), Name ) )
			return *(*(FName*)((BYTE*)Obj + P->Offset));
		return FString();
	}

	// The level's own walk-out exit to a hub (Tag "Backup" preferred).
	AActor* HubExit( UGameEngine* Engine )
	{
		if( !Engine->GLevel || MapHas(*Engine->GLevel->URL.Map,TEXT("_hub")) )
			return NULL;
		AActor* Best = NULL;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || !ClassChainIs(A,TEXT("TriggerChangeLevel")) )
				continue;
			const FString To = PropString( A, TEXT("NewMapName") ).Caps();
			if( !MapHas(*To,TEXT("_HUB")) )
				continue;
			if( !Best || A->Tag==FName(TEXT("Backup")) )
				Best = A;
		}
		return Best;
	}

	void LeaveChallenge( UGameEngine* Engine, APlayerPawn* Harry )
	{
		AActor* Exit = HubExit( Engine );
		if( !Exit || !Harry )
			return;
		RpgLog( TEXT("leaving challenge through %s to %s"), Exit->GetName(), *PropString(Exit,TEXT("NewMapName")) );
		CloseMenu( Engine );
		if( !CallScript( Exit, TEXT("ProcessTrigger"), NULL, 0 ) )
			Exit->eventTrigger( Harry, Harry );
	}
