/*=============================================================================
	HP2RpgMenus.inl: Menus, HUD, input and console commands of FHP2Rpg.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.
=============================================================================*/

	// Extra menu state.
	AActor* PreviewNpc;
	INT SpellTab;
	INT CollectionTab;
	UBOOL bActivate;

	/*-------------------------------------------------------------------------
		Opening and closing.
	-------------------------------------------------------------------------*/

	UViewport* MainViewport( UGameEngine* Engine )
	{
		return Engine->Client && Engine->Client->Viewports.Num() ? Engine->Client->Viewports(0) : NULL;
	}

	void OpenMenu( UGameEngine* Engine, INT NewMenu )
	{
		Menu = NewMenu;
		Sel = 0;
		Scroll = 0;
		ConfirmIndex = -1;
		Notice = TEXT("");
		bActivate = 0;
		ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
		if( !Info->Pauser.Len() )
		{
			Info->Pauser = TEXT("HP2Rpg");
			bWePaused = 1;
		}
		UViewport* Viewport = MainViewport( Engine );
		Ui.SetMenuMouse( Viewport, 1 );
		if( Viewport && Viewport->Input )
			Viewport->Input->ResetInput();
		Ui.Play( RSND_Open, CurrentHarry() );
	}

	void CloseMenu( UGameEngine* Engine )
	{
		Menu = MENU_None;
		ShopVendor = NULL;
		if( bWePaused && Engine->GLevel )
		{
			ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
			if( Info->Pauser==TEXT("HP2Rpg") )
				Info->Pauser = TEXT("");
		}
		bWePaused = 0;
		UViewport* Viewport = MainViewport( Engine );
		Ui.SetMenuMouse( Viewport, 0 );
		if( Viewport && Viewport->Input )
			Viewport->Input->ResetInput();
		APlayerPawn* Harry = CurrentHarry();
		if( Harry )
			ApplyLook( State.Equip[RSLOT_Outfit], State.Equip[RSLOT_Glasses] );
	}

	void SetNotice( const FString& Text )
	{
		Notice = Text;
		NoticeTime = 3.f;
	}

	/*-------------------------------------------------------------------------
		Vendors: daily offers, prices and transactions.
	-------------------------------------------------------------------------*/

	static INT ConsumableTab( INT Id )
	{
		switch( Id )
		{
			case CON_Potion: case CON_ChocFrog: return TAB_Potions;
			case CON_FMucus: case CON_WBark: case CON_Bicorn: case CON_Boomslang: return TAB_Ingredients;
			case CON_BronzeCard: case CON_SilverCard: return TAB_Cards;
			default: return TAB_Magic;
		}
	}

	void AddOffer( INT Kind, INT Id, INT Tab )
	{
		FOffer* O = new(Offers) FOffer;
		O->Kind = Kind;
		O->Id = Id;
		O->Tab = Tab;
	}

	void BuildOffers( INT Vendor )
	{
		Offers.Empty();
		const FVendorDef& V = GVendors[Vendor];
		for( INT c=0; c<CON_MAX; c++ )
		{
			if( !(V.Consumables & CONBIT(c)) )
				continue;
			INT Tab = ConsumableTab( c );
			if( !(V.Tabs & TABBIT(Tab)) )
				Tab = (V.Tabs & TABBIT(TAB_Magic)) ? TAB_Magic : Tab;
			AddOffer( OFFER_Consumable, c, Tab );
		}

		// Cosmetics rotate daily: a seeded pick from this vendor's pool.
		INT Pool[MAX_COSMETICS];
		DWORD Keys[MAX_COSMETICS];
		INT N = 0;
		for( INT i=0; i<GNumCosmetics; i++ )
		{
			const FCosmetic& C = GCosmetics[i];
			if( C.Price<=0 || !(V.Tabs & TABBIT(GCosmeticTab[C.Slot])) || !NameMatchesFilter(C.Name,V.CosmeticFilter) )
				continue;
			Pool[N] = i;
			Keys[N] = HashMix( (DWORD)(State.Day*7919 + Vendor*104729), (DWORD)i );
			N++;
		}
		for( INT a=0; a<N; a++ )
			for( INT b=a+1; b<N; b++ )
				if( Keys[b]<Keys[a] )
				{
					Exchange( Keys[a], Keys[b] );
					Exchange( Pool[a], Pool[b] );
				}
		const INT Take = Min( N, V.DailyCosmetics );
		for( INT a=0; a<Take; a++ )
			AddOffer( OFFER_Cosmetic, Pool[a], GCosmeticTab[GCosmetics[Pool[a]].Slot] );

		if( V.Tabs & TABBIT(TAB_Sell) )
		{
			AddOffer( OFFER_Sell, CON_Potion, TAB_Sell );
			AddOffer( OFFER_Sell, CON_FMucus, TAB_Sell );
			AddOffer( OFFER_Sell, CON_WBark, TAB_Sell );
		}
	}

	INT Discount()
	{
		return Clamp( GetStat(STAT_Discount), 0, 40 );
	}

	INT VendorPrice( INT Base )
	{
		const INT Markup = ShopVendorDef>=0 ? GVendors[ShopVendorDef].PriceMarkup : 0;
		return Max( 1, Base*(100+Markup)/100*(100-Discount())/100 );
	}

	INT OfferPrice( const FOffer& O )
	{
		if( O.Kind==OFFER_Consumable )
			return VendorPrice( GConsumables[O.Id].Price );
		if( O.Kind==OFFER_Cosmetic )
			return VendorPrice( GCosmetics[O.Id].Price );
		return O.Id==CON_Potion ? GSellPotion : GSellIngredient;
	}

	const TCHAR* OfferName( const FOffer& O )
	{
		if( O.Kind==OFFER_Cosmetic )
			return GCosmetics[O.Id].Name;
		return GConsumables[O.Id].Name;
	}

	INT StockLeft( const FOffer& O )
	{
		if( O.Kind!=OFFER_Consumable || ShopVendorDef<0 )
			return 99;
		return Max( 0, GConsumables[O.Id].DailyStock - State.StockUsed[ShopVendorDef*CON_MAX + O.Id] );
	}

	INT SellCount( APlayerPawn* Harry, INT Id )
	{
		UObject* Status = GetStatus( Harry );
		switch( Id )
		{
			case CON_Potion: return CallInt( Status, TEXT("GetPotionCount") );
			case CON_FMucus: return CallInt( Status, TEXT("GetFMucusCount") );
			case CON_WBark:  return CallInt( Status, TEXT("GetWBarkCount") );
		}
		return 0;
	}

	UBOOL GiveRandomCard( UObject* Status, UBOOL bSilver )
	{
		// IsOwnedByHarry lives on the wizard card status item, not the manager.
		UClass* Group = FindClass( TEXT("StatusGroupWizardCards") );
		UClass* ItemClass = FindClass( TEXT("StatusItemBronzeCards") );
		FRpgArg GetArgs[2] = { FRpgArg::O(Group), FRpgArg::O(ItemClass) };
		FRpgArg CardItem;
		if( !Group || !ItemClass || !CallScript( Status, TEXT("GetStatusItem"), GetArgs, 2, &CardItem ) || !CardItem.Obj )
			return 0;
		static const INT Bronze[] = { 1,2,4,5,6,7,9,10,12,18,19,21,24,25,26,28,29,31,32,33,37,44,46,47,49,50,51,53,57,58,61,63,64,66,67,75,76,77,78,80,81,83,84,86,88,91,93,94,95,96 };
		static const INT Gold[] = { 11,15,40,41,48,69,72,74,82,100,101 };
		INT Candidates[128];
		INT Num = 0;
		for( INT Id=1; Id<=99; Id++ )
		{
			UBOOL bIsBronze = 0, bIsGold = 0;
			for( INT i=0; i<ARRAY_COUNT(Bronze); i++ ) if( Bronze[i]==Id ) bIsBronze = 1;
			for( INT i=0; i<ARRAY_COUNT(Gold); i++ ) if( Gold[i]==Id ) bIsGold = 1;
			if( bIsGold || (bSilver ? bIsBronze : !bIsBronze) )
				continue;
			FRpgArg Arg = FRpgArg::I( Id );
			FRpgArg Ret;
			if( !CallScript( CardItem.Obj, TEXT("IsOwnedByHarry"), &Arg, 1, &Ret ) )
				return 0;
			if( !Ret.Int )
				Candidates[Num++] = Id;
		}
		if( !Num )
			return 0;
		FRpgArg Arg = FRpgArg::I( Candidates[RpgRand(Num)] );
		return CallScript( Status, TEXT("GiveCardToHarry"), &Arg, 1 );
	}

	void Respec( APlayerPawn* Harry )
	{
		INT Refund = 0;
		for( INT s=0; s<SKILL_MAX; s++ )
		{
			Refund += State.Skill[s];
			State.Skill[s] = 0;
		}
		for( INT p=0; p<PERK_MAX; p++ )
			if( HasPerk(p) )
				Refund += GPerks[p].Cost;
		State.Perks = 0;
		State.SkillPoints += Refund;
		if( Harry )
			SetBoolProp( Harry, TEXT("bCastFastSpells"), 0 );
		AddFeed( TEXT("Forgetfulness Potion"), FColor(200,170,255,255), TOAST_Reward, *FString::Printf(TEXT("%i skill points to spend again"), Refund) );
	}

	UBOOL ApplyConsumable( APlayerPawn* Harry, INT Id, INT Count )
	{
		UObject* Status = GetStatus( Harry );
		FRpgArg N = FRpgArg::I( Count );
		switch( Id )
		{
			case CON_Potion:     return CallScript( Status, TEXT("AddPotions"), &N, 1 );
			case CON_FMucus:     return CallScript( Status, TEXT("AddFMucus"), &N, 1 );
			case CON_WBark:      return CallScript( Status, TEXT("AddWBark"), &N, 1 );
			case CON_Bicorn:     return CallScript( Status, TEXT("AddBicorn"), &N, 1 );
			case CON_Boomslang:  return CallScript( Status, TEXT("AddBoomslang"), &N, 1 );
			case CON_BronzeCard: return GiveRandomCard( Status, 0 );
			case CON_SilverCard: return GiveRandomCard( Status, 1 );
			case CON_Lesson:     State.SkillPoints += Count; return 1;
			case CON_Respec:     Respec( Harry ); return 1;
			case CON_ChocFrog:
			{
				FRpgArg Full = FRpgArg::I( CallInt(Status,TEXT("GetHealthPotentialCount"),100) );
				return CallScript( Status, TEXT("SetHealthCount"), &Full, 1 );
			}
			case CON_Felix:      State.FelixSeconds = 300; return 1;
			case CON_Pardon:     State.Trouble = Max( 0, State.Trouble-60 ); State.DetentionSeconds = 0; return 1;
		}
		return 0;
	}

	UBOOL VendorRefuses()
	{
		return State.DetentionSeconds>0 && ShopVendorDef!=VEN_Pomfrey;
	}

	void Transact( APlayerPawn* Harry, const FOffer& O )
	{
		if( VendorRefuses() )
		{
			Speech = TEXT("You're in detention, Potter. Come back later.");
			Ui.Play( RSND_Deny, Harry );
			return;
		}
		const INT Price = OfferPrice( O );
		const INT Beans = GetBeans( Harry );
		const TCHAR* Name = OfferName( O );

		if( O.Kind==OFFER_Cosmetic && IsOwned(O.Id) )
		{
			State.Equip[GCosmetics[O.Id].Slot] = O.Id;
			Speech = FString::Printf( TEXT("Looking good in that %s!"), Name );
			Ui.Play( RSND_Click, Harry );
			return;
		}
		if( O.Kind==OFFER_Sell )
		{
			if( SellCount(Harry,O.Id)<=0 )
			{
				Speech = TEXT("You haven't got any of those to sell.");
				Ui.Play( RSND_Deny, Harry );
				return;
			}
			ApplyConsumable( Harry, O.Id, -1 );
			GiveBeans( Harry, Price );
			Speech = FString::Printf( TEXT("I'll take that %s. Here's %i beans."), Name, Price );
			Ui.Play( RSND_Buy, Harry );
			return;
		}
		if( O.Kind==OFFER_Consumable && StockLeft(O)<=0 )
		{
			Speech = TEXT("Sold out for today. Come back tomorrow!");
			Ui.Play( RSND_Deny, Harry );
			return;
		}
		if( Beans < Price )
		{
			Speech = FString::Printf( TEXT("That's %i beans. You've only got %i."), Price, Beans );
			Ui.Play( RSND_Deny, Harry );
			return;
		}
		if( O.Kind==OFFER_Consumable )
		{
			if( !ApplyConsumable(Harry,O.Id,GConsumables[O.Id].Count) )
			{
				Speech = O.Id==CON_BronzeCard || O.Id==CON_SilverCard ? TEXT("You've already got every one of those cards!") : TEXT("I can't sell you that right now.");
				Ui.Play( RSND_Deny, Harry );
				return;
			}
			State.StockUsed[ShopVendorDef*CON_MAX + O.Id]++;
		}
		else
		{
			SetOwned( O.Id );
			State.Equip[GCosmetics[O.Id].Slot] = O.Id;
		}
		GiveBeans( Harry, -Price );
		State.Counters[CNT_Bought]++;
		Speech = FString::Printf( TEXT("%s. Pleasure doing business!"), Name );
		Ui.Play( RSND_Buy, Harry );
		RpgLog( TEXT("bought %s for %i (had %i)"), Name, Price, Beans );
	}

	/*-------------------------------------------------------------------------
		3D previews.
	-------------------------------------------------------------------------*/

	// Points A at a mesh, resetting animation state when the mesh changes so
	// it uses that mesh's own default animation.
	void SetPreviewMesh( AActor* A, UMesh* Mesh )
	{
		if( A->Mesh==Mesh )
			return;
		A->Mesh = Mesh;
		A->SkelAnim = NULL;
		A->AnimSequence = NAME_None;
		A->AnimFrame = 0.f;
		for( INT i=0; i<8; i++ )
			A->MultiSkins[i] = NULL;
	}

	AActor* EnsurePreview( UGameEngine* Engine, AActor*& A )
	{
		if( A && !IsInLevel(A) )
			A = NULL;
		if( !A )
		{
			A = SpawnRenderActor( Engine, NULL, FVector(0,0,0) );
			if( A )
			{
				A->DrawType = DT_Mesh;
				A->bHidden = 1;
				A->bUnlit = 1;
			}
		}
		return A;
	}

	// Places Body so the given bone sits in the middle of the preview camera.
	void FocusOnBone( AActor* Body, INT Bone, USkeletalMesh* Skel, FLOAT Dist, FLOAT Yaw, FLOAT Lift )
	{
		Body->Location = FVector( 0, 0, 0 );
		Body->Rotation = FRotator( 0, 32768 + appRound(Yaw), 0 );
		const FVector Point = Skel->GetBoneCoords( Body, Bone ).Origin;
		Body->Location = FVector( Dist, 0, Lift ) - Point;
	}

	void CopyAnim( AActor* Dest, AActor* Src )
	{
		Dest->SkelAnim = Src->SkelAnim;
		Dest->AnimSequence = Src->AnimSequence;
		Dest->AnimFrame = Src->AnimFrame;
		Dest->AnimRate = 0.f;
	}

	// Draws Harry wearing the given cosmetics into R.
	void DrawHarryPreview( UGameEngine* Engine, const FRpgRect& R, INT Outfit, INT Glasses, INT Hat, INT Pet, INT Framing, FLOAT Yaw )
	{
		APlayerPawn* Harry = CurrentHarry();
		if( !Harry || !HarryMesh )
			return;
		AActor* Body = EnsurePreview( Engine, PreviewBody );
		AActor* Attach = EnsurePreview( Engine, PreviewAttach );
		AActor* Item = EnsurePreview( Engine, PreviewItem );
		if( !Body )
			return;
		SetCostume( Body, Outfit, 0 );
		CopyAnim( Body, Harry );
		ApplyLook( Outfit, Glasses );

		// Full body fits the rect; close-ups zoom in on a bone.
		USkeletalMesh* Skel = NULL;
		INT Bone = -1;
		FLOAT Zoom = 1.f;
		if( Framing==PREV_Head )
		{
			Bone = HeadBone( Body, Skel );
			Zoom = 3.2f;
		}
		else if( Framing==PREV_Shoulder )
		{
			Bone = ShoulderBone( Body, Skel );
			Zoom = 2.2f;
		}
		if( !Ui.PlaceInRect( Body, R, Yaw, Bone>=0 ? Zoom : 1.f ) )
			return;
		if( Bone>=0 && Skel )
			Ui.FocusPoint( Body, Skel->GetBoneCoords(Body,Bone).Origin, R.X+R.W*0.5f, R.Y+R.H*(Framing==PREV_Head ? 0.62f : 0.5f) );
		Ui.DrawActorInRect( Body, 1 );

		if( Attach && GCosmetics[Hat].Mesh )
		{
			UMesh* Mesh = LoadMesh( GCosmetics[Hat].Mesh );
			if( Mesh )
			{
				SetPreviewMesh( Attach, Mesh );
				if( PlaceHat(Body,Attach,GCosmetics[Hat],NULL) )
					Ui.DrawActorInRect( Attach, 0 );
			}
		}
		if( Item && GCosmetics[Pet].Mesh )
		{
			UMesh* Mesh = LoadMesh( GCosmetics[Pet].Mesh );
			if( Mesh )
			{
				SetPreviewMesh( Item, Mesh );
				if( PlacePet(Body,Item,GCosmetics[Pet],NULL) )
					Ui.DrawActorInRect( Item, 0 );
			}
		}
	}

	void DrawMeshPreview( UGameEngine* Engine, const FRpgRect& R, const TCHAR* MeshPath, FLOAT Scale, FLOAT Yaw )
	{
		AActor* Item = EnsurePreview( Engine, PreviewItem );
		UMesh* Mesh = LoadMesh( MeshPath );
		if( !Item || !Mesh )
			return;
		SetPreviewMesh( Item, Mesh );
		if( Ui.PlaceInRect( Item, R, Yaw, 1.f ) )
			Ui.DrawActorInRect( Item, 1 );
	}

	void DrawNpcPreview( UGameEngine* Engine, const FRpgRect& R, AActor* Npc, FLOAT Yaw )
	{
		AActor* Body = EnsurePreview( Engine, PreviewNpc );
		if( !Body || !Npc || !Npc->Mesh )
			return;
		Body->Mesh = Npc->Mesh;
		Body->Skin = Npc->Skin;
		for( INT i=0; i<8; i++ )
			Body->MultiSkins[i] = Npc->MultiSkins[i];
		CopyAnim( Body, Npc );
		if( Ui.PlaceInRect( Body, R, Yaw, 1.f ) )
			Ui.DrawActorInRect( Body, 1 );
	}

	// What a cosmetic tile shows: Harry wearing it, framed for its slot.
	void DrawCosmeticPreview( UGameEngine* Engine, const FRpgRect& R, INT Id, FLOAT Yaw )
	{
		const FCosmetic& C = GCosmetics[Id];
		INT Outfit = State.Equip[RSLOT_Outfit], Glasses = State.Equip[RSLOT_Glasses];
		INT Hat = DefaultCosmetic(RSLOT_Hat), Pet = DefaultCosmetic(RSLOT_Pet);
		INT Framing = PREV_Body;
		switch( C.Slot )
		{
			case RSLOT_Outfit:  Outfit = Id; break;
			case RSLOT_Hat:     Hat = Id; Framing = PREV_Head; break;
			case RSLOT_Glasses: Glasses = Id; Framing = PREV_Head; break;
			case RSLOT_Pet:     Pet = Id; Framing = PREV_Shoulder; break;
			case RSLOT_Held:
				if( C.Mesh )
				{
					DrawMeshPreview( Engine, R, C.Mesh, C.Scale, Yaw );
					return;
				}
				break;
			case RSLOT_Aura:
			{
				// Particle auras can't be previewed in a tile; show a star and the name.
				UTexture* Star = LoadTex( TEXT("HP2_Menu.Icons.HP2StarCounter") );
				const FLOAT Sz = Min( R.W, R.H )*0.45f;
				if( Star && C.Mesh )
					Ui.Icon( Star, FRpgRect(R.X+(R.W-Sz)*0.5f, R.Y+4*Ui.S, Sz, Sz) );
				Ui.Text( Ui.FontBody, R.X+R.W*0.5f, R.Y+Sz+8*Ui.S, RpgParchment, *Ui.FitText(Ui.FontBody,C.Name,R.W-6*Ui.S), RALIGN_Center );
				return;
			}
			default: break;
		}
		DrawHarryPreview( Engine, R, Outfit, Glasses, Hat, Pet, Framing, Yaw );
	}

	void DrawConsumablePreview( UGameEngine* Engine, const FRpgRect& R, INT Id, FLOAT Yaw )
	{
		const FConsumable& C = GConsumables[Id];
		UTexture* Icon = LoadTex( C.Icon );
		if( C.Mesh && LoadMesh(C.Mesh) )
			DrawMeshPreview( Engine, R, C.Mesh, C.MeshScale, Yaw );
		else if( Icon )
		{
			const FLOAT S = Min( R.W, R.H )*0.8f;
			Ui.Icon( Icon, FRpgRect(R.X+(R.W-S)*0.5f, R.Y+(R.H-S)*0.5f, S, S) );
		}
	}

	/*-------------------------------------------------------------------------
		Drawing: entry point and HUD.
	-------------------------------------------------------------------------*/

	void ResolveHudTextures()
	{
		if( BeanIcon )
			return;
		BeanIcon = LoadTex( TEXT("HP2_Menu.Icons.HP2BeanCounter") );
		for( INT i=0; i<HOUSE_MAX; i++ )
			Crests[i] = LoadTex( GHouseCrests[i] );
	}

	void Draw( UGameEngine* Engine, UViewport* Viewport, UCanvas* C )
	{
		if( !bInitialized || !bEnabled || !Engine->GLevel )
			return;
		APlayerPawn* Harry = GetHarry( Engine );
		if( !Harry || Harry!=LastHarry )
			return;
		ResolveHudTextures();

		const FLOAT SavedOrgX = C->OrgX, SavedOrgY = C->OrgY, SavedClipX = C->ClipX, SavedClipY = C->ClipY;
		const FLOAT SavedCurX = C->CurX, SavedCurY = C->CurY;
		const FColor SavedColor = C->Color;
		const BYTE SavedStyle = C->Style;
		C->OrgX = 0.f;
		C->OrgY = 0.f;
		C->ClipX = C->X;
		C->ClipY = C->Y;
		C->Style = STY_Normal;

		Ui.Begin( Engine, Viewport, C, Menu!=MENU_None );
		const UBOOL bGameplay = IsFreeGameplay( Engine, Harry );
		if( Menu==MENU_None )
		{
			if( bGameplay || HouseHudTimer>0.f )
				DrawStatusHud( Harry, bGameplay );
			DrawFeed();
		}
		else if( Menu==MENU_Shop )
			DrawShop( Engine, Harry );
		else if( Menu==MENU_Book )
			DrawBook( Engine, Harry );
		if( Menu!=MENU_None )
			DrawFeed();
		Ui.End();
		bActivate = 0;

		C->OrgX = SavedOrgX;
		C->OrgY = SavedOrgY;
		C->ClipX = SavedClipX;
		C->ClipY = SavedClipY;
		C->CurX = SavedCurX;
		C->CurY = SavedCurY;
		C->Color = SavedColor;
		C->Style = SavedStyle;
	}

	void DrawStatusHud( APlayerPawn* Harry, UBOOL bGameplay )
	{
		UCanvas* C = Ui.C;
		const FLOAT S = Ui.S;
		const FLOAT Crest = 18*S, Gap = 50*S;
		const FLOAT X0 = C->ClipX*0.5f - Gap*1.5f, Y0 = 6*S;
		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			const FLOAT X = X0 + Gap*i;
			if( Crests[i] )
				Ui.Icon( Crests[i], FRpgRect(X-Crest*0.5f, Y0, Crest, Crest), (PendingHouse[i] || i==HOUSE_Gryffindor) ? 1.f : 0.7f );
			Ui.Text( Ui.FontModernBold, X, Y0+Crest+1*S, GHouseColors[i], *FString::Printf(TEXT("%i"), bBaselineValid ? LastHouse[i] : 0), RALIGN_Center );
		}
		const FLOAT BarW = 120*S, BarH = 4*S;
		const FLOAT BarX = C->ClipX*0.5f - BarW*0.5f, BarY = Y0 + Crest + 16*S;
		Ui.Bar( FRpgRect(BarX,BarY,BarW,BarH), (FLOAT)State.Xp/XpForNextLevel(State.Level), FColor(120,190,255,255) );
		Ui.Text( Ui.FontModernBold, BarX-6*S, BarY-Ui.TextHeight(Ui.FontModernBold)*0.5f, RpgGoldBright, *FString::Printf(TEXT("Lv %i"), State.Level), RALIGN_Right );
		if( State.SkillPoints>0 )
			Ui.Text( Ui.FontModern, BarX+BarW+6*S, BarY-Ui.TextHeight(Ui.FontModern)*0.5f, RpgGreen, *FString::Printf(TEXT("%i point%s [K]"), State.SkillPoints, State.SkillPoints>1 ? TEXT("s") : TEXT("")) );
		FLOAT LineY = BarY + 8*S;
		if( State.DetentionSeconds>0 )
		{
			Ui.Text( Ui.FontSmall, C->ClipX*0.5f, LineY, RpgRed, *FString::Printf(TEXT("Detention %i:%02i"), State.DetentionSeconds/60, State.DetentionSeconds%60), RALIGN_Center );
			LineY += Ui.TextHeight(Ui.FontSmall);
		}
		if( State.FelixSeconds>0 )
			Ui.Text( Ui.FontSmall, C->ClipX*0.5f, LineY, FColor(255,220,120,255), *FString::Printf(TEXT("Felix Felicis %i:%02i"), State.FelixSeconds/60, State.FelixSeconds%60), RALIGN_Center );

		if( bGameplay && NearNpc && IsInLevel(NearNpc) )
		{
			FString Prompt = NpcName( NearNpc ) + TEXT("    [E] Talk");
			if( NearVendor>=0 )
				Prompt += TEXT("    [B] Trade");
			const FLOAT PW = Ui.TextWidth( Ui.FontModernBold, *Prompt ) + 24*S, PH = Ui.TextHeight( Ui.FontModernBold ) + 10*S;
			const FRpgRect PR( (C->ClipX-PW)*0.5f, C->ClipY*0.78f, PW, PH );
			Ui.Rect( PR, FColor(14,18,38,255) );
			Ui.Rect( FRpgRect(PR.X, PR.Y+PR.H-2*S, PR.W, 2*S), RpgGold );
			Ui.Text( Ui.FontModernBold, C->ClipX*0.5f, PR.Y+5*S, FColor(240,240,248,255), *Prompt, RALIGN_Center, 0 );
		}
	}

	UTexture* ToastIcon( const FRpgFeed& Entry )
	{
		static UTexture* Star = NULL;
		static UTexture* Key = NULL;
		if( !Star )
		{
			Star = LoadTex( TEXT("HP2_Menu.Icons.HP2StarCounter") );
			Key = LoadTex( TEXT("HP2_Menu.Icons.HP2SilverCardKey") );
		}
		switch( Entry.Kind )
		{
			case TOAST_Bean:    return BeanIcon;
			case TOAST_House:   return Entry.Amount>=0 && Entry.Amount<HOUSE_MAX ? Crests[Entry.Amount] : NULL;
			case TOAST_Trouble: return Key;
			default:            return Star;
		}
	}

	// Notification cards on the right, sliding in and out.
	void DrawFeed()
	{
		UCanvas* C = Ui.C;
		const FLOAT S = Ui.S;
		UFont* Title = Ui.FontModernBold;
		UFont* Sub = Ui.FontModern;
		const FLOAT TitleH = Ui.TextHeight( Title ), SubH = Ui.TextHeight( Sub );
		const FLOAT W = Min( 250*S, C->ClipX*0.32f );
		FLOAT Y = C->ClipY*0.16f;
		for( INT i=Feed.Num()-1; i>=0; i-- )
		{
			const FRpgFeed& Entry = Feed(i);
			const UBOOL bSub = Entry.Sub.Len()>0;
			const FLOAT H = 10*S + TitleH + (bSub ? SubH : 0.f);
			// Slide in over 0.2s, out after 3.6s.
			FLOAT Slide = 0.f;
			if( Entry.Age<0.2f )
				Slide = 1.f - Entry.Age/0.2f;
			else if( Entry.Age>3.6f )
				Slide = Min( 1.f, (Entry.Age-3.6f)/0.4f );
			const FLOAT X = C->ClipX - W - 14*S + Slide*(W+20*S);
			const FRpgRect Card( X, Y, W, H );
			Ui.Rect( Card, FColor(14,18,38,255) );
			Ui.Rect( FRpgRect(Card.X, Card.Y, 3*S, Card.H), Entry.Color );
			Ui.Rect( FRpgRect(Card.X, Card.Y+Card.H-1, Card.W, Max(1.f,S*0.5f)), FColor(48,56,96,255) );
			FLOAT TX = Card.X + 10*S;
			UTexture* Icon = ToastIcon( Entry );
			if( Icon )
			{
				const FLOAT IS = H - 10*S;
				Ui.Icon( Icon, FRpgRect(TX, Card.Y+5*S, IS, IS) );
				TX += IS + 8*S;
			}
			Ui.Text( Title, TX, Card.Y+5*S, FColor(240,240,248,255), *Ui.FitText(Title,*Entry.Text,Card.X+Card.W-TX-6*S), RALIGN_Left, 0 );
			if( bSub )
				Ui.Text( Sub, TX, Card.Y+5*S+TitleH, FColor(150,158,190,255), *Ui.FitText(Sub,*Entry.Sub,Card.X+Card.W-TX-6*S), RALIGN_Left, 0 );
			Y += H + 6*S;
		}

		// Level-up banner.
		if( BannerText.Len() && BannerAge<3.5f )
		{
			const FLOAT Grow = Min( 1.f, BannerAge/0.25f );
			const FLOAT BW = C->ClipX*0.42f*Grow, BH = 64*S;
			const FRpgRect B( (C->ClipX-BW)*0.5f, C->ClipY*0.18f, BW, BH );
			Ui.Rect( B, FColor(12,16,36,255) );
			Ui.Rect( FRpgRect(B.X, B.Y, B.W, 2*S), RpgGoldBright );
			Ui.Rect( FRpgRect(B.X, B.Y+B.H-2*S, B.W, 2*S), RpgGoldBright );
			if( Grow>=1.f )
			{
				Ui.Text( Ui.FontModernBig, C->ClipX*0.5f, B.Y+6*S, RpgGoldBright, *BannerText, RALIGN_Center );
				Ui.Text( Ui.FontModern, C->ClipX*0.5f, B.Y+B.H-SubH-8*S, FColor(220,224,240,255), *BannerSub, RALIGN_Center, 0 );
			}
		}
	}

	/*-------------------------------------------------------------------------
		Shared menu pieces.
	-------------------------------------------------------------------------*/

	// A row of tabs across R; returns the clicked index or -1.
	INT DrawTabs( const FRpgRect& R, const TCHAR* const* Labels, const INT* Ids, INT Count, INT Active )
	{
		// Pick the largest font that fits, then size each tab to its label.
		const FLOAT Pad = 14*Ui.S;
		UFont* Font = Ui.FontToFit( Labels, Count, R.W, Pad );
		FLOAT Total = 0.f;
		for( INT i=0; i<Count; i++ )
			Total += Ui.TextWidth( Font, Labels[i] ) + Pad;
		const FLOAT Extra = Count ? Max( 0.f, (R.W-Total)/Count ) : 0.f;
		INT Clicked = -1;
		FLOAT X = R.X;
		for( INT i=0; i<Count; i++ )
		{
			const INT Id = Ids ? Ids[i] : i;
			const FLOAT W = Ui.TextWidth( Font, Labels[i] ) + Pad + Extra;
			if( Ui.Tab( FRpgRect(X, R.Y, W, R.H), Labels[i], Id==Active, &Labels[i], Font ) )
				Clicked = Id;
			X += W;
		}
		Ui.Rect( FRpgRect(R.X, R.Y+R.H-1*Ui.S, R.W, Max(1.f,Ui.S*0.5f)), FColor(120,104,60,255) );
		return Clicked;
	}

	void DrawBeansLine( APlayerPawn* Harry, FLOAT X, FLOAT Y )
	{
		const FLOAT S = Ui.S;
		const FLOAT IconSize = 24*S;
		if( BeanIcon )
			Ui.Icon( BeanIcon, FRpgRect(X, Y-IconSize*0.2f, IconSize, IconSize) );
		Ui.Text( Ui.FontItem, X+IconSize+6*S, Y, RpgGoldBright, *FString::Printf(TEXT("Your beans: %i"), GetBeans(Harry)) );
	}

	FString StatText( INT Stat, INT Value )
	{
		return Stat!=STAT_None ? FString::Printf( GStatFormat[Stat], Value ) : FString();
	}

	// Grid layout helper: returns the rectangle of visible cell Index.
	FRpgRect GridCell( const FRpgRect& Area, INT Cols, INT Rows, INT Index )
	{
		const FLOAT Gap = 8*Ui.S;
		const FLOAT W = (Area.W - Gap*(Cols-1))/Cols;
		const FLOAT H = (Area.H - Gap*(Rows-1))/Rows;
		return FRpgRect( Area.X + (Index%Cols)*(W+Gap), Area.Y + (Index/Cols)*(H+Gap), W, H );
	}

	void ClampSelection( INT Count, INT Cols, INT Rows )
	{
		Sel = Count>0 ? Clamp( Sel, 0, Count-1 ) : 0;
		const INT SelRow = Sel/Cols;
		if( SelRow < Scroll )
			Scroll = SelRow;
		if( SelRow >= Scroll+Rows )
			Scroll = SelRow-Rows+1;
		const INT MaxScroll = Max( 0, (Count+Cols-1)/Cols - Rows );
		Scroll = Clamp( Scroll, 0, MaxScroll );
	}

	void ApplyWheel( INT Count, INT Cols, INT Rows )
	{
		if( Ui.WheelDelta )
		{
			Scroll -= Ui.WheelDelta;
			const INT MaxScroll = Max( 0, (Count+Cols-1)/Cols - Rows );
			Scroll = Clamp( Scroll, 0, MaxScroll );
			Ui.WheelDelta = 0;
		}
	}

	void DrawScrollHints( const FRpgRect& Area, INT Count, INT Cols, INT Rows )
	{
		const INT TotalRows = (Count+Cols-1)/Cols;
		if( Scroll>0 )
			Ui.Text( Ui.FontBody, Area.X+Area.W, Area.Y-Ui.TextHeight(Ui.FontBody), RpgGoldBright, TEXT("^"), RALIGN_Right );
		if( Scroll+Rows<TotalRows )
			Ui.Text( Ui.FontBody, Area.X+Area.W, Area.Y+Area.H, RpgGoldBright, TEXT("v"), RALIGN_Right );
	}

	/*-------------------------------------------------------------------------
		Shop.
	-------------------------------------------------------------------------*/

	void DrawShop( UGameEngine* Engine, APlayerPawn* Harry )
	{
		const FLOAT S = Ui.S;
		FRpgRect Left, Right;
		Ui.Screen( Left, Right );
		const FVendorDef& V = GVendors[ShopVendorDef];

		// Left: the vendor in 3D, their name and what they say.
		const FRpgRect Stage( Left.X, Left.Y, Left.W, Left.H*0.66f );
		Ui.Rect( Stage, FColor(14,18,46,255) );
		if( ShopVendor && !IsInLevel(ShopVendor) )
			ShopVendor = NULL;
		if( ShopVendor )
			DrawNpcPreview( Engine, Stage, ShopVendor, PreviewYaw*0.15f );
		FLOAT Y = Stage.Y + Stage.H + 6*S;
		const FString VendorName = ShopVendorDef>=VEN_StudentGryffindor && ShopVendor ? NpcName(ShopVendor) : FString(V.Name);
		Ui.Text( Ui.FontTitle, Left.X+Left.W*0.5f, Y, RpgGoldBright, *VendorName, RALIGN_Center );
		Y += Ui.TextHeight( Ui.FontTitle ) + 6*S;
		const FRpgRect Bubble( Left.X, Y, Left.W, Left.Y+Left.H - Y - 36*S );
		Ui.Rect( Bubble, FColor(20,26,62,255) );
		Ui.TextWrapped( Ui.FontBody, Bubble.Inset(8*S), RpgParchment, VendorRefuses() ? TEXT("You're in detention, Potter. No trading until it's over.") : *Speech );
		DrawBeansLine( Harry, Left.X, Left.Y+Left.H-24*S );

		// Right: title, tabs, grid.
		FLOAT RY = Right.Y;
		Ui.Text( Ui.FontTitle, Right.X, RY, RpgGoldBright, TEXT("Shop") );
		if( Discount()>0 )
			Ui.Text( Ui.FontBody, Right.X+Right.W, RY+6*S, RpgGreen, *FString::Printf(TEXT("-%i%% discount"), Discount()), RALIGN_Right );
		RY += Ui.TextHeight( Ui.FontTitle ) + 6*S;

		INT TabIds[TAB_MAX];
		const TCHAR* TabLabels[TAB_MAX];
		INT NumTabs = 0;
		for( INT t=0; t<TAB_MAX; t++ )
			for( INT o=0; o<Offers.Num(); o++ )
				if( Offers(o).Tab==t )
				{
					TabIds[NumTabs] = t;
					TabLabels[NumTabs] = GShopTabNames[t];
					NumTabs++;
					break;
				}
		if( !NumTabs )
		{
			if( Ui.Button( FRpgRect(Right.X+Right.W-110*S, Right.Y+Right.H-28*S, 110*S, 24*S), TEXT("Close"), 1, &Offers ) )
				CloseMenu( Engine );
			return;
		}
		UBOOL bTabValid = 0;
		for( INT i=0; i<NumTabs; i++ )
			if( TabIds[i]==ShopTab )
				bTabValid = 1;
		if( !bTabValid )
			ShopTab = TabIds[0];
		const INT ClickedTab = DrawTabs( FRpgRect(Right.X, RY, Right.W, Ui.TextHeight(Ui.FontItem)+8*S), TabLabels, TabIds, NumTabs, ShopTab );
		if( ClickedTab>=0 && ClickedTab!=ShopTab )
		{
			ShopTab = ClickedTab;
			Sel = Scroll = 0;
			ConfirmIndex = -1;
			Ui.Play( RSND_Tab, Harry );
		}
		RY += Ui.TextHeight( Ui.FontItem ) + 16*S;

		INT Items[64];
		INT Count = 0;
		for( INT o=0; o<Offers.Num() && Count<64; o++ )
			if( Offers(o).Tab==ShopTab )
				Items[Count++] = o;
		const INT Cols = 3, Rows = 2;
		ApplyWheel( Count, Cols, Rows );
		ClampSelection( Count, Cols, Rows );
		const FRpgRect GridArea( Right.X, RY, Right.W, Right.H*0.52f );
		const INT Beans = GetBeans( Harry );
		for( INT v=0; v<Cols*Rows; v++ )
		{
			const INT Index = Scroll*Cols + v;
			if( Index>=Count )
				break;
			const FOffer& O = Offers(Items[Index]);
			const FRpgRect Cell = GridCell( GridArea, Cols, Rows, v );
			const UBOOL bHover = Ui.Hovered( Cell, &Offers(Items[Index]) );
			const FColor Accent = O.Kind==OFFER_Cosmetic ? GRarityColors[GCosmetics[O.Id].Rarity] : FColor(70,78,120,255);
			Ui.Tile( Cell, Index==Sel, bHover, Accent );
			const FRpgRect Pic( Cell.X+3*S, Cell.Y+3*S, Cell.W-6*S, Cell.H-Ui.TextHeight(Ui.FontBody)-8*S );
			const FLOAT Yaw = (Index==Sel || bHover) ? PreviewYaw : 0.f;
			if( O.Kind==OFFER_Cosmetic )
				DrawCosmeticPreview( Engine, Pic, O.Id, Yaw );
			else
				DrawConsumablePreview( Engine, Pic, O.Id, Yaw );
			FString PriceText;
			FColor PriceColor = RpgParchment;
			const INT Price = OfferPrice( O );
			if( O.Kind==OFFER_Cosmetic && IsOwned(O.Id) )
			{
				PriceText = State.Equip[GCosmetics[O.Id].Slot]==O.Id ? TEXT("Wearing") : TEXT("Owned");
				PriceColor = RpgGreen;
			}
			else if( O.Kind==OFFER_Sell )
			{
				PriceText = FString::Printf( TEXT("+%i (have %i)"), Price, SellCount(Harry,O.Id) );
				PriceColor = RpgGreen;
			}
			else if( StockLeft(O)<=0 )
			{
				PriceText = TEXT("Sold out");
				PriceColor = RpgMuted;
			}
			else
			{
				PriceText = FString::Printf( TEXT("%i beans"), Price );
				PriceColor = Price<=Beans ? RpgParchment : RpgRed;
			}
			Ui.Text( Ui.FontBody, Cell.X+Cell.W*0.5f, Cell.Y+Cell.H-Ui.TextHeight(Ui.FontBody)-3*S, PriceColor, *PriceText, RALIGN_Center );
			if( Ui.Clicked( Cell, &Offers(Items[Index]) ) )
			{
				if( Sel==Index && ConfirmIndex==Index )
					bActivate = 1;
				Sel = Index;
				Ui.Play( RSND_Click, Harry );
			}
		}
		DrawScrollHints( GridArea, Count, Cols, Rows );

		// Selected item details and the buy button.
		FLOAT DY = GridArea.Y + GridArea.H + 10*S;
		if( Count>0 )
		{
			const FOffer& O = Offers(Items[Sel]);
			Ui.Text( Ui.FontTitle, Right.X, DY, RpgGoldBright, OfferName(O) );
			DY += Ui.TextHeight( Ui.FontTitle );
			FString Meta;
			if( O.Kind==OFFER_Cosmetic )
			{
				const FCosmetic& Cos = GCosmetics[O.Id];
				Meta = FString::Printf( TEXT("%s %s"), GRarityNames[Cos.Rarity], GSlotNames[Cos.Slot] );
				if( Cos.Stat!=STAT_None )
					Meta += FString(TEXT("  ")) + StatText( Cos.Stat, Cos.StatValue );
				Ui.Text( Ui.FontBody, Right.X, DY, GRarityColors[Cos.Rarity], *Meta );
			}
			else if( O.Kind==OFFER_Consumable )
				Ui.Text( Ui.FontBody, Right.X, DY, RpgMuted, *FString::Printf(TEXT("%i left today"), StockLeft(O)) );
			else
				Ui.Text( Ui.FontBody, Right.X, DY, RpgMuted, TEXT("Sell one to the vendor") );
			DY += Ui.TextHeight( Ui.FontBody ) + 2*S;
			const TCHAR* Desc = O.Kind==OFFER_Cosmetic ? GCosmetics[O.Id].Desc : GConsumables[O.Id].Desc;
			Ui.TextWrapped( Ui.FontBody, FRpgRect(Right.X, DY, Right.W, Right.Y+Right.H-DY-34*S), RpgParchment, Desc );

			const UBOOL bOwned = O.Kind==OFFER_Cosmetic && IsOwned(O.Id);
			const TCHAR* Label = bOwned ? TEXT("Wear") : (O.Kind==OFFER_Sell ? TEXT("Sell") : (ConfirmIndex==Sel ? TEXT("Confirm") : TEXT("Buy")));
			const FRpgRect BuyRect( Right.X+Right.W-230*S, Right.Y+Right.H-28*S, 110*S, 24*S );
			if( Ui.Button( BuyRect, Label, !VendorRefuses(), &Ui ) )
				bActivate = 1;
			if( bActivate )
			{
				if( bOwned || O.Kind==OFFER_Sell || ConfirmIndex==Sel )
				{
					Transact( Harry, O );
					ConfirmIndex = -1;
				}
				else
				{
					ConfirmIndex = Sel;
					Speech = FString::Printf( TEXT("%s for %i beans? Press Buy again and it's yours."), OfferName(O), OfferPrice(O) );
					Ui.Play( RSND_Click, Harry );
				}
				bActivate = 0;
			}
		}
		if( Ui.Button( FRpgRect(Right.X+Right.W-110*S, Right.Y+Right.H-28*S, 110*S, 24*S), TEXT("Close"), 1, &Offers ) )
			CloseMenu( Engine );
		Ui.Footer( TEXT("Q/E: tab    Arrows: choose    Enter: buy    Esc/B: close") );
	}

	/*-------------------------------------------------------------------------
		Character book.
	-------------------------------------------------------------------------*/

	void DrawBook( UGameEngine* Engine, APlayerPawn* Harry )
	{
		const FLOAT S = Ui.S;
		FRpgRect Left, Right;
		Ui.Screen( Left, Right );

		// Left: Harry as he looks now, and what he wears.
		const FRpgRect Stage( Left.X, Left.Y+Ui.TextHeight(Ui.FontItem)+12*S, Left.W, Left.H*0.62f );
		DrawHarryPreview( Engine, Stage, State.Equip[RSLOT_Outfit], State.Equip[RSLOT_Glasses], State.Equip[RSLOT_Hat], State.Equip[RSLOT_Pet], PREV_Body, PreviewYaw*0.15f );
		FLOAT Y = Stage.Y + Stage.H + 6*S;
		const FLOAT LineH = Ui.TextHeight( Ui.FontBody );
		for( INT s=0; s<RSLOT_MAX && Y < Left.Y+Left.H-LineH; s++ )
		{
			Ui.Text( Ui.FontBody, Left.X+8*S, Y, RpgMuted, *FString::Printf(TEXT("%s:"), GSlotNames[s]) );
			Ui.Text( Ui.FontBody, Left.X+Left.W*0.32f, Y, RpgParchment, GCosmetics[State.Equip[s]].Name );
			Y += LineH;
		}

		// Level, XP and points above the 3D stage.
		Ui.Text( Ui.FontItem, Left.X+8*S, Left.Y, RpgGoldBright, *FString::Printf(TEXT("Level %i"), State.Level) );
		Ui.Text( Ui.FontBody, Left.X+Left.W-8*S, Left.Y+2*S, State.SkillPoints ? RpgGreen : RpgParchment, *FString::Printf(TEXT("Skill points: %i"), State.SkillPoints), RALIGN_Right );
		Ui.Bar( FRpgRect(Left.X+8*S, Left.Y+Ui.TextHeight(Ui.FontItem)+2*S, Left.W-16*S, 5*S), (FLOAT)State.Xp/XpForNextLevel(State.Level), FColor(120,190,255,255) );

		// Right: title and tabs.
		FLOAT RY = Right.Y;
		Ui.Text( Ui.FontTitle, Right.X, RY, RpgGoldBright, GBookTabNames[BookTab] );
		RY += Ui.TextHeight( Ui.FontTitle ) + 6*S;
		const INT ClickedTab = DrawTabs( FRpgRect(Right.X, RY, Right.W, Ui.TextHeight(Ui.FontItem)+8*S), GBookTabNames, NULL, BOOK_MAX, BookTab );
		if( ClickedTab>=0 && ClickedTab!=BookTab )
		{
			BookTab = ClickedTab;
			Sel = Scroll = 0;
			Ui.Play( RSND_Tab, Harry );
		}
		RY += Ui.TextHeight( Ui.FontItem ) + 14*S;
		const FRpgRect Body( Right.X, RY, Right.W, Right.Y+Right.H-RY-30*S );

		switch( BookTab )
		{
			case BOOK_Skills:     DrawSkillsTab( Harry, Body ); break;
			case BOOK_Spells:     DrawSpellsTab( Harry, Body ); break;
			case BOOK_Wardrobe:   DrawWardrobeTab( Engine, Harry, Body ); break;
			case BOOK_Collection: DrawCollectionTab( Engine, Harry, Body ); break;
			case BOOK_Trophies:   DrawTrophiesTab( Harry, Body ); break;
			default:              DrawStatusTab( Harry, Body ); break;
		}
		bActivate = 0;

		if( NoticeTime>0.f && Notice.Len() )
			Ui.Text( Ui.FontBody, Right.X, Right.Y+Right.H-24*S, RpgGoldBright, *Ui.FitText(Ui.FontBody,*Notice,Right.W-120*S) );
		if( Ui.Button( FRpgRect(Right.X+Right.W-110*S, Right.Y+Right.H-28*S, 110*S, 24*S), TEXT("Close"), 1, &Menu ) )
			CloseMenu( Engine );
		Ui.Footer( TEXT("Q/E: tab    Tab: section    Arrows: choose    Enter: select    K/Esc: close") );
	}

	UBOOL RowActivated( const FRpgRect& Row, INT Index, const void* Id )
	{
		const UBOOL bHover = Ui.Hovered( Row, Id );
		if( Index==Sel || bHover )
			Ui.Rect( Row, Index==Sel ? FColor(34,44,96,255) : FColor(24,32,72,255) );
		if( Ui.Clicked(Row,Id) )
		{
			Sel = Index;
			return 1;
		}
		return bActivate && Index==Sel;
	}

	void DrawSkillsTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.S;
		Sel = Clamp( Sel, 0, SKILL_MAX-1 );
		const FLOAT RowH = Min( Area.H/SKILL_MAX, Ui.TextHeight(Ui.FontItem)+Ui.TextHeight(Ui.FontBody)+14*S );
		for( INT i=0; i<SKILL_MAX; i++ )
		{
			const FRpgRect Row( Area.X, Area.Y+i*RowH, Area.W, RowH-4*S );
			if( RowActivated(Row,i,&GSkillNames[i]) )
			{
				if( State.SkillPoints<=0 )
				{
					SetNotice( TEXT("No skill points. Level up, collect cards, or buy an Extra Lesson.") );
					Ui.Play( RSND_Deny, Harry );
				}
				else if( State.Skill[i]>=SKILL_MAX_PIPS )
					SetNotice( TEXT("That skill is maxed out.") );
				else
				{
					State.SkillPoints--;
					State.Skill[i]++;
					ApplySkills( Harry );
					SetNotice( FString::Printf(TEXT("%s improved!"), GSkillNames[i]) );
					Ui.Play( RSND_Buy, Harry );
				}
			}
			Ui.Text( Ui.FontItem, Row.X+8*S, Row.Y+2*S, i==Sel ? RpgGoldBright : RpgParchment, GSkillNames[i] );
			Ui.Text( Ui.FontBody, Row.X+8*S, Row.Y+Ui.TextHeight(Ui.FontItem)+2*S, RpgMuted, *Ui.FitText(Ui.FontBody,GSkillDescs[i],Row.W-16*S) );
			const FLOAT Pip = 16*S;
			Ui.Pips( Row.X+Row.W-SKILL_MAX_PIPS*(Pip+4*S)-4*S, Row.Y+4*S, State.Skill[i], SKILL_MAX_PIPS, Pip );
		}
	}

	// Perks of one spell in upgrade order (each requires the previous).
	INT SpellChain( INT Spell, INT* Out )
	{
		INT N = 0;
		INT Prev = -1;
		for( INT Step=0; Step<PERK_MAX; Step++ )
		{
			INT Found = -1;
			for( INT p=0; p<PERK_MAX; p++ )
				if( GPerks[p].Spell==Spell && GPerks[p].Requires==Prev )
				{
					Found = p;
					break;
				}
			if( Found<0 )
				break;
			Out[N++] = Found;
			Prev = Found;
		}
		return N;
	}

	void DrawSpellsTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.S;
		Sel = Clamp( Sel, 0, SP_MAX-1 );
		const FLOAT RowH = Area.H/SP_MAX;
		for( INT sp=0; sp<SP_MAX; sp++ )
		{
			INT Chain[PERK_MAX];
			const INT Count = SpellChain( sp, Chain );
			INT Level = 0;
			while( Level<Count && HasPerk(Chain[Level]) )
				Level++;
			const FRpgRect Row( Area.X, Area.Y+sp*RowH, Area.W, RowH-4*S );
			const UBOOL bAct = RowActivated( Row, sp, &GSpellNames[sp] );

			const FLOAT IconS = Min( RowH-10*S, 40*S );
			UTexture* Icon = LoadTex( GSpellIcons[sp] );
			if( Icon )
				Ui.Icon( Icon, FRpgRect(Row.X+4*S, Row.Y+(Row.H-IconS)*0.5f, IconS, IconS) );
			const FLOAT TX = Row.X + IconS + 12*S;
			Ui.Text( Ui.FontItem, TX, Row.Y+2*S, sp==Sel ? RpgGoldBright : RpgParchment, GSpellNames[sp] );
			const FLOAT Pip = 14*S;
			Ui.Pips( Row.X+Row.W-Count*(Pip+4*S)-4*S, Row.Y+6*S, Level, Count, Pip );

			FString Line;
			FColor LineColor = RpgMuted;
			if( Level<Count )
			{
				const FPerkDef& Next = GPerks[Chain[Level]];
				Line = FString::Printf( TEXT("Next: %s (%i pt) - %s"), Next.Name, Next.Cost, Next.Desc );
				LineColor = State.SkillPoints>=Next.Cost ? RpgParchment : RpgMuted;
			}
			else
			{
				Line = TEXT("Mastered!");
				LineColor = RpgGreen;
			}
			Ui.Text( Ui.FontBody, TX, Row.Y+Ui.TextHeight(Ui.FontItem)+2*S, LineColor, *Ui.FitText(Ui.FontBody,*Line,Row.X+Row.W-TX-4*S) );

			if( bAct )
			{
				if( Level>=Count )
					SetNotice( FString::Printf(TEXT("%s is already mastered."), GSpellNames[sp]) );
				else
				{
					const FPerkDef& Next = GPerks[Chain[Level]];
					if( State.SkillPoints < Next.Cost )
					{
						SetNotice( FString::Printf(TEXT("%s needs %i skill point%s."), Next.Name, Next.Cost, Next.Cost>1 ? TEXT("s") : TEXT("")) );
						Ui.Play( RSND_Deny, Harry );
					}
					else
					{
						State.SkillPoints -= Next.Cost;
						State.Perks |= (1<<Chain[Level]);
						SetNotice( FString::Printf(TEXT("%s level %i: %s!"), GSpellNames[sp], Level+1, Next.Name) );
						Ui.Play( RSND_Reward, Harry );
						ApplySkills( Harry );
					}
				}
			}
		}
	}

	void DrawWardrobeTab( UGameEngine* Engine, APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.S;
		const INT Clicked = DrawTabs( FRpgRect(Area.X, Area.Y, Area.W, Ui.TextHeight(Ui.FontBody)+6*S), GSlotNames, NULL, RSLOT_MAX, WardrobeSlot );
		if( Clicked>=0 && Clicked!=WardrobeSlot )
		{
			WardrobeSlot = Clicked;
			Sel = Scroll = 0;
		}
		// Owned first, then locked items.
		INT Items[MAX_COSMETICS];
		INT Count = 0;
		for( INT Pass=0; Pass<2; Pass++ )
			for( INT i=0; i<GNumCosmetics; i++ )
				if( GCosmetics[i].Slot==WardrobeSlot && (IsOwned(i)==(Pass==0)) )
					Items[Count++] = i;
		const INT Cols = 3, Rows = 2;
		ApplyWheel( Count, Cols, Rows );
		ClampSelection( Count, Cols, Rows );
		const FRpgRect GridArea( Area.X, Area.Y+Ui.TextHeight(Ui.FontBody)+14*S, Area.W, Area.H*0.6f );
		for( INT v=0; v<Cols*Rows; v++ )
		{
			const INT Index = Scroll*Cols + v;
			if( Index>=Count )
				break;
			const INT Id = Items[Index];
			const FCosmetic& Cos = GCosmetics[Id];
			const FRpgRect Cell = GridCell( GridArea, Cols, Rows, v );
			const UBOOL bHover = Ui.Hovered( Cell, &GCosmetics[Id] );
			const UBOOL bOwned = IsOwned( Id );
			Ui.Tile( Cell, Index==Sel, bHover, GRarityColors[Cos.Rarity] );
			const FRpgRect Pic( Cell.X+3*S, Cell.Y+3*S, Cell.W-6*S, Cell.H-Ui.TextHeight(Ui.FontBody)-8*S );
			DrawCosmeticPreview( Engine, Pic, Id, (Index==Sel || bHover) ? PreviewYaw : 0.f );
			const FString Label = State.Equip[WardrobeSlot]==Id ? FString(TEXT("Wearing")) : (bOwned ? FString(TEXT("Owned")) : FString::Printf(TEXT("%i beans"), Cos.Price));
			Ui.Text( Ui.FontBody, Cell.X+Cell.W*0.5f, Cell.Y+Cell.H-Ui.TextHeight(Ui.FontBody)-3*S, bOwned ? RpgGreen : RpgMuted, *Label, RALIGN_Center );
			if( Ui.Clicked( Cell, &GCosmetics[Id] ) )
			{
				if( Sel==Index )
					bActivate = 1;
				Sel = Index;
				Ui.Play( RSND_Click, Harry );
			}
		}
		DrawScrollHints( GridArea, Count, Cols, Rows );
		if( !Count )
			return;
		const INT Id = Items[Sel];
		const FCosmetic& Cos = GCosmetics[Id];
		FLOAT Y = GridArea.Y + GridArea.H + 8*S;
		Ui.Text( Ui.FontItem, Area.X, Y, RpgGoldBright, Cos.Name );
		Y += Ui.TextHeight( Ui.FontItem );
		FString Meta = FString::Printf( TEXT("%s %s"), GRarityNames[Cos.Rarity], GSlotNames[Cos.Slot] );
		if( Cos.Stat!=STAT_None )
			Meta += FString(TEXT("  ")) + StatText( Cos.Stat, Cos.StatValue );
		Ui.Text( Ui.FontBody, Area.X, Y, GRarityColors[Cos.Rarity], *Meta );
		Y += Ui.TextHeight( Ui.FontBody );
		Ui.TextWrapped( Ui.FontBody, FRpgRect(Area.X, Y, Area.W, Area.Y+Area.H-Y), RpgParchment,
			IsOwned(Id) ? Cos.Desc : *FString::Printf(TEXT("%s Locked: buy it from a vendor."), Cos.Desc) );
		if( bActivate )
		{
			if( IsOwned(Id) )
			{
				State.Equip[WardrobeSlot] = Id;
				SetNotice( FString::Printf(TEXT("Now wearing: %s"), Cos.Name) );
				Ui.Play( RSND_Buy, Harry );
			}
			else
			{
				SetNotice( TEXT("Locked. Find a vendor who sells it (press B near one).") );
				Ui.Play( RSND_Deny, Harry );
			}
			bActivate = 0;
		}
	}

	void DrawCollectionTab( UGameEngine* Engine, APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.S;
		static const TCHAR* Sections[2] = { TEXT("Wizard Cards"), TEXT("Bestiary") };
		const INT Clicked = DrawTabs( FRpgRect(Area.X, Area.Y, Area.W*0.6f, Ui.TextHeight(Ui.FontBody)+6*S), Sections, NULL, 2, CollectionTab );
		if( Clicked>=0 && Clicked!=CollectionTab )
		{
			CollectionTab = Clicked;
			Sel = Scroll = 0;
		}
		FLOAT Y = Area.Y + Ui.TextHeight(Ui.FontBody) + 14*S;
		const FLOAT LineH = Ui.TextHeight( Ui.FontBody );
		if( CollectionTab==0 )
		{
			UObject* Status = GetStatus( Harry );
			static const TCHAR* Kinds[3] = { TEXT("Bronze"), TEXT("Silver"), TEXT("Gold") };
			static const INT Totals[3] = { 50, 40, 11 };
			static const FColor KindColors[3] = { FColor(205,127,50,255), FColor(200,205,215,255), FColor(255,205,70,255) };
			for( INT k=0; k<3; k++ )
			{
				const INT Have = CountCards( Status, k );
				Ui.Text( Ui.FontItem, Area.X, Y, KindColors[k], *FString::Printf(TEXT("%s cards  %i / %i"), Kinds[k], Have, Totals[k]) );
				Y += Ui.TextHeight( Ui.FontItem );
				Ui.Bar( FRpgRect(Area.X, Y, Area.W, 6*S), (FLOAT)Have/Totals[k], KindColors[k] );
				Y += 12*S;
			}
			Y += 4*S;
			for( INT r=0; r<GNumCardRewards && Y < Area.Y+Area.H-LineH; r++ )
			{
				const FCardReward& R = GCardRewards[r];
				const UBOOL bDone = (State.CardRewards & (1<<r))!=0;
				FString Reward = StatText( R.Stat, R.StatValue );
				if( R.SkillPoints )
					Reward += FString::Printf( TEXT("%s+%i skill point%s"), Reward.Len() ? TEXT(", ") : TEXT(""), R.SkillPoints, R.SkillPoints>1 ? TEXT("s") : TEXT("") );
				const FLOAT RewardW = Ui.TextWidth( Ui.FontSmall, *Reward );
				const FString Name = Ui.FitText( Ui.FontBody, *FString::Printf(TEXT("%s %s (%i)"), bDone ? TEXT("[x]") : TEXT("[ ]"), R.Name, R.Count), Area.W-RewardW-12*S );
				Ui.Text( Ui.FontBody, Area.X, Y, bDone ? RpgGreen : RpgParchment, *Name );
				Ui.Text( Ui.FontSmall, Area.X+Area.W, Y+(LineH-Ui.TextHeight(Ui.FontSmall))*0.5f, bDone ? RpgGreen : RpgMuted, *Reward, RALIGN_Right );
				Y += LineH;
			}
			return;
		}
		// Bestiary list with a 3D model of the selected creature.
		const INT Rows = Max( 1, appFloor((Area.Y+Area.H-Y)/LineH) );
		ApplyWheel( CR_MAX, 1, Rows );
		ClampSelection( CR_MAX, 1, Rows );
		const FRpgRect ListArea( Area.X, Y, Area.W*0.58f, Rows*LineH );
		for( INT r=0; r<Rows; r++ )
		{
			const INT c = Scroll + r;
			if( c>=CR_MAX )
				break;
			const FRpgRect Row( ListArea.X, Y + r*LineH, ListArea.W, LineH );
			RowActivated( Row, c, &GCreatures[c] );
			const INT Tier = BestiaryTier( c );
			const UBOOL bKnown = State.Kills[c]>0;
			const FString Text = bKnown ? FString(GCreatures[c].Name) : FString(TEXT("Unknown creature"));
			Ui.Text( Ui.FontBody, Row.X+4*S, Row.Y, c==Sel ? RpgGoldBright : (bKnown ? RpgParchment : RpgMuted), *Ui.FitText(Ui.FontBody,*Text,Row.W*0.62f) );
			if( bKnown )
				Ui.Text( Ui.FontBody, Row.X+Row.W-4*S, Row.Y, Tier ? RpgGreen : RpgParchment, *FString::Printf(TEXT("%i defeated  %s"), State.Kills[c], Tier ? *FString::Printf(TEXT("R%i"),Tier) : TEXT("")), RALIGN_Right );
		}
		const INT c = Sel;
		const FRpgRect Side( Area.X+Area.W*0.62f, Y, Area.W*0.38f, Area.Y+Area.H-Y );
		const FRpgRect Pic( Side.X, Side.Y, Side.W, Side.H*0.5f );
		Ui.Rect( Pic, FColor(14,18,46,255) );
		if( State.Kills[c]>0 && GCreatures[c].Mesh )
			DrawMeshPreview( Engine, Pic, GCreatures[c].Mesh, 1.f, PreviewYaw );
		FLOAT SY = Pic.Y + Pic.H + 4*S;
		const INT Tier = BestiaryTier( c );
		const FString Next = Tier<3 ? FString::Printf( TEXT("Next rank at %i"), GBestiaryTiers[Tier] ) : FString( TEXT("Mastered!") );
		Ui.Text( Ui.FontBody, Side.X, SY, RpgGoldBright, *Next );
		SY += LineH;
		Ui.TextWrapped( Ui.FontBody, FRpgRect(Side.X, SY, Side.W, Side.Y+Side.H-SY), RpgMuted, State.Kills[c]>0 ? GCreatures[c].Lore : TEXT("Defeat one to learn more.") );
	}

	void DrawTrophiesTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.S;
		const INT Cols = 3, Rows = 3;
		ApplyWheel( GNumTrophies, Cols, Rows );
		ClampSelection( GNumTrophies, Cols, Rows );
		const FRpgRect GridArea( Area.X, Area.Y, Area.W, Area.H*0.78f );
		UTexture* Star = LoadTex( TEXT("HP2_Menu.Icons.HP2StarCounter") );
		for( INT v=0; v<Cols*Rows; v++ )
		{
			const INT t = Scroll*Cols + v;
			if( t>=GNumTrophies )
				break;
			const FTrophyDef& T = GTrophies[t];
			const INT Tier = State.TrophyTier[t];
			const FRpgRect Cell = GridCell( GridArea, Cols, Rows, v );
			const UBOOL bHover = Ui.Hovered( Cell, &GTrophies[t] );
			Ui.Tile( Cell, t==Sel, bHover, Tier ? GTrophyTierColors[Tier-1] : FColor(60,64,96,255) );
			if( Ui.Clicked(Cell,&GTrophies[t]) )
				Sel = t;
			const FLOAT IconS = Min( Cell.H*0.45f, 40*S );
			if( Star )
				Ui.Icon( Star, FRpgRect(Cell.X+(Cell.W-IconS)*0.5f, Cell.Y+4*S, IconS, IconS), Tier ? 1.f : 0.25f );
			UFont* NameFont = Ui.TextWidth(Ui.FontBody,T.Name) <= Cell.W-8*S ? Ui.FontBody : Ui.FontSmall;
			Ui.Text( NameFont, Cell.X+Cell.W*0.5f, Cell.Y+IconS+6*S, Tier ? GTrophyTierColors[Tier-1] : RpgMuted, *Ui.FitText(NameFont,T.Name,Cell.W-8*S), RALIGN_Center );
			Ui.Text( Ui.FontSmall, Cell.X+Cell.W*0.5f, Cell.Y+IconS+6*S+Ui.TextHeight(Ui.FontBody), RpgMuted,
				Tier ? GTrophyTierNames[Tier-1] : TEXT("Not yet"), RALIGN_Center );
		}
		DrawScrollHints( GridArea, GNumTrophies, Cols, Rows );
		const FTrophyDef& T = GTrophies[Sel];
		const INT Tier = State.TrophyTier[Sel];
		const INT Value = CounterValue( Harry, T.Counter );
		FLOAT Y = GridArea.Y + GridArea.H + 6*S;
		const FString Goal = Tier<3 ? FString::Printf( T.Desc, T.Goals[Tier] ) : FString( TEXT("Gold trophy earned!") );
		Ui.Text( Ui.FontItem, Area.X, Y, RpgGoldBright, *Goal );
		Y += Ui.TextHeight( Ui.FontItem );
		if( Tier<3 )
			Ui.Bar( FRpgRect(Area.X, Y+2*S, Area.W, 6*S), (FLOAT)Value/T.Goals[Tier], GTrophyTierColors[Tier] );
		Ui.Text( Ui.FontBody, Area.X+Area.W, Y+10*S, RpgMuted, *FString::Printf(TEXT("%i"), Value), RALIGN_Right );
	}

	void DrawStatusTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.S;
		FLOAT Y = Area.Y;
		const FLOAT LineH = Ui.TextHeight( Ui.FontBody );
		Ui.Text( Ui.FontItem, Area.X, Y, RpgGoldBright, *FString::Printf(TEXT("Level %i"), State.Level) );
		Ui.Text( Ui.FontBody, Area.X+Area.W, Y+4*S, RpgParchment, *FString::Printf(TEXT("XP %i / %i"), State.Xp, XpForNextLevel(State.Level)), RALIGN_Right );
		Y += Ui.TextHeight( Ui.FontItem );
		Ui.Bar( FRpgRect(Area.X, Y, Area.W, 6*S), (FLOAT)State.Xp/XpForNextLevel(State.Level), FColor(120,190,255,255) );
		Y += 14*S;

		Ui.Text( Ui.FontItem, Area.X, Y, RpgGoldBright, TEXT("Bonuses") );
		Y += Ui.TextHeight( Ui.FontItem );
		for( INT s=1; s<STAT_MAX; s++ )
		{
			const INT V = s==STAT_Discount ? Discount() : GetStat(s);
			Ui.Text( Ui.FontBody, Area.X+8*S, Y, V ? RpgParchment : RpgMuted, *StatText(s,V) );
			Y += LineH;
		}
		Ui.Text( Ui.FontBody, Area.X+8*S, Y, RpgParchment, TEXT("Striped bean 5, golden 3, speckled 2, others 1") );
		Y += LineH + 6*S;

		Ui.Text( Ui.FontItem, Area.X, Y, RpgGoldBright, TEXT("Trouble") );
		Y += Ui.TextHeight( Ui.FontItem );
		Ui.Bar( FRpgRect(Area.X, Y, Area.W, 6*S), State.Trouble/100.f, State.Trouble>=70 ? RpgRed : FColor(230,160,70,255) );
		Y += 10*S;
		const FString TroubleText = State.DetentionSeconds>0
			? FString::Printf( TEXT("In detention for %i:%02i. Only Madam Pomfrey will trade."), State.DetentionSeconds/60, State.DetentionSeconds%60 )
			: FString::Printf( TEXT("%i / 100. Hexing students or getting caught adds trouble; it fades over time."), State.Trouble );
		Ui.TextWrapped( Ui.FontBody, FRpgRect(Area.X, Y, Area.W, LineH*2), RpgMuted, *TroubleText );
		Y += LineH*2 + 4*S;
		FString Extra = FString::Printf( TEXT("School day %i"), State.Day );
		if( State.FelixSeconds>0 )
			Extra += FString::Printf( TEXT("   Felix Felicis %i:%02i"), State.FelixSeconds/60, State.FelixSeconds%60 );
		Ui.Text( Ui.FontBody, Area.X, Y, RpgParchment, *Extra );
	}

	/*-------------------------------------------------------------------------
		Input.
	-------------------------------------------------------------------------*/

	void TalkTo( APlayerPawn* Harry, AActor* Npc )
	{
		if( !Npc || RpgClock-LastTalkTime < 1.f )
			return;
		LastTalkTime = RpgClock;
		FRpgArg JustTalk = FRpgArg::I( 1 );
		CallScript( Npc, TEXT("DoBumpLine"), &JustTalk, 1 );
		State.Counters[CNT_Talks]++;
		if( !TestAndSet(Npc,AF_Talked) )
			GiveXp( 3, TEXT("new friend") );
	}

	void OpenShop( UGameEngine* Engine, AActor* Vendor, INT Def )
	{
		ShopVendor = Vendor;
		ShopVendorDef = Def;
		ShopTab = -1;
		BuildOffers( Def );
		Speech = GVendors[Def].Greeting;
		OpenMenu( Engine, MENU_Shop );
	}

	UBOOL InputEvent( UGameEngine* Engine, UViewport* Viewport, EInputKey Key, EInputAction Action, FLOAT Delta )
	{
		if( !bInitialized || !bEnabled || !Engine->GLevel || Engine->Replay.Replaying() || Engine->Replay.Recording() )
			return 0;
		APlayerPawn* Harry = GetHarry( Engine );

		if( Menu==MENU_None )
		{
			if( Action!=IST_Press || !Harry || Harry!=LastHarry )
				return 0;
			if( Key==IK_K && IsFreeGameplay(Engine,Harry) )
			{
				OpenMenu( Engine, MENU_Book );
				return 1;
			}
			if( Key==IK_B && IsFreeGameplay(Engine,Harry) )
			{
				if( NearNpc && NearVendor>=0 && IsInLevel(NearNpc) )
					OpenShop( Engine, NearNpc, NearVendor );
				else
					AddFeed( TEXT("No vendor nearby"), FColor(180,180,200,255), TOAST_Info, TEXT("Look for [B] Trade near a vendor") );
				return 1;
			}
			if( Key==IK_E && NearNpc && IsInLevel(NearNpc) && IsFreeGameplay(Engine,Harry) )
			{
				TalkTo( Harry, NearNpc );
				return 1;
			}
			return 0;
		}

		// A menu is open: it owns all input, except releases, so keys held
		// when the menu opened are not left stuck down.
		if( Action==IST_Release )
			return 0;
		if( Key==IK_MouseWheelUp || Key==IK_MouseWheelDown )
		{
			if( Action==IST_Press )
				Ui.WheelDelta += Key==IK_MouseWheelUp ? 1 : -1;
			return 1;
		}
		if( Action!=IST_Press )
			return 1;
		if( !Harry )
		{
			CloseMenu( Engine );
			return 1;
		}
		if( Key==IK_LeftMouse )
		{
			Ui.bClickPending = 1;
			return 1;
		}
		const UBOOL bClose = Key==IK_Escape || Key==IK_Backspace || (Menu==MENU_Shop && Key==IK_B) || (Menu==MENU_Book && Key==IK_K);
		if( bClose )
		{
			CloseMenu( Engine );
			return 1;
		}
		const INT Cols = (Menu==MENU_Shop || (Menu==MENU_Book && (BookTab==BOOK_Wardrobe || BookTab==BOOK_Trophies))) ? 3 : 1;
		if( Key==IK_Q || Key==IK_E )
		{
			const INT Dir = Key==IK_E ? 1 : -1;
			if( Menu==MENU_Book )
				BookTab = (BookTab + Dir + BOOK_MAX) % BOOK_MAX;
			else
			{
				// Next tab that has offers.
				for( INT Step=1; Step<=TAB_MAX; Step++ )
				{
					const INT T = (ShopTab + Dir*Step + TAB_MAX*4) % TAB_MAX;
					UBOOL bAny = 0;
					for( INT o=0; o<Offers.Num(); o++ )
						if( Offers(o).Tab==T )
							bAny = 1;
					if( bAny )
					{
						ShopTab = T;
						break;
					}
				}
			}
			Sel = Scroll = 0;
			ConfirmIndex = -1;
			Ui.Play( RSND_Tab, Harry );
			return 1;
		}
		if( Key==IK_Tab && Menu==MENU_Book )
		{
			if( BookTab==BOOK_Wardrobe ) WardrobeSlot = (WardrobeSlot+1) % RSLOT_MAX;
			if( BookTab==BOOK_Collection ) CollectionTab = (CollectionTab+1) % 2;
			Sel = Scroll = 0;
			Ui.Play( RSND_Tab, Harry );
			return 1;
		}
		const INT OldSel = Sel;
		if( Key==IK_Up || Key==IK_W )    Sel -= Cols;
		if( Key==IK_Down || Key==IK_S )  Sel += Cols;
		if( Key==IK_Left || Key==IK_A )  Sel -= 1;
		if( Key==IK_Right || Key==IK_D ) Sel += 1;
		if( Sel<0 )
			Sel = 0;
		if( Sel!=OldSel )
		{
			ConfirmIndex = Menu==MENU_Shop ? -1 : ConfirmIndex;
			Ui.Play( RSND_Hover, Harry );
		}
		if( Key==IK_Enter || Key==IK_Space )
			bActivate = 1;
		return 1;
	}

	/*-------------------------------------------------------------------------
		Console commands.
	-------------------------------------------------------------------------*/

	UBOOL Exec( UGameEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar )
	{
		Init();
		if( !ParseCommand(&Cmd,TEXT("RPG")) )
			return 0;
		APlayerPawn* Harry = GetHarry( Engine );
		if( ParseCommand(&Cmd,TEXT("STATUS")) )
		{
			Ar.Logf( TEXT("HP2Rpg %s: Level %i Xp %i SkillPoints %i Perks %i Trouble %i Day %i"),
				bEnabled ? TEXT("on") : TEXT("off"), State.Level, State.Xp, State.SkillPoints, State.Perks, State.Trouble, State.Day );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("XP")) )
		{
			GiveXp( appAtoi(Cmd), TEXT("console") );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("BEANS")) )
		{
			if( Harry )
				GiveBeans( Harry, appAtoi(Cmd) );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("POINTS")) )
		{
			State.SkillPoints += appAtoi( Cmd );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("UNLOCKALL")) )
		{
			for( INT i=0; i<GNumCosmetics; i++ )
				SetOwned( i );
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("HAT")) )
		{
			// RPG HAT <up> <forward> <side> <scale>: tune hat placement live.
			FLOAT Values[4] = { 0.f, 0.f, 0.f, 1.f };
			INT N = 0;
			FString Token;
			while( N<4 && ParseToken(Cmd,Token,0) )
				Values[N++] = appAtof( *Token );
			HatTweak = FVector( Values[0], Values[1], Values[2] );
			HatScaleTweak = Values[3]>0.f ? Values[3] : 1.f;
			Ar.Logf( TEXT("HP2Rpg hat tweak up=%.1f fwd=%.1f side=%.1f scale=%.2f"), HatTweak.X, HatTweak.Y, HatTweak.Z, HatScaleTweak );
			return 1;
		}
		Ar.Logf( TEXT("Usage: RPG STATUS | XP n | BEANS n | POINTS n | UNLOCKALL | HAT up fwd side scale") );
		return 1;
	}
