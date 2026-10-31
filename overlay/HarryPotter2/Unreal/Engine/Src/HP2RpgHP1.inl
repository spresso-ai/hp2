/*=============================================================================
	HP2RpgHP1.inl: content from Harry Potter 1 (PC), installed next to the
	HP2 data by hp1/hp1_install.py: its voice lines, characters and areas.
	Included inside FHP2Rpg. Everything here does nothing when the HP1
	packages are not installed.
=============================================================================*/

	/*-------------------------------------------------------------------------
		Voice lines: H1AllDialog sounds with their HP1 subtitles.
	-------------------------------------------------------------------------*/

	INT H1Next[H1_MAX];          // rotation per category
	INT H1Have;                  // 0 unknown, 1 installed, 2 not installed

	UBOOL HasHP1()
	{
		if( !H1Have )
			H1Have = FRpgUi::LoadRooted( USound::StaticClass(), TEXT("H1AllDialog.111DumbledoreInfo1") ) ? 1 : 2;
		return H1Have==1;
	}

	static UBOOL H1Fits( const FH1Line& L, INT Cat, const TCHAR* Filter )
	{
		if( L.Cat!=Cat )
			return 0;
		if( !Filter )
			return 1;
		TCHAR Word[32];
		for( INT w=0; ListToken(Filter, w, Word, (INT)ARRAY_COUNT(Word)); w++ )
			if( appStrfind(L.Text, Word) )
				return 1;
		return 0;
	}

	// Cosmetics made from Harry Potter 1 models are only sold when it is installed.
	UBOOL CosmeticAvailable( INT i )
	{
		const TCHAR* M = GCosmetics[i].Mesh;
		const UBOOL bHP1 = M && (!appStrnicmp(M,TEXT("HarryPotter."),12) || !appStrnicmp(M,TEXT("H1Models."),9) || !appStrnicmp(M,TEXT("H1Props."),8));
		return !bHP1 || HasHP1();
	}

	// The next line of a category, stepping through it in a scrambled order;
	// Filter ('|' words) keeps only lines whose text has one of them.
	const FH1Line* NextH1Line( INT Cat, const TCHAR* Filter=NULL )
	{
		INT Count = 0;
		for( INT i=0; i<GNumH1Lines; i++ )
			if( H1Fits(GH1Lines[i], Cat, Filter) )
				Count++;
		if( !Count )
			return NULL;
		const INT Want = (H1Next[Cat]++ * 7 + Cat*3) % Count;
		for( INT i=0, n=0; i<GNumH1Lines; i++ )
			if( H1Fits(GH1Lines[i], Cat, Filter) && n++==Want )
				return &GH1Lines[i];
		return NULL;
	}

	// Speaks a Harry Potter 1 line on Speaker: the voice from the speaker's
	// position and the subtitle in the dialogue panel. Returns its length.
	FLOAT SpeakH1( AActor* Speaker, const TCHAR* Name, INT Cat, UBOOL bSubtitle=1, const TCHAR* Filter=NULL )
	{
		if( !Speaker || Cat<0 || !HasHP1() || !Ui.Engine || !Ui.Engine->Audio )
			return 0.f;
		const FH1Line* L = NextH1Line( Cat, Filter );
		if( !L )
			return 0.f;
		USound* Sound = (USound*)FRpgUi::LoadRooted( USound::StaticClass(), *FString::Printf(TEXT("H1AllDialog.%s"), L->Sound) );
		if( !Sound )
			return 0.f;
		Ui.Engine->Audio->PlaySound( Speaker, (INT)Speaker->GetIndex()*16 + SLOT_Talk*2, Sound, Speaker->Location, 1.f, GAudioDefaultRadius*1.5f, 1.f, 0, 0.f );
		const FLOAT Secs = Clamp( Sound->Duration>0.f ? Sound->Duration : 2.5f, 0.6f, 12.f );
		if( bSubtitle && L->Text[0] )
		{
			Say( Name, L->Text );
			DialogLeft = Max( DialogLeft, Secs + 0.8f );
		}
		MateVoiceFree = RpgClock + Secs + 1.5f;
		static const TCHAR* Talks[4] = { TEXT("talk_bothhands"), TEXT("talkbothhands"), TEXT("talk_rhand"), TEXT("talk1") };
		for( INT t=0; t<4 && L->Text[0] && !Speaker->IsA(APlayerPawn::StaticClass()); t++ )
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

	// Who in HP2 speaks with which HP1 voice when hit or talked to.
	INT H1EmotiveFor( AActor* A )
	{
		if( !A )
			return -1;
		if( ClassChainIsAny(A,TEXT("Ron|RonWeasley")) )   return H1_EmoRon;
		if( ClassChainIsAny(A,TEXT("Peeves")) )           return H1_EmoPeeves;
		if( ClassChainIsAny(A,TEXT("ProfSnape|Snape")) )  return H1_EmoSnape;
		if( ClassChainIsAny(A,TEXT("Filch")) )            return H1_EmoFilch;
		if( ClassChainIsAny(A,TEXT("ProfFlitwick")) )     return H1_EmoFlitwick;
		return -1;
	}

	INT H1TalkFor( AActor* A )
	{
		if( !A )
			return -1;
		if( ClassChainIsAny(A,TEXT("NearlyHeadlessNick|Nick")) ) return H1_Nick;
		if( ClassChainIsAny(A,TEXT("Peeves")) )                  return H1_Peeves;
		if( ClassChainIsAny(A,TEXT("Neville|NevilleLongbottom")) ) return H1_Neville;
		if( ClassChainIsAny(A,TEXT("ProfDumbleDore|Dumbledore")) ) return H1_Dumbledore;
		if( ClassChainIsAny(A,TEXT("Filch")) )                   return H1_Filch;
		if( ClassChainIsAny(A,TEXT("Draco|DracoMalfoy")) )       return H1_Malfoy;
		if( ClassChainIsAny(A,TEXT("Percy|SlytherinPrefect")) )  return H1_Prefect;
		if( IsStudent(A) )                                       return H1_Student;
		return -1;
	}

	// E on someone with HP1 lines: every other talk uses them, so the HP2
	// lines still come up. Returns 1 if a line was spoken.
	UBOOL TalkH1( AActor* Npc )
	{
		const INT Cat = H1TalkFor( Npc );
		if( Cat<0 || RpgClock<MateVoiceFree || (H1Next[Cat] & 1 && Cat!=H1_Nick && Cat!=H1_Student) )
		{
			if( Cat>=0 )
				H1Next[Cat]++;
			return 0;
		}
		return SpeakH1( Npc, *NpcName(Npc), Cat )>0.f;
	}

	// Students in the hubs chat to themselves now and then.
	FLOAT H1ChatterAt;

	void UpdateH1Chatter( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !HasHP1() || RpgClock<H1ChatterAt || RpgClock<MateVoiceFree || IsTalking() || !IsFreeGameplay(Engine,Harry) )
			return;
		H1ChatterAt = RpgClock + 50.f + (HashMix((DWORD)State.Day, (DWORD)(RpgClock*10.f)) % 50u);
		AActor* Best = NULL;
		FLOAT BestD = 700.f*700.f;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden || !A->IsA(APawn::StaticClass()) || !IsStudent(A) || !IsOutAndAbout(Engine,A) )
				continue;
			const FLOAT D = (A->Location - Harry->Location).SizeSquared();
			if( D>200.f*200.f && D<BestD )
			{
				Best = A;
				BestD = D;
			}
		}
		if( Best )
			SpeakH1( Best, *NpcName(Best), H1_Student );
	}

	void ForgetH1()
	{
		H1ChatterAt = RpgClock + 25.f;
	}

	/*-------------------------------------------------------------------------
		Areas of the 1991 castle: Harry Potter 1 levels, reached through
		the Great Hall doors in the Entrance Hall (R picks where they open
		onto) and left again where Harry arrives. HP1's own collectibles and
		creatures don't know HP2's Harry, so the mod hands them out and
		lets spells defeat them.
	-------------------------------------------------------------------------*/

	struct FH1Area { const TCHAR* Map; const TCHAR* Name; };

	static const FH1Area* H1Areas( INT& Count )
	{
		static const FH1Area Areas[] =
		{
			{ TEXT("Lev_Tut1"),        TEXT("Entrance Hall, 1991") },
			{ TEXT("Lev3_Intro"),      TEXT("Halloween Feast Corridors") },
			{ TEXT("Lev3_PreDungeon"), TEXT("Way to the Dungeons") },
			{ TEXT("Lev3_Dungeon"),    TEXT("The Old Dungeons") },
			{ TEXT("Snapes_Office"),   TEXT("Snape's Office") },
			{ TEXT("Lev_Tut3"),        TEXT("Charms Corridor") },
			{ TEXT("Lev3_Lumos"),      TEXT("Lumos Corridors") },
			{ TEXT("Lev4_Sneak"),      TEXT("The Library at Night") },
			{ TEXT("Lev5_fluffy"),     TEXT("Third-Floor Corridor") },
			{ TEXT("Lev2_HogFront"),   TEXT("Castle Courtyard") },
			{ TEXT("Lev2_Inc_A"),      TEXT("Greenhouses") },
			{ TEXT("Lev2_fire1"),      TEXT("Hagrid's Hut") },
		};
		Count = (INT)ARRAY_COUNT(Areas);
		return Areas;
	}

	static INT H1AreaIndex( const TCHAR* Map )
	{
		INT Count;
		const FH1Area* Areas = H1Areas( Count );
		FString Base( Map );
		INT Dot = Base.InStr( TEXT(".") );
		if( Dot>=0 )
			Base = Base.Left( Dot );
		for( INT i=0; i<Count; i++ )
			if( appStricmp(*Base,Areas[i].Map)==0 )
				return i;
		return -1;
	}

	// Any Harry Potter 1 level, not just the ones the doors lead to.
	static UBOOL IsH1Map( const TCHAR* Map )
	{
		return H1AreaIndex(Map)>=0 || MapHas(Map,TEXT("Lev2_")) || MapHas(Map,TEXT("Lev3_")) || MapHas(Map,TEXT("Lev4_"))
			|| MapHas(Map,TEXT("Lev5_")) || MapHas(Map,TEXT("Lev_Tut")) || MapHas(Map,TEXT("Quid_")) || MapHas(Map,TEXT("Snapes_Office"));
	}

	UBOOL InH1Area()
	{
		return LastLevel && IsH1Map( *LastLevel->URL.Map );
	}

	// Where the doors lead: 0 the Entrance Hall, 1 the fan map, 2.. HP1 areas.
	INT DoorDest;
	FVector H1Start;

	INT NumDoorDests()
	{
		INT Count;
		H1Areas( Count );
		return 2 + (HasHP1() ? Count : 0);
	}

	const TCHAR* DoorDestMap( INT d )
	{
		INT Count;
		const FH1Area* Areas = H1Areas( Count );
		if( d<=0 ) return TEXT("Entryhall_hub");
		if( d==1 ) return TEXT("AverageDayHogwarts");
		return Areas[Clamp(d-2,0,Count-1)].Map;
	}

	const TCHAR* DoorDestName( INT d )
	{
		INT Count;
		const FH1Area* Areas = H1Areas( Count );
		if( d<=0 ) return TEXT("Entrance Hall");
		if( d==1 ) return TEXT("Average Day Through Hogwarts");
		return Areas[Clamp(d-2,0,Count-1)].Name;
	}

	UBOOL DoorDestValid( APlayerPawn* Harry, INT d )
	{
		if( d<0 || d>=NumDoorDests() )
			return 0;
		if( d==0 )
			return !AtGreatHallDoor( Harry );          // from the hall it is the hall itself
		return !(LastLevel && MapHas( *LastLevel->URL.Map, DoorDestMap(d) ));
	}

	void FixDoorDest( APlayerPawn* Harry )
	{
		for( INT n=0; n<NumDoorDests() && !DoorDestValid(Harry,DoorDest); n++ )
			DoorDest = (DoorDest+1) % NumDoorDests();
	}

	void NextDoorDest( APlayerPawn* Harry )
	{
		DoorDest = (DoorDest+1) % NumDoorDests();
		FixDoorDest( Harry );
		Ui.Play( RSND_Click, Harry );
	}

	UBOOL AtH1Start( APlayerPawn* Harry )
	{
		return InH1Area() && !H1Start.IsZero() && (Harry->Location-H1Start).SizeSquared() < 200.f*200.f;
	}

	/*-------------------------------------------------------------------------
		Inside an HP1 level.
	-------------------------------------------------------------------------*/

	UBOOL bH1Prepared;
	FLOAT H1CheckAt;

	FString H1Key( AActor* A )
	{
		return FString::Printf( TEXT("h1:%s:%s"), LastLevel ? *LastLevel->URL.Map : TEXT(""), A->GetName() );
	}

	// HP1's own Harry stands in every level; HP2's Harry is the one played.
	UBOOL IsH1Harry( AActor* A )
	{
		return A && A!=CurrentHarry() && A->GetClass()->GetOuter() && appStricmp( A->GetClass()->GetOuter()->GetName(), TEXT("HGame") )!=0
			&& ClassChainIsAny( A, TEXT("baseHarry") );
	}

	static INT H1PickupKind( AActor* A )
	{
		// 1 bean, 2 chocolate frog, 3 wizard card, 4 challenge star
		const TCHAR* N = A->GetClass()->GetName();
		const TCHAR* Pkg = A->GetClass()->GetOuter() ? A->GetClass()->GetOuter()->GetName() : TEXT("");
		if( appStricmp(Pkg,TEXT("H1Props"))!=0 && appStricmp(Pkg,TEXT("HarryPotter"))!=0 )
			return 0;
		if( appStrfind(N,TEXT("Bean")) )                               return 1;
		if( appStricmp(N,TEXT("ChocolateFrog"))==0 )                   return 2;
		if( !appStrnicmp(N,TEXT("WC"),2) && appStrlen(N)>3 )          return 3;
		if( appStricmp(N,TEXT("Star"))==0 )                            return 4;
		return 0;
	}

	// Creatures HP1 placed: the bestiary ones plus snails and Devil's Snare.
	INT H1CreatureType( AActor* A )
	{
		const INT T = CreatureType( A );
		if( T>=0 )
			return T;
		return ClassChainIsAny( A, TEXT("Snail|DevilsSnare|DevilsSnareNew") ) ? CR_MAX : -1;
	}

	UBOOL IsH1Person( AActor* A )
	{
		return InH1Area() && A && !A->bHidden && A->IsA(APawn::StaticClass()) && !A->IsA(APlayerPawn::StaticClass())
			&& ClassChainIsAny( A, TEXT("baseChar") ) && !IsH1Harry(A) && H1CreatureType(A)<0
			&& !ClassChainIsAny( A, TEXT("baseBoss|QuidPlayer|QuidditchCrowd|ChessPiece") );
	}

	FString H1PersonName( AActor* A )
	{
		static const TCHAR* Names[][2] =
		{
			{ TEXT("Hermione"), TEXT("Hermione Granger") }, { TEXT("Ron"), TEXT("Ron Weasley") }, { TEXT("Fred"), TEXT("Fred Weasley") },
			{ TEXT("George"), TEXT("George Weasley") }, { TEXT("Neville"), TEXT("Neville Longbottom") }, { TEXT("NHNick"), TEXT("Nearly Headless Nick") },
			{ TEXT("Peeves"), TEXT("Peeves") }, { TEXT("hagrid"), TEXT("Hagrid") }, { TEXT("Filch"), TEXT("Argus Filch") },
			{ TEXT("McGonagall"), TEXT("Professor McGonagall") }, { TEXT("Snape"), TEXT("Professor Snape") }, { TEXT("Flitwick"), TEXT("Professor Flitwick") },
			{ TEXT("Sprout"), TEXT("Professor Sprout") }, { TEXT("Quirrell"), TEXT("Professor Quirrell") }, { TEXT("Dumbledore"), TEXT("Professor Dumbledore") },
			{ TEXT("Malfoy"), TEXT("Draco Malfoy") }, { TEXT("Draco"), TEXT("Draco Malfoy") }, { TEXT("Crabbe"), TEXT("Crabbe") },
			{ TEXT("Goyle"), TEXT("Goyle") }, { TEXT("Percy"), TEXT("Percy Weasley") }, { TEXT("Hooch"), TEXT("Madam Hooch") },
			{ TEXT("BloodyBaron"), TEXT("The Bloody Baron") }, { TEXT("barron"), TEXT("The Bloody Baron") }, { TEXT("gen_"), TEXT("Student") },
		};
		for( UClass* C=A->GetClass(); C; C=C->GetSuperClass() )
			for( INT i=0; i<(INT)ARRAY_COUNT(Names); i++ )
				if( appStrfind( C->GetName(), Names[i][0] ) )
					return Names[i][1];
		return FString();
	}

	// What the 1991 people say to HP2's Harry when they have no HP1 line.
	const TCHAR* H1WrittenLine( const FString& Who )
	{
		if( Who==TEXT("Hermione Granger") )   return TEXT("Have you read Hogwarts: A History? It's all in there, you know.");
		if( Who==TEXT("Ron Weasley") )        return TEXT("Blimey, Harry, this place feels like our first year all over again.");
		if( Who==TEXT("Fred Weasley") || Who==TEXT("George Weasley") ) return TEXT("Every bean in this castle is ours. Mostly.");
		if( Who==TEXT("Hagrid") )             return TEXT("Mind yerself, Harry. Some o' these old corridors aren't used no more.");
		if( Who==TEXT("Professor McGonagall") ) return TEXT("Potter. I trust you have a good reason to be in this part of the castle.");
		if( Who==TEXT("Professor Quirrell") ) return TEXT("P-P-Potter! W-what a surprise...");
		if( Who==TEXT("Professor Snape") )    return TEXT("Wandering again, Potter? How very like your father.");
		if( Who==TEXT("Professor Flitwick") || Who==TEXT("Professor Sprout") ) return TEXT("Hello, Harry! Lovely to see you down here.");
		return TEXT("Hello, Harry.");
	}

	// E on someone in an HP1 level.
	UBOOL TalkH1Person( AActor* Npc )
	{
		if( !IsH1Person(Npc) )
			return 0;
		const FString Who = H1PersonName( Npc );
		INT Cat = -1;
		if( Who==TEXT("Student") )                  Cat = H1_Student;
		else if( Who==TEXT("Nearly Headless Nick") ) Cat = H1_Nick;
		else if( Who==TEXT("Peeves") )               Cat = H1_Peeves;
		else if( Who==TEXT("Neville Longbottom") )   Cat = H1_Neville;
		else if( Who==TEXT("Professor Dumbledore") ) Cat = H1_Dumbledore;
		else if( Who==TEXT("Argus Filch") )          Cat = H1_Filch;
		else if( Who==TEXT("Draco Malfoy") )         Cat = H1_Malfoy;
		else if( Who==TEXT("Percy Weasley") )        Cat = H1_Prefect;
		if( Cat<0 || SpeakH1( Npc, Who.Len() ? *Who : TEXT("Student"), Cat )<=0.f )
			Say( Who.Len() ? *Who : TEXT("Student"), H1WrittenLine(Who) );
		return 1;
	}

	void PrepareH1Area( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bH1Prepared = 1;
		H1Start = Harry->Location;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe )
				continue;
			if( A->IsA(APlayerStart::StaticClass()) && (A->Location-Harry->Location).SizeSquared() < 400.f*400.f )
				H1Start = A->Location;
			// HP1's Harry, and its save books (they save HP1's own way).
			if( IsH1Harry(A) || ClassChainIsAny(A,TEXT("savepoint")) )
			{
				A->bHidden = 1;
				A->SetCollision( 0, 0, 0 );
			}
			// Taken on an earlier visit.
			if( H1PickupKind(A) && OpenedTreasure.FindItemIndex(H1Key(A))!=INDEX_NONE )
				DestroyActorSafe( Engine, A );
		}
		RpgLog( TEXT("hp1 area %s ready"), LastLevel ? *LastLevel->URL.Map : TEXT("?") );
	}

	void CollectH1( UGameEngine* Engine, APlayerPawn* Harry, AActor* A, INT Kind )
	{
		OpenedTreasure.AddItem( H1Key(A) );
		const FVector At = A->Location;
		switch( Kind )
		{
			case 1:
				GiveBeans( Harry, 1 );
				break;
			case 2:
				GiveBeans( Harry, 3 );
				Harry->Health = Min( Harry->Health + 15, Max(Harry->Health, 100) );
				AddFeed( TEXT("Chocolate Frog"), FColor(205,150,90,255), TOAST_Reward, TEXT("+15 health") );
				break;
			case 3:
			{
				State.Counters[CNT_Cards]++;
				GiveXp( 40, TEXT("Famous Wizard card") );
				FString Name = A->GetClass()->GetName() + 2;
				AddFeed( FString::Printf(TEXT("Famous Wizard: %s"), *Name), FColor(255,222,130,255), TOAST_Reward, TEXT("A card from 1991") );
				break;
			}
			case 4:
				GiveXp( 25, TEXT("Challenge star") );
				AddFeed( TEXT("Challenge Star"), FColor(255,222,130,255), TOAST_Reward, TEXT("+25 XP") );
				break;
		}
		SpawnFx( Engine, TEXT("WizCrackSparkle"), At, 1.f );
		Ui.Play( RSND_Buy, Harry );
		DestroyActorSafe( Engine, A );
	}

	void UpdateH1Area( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !InH1Area() )
			return;
		if( !bH1Prepared )
			PrepareH1Area( Engine, Harry );
		if( RpgClock<H1CheckAt )
			return;
		H1CheckAt = RpgClock + 0.1f;
		ULevel* Level = Engine->GLevel;
		for( INT i=0; i<Level->Actors.Num(); i++ )
		{
			AActor* A = Level->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden )
				continue;
			// Pickups Harry walks into.
			if( const INT Kind = H1PickupKind(A) )
			{
				const FVector D = A->Location - Harry->Location;
				if( D.X*D.X + D.Y*D.Y < 70.f*70.f && Abs(D.Z) < 90.f )
					CollectH1( Engine, Harry, A, Kind );
				continue;
			}
			// Harry's spells defeat HP1's creatures.
			if( !IsHarrysSpell(A,Harry) || !ClassChainIsAny(A,TEXT("baseSpell")) )
				continue;
			for( INT j=0; j<Level->Actors.Num(); j++ )
			{
				AActor* C = Level->Actors(j);
				if( !C || C->bDeleteMe || C->bHidden || !C->IsA(APawn::StaticClass()) )
					continue;
				const INT Type = H1CreatureType( C );
				if( Type<0 )
					continue;
				const FVector D = C->Location - A->Location;
				const FLOAT Reach = C->CollisionRadius + 40.f;
				if( D.X*D.X + D.Y*D.Y > Reach*Reach || Abs(D.Z) > C->CollisionHeight + 40.f )
					continue;
				if( Type<CR_MAX )
					CreditKill( C, Type );
				else if( !TestAndSet(C,AF_Rewarded) )
					GiveXp( 15, TEXT("creature defeated") );
				SpawnFx( Engine, TEXT("WizCrackSparkle"), C->Location, 1.f );
				if( Type!=CR_SpikyPlant && Type!=CR_Tentacula )
					QueueBeans( C->Location, 1 );
				DestroyActorSafe( Engine, C );
				DestroyActorSafe( Engine, A );
				break;
			}
		}
	}

	void ForgetH1Area()
	{
		bH1Prepared = 0;
		H1CheckAt = 0.f;
		H1Start = FVector( 0, 0, 0 );
	}
