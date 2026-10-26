/*=============================================================================
	HP2RpgRhythm.inl: The school rhythm. Included inside the FHP2Rpg class
	body in HP2Rpg.cpp.

	A school day runs Lesson -> Free time -> Evening. The phase only moves
	on when Harry enters a hub (one step per visit), so nobody ever
	appears or vanishes in front of him, and it is derived from saved
	state only: loading a save never moves it on.

	- Lesson: a lesson was not taken yet today.
	- Free time: after today's lesson, a house task, a finished challenge
	  or 8 minutes of play.
	- Evening: after 15 minutes of play, or 5 once a house task is done.

	In Entryhall_hub, Ron, Hermione, Neville (a generic Gryffindor boy;
	the game has no Neville) and Draco stand at fixed spots for the phase
	and play small activity loops. They speak retail voice lines with
	subtitles through Actor.DeliverLocalizedDialog. A character is left
	out while the story uses the retail copy or while it is Harry's
	friend follower, and all of them hide during cutscenes.
=============================================================================*/

	enum ERpgPhase { PHASE_Lesson, PHASE_Free, PHASE_Evening, PHASE_MAX };
	enum ERpgMate { MATE_Ron, MATE_Hermione, MATE_Neville, MATE_Draco, MATE_MAX };

	struct FMateDef
	{
		const TCHAR* Name;
		const TCHAR* Class;     // retail class (story copy, quests); NULL = none
		const TCHAR* Mesh;
		const TCHAR* Skin0;
		const TCHAR* Skin1;
	};

	static const FMateDef& MateDef( INT m )
	{
		static const FMateDef Defs[MATE_MAX] =
		{
			{ TEXT("Ron Weasley"), TEXT("Ron"), TEXT("HPModels.skronMesh"), NULL, NULL },
			{ TEXT("Hermione Granger"), TEXT("Hermione"), TEXT("HPModels.skhermioneMesh"), NULL, NULL },
			{ TEXT("Neville Longbottom"), NULL, TEXT("HPModels.skhp2_genmale1Mesh"), TEXT("HPModels.Skins.skhp2_genmale1_0Tex0"), TEXT("HPModels.Skins.skhp2_genmale1_0Tex1") },
			{ TEXT("Draco Malfoy"), TEXT("Draco"), TEXT("HPModels.skDracoMesh"), NULL, NULL },
		};
		return Defs[ Clamp(m, 0, (INT)MATE_MAX-1) ];
	}

	// Where a character is in a phase, what it does there and what it says.
	struct FMateSpot
	{
		const TCHAR* Room;
		FLOAT X, Y, Z;          // stand point (pawn centre height; traced down)
		FLOAT LookX, LookY;     // faces this point
		const TCHAR* Acts;      // one-shot animations, '|' separated
		const TCHAR* Lines;     // AllDialog ids, '|' separated
	};

	static const FMateSpot& MateSpot( INT m, INT Phase )
	{
		static const FMateSpot Spots[MATE_MAX][PHASE_MAX] =
		{
			{	// Ron
				{ TEXT("Potions classroom"), -4606.f, -352.f, -471.f, -4411.f, 341.f, TEXT("fidget_2|Negative|fidget_4"), TEXT("PC_Ron_GoToDADA_00|PC_Ron_GoToDADA2_05") },
				{ TEXT("Common room"), 2196.f, -1810.f, 355.f, 2155.f, -1646.f, TEXT("talk_bothhands|clap|fidget_1"), TEXT("PC_Ron_Parselmouth_30|PC_Ron_BitOfGoyle_32") },
				{ TEXT("Common room"), 2196.f, -1810.f, 355.f, 2286.f, -1672.f, TEXT("fidget_3|talk_flustered|vendor_idle"), TEXT("PC_Ron_WhompFree_06|PC_Ron_Password_46|PC_Ron_GoToGCR_00") },
			},
			{	// Hermione
				{ TEXT("Potions classroom"), -4419.f, -352.f, -471.f, -4411.f, 341.f, TEXT("talk_smugloop|MidSearch|Positive"), TEXT("PC_Her_GoToPotions_00|PC_Her_GoToPotions2_01") },
				{ TEXT("Potions classroom"), -5485.f, 128.f, -490.f, -5540.f, 150.f, TEXT("ReachHigh|MidSearch|fidget_1"), TEXT("PC_HER_BUMPLINES_1012|PC_HER_BUMPLINES_1013") },
				{ TEXT("Common room"), 2286.f, -1672.f, 355.f, 2336.f, -1630.f, TEXT("talk_dreamy|SitSearch|fidget_2"), TEXT("PC_HER_BUMPLINES_600|PC_Her_Parselmouth_31") },
			},
			{	// Neville
				{ TEXT("Potions classroom"), -4689.f, -352.f, -471.f, -4411.f, 341.f, TEXT("SitSearch|scared|fidget_1"), TEXT("PC_GMG_BUMPLINES_630|PC_OMG_BUMPLINES_285") },
				{ TEXT("Common room"), 2459.f, -1640.f, 373.f, 2336.f, -1630.f, TEXT("MidSearch|talk_dontKnow|fidget_2"), TEXT("PC_GMG_BUMPLINES_224|PC_GMG_BUMPLINES_901") },
				{ TEXT("Common room"), 2512.f, -1802.f, 355.f, 2336.f, -1630.f, TEXT("SitSearch|talk_remember|fidget_4"), TEXT("PC_OMG_BUMPLINES_285|PC_GMG_BUMPLINES_901") },
			},
			{	// Draco
				{ TEXT("Potions classroom"), -4793.f, -476.f, -471.f, -4411.f, 341.f, TEXT("taunt_1|vendor_idle2|disgust"), TEXT("PC_Drc_DuelIntro_24|PC_Drc_Dueling_01") },
				{ TEXT("House-point hall"), -1872.f, -1088.f, -42.f, -2113.f, -1092.f, TEXT("taunt_2|talk_rhand|vendor_idle2"), TEXT("PC_Drc_HPCeremony_22|PC_Drc_QuidTaunts_11") },
				{ TEXT("Dungeon corridor"), -2508.f, -997.f, -440.f, -2508.f, -1087.f, TEXT("vendor_idle2|disgust|taunt_1"), TEXT("PC_Drc_DuelIntro_24|PC_Drc_Dueling_01") },
			},
		};
		return Spots[ Clamp(m, 0, (INT)MATE_MAX-1) ][ Clamp(Phase, 0, (INT)PHASE_MAX-1) ];
	}

	// Neville's Christmas lines replace his usual ones over the holidays.
	static const TCHAR* ChristmasLines( INT m )
	{
		return m==MATE_Neville ? TEXT("PC_GMG_BUMPLINES_898|PC_GMG_BUMPLINES_944") : NULL;
	}

	struct FSchoolmate
	{
		AActor* Actor;
		AActor* StoryCopy;      // the retail actor parked in the people room
		FLOAT NextAct;
		INT ActIndex;
		INT LineIndex;
		UBOOL bGreeted;
		UBOOL bSuppressed;      // the story took the character this visit
		FLOAT NextTalk;
	};
	FSchoolmate Mates[MATE_MAX];
	UBOOL bMatesSpawned;
	FLOAT MateDelay;
	FLOAT MateCheck;
	FLOAT MateVoiceFree;        // RpgClock when the next spoken line may start

	static const TCHAR* PhaseName( INT Phase )
	{
		return Phase==PHASE_Evening ? TEXT("Evening") : Phase==PHASE_Free ? TEXT("Free time") : TEXT("Lesson");
	}

	static INT GameStateNum( APlayerPawn* Harry )
	{
		return Harry && Harry->CurrentGameState.Len()>=3 ? appAtoi( *Harry->CurrentGameState.Right(3) ) : -1;
	}

	static UBOOL IsChristmas( INT GS )
	{
		return GS>=100 && GS<130;
	}

	// Pick token N (wrapping) from a '|' list.
	static UBOOL ListToken( const TCHAR* List, INT N, TCHAR* Out, INT OutLen )
	{
		INT Count = 0;
		for( const TCHAR* P=List; P && *P; P++ )
			if( *P=='|' )
				Count++;
		if( !List || !*List )
			return 0;
		N %= Count+1;
		INT k = 0, L = 0;
		for( const TCHAR* P=List; ; P++ )
		{
			if( *P=='|' || *P==0 )
			{
				if( k==N )
				{
					Out[L] = 0;
					return L>0;
				}
				k++;
				L = 0;
				if( !*P )
					return 0;
			}
			else if( k==N && L<OutLen-1 )
				Out[L++] = *P;
		}
	}

	// Steps the phase on entering a hub. Not on a hub loaded from a save,
	// so reloading never moves the day on.
	void StepRhythm( const TCHAR* Map, UBOOL bFromSave )
	{
		if( !MapHas(Map,TEXT("_hub")) )
			return;
		if( State.RhythmDay!=State.Day )
		{
			State.RhythmDay = State.Day;
			State.RhythmPhase = PHASE_Lesson;
			return;
		}
		if( bFromSave )
			return;
		const INT Old = State.RhythmPhase;
		if( State.RhythmPhase==PHASE_Lesson
			&& (State.LessonDay==State.Day || State.DayTaskDone || State.RhythmEventDay==State.Day || State.DayPlaySeconds>=480) )
			State.RhythmPhase = PHASE_Free;
		else if( State.RhythmPhase==PHASE_Free
			&& (State.DayPlaySeconds>=900 || (State.DayTaskDone && State.DayPlaySeconds>=300)) )
			State.RhythmPhase = PHASE_Evening;
		State.RhythmPhase = Clamp( State.RhythmPhase, (INT)PHASE_Lesson, (INT)PHASE_Evening );
		if( State.RhythmPhase!=Old )
			RpgLog( TEXT("rhythm: day %i is now %s"), State.Day, PhaseName(State.RhythmPhase) );
	}

	// Who is free to be shown at this point of the story.
	static UBOOL MateInStory( INT m, INT GS )
	{
		if( GS<20 || GS>=170 )
			return 0;                       // arrival night, the Chamber, the ending
		if( m==MATE_Hermione && ((GS>=120 && GS<130) || GS>=150) )
			return 0;                       // Polyjuice mishap, then petrified
		return 1;
	}

	UBOOL IsFriendMate( INT m )
	{
		const INT Id = State.Equip[RSLOT_Friend];
		if( Id<0 || Id>=GNumCosmetics || GCosmetics[Id].Slot!=RSLOT_Friend || !GCosmetics[Id].Mesh )
			return 0;
		return appStricmp( GCosmetics[Id].Mesh, MateDef(m).Mesh )==0;
	}

	// The story uses its own copy: it is visible and away from the people
	// room, or held by a cutscene.
	static UBOOL StoryCopyBusy( AActor* A )
	{
		if( !A || A->bDeleteMe || A->bHidden )
			return 0;
		if( A->CutNotifyActor )
			return 1;
		return (A->Location - FVector(-1050.f,410.f,-291.f)).SizeSquared() > 300.f*300.f;
	}

	FSchoolmate* FindSchoolmate( AActor* A )
	{
		for( INT m=0; A && m<MATE_MAX; m++ )
			if( Mates[m].Actor==A )
				return &Mates[m];
		return NULL;
	}

	INT MateIndex( AActor* A )
	{
		FSchoolmate* S = FindSchoolmate( A );
		return S ? (INT)(S-Mates) : -1;
	}

	void SpawnSchoolmates( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bMatesSpawned = 1;
		if( !MapHas(*Engine->GLevel->URL.Map,TEXT("Entryhall_hub")) )
			return;
		const INT GS = GameStateNum( Harry );
		const INT Phase = Clamp( State.RhythmPhase, (INT)PHASE_Lesson, (INT)PHASE_Evening );
		// The one level scan: find the story copies once per visit.
		AActor* Story[MATE_MAX] = { NULL, NULL, NULL, NULL };
		FName StoryClass[MATE_MAX];
		for( INT m=0; m<MATE_MAX; m++ )
			StoryClass[m] = MateDef(m).Class ? FName( MateDef(m).Class, FNAME_Find ) : NAME_None;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || !A->IsA(APawn::StaticClass()) )
				continue;
			for( INT m=0; m<MATE_MAX; m++ )
				if( StoryClass[m]!=NAME_None && !Story[m] && A->GetClass()->GetFName()==StoryClass[m] )
					Story[m] = A;
		}
		INT Count = 0;
		for( INT m=0; m<MATE_MAX; m++ )
		{
			FSchoolmate& S = Mates[m];
			appMemzero( &S, sizeof(S) );
			S.StoryCopy = Story[m];
			if( !MateInStory(m,GS) || IsFriendMate(m) || StoryCopyBusy(Story[m]) )
				continue;
			const FMateSpot& Spot = MateSpot( m, Phase );
			FVector Where( Spot.X, Spot.Y, Spot.Z );
			if( (Where-Harry->Location).SizeSquared() < 120.f*120.f )
				continue;                   // never on top of Harry
			FCheckResult Hit;
			const FVector Top = Where + FVector( 0, 0, 40.f );
			if( !Engine->GLevel->SingleLineCheck( Hit, NULL, Top - FVector(0,0,300.f), Top, TRACE_VisBlocking, FVector(0,0,0) ) )
				Where = Hit.Location;
			else
				Where.Z -= 40.f;
			AActor* A = SpawnPickup( Engine, MateDef(m).Mesh, Where, 1.f );
			if( !A )
				continue;
			A->AmbientGlow = 0;
			LinkCharacterAnims( A, MateDef(m).Mesh );
			if( MateDef(m).Skin0 )
			{
				A->MultiSkins[0] = LoadTex( MateDef(m).Skin0 );
				A->MultiSkins[1] = LoadTex( MateDef(m).Skin1 );
			}
			FVector Face = FVector( Spot.LookX, Spot.LookY, Where.Z ) - Where;
			Face.Z = 0.f;
			A->Rotation = FRotator( 0, Face.Rotation().Yaw, 0 );
			S.Actor = A;
			S.NextAct = 3.f + RpgRand(5);
			S.ActIndex = RpgRand( 3 );
			S.LineIndex = State.Day + m;
			Count++;
		}
		RpgLog( TEXT("rhythm: %s, state %i, %i schoolmates"), PhaseName(Phase), GS, Count );
	}

	// Speaks the next retail line of a '|' list on Speaker, with its voice
	// and the game's own subtitle. Returns the line's length (0 = none).
	FLOAT SpeakLine( AActor* Speaker, const TCHAR* Lines, INT& Index )
	{
		TCHAR Id[64];
		if( !Speaker || !Lines || !ListToken( Lines, Index++, Id, (INT)ARRAY_COUNT(Id) ) )
			return 0.f;
		FRpgArg Args[4];
		Args[0] = FRpgArg::S( Id );
		Args[1] = FRpgArg::I( 1 );
		Args[2] = FRpgArg::F( 0.f );
		Args[3] = FRpgArg::S( appStrstr(Id,TEXT("BUMPLINES")) ? TEXT("BumpDialog") : TEXT("HPdialog") );
		FRpgArg Len;
		Len.Float = 4.f;
		if( !CallScript( Speaker, TEXT("DeliverLocalizedDialog"), Args, 4, &Len ) )
			return 0.f;
		const FLOAT Secs = Clamp( Len.Float, 1.5f, 12.f );
		MateVoiceFree = RpgClock + Secs + 2.f;
		LastTalkTime = RpgClock + Secs - 4.f;   // IsTalking() while the line plays
		static const TCHAR* Talks[4] = { TEXT("talk_bothhands"), TEXT("talkbothhands"), TEXT("talk_rhand"), TEXT("talk1") };
		// Harry himself only lends his voice (a teacher calling from afar).
		for( INT t=0; t<4 && !Speaker->IsA(APlayerPawn::StaticClass()); t++ )
		{
			const FName Talk = FindSeq( Speaker->SkelAnim, Talks[t] );
			if( Talk!=NAME_None )
			{
				Speaker->PlayAnim( Talk, false, 1.f, 0.2f );
				break;
			}
		}
		return Secs;
	}

	void SayMateLine( INT m )
	{
		FSchoolmate& S = Mates[m];
		const TCHAR* Lines = IsChristmas( GameStateNum(CurrentHarry()) ) && ChristmasLines(m) ? ChristmasLines(m) : MateSpot(m,State.RhythmPhase).Lines;
		const FLOAT Secs = SpeakLine( S.Actor, Lines, S.LineIndex );
		if( Secs>0.f )
		{
			S.NextTalk = RpgClock + Secs + 6.f;
			S.NextAct = Secs + 1.f;
		}
		else if( S.Actor )
			Say( MateDef(m).Name, TEXT("...") );
	}

	// Talking to a schoolmate (E). Returns 1 if it was one.
	UBOOL MateTalk( AActor* Npc )
	{
		const INT m = MateIndex( Npc );
		if( m<0 )
			return 0;
		if( RpgClock>=Mates[m].NextTalk && RpgClock>=MateVoiceFree )
			SayMateLine( m );
		Mates[m].bGreeted = 1;
		return 1;
	}

	void UpdateSchoolmates( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( !bMatesSpawned )
		{
			MateDelay -= DeltaSeconds;
			if( MateDelay<=0.f )
				SpawnSchoolmates( Engine, Harry );
			return;
		}
		// Once a second: drop lost actors, step aside for the story.
		MateCheck -= DeltaSeconds;
		const UBOOL bCheck = MateCheck<=0.f;
		if( bCheck )
			MateCheck = 1.f;
		const UBOOL bFree = IsFreeGameplay( Engine, Harry ) && !IsTalking();
		for( INT m=0; m<MATE_MAX; m++ )
		{
			FSchoolmate& S = Mates[m];
			AActor* A = S.Actor;
			if( !A )
				continue;
			if( A->bDeleteMe || (bCheck && !IsInLevel(A)) )
			{
				S.Actor = NULL;
				continue;
			}
			if( bCheck && !S.bSuppressed && (StoryCopyBusy(S.StoryCopy) || IsFriendMate(m)) )
				S.bSuppressed = 1;
			A->bHidden = bWasCutscene || S.bSuppressed;
			if( A->bHidden )
				continue;
			const FLOAT DistSq = (A->Location-Harry->Location).SizeSquared();
			// Turn to Harry while he stands close.
			if( DistSq < 200.f*200.f )
			{
				FVector Face = Harry->Location - A->Location;
				Face.Z = 0.f;
				const INT Diff = ((Face.Rotation().Yaw - A->Rotation.Yaw + 32768) & 65535) - 32768;
				A->Rotation = FRotator( 0, A->Rotation.Yaw + appRound(Diff*Min(1.f,DeltaSeconds*4.f)), 0 );
			}
			// A greeting the first time Harry comes by this visit.
			if( !S.bGreeted && bFree && DistSq < 260.f*260.f && RpgClock>=MateVoiceFree )
			{
				S.bGreeted = 1;
				SayMateLine( m );
			}
			// Activity loop: idle, now and then a one-shot from the spot's list.
			S.NextAct -= DeltaSeconds;
			const FName Idle = FindSeq( A->SkelAnim, m==MATE_Draco ? TEXT("vendor_idle2") : TEXT("Idle") );
			if( S.NextAct<=0.f )
			{
				TCHAR Act[32];
				FName Seq = NAME_None;
				if( ListToken( MateSpot(m,State.RhythmPhase).Acts, S.ActIndex++, Act, (INT)ARRAY_COUNT(Act) ) )
					Seq = FindSeq( A->SkelAnim, Act );
				if( Seq!=NAME_None )
					A->PlayAnim( Seq, false, 1.f, 0.3f );
				S.NextAct = 6.f + RpgRand(7);
			}
			else if( Idle!=NAME_None && A->AnimSequence!=Idle && (A->bAnimLoop || !A->IsAnimating()) )
				A->PlayAnim( Idle, true, 1.f, 0.3f );
		}
	}

	void EndSchoolmates( UGameEngine* Engine )
	{
		for( INT m=0; m<MATE_MAX; m++ )
			DestroyActorSafe( Engine, Mates[m].Actor );
		bMatesSpawned = 0;
		MateDelay = 1.f;
	}

	void ForgetSchoolmates()
	{
		appMemzero( Mates, sizeof(Mates) );
		bMatesSpawned = 0;
		MateDelay = 3.f;
		MateVoiceFree = 0.f;
	}
