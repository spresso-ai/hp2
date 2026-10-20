/*=============================================================================
	HP2RpgWorld.inl: Living-world touches of FHP2Rpg.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	- Owls circle over the school grounds and hoot now and then.
	- Students react when Harry walks past: friendly houses wave or cheer,
	  hostile ones jeer. Reactions use the game's own student animations and
	  a short soft freeze (Role = ROLE_DumbProxy) so the AI does not override
	  them; every change is undone before saves and level changes.
=============================================================================*/

	struct FOwl { AActor* Actor; FLOAT Angle; FLOAT Radius; FLOAT Height; FLOAT Speed; FVector Centre; };
	struct FReaction { AActor* Actor; BYTE OldRole; FLOAT Left; };
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
		if( !appStrstr(Map,TEXT("Grounds")) )
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
		Student->Role = ROLE_DumbProxy;
		Student->Velocity = FVector( 0, 0, 0 );
		Student->Acceleration = FVector( 0, 0, 0 );
		Student->PlayAnim( FName(Anim), false, 1.f, 0.15f );
	}

	void EndReaction( FReaction& R )
	{
		R.Actor->Role = R.OldRole;
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
		Owls.Empty();
		Reactions.Empty();
		ReactCooldowns.Empty();
		bOwlsSpawned = 0;
	}
