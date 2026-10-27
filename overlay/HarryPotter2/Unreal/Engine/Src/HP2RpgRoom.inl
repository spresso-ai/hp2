/*=============================================================================
	HP2RpgRoom.inl: Decorating mode - make a corner of the castle your own
	room. Included inside the FHP2Rpg class body in HP2Rpg.cpp.

	Opened from the book (Room tab, "Decorate here") in a hub. The game
	pauses and the camera looks down on the floor around Harry, with a grid.
	The panel on the right lists furniture by category; pick a piece and
	click a grid cell to place it, click a placed piece to pick it up and
	drag it, Turn rotates it, Size scales it, Remove puts it back in storage.
	Everything is saved with the existing Decor list.
=============================================================================*/

	enum { ROOM_CELL = 48, ROOM_CELLS = 8 };   // grid: 17 x 17 cells of 48 units

	INT RoomCat;             // category shown in the panel
	INT RoomSel;             // catalogue piece to place, -1 if none
	INT RoomPicked;          // placed piece selected / being dragged, -1 if none
	UBOOL bRoomDragging, bRoomMouseDown, bRoomReleased;
	FVector RoomCenter;
	FLOAT RoomFloorZ, RoomYaw, RoomDist;

	void OpenRoom( UGameEngine* Engine, APlayerPawn* Harry )
	{
		RoomCenter = Harry->Location;
		RoomFloorZ = Harry->Location.Z - Harry->CollisionHeight;
		FCheckResult Hit;
		if( !Engine->GLevel->SingleLineCheck( Hit, Harry, Harry->Location-FVector(0,0,300.f), Harry->Location, TRACE_VisBlocking, FVector(0,0,0) ) )
			RoomFloorZ = Hit.Location.Z;
		AActor* Cam = HarryCam( Harry );
		const FVector Off = Cam ? Cam->Location - Harry->Location : FVector(-200.f,0.f,0.f);
		RoomYaw = (FLOAT)appAtan2( Off.Y, Off.X );
		RoomDist = 640.f;
		RoomSel = RoomPicked = -1;
		bRoomDragging = bRoomMouseDown = bRoomReleased = 0;
		OpenMenu( Engine, MENU_Room );
		RpgLog( TEXT("decorating at %.0f %.0f"), RoomCenter.X, RoomCenter.Y );
	}

	// Keeps the paused camera high above the room, looking down at it.
	void UpdateRoomCam( APlayerPawn* Harry )
	{
		AActor* Cam = HarryCam( Harry );
		if( Menu!=MENU_Room || !Cam )
			return;
		const FLOAT Pitch = 0.95f;   // about 55 degrees down
		const FVector Off( (FLOAT)(appCos(RoomYaw)*appCos(Pitch)), (FLOAT)(appSin(RoomYaw)*appCos(Pitch)), (FLOAT)appSin(Pitch) );
		const FVector Focus( RoomCenter.X, RoomCenter.Y, RoomFloorZ );
		Cam->Location = Focus + Off*RoomDist;
		Cam->Rotation = (-Off).Rotation();
	}

	// Where the mouse points on the room's floor plane.
	UBOOL MouseOnFloor( FLOAT MX, FLOAT MY, FVector& Out )
	{
		FSceneNode* F = Ui.C ? Ui.C->Frame : NULL;
		if( !F || F->Proj.Z<=0.f )
			return 0;
		const FVector Dir = F->Coords.ZAxis + F->Coords.XAxis*((MX-F->FX2)/F->Proj.Z) + F->Coords.YAxis*((MY-F->FY2)/F->Proj.Z);
		if( Dir.Z > -0.01f )
			return 0;
		const FLOAT T = (RoomFloorZ - F->Coords.Origin.Z)/Dir.Z;
		Out = F->Coords.Origin + Dir*T;
		return 1;
	}

	FVector SnapToCell( const FVector& P )
	{
		INT CX = appRound( (P.X-RoomCenter.X)/ROOM_CELL ), CY = appRound( (P.Y-RoomCenter.Y)/ROOM_CELL );
		CX = Clamp( CX, -(INT)ROOM_CELLS, (INT)ROOM_CELLS );
		CY = Clamp( CY, -(INT)ROOM_CELLS, (INT)ROOM_CELLS );
		return FVector( RoomCenter.X + CX*ROOM_CELL, RoomCenter.Y + CY*ROOM_CELL, RoomFloorZ );
	}

	// Floor height under P, and the actor rested on it.
	void RestOnFloor( UGameEngine* Engine, AActor* A, FVector Where )
	{
		FCheckResult Hit;
		if( !Engine->GLevel->SingleLineCheck( Hit, A, Where-FVector(0,0,200.f), Where+FVector(0,0,120.f), TRACE_VisBlocking, FVector(0,0,0) ) )
			Where.Z = Hit.Location.Z;
		const FVector Old = A->Location;
		A->Location = FVector( 0, 0, 0 );
		const FCoords Box = A->GetRenderBoundingBox( 1 );
		A->Location = Old;
		Where.Z += -Min( Box.Origin.Z, Box.Origin.Z+Box.ZAxis.Z );
		Engine->GLevel->FarMoveActor( A, Where, 0, 1 );
	}

	void PlaceDecorAt( UGameEngine* Engine, INT Id, const FVector& Cell )
	{
		if( State.DecorOwned[Id]<=0 )
			return;
		AActor* A = SpawnPickup( Engine, Decor(Id).Mesh, Cell, Decor(Id).Scale );
		if( !A )
		{
			SetNotice( FString::Printf(TEXT("%s can't be placed here."), Decor(Id).Name) );
			return;
		}
		A->AmbientGlow = 0;
		A->Rotation = FRotator( 0, appRound(RoomYaw*(65536.f/(2.f*PI))) + 32768, 0 );
		RestOnFloor( Engine, A, Cell );
		A->SetCollision( 1, 1, 1 );
		FDecorPlaced* D = new(Placed) FDecorPlaced;
		D->Map = Engine->GLevel->URL.Map;
		D->Id = Id;
		D->Loc = A->Location;
		D->Yaw = A->Rotation.Yaw;
		D->Actor = A;
		D->Size = 100;
		State.DecorOwned[Id]--;
		RoomPicked = Placed.Num()-1;
		SpawnFx( Engine, TEXT("SmokeExplo_01"), A->Location, 1.f );
		Ui.Play( RSND_Buy, CurrentHarry() );
		if( State.DecorOwned[Id]<=0 )
			RoomSel = -1;
	}

	void SetDecorSize( UGameEngine* Engine, INT p, INT Size )
	{
		FDecorPlaced& D = Placed(p);
		D.Size = Clamp( Size, 50, 200 );
		if( IsInLevel(D.Actor) )
		{
			D.Actor->DrawScale = Decor(D.Id).Scale*D.Size*0.01f;
			RestOnFloor( Engine, D.Actor, D.Actor->Location );
			D.Loc = D.Actor->Location;
		}
	}

	void DrawRoom( UGameEngine* Engine, APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		const FLOAT PanelW = Min( Ui.P(360.f), C->ClipX*0.34f );
		const FRpgRect PanelR( C->ClipX-PanelW-Ui.P(14.f), Ui.P(14.f), PanelW, C->ClipY-Ui.P(28.f) );
		const FLOAT MX = Ui.MouseX, MY = Ui.MouseY;
		const UBOOL bOverPanel = PanelR.Contains( MX, MY );
		if( Ui.WheelDelta )
		{
			RoomDist = Clamp( RoomDist - Ui.WheelDelta*60.f, 300.f, 1100.f );
			Ui.WheelDelta = 0;
		}

		// Grid dots on the floor.
		for( INT gx=-ROOM_CELLS; gx<=ROOM_CELLS; gx++ )
			for( INT gy=-ROOM_CELLS; gy<=ROOM_CELLS; gy++ )
			{
				FLOAT SX, SY;
				const FVector P( RoomCenter.X + (gx+0.5f)*ROOM_CELL, RoomCenter.Y + (gy+0.5f)*ROOM_CELL, RoomFloorZ+2.f );
				if( WorldToScreen( P, SX, SY ) )
					Ui.Rect( FRpgRect(SX-Ui.P(1.5f), SY-Ui.P(1.5f), Ui.P(3.f), Ui.P(3.f)), FColor(200,170,90,255) );
			}

		// Mouse on the floor: the cell it points at.
		FVector Floor;
		const UBOOL bOnFloor = !bOverPanel && MouseOnFloor( MX, MY, Floor );
		const FVector Cell = bOnFloor ? SnapToCell( Floor ) : RoomCenter;
		if( bOnFloor && (RoomSel>=0 || bRoomDragging) )
		{
			FLOAT SX, SY;
			if( WorldToScreen( Cell + FVector(0,0,2.f), SX, SY ) )
				Ui.Brackets( FRpgRect(SX-Ui.P(22.f), SY-Ui.P(14.f), Ui.P(44.f), Ui.P(28.f)), Ui.P(8.f), Max(1.f,Ui.P(2.f)), RpgGreen );
		}

		// Clicks in the room (the panel handles its own).
		if( Ui.bClickPending && !bOverPanel )
		{
			Ui.bClickPending = 0;
			// A placed piece under the mouse?
			INT Hit = -1;
			FLOAT BestD = Ui.P(34.f)*Ui.P(34.f);
			for( INT p=0; p<Placed.Num(); p++ )
			{
				FLOAT SX, SY;
				if( !IsInLevel(Placed(p).Actor) || !WorldToScreen(Placed(p).Actor->Location, SX, SY) )
					continue;
				const FLOAT D = (SX-MX)*(SX-MX) + (SY-MY)*(SY-MY);
				if( D<BestD )
				{
					BestD = D;
					Hit = p;
				}
			}
			if( Hit>=0 )
			{
				RoomPicked = Hit;
				RoomSel = -1;
				bRoomDragging = 1;
			}
			else if( RoomSel>=0 && bOnFloor )
				PlaceDecorAt( Engine, RoomSel, Cell );
			else
				RoomPicked = -1;
		}
		// Dragging a placed piece follows the mouse cell.
		if( bRoomDragging && RoomPicked>=0 && RoomPicked<Placed.Num() && bOnFloor && IsInLevel(Placed(RoomPicked).Actor) )
		{
			AActor* A = Placed(RoomPicked).Actor;
			if( (A->Location-Cell).Size2D() > 4.f )
				RestOnFloor( Engine, A, Cell );
		}
		if( bRoomReleased )
		{
			bRoomReleased = 0;
			if( bRoomDragging && RoomPicked>=0 && RoomPicked<Placed.Num() && IsInLevel(Placed(RoomPicked).Actor) )
				Placed(RoomPicked).Loc = Placed(RoomPicked).Actor->Location;
			bRoomDragging = 0;
		}
		// The selected piece: gold brackets.
		if( RoomPicked>=0 && RoomPicked<Placed.Num() && IsInLevel(Placed(RoomPicked).Actor) )
		{
			FLOAT SX, SY;
			if( WorldToScreen( Placed(RoomPicked).Actor->Location, SX, SY ) )
				Ui.Brackets( FRpgRect(SX-Ui.P(30.f), SY-Ui.P(30.f), Ui.P(60.f), Ui.P(60.f)), Ui.P(10.f), Max(1.f,Ui.P(2.f)), RpgGoldBright );
		}

		DrawRoomPanel( Engine, Harry, PanelR );

		// Hint bar, bottom left.
		const TCHAR* Hint = RoomSel>=0 ? TEXT("Click a grid cell to place it   -   right panel: choose another")
			: RoomPicked>=0 ? TEXT("Drag to move   -   Turn, Size or Remove on the right")
			: TEXT("Choose a piece on the right, or click your furniture to move it");
		const FLOAT HW = C->ClipX - PanelW - Ui.P(56.f), HH = Ui.TextHeight(Ui.FontBody)*2 + Ui.P(18.f);
		const FRpgRect HR( Ui.P(14.f), C->ClipY-HH-Ui.P(14.f), HW, HH );
		Ui.Glass( HR, 1 );
		Ui.Text( Ui.FontBody, HR.X+HR.W*0.5f, HR.Y+Ui.P(6.f), RpgGoldBright, Hint, RALIGN_Center, 0 );
		Ui.Text( Ui.FontSmall, HR.X+HR.W*0.5f, HR.Y+Ui.P(10.f)+Ui.TextHeight(Ui.FontBody), RpgParchment, TEXT("Q / E: turn the view    Wheel: zoom    Esc: done"), RALIGN_Center, 0 );
	}

	void DrawRoomPanel( UGameEngine* Engine, APlayerPawn* Harry, const FRpgRect& R )
	{
		Ui.Panel( R );
		UFont* HeadFont = Ui.FontItem ? Ui.FontItem : Ui.FontModernBold;
		const FLOAT Pad = Ui.P(12.f);
		FLOAT Y = R.Y + Pad;
		// Category with arrows.
		const FLOAT AW = Ui.P(28.f), AH = Ui.TextHeight(HeadFont);
		if( Ui.Button( FRpgRect(R.X+Pad, Y, AW, AH), TEXT("<"), 1, &RoomCat ) )
			RoomCat = (RoomCat+DC_MAX-1) % DC_MAX;
		if( Ui.Button( FRpgRect(R.X+R.W-Pad-AW, Y, AW, AH), TEXT(">"), 1, &RoomYaw ) )
			RoomCat = (RoomCat+1) % DC_MAX;
		Ui.Text( HeadFont, R.X+R.W*0.5f, Y, RpgGoldBright, DecorCatName(RoomCat), RALIGN_Center, 0 );
		Y += AH + Ui.P(2.f);
		Ui.Text( Ui.FontSmall, R.X+R.W*0.5f, Y, RpgMuted, TEXT("Make this room your own"), RALIGN_Center, 0 );
		Y += Ui.TextHeight(Ui.FontSmall) + Ui.P(10.f);

		// Tiles of this category.
		INT Items[NUM_DECOR];
		INT N = 0;
		for( INT i=0; i<NUM_DECOR; i++ )
			if( Decor(i).Cat==RoomCat )
				Items[N++] = i;
		const INT Cols = 3;
		const FLOAT Gap = Ui.P(6.f);
		const FLOAT TW = (R.W - Pad*2 - Gap*(Cols-1))/Cols;
		const FLOAT TH = TW + Ui.TextHeight(Ui.FontSmall)*2;
		const INT Beans = GetBeans( Harry );
		for( INT n=0; n<N; n++ )
		{
			const INT Id = Items[n];
			const FRpgRect T( R.X+Pad + (n%Cols)*(TW+Gap), Y + (n/Cols)*(TH+Gap), TW, TH );
			const UBOOL bSel = RoomSel==Id;
			const UBOOL bHover = Ui.Hovered( T, &Decor(Id) );
			Ui.Rect( T, bSel ? FColor(34,44,96,255) : bHover ? FColor(26,34,74,255) : FColor(18,24,54,255) );
			Ui.Border( T, Max(1.f,Ui.P(1.f)), bSel ? RpgGoldBright : FColor(70,74,104,255) );
			DrawMeshPreview( Engine, FRpgRect(T.X+Ui.P(4.f), T.Y+Ui.P(4.f), T.W-Ui.P(8.f), T.W-Ui.P(8.f)), Decor(Id).Mesh, Decor(Id).Scale, 0.f );
			Ui.Text( Ui.FontSmall, T.X+T.W*0.5f, T.Y+T.W, bSel ? RpgGoldBright : RpgParchment, *Ui.FitText(Ui.FontSmall,Decor(Id).Name,T.W-Ui.P(4.f)), RALIGN_Center, 0 );
			const UBOOL bOwned = State.DecorOwned[Id]>0;
			Ui.Text( Ui.FontSmall, T.X+T.W*0.5f, T.Y+T.W+Ui.TextHeight(Ui.FontSmall), bOwned ? RpgGreen : (Decor(Id).Price<=Beans ? RpgMuted : RpgRed),
				*(bOwned ? FString::Printf(TEXT("%i in storage"), State.DecorOwned[Id]) : FString::Printf(TEXT("%i beans"), Decor(Id).Price)), RALIGN_Center, 0 );
			if( Ui.Clicked( T, &Decor(Id) ) )
			{
				RoomPicked = -1;
				if( bOwned )
					RoomSel = bSel ? -1 : Id;
				else if( Decor(Id).Price<=Beans )
				{
					// Buy one and pick it up straight away.
					GiveBeans( Harry, -Decor(Id).Price );
					State.DecorOwned[Id]++;
					RoomSel = Id;
					Ui.Play( RSND_Buy, Harry );
					SetNotice( FString::Printf(TEXT("%s bought - click a cell to place it."), Decor(Id).Name) );
				}
				else
				{
					Ui.Play( RSND_Deny, Harry );
					SetNotice( FString::Printf(TEXT("%s costs %i beans."), Decor(Id).Name, Decor(Id).Price) );
				}
			}
		}

		// Size and buttons at the bottom.
		const FLOAT BH = Ui.TextHeight(Ui.FontBody) + Ui.P(14.f);
		FLOAT BY = R.Y + R.H - Pad - BH;
		const FLOAT BW = (R.W - Pad*2 - Gap*2)/3.f;
		const UBOOL bPicked = RoomPicked>=0 && RoomPicked<Placed.Num() && IsInLevel(Placed(RoomPicked).Actor);
		if( Ui.Button( FRpgRect(R.X+Pad, BY, BW, BH), TEXT("Turn"), bPicked, &RoomPicked ) && bPicked )
		{
			FDecorPlaced& D = Placed(RoomPicked);
			D.Actor->Rotation.Yaw += 16384;
			D.Yaw = D.Actor->Rotation.Yaw;
		}
		if( Ui.Button( FRpgRect(R.X+Pad+BW+Gap, BY, BW, BH), TEXT("Remove"), bPicked, &bRoomDragging ) && bPicked )
		{
			SpawnFx( Engine, TEXT("SmokeExplo_02"), Placed(RoomPicked).Actor->Location, 1.f );
			DestroyActorSafe( Engine, Placed(RoomPicked).Actor );
			State.DecorOwned[Placed(RoomPicked).Id]++;
			Placed.Remove( RoomPicked );
			RoomPicked = -1;
		}
		if( Ui.Button( FRpgRect(R.X+Pad+(BW+Gap)*2, BY, BW, BH), TEXT("Done"), 1, &RoomDist ) )
		{
			CloseMenu( Engine );
			return;
		}
		BY -= BH + Gap;
		Ui.Text( Ui.FontBody, R.X+Pad, BY+Ui.P(6.f), RpgParchment, TEXT("Size"), RALIGN_Left, 0 );
		const INT Size = bPicked ? Placed(RoomPicked).Size : 100;
		if( Ui.Button( FRpgRect(R.X+R.W-Pad-BW*2-Gap, BY, AW*1.5f, BH), TEXT("<"), bPicked && Size>50, &RoomFloorZ ) && bPicked )
			SetDecorSize( Engine, RoomPicked, Size-25 );
		Ui.Text( Ui.FontBody, R.X+R.W-Pad-BW-Gap*0.5f, BY+Ui.P(6.f), bPicked ? RpgGoldBright : RpgMuted,
			bPicked ? *FString::Printf(TEXT("%i%%"), Size) : TEXT("Select an item"), RALIGN_Center, 0 );
		if( Ui.Button( FRpgRect(R.X+R.W-Pad-AW*1.5f, BY, AW*1.5f, BH), TEXT(">"), bPicked && Size<200, &RoomCenter ) && bPicked )
			SetDecorSize( Engine, RoomPicked, Size+25 );
	}

	UBOOL RoomKey( UGameEngine* Engine, EInputKey Key )
	{
		if( Key==IK_Escape || Key==IK_Backspace || Key==IK_Enter )
		{
			CloseMenu( Engine );
			return 1;
		}
		if( Key==IK_Q || Key==IK_E )
			RoomYaw += (Key==IK_E ? 1.f : -1.f)*(PI/4.f);
		else if( Key==IK_RightMouse )
		{
			RoomSel = RoomPicked = -1;
			bRoomDragging = 0;
		}
		else if( Key==IK_Delete && RoomPicked>=0 && RoomPicked<Placed.Num() )
		{
			DestroyActorSafe( Engine, Placed(RoomPicked).Actor );
			State.DecorOwned[Placed(RoomPicked).Id]++;
			Placed.Remove( RoomPicked );
			RoomPicked = -1;
		}
		return 1;
	}
