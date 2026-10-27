/*=============================================================================
	HP2RpgWorld.inl: Living-world touches of FHP2Rpg.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	- Owls circle over the school grounds and hoot now and then.
	- Troublemakers: a few Slytherin students get up to mischief. A teacher
	  or prefect nearby catches them (points off Slytherin); Harry can teach
	  them a lesson with Flipendo, Leviosa or Transfiguration.
	- Students react when Harry walks past: friendly houses wave or cheer,
	  hostile ones jeer. Reactions use the game's own student animations and
	  a short soft freeze (Role = ROLE_DumbProxy) so the AI does not override
	  them; every change is undone before saves and level changes.
=============================================================================*/

	struct FOwl { AActor* Actor; FLOAT Angle; FLOAT Radius; FLOAT Height; FLOAT Speed; FVector Centre; };
	struct FReaction { AActor* Actor; BYTE OldRole; FLOAT Left; FName OldSeq; UBOOL bOldLoop; };
	struct FReactCooldown { AActor* Actor; FName Name; FLOAT Until; };

	TArray<FOwl> Owls;
	TArray<FReaction> Reactions;
	TArray<FReactCooldown> ReactCooldowns;
	FLOAT HootTimer;
	FLOAT ReactPollTimer;
	UBOOL bOwlsSpawned;

	/*-------------------------------------------------------------------------
		Owls.
	-------------------------------------------------------------------------*/

	void SpawnOwls( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bOwlsSpawned = 1;
		const TCHAR* Map = Engine->GLevel->URL.Map.Len() ? *Engine->GLevel->URL.Map : TEXT("");
		if( !MapHas(Map,TEXT("Grounds")) )
			return; // indoor hubs would clip owls through the ceilings
		UMesh* Mesh = LoadMesh( TEXT("HPModels.skowlbarnMesh") );
		if( !Mesh )
			return;
		for( INT o=0; o<2; o++ )
		{
			AActor* A = SpawnRenderActor( Engine, NULL, Harry->Location );
			if( !A )
				continue;
			A->DrawType = DT_Mesh;
			A->Mesh = Mesh;
			A->DrawScale = 0.55f;
			A->AmbientGlow = 40;
			A->PlayAnim( FName(TEXT("Fly")), true, 1.f, 0.f );
			FOwl* Owl = new(Owls) FOwl;
			Owl->Actor = A;
			Owl->Angle = o*3.1f;
			Owl->Radius = 900.f + o*450.f;
			Owl->Height = 650.f + o*180.f;
			Owl->Speed = o ? -0.22f : 0.3f;
			Owl->Centre = Harry->Location;
		}
		RpgLog( TEXT("owls: %i"), Owls.Num() );
	}

	void UpdateOwls( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( !bOwlsSpawned )
			SpawnOwls( Engine, Harry );
		for( INT o=Owls.Num()-1; o>=0; o-- )
		{
			FOwl& Owl = Owls(o);
			if( !IsInLevel(Owl.Actor) )
			{
				Owls.Remove( o );
				continue;
			}
			// Drift the circle's centre slowly toward Harry.
			Owl.Centre += (Harry->Location - Owl.Centre)*Min( 1.f, DeltaSeconds*0.05f );
			Owl.Angle += Owl.Speed*DeltaSeconds;
			const FVector P = Owl.Centre + FVector( (FLOAT)appCos(Owl.Angle)*Owl.Radius, (FLOAT)appSin(Owl.Angle)*Owl.Radius, Owl.Height + (FLOAT)appSin(Owl.Angle*3.f)*40.f );
			Engine->GLevel->FarMoveActor( Owl.Actor, P, 0, 1 );
			// Face along the flight direction.
			const FLOAT Heading = Owl.Angle + (Owl.Speed>0.f ? PI*0.5f : -PI*0.5f);
			Owl.Actor->Rotation = FRotator( 0, appRound(Heading*32768.f/PI), 0 );
			if( Owl.Actor->AnimSequence==NAME_None )
				Owl.Actor->PlayAnim( FName(TEXT("Fly")), true, 1.f, 0.f );
		}
		if( Owls.Num() )
		{
			HootTimer -= DeltaSeconds;
			if( HootTimer<=0.f )
			{
				HootTimer = 14.f + RpgRand(16);
				PlaySfx( RpgRand(2) ? TEXT("HPSounds.Critters_sfx.owl_hoot01") : TEXT("HPSounds.Critters_sfx.owl_hoot02") );
			}
		}
	}


	/*-------------------------------------------------------------------------
		Troublemakers.
	-------------------------------------------------------------------------*/

	struct FTroublemaker
	{
		AActor* Actor;
		FName Name;
		FLOAT NextMischief;   // RpgClock time
		FLOAT RedHandedUntil; // caught in the act until then
		FLOAT LessonUntil;    // taught a lesson: quiet until then
		UBOOL bCaughtChecked;
	};
	TArray<FTroublemaker> Troublemakers;
	UBOOL bTroublePicked;
	FLOAT TroublePickTime;

	FTroublemaker* FindTroublemaker( AActor* A )
	{
		if( !A )
			return NULL;
		for( INT t=0; t<Troublemakers.Num(); t++ )
			if( Troublemakers(t).Actor==A && Troublemakers(t).Name==A->GetFName() )
				return &Troublemakers(t);
		return NULL;
	}

	UBOOL IsTroublemaker( AActor* A )
	{
		return FindTroublemaker( A )!=NULL;
	}

	UBOOL IsStaff( AActor* A )
	{
		return ClassChainIsAny( A, GStaffClasses );
	}

	FString StaffName( AActor* A )
	{
		static const TCHAR* Names[][2] =
		{
			{ TEXT("Percy"), TEXT("Percy") }, { TEXT("SlytherinPrefect"), TEXT("a prefect") },
			{ TEXT("ProfSnape"), TEXT("Professor Snape") }, { TEXT("Snape"), TEXT("Professor Snape") },
			{ TEXT("ProfMcGonagall"), TEXT("Professor McGonagall") }, { TEXT("ProfFlitwick"), TEXT("Professor Flitwick") },
			{ TEXT("ProfSprout"), TEXT("Professor Sprout") }, { TEXT("ProfLockhart"), TEXT("Professor Lockhart") },
			{ TEXT("ProfDumbleDore"), TEXT("Professor Dumbledore") }, { TEXT("Filch"), TEXT("Filch") },
			{ TEXT("MrsNorris"), TEXT("Mrs Norris") }, { TEXT("MadamePomfrey"), TEXT("Madam Pomfrey") },
		};
		for( INT i=0; i<ARRAY_COUNT(Names); i++ )
			if( ClassChainIs(A,Names[i][0]) )
				return Names[i][1];
		return A->GetClass()->GetName();
	}

	// The nearest teacher or prefect within Range of P (with a clear view).
	AActor* StaffWatching( UGameEngine* Engine, const FVector& P, FLOAT Range )
	{
		AActor* Best = NULL;
		FLOAT BestD = Range*Range;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden || IsHolding(A) || !IsStaff(A) )
				continue;
			const FLOAT D = (A->Location - P).SizeSquared();
			if( D<BestD && Engine->GLevel->Model->FastLineCheck( A->Location + FVector(0,0,A->CollisionHeight*0.8f), P ) && IsOutAndAbout(Engine,A) )
			{
				BestD = D;
				Best = A;
			}
		}
		return Best;
	}

	// A spell hit a student: troublemakers deserve it, others are trouble.
	void StudentHitBySpell( AActor* Student, INT Trouble, INT Rep, const TCHAR* Reason )
	{
		// McGonagall's practical: transfiguring classmates is the homework.
		if( (LessonKind==LES_Transfig || HasPerk(PERK_GentleTouch)) && appStrstr(Reason,TEXT("Transfig")) )
			return;
		APlayerPawn* Harry = CurrentHarry();
		FTroublemaker* T = FindTroublemaker( Student );
		if( T )
		{
			if( RpgClock < T->LessonUntil )
				return;
			const UBOOL bRedHanded = RpgClock < T->RedHandedUntil;
			const INT Points = bRedHanded ? 15 : 5;
			T->LessonUntil = RpgClock + 150.f;
			State.Counters[CNT_Lessons]++;
			T->RedHandedUntil = 0.f;
			T->NextMischief = RpgClock + 150.f + RpgRand(60);
			AddHousePoints( Harry, HOUSE_Gryffindor, Points );
			GiveXp( bRedHanded ? 25 : 10, TEXT("troublemaker") );
			AddRep( HOUSE_Ravenclaw, 2 );
			AddRep( HOUSE_Hufflepuff, 2 );
			AddRep( HOUSE_Slytherin, -2 );
			AddFeed( TEXT("Lesson taught!"), FColor(120,220,140,255), TOAST_Reward, *FString::Printf(TEXT("+%i Gryffindor%s"), Points, bRedHanded ? TEXT(", caught red-handed") : TEXT("")) );
			RpgLog( TEXT("troublemaker %s taught a lesson (%s)"), Student->GetName(), bRedHanded ? TEXT("red-handed") : TEXT("quiet") );
			return;
		}
		AddTrouble( Trouble, Reason );
		AddRep( StudentHouse(Student), Rep );
		if( Trouble>=12 )
			AddHousePoints( Harry, HOUSE_Gryffindor, -5 );
		// A teacher or prefect saw it.
		UGameEngine* Engine = Ui.Engine;
		AActor* Witness = Engine && Harry ? StaffWatching( Engine, Harry->Location, 900.f ) : NULL;
		if( Witness )
		{
			AddHousePoints( Harry, HOUSE_Gryffindor, -10 );
			AddTrouble( 10, *FString::Printf(TEXT("Seen by %s"), *StaffName(Witness)) );
		}
	}

	void PickTroublemakers( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bTroublePicked = 1;
		// Draco and his friends first, then ordinary Slytherins.
		TArray<AActor*> Gang, Candidates;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden || IsHolding(A) )
				continue;
			if( (IsStudent(A) || ClassChainIsAny(A,TEXT("Draco|Crabbe|Goyle"))) && !IsOutAndAbout(Engine,A) )
				continue; // parked out of reach for a cutscene
			if( ClassChainIsAny(A,TEXT("Draco|Crabbe|Goyle")) )
				Gang.AddItem( A );
			else if( IsStudent(A) && StudentHouse(A)==HOUSE_Slytherin && VendorFor(A)<0 )
				Candidates.AddItem( A );
		}
		// Gang members first (in random order), then random Slytherins.
		TArray<AActor*> Order;
		while( Gang.Num() )
		{
			const INT g = RpgRand( Gang.Num() );
			Order.AddItem( Gang(g) );
			Gang.Remove( g );
		}
		while( Candidates.Num() )
		{
			const INT k = RpgRand( Candidates.Num() );
			Order.AddItem( Candidates(k) );
			Candidates.Remove( k );
		}
		const INT Want = Min( Order.Num(), 2 + (State.Day % 2) );
		for( INT n=0; n<Want; n++ )
		{
			FTroublemaker* T = new(Troublemakers) FTroublemaker;
			T->Actor = Order(n);
			T->Name = Order(n)->GetFName();
			T->NextMischief = RpgClock + 30.f + RpgRand(30) + n*25.f;
			T->RedHandedUntil = 0.f;
			T->LessonUntil = 0.f;
			T->bCaughtChecked = 1;
		}
		RpgLog( TEXT("troublemakers: %i of %i candidates"), Troublemakers.Num(), Order.Num() );
	}

	// Holds a character still for a moment in a one-shot pose.
	void StartPose( AActor* A, const TCHAR* Anim )
	{
		if( IsReacting(A) )
			return;
		FReaction* R = new(Reactions) FReaction;
		R->Actor = A;
		R->OldRole = A->Role;
		R->Left = 2.2f;
		R->OldSeq = A->AnimSequence;
		R->bOldLoop = A->bAnimLoop;
		A->Role = ROLE_DumbProxy;
		A->Velocity = FVector( 0, 0, 0 );
		A->Acceleration = FVector( 0, 0, 0 );
		if( !PlayReactAnim( A, Anim ) )
			PlayReactAnim( A, TEXT("taunt_1") );
	}

	void DoMischief( UGameEngine* Engine, APlayerPawn* Harry, FTroublemaker& T )
	{
		AActor* A = T.Actor;
		static const TCHAR* Acts[][2] =
		{
			{ TEXT("set off a Dungbomb"),        TEXT("FSSeedSmoke") },
			{ TEXT("is hexing a first-year"),    TEXT("Verd_hit") },
			{ TEXT("is tripping up students"),   TEXT("DustCloud01_tiny") },
			{ TEXT("dropped Fanged Frisbees"),   TEXT("Verd_fly") },
		};
		INT k = RpgRand( (INT)ARRAY_COUNT(Acts) );
		// A student nearby to pick on: the hex and the trip are cast at them.
		AActor* Victim = NULL;
		FLOAT BestD = 700.f*700.f;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* V = Engine->GLevel->Actors(i);
			if( !V || V==A || V->bDeleteMe || V->bHidden || !IsStudent(V) || IsTroublemaker(V) || IsHolding(V) || IsTransfigured(V) )
				continue;
			const FLOAT D = (V->Location - A->Location).SizeSquared();
			if( D<BestD && Engine->GLevel->Model->FastLineCheck( V->Location + FVector(0,0,30.f), A->Location + FVector(0,0,30.f) ) )
			{
				BestD = D;
				Victim = V;
			}
		}
		if( Victim && k!=1 && k!=2 && RpgRand(3)>0 )
			k = 1 + RpgRand(2);
		if( Victim && (k==1 || k==2) )
		{
			// Face the victim, cast, and send a visible bolt across.
			FVector D = Victim->Location - A->Location;
			D.Z = 0.f;
			A->Rotation = FRotator( 0, D.Rotation().Yaw, 0 );
			StartPose( A, TEXT("cast") );
			const FVector From = A->Location + FVector( 0, 0, 30.f ), To = Victim->Location + FVector( 0, 0, 30.f );
			for( INT b=1; b<5; b++ )
				SpawnFx( Engine, k==1 ? TEXT("Verd_fly") : TEXT("Wing_hit"), From + (To-From)*(b/5.f), 1.2f );
			SpawnFx( Engine, Acts[k][1], To, 2.f );
			StartPose( Victim, k==1 ? TEXT("flinch") : TEXT("scared") );
			AddFeed( FString::Printf(TEXT("%s %s %s!"), *NpcName(A), k==1 ? TEXT("is hexing") : TEXT("jinxed"), *NpcName(Victim)), FColor(90,200,110,255), TOAST_Trouble, TEXT("Stop them: Flipendo, Leviosa or Transfiguration") );
		}
		else
		{
			if( k==1 || k==2 )
				k = RpgRand(2) ? 0 : 3;
			SpawnFx( Engine, Acts[k][1], A->Location, 3.f );
			StartPose( A, RpgRand(2) ? TEXT("taunt_1") : TEXT("taunt_2") );
			AddFeed( FString::Printf(TEXT("%s %s!"), *NpcName(A), Acts[k][0]), FColor(90,200,110,255), TOAST_Trouble, TEXT("Stop them: Flipendo, Leviosa or Transfiguration") );
		}
		T.RedHandedUntil = RpgClock + 20.f;
		T.bCaughtChecked = 0;
		T.NextMischief = RpgClock + 70.f + RpgRand(60);
		RpgLog( TEXT("mischief by %s: %s"), A->GetName(), Acts[k][0] );
	}

	void UpdateTroublemakers( UGameEngine* Engine, APlayerPawn* Harry, UBOOL bGameplay )
	{
		if( !bTroublePicked )
		{
			if( TroublePickTime==0.f )
				TroublePickTime = RpgClock + 4.f;
			if( RpgClock>=TroublePickTime )
				PickTroublemakers( Engine, Harry );
			return;
		}
		for( INT t=Troublemakers.Num()-1; t>=0; t-- )
		{
			FTroublemaker& T = Troublemakers(t);
			if( !IsInLevel(T.Actor) || T.Actor->GetFName()!=T.Name )
			{
				Troublemakers.Remove( t );
				continue;
			}
			// A teacher or prefect spots the mischief a moment later.
			if( !T.bCaughtChecked && RpgClock > T.RedHandedUntil-17.f )
			{
				T.bCaughtChecked = 1;
				AActor* Staff = StaffWatching( Engine, T.Actor->Location, 1000.f );
				if( Staff )
				{
					AddHousePoints( Harry, HOUSE_Slytherin, -10 );
					AddFeed( FString::Printf(TEXT("Caught by %s!"), *StaffName(Staff)), FColor(90,200,110,255), TOAST_House, TEXT("-10 points from Slytherin") );
					Feed(Feed.Num()-1).Amount = HOUSE_Slytherin;
					T.RedHandedUntil = 0.f;
					RpgLog( TEXT("troublemaker %s caught by %s"), T.Actor->GetName(), Staff->GetName() );
				}
			}
			if( !bGameplay || RpgClock<T.NextMischief || RpgClock<T.LessonUntil || IsTransfigured(T.Actor) )
				continue;
			// Only where Harry can see it happen, and one at a time.
			UBOOL bOtherActive = 0;
			for( INT o=0; o<Troublemakers.Num(); o++ )
				bOtherActive |= o!=t && RpgClock<Troublemakers(o).RedHandedUntil;
			if( bOtherActive || T.Actor->bHidden || (T.Actor->Location - Harry->Location).SizeSquared() > 1500.f*1500.f
				|| !Engine->GLevel->Model->FastLineCheck( Harry->Location + FVector(0,0,40.f), T.Actor->Location + FVector(0,0,30.f) ) )
			{
				T.NextMischief = RpgClock + 10.f;
				continue;
			}
			DoMischief( Engine, Harry, T );
		}
	}

	// A small marker over troublemakers caught in the act.
	void DrawTroublemakers( APlayerPawn* Harry )
	{
		for( INT t=0; t<Troublemakers.Num(); t++ )
		{
			const FTroublemaker& T = Troublemakers(t);
			if( RpgClock>=T.RedHandedUntil || !IsInLevel(T.Actor) )
				continue;
			FLOAT SX, SY;
			if( !WorldToScreen( T.Actor->Location + FVector(0,0,T.Actor->CollisionHeight+25.f), SX, SY ) )
				continue;
			const FLOAT S = Ui.S;
			Ui.Text( Ui.FontModernBig, SX, SY-Ui.TextHeight(Ui.FontModernBig), FColor(90,220,110,255), TEXT("!"), RALIGN_Center );
			Ui.Text( Ui.FontModern, SX, SY+2*S, FColor(200,240,200,255), TEXT("Troublemaker"), RALIGN_Center );
		}
	}

	/*-------------------------------------------------------------------------
		Student reactions.
	-------------------------------------------------------------------------*/

	UBOOL ReactCoolingDown( AActor* A )
	{
		const FName N = A->GetFName();
		for( INT i=0; i<ReactCooldowns.Num(); i++ )
			if( ReactCooldowns(i).Actor==A && ReactCooldowns(i).Name==N )
				return RpgClock < ReactCooldowns(i).Until;
		return 0;
	}

	void SetReactCooldown( AActor* A, FLOAT Seconds )
	{
		const FName N = A->GetFName();
		for( INT i=0; i<ReactCooldowns.Num(); i++ )
			if( ReactCooldowns(i).Actor==A )
			{
				ReactCooldowns(i).Name = N;
				ReactCooldowns(i).Until = RpgClock + Seconds;
				return;
			}
		FReactCooldown* C = new(ReactCooldowns) FReactCooldown;
		C->Actor = A;
		C->Name = N;
		C->Until = RpgClock + Seconds;
	}

	UBOOL IsReacting( AActor* A )
	{
		for( INT r=0; r<Reactions.Num(); r++ )
			if( Reactions(r).Actor==A )
				return 1;
		return 0;
	}

	void StartReaction( APlayerPawn* Harry, AActor* Student )
	{
		const INT Rep = State.Rep[ StudentHouse(Student) ];
		const UBOOL bFemale = ClassChainIs( Student, TEXT("GFemGry1") );
		const TCHAR* Anim = NULL;
		if( Rep>=40 )
			Anim = bFemale ? TEXT("clap") : TEXT("winner");
		else if( Rep>=10 )
			Anim = TEXT("wave");
		else if( Rep<=-30 )
			Anim = RpgRand(2) ? TEXT("taunt_1") : TEXT("taunt_2");
		else if( RpgRand(4)==0 )
			Anim = TEXT("wave");
		SetReactCooldown( Student, 40.f + RpgRand(30) );
		if( !Anim )
			return;
		// Face Harry and hold the pose briefly.
		FVector D = Harry->Location - Student->Location;
		D.Z = 0.f;
		if( D.SizeSquared()>1.f )
			Student->Rotation = FRotator( 0, D.Rotation().Yaw, 0 );
		FReaction* R = new(Reactions) FReaction;
		R->Actor = Student;
		R->OldRole = Student->Role;
		R->Left = 2.2f;
		R->OldSeq = Student->AnimSequence;
		R->bOldLoop = Student->bAnimLoop;
		Student->Role = ROLE_DumbProxy;
		Student->Velocity = FVector( 0, 0, 0 );
		Student->Acceleration = FVector( 0, 0, 0 );
		PlayReactAnim( Student, Anim );
	}

	// Only sequences the character really has: a missing one leaves it in
	// its bind pose (arms out) until its own script picks a new animation.
	static UBOOL PlayReactAnim( AActor* A, const TCHAR* Name )
	{
		const FName Seq( Name, FNAME_Find );
		if( Seq==NAME_None || !A->SkelAnim || !A->SkelAnim->GetAnimSeq(Seq) )
			return 0;
		A->PlayAnim( Seq, false, 1.f, 0.15f );
		return 1;
	}

	void EndReaction( FReaction& R )
	{
		R.Actor->Role = R.OldRole;
		// Back to what it was doing (walking, idling), not frozen on the
		// last frame of the wave.
		if( R.OldSeq!=NAME_None && R.Actor->SkelAnim && R.Actor->SkelAnim->GetAnimSeq(R.OldSeq) )
			R.Actor->PlayAnim( R.OldSeq, R.bOldLoop!=0, 1.f, 0.2f );
	}

	void UpdateStudents( UGameEngine* Engine, APlayerPawn* Harry, UBOOL bGameplay, FLOAT DeltaSeconds )
	{
		for( INT r=Reactions.Num()-1; r>=0; r-- )
		{
			FReaction& R = Reactions(r);
			if( !IsInLevel(R.Actor) )
			{
				Reactions.Remove( r );
				continue;
			}
			R.Left -= DeltaSeconds;
			if( R.Left<=0.f )
			{
				EndReaction( R );
				Reactions.Remove( r );
			}
		}
		ReactPollTimer -= DeltaSeconds;
		if( !bGameplay || ReactPollTimer>0.f || Reactions.Num()>=2 )
			return;
		ReactPollTimer = 0.5f;
		static FName DoingBumpLine( TEXT("DoingBumpLine") );
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || A->bHidden || !IsStudent(A) || IsHolding(A) || A->Physics!=PHYS_Walking )
				continue;
			if( (A->Location - Harry->Location).SizeSquared() > 240.f*240.f )
				continue;
			if( GetStateName(A)==DoingBumpLine || IsReacting(A) || ReactCoolingDown(A) )
				continue;
			StartReaction( Harry, A );
			break;
		}
	}

	void EndWorld()
	{
		for( INT r=0; r<Reactions.Num(); r++ )
			if( IsInLevel(Reactions(r).Actor) )
				EndReaction( Reactions(r) );
		Reactions.Empty();
	}

	void ForgetWorld()
	{
		Troublemakers.Empty();
		bTroublePicked = 0;
		TroublePickTime = 0.f;
		Owls.Empty();
		Reactions.Empty();
		ReactCooldowns.Empty();
		bOwlsSpawned = 0;
	}
