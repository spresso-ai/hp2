/*=============================================================================
	HP2RpgTimetable.inl: The school clock, the weekly timetable, going to
	class and what happens when Harry skips it. Included inside the FHP2Rpg
	class body in HP2Rpg.cpp.

	- Clock: a school day runs 07:00 to 22:00 over the 20 minutes of play a
	  day takes (DayPlaySeconds, saved), so it never jumps on a reload.
	  School day 1 is a Monday.
	- Timetable (T): two or three classes a weekday, Quidditch practice on
	  some days, Sunday free. A class can be started with its teacher (E)
	  from half an hour before it begins until 45 minutes after it ends.
	- Skipping: a class nobody went to costs house points, with the
	  teacher's own voice - but only if Harry was free in a hub when it
	  ended. During a story challenge it is excused.
=============================================================================*/

	enum { TT_SLOTS = 3, TT_OPEN_BEFORE = 30, TT_OPEN_AFTER = 45 };

	struct FTimeSlot { INT Kind; INT Start; INT End; };   // minutes after midnight

	// Today's classes by weekday (0 = Monday). Returns how many.
	static INT DaySlots( INT WeekDay, FTimeSlot* Out )
	{
		static const FTimeSlot Week[7][TT_SLOTS] =
		{
			{ { LES_Potions, 540, 600 }, { LES_Quidditch, 750, 810 }, { LES_Charms, 900, 960 } },
			{ { LES_Transfig, 540, 600 }, { LES_Herbology, 840, 900 }, { -1, 0, 0 } },
			{ { LES_Defence, 540, 600 }, { LES_Charms, 840, 900 }, { -1, 0, 0 } },
			{ { LES_Herbology, 540, 600 }, { LES_Quidditch, 750, 810 }, { LES_Potions, 900, 960 } },
			{ { LES_Transfig, 540, 600 }, { LES_Defence, 840, 900 }, { -1, 0, 0 } },
			{ { LES_Quidditch, 600, 660 }, { -1, 0, 0 }, { -1, 0, 0 } },
			{ { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 } },
		};
		INT N = 0;
		for( INT s=0; s<TT_SLOTS; s++ )
			if( Week[Clamp(WeekDay,0,6)][s].Kind>=0 )
				Out[N++] = Week[Clamp(WeekDay,0,6)][s];
		return N;
	}

	static const TCHAR* WeekDayName( INT d )
	{
		static const TCHAR* Names[7] = { TEXT("Monday"), TEXT("Tuesday"), TEXT("Wednesday"), TEXT("Thursday"), TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday") };
		return Names[ Clamp(d,0,6) ];
	}

	// Where each class is held (shown on the timetable).
	static const TCHAR* LessonPlace( INT k )
	{
		static const TCHAR* Places[LES_MAX] = { TEXT("Grand Staircase"), TEXT("Greenhouses, Grounds"), TEXT("Dungeons, Entrance Hall"), TEXT("Entrance Hall"), TEXT("Entrance Hall"), TEXT("Quidditch Pitch, Grounds") };
		return Places[ Clamp(k,0,LES_MAX-1) ];
	}

	INT WeekDay() { return (Max(State.Day,1)-1) % 7; }
	INT WeekNum() { return (Max(State.Day,1)-1) / 7 + 1; }

	// Minutes after midnight on the school clock.
	INT ClockMinutes()
	{
		return Min( 7*60 + appFloor(State.DayPlaySeconds*0.75f), 23*60+59 );
	}

	FString ClockText()
	{
		const INT M = ClockMinutes();
		return FString::Printf( TEXT("%s, %02i:%02i"), WeekDayName(WeekDay()), M/60, M%60 );
	}

	static FString SlotTimes( const FTimeSlot& S )
	{
		return FString::Printf( TEXT("%02i:%02i - %02i:%02i"), S.Start/60, S.Start%60, S.End/60, S.End%60 );
	}

	// Today's bits: attended (0-7), missed (8-15), excused (16-23).
	void SyncSlotDay()
	{
		if( State.SlotDay!=State.Day )
		{
			State.SlotDay = State.Day;
			State.SlotBits = 0;
		}
	}
	UBOOL SlotResolved( INT s ) { SyncSlotDay(); return (State.SlotBits & (0x10101<<s))!=0; }

	enum ESlotStatus { SLOT_Upcoming, SLOT_Open, SLOT_Attended, SLOT_Missed, SLOT_Excused, SLOT_Past };

	INT SlotStatus( INT s, const FTimeSlot& Slot )
	{
		SyncSlotDay();
		if( State.SlotBits & (1<<s) ) return SLOT_Attended;
		if( State.SlotBits & (1<<(8+s)) ) return SLOT_Missed;
		if( State.SlotBits & (1<<(16+s)) ) return SLOT_Excused;
		const INT M = ClockMinutes();
		if( M < Slot.Start-TT_OPEN_BEFORE ) return SLOT_Upcoming;
		if( M <= Slot.End+TT_OPEN_AFTER ) return SLOT_Open;
		return SLOT_Past;
	}

	// The class that can be started right now (-1 if none); Slot is its index.
	INT OpenSlotKind( INT* SlotOut=NULL )
	{
		FTimeSlot Slots[TT_SLOTS];
		const INT N = DaySlots( WeekDay(), Slots );
		for( INT s=0; s<N; s++ )
			if( SlotStatus(s,Slots[s])==SLOT_Open )
			{
				if( SlotOut )
					*SlotOut = s;
				return Slots[s].Kind;
			}
		return -1;
	}

	// The open class, else the next one today (for the teacher marker).
	INT NextSlotKind()
	{
		FTimeSlot Slots[TT_SLOTS];
		const INT N = DaySlots( WeekDay(), Slots );
		for( INT s=0; s<N; s++ )
		{
			const INT St = SlotStatus( s, Slots[s] );
			if( St==SLOT_Open || St==SLOT_Upcoming )
				return Slots[s].Kind;
		}
		return -1;
	}

	INT ActiveSlot;          // slot index of the running lesson, -1 if none
	INT AnnouncedSlots;      // bits: "class starts soon" shown today
	INT AnnouncedDay;

	void MarkSlotAttended()
	{
		SyncSlotDay();
		if( ActiveSlot>=0 && ActiveSlot<TT_SLOTS )
			State.SlotBits |= 1<<ActiveSlot;
		ActiveSlot = -1;
	}

	// A skipped class: the teacher's own voice and a few points off.
	void SkippedClass( APlayerPawn* Harry, INT Kind )
	{
		static const struct { const TCHAR* Line; const TCHAR* Text; INT Points; } Skips[LES_MAX] =
		{
			{ TEXT("PC_Flt_Chal2Skurge_43"), TEXT("Mr. Potter! An empty seat in Charms today. Ten points from Gryffindor."), 10 },
			{ TEXT("PC_Spr_Chal3Diff_16"), TEXT("No Harry in Herbology today? The Mandrakes missed you. Ten points from Gryffindor, dear."), 10 },
			{ TEXT("PC_Snp_SnpDeductHP_05"), TEXT("Too important for Potions, Potter? Ten points from Gryffindor."), 10 },
			{ TEXT("PC_Gil_Chal4Spong_16"), TEXT("Harry, you missed my class! I'm afraid I must take ten points - it pains me more than you."), 10 },
			{ TEXT("PC_MCG_BUMPLINES_599"), TEXT("You were not in my class, Potter. Five points from Gryffindor!"), 5 },
			{ TEXT("PC_Owd_GoToQuid2_02"), TEXT("Harry, where were you? You can't skip practice - we've got a Cup to win! McGonagall says that's ten points from Gryffindor."), 10 },
		};
		const INT k = Clamp( Kind, 0, LES_MAX-1 );
		AddHousePoints( Harry, HOUSE_Gryffindor, -Skips[k].Points );
		INT Index = 0;
		SpeakLine( Harry, Skips[k].Line, Index );
		SayAs( Lesson(k).TeacherName, Skips[k].Text, LessonTeacherMesh(k) );
		RpgLog( TEXT("skipped %s: -%i"), Lesson(k).Subject, Skips[k].Points );
	}

	// Once a second: announce classes and settle the ones that are over.
	void UpdateTimetable( APlayerPawn* Harry )
	{
		SyncSlotDay();
		if( AnnouncedDay!=State.Day )
		{
			AnnouncedDay = State.Day;
			AnnouncedSlots = 0;
		}
		const UBOOL bHub = LastLevel && MapHas( *LastLevel->URL.Map, TEXT("_hub") );
		const UBOOL bFree = Ui.Engine && IsFreeGameplay( Ui.Engine, Harry );
		FTimeSlot Slots[TT_SLOTS];
		const INT N = DaySlots( WeekDay(), Slots );
		const INT M = ClockMinutes();
		for( INT s=0; s<N; s++ )
		{
			if( SlotResolved(s) || (LessonKind>=0 && ActiveSlot==s) )
				continue;
			const FTimeSlot& Sl = Slots[s];
			if( M >= Sl.Start-TT_OPEN_BEFORE && M <= Sl.End && !(AnnouncedSlots & (1<<s)) && bFree )
			{
				AnnouncedSlots |= 1<<s;
				AddFeed( FString::Printf(TEXT("%s at %02i:%02i"), Lesson(Sl.Kind).Subject, Sl.Start/60, Sl.Start%60), FColor(120,170,255,255), TOAST_Info,
					*FString::Printf(TEXT("%s - %s"), Lesson(Sl.Kind).TeacherName, LessonPlace(Sl.Kind)) );
			}
			if( M > Sl.End+TT_OPEN_AFTER )
			{
				if( bHub && bFree )
				{
					State.SlotBits |= 1<<(8+s);
					SkippedClass( Harry, Sl.Kind );
				}
				else
					State.SlotBits |= 1<<(16+s);
			}
		}
	}

	/*-------------------------------------------------------------------------
		Clock on the HUD.
	-------------------------------------------------------------------------*/

	void DrawClockHud( APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		FString T = ClockText();
		const INT K = OpenSlotKind();
		if( K>=0 && LessonKind<0 )
			T += FString::Printf( TEXT("   -   %s now"), Lesson(K).Subject );
		const FLOAT W = Ui.TextWidth( Ui.FontCaption, *T ) + Ui.P(24.f), H = Ui.TextHeight( Ui.FontCaption ) + Ui.P(10.f);
		const FRpgRect R( (C->ClipX-W)*0.5f, Ui.P(10.f), W, H );
		Ui.Glass( R );
		Ui.Border( R, Max(1.f,Ui.P(1.f)), RpgGold );
		Ui.Text( Ui.FontCaption, C->ClipX*0.5f, R.Y+Ui.P(5.f), K>=0 && LessonKind<0 ? RpgGoldBright : RpgParchment, *T, RALIGN_Center, 0 );
	}

	/*-------------------------------------------------------------------------
		Timetable screen (T).
	-------------------------------------------------------------------------*/

	INT TimetableDay;

	void OpenTimetable( UGameEngine* Engine )
	{
		TimetableDay = WeekDay();
		OpenMenu( Engine, MENU_Timetable );
	}

	static const TCHAR* LessonIcon( INT k )
	{
		static const TCHAR* Icons[LES_MAX] =
		{
			TEXT("SpellShapes.Shapes.SPSflipendo"), TEXT("SpellShapes.Shapes.SPSdiffindo"), TEXT("HP2_Menu.Icons.HP2BeanCounter"),
			TEXT("SpellShapes.Shapes.SPSrictusempra"), TEXT("SpellShapes.Shapes.SPSskurge"), TEXT("HP2_Menu.Icons.HP2StarCounter"),
		};
		return Icons[ Clamp(k,0,LES_MAX-1) ];
	}

	void DrawTimetable( UGameEngine* Engine, APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		Ui.Sky( FRpgRect(0,0,C->ClipX,C->ClipY) );
		UFont* TitleFont = Ui.FontInk ? Ui.FontInk : Ui.FontTitle;
		UFont* HeadFont = Ui.FontItem ? Ui.FontItem : Ui.FontModernBold;
		const FLOAT W = Min( C->ClipX - Ui.P(48.f), Ui.P(900.f) );
		const FLOAT X = (C->ClipX-W)*0.5f;
		FLOAT Y = Ui.P(20.f);
		Ui.Text( TitleFont, C->ClipX*0.5f, Y, RpgGoldBright, TEXT("Timetable"), RALIGN_Center, 0 );
		Y += Ui.TextHeight( TitleFont );
		Ui.Text( Ui.FontBody, C->ClipX*0.5f, Y, RpgParchment, *FString::Printf(TEXT("%s   -   Week %i"), *ClockText(), WeekNum()), RALIGN_Center, 0 );
		Y += Ui.TextHeight( Ui.FontBody ) + Ui.P(16.f);

		// Day buttons.
		const FLOAT Gap = Ui.P(8.f), BW = (W - Gap*6)/7.f, BH = Ui.TextHeight(Ui.FontBody) + Ui.P(12.f);
		for( INT d=0; d<7; d++ )
		{
			const FRpgRect B( X + d*(BW+Gap), Y, BW, BH );
			const UBOOL bSel = d==TimetableDay;
			const UBOOL bHover = Ui.Hovered( B, &WeekDayName(d)[0] );
			Ui.Rect( B, bSel ? FColor(120,84,24,255) : (bHover ? FColor(70,56,20,255) : FColor(46,36,18,255)) );
			Ui.Border( B, Max(1.f,Ui.P(1.f)), bSel ? RpgGoldBright : RpgGold );
			FString Short = WeekDayName(d);
			Short = Short.Left( 3 );
			Ui.Text( Ui.FontBody, B.X+B.W*0.5f, B.Y+Ui.P(6.f), bSel ? RpgGoldBright : RpgParchment, *Short, RALIGN_Center, 0 );
			if( d==WeekDay() )
				Ui.Text( Ui.FontSmall, B.X+B.W*0.5f, B.Y+B.H+Ui.P(2.f), RpgMuted, TEXT("Today"), RALIGN_Center, 0 );
			if( Ui.Clicked( B, &WeekDayName(d)[0] ) )
			{
				TimetableDay = d;
				Ui.Play( RSND_Tab, Harry );
			}
		}
		Y += BH + Ui.TextHeight(Ui.FontSmall) + Ui.P(14.f);

		// The day's classes.
		FTimeSlot Slots[TT_SLOTS];
		const INT N = DaySlots( TimetableDay, Slots );
		const FLOAT RowH = Ui.TextHeight(HeadFont) + Ui.TextHeight(Ui.FontSmall) + Ui.P(26.f);
		const FLOAT PanelH = Ui.TextHeight(HeadFont) + Ui.P(28.f) + Max(1,N)*RowH;
		const FRpgRect Panel( X, Y, W, PanelH );
		Ui.Panel( Panel );
		Ui.Text( HeadFont, C->ClipX*0.5f, Panel.Y+Ui.P(10.f), RpgGoldBright, WeekDayName(TimetableDay), RALIGN_Center, 0 );
		FLOAT RY = Panel.Y + Ui.TextHeight(HeadFont) + Ui.P(22.f);
		if( !N )
			Ui.Text( Ui.FontBody, C->ClipX*0.5f, RY + Ui.P(10.f), RpgMuted, TEXT("No classes - a free day!"), RALIGN_Center, 0 );
		for( INT s=0; s<N; s++ )
		{
			const FTimeSlot& Sl = Slots[s];
			const FLessonDef& L = Lesson( Sl.Kind );
			if( s>0 )
				Ui.Rect( FRpgRect(Panel.X+Ui.P(30.f), RY-Ui.P(6.f), Panel.W-Ui.P(60.f), Max(1.f,Ui.P(1.f))), FColor(90,74,40,255) );
			Ui.Text( Ui.FontBody, Panel.X+Ui.P(30.f), RY+Ui.P(14.f), RpgParchment, *SlotTimes(Sl), RALIGN_Left, 0 );
			const FLOAT IconS = RowH - Ui.P(20.f);
			const FLOAT IX = Panel.X + Ui.P(30.f) + Ui.TextWidth(Ui.FontBody,TEXT("00:00 - 00:00")) + Ui.P(28.f);
			if( UTexture* Icon = LoadTex( LessonIcon(Sl.Kind) ) )
				Ui.Icon( Icon, FRpgRect(IX, RY, IconS, IconS) );
			const FLOAT TX = IX + IconS + Ui.P(16.f);
			Ui.Text( HeadFont, TX, RY, RpgGoldBright, L.Subject, RALIGN_Left, 0 );
			Ui.Text( Ui.FontSmall, TX, RY+Ui.TextHeight(HeadFont)+Ui.P(2.f), RpgMuted, *FString::Printf(TEXT("%s, %s"), L.TeacherName, LessonPlace(Sl.Kind)), RALIGN_Left, 0 );
			// Status.
			const TCHAR* Status = TEXT("Upcoming");
			FColor SC = RpgParchment;
			if( TimetableDay==WeekDay() )
			{
				switch( SlotStatus(s,Sl) )
				{
					case SLOT_Open:     Status = TEXT("Now - go!"); SC = RpgGoldBright; break;
					case SLOT_Attended: Status = TEXT("Attended"); SC = RpgGreen; break;
					case SLOT_Missed:   Status = TEXT("Missed"); SC = RpgRed; break;
					case SLOT_Excused:  Status = TEXT("Excused"); SC = RpgMuted; break;
					case SLOT_Past:     Status = TEXT("Over"); SC = RpgMuted; break;
					default: break;
				}
			}
			else if( TimetableDay<WeekDay() )
			{
				Status = TEXT("Last week");
				SC = RpgMuted;
			}
			const FLOAT SW = Ui.P(150.f), SH = Ui.TextHeight(Ui.FontBody)+Ui.P(12.f);
			const FRpgRect SB( Panel.X+Panel.W-Ui.P(30.f)-SW, RY+Ui.P(6.f), SW, SH );
			Ui.Rect( SB, FColor(46,36,18,255) );
			Ui.Border( SB, Max(1.f,Ui.P(1.f)), RpgGold );
			Ui.Text( Ui.FontBody, SB.X+SB.W*0.5f, SB.Y+Ui.P(6.f), SC, Status, RALIGN_Center, 0 );
			RY += RowH;
		}
		Ui.Footer( TEXT("Left / Right: another day    Talk to the teacher (E) when a class is on    T / Esc: close") );
	}

	UBOOL TimetableInput( UGameEngine* Engine, EInputKey Key )
	{
		if( Key==IK_Left || Key==IK_A )
			TimetableDay = (TimetableDay+6) % 7;
		else if( Key==IK_Right || Key==IK_D )
			TimetableDay = (TimetableDay+1) % 7;
		else if( Key==IK_T )
			CloseMenu( Engine );
		else
			return 0;
		Ui.Play( RSND_Tab, CurrentHarry() );
		return 1;
	}

	/*-------------------------------------------------------------------------
		Speaker portraits for the mod's dialogue panel.
	-------------------------------------------------------------------------*/

	FString DialogMesh;      // speaker's mesh path for the portrait, or empty

	void SayAs( const TCHAR* Speaker, const TCHAR* Text, const TCHAR* MeshPath )
	{
		Say( Speaker, Text );
		DialogMesh = MeshPath ? MeshPath : TEXT("");
	}

	static const TCHAR* LessonTeacherMesh( INT k )
	{
		static const TCHAR* Meshes[LES_MAX] =
		{
			TEXT("HPModels.skProfFlitwickMesh"), TEXT("HPModels.skProfSproutMesh"), TEXT("HPModels.skProfSnapeMesh"),
			TEXT("HPModels.skProfLockhartMesh"), TEXT("HPModels.skProfMcGonagallMesh"), TEXT("HPModels.skOliverWoodMesh"),
		};
		return Meshes[ Clamp(k,0,LES_MAX-1) ];
	}

	// The speaker's head in a gold ring, beside the dialogue panel.
	void DrawPortrait( const FRpgRect& R, const TCHAR* MeshPath )
	{
		UGameEngine* Engine = Ui.Engine;
		UMesh* Mesh = MeshPath && *MeshPath ? LoadMesh( MeshPath ) : NULL;
		if( !Engine || !Mesh )
			return;
		Ui.Rect( R, FColor(22,30,66,255) );
		AActor* Body = EnsurePreview( Engine, PreviewNpc );
		if( !Body )
			return;
		{
			FRpgClipScope Clip( Ui, R );
			if( Body->Mesh!=Mesh )
			{
				SetPreviewMesh( Body, Mesh );
				LinkCharacterAnims( Body, MeshPath );
			}
			const FName Pose = FindSeq( Body->SkelAnim, TEXT("Idle") );
			if( Pose!=NAME_None )
			{
				Body->AnimSequence = Pose;
				Body->AnimFrame = PreviewFrame( RpgClock );
				Body->AnimRate = 0.f;
			}
			USkeletalMesh* Skel = NULL;
			const INT Bone = HeadBone( Body, Skel );
			if( Ui.PlaceInRect( Body, R, 0.f, Bone>=0 ? 3.0f : 1.f ) )
			{
				if( Bone>=0 && Skel )
					Ui.FocusPoint( Body, Skel->GetBoneCoords(Body,Bone).Origin, R.X+R.W*0.5f, R.Y+R.H*0.58f );
				Ui.DrawActorInRect( Body, 1 );
			}
		}
		Ui.Border( R, Max(1.f,Ui.P(2.f)), RpgGold );
	}

	/*-------------------------------------------------------------------------
		Day and night: the evening glows orange, the night is dark blue.
		The engine blends Harry's FlashFog colour over the scene with
		strength 1-W (the same effect as the game's damage flash), so the
		tint is only set while the game isn't flashing on its own.
	-------------------------------------------------------------------------*/

	FLOAT TintW;             // last W we set (1 = no tint)
	FPlane TintGlow;         // last ConstantGlowFog we set
	FLOAT SleepFade;         // > 0 while falling asleep

	void UpdateDayNight( APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		const INT M = ClockMinutes();
		FVector Col( 0.f, 0.f, 0.f );
		FLOAT W = 1.f;
		const UBOOL bHub = LastLevel && MapHas( *LastLevel->URL.Map, TEXT("_hub") );
		if( bHub && !bWasCutscene )
		{
			if( M>=18*60 && M<20*60 )
			{
				const FLOAT T = (M-18*60)/120.f;
				Col = FVector( 0.32f, 0.16f, 0.05f );
				W = 1.f - 0.18f*T;
			}
			else if( M>=20*60 )
			{
				const FLOAT T = Min( 1.f, (M-20*60)/60.f );
				Col = FVector( 0.02f, 0.03f, 0.10f )*T + FVector( 0.32f, 0.16f, 0.05f )*(1.f-T);
				W = 0.82f - 0.27f*T;
			}
		}
		// Falling asleep: fade to black, then the new day starts.
		if( SleepFade>0.f )
		{
			SleepFade += DeltaSeconds;
			const FLOAT K = SleepFade<1.5f ? SleepFade/1.5f : SleepFade<2.5f ? 1.f : Max( 0.f, 1.f-(SleepFade-2.5f)/1.2f );
			Col = Col*(1.f-K);
			W = W*(1.f-K);
			if( SleepFade>=1.5f && SleepFade-DeltaSeconds<1.5f )
				WakeUp( Harry );
			if( SleepFade>=3.7f )
				SleepFade = 0.f;
		}
		// The tint is the player's ConstantGlowFog (the engine's ViewFlash
		// eases the screen towards it every frame, so it never flickers).
		// Cutscene fades use it too: only touch it while it is ours.
		FPlane& Glow = Harry->ConstantGlowFog;
		const FPlane Want( Col.X, Col.Y, Col.Z, W-1.f );
		const UBOOL bOurs = Harry->FadeRate==0.f
			&& ( (Abs(Glow.X-TintGlow.X)<0.002f && Abs(Glow.Y-TintGlow.Y)<0.002f && Abs(Glow.Z-TintGlow.Z)<0.002f && Abs(Glow.W-TintGlow.W)<0.002f)
			  || (Abs(Glow.X)<0.002f && Abs(Glow.Y)<0.002f && Abs(Glow.Z)<0.002f && Abs(Glow.W)<0.002f) );
		if( !bOurs )
			return;
		Glow = Want;
		TintGlow = Want;
		TintW = W;
	}

	/*-------------------------------------------------------------------------
		Your own room and sleeping.

		The door at the top of the Gryffindor common room (the locked bonus
		challenge door, which the retail game never opens). Standing at it:
		E goes to your room - the unused, empty Entry map - and Z sleeps
		until morning once it is 20:00. In your room: E back, Z sleep.
	-------------------------------------------------------------------------*/

	UBOOL bReturnFromRoom;   // just came back from the room: stand at the gate
	FLOAT ReturnAt;          // once the level has placed Harry itself

	static UBOOL IsRoomMap( const TCHAR* Map )
	{
		return Map && (appStricmp(Map,TEXT("Entry"))==0 || appStricmp(Map,TEXT("Entry.unr"))==0);
	}

	static FVector RoomGateSpot() { return FVector( 2646.f, -1874.f, 616.f ); }

	UBOOL AtRoomGate( APlayerPawn* Harry )
	{
		return LastLevel && MapHas( *LastLevel->URL.Map, TEXT("Entryhall") ) && (Harry->Location-RoomGateSpot()).SizeSquared() < 190.f*190.f;
	}

	UBOOL InRoom()
	{
		return LastLevel && IsRoomMap( *LastLevel->URL.Map );
	}

	// Travel the way TriggerChangeLevel does: save the level's actors, then
	// let the console change level (with Harry's inventory).
	void TravelTo( UGameEngine* Engine, APlayerPawn* Harry, const TCHAR* Map )
	{
		UObject* Console = Harry && Harry->Player ? (UObject*)Harry->Player->Console : NULL;
		if( !Console )
			return;
		PreSave( Engine );
		Engine->Exec( TEXT("SAVEPACTORS"), *GLog );
		FRpgArg Args[2] = { FRpgArg::S(Map), FRpgArg::I(1) };
		CallScript( Console, TEXT("ChangeLevel"), Args, 2 );
		RpgLog( TEXT("travel to %s"), Map );
	}

	UBOOL SleepInput( UGameEngine* Engine, APlayerPawn* Harry, EInputKey Key )
	{
		const UBOOL bGate = AtRoomGate( Harry ), bRoom = InRoom();
		if( (!bGate && !bRoom) || SleepFade>0.f )
			return 0;
		if( Key==IK_Z )
		{
			if( ClockMinutes() < 20*60 )
			{
				SayAs( TEXT("Percy Weasley"), TEXT("Bed already? It's not even eight o'clock. There's plenty of day left."), TEXT("HPModels.skPercyMesh") );
				return 1;
			}
			SleepFade = 0.001f;
			Ui.Play( RSND_Open, Harry );
			return 1;
		}
		if( Key==IK_E && !NearNpc )
		{
			Ui.Play( RSND_Open, Harry );
			TravelTo( Engine, Harry, bRoom ? TEXT("Entryhall_hub.unr") : TEXT("Entry.unr") );
			return 1;
		}
		return 0;
	}

	// Back from the room: Harry stands at the gate again.
	void PlaceAfterRoom( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( !bReturnFromRoom || RpgClock<ReturnAt || !LastLevel || !MapHas(*LastLevel->URL.Map,TEXT("Entryhall")) )
			return;
		bReturnFromRoom = 0;
		Engine->GLevel->FarMoveActor( Harry, RoomGateSpot() + FVector(-50.f,60.f,24.f), 0, 1 );
		Harry->Rotation = FRotator( 0, 16384, 0 );
		Harry->ViewRotation = Harry->Rotation;
	}

	/*-------------------------------------------------------------------------
		Night on the grounds: after 20:00 the door to the grounds leads to
		the game's own night map (Grounds_Night), outside the two story
		visits that use it. Its way into the Forbidden Forest is closed.
	-------------------------------------------------------------------------*/

	FLOAT NightCheck;

	/*-------------------------------------------------------------------------
		Fan maps: "Average Day Through Hogwarts" by David Kapitan, converted
		to the retail package format (AverageDayHogwarts.unr). Its exit
		leads to the bean reward room; here it leads back to the Entrance
		Hall instead, so the book's Leave button works too.
	-------------------------------------------------------------------------*/

	UBOOL bConfirmFanMap;

	static UBOOL IsFanMap( const TCHAR* Map )
	{
		return MapHas( Map, TEXT("AverageDayHogwarts") );
	}

	void UpdateFanMap( UGameEngine* Engine )
	{
		if( !LastLevel || !IsFanMap(*LastLevel->URL.Map) )
			return;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( !A || A->bDeleteMe || !ClassChainIs(A,TEXT("TriggerChangeLevel")) )
				continue;
			if( UStrProperty* P = FindField<UStrProperty>( A->GetClass(), TEXT("NewMapName") ) )
			{
				FString& To = *(FString*)((BYTE*)A + P->Offset);
				if( !MapHas(*To,TEXT("_hub")) )
				{
					RpgLog( TEXT("fan map exit %s: %s -> Entryhall_hub"), A->GetName(), *To );
					To = TEXT("Entryhall_hub.unr");
				}
			}
		}
	}

	static UBOOL NightGroundsAllowed( INT GS )
	{
		return GS!=10 && GS!=80 && GS!=150 && GS!=160;
	}

	void UpdateNightGrounds( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		NightCheck -= DeltaSeconds;
		if( NightCheck>0.f || !LastLevel )
			return;
		NightCheck = 1.f;
		UpdateFanMap( Engine );
		// The door to your room glitters when Harry is near.
		if( MapHas(*LastLevel->URL.Map,TEXT("Entryhall")) && (Harry->Location-RoomGateSpot()).SizeSquared() < 900.f*900.f )
			SpawnFx( Engine, TEXT("GoldSparkle01"), RoomGateSpot() + FVector(-40.f,-40.f,40.f), 1.2f );
		const INT GS = GameStateNum( Harry );
		if( !NightGroundsAllowed(GS) )
			return;
		const TCHAR* Map = *LastLevel->URL.Map;
		if( MapHas(Map,TEXT("Entryhall")) )
		{
			// The day door to the grounds: Grounds_hub by day, Grounds_Night at night.
			const UBOOL bNight = ClockMinutes() >= 20*60;
			for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
			{
				AActor* A = Engine->GLevel->Actors(i);
				if( !A || A->bDeleteMe || A->Tag!=FName(TEXT("GroundsHub")) || !ClassChainIs(A,TEXT("TriggerChangeLevel")) )
					continue;
				if( UStrProperty* P = FindField<UStrProperty>( A->GetClass(), TEXT("NewMapName") ) )
				{
					FString& To = *(FString*)((BYTE*)A + P->Offset);
					const TCHAR* Want = bNight ? TEXT("Grounds_Night.unr") : TEXT("Grounds_hub.unr");
					if( appStricmp(*To,Want)!=0 )
					{
						To = Want;
						RpgLog( TEXT("grounds door now leads to %s"), Want );
					}
				}
			}
		}
		else if( MapHas(Map,TEXT("Grounds_Night")) )
		{
			// No way into the forest outside the story night.
			const FVector Forest( 4000.f, 3400.f, -150.f );
			for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
			{
				AActor* A = Engine->GLevel->Actors(i);
				if( !A || A->bDeleteMe || !A->bCollideActors )
					continue;
				const UBOOL bForestExit = A->Tag==FName(TEXT("ChangeLevelAdv8Forest"));
				const UBOOL bNear = (A->Location-Forest).SizeSquared() < 900.f*900.f && (ClassChainIs(A,TEXT("Trigger")) || ClassChainIs(A,TEXT("CutScene")));
				if( bForestExit || bNear )
				{
					A->SetCollision( 0, 0, 0 );
					RpgLog( TEXT("night grounds: closed %s"), A->GetName() );
				}
			}
		}
	}

	// The night passes: a new school day at 07:00.
	void WakeUp( APlayerPawn* Harry )
	{
		LastDayClock = RpgClock;
		State.Day++;
		State.DayPlaySeconds = 0;
		State.DayTaskDone = 0;
		TalkedToday.Empty();
		SyncSlotDay();
		if( LastLevel )
			StepRhythm( *LastLevel->URL.Map, 0 );
		if( Ui.Engine )
			EndSchoolmates( Ui.Engine );
		AddFeed( FString::Printf(TEXT("Good morning! %s"), WeekDayName(WeekDay())), FColor(255,222,130,255), TOAST_Level,
			*FString::Printf(TEXT("School day %i - week %i"), State.Day, WeekNum()) );
		RpgLog( TEXT("slept: day %i"), State.Day );
	}

	void DrawSleepPrompt( APlayerPawn* Harry )
	{
		const UBOOL bGate = AtRoomGate( Harry ), bRoom = InRoom();
		if( (!bGate && !bRoom) || SleepFade>0.f )
			return;
		const TCHAR* Keys[2] = { TEXT("E"), TEXT("Z") };
		const TCHAR* Verbs[2] = { bRoom ? TEXT("Back to the common room") : TEXT("Your room"),
			ClockMinutes()>=20*60 ? TEXT("Sleep until morning") : TEXT("Sleep (after 20:00)") };
		Ui.Prompt( Ui.C->ClipX*0.5f, Ui.C->ClipY*0.62f, Keys, Verbs, 2, bRoom ? TEXT("Your room") : TEXT("Dormitory") );
	}

	/*-------------------------------------------------------------------------
		The door between the castle and the fan map: the Great Hall doors
		in the Entrance Hall (closed all story long until the ending) open
		onto "Average Day Through Hogwarts"; its own front doors, beside
		where Harry arrives, lead back.
	-------------------------------------------------------------------------*/

	FLOAT FanTravelAt;       // the doors are opening: travel then
	UBOOL bReturnFromFan;
	FLOAT FanReturnAt;

	static FVector GreatHallDoorSpot() { return FVector( -340.f, -1088.f, -262.f ); }
	static FVector FanStartSpot() { return FVector( 4.f, -1.f, -83.f ); }

	UBOOL AtGreatHallDoor( APlayerPawn* Harry )
	{
		return LastLevel && MapHas( *LastLevel->URL.Map, TEXT("Entryhall") ) && GameStateNum(Harry)<180
			&& (Harry->Location-GreatHallDoorSpot()).SizeSquared() < 230.f*230.f;
	}

	UBOOL AtFanStart( APlayerPawn* Harry )
	{
		return (LastLevel && IsFanMap( *LastLevel->URL.Map ) && (Harry->Location-FanStartSpot()).SizeSquared() < 220.f*220.f)
			|| AtH1Start( Harry );
	}

	// Opens (or closes) the movers carrying Tag.
	void ToggleDoors( UGameEngine* Engine, APlayerPawn* Harry, const TCHAR* Tag )
	{
		const FName T( Tag, FNAME_Find );
		if( T==NAME_None )
			return;
		for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
		{
			AActor* A = Engine->GLevel->Actors(i);
			if( A && !A->bDeleteMe && A->Tag==T && A->IsA(AMover::StaticClass()) )
				A->eventTrigger( Harry, Harry );
		}
	}

	UBOOL FanDoorInput( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( FanTravelAt>0.f )
			return 1;
		if( AtGreatHallDoor(Harry) )
		{
			FixDoorDest( Harry );
			ToggleDoors( Engine, Harry, TEXT("CloseGreatHall") );
			PlaySfx( TEXT("HPSounds.Magic_sfx.Lumos_pass_through") );
			FanTravelAt = RpgClock + 1.4f;
			return 1;
		}
		if( AtFanStart(Harry) )
		{
			FixDoorDest( Harry );
			ToggleDoors( Engine, Harry, TEXT("Doors1") );
			PlaySfx( TEXT("HPSounds.Magic_sfx.Lumos_pass_through") );
			FanTravelAt = RpgClock + 0.6f;
			return 1;
		}
		return 0;
	}

	void UpdateFanDoor( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( FanTravelAt>0.f && RpgClock>=FanTravelAt )
		{
			FanTravelAt = 0.f;
			FixDoorDest( Harry );
			TravelTo( Engine, Harry, *(FString(DoorDestMap(DoorDest)) + TEXT(".unr")) );
		}
		// Back from the fan map: in front of the Great Hall doors, closed again.
		if( bReturnFromFan && RpgClock>=FanReturnAt && LastLevel && MapHas(*LastLevel->URL.Map,TEXT("Entryhall")) )
		{
			bReturnFromFan = 0;
			Engine->GLevel->FarMoveActor( Harry, GreatHallDoorSpot() - FVector(60.f,0.f,0.f), 0, 1 );
			Harry->Rotation = FRotator( 0, 32768, 0 );
			Harry->ViewRotation = Harry->Rotation;
			for( INT i=0; i<Engine->GLevel->Actors.Num(); i++ )
			{
				AMover* M = Cast<AMover>( Engine->GLevel->Actors(i) );
				if( M && !M->bDeleteMe && M->Tag==FName(TEXT("CloseGreatHall")) && M->KeyNum!=0 )
					M->eventTrigger( Harry, Harry );
			}
		}
	}

	void DrawFanDoorPrompt( APlayerPawn* Harry )
	{
		const UBOOL bHall = AtGreatHallDoor( Harry ), bFan = AtFanStart( Harry );
		if( (!bHall && !bFan) || FanTravelAt>0.f )
			return;
		FixDoorDest( Harry );
		const TCHAR* Keys[2] = { TEXT("E"), TEXT("R") };
		const TCHAR* Verbs[2] = { bHall ? TEXT("Open the doors") : TEXT("Go there"), TEXT("Somewhere else") };
		Ui.Prompt( Ui.C->ClipX*0.5f, Ui.C->ClipY*0.62f, Keys, Verbs, NumDoorDests()>2 ? 2 : 1, DoorDestName(DoorDest) );
	}

	/*-------------------------------------------------------------------------
		Spell spawners around the castle, as the fan map uses them: suits
		of armour (Flipendo, beans), ingredient jars (Flobberworm mucus,
		Wiggentree bark) and boxes that open with Alohomora. A few per hub
		each school day; once emptied they stay empty that day, even after
		loading a save.
	-------------------------------------------------------------------------*/

	enum { MAX_SPAWNERS = 5 };
	struct FCastleSpawner { AActor* Actor; INT Index; };
	FCastleSpawner Spawners[MAX_SPAWNERS];
	INT NumSpawners;
	UBOOL bSpawnersPlaced;
	FLOAT SpawnerDelay, SpawnerCheck;

	FString SpawnerKey( INT i )
	{
		return FString::Printf( TEXT("spawner:%s:%i:%i"), LastLevel ? *LastLevel->URL.Map : TEXT(""), State.Day, i );
	}

	void PlaceSpawners( UGameEngine* Engine, APlayerPawn* Harry )
	{
		bSpawnersPlaced = 1;
		NumSpawners = 0;
		if( !LastLevel || !MapHas(*LastLevel->URL.Map,TEXT("_hub")) )
			return;
		static const TCHAR* Kinds[8] = { TEXT("Knightspawn"), TEXT("MucusSpawn"), TEXT("BarkSpawn"), TEXT("JewelBoxSpawn"),
			TEXT("MusicBoxSpawn"), TEXT("DecanterSpawn"), TEXT("Knightspawn"), TEXT("PlantPotSpawn") };
		const INT Want = 3 + (State.Day % 2);
		for( INT i=0; i<Want && NumSpawners<MAX_SPAWNERS; i++ )
		{
			if( OpenedTreasure.FindItemIndex(SpawnerKey(i))!=INDEX_NONE )
				continue;
			UClass* Class = FindClass( Kinds[ HashMix((DWORD)State.Day*31u, (DWORD)i) % 8u ] );
			FVector Where;
			if( !Class || !PickSpot( Engine, (DWORD)(State.Day*97 + i*389 + 17), Where ) || (Where-Harry->Location).SizeSquared() < 300.f*300.f )
				continue;
			AActor* Def = Class->GetDefaultActor();
			Where.Z += Def ? Def->CollisionHeight - 18.f : 30.f;
			AActor* A = TrySpawn( Engine, Class, Where, 0 );
			if( !A )
				continue;
			A->Tag = FName( TEXT("HP2RpgActor") );   // never saved into the level
			A->Rotation = FRotator( 0, RpgRand(65536), 0 );
			Spawners[NumSpawners].Actor = A;
			Spawners[NumSpawners].Index = i;
			NumSpawners++;
		}
		if( NumSpawners )
			RpgLog( TEXT("castle spawners: %i"), NumSpawners );
	}

	void UpdateSpawners( UGameEngine* Engine, APlayerPawn* Harry, FLOAT DeltaSeconds )
	{
		if( !bSpawnersPlaced )
		{
			SpawnerDelay -= DeltaSeconds;
			if( SpawnerDelay<=0.f )
				PlaceSpawners( Engine, Harry );
			return;
		}
		SpawnerCheck -= DeltaSeconds;
		if( SpawnerCheck>0.f )
			return;
		SpawnerCheck = 1.f;
		for( INT s=0; s<NumSpawners; s++ )
		{
			AActor* A = Spawners[s].Actor;
			if( !A )
				continue;
			// Used up (no lives left) or gone: remember it for today.
			const UBOOL bGone = !IsInLevel(A);
			if( bGone || GetIntProp(A,TEXT("Lives"),1)<=0 )
			{
				const FString Key = SpawnerKey( Spawners[s].Index );
				if( OpenedTreasure.FindItemIndex(Key)==INDEX_NONE && OpenedTreasure.Num()<400 )
					OpenedTreasure.AddItem( Key );
				if( bGone )
					Spawners[s].Actor = NULL;
			}
		}
	}

	void EndSpawners( UGameEngine* Engine )
	{
		for( INT s=0; s<NumSpawners; s++ )
			DestroyActorSafe( Engine, Spawners[s].Actor );
		NumSpawners = 0;
		bSpawnersPlaced = 0;
		SpawnerDelay = 1.f;
	}

	void ForgetSpawners()
	{
		for( INT s=0; s<MAX_SPAWNERS; s++ )
			Spawners[s].Actor = NULL;
		NumSpawners = 0;
		bSpawnersPlaced = 0;
		SpawnerDelay = 3.f;
	}
