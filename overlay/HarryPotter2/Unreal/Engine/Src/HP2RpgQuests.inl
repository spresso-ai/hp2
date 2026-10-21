/*=============================================================================
	HP2RpgQuests.inl: Side quests, Field Guide pages, the Journal tab,
	difficulty and photo mode of FHP2Rpg.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	- Quest givers show a gold "!" (new quest) or "?" (ready to hand in);
	  talking to them (E) gives or completes the quest.
	- Collect quests scatter their items over a hub's navigation points.
	- Field Guide pages hide in the hubs until Revelio shows them.
	- Photo mode (P) pauses the game, hides the HUD and lets the mouse orbit
	  the camera around Harry, with a few film filters.
=============================================================================*/

	enum EQuestState { QS_None, QS_Active, QS_Ready, QS_Done };

	struct FWorldPickup { AActor* Actor; INT Quest; INT Page; UBOOL bRevealed; FVector Base; };

	TArray<FWorldPickup> Pickups;   // quest items and Field Guide pages in this level
	TArray<AActor*> Givers;         // quest givers in this level (refreshed now and then)
	UBOOL bPickupsSpawned;
	FLOAT PickupSpawnDelay;
	FLOAT GiverScanTimer;
	FLOAT TrackerTimer;
	// Speech panel for quest dialogue.
	FString DialogSpeaker, DialogText;
	FLOAT DialogLeft;
	// Photo mode.
	UBOOL bPhoto;
	INT PhotoFilter;
	FLOAT PhotoYaw, PhotoPitch, PhotoDist;
	UBOOL bPhotoHint;

	/*-------------------------------------------------------------------------
		Quest bookkeeping.
	-------------------------------------------------------------------------*/

	INT QuestProgress( APlayerPawn* Harry, INT q )
	{
		const FQuestDef& Q = GQuests[q];
		if( State.QuestState[q]==QS_Ready || State.QuestState[q]==QS_Done )
			return Q.Count;
		if( State.QuestState[q]!=QS_Active )
			return 0;
		switch( Q.Type )
		{
			case QT_Collect: return Min( Q.Count, State.QuestBase[q] );
			case QT_Counter: return Clamp( CounterValue(Harry,Q.Counter) - State.QuestBase[q], 0, Q.Count );
			case QT_Kills:   return Clamp( (Q.Counter<0 ? State.Counters[CNT_Creatures] : State.Kills[Q.Counter]) - State.QuestBase[q], 0, Q.Count );
		}
		return 0;
	}

	UBOOL IsGiverOf( AActor* A, INT q )
	{
		return A && NpcIs( A, GQuests[q].Giver );
	}

	// The quest this NPC has for Harry right now: ready ones first.
	INT QuestFor( AActor* Npc, UBOOL bReadyOnly )
	{
		for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
			if( State.QuestState[q]==QS_Ready && IsGiverOf(Npc,q) )
				return q;
		if( bReadyOnly )
			return -1;
		for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
		{
			if( State.QuestState[q]!=QS_None || State.Level<GQuests[q].MinLevel || !IsGiverOf(Npc,q) )
				continue;
			// One open quest per giver at a time.
			UBOOL bBusy = 0;
			for( INT o=0; o<GNumQuests && o<MAX_QUESTS; o++ )
				if( State.QuestState[o]==QS_Active && IsGiverOf(Npc,o) )
					bBusy = 1;
			if( !bBusy )
				return q;
		}
		return -1;
	}

	void Say( const TCHAR* Speaker, const TCHAR* Text )
	{
		DialogSpeaker = Speaker;
		DialogText = Text;
		DialogLeft = 7.f;
	}

	// Talking to a giver: returns 1 if a quest was given or completed.
	UBOOL QuestTalk( APlayerPawn* Harry, AActor* Npc )
	{
		const INT q = QuestFor( Npc, 0 );
		if( q<0 )
		{
			// Remind Harry what an active quest from this NPC needs.
			for( INT a=0; a<GNumQuests && a<MAX_QUESTS; a++ )
				if( State.QuestState[a]==QS_Active && IsGiverOf(Npc,a) )
				{
					Say( GQuests[a].GiverName, *FString::Printf(TEXT("%s (%i/%i)"), GQuests[a].Objective, QuestProgress(Harry,a), GQuests[a].Count) );
					return 1;
				}
			return 0;
		}
		const FQuestDef& Q = GQuests[q];
		if( State.QuestState[q]==QS_Ready )
		{
			State.QuestState[q] = QS_Done;
			State.Counters[CNT_Quests]++;
			GiveBeans( Harry, Q.Beans );
			if( Q.Points )
				AddHousePoints( Harry, HOUSE_Gryffindor, Q.Points );
			GiveXp( Q.Xp, TEXT("quest") );
			Say( Q.GiverName, Q.Done );
			AddFeed( TEXT("Quest complete"), FColor(255,222,130,255), TOAST_Reward,
				*FString::Printf(TEXT("%s: +%i beans, +%i XP%s"), Q.Title, Q.Beans, Q.Xp, Q.Points ? *FString::Printf(TEXT(", +%i points"), Q.Points) : TEXT("")) );
			Ui.Play( RSND_Reward, Harry );
			RpgLog( TEXT("quest done: %s"), Q.Title );
			return 1;
		}
		State.QuestState[q] = QS_Active;
		switch( Q.Type )
		{
			case QT_Collect: State.QuestBase[q] = 0; break;
			case QT_Counter: State.QuestBase[q] = CounterValue( Harry, Q.Counter ); break;
			case QT_Kills:   State.QuestBase[q] = Q.Counter<0 ? State.Counters[CNT_Creatures] : State.Kills[Q.Counter]; break;
		}
		Say( Q.GiverName, Q.Intro );
		AddFeed( TEXT("New quest"), FColor(255,222,130,255), TOAST_Reward, Q.Title );
		TrackerTimer = 8.f;
		bPickupsSpawned = 0; // collect quests place their items
		PickupSpawnDelay = 0.5f;
		RpgLog( TEXT("quest accepted: %s"), Q.Title );
		return 1;
	}

	void CheckQuests( APlayerPawn* Harry )
	{
		for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
		{
			if( State.QuestState[q]!=QS_Active || QuestProgress(Harry,q) < GQuests[q].Count )
				continue;
			State.QuestState[q] = QS_Ready;
			TrackerTimer = 8.f;
			AddFeed( TEXT("Quest ready"), FColor(255,222,130,255), TOAST_Reward, *FString::Printf(TEXT("Return to %s"), GQuests[q].GiverName) );
			Ui.Play( RSND_Reward, Harry );
		}
	}

	/*-------------------------------------------------------------------------
		Quest items and Field Guide pages in the world.
	-------------------------------------------------------------------------*/

	UBOOL PageFound( INT p )
	{
		return p>=0 && p<MAX_PAGES && (State.Pages[p>>5] & (1<<(p&31)))!=0;
	}

	// A spot on the floor near a navigation point, picked by a stable hash.
	UBOOL PickSpot( UGameEngine* Engine, DWORD Seed, FVector& Out )
	{
		TArray<ANavigationPoint*> Nav;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			ANavigationPoint* N = Cast<ANavigationPoint>( Engine->GLevel->Actors(i) );
			if( N && !N->bDeleteMe && N->IsA(APathNode::StaticClass()) )
				Nav.AddItem( N );
		}
		if( !Nav.Num() )
			return 0;
		ANavigationPoint* N = Nav( HashMix(Seed,7919u) % (DWORD)Nav.Num() );
		FCheckResult Hit;
		const FVector Start = N->Location + FVector( 0, 0, 20.f );
		if( !Engine->GLevel->SingleLineCheck( Hit, NULL, Start - FVector(0,0,300.f), Start, TRACE_VisBlocking, FVector(0,0,0) ) )
			Out = Hit.Location + FVector( 0, 0, 22.f );
		else
			Out = N->Location;
		return 1;
	}

	AActor* SpawnPickup( UGameEngine* Engine, const TCHAR* MeshPath, FVector Where, FLOAT Scale )
	{
		UMesh* Mesh = LoadMesh( MeshPath );
		if( !Mesh )
			return NULL;
		AActor* A = SpawnRenderActor( Engine, NULL, Where );
		if( !A )
			return NULL;
		A->DrawType = DT_Mesh;
		A->Mesh = Mesh;
		A->DrawScale = Scale;
		A->AmbientGlow = 90;
		return A;
	}

	void SpawnPickups( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bPickupsSpawned = 1;
		for( INT p=0; p<Pickups.Num(); p++ )
			DestroyActorSafe( Engine, Pickups(p).Actor );
		Pickups.Empty();
		const TCHAR* Map = *Engine->GLevel->URL.Map;
		// Collect quests.
		for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
		{
			const FQuestDef& Q = GQuests[q];
			if( Q.Type!=QT_Collect || State.QuestState[q]!=QS_Active || !Q.Map || !appStrstr(Map,Q.Map) )
				continue;
			for( INT i=State.QuestBase[q]; i<Q.Count; i++ )
			{
				FVector Where;
				if( !PickSpot( Engine, (DWORD)(q*131 + i*17 + 5), Where ) )
					continue;
				AActor* A = SpawnPickup( Engine, Q.Mesh, Where, 1.f );
				if( !A )
					continue;
				FWorldPickup* P = new(Pickups) FWorldPickup;
				P->Actor = A; P->Quest = q; P->Page = -1; P->bRevealed = 1; P->Base = Where;
			}
		}
		// Field Guide pages: hidden until Revelio.
		for( INT g=0; g<GNumFieldPages && g<MAX_PAGES; g++ )
		{
			if( PageFound(g) || !appStrstr(Map,GFieldPages[g].Map) )
				continue;
			FVector Where;
			if( !PickSpot( Engine, (DWORD)(g*977 + 31), Where ) )
				continue;
			// A loose golden page floating a little above the floor.
			Where.Z += 26.f;
			AActor* A = SpawnPickup( Engine, TEXT("HProps.skSheetTest1Mesh"), Where, 1.4f );
			if( !A )
				A = SpawnPickup( Engine, TEXT("HProps.skBooksBigFlitwickBookMesh"), Where, 0.5f );
			if( !A )
				continue;
			A->bHidden = 1;
			FWorldPickup* P = new(Pickups) FWorldPickup;
			P->Actor = A; P->Quest = -1; P->Page = g; P->bRevealed = 0; P->Base = Where;
		}
		if( Pickups.Num() )
			RpgLog( TEXT("pickups placed: %i"), Pickups.Num() );
	}

	// Revelio shows hidden pages nearby.
	INT RevealPages( UGameEngine* Engine, APlayerPawn* Harry )
	{
		INT N = 0;
		for( INT p=0; p<Pickups.Num(); p++ )
		{
			FWorldPickup& P = Pickups(p);
			if( P.Page<0 || P.bRevealed || !IsInLevel(P.Actor) || (P.Base-Harry->Location).SizeSquared() > 2600.f*2600.f )
				continue;
			P.bRevealed = 1;
			P.Actor->bHidden = 0;
			SpawnFx( Engine, TEXT("GoldSparkle01"), P.Base, 2.f );
			FRevealEntry* E = new(Reveals) FRevealEntry;
			E->Actor = P.Actor;
			E->Location = P.Base;
			E->Kind = 4;
			N++;
		}
		return N;
	}

	void CollectPickup( UGameEngine* Engine, APlayerPawn* Harry, INT p )
	{
		FWorldPickup P = Pickups(p);
		Pickups.Remove( p );
		SpawnFx( Engine, TEXT("WizCrackSparkle"), P.Base, 1.f );
		DestroyActorSafe( Engine, P.Actor );
		Ui.Play( RSND_Reward, Harry );
		if( P.Quest>=0 )
		{
			const FQuestDef& Q = GQuests[P.Quest];
			State.QuestBase[P.Quest] = Min( Q.Count, State.QuestBase[P.Quest]+1 );
			TrackerTimer = 6.f;
			AddFeed( Q.Title, FColor(255,222,130,255), TOAST_Reward, *FString::Printf(TEXT("%i / %i found"), State.QuestBase[P.Quest], Q.Count) );
		}
		else if( P.Page>=0 )
		{
			State.Pages[P.Page>>5] |= 1<<(P.Page&31);
			State.Counters[CNT_Pages]++;
			GiveXp( 30, TEXT("field guide page") );
			AddFeed( TEXT("Field Guide page"), FColor(230,210,170,255), TOAST_Reward, GFieldPages[P.Page].Title );
			// A complete hub set: five pages of one place.
			INT Have = 0, Total = 0;
			for( INT g=0; g<GNumFieldPages; g++ )
				if( appStricmp(GFieldPages[g].Place,GFieldPages[P.Page].Place)==0 )
				{
					Total++;
					Have += PageFound(g);
				}
			if( Have==Total )
			{
				State.SkillPoints++;
				GiveBeans( Harry, 150 );
				AddFeed( FString::Printf(TEXT("%s pages complete!"), GFieldPages[P.Page].Place), FColor(255,222,130,255), TOAST_Level, TEXT("+1 skill point, +150 beans") );
			}
		}
	}

	void UpdatePickups( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( !bPickupsSpawned )
		{
			PickupSpawnDelay -= DeltaSeconds;
			if( PickupSpawnDelay<=0.f )
				SpawnPickups( Engine, Harry );
			return;
		}
		for( INT p=Pickups.Num()-1; p>=0; p-- )
		{
			FWorldPickup& P = Pickups(p);
			if( !IsInLevel(P.Actor) )
			{
				Pickups.Remove( p );
				continue;
			}
			if( !P.bRevealed )
				continue;
			// Spin and bob so it reads as something to grab.
			P.Actor->Rotation.Yaw += appRound( 16000.f*DeltaSeconds );
			Engine->GLevel->FarMoveActor( P.Actor, P.Base + FVector(0,0,(FLOAT)appSin(RpgClock*2.5f + p)*6.f), 0, 1 );
			const FVector D = Harry->Location - P.Base;
			if( D.X*D.X + D.Y*D.Y < 75.f*75.f && Abs(D.Z) < 110.f )
				CollectPickup( Engine, Harry, p );
		}
	}

	void EndPickups( UGameEngine* Engine )
	{
		for( INT p=0; p<Pickups.Num(); p++ )
			DestroyActorSafe( Engine, Pickups(p).Actor );
		Pickups.Empty();
		bPickupsSpawned = 0;
		PickupSpawnDelay = 1.f;
	}

	void ForgetQuests()
	{
		Pickups.Empty();
		Givers.Empty();
		bPickupsSpawned = 0;
		PickupSpawnDelay = 2.f;
		GiverScanTimer = 0.f;
	}

	void UpdateQuests( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		DialogLeft = Max( 0.f, DialogLeft-DeltaSeconds );
		TrackerTimer = Max( 0.f, TrackerTimer-DeltaSeconds );
		UpdatePickups( Engine, Harry, DeltaSeconds );
		GiverScanTimer -= DeltaSeconds;
		if( GiverScanTimer<=0.f )
		{
			GiverScanTimer = 2.f;
			Givers.Empty();
			TodaysTeacher = NULL;
			for( INT si=0; si<StandIns.Num(); si++ )
			{
				AActor* A = StandIns(si).Actor;
				if( !A || A->bDeleteMe )
					continue;
				if( !TodaysTeacher && NpcIs(A, Lesson(TodaysLesson()).Teacher) )
					TodaysTeacher = A;
				for( INT g=0; g<GNumQuests && g<MAX_QUESTS; g++ )
					if( IsGiverOf(A,g) )
					{
						Givers.AddItem( A );
						break;
					}
			}
			for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
			{
				AActor* A = Engine->GLevel->Actors(i);
				if( !A || A->bDeleteMe || A->bHidden || IsHolding(A) || !A->IsA(APawn::StaticClass()) )
					continue;
				if( !TodaysTeacher && NpcIs(A, Lesson(TodaysLesson()).Teacher) )
					TodaysTeacher = A;
				for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
					if( IsGiverOf(A,q) )
					{
						Givers.AddItem( A );
						break;
					}
			}
		}
	}

	/*-------------------------------------------------------------------------
		Drawing: giver markers, quest tracker, dialogue.
	-------------------------------------------------------------------------*/

	void DrawQuestHud( APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		const FColor GoldHi( 255,222,130,255 ), Text1( 240,238,230,255 ), Text2( 182,186,206,255 );
		// "!" new quest, "?" ready to hand in.
		for( INT g=0; g<Givers.Num(); g++ )
		{
			AActor* A = Givers(g);
			if( !IsInLevel(A) || (A->Location-Harry->Location).SizeSquared() > 3000.f*3000.f )
				continue;
			const INT q = QuestFor( A, 0 );
			if( q<0 )
				continue;
			FLOAT SX, SY;
			if( !WorldToScreen( A->Location + FVector(0,0,A->CollisionHeight+30.f), SX, SY ) )
				continue;
			const UBOOL bReady = State.QuestState[q]==QS_Ready;
			Ui.Text( Ui.FontModernBig, SX, SY-Ui.TextHeight(Ui.FontModernBig), bReady ? FColor(120,220,140,255) : GoldHi, bReady ? TEXT("?") : TEXT("!"), RALIGN_Center );
		}
		// Collect-quest items: a small marker so they can be found.
		for( INT p=0; p<Pickups.Num(); p++ )
		{
			const FWorldPickup& P = Pickups(p);
			if( P.Quest<0 || !P.bRevealed )
				continue;
			FLOAT SX, SY;
			if( !WorldToScreen( P.Base + FVector(0,0,40.f), SX, SY ) )
				continue;
			const FLOAT Dist = (P.Base-Harry->Location).Size();
			if( Dist>1800.f )
				continue;
			Ui.Brackets( FRpgRect(SX-Ui.P(10.f), SY-Ui.P(10.f), Ui.P(20.f), Ui.P(20.f)), Ui.P(6.f), Max(1.f,Ui.P(2.f)), GoldHi );
		}
		// Tracker: active quests, shown after a change or with Tab.
		if( (TrackerTimer>0.f || bShowAllHud) && !IsTalking() && LessonKind<0 )
		{
			FLOAT Y = Ui.P(120.f);
			const FLOAT X = C->ClipX - Ui.P(48.f);
			const FLOAT W = Min(Ui.P(340.f),C->ClipX*0.34f);
			INT Shown = 0, More = 0;
			for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
			{
				if( State.QuestState[q]!=QS_Active && State.QuestState[q]!=QS_Ready )
					continue;
				if( Shown>=2 )
				{
					More++;
					continue;
				}
				Shown++;
				const FQuestDef& Q = GQuests[q];
				Ui.Text( Ui.FontModernBold, X, Y, GoldHi, *Ui.FitText(Ui.FontModernBold,Q.Title,W), RALIGN_Right );
				Y += Ui.TextHeight( Ui.FontModernBold );
				const FString Line = State.QuestState[q]==QS_Ready
					? FString::Printf( TEXT("Return to %s"), Q.GiverName )
					: FString::Printf( TEXT("%s  %i/%i"), Q.Objective, QuestProgress(Harry,q), Q.Count );
				Ui.Text( Ui.FontCaption, X, Y, State.QuestState[q]==QS_Ready ? FColor(120,220,140,255) : Text2, *Ui.FitText(Ui.FontCaption,*Line,W), RALIGN_Right );
				Y += Ui.TextHeight( Ui.FontCaption ) + Ui.P(8.f);
			}
			if( More>0 )
				Ui.Text( Ui.FontCaption, X, Y, Text2, *FString::Printf(TEXT("+%i in Journal [K]"),More), RALIGN_Right );
		}
		// Quest dialogue: a parchment-toned panel above the hotbar.
		if( DialogLeft>0.f && DialogText.Len() )
		{
			const FLOAT W = Min( Ui.P(820.f), C->ClipX*0.7f );
			const FLOAT LineH = Ui.TextHeight( Ui.FontModern );
			const FLOAT H = Ui.TextHeight(Ui.FontModernBold) + LineH*3 + Ui.P(28.f);
			const FRpgRect R( (C->ClipX-W)*0.5f, C->ClipY - Ui.P(150.f) - H, W, H );
			Ui.Glass( R, 1 );
			Ui.Text( Ui.FontModernBold, R.X+Ui.P(16.f), R.Y+Ui.P(10.f), GoldHi, *DialogSpeaker, RALIGN_Left, 0 );
			Ui.TextWrapped( Ui.FontModern, FRpgRect(R.X+Ui.P(16.f), R.Y+Ui.P(14.f)+Ui.TextHeight(Ui.FontModernBold), R.W-Ui.P(32.f), LineH*3+Ui.P(4.f)), Text1, *DialogText );
		}
	}

	/*-------------------------------------------------------------------------
		Journal tab: quests and the Field Guide.
	-------------------------------------------------------------------------*/

	INT JournalSection;

	void DrawJournalTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.P(2.f);
		static const TCHAR* Sections[3] = { TEXT("Quests"), TEXT("Field Guide"), TEXT("Duelling") };
		const INT Clicked = DrawTabs( FRpgRect(Area.X, Area.Y, Area.W*0.8f, Ui.TextHeight(Ui.FontItem)+6*S), Sections, NULL, 3, JournalSection );
		if( Clicked>=0 && Clicked!=JournalSection )
		{
			JournalSection = Clicked;
			Sel = Scroll = 0;
		}
		const FRpgRect Body( Area.X, Area.Y+Ui.TextHeight(Ui.FontItem)+14*S, Area.W, Area.H-Ui.TextHeight(Ui.FontItem)-14*S );
		const FLOAT RowH = Ui.TextHeight(Ui.FontBody) + Ui.TextHeight(Ui.FontSmall) + 8*S;
		const FLOAT ListW = Body.W*0.42f;
		const FRpgRect Detail( Body.X+ListW+12*S, Body.Y, Body.W-ListW-12*S, Body.H );
		if( JournalSection==2 )
		{
			DrawDuelLadder( Body );
			return;
		}
		if( JournalSection==0 )
		{
			// Active and ready first, then done; available quests as hints.
			INT Order[MAX_QUESTS], N = 0;
			for( INT Pass=0; Pass<3; Pass++ )
				for( INT q=0; q<GNumQuests && q<MAX_QUESTS; q++ )
				{
					const INT St = State.QuestState[q];
					if( (Pass==0 && (St==QS_Active || St==QS_Ready)) || (Pass==1 && St==QS_None && State.Level>=GQuests[q].MinLevel) || (Pass==2 && St==QS_Done) )
						Order[N++] = q;
				}
			if( !N )
			{
				Ui.TextWrapped( Ui.FontBody, Body, RpgMuted, TEXT("No quests yet. Look for a gold ! above someone's head and talk to them (E).") );
				return;
			}
			const INT Visible = Max( 1, appFloor(Body.H/RowH) );
			ApplyWheel( N, 1, Visible );
			ClampSelection( N, 1, Visible );
			for( INT v=0; v<Visible && Scroll+v<N; v++ )
			{
				const INT i = Scroll+v;
				const FRpgRect Row( Body.X, Body.Y + v*RowH, ListW, RowH-2*S );
				if( Row.Y+Row.H > Body.Y+Body.H )
					break;
				RowActivated( Row, i, &Order[i] );
				const INT q = Order[i];
				const INT St = State.QuestState[q];
				const FColor C1 = i==Sel ? RpgGoldBright : (St==QS_Done ? RpgMuted : RpgParchment);
				Ui.Text( Ui.FontBody, Row.X+6*S, Row.Y+2*S, C1, *Ui.FitText(Ui.FontBody,GQuests[q].Title,Row.W-12*S) );
				const TCHAR* Tag = St==QS_Active ? TEXT("In progress") : St==QS_Ready ? TEXT("Ready to hand in") : St==QS_Done ? TEXT("Completed") : TEXT("Available");
				Ui.Text( Ui.FontSmall, Row.X+6*S, Row.Y+Ui.TextHeight(Ui.FontBody)+2*S, St==QS_Ready ? RpgGreen : RpgMuted, Tag );
			}
			const INT q = Order[Sel];
			const FQuestDef& Q = GQuests[q];
			FLOAT Y = Detail.Y;
			Ui.Text( Ui.FontItem, Detail.X, Y, RpgGoldBright, *Ui.FitText(Ui.FontItem,Q.Title,Detail.W) );
			Y += Ui.TextHeight( Ui.FontItem );
			Ui.Text( Ui.FontBody, Detail.X, Y, RpgMuted, Q.GiverName );
			Y += Ui.TextHeight( Ui.FontBody ) + 6*S;
			const INT St = State.QuestState[q];
			const FString Goal = St==QS_None ? FString::Printf( TEXT("Talk to %s to start this quest."), Q.GiverName )
				: St==QS_Ready ? FString::Printf( TEXT("Done! Return to %s."), Q.GiverName )
				: St==QS_Done ? FString( Q.Done )
				: FString::Printf( TEXT("%s: %i / %i"), Q.Objective, QuestProgress(Harry,q), Q.Count );
			Ui.TextWrapped( Ui.FontBody, FRpgRect(Detail.X, Y, Detail.W, Ui.TextHeight(Ui.FontBody)*3), RpgParchment, *Goal );
			Y += Ui.TextHeight( Ui.FontBody )*3 + 6*S;
			if( St!=QS_None )
				Ui.TextWrapped( Ui.FontBody, FRpgRect(Detail.X, Y, Detail.W, Detail.Y+Detail.H-Y-Ui.TextHeight(Ui.FontBody)*2), RpgMuted, Q.Intro );
			Ui.Text( Ui.FontBody, Detail.X, Detail.Y+Detail.H-Ui.TextHeight(Ui.FontBody), RpgGreen,
				*FString::Printf(TEXT("Reward: %i beans, %i XP%s"), Q.Beans, Q.Xp, Q.Points ? *FString::Printf(TEXT(", %i points"), Q.Points) : TEXT("")) );
			return;
		}
		// Field Guide.
		const INT N = Min( GNumFieldPages, MAX_PAGES );
		Sel = Clamp( Sel, 0, N-1 );
		const INT Visible = Max( 1, appFloor(Body.H/RowH) );
		ApplyWheel( N, 1, Visible );
		ClampSelection( N, 1, Visible );
		INT Found = 0;
		for( INT g=0; g<N; g++ )
			Found += PageFound( g );
		for( INT v=0; v<Visible && Scroll+v<N; v++ )
		{
			const INT g = Scroll+v;
			const FRpgRect Row( Body.X, Body.Y + v*RowH, ListW, RowH-2*S );
			RowActivated( Row, g, &GFieldPages[g] );
			const UBOOL bHave = PageFound( g );
			Ui.Text( Ui.FontBody, Row.X+6*S, Row.Y+2*S, g==Sel ? RpgGoldBright : (bHave ? RpgParchment : RpgMuted), bHave ? GFieldPages[g].Title : TEXT("Missing page") );
			Ui.Text( Ui.FontSmall, Row.X+6*S, Row.Y+Ui.TextHeight(Ui.FontBody)+2*S, RpgMuted, GFieldPages[g].Place );
		}
		const FFieldPage& Pg = GFieldPages[Sel];
		const FRpgRect Paper( Detail.X, Detail.Y, Detail.W, Detail.H-Ui.TextHeight(Ui.FontBody)-6*S );
		Ui.Rect( Paper, FColor(228,214,180,255) );
		Ui.Border( Paper, Max(1.f,S*1.5f), FColor(150,120,70,255) );
		if( PageFound(Sel) )
		{
			Ui.Text( Ui.FontItem, Paper.X+12*S, Paper.Y+8*S, FColor(90,50,20,255), *Ui.FitText(Ui.FontItem,Pg.Title,Paper.W-24*S), RALIGN_Left, 0 );
			DrawInk( FRpgRect(Paper.X+12*S, Paper.Y+Ui.TextHeight(Ui.FontItem)+16*S, Paper.W-24*S, Paper.H-Ui.TextHeight(Ui.FontItem)-24*S), Pg.Text );
		}
		else
			DrawInk( Paper.Inset(12*S), *FString::Printf(TEXT("This page is still hidden somewhere in the %s. Cast Revelio to find it."), Pg.Place) );
		Ui.Text( Ui.FontBody, Detail.X, Detail.Y+Detail.H-Ui.TextHeight(Ui.FontBody), RpgParchment, *FString::Printf(TEXT("%i / %i pages found - a full set of a place gives a skill point"), Found, N) );
	}

	// Dark ink text wrapped on parchment (no drop shadow).
	void DrawInk( const FRpgRect& R, const TCHAR* Str )
	{
		FString Line, Word;
		FLOAT Y = R.Y;
		const FLOAT LineH = Ui.TextHeight( Ui.FontBody );
		for( const TCHAR* P=Str; ; P++ )
		{
			const UBOOL bEnd = *P==0;
			if( bEnd || *P==' ' || *P=='\n' )
			{
				FString Candidate = Line.Len() ? Line + TEXT(" ") + Word : Word;
				if( Line.Len() && Ui.TextWidth(Ui.FontBody,*Candidate) > R.W )
				{
					Ui.Text( Ui.FontBody, R.X, Y, FColor(50,34,20,255), *Line, RALIGN_Left, 0 );
					Y += LineH;
					Line = Word;
				}
				else
					Line = Candidate;
				Word = TEXT("");
				if( *P=='\n' || bEnd )
				{
					Ui.Text( Ui.FontBody, R.X, Y, FColor(50,34,20,255), *Line, RALIGN_Left, 0 );
					Y += LineH;
					Line = TEXT("");
				}
				if( bEnd || Y > R.Y+R.H-LineH )
					break;
			}
			else
			{
				TCHAR Ch[2] = { *P, 0 };
				Word += Ch;
			}
		}
	}

	/*-------------------------------------------------------------------------
		Photo mode.
	-------------------------------------------------------------------------*/

	AActor* HarryCam( APlayerPawn* Harry )
	{
		return (AActor*)GetObjProp( Harry, TEXT("Cam") );
	}

	void SetPhoto( UGameEngine* Engine, APlayerPawn* Harry, UBOOL bOn )
	{
		if( bOn==bPhoto || !Harry )
			return;
		bPhoto = bOn;
		ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
		AActor* Hud = Harry->myHUD;
		if( bOn )
		{
			Info->Pauser = TEXT("HP2RpgPhoto");
			if( Hud )
				SetBoolProp( Hud, TEXT("bHideStatus"), 1 );
			AActor* Cam = HarryCam( Harry );
			const FVector Off = Cam ? Cam->Location - Harry->Location : FVector(-200.f,0.f,80.f);
			PhotoDist = Clamp( Off.Size(), 80.f, 600.f );
			PhotoYaw = (FLOAT)appAtan2( Off.Y, Off.X );
			PhotoPitch = (FLOAT)appAtan2( Off.Z, Max(1.f,Off.Size2D()) );
			bPhotoHint = 1;
			RpgLog( TEXT("photo mode on (camera %s)"), Cam ? Cam->GetName() : TEXT("none") );
		}
		else
		{
			if( Info->Pauser==TEXT("HP2RpgPhoto") )
				Info->Pauser = TEXT("");
			if( Hud )
				SetBoolProp( Hud, TEXT("bHideStatus"), 0 );
		}
	}

	// Keeps the paused camera on the orbit the mouse sets.
	void UpdatePhoto( APlayerPawn* Harry )
	{
		AActor* Cam = HarryCam( Harry );
		if( !bPhoto || !Cam )
			return;
		const FVector Focus = Harry->Location + FVector( 0, 0, Harry->CollisionHeight*0.6f );
		const FVector Off( (FLOAT)(appCos(PhotoYaw)*appCos(PhotoPitch)), (FLOAT)(appSin(PhotoYaw)*appCos(PhotoPitch)), (FLOAT)appSin(PhotoPitch) );
		Cam->Location = Focus + Off*PhotoDist;
		Cam->Rotation = (-Off).Rotation();
	}

	UBOOL PhotoInput( UGameEngine* Engine, APlayerPawn* Harry, EInputKey Key, EInputAction Action, FLOAT Delta )
	{
		if( !bPhoto )
			return 0;
		if( Action==IST_Axis )
		{
			if( Key==IK_MouseX )
				PhotoYaw += Delta*0.004f;
			else if( Key==IK_MouseY )
				PhotoPitch = Clamp( PhotoPitch - Delta*0.004f, -1.2f, 1.3f );
			return 1;
		}
		if( Action!=IST_Press )
			return 1;
		if( Key==IK_P || Key==IK_Escape )
			SetPhoto( Engine, Harry, 0 );
		else if( Key==IK_MouseWheelUp )
			PhotoDist = Max( 60.f, PhotoDist-20.f );
		else if( Key==IK_MouseWheelDown )
			PhotoDist = Min( 900.f, PhotoDist+20.f );
		else if( Key>=IK_1 && Key<=IK_4 )
			PhotoFilter = Key-IK_1;
		else if( Key==IK_H )
			bPhotoHint = !bPhotoHint;
		return 1;
	}

	void DrawPhoto()
	{
		UCanvas* C = Ui.C;
		const FRpgRect Full( 0, 0, C->ClipX, C->ClipY );
		switch( PhotoFilter )
		{
			case 1: // Sepia: warm multiply, then a faint warm lift.
				C->DrawTile( Ui.White, 0, 0, C->ClipX, C->ClipY, 0, 0, 1, 1, NULL, 1.f, FPlane(1.f,0.84f,0.62f,1.f), FPlane(0,0,0,0), PF_Modulated );
				C->DrawTile( Ui.White, 0, 0, C->ClipX, C->ClipY, 0, 0, 1, 1, NULL, 1.f, FPlane(0.08f,0.05f,0.02f,1.f), FPlane(0,0,0,0), PF_Translucent );
				break;
			case 2: // Daily Prophet: strong sepia and dark edges.
				C->DrawTile( Ui.White, 0, 0, C->ClipX, C->ClipY, 0, 0, 1, 1, NULL, 1.f, FPlane(0.95f,0.82f,0.6f,1.f), FPlane(0,0,0,0), PF_Modulated );
				Ui.Scrim( FRpgRect(0, 0, C->ClipX, C->ClipY*0.08f), 0.4f );
				Ui.Scrim( FRpgRect(0, C->ClipY*0.92f, C->ClipX, C->ClipY*0.08f), 0.4f );
				Ui.Scrim( FRpgRect(0, 0, C->ClipX*0.06f, C->ClipY), 0.5f );
				Ui.Scrim( FRpgRect(C->ClipX*0.94f, 0, C->ClipX*0.06f, C->ClipY), 0.5f );
				break;
			case 3: // Moonlight.
				C->DrawTile( Ui.White, 0, 0, C->ClipX, C->ClipY, 0, 0, 1, 1, NULL, 1.f, FPlane(0.62f,0.72f,1.f,1.f), FPlane(0,0,0,0), PF_Modulated );
				break;
		}
		if( bPhotoHint )
		{
			static const TCHAR* Keys[5] = { TEXT("Mouse"), TEXT("Wheel"), TEXT("1-4"), TEXT("H"), TEXT("P") };
			static const TCHAR* Verbs[5] = { TEXT("Orbit"), TEXT("Zoom"), TEXT("Filter"), TEXT("Hide help"), TEXT("Exit") };
			Ui.Prompt( C->ClipX*0.5f, C->ClipY-Ui.P(72.f), Keys, Verbs, 5, TEXT("Photo mode") );
		}
	}

	/*-------------------------------------------------------------------------
		Difficulty (chosen in the Status tab).
	-------------------------------------------------------------------------*/

	INT DifficultyDamagePct()
	{
		return GDifficultyDamage[ Clamp(State.Difficulty, 0, DIFF_MAX-1) ];
	}


	/*-------------------------------------------------------------------------
		Duelling Club ladder: the retail duel ranks (Duel01-Duel10) with
		titles and a first-win reward for every rank.
	-------------------------------------------------------------------------*/

	static const TCHAR* DuelTitle( INT Rank )
	{
		static const TCHAR* Titles[11] = { TEXT("Unranked"), TEXT("Novice"), TEXT("Apprentice"), TEXT("Contender"), TEXT("Spellslinger"), TEXT("Duellist"),
			TEXT("Adept"), TEXT("Master Duellist"), TEXT("Champion"), TEXT("Grand Champion"), TEXT("Legend of the Club") };
		return Titles[ Clamp(Rank,0,10) ];
	}

	void DuelWon()
	{
		const TCHAR* Map = LastLevel ? *LastLevel->URL.Map : TEXT("");
		const TCHAR* P = appStrstr( Map, TEXT("Duel") );
		if( !P )
			P = appStrstr( Map, TEXT("duel") );
		const INT Rank = P ? appAtoi( P+4 ) : 0;
		if( Rank<1 || Rank>10 || Rank<=State.DuelRank )
			return;
		State.DuelRank = Rank;
		APlayerPawn* Harry = CurrentHarry();
		const INT Beans = 25*Rank, Xp = 40*Rank;
		GiveBeans( Harry, Beans );
		GiveXp( Xp, TEXT("new duelling rank") );
		AddHousePoints( Harry, HOUSE_Gryffindor, 5 + Rank );
		AddFeed( FString::Printf(TEXT("Rank %i: %s"), Rank, DuelTitle(Rank)), FColor(255,214,100,255), TOAST_Level, *FString::Printf(TEXT("Duelling Club  +%i beans, +%i points"), Beans, 5+Rank) );
		RpgLog( TEXT("duel rank %i beaten"), Rank );
	}

	void DrawDuelLadder( const FRpgRect& Body )
	{
		const FLOAT S = Ui.S;
		const FLOAT RowH = (Body.H - Ui.TextHeight(Ui.FontBody)*2) / 10.f;
		Ui.Text( Ui.FontBody, Body.X, Body.Y, RpgParchment, *FString::Printf(TEXT("Your title: %s"), DuelTitle(State.DuelRank)) );
		for( INT r=1; r<=10; r++ )
		{
			const FLOAT Y = Body.Y + Ui.TextHeight(Ui.FontBody)*1.4f + (r-1)*RowH;
			const UBOOL bBeaten = r<=State.DuelRank, bNext = r==State.DuelRank+1;
			const FRpgRect Row( Body.X, Y, Body.W, RowH-2*S );
			Ui.Tile( Row, bNext, 0, bBeaten ? FColor(120,220,140,255) : (bNext ? FColor(255,222,130,255) : FColor(46,56,96,255)) );
			Ui.Text( Ui.FontModernBold, Row.X+10*S, Row.Y+(Row.H-Ui.TextHeight(Ui.FontModernBold))*0.5f, bBeaten ? FColor(120,220,140,255) : (bNext ? FColor(255,222,130,255) : FColor(182,186,206,255)),
				*FString::Printf(TEXT("Rank %i  %s"), r, DuelTitle(r)), RALIGN_Left, 0 );
			Ui.Text( Ui.FontModern, Row.X+Row.W-10*S, Row.Y+(Row.H-Ui.TextHeight(Ui.FontModern))*0.5f, bBeaten ? FColor(120,220,140,255) : FColor(182,186,206,255),
				bBeaten ? TEXT("Beaten") : *FString::Printf(TEXT("First win: %i beans, %i XP"), 25*r, 40*r), RALIGN_Right, 0 );
		}
		Ui.Text( Ui.FontSmall, Body.X, Body.Y+Body.H-Ui.TextHeight(Ui.FontSmall), RpgMuted, TEXT("Challenge the next rank at the Duelling Club kiosk in the Entrance Hall.") );
	}

