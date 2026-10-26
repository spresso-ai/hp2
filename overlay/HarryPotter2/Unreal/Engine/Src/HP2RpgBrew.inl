/*=============================================================================
	HP2RpgBrew.inl: Potions class as a real brewing station. Included
	inside the FHP2Rpg class body in HP2Rpg.cpp.

	Snape starts it (E during the Potions slot). A cauldron and four jars
	appear in front of Harry and the game pauses on them. Follow the recipe:
	drag a jar onto the cauldron to add it, hold the mouse on the cauldron
	and circle around it to stir (the direction matters), then finish the
	potion. Wrong jars and wrong stirring count as mistakes; the quality
	sets the grade, house points and Snape's verdict.
=============================================================================*/

	enum EBrewStep { BREW_Add, BREW_StirCW, BREW_StirCCW, BREW_Finish };
	enum { BREW_JARS = 4, BREW_STEPS = 6, BREW_RUINED = 5 };

	struct FBrewStepDef { INT Type; INT Jar; INT Count; const TCHAR* Text; };

	static const FBrewStepDef& BrewStep( INT i )
	{
		static const FBrewStepDef Steps[BREW_STEPS] =
		{
			{ BREW_Add,     0, 1, TEXT("Add the Wiggentree Bark") },
			{ BREW_StirCW, -1, 3, TEXT("Stir clockwise three times") },
			{ BREW_Add,     1, 1, TEXT("Add the Flobberworm Mucus") },
			{ BREW_Add,     2, 1, TEXT("Add the Horklump Chunks") },
			{ BREW_StirCCW,-1, 2, TEXT("Stir counterclockwise twice") },
			{ BREW_Finish, -1, 1, TEXT("Finish the potion") },
		};
		return Steps[ Clamp(i,0,BREW_STEPS-1) ];
	}

	static const TCHAR* BrewJarName( INT j )
	{
		static const TCHAR* Names[BREW_JARS] = { TEXT("Wiggentree Bark"), TEXT("Flobberworm Mucus"), TEXT("Horklump Chunks"), TEXT("Bicorn Horn") };
		return Names[ Clamp(j,0,BREW_JARS-1) ];
	}

	static const TCHAR* BrewJarMesh( INT j )
	{
		static const TCHAR* Meshes[BREW_JARS] = { TEXT("HProps.skJarBeansMesh"), TEXT("HProps.skBottlePotionGreen1Mesh"), TEXT("HProps.skBottlePotionOrangeMesh"), TEXT("HProps.skBottlePotionPurpleMesh") };
		return Meshes[ Clamp(j,0,BREW_JARS-1) ];
	}

	AActor* BrewCauldron;
	AActor* BrewJars[BREW_JARS];
	INT BrewStepIndex, BrewProgress, BrewMistakes;
	INT BrewDrag;            // jar being dragged, -1 if none
	UBOOL bBrewMouseDown, bBrewReleased, bBrewStirring, bBrewAngleValid, bBrewDone;
	FLOAT BrewAngle, BrewLastAngle;
	FString BrewMsg;
	FLOAT BrewMsgUntil;
	INT BrewGrade;

	// Rests a spawned prop on the floor at Where.
	AActor* SpawnOnFloor( UGameEngine* Engine, const TCHAR* Mesh, FVector Where, FLOAT Scale )
	{
		FCheckResult Hit;
		if( !Engine->GLevel->SingleLineCheck( Hit, NULL, Where-FVector(0,0,300.f), Where+FVector(0,0,40.f), TRACE_VisBlocking, FVector(0,0,0) ) )
			Where = Hit.Location;
		AActor* A = SpawnPickup( Engine, Mesh, Where, Scale );
		if( !A )
			return NULL;
		A->Location = FVector( 0, 0, 0 );
		const FCoords Box = A->GetRenderBoundingBox( 1 );
		Where.Z += -Min( Box.Origin.Z, Box.Origin.Z+Box.ZAxis.Z );
		Engine->GLevel->FarMoveActor( A, Where, 0, 1 );
		A->AmbientGlow = 60;
		return A;
	}

	void EndBrew( UGameEngine* Engine )
	{
		DestroyActorSafe( Engine, BrewCauldron );
		for( INT j=0; j<BREW_JARS; j++ )
			DestroyActorSafe( Engine, BrewJars[j] );
	}

	void ForgetBrew()
	{
		BrewCauldron = NULL;
		for( INT j=0; j<BREW_JARS; j++ )
			BrewJars[j] = NULL;
	}

	UBOOL StartBrew( UGameEngine* Engine, APlayerPawn* Harry, AActor* Teacher )
	{
		EndBrew( Engine );
		FVector Fwd = Harry->Rotation.Vector();
		Fwd.Z = 0.f;
		Fwd = Fwd.SafeNormal();
		const FVector Right( -Fwd.Y, Fwd.X, 0.f );
		const FVector Base = Harry->Location - FVector( 0, 0, Harry->CollisionHeight-20.f );
		BrewCauldron = SpawnOnFloor( Engine, TEXT("HProps.skCauldronStudentMesh"), Base + Fwd*130.f, 1.3f );
		if( !BrewCauldron )
			BrewCauldron = SpawnOnFloor( Engine, TEXT("HProps.skCauldronSmallMesh"), Base + Fwd*130.f, 1.6f );
		static const FLOAT Side[BREW_JARS] = { -95.f, -60.f, 60.f, 95.f };
		for( INT j=0; j<BREW_JARS; j++ )
			BrewJars[j] = SpawnOnFloor( Engine, BrewJarMesh(j), Base + Fwd*105.f + Right*Side[j], 1.6f );
		if( !BrewCauldron )
		{
			EndBrew( Engine );
			return 0;
		}
		LessonKind = LES_Potions;
		LessonLeft = Lesson(LES_Potions).Time;
		LessonProgress = LessonMisses = 0;
		BrewStepIndex = BrewProgress = BrewMistakes = 0;
		BrewDrag = -1;
		bBrewMouseDown = bBrewReleased = bBrewStirring = bBrewAngleValid = bBrewDone = 0;
		BrewAngle = 0.f;
		BrewMsg = TEXT("");
		SpawnFx( Engine, TEXT("SmokeExplo_01"), BrewCauldron->Location, 1.f );
		INT Index = 0;
		SpeakLine( Harry, TEXT("PC_Snp_PotionTeach_03"), Index );
		OpenMenu( Engine, MENU_Brew );
		RpgLog( TEXT("brewing started") );
		return 1;
	}

	void BrewFeedback( const TCHAR* Text )
	{
		BrewMsg = Text;
		BrewMsgUntil = RpgClock + 1.8f;
	}

	void BrewMistake( const TCHAR* Text )
	{
		BrewMistakes++;
		BrewFeedback( Text );
		Ui.Play( RSND_Deny, CurrentHarry() );
		if( BrewMistakes>=BREW_RUINED )
			FinishBrew( 1 );
	}

	void BrewAdvance()
	{
		if( ++BrewProgress >= BrewStep(BrewStepIndex).Count )
		{
			BrewStepIndex++;
			BrewProgress = 0;
		}
		Ui.Play( RSND_Buy, CurrentHarry() );
	}

	void AddJar( INT j )
	{
		UGameEngine* Engine = Ui.Engine;
		const FBrewStepDef& St = BrewStep( BrewStepIndex );
		if( Engine && BrewCauldron )
			SpawnFx( Engine, j==St.Jar ? TEXT("Potion_sparkle") : TEXT("SmokeExplo_02"), BrewCauldron->Location + FVector(0,0,30.f), 1.f );
		if( St.Type!=BREW_Add || j!=St.Jar )
		{
			BrewMistake( *FString::Printf(TEXT("%s - not now!"), BrewJarName(j)) );
			return;
		}
		BrewFeedback( *FString::Printf(TEXT("%s added"), BrewJarName(j)) );
		BrewAdvance();
	}

	void StirTurn( UBOOL bClockwise )
	{
		UGameEngine* Engine = Ui.Engine;
		const FBrewStepDef& St = BrewStep( BrewStepIndex );
		const UBOOL bWant = St.Type==BREW_StirCW ? 1 : St.Type==BREW_StirCCW ? 0 : 2;
		if( Engine && BrewCauldron )
			SpawnFx( Engine, TEXT("Potion_sparkle"), BrewCauldron->Location + FVector(0,0,30.f), 0.8f );
		if( bWant==2 || bWant!=bClockwise )
		{
			BrewMistake( bWant==2 ? TEXT("No stirring now!") : TEXT("Wrong direction!") );
			return;
		}
		BrewAdvance();
	}

	// Ends the class: grade, points and Snape's verdict.
	void FinishBrew( UBOOL bRuined )
	{
		UGameEngine* Engine = Ui.Engine;
		APlayerPawn* Harry = CurrentHarry();
		bBrewDone = 1;
		bBrewStirring = 0;
		BrewDrag = -1;
		BrewGrade = bRuined ? 1 : Max( 2, 5-Min(BrewMistakes,3) );
		LessonProgress = bRuined ? 0 : Lesson(LES_Potions).Count;
		LessonMisses = Min( BrewMistakes, 3 );
		if( Engine && Harry )
		{
			FinishLesson( Engine, Harry, 0 );
			if( BrewGrade>=3 )
				ApplyConsumable( Harry, CON_Potion, 1 );
			if( BrewCauldron )
				SpawnFx( Engine, bRuined ? TEXT("SmokeExplo_02") : TEXT("Cauldron_Green"), BrewCauldron->Location + FVector(0,0,20.f), 2.f );
			INT Index = 0;
			SpeakLine( Harry, BrewGrade>=4 ? TEXT("PC_SNP_BUMPLINES_973") : TEXT("PC_Snp_SnpDeductHP_08"), Index );
		}
		RpgLog( TEXT("brewing done: %i mistakes, grade %s"), BrewMistakes, GradeLetter(BrewGrade) );
	}

	void CloseBrew( UGameEngine* Engine )
	{
		if( !bBrewDone && LessonKind==LES_Potions )
			FinishBrew( 1 ); // walked away: the potion is ruined
		EndBrew( Engine );
		CloseMenu( Engine );
	}

	UBOOL BrewKey( UGameEngine* Engine, EInputKey Key )
	{
		if( Key==IK_Escape || Key==IK_Backspace )
		{
			CloseBrew( Engine );
			return 1;
		}
		if( Key==IK_Enter )
		{
			if( bBrewDone )
				CloseBrew( Engine );
			else if( BrewStep(BrewStepIndex).Type==BREW_Finish )
				FinishBrew( 0 );
			return 1;
		}
		return 1;
	}

	void DrawBrew( UGameEngine* Engine, APlayerPawn* Harry )
	{
		UCanvas* C = Ui.C;
		if( bBrewDone )
		{
			DrawBrewResult( Engine );
			return;
		}
		UFont* HeadFont = Ui.FontItem ? Ui.FontItem : Ui.FontModernBold;
		const FBrewStepDef& St = BrewStep( BrewStepIndex );

		// Where the cauldron and jars are on screen.
		FLOAT CX = C->ClipX*0.5f, CY = C->ClipY*0.6f;
		if( BrewCauldron )
			WorldToScreen( BrewCauldron->Location + FVector(0,0,25.f), CX, CY );
		const FLOAT CR = Ui.P(70.f);
		const FRpgRect CauldronR( CX-CR, CY-CR*0.8f, CR*2.f, CR*1.6f );
		FRpgRect JarR[BREW_JARS];
		for( INT j=0; j<BREW_JARS; j++ )
		{
			FLOAT JX = -1000.f, JY = -1000.f;
			if( BrewJars[j] )
				WorldToScreen( BrewJars[j]->Location + FVector(0,0,15.f), JX, JY );
			JarR[j] = FRpgRect( JX-Ui.P(26.f), JY-Ui.P(36.f), Ui.P(52.f), Ui.P(64.f) );
		}

		// Mouse: press picks a jar or starts stirring; release drops the jar.
		const FLOAT MX = Ui.MouseX, MY = Ui.MouseY;
		if( Ui.bClickPending )
		{
			Ui.bClickPending = 0;
			for( INT j=0; j<BREW_JARS && BrewDrag<0; j++ )
				if( JarR[j].Contains(MX,MY) )
					BrewDrag = j;
			if( BrewDrag<0 && CauldronR.Contains(MX,MY) )
			{
				if( St.Type==BREW_Finish )
					FinishBrew( 0 );
				else
				{
					bBrewStirring = 1;
					bBrewAngleValid = 0;
					BrewAngle = 0.f;
				}
			}
		}
		if( bBrewReleased )
		{
			bBrewReleased = 0;
			if( BrewDrag>=0 && CauldronR.Contains(MX,MY) )
				AddJar( BrewDrag );
			BrewDrag = -1;
			bBrewStirring = 0;
		}
		if( bBrewDone )
			return;
		// Stirring: a full circle around the cauldron counts as one stir
		// (screen y points down, so a growing angle is clockwise).
		if( bBrewStirring && bBrewMouseDown )
		{
			const FLOAT A = (FLOAT)appAtan2( MY-CY, MX-CX );
			if( bBrewAngleValid )
			{
				FLOAT D = A - BrewLastAngle;
				if( D>PI ) D -= 2.f*PI;
				if( D<-PI ) D += 2.f*PI;
				BrewAngle += D;
				if( BrewAngle >= 2.f*PI ) { BrewAngle = 0.f; StirTurn( 1 ); }
				else if( BrewAngle <= -2.f*PI ) { BrewAngle = 0.f; StirTurn( 0 ); }
			}
			BrewLastAngle = A;
			bBrewAngleValid = 1;
		}
		if( bBrewDone )
			return;

		// Stirring ring around the cauldron.
		if( St.Type==BREW_StirCW || St.Type==BREW_StirCCW || bBrewStirring )
		{
			const FLOAT Turn = Clamp( Abs(BrewAngle)/(2.f*PI), 0.f, 1.f );
			for( INT d=0; d<24; d++ )
			{
				const FLOAT A = d*(2.f*PI/24.f);
				const FLOAT Dot = Ui.P(5.f);
				const UBOOL bLit = d < appRound(Turn*24.f);
				Ui.Rect( FRpgRect(CX + (FLOAT)appCos(A)*CR - Dot*0.5f, CY + (FLOAT)appSin(A)*CR*0.6f - Dot*0.5f, Dot, Dot), bLit ? RpgGoldBright : FColor(90,84,70,255) );
			}
		}
		// Jar labels on hover, the dragged jar at the mouse.
		for( INT j=0; j<BREW_JARS; j++ )
			if( JarR[j].Contains(MX,MY) && BrewDrag<0 )
				DrawBrewLabel( JarR[j].X+JarR[j].W*0.5f, JarR[j].Y-Ui.P(6.f), BrewJarName(j), RpgParchment );
		if( BrewDrag>=0 )
			DrawBrewLabel( MX, MY-Ui.P(10.f), BrewJarName(BrewDrag), RpgGoldBright );
		if( RpgClock<BrewMsgUntil && BrewMsg.Len() )
			DrawBrewLabel( CX, CY-CR-Ui.P(8.f), *BrewMsg, BrewMsg.InStr(TEXT("added"))>=0 ? RpgGreen : RpgRed );

		// Recipe, top left.
		{
			const FLOAT LineH = Ui.TextHeight( Ui.FontBody ) + Ui.P(6.f);
			const FLOAT W = Ui.P(300.f), H = Ui.TextHeight(HeadFont) + Ui.P(26.f) + LineH*BREW_STEPS + LineH;
			const FRpgRect R( Ui.P(24.f), Ui.P(200.f), W, H );
			Ui.Panel( R );
			Ui.Text( HeadFont, R.X+R.W*0.5f, R.Y+Ui.P(8.f), RpgGoldBright, TEXT("Wiggenweld Potion"), RALIGN_Center, 0 );
			FLOAT Y = R.Y + Ui.TextHeight(HeadFont) + Ui.P(18.f);
			for( INT s=0; s<BREW_STEPS; s++ )
			{
				const FColor Col = s<BrewStepIndex ? RpgMuted : (s==BrewStepIndex ? RpgGoldBright : RpgParchment);
				Ui.Rect( FRpgRect(R.X+Ui.P(14.f), Y+LineH*0.5f-Ui.P(3.f), Ui.P(6.f), Ui.P(6.f)), s==BrewStepIndex ? RpgGoldBright : FColor(110,100,140,255) );
				Ui.Text( Ui.FontBody, R.X+Ui.P(28.f), Y, Col, *FString::Printf(TEXT("%i. %s"), s+1, BrewStep(s).Text), RALIGN_Left, 0 );
				Y += LineH;
			}
			Ui.Text( Ui.FontSmall, R.X+R.W*0.5f, Y+Ui.P(2.f), BrewMistakes ? RpgRed : RpgMuted, *FString::Printf(TEXT("Mistakes: %i"), BrewMistakes), RALIGN_Center, 0 );
		}

		// Instruction box, bottom centre.
		{
			FString Head = St.Text;
			const TCHAR* Hint = TEXT("");
			if( St.Type==BREW_Add )
			{
				Head = FString::Printf( TEXT("Put the %s into the cauldron"), BrewJarName(St.Jar) );
				Hint = TEXT("Drag a jar onto the cauldron and let go");
			}
			else if( St.Type==BREW_StirCW || St.Type==BREW_StirCCW )
			{
				Head = FString::Printf( TEXT("Stir %s:  %i / %i"), St.Type==BREW_StirCW ? TEXT("clockwise") : TEXT("counterclockwise"), BrewProgress, St.Count );
				Hint = TEXT("Hold the left mouse button on the cauldron and circle around it");
			}
			else
				Hint = TEXT("Click the cauldron or press Enter");
			const FLOAT W = Max( Ui.TextWidth(HeadFont,*Head), Ui.TextWidth(Ui.FontSmall,Hint) ) + Ui.P(48.f);
			const FLOAT H = Ui.TextHeight(HeadFont) + Ui.TextHeight(Ui.FontSmall) + Ui.P(22.f);
			const FRpgRect R( (C->ClipX-W)*0.5f, C->ClipY - H - Ui.P(40.f), W, H );
			Ui.Panel( R );
			Ui.Text( HeadFont, R.X+R.W*0.5f, R.Y+Ui.P(8.f), RpgGoldBright, *Head, RALIGN_Center, 0 );
			Ui.Text( Ui.FontSmall, R.X+R.W*0.5f, R.Y+Ui.P(12.f)+Ui.TextHeight(HeadFont), RpgParchment, Hint, RALIGN_Center, 0 );
		}
		Ui.Footer( TEXT("Mouse: drag jars and stir    Enter: finish    Esc: leave the class") );
	}

	void DrawBrewLabel( FLOAT X, FLOAT Y, const TCHAR* Text, FColor Color )
	{
		const FLOAT W = Ui.TextWidth( Ui.FontCaption, Text ) + Ui.P(16.f), H = Ui.TextHeight( Ui.FontCaption ) + Ui.P(8.f);
		const FRpgRect R( X-W*0.5f, Y-H, W, H );
		Ui.Glass( R );
		Ui.Border( R, Max(1.f,Ui.P(1.f)), Color );
		Ui.Text( Ui.FontCaption, X, R.Y+Ui.P(4.f), Color, Text, RALIGN_Center, 0 );
	}

	void DrawBrewResult( UGameEngine* Engine )
	{
		UCanvas* C = Ui.C;
		Ui.Sky( FRpgRect(0,0,C->ClipX,C->ClipY) );
		UFont* TitleFont = Ui.FontInk ? Ui.FontInk : Ui.FontTitle;
		const UBOOL bRuined = BrewGrade<=1;
		Ui.Text( TitleFont, C->ClipX*0.5f, C->ClipY*0.18f, RpgGoldBright, bRuined ? TEXT("The Potion Is Ruined") : TEXT("Wiggenweld Potion Completed!"), RALIGN_Center, 0 );
		static const INT Points[6] = { 0, -5, 5, 10, 15, 20 };
		static const TCHAR* Quality[6] = { TEXT(""), TEXT("Ruined"), TEXT("Poor - three mistakes or more"), TEXT("Acceptable - two mistakes"), TEXT("Good - one mistake"), TEXT("Excellent - no mistakes") };
		static const TCHAR* Verdict[6] = { TEXT(""), TEXT("Pathetic. Clean up this mess, Potter."), TEXT("Is this what passes for a potion in Gryffindor?"),
			TEXT("Passable. Barely."), TEXT("Hm. Not entirely useless."), TEXT("Hm. Adequate, Potter... for once.") };
		const INT G = Clamp( BrewGrade, 1, 5 );
		const FString L1 = FString::Printf( TEXT("Quality: %s"), Quality[G] );
		const FString L2 = FString::Printf( TEXT("Gryffindor %s%i Points"), Points[G]>=0 ? TEXT("+") : TEXT(""), Points[G] );
		const FString L3 = FString::Printf( TEXT("Professor Snape: \"%s\""), Verdict[G] );
		const FString L4 = G>=3 ? FString(TEXT("+1 Wiggenweld Potion")) : FString(TEXT(""));
		const FLOAT LineH = Ui.TextHeight( Ui.FontBody ) + Ui.P(14.f);
		FLOAT W = Max( Max(Ui.TextWidth(Ui.FontBody,*L1), Ui.TextWidth(Ui.FontBody,*L2)), Ui.TextWidth(Ui.FontBody,*L3) ) + Ui.P(80.f);
		W = Min( W, C->ClipX - Ui.P(40.f) );
		const FRpgRect R( (C->ClipX-W)*0.5f, C->ClipY*0.32f, W, LineH*(L4.Len() ? 4 : 3) + Ui.P(28.f) );
		Ui.Rect( R, FColor(224,206,160,255) );
		Ui.Border( R, Max(1.f,Ui.P(2.f)), RpgGold );
		const FColor Ink( 52,38,20,255 );
		FLOAT Y = R.Y + Ui.P(18.f);
		Ui.Text( Ui.FontBody, R.X+R.W*0.5f, Y, Ink, *L1, RALIGN_Center, 0 ); Y += LineH;
		Ui.Text( Ui.FontBody, R.X+R.W*0.5f, Y, Ink, *L2, RALIGN_Center, 0 ); Y += LineH;
		Ui.Text( Ui.FontBody, R.X+R.W*0.5f, Y, Ink, *Ui.FitText(Ui.FontBody,*L3,R.W-Ui.P(24.f)), RALIGN_Center, 0 ); Y += LineH;
		if( L4.Len() )
			Ui.Text( Ui.FontBody, R.X+R.W*0.5f, Y, FColor(40,110,50,255), *L4, RALIGN_Center, 0 );
		const FLOAT BW = Ui.P(180.f), BH = Ui.TextHeight(Ui.FontBody)+Ui.P(16.f);
		if( Ui.Button( FRpgRect((C->ClipX-BW)*0.5f, R.Y+R.H+Ui.P(30.f), BW, BH), TEXT("Continue"), 1, &BrewGrade ) )
			CloseBrew( Engine );
		Ui.Footer( TEXT("Enter: continue") );
	}
