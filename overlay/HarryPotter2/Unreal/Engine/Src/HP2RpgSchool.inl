/*=============================================================================
	HP2RpgSchool.inl: Lessons as minigames and the Room of Requirement.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	Lessons: one class a school day. Its teacher shows a blue "Class" marker;
	talk to them (E) to start. Each class is a short timed task built from the
	mod's spells, graded O / E / A / P / T for house points and XP.

	Room of Requirement: furniture bought with beans can be placed in any hub
	(book: Room tab), moved around with Wingardium Leviosa and stays where it
	is left. A Brewing Cauldron brews one Wiggenweld Potion a school day.
=============================================================================*/

	enum ELessonKind { LES_Charms, LES_Herbology, LES_Potions, LES_Defence, LES_Transfig, LES_Quidditch, LES_MAX };

	struct FLessonDef
	{
		const TCHAR* Teacher;   // class chain
		const TCHAR* TeacherName;
		const TCHAR* Subject;
		const TCHAR* Brief;
		INT Count;
		FLOAT Time;
	};

	static const FLessonDef& Lesson( INT k )
	{
		static const FLessonDef Defs[LES_MAX] =
		{
			{ TEXT("ProfFlitwick"), TEXT("Professor Flitwick"), TEXT("Charms"),
				TEXT("Swish and flick! Levitate three objects into the golden circle."), 3, 150.f },
			{ TEXT("ProfSprout"), TEXT("Professor Sprout"), TEXT("Herbology"),
				TEXT("Some of my potted plants have wandered off. Bring back six - Accio helps!"), 6, 150.f },
			{ TEXT("ProfSnape|Snape"), TEXT("Professor Snape"), TEXT("Potions"),
				TEXT("Wiggentree bark and Flobberworm mucus. Stir when the needle is in the gold. Three times. Do not disappoint me."), 3, 60.f },
			{ TEXT("ProfLockhart"), TEXT("Professor Lockhart"), TEXT("Defence Against the Dark Arts"),
				TEXT("I've hidden four dark marks around here. Use Revelio, find them all - just as I would!"), 4, 120.f },
			{ TEXT("ProfMcGonagall"), TEXT("Professor McGonagall"), TEXT("Transfiguration"),
				TEXT("Practical work today: transfigure two students. No points will be taken - this time."), 2, 120.f },
			{ TEXT("OliverWood"), TEXT("Oliver Wood"), TEXT("Quidditch Practice"),
				TEXT("Five Snitches are loose on the grounds, Harry. Catch them all before time's up!"), 5, 150.f },
		};
		return Defs[ Clamp(k,0,LES_MAX-1) ];
	}

	static const TCHAR* GradeName( INT G )
	{
		static const TCHAR* Names[6] = { TEXT("-"), TEXT("Troll"), TEXT("Poor"), TEXT("Acceptable"), TEXT("Exceeds Expectations"), TEXT("Outstanding") };
		return Names[ Clamp(G,0,5) ];
	}
	static const TCHAR* GradeLetter( INT G )
	{
		static const TCHAR* L[6] = { TEXT("-"), TEXT("T"), TEXT("P"), TEXT("A"), TEXT("E"), TEXT("O") };
		return L[ Clamp(G,0,5) ];
	}

	// The running lesson.
	INT LessonKind;          // -1: none
	FLOAT LessonLeft;
	INT LessonProgress;
	INT LessonMisses;
	INT LessonBase;          // counter baseline
	FVector LessonSpot;      // Charms circle
	TArray<AActor*> LessonItems;
	TArray<UBOOL> LessonItemShown;
	TArray<AActor*> LessonCounted;
	TArray<AActor*> LessonLifted;    // Charms: levitated during the lesson
	AActor* TodaysTeacher;
	UBOOL bKnockReady;       // teacher behind a closed classroom door
	UBOOL bKnocked;
	FLOAT H1LeeAt;
	FLOAT NeedlePos, NeedleDir;
	FLOAT LessonFxTimer;

	// The class on the timetable that is on now, else the next one today
	// (-1: no more classes today).
	INT TodaysLesson()
	{
		return NextSlotKind();
	}

	UBOOL LessonAvailableFrom( AActor* Npc )
	{
		const INT k = OpenSlotKind();
		return LessonKind<0 && k>=0 && Npc && NpcIs( Npc, Lesson(k).Teacher );
	}

	void ClearLessonItems( UGameEngine* Engine )
	{
		for( INT i=0; i<LessonItems.Num(); i++ )
			DestroyActorSafe( Engine, LessonItems(i) );
		LessonItems.Empty();
		LessonItemShown.Empty();
		LessonCounted.Empty();
	}

	// Starts today's lesson with its teacher (from TalkTo).
	UBOOL LessonTalk( UGameEngine* Engine, APlayerPawn* Harry, AActor* Npc )
	{
		if( !LessonAvailableFrom(Npc) )
			return 0;
		const INT k = OpenSlotKind( &ActiveSlot );
		const FLessonDef& L = Lesson( k );
		// Potions is brewed at a cauldron of its own.
		if( k==LES_Potions )
			return StartBrew( Engine, Harry, Npc );
		// Quidditch: once the story has had its first practice, Wood takes
		// Harry to the real pitch - the game's own replayable match, on a
		// broom, catching the Snitch (match 5, the final, is left alone).
		const INT GS = GameStateNum( Harry );
		if( k==LES_Quidditch && GS>=40 )
		{
			const INT Match = GS>=130 ? 4 : GS>=100 ? 3 : GS>=80 ? 2 : GS>=50 ? 1 : 0;
			SetIntProp( Harry, TEXT("curQuidMatchNum"), Match );
			State.LessonDay = State.Day;
			MarkSlotAttended();
			GiveXp( 60, TEXT("Quidditch practice") );
			SayAs( L.TeacherName, TEXT("Right, Harry - onto your broom! Proper practice on the pitch today: find the Snitch and catch it."), LessonTeacherMesh(k) );
			TravelTo( Engine, Harry, TEXT("Quidditch.unr") );
			return 1;
		}
		LessonKind = k;
		LessonLeft = L.Time;
		LessonProgress = LessonMisses = 0;
		LessonFxTimer = 0.f;
		ClearLessonItems( Engine );
		LessonLifted.Empty();
		switch( k )
		{
			case LES_Charms:
			{
				// A circle on the floor a little way from the teacher.
				// Knocked from the corridor: the circle comes out to Harry.
				const FVector From = bKnocked ? Harry->Location : Npc->Location;
				FVector Best = From;
				FLOAT BestScore = 1e9f;
				for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
				{
					AActor* N = Engine->GLevel->Actors(i);
					if( !IsWalkPoint(N) )
						continue;
					const FLOAT D = (N->Location - From).Size();
					const FLOAT Score = bKnocked ? D : Abs( D-350.f );
					if( Score<BestScore )
					{
						BestScore = Score;
						Best = N->Location;
					}
				}
				FCheckResult Hit;
				if( !Engine->GLevel->SingleLineCheck( Hit, NULL, Best-FVector(0,0,300.f), Best+FVector(0,0,20.f), TRACE_VisBlocking, FVector(0,0,0) ) )
					Best = Hit.Location;
				LessonSpot = Best;
				break;
			}
			case LES_Herbology:
			{
				static const TCHAR* Pots[3] = { TEXT("HProps.skPlantsFlowerRedMesh"), TEXT("HProps.skPlantsFlowerYellowMesh"), TEXT("HProps.skPlantsCactus1Mesh") };
				for( INT i=0; i<L.Count; i++ )
				{
					FVector Where;
					if( !PickSpotNear( Engine, Harry, 1800.f, (DWORD)(State.Day*53 + i*29 + 7), Where ) )
						continue;
					AActor* A = SpawnPickup( Engine, Pots[i%3], Where, 0.9f );
					if( A )
					{
						LessonItems.AddItem( A );
						LessonItemShown.AddItem( 1 );
					}
				}
				break;
			}
			case LES_Quidditch:
				// Golden Snitches hovering about the grounds.
				for( INT i=0; i<L.Count; i++ )
				{
					FVector Where;
					if( !PickSpotNear( Engine, Harry, 1800.f, (DWORD)(State.Day*61 + i*43 + 11), Where ) )
						continue;
					Where.Z += 55.f;
					AActor* A = SpawnPickup( Engine, TEXT("HPModels.skSnitchMesh"), Where, 2.f );
					if( !A )
						A = SpawnPickup( Engine, TEXT("HProps.skChallengeStarMesh"), Where, 0.5f );
					if( A )
					{
						A->AmbientGlow = 200;
						LessonItems.AddItem( A );
						LessonItemShown.AddItem( 1 );
					}
				}
				break;
			case LES_Defence:
				for( INT i=0; i<L.Count; i++ )
				{
					FVector Where;
					if( !PickSpotNear( Engine, Harry, 1800.f, (DWORD)(State.Day*71 + i*37 + 3), Where ) )
						continue;
					AActor* A = SpawnPickup( Engine, TEXT("HProps.skChallengeStarMesh"), Where, 0.5f );
					if( A )
					{
						A->bHidden = 1;
						LessonItems.AddItem( A );
						LessonItemShown.AddItem( 0 );
					}
				}
				break;
			case LES_Transfig:
				LessonBase = State.Counters[CNT_Transfigs];
				break;
		}
		Say( L.TeacherName, L.Brief );
		AddFeed( FString::Printf(TEXT("%s class"), L.Subject), FColor(120,170,255,255), TOAST_Info, TEXT("The lesson has started") );
		RpgLog( TEXT("lesson started: %s"), L.Subject );
		return 1;
	}

	void FinishLesson( UGameEngine* Engine, APlayerPawn* Harry, UBOOL bAbandon )
	{
		const INT k = LessonKind;
		if( k<0 )
			return;
		const FLessonDef& L = Lesson( k );
		INT Grade = 1;
		if( !bAbandon && LessonProgress>=L.Count )
		{
			const FLOAT Left = LessonLeft/L.Time;
			Grade = Left>0.55f ? 5 : Left>0.35f ? 4 : Left>0.15f ? 3 : 2;
			if( k==LES_Potions )
				Grade = Max( 2, 5-LessonMisses );
		}
		else if( !bAbandon && LessonProgress>0 )
			Grade = 2;
		static const INT Points[6] = { 0, -5, 5, 10, 15, 20 };
		static const INT Xp[6] = { 0, 0, 40, 80, 120, 160 };
		static const INT Beans[6] = { 0, 0, 10, 25, 40, 60 };
		State.LessonDay = State.Day;
		MarkSlotAttended();
		State.LessonGrades[k] = Max( State.LessonGrades[k], Grade );
		AddHousePoints( Harry, HOUSE_Gryffindor, Points[Grade] );
		if( Xp[Grade] )
			GiveXp( Xp[Grade], TEXT("lesson") );
		if( Beans[Grade] )
			GiveBeans( Harry, Beans[Grade] );
		AddFeed( FString::Printf(TEXT("%s: %s"), L.Subject, GradeName(Grade)), Grade>=4 ? FColor(255,222,130,255) : FColor(182,186,206,255), TOAST_Level,
			*FString::Printf(TEXT("%s%i points for Gryffindor"), Points[Grade]>=0 ? TEXT("+") : TEXT(""), Points[Grade]) );
		ClearLessonItems( Engine );
		LessonKind = -1;
		RpgLog( TEXT("lesson %s graded %s"), L.Subject, GradeLetter(Grade) );
	}

	// Potions is brewed in its own screen (HP2RpgBrew.inl); no key input here.
	// The teacher is teaching in a classroom behind a closed door (the
	// story keeps those shut): knocking starts the class from the corridor.
	UBOOL TeacherBehindDoor( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( LessonKind>=0 || OpenSlotKind()<0 || NearNpc || !IsInLevel(TodaysTeacher) || !IsFreeGameplay(Engine,Harry) )
			return 0;
		const FVector D = TodaysTeacher->Location - Harry->Location;
		if( D.X*D.X + D.Y*D.Y > 750.f*750.f || Abs(D.Z) > 220.f )
			return 0;
		FCheckResult Hit;
		return !Engine->GLevel->SingleLineCheck( Hit, Harry, TodaysTeacher->Location, Harry->Location+FVector(0,0,30.f), TRACE_VisBlocking, FVector(0,0,0) );
	}

	UBOOL LessonInput( UGameEngine* Engine, APlayerPawn* Harry, EInputKey Key, EInputAction Action )
	{
		if( Key!=IK_E || Action!=IST_Press || !bKnockReady || !TeacherBehindDoor(Engine,Harry) )
			return 0;
		PlaySfx( TEXT("HPSounds.General.door_no_bump_wood") );
		bKnocked = 1;
		const UBOOL bStarted = LessonTalk( Engine, Harry, TodaysTeacher );
		bKnocked = 0;
		bKnockReady = 0;
		return bStarted;
	}

	void UpdateLesson( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		bKnockReady = TeacherBehindDoor( Engine, Harry );
		if( LessonKind<0 || LessonKind==LES_Potions )
			return; // Potions runs in the brewing screen
		const FLessonDef& L = Lesson( LessonKind );
		LessonLeft -= DeltaSeconds;
		if( LessonLeft<=0.f )
		{
			LessonLeft = 0.f;
			FinishLesson( Engine, Harry, 0 );
			return;
		}
		switch( LessonKind )
		{
			case LES_Charms:
			{
				LessonFxTimer -= DeltaSeconds;
				if( LessonFxTimer<=0.f )
				{
					LessonFxTimer = 1.5f;
					SpawnFx( Engine, TEXT("GoldSparkle01"), LessonSpot+FVector(0,0,10.f), 1.6f );
				}
				// Something levitated that came to rest in the circle counts once.
				for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
				{
					AActor* A = Engine->GLevel->Actors(i);
					if( !A || A->bDeleteMe || A==Harry || !A->Mesh || A->Physics==PHYS_Falling )
						continue;
					const FVector D = A->Location - LessonSpot;
					if( D.X*D.X + D.Y*D.Y > 110.f*110.f || Abs(D.Z) > 120.f )
						continue;
					// Only things levitated during this lesson, once they've landed.
					if( LessonLifted.FindItemIndex(A)==INDEX_NONE || LessonCounted.FindItemIndex(A)!=INDEX_NONE )
						continue;
					UBOOL bLifting = 0;
					for( INT l=0; l<Lifts.Num(); l++ )
						if( Lifts(l).Actor==A )
							bLifting = 1;
					if( bLifting )
						continue;
					LessonCounted.AddItem( A );
					LessonProgress++;
					SpawnFx( Engine, TEXT("WizCrackSparkle"), A->Location, 1.f );
					Ui.Play( RSND_Buy, Harry );
				}
				break;
			}
			case LES_Herbology:
			case LES_Defence:
			case LES_Quidditch:
				for( INT i=LessonItems.Num()-1; i>=0; i-- )
				{
					AActor* A = LessonItems(i);
					if( !IsInLevel(A) )
						continue;
					if( !LessonItemShown(i) )
						continue;
					const FVector D = Harry->Location - A->Location;
					if( D.X*D.X + D.Y*D.Y < 80.f*80.f && Abs(D.Z) < 110.f )
					{
						SpawnFx( Engine, TEXT("WizCrackSparkle"), A->Location, 1.f );
						DestroyActorSafe( Engine, LessonItems(i) );
						LessonItems.Remove( i );
						LessonItemShown.Remove( i );
						LessonProgress++;
						Ui.Play( RSND_Buy, Harry );
					}
				}
				break;
			case LES_Transfig:
				LessonProgress = State.Counters[CNT_Transfigs] - LessonBase;
				break;
		}
		// Lee Jordan commentates the Snitch hunt (Harry Potter 1 voice).
		if( LessonKind==LES_Quidditch && RpgClock>=H1LeeAt && RpgClock>=MateVoiceFree )
		{
			H1LeeAt = RpgClock + 16.f;
			SpeakH1( Harry, TEXT("Lee Jordan"), H1_Lee, 1, TEXT("Snitch|Seeker|Potter|Quidditch") );
		}
		if( LessonKind>=0 && LessonKind!=LES_Potions && LessonProgress>=L.Count )
			FinishLesson( Engine, Harry, 0 );
	}

	// Revelio shows Lockhart's marks.
	void RevealLessonItems( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( LessonKind!=LES_Defence )
			return;
		for( INT i=0; i<LessonItems.Num(); i++ )
			if( !LessonItemShown(i) && IsInLevel(LessonItems(i)) && (LessonItems(i)->Location-Harry->Location).SizeSquared() < 2600.f*2600.f )
			{
				LessonItemShown(i) = 1;
				LessonItems(i)->bHidden = 0;
				SpawnFx( Engine, TEXT("GoldSparkle01"), LessonItems(i)->Location, 2.f );
			}
	}

	// Accio pulls Sprout's plants straight in.
	void AccioLessonItems( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( LessonKind!=LES_Herbology )
			return;
		for( INT i=0; i<LessonItems.Num(); i++ )
			if( IsInLevel(LessonItems(i)) && (LessonItems(i)->Location-Harry->Location).SizeSquared() < 900.f*900.f && Harry->LineOfSightTo(LessonItems(i)) )
				Engine->GLevel->FarMoveActor( LessonItems(i), Harry->Location - FVector(0,0,Harry->CollisionHeight-20.f), 0, 1 );
	}

	void DrawLessonHud( APlayerPawn* Harry )
	{
		if( bKnockReady && LessonKind<0 && TodaysLesson()>=0 )
		{
			const TCHAR* Keys[1] = { TEXT("E") };
			const TCHAR* Verbs[1] = { TEXT("Knock and join the class") };
			Ui.Prompt( Ui.C->ClipX*0.5f, Ui.C->ClipY*0.62f, Keys, Verbs, 1, *FString::Printf(TEXT("%s class"), Lesson(TodaysLesson()).Subject) );
		}
		UCanvas* C = Ui.C;
		const FColor Blue( 120,170,255,255 ), Text1( 240,238,230,255 ), GoldHi( 255,222,130,255 );
		// Teacher marker for today's class.
		if( LessonKind<0 && TodaysLesson()>=0 && TodaysTeacher && IsInLevel(TodaysTeacher)
			&& (TodaysTeacher->Location-Harry->Location).SizeSquared() < 3000.f*3000.f )
		{
			FLOAT SX, SY;
			if( WorldToScreen( TodaysTeacher->Location + FVector(0,0,TodaysTeacher->CollisionHeight+30.f), SX, SY ) )
			{
				FTimeSlot Slots[TT_SLOTS];
				const INT NS = DaySlots( WeekDay(), Slots );
				INT Start = 0;
				for( INT s=0; s<NS; s++ )
					if( Slots[s].Kind==TodaysLesson() && !SlotResolved(s) )
					{
						Start = Slots[s].Start;
						break;
					}
				const FString T = OpenSlotKind()>=0 ? FString::Printf( TEXT("%s class - now"), Lesson(TodaysLesson()).Subject )
					: FString::Printf( TEXT("%s at %02i:%02i"), Lesson(TodaysLesson()).Subject, Start/60, Start%60 );
				const FLOAT W = Ui.TextWidth( Ui.FontCaption, *T ) + Ui.P(16.f), H = Ui.TextHeight( Ui.FontCaption ) + Ui.P(8.f);
				const FRpgRect R( SX-W*0.5f, SY-H, W, H );
				Ui.Glass( R );
				Ui.Rect( FRpgRect(R.X, R.Y+R.H-Max(1.f,Ui.P(2.f)), R.W, Max(1.f,Ui.P(2.f))), Blue );
				Ui.Text( Ui.FontCaption, SX, R.Y+Ui.P(4.f), Text1, *T, RALIGN_Center, 0 );
			}
		}
		if( LessonKind<0 )
			return;
		const FLessonDef& L = Lesson( LessonKind );
		// Panel: subject, progress, time.
		const FString Line = FString::Printf( TEXT("%s   %i / %i   %i:%02i"), L.Subject, Min(LessonProgress,L.Count), L.Count, (INT)LessonLeft/60, (INT)LessonLeft%60 );
		const FLOAT W = Ui.TextWidth( Ui.FontModernBold, *Line ) + Ui.P(32.f), H = Ui.TextHeight(Ui.FontModernBold) + Ui.P(14.f);
		const FRpgRect R( (C->ClipX-W)*0.5f, Ui.P(76.f), W, H );
		Ui.Glass( R );
		Ui.Rect( FRpgRect(R.X, R.Y+R.H-Max(1.f,Ui.P(2.f)), R.W*Clamp(LessonLeft/L.Time,0.f,1.f), Max(1.f,Ui.P(2.f))), Blue );
		Ui.Text( Ui.FontModernBold, C->ClipX*0.5f, R.Y+Ui.P(7.f), Text1, *Line, RALIGN_Center, 0 );
		if( LessonKind==LES_Charms )
		{
			FLOAT SX, SY;
			if( WorldToScreen( LessonSpot+FVector(0,0,20.f), SX, SY ) )
				Ui.Brackets( FRpgRect(SX-Ui.P(30.f), SY-Ui.P(20.f), Ui.P(60.f), Ui.P(40.f)), Ui.P(12.f), Max(1.f,Ui.P(2.f)), GoldHi );
		}
		if( LessonKind==LES_Herbology || LessonKind==LES_Defence || LessonKind==LES_Quidditch )
			for( INT i=0; i<LessonItems.Num(); i++ )
			{
				FLOAT SX, SY;
				if( LessonItemShown(i) && IsInLevel(LessonItems(i)) && WorldToScreen(LessonItems(i)->Location+FVector(0,0,30.f), SX, SY) )
					Ui.Brackets( FRpgRect(SX-Ui.P(10.f), SY-Ui.P(10.f), Ui.P(20.f), Ui.P(20.f)), Ui.P(6.f), Max(1.f,Ui.P(2.f)), Blue );
			}
	}

	/*-------------------------------------------------------------------------
		Room of Requirement: furniture you place and move yourself.
	-------------------------------------------------------------------------*/

	enum EDecorCat { DC_Furniture, DC_Storage, DC_Statues, DC_Games, DC_Curiosities, DC_Plants, DC_Lights, DC_MAX };

	static const TCHAR* DecorCatName( INT c )
	{
		static const TCHAR* Names[DC_MAX] = { TEXT("Furniture"), TEXT("Storage"), TEXT("Statues"), TEXT("Games"), TEXT("Curiosities"), TEXT("Plants"), TEXT("Lights") };
		return Names[ Clamp(c,0,DC_MAX-1) ];
	}

	struct FDecorDef { const TCHAR* Name; const TCHAR* Mesh; INT Price; FLOAT Scale; INT Cat; };

	// Append-only: placed furniture and DecorOwned are saved by index.
	static const FDecorDef& Decor( INT i )
	{
		static const FDecorDef Defs[] =
		{
			{ TEXT("Brewing Cauldron"), TEXT("HProps.skCauldronTeacherMesh"), 400, 1.f, DC_Curiosities },
			{ TEXT("Red Armchair"), TEXT("HProps.skChairsRedComfyMesh"), 120, 1.f, DC_Furniture },
			{ TEXT("Red Couch"), TEXT("HProps.skChairsRedCouchMesh"), 220, 1.f, DC_Furniture },
			{ TEXT("Gryffindor Bed"), TEXT("HProps.skBedGryffindorMesh"), 350, 1.f, DC_Furniture },
			{ TEXT("Bookcase"), TEXT("HProps.skBookcaseGlassDoorsMesh"), 260, 1.f, DC_Storage },
			{ TEXT("Christmas Tree"), TEXT("HProps.skChristmasTreeMesh"), 300, 1.f, DC_Plants },
			{ TEXT("Telescope"), TEXT("HProps.skTelescopeMesh"), 240, 1.f, DC_Curiosities },
			{ TEXT("Owl Statue"), TEXT("HProps.skStatueOwlMesh"), 180, 1.f, DC_Statues },
			{ TEXT("Round Table"), TEXT("HProps.skTableWoodRoundMesh"), 150, 1.f, DC_Furniture },
			{ TEXT("School Trunk"), TEXT("HProps.skTrunkHogwarts1Mesh"), 100, 1.f, DC_Storage },
			{ TEXT("Candelabra"), TEXT("HProps.skCandlesFloorTrippleCandleMesh"), 90, 1.f, DC_Lights },
			{ TEXT("Red Flowers"), TEXT("HProps.skPlantsFlowerRedMesh"), 60, 1.f, DC_Plants },
			{ TEXT("Easel"), TEXT("HProps.skEaselMesh"), 110, 1.f, DC_Curiosities },
			{ TEXT("Suit of Armour"), TEXT("HProps.skArmorWholeSuitMesh"), 500, 1.f, DC_Statues },
			// --- Appended (v6.0) ---------------------------------------------
			{ TEXT("Armoire"), TEXT("HProps.skArmoireMesh"), 200, 1.f, DC_Storage },
			{ TEXT("Old Trunk"), TEXT("HProps.skTrunkMesh"), 80, 1.f, DC_Storage },
			{ TEXT("Dresser"), TEXT("HProps.skDresserMesh"), 160, 1.f, DC_Storage },
			{ TEXT("Dragon Statue"), TEXT("HProps.skStatueDragonMesh"), 300, 1.f, DC_Statues },
			{ TEXT("Gregory the Smarmy"), TEXT("HProps.skStatueGregorySmarmyMesh"), 260, 1.f, DC_Statues },
			{ TEXT("Hunchback Witch"), TEXT("HProps.skStatueHunchbackWitchMesh"), 260, 1.f, DC_Statues },
			{ TEXT("Gargoyle"), TEXT("HPModels.skgargoyleMesh"), 320, 1.f, DC_Statues },
			{ TEXT("Quidditch Board"), TEXT("HProps.skBlackboardQuidditchMesh"), 200, 1.f, DC_Games },
			{ TEXT("Duelling Board"), TEXT("HProps.skBlackboardWizDuelMesh"), 200, 1.f, DC_Games },
			{ TEXT("Dragon Skull"), TEXT("HProps.skDragonSkullMesh"), 400, 1.f, DC_Curiosities },
			{ TEXT("Flying Car"), TEXT("HProps.skFordAngliaMesh"), 1500, 1.f, DC_Curiosities },
			{ TEXT("Sorting Stool"), TEXT("HProps.skStoolsSortingHatMesh"), 150, 1.f, DC_Curiosities },
			{ TEXT("Stone Urn"), TEXT("HProps.skVaseUrnMesh"), 90, 1.f, DC_Curiosities },
			{ TEXT("Crystal Ball"), TEXT("HProps.skCrystalBallMesh"), 140, 1.f, DC_Curiosities },
			{ TEXT("Slytherin Couch"), TEXT("HProps.skCouchSlytherinMesh"), 220, 1.f, DC_Furniture },
			{ TEXT("Footstool"), TEXT("HProps.skFootStoolMesh"), 40, 1.f, DC_Furniture },
			{ TEXT("Table Lamp"), TEXT("HProps.sktablelampMesh"), 60, 1.f, DC_Lights },
			{ TEXT("Cactus"), TEXT("HProps.skPlantsCactus1Mesh"), 50, 1.f, DC_Plants },
		};
		return Defs[ Clamp(i, 0, (INT)ARRAY_COUNT(Defs)-1) ];
	}
	enum { NUM_DECOR = 32 };

	struct FDecorPlaced { FString Map; INT Id; FVector Loc; INT Yaw; AActor* Actor; INT Size; };   // Size in percent
	TArray<FDecorPlaced> Placed;
	UBOOL bDecorSpawned;

	UBOOL IsDecor( AActor* A )
	{
		for( INT i=0; i<Placed.Num(); i++ )
			if( Placed(i).Actor==A && A )
				return 1;
		return 0;
	}

	void SpawnDecor( UGameEngine* Engine )
	{
		bDecorSpawned = 1;
		const FString Map = Engine->GLevel->URL.Map;
		for( INT i=0; i<Placed.Num(); i++ )
		{
			FDecorPlaced& D = Placed(i);
			D.Actor = NULL;
			if( D.Map!=Map )
				continue;
			AActor* A = SpawnPickup( Engine, Decor(D.Id).Mesh, D.Loc, Decor(D.Id).Scale*Clamp(D.Size,50,200)*0.01f );
			if( A )
			{
				A->AmbientGlow = 0;
				A->Rotation = FRotator( 0, D.Yaw, 0 );
				A->SetCollision( 1, 1, 1 );
				D.Actor = A;
			}
		}
	}

	void EndDecor( UGameEngine* Engine )
	{
		for( INT i=0; i<Placed.Num(); i++ )
			DestroyActorSafe( Engine, Placed(i).Actor );
		bDecorSpawned = 0;
	}

	void ForgetDecor()
	{
		for( INT i=0; i<Placed.Num(); i++ )
			Placed(i).Actor = NULL;
		bDecorSpawned = 0;
	}

	// Places an owned piece a little in front of Harry.
	void PlaceDecor( UGameEngine* Engine, APlayerPawn* Harry, INT Id )
	{
		if( State.DecorOwned[Id]<=0 )
			return;
		const FVector Ahead = Harry->Location + Harry->Rotation.Vector()*120.f;
		FCheckResult Hit;
		FVector Where = Ahead;
		if( !Engine->GLevel->SingleLineCheck( Hit, NULL, Ahead-FVector(0,0,400.f), Ahead+FVector(0,0,40.f), TRACE_VisBlocking, FVector(0,0,0) ) )
			Where = Hit.Location;
		AActor* A = SpawnPickup( Engine, Decor(Id).Mesh, Where, Decor(Id).Scale );
		if( !A )
			return;
		// Rest the piece on the floor.
		A->Location = FVector( 0, 0, 0 );
		const FCoords Box = A->GetRenderBoundingBox( 1 );
		Where.Z += -Min( Box.Origin.Z, Box.Origin.Z+Box.ZAxis.Z );
		Engine->GLevel->FarMoveActor( A, Where, 0, 1 );
		A->AmbientGlow = 0;
		A->Rotation = FRotator( 0, Harry->Rotation.Yaw + 32768, 0 );
		A->SetCollision( 1, 1, 1 );
		FDecorPlaced* D = new(Placed) FDecorPlaced;
		D->Map = Engine->GLevel->URL.Map;
		D->Id = Id;
		D->Loc = Where;
		D->Yaw = A->Rotation.Yaw;
		D->Actor = A;
		D->Size = 100;
		State.DecorOwned[Id]--;
		SpawnFx( Engine, TEXT("SmokeExplo_01"), Where, 1.f );
		RpgLog( TEXT("decor placed: %s"), Decor(Id).Name );
	}

	// Picks up the nearest placed piece back into storage.
	void PickUpDecor( UGameEngine* Engine, APlayerPawn* Harry )
	{
		INT Best = -1;
		FLOAT BestD = 300.f*300.f;
		for( INT i=0; i<Placed.Num(); i++ )
			if( IsInLevel(Placed(i).Actor) && (Placed(i).Actor->Location-Harry->Location).SizeSquared() < BestD )
			{
				BestD = (Placed(i).Actor->Location-Harry->Location).SizeSquared();
				Best = i;
			}
		if( Best<0 )
		{
			SetNotice( TEXT("Stand next to a piece of your furniture to pick it up.") );
			return;
		}
		SpawnFx( Engine, TEXT("SmokeExplo_02"), Placed(Best).Actor->Location, 1.f );
		DestroyActorSafe( Engine, Placed(Best).Actor );
		State.DecorOwned[Placed(Best).Id]++;
		Placed.Remove( Best );
	}

	void UpdateDecor( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( !bDecorSpawned )
			SpawnDecor( Engine );
		for( INT i=0; i<Placed.Num(); i++ )
		{
			FDecorPlaced& D = Placed(i);
			if( !D.Actor || !IsInLevel(D.Actor) )
				continue;
			// Keep where Leviosa left it.
			if( D.Actor->Physics!=PHYS_Falling && (D.Actor->Location-D.Loc).SizeSquared() > 4.f )
			{
				UBOOL bLifting = 0;
				for( INT l=0; l<Lifts.Num(); l++ )
					if( Lifts(l).Actor==D.Actor )
						bLifting = 1;
				if( !bLifting )
				{
					D.Loc = D.Actor->Location;
					D.Yaw = D.Actor->Rotation.Yaw;
				}
			}
		}
	}

	// E next to a placed Brewing Cauldron: one potion a school day.
	UBOOL BrewInput( UGameEngine* Engine, APlayerPawn* Harry )
	{
		for( INT i=0; i<Placed.Num(); i++ )
		{
			const FDecorPlaced& D = Placed(i);
			if( D.Id!=0 || !IsInLevel(D.Actor) || (D.Actor->Location-Harry->Location).SizeSquared() > 160.f*160.f )
				continue;
			if( State.BrewDay==State.Day )
			{
				AddFeed( TEXT("Brewing Cauldron"), FColor(120,220,140,255), TOAST_Info, TEXT("Still brewing - come back tomorrow") );
				return 1;
			}
			State.BrewDay = State.Day;
			ApplyConsumable( Harry, CON_Potion, 1 );
			SpawnFx( Engine, TEXT("Cauldron_Green"), D.Actor->Location, 2.f );
			AddFeed( TEXT("Brewing Cauldron"), FColor(120,220,140,255), TOAST_Reward, TEXT("+1 Wiggenweld Potion") );
			Ui.Play( RSND_Reward, Harry );
			return 1;
		}
		return 0;
	}

	FString SaveDecor()
	{
		FString Out;
		for( INT i=0; i<Placed.Num(); i++ )
		{
			const FDecorPlaced& D = Placed(i);
			Out += FString::Printf( TEXT("%s,%i,%i,%i,%i,%i,%i;"), *D.Map, D.Id, appRound(D.Loc.X), appRound(D.Loc.Y), appRound(D.Loc.Z), D.Yaw, D.Size );
		}
		return Out;
	}

	void LoadDecor( const TCHAR* Stream )
	{
		Placed.Empty();
		bDecorSpawned = 0;
		const TCHAR* P = appStrstr( Stream, TEXT("Decor=") );
		if( !P )
			return;
		P += 6;
		while( *P && *P!='\r' && *P!='\n' )
		{
			FString Map;
			while( *P && *P!=',' && *P!='\r' && *P!='\n' )
				Map += FString::Printf( TEXT("%c"), *P++ );
			INT V[6] = { 0, 0, 0, 0, 0, 100 };   // older saves have no size
			for( INT k=0; k<6 && *P==','; k++ )
			{
				P++;
				V[k] = appAtoi( P );
				while( *P && *P!=',' && *P!=';' && *P!='\r' && *P!='\n' )
					P++;
			}
			if( *P==';' )
				P++;
			if( Map.Len() && V[0]>=0 && V[0]<NUM_DECOR )
			{
				FDecorPlaced* D = new(Placed) FDecorPlaced;
				D->Map = Map;
				D->Id = V[0];
				D->Loc = FVector( V[1], V[2], V[3] );
				D->Yaw = V[4];
				D->Size = Clamp( V[5], 50, 200 );
				D->Actor = NULL;
			}
		}
	}

	// Book tab: buy, place and pick up furniture.
	void DrawRoomTab( UGameEngine* Engine, APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.P(2.f);
		Ui.TextWrapped( Ui.FontBody, FRpgRect(Area.X, Area.Y, Area.W, Ui.TextHeight(Ui.FontBody)*3), RpgMuted,
			TEXT("Place furniture in a hub; move it with Wingardium Leviosa. Brewing Cauldron: one potion per school day (E).") );
		const FLOAT Top = Area.Y + Ui.TextHeight(Ui.FontBody)*3 + 10*S;
		const FLOAT RowH = Ui.TextHeight(Ui.FontBody) + 10*S;
		const INT Visible = Max( 1, appFloor((Area.Y+Area.H-Top-RowH*3.2f)/RowH) );
		Sel = Clamp( Sel, 0, NUM_DECOR-1 );
		ApplyWheel( NUM_DECOR, 1, Visible );
		ClampSelection( NUM_DECOR, 1, Visible );
		const INT Beans = GetBeans( Harry );
		for( INT v=0; v<Visible && Scroll+v<NUM_DECOR; v++ )
		{
			const INT i = Scroll+v;
			const FDecorDef& Dd = Decor( i );
			const FRpgRect Row( Area.X, Top + v*RowH, Area.W, RowH-3*S );
			RowActivated( Row, i, &Decor(i) );
			Ui.Text( Ui.FontBody, Row.X+6*S, Row.Y+5*S, i==Sel ? RpgGoldBright : RpgParchment, Dd.Name );
			INT Placed2 = 0;
			for( INT p=0; p<Placed.Num(); p++ )
				Placed2 += Placed(p).Id==i;
			Ui.Text( Ui.FontSmall, Row.X+Row.W*0.45f, Row.Y+6*S, RpgMuted, *FString::Printf(TEXT("stored %i, placed %i"), State.DecorOwned[i], Placed2) );
			Ui.Text( Ui.FontBody, Row.X+Row.W-6*S, Row.Y+5*S, Dd.Price<=Beans ? RpgParchment : RpgRed, *FString::Printf(TEXT("%i beans"), Dd.Price), RALIGN_Right );
		}
		// Actions for the selected piece.
		const FLOAT BY = Area.Y+Area.H-RowH;
		const FLOAT BW = Area.W/3.f - 6*S;
		const FDecorDef& Dd = Decor( Sel );
		const UBOOL bHub = LastLevel && (MapHas(*LastLevel->URL.Map,TEXT("_hub")) || IsRoomMap(*LastLevel->URL.Map));
		if( Ui.Button( FRpgRect(Area.X, BY, BW, RowH-2*S), *FString::Printf(TEXT("Buy (%i)"), Dd.Price), Beans>=Dd.Price, &Placed ) )
		{
			GiveBeans( Harry, -Dd.Price );
			State.DecorOwned[Sel]++;
			Ui.Play( RSND_Buy, Harry );
			SetNotice( FString::Printf(TEXT("%s bought - place it in a hub."), Dd.Name) );
		}
		if( Ui.Button( FRpgRect(Area.X+BW+9*S, BY, BW, RowH-2*S), TEXT("Place here"), bHub && State.DecorOwned[Sel]>0, &State.DecorOwned[0] ) )
		{
			PlaceDecor( Engine, Harry, Sel );
			CloseMenu( Engine );
			return;
		}
		if( Ui.Button( FRpgRect(Area.X+(BW+9*S)*2, BY, BW, RowH-2*S), TEXT("Pick up nearest"), Placed.Num()>0, &State.BrewDay ) )
			PickUpDecor( Engine, Harry );
		// The full decorating mode: top-down view, grid and categories.
		if( Ui.Button( FRpgRect(Area.X, BY-RowH-4*S, Area.W, RowH-2*S), TEXT("Decorate here (room view)"), bHub, &bDecorSpawned ) )
		{
			CloseMenu( Engine );
			OpenRoom( Engine, Harry );
			return;
		}
		if( !bHub )
			Ui.Text( Ui.FontSmall, Area.X, BY-Ui.TextHeight(Ui.FontSmall)-2*S, RpgMuted, TEXT("Furniture can only be placed in the hubs.") );
	}

