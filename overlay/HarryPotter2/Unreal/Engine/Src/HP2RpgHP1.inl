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
	static UBOOL IsHP1Asset( const TCHAR* M )
	{
		return M && (!appStrnicmp(M,TEXT("HarryPotter."),12) || !appStrnicmp(M,TEXT("H1Models."),9) || !appStrnicmp(M,TEXT("H1Props."),8));
	}

	UBOOL CosmeticAvailable( INT i )
	{
		return !IsHP1Asset(GCosmetics[i].Mesh) || HasHP1();
	}

	UBOOL QuestAvailable( INT q )
	{
		return !IsHP1Asset(GQuests[q].Mesh) || HasHP1();
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
