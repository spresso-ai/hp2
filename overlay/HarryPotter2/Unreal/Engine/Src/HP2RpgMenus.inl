/*=============================================================================
	HP2RpgMenus.inl: Menus, HUD, input and console commands of FHP2Rpg.
	Included inside the FHP2Rpg class body in HP2Rpg.cpp.
=============================================================================*/

	// Extra menu state.
	UBOOL bPreviewDiagDone;
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
		bConfirmLeave = 0;
		Sel = 0;
		Scroll = 0;
		ConfirmIndex = -1;
		TryOn = -1;
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
			case CON_Nimbus: case CON_QArmor: return TAB_Gear;
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

	// One-time game items Harry already has.
	UBOOL OwnsGameItem( INT c )
	{
		APlayerPawn* Harry = CurrentHarry();
		if( !Harry )
			return 0;
		if( c==CON_Nimbus )
			return GetBoolProp( Harry, TEXT("bHaveNimbus2001") ) || CallInt( GetStatus(Harry), TEXT("GetNimbusCount") )>0;
		if( c==CON_QArmor )
			return GetBoolProp( Harry, TEXT("bHaveQArmor") ) || CallInt( GetStatus(Harry), TEXT("GetQArmorCount") )>0;
		return 0;
	}

	void BuildOffers( INT Vendor )
	{
		Offers.Empty();
		const FVendorDef& V = GVendors[Vendor];
		for( INT c=0; c<CON_MAX; c++ )
		{
			if( !(V.Consumables & CONBIT(c)) || OwnsGameItem(c) )
				continue;
			INT Tab = ConsumableTab( c );
			if( !(V.Tabs & TABBIT(Tab)) )
				Tab = (V.Tabs & TABBIT(TAB_Magic)) ? TAB_Magic : Tab;
			AddOffer( OFFER_Consumable, c, Tab );
		}

		// Friends are always on offer where they're sold.
		if( V.Tabs & TABBIT(TAB_Friends) )
			for( INT i=0; i<GNumCosmetics; i++ )
				if( GCosmetics[i].Slot==RSLOT_Friend && GCosmetics[i].Price>0 )
					AddOffer( OFFER_Cosmetic, i, TAB_Friends );

		// Cosmetics rotate daily: a seeded pick from this vendor's pool.
		INT Pool[MAX_COSMETICS];
		DWORD Keys[MAX_COSMETICS];
		INT N = 0;
		for( INT i=0; i<GNumCosmetics; i++ )
		{
			const FCosmetic& C = GCosmetics[i];
			if( C.Price<=0 || C.Slot==RSLOT_Friend || !(V.Tabs & TABBIT(GCosmeticTab[C.Slot])) || !NameMatchesFilter(C.Name,V.CosmeticFilter) )
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
		INT Markup = ShopVendorDef>=0 ? GVendors[ShopVendorDef].PriceMarkup : 0;
		if( ShopVendorDef>=VEN_StudentGryffindor )
			Markup += RepPriceDelta( State.Rep[ShopVendorDef-VEN_StudentGryffindor] );
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
		{
			SetBoolProp( Harry, TEXT("bCastFastSpells"), 0 );
			UObject* Cursor = GetObjProp( Harry, TEXT("SpellCursor") );
			if( Cursor )
				SetFloatProp( Cursor, TEXT("fLOS_Distance"), GetFloatProp(Cursor->GetClass()->GetDefaultObject(),TEXT("fLOS_Distance"),0.f) );
		}
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
			// The game's own Quidditch gear: the status item plus Harry's flag,
			// so Fred and George stop offering it too.
			case CON_Nimbus:     SetBoolProp( Harry, TEXT("bHaveNimbus2001"), 1 ); return CallScript( Status, TEXT("AddNimbus"), &N, 1 );
			case CON_QArmor:     SetBoolProp( Harry, TEXT("bHaveQArmor"), 1 ); return CallScript( Status, TEXT("AddQArmor"), &N, 1 );
		}
		return 0;
	}

	UBOOL VendorRefuses()
	{
		return State.DetentionSeconds>0 && ShopVendorDef!=VEN_Pomfrey;
	}

	void Transact( APlayerPawn* Harry, const FOffer& O )
	{
		if( VendorRefuses() && !(O.Kind==OFFER_Consumable && O.Id==CON_Pardon) )
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
			// Pay only once the item has really left the inventory.
			if( !ApplyConsumable( Harry, O.Id, -1 ) )
			{
				Speech = TEXT("Hmm, I can't take that right now.");
				Ui.Play( RSND_Deny, Harry );
				return;
			}
			GiveBeans( Harry, Price );
			Speech = FString::Printf( TEXT("I'll take that %s. Here's %i beans."), Name, Price );
			Ui.Play( RSND_Buy, Harry );
			return;
		}
		if( O.Kind==OFFER_Consumable && OwnsGameItem(O.Id) )
		{
			Speech = TEXT("You've already got one of those.");
			Ui.Play( RSND_Deny, Harry );
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
		FRpgClipScope Clip( Ui, R );
		SetCostume( Body, Outfit, 0 );
		// Always the same calm standing pose. Bounds are measured once per
		// mesh (from Harry's pose at that moment) so framing stays stable.
		INT BoxIdx = -1;
		for( INT b=0; b<PreviewBoxes.Num(); b++ )
			if( PreviewBoxes(b).Mesh==Body->Mesh )
				BoxIdx = b;
		if( BoxIdx<0 )
		{
			CopyAnim( Body, Harry );
			FMeshBox* MB = new(PreviewBoxes) FMeshBox;
			MB->Mesh = Body->Mesh;
			FRpgUi::MeasureBounds( Body, MB->Lo, MB->Hi );
			BoxIdx = PreviewBoxes.Num()-1;
		}
		Body->SkelAnim = HarryAnimSet() ? HarryAnimSet() : Harry->SkelAnim;
		{
			const FName Pose = FindSeq( Body->SkelAnim, TEXT("Idle") );
			Body->AnimSequence = Pose!=NAME_None ? Pose : Harry->AnimSequence;
			Body->AnimFrame = 0.f;
			Body->AnimRate = 0.f;
		}
		ApplyLook( Outfit, Glasses );
		if( Body->Mesh==HarryMesh )
			Body->MultiSkins[3] = GCosmetics[Glasses].Skin0 ? LoadTex( GCosmetics[Glasses].Skin0 ) : NULL;

		// Full body fits the rect; close-ups zoom in on a bone.
		USkeletalMesh* Skel = NULL;
		INT Bone = -1;
		FLOAT Zoom = 1.f;
		if( Framing==PREV_Head || Framing==PREV_Hat )
		{
			Bone = HeadBone( Body, Skel );
			Zoom = Framing==PREV_Hat ? 2.3f : 3.2f;
		}
		else if( Framing==PREV_Shoulder )
		{
			Bone = ShoulderBone( Body, Skel );
			Zoom = 2.2f;
		}
		if( !Ui.PlaceInRect( Body, R, Yaw, Bone>=0 ? Zoom : 1.f, &PreviewBoxes(BoxIdx).Lo, &PreviewBoxes(BoxIdx).Hi ) )
			return;
		if( Bone>=0 && Skel )
			Ui.FocusPoint( Body, Skel->GetBoneCoords(Body,Bone).Origin, R.X+R.W*0.5f, R.Y+R.H*(Framing==PREV_Hat ? 0.68f : Framing==PREV_Head ? 0.6f : 0.5f) );
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

	void DrawMeshPreview( UGameEngine* Engine, const FRpgRect& R, const TCHAR* MeshPath, FLOAT Scale, FLOAT Yaw, const TCHAR* AnimPath=NULL )
	{
		AActor* Item = EnsurePreview( Engine, PreviewItem );
		UMesh* Mesh = LoadMesh( MeshPath );
		if( !Item || !Mesh )
			return;
		FRpgClipScope Clip( Ui, R );
		SetPreviewMesh( Item, Mesh );
		// Characters and animals stand in their idle pose, not the bind pose.
		{
			FString Path = AnimPath ? FString(AnimPath) : FString(MeshPath);
			if( !AnimPath )
			{
				const INT Cut = Path.InStr( TEXT("Mesh"), 1 );
				Path = Cut>0 ? Path.Left(Cut) + TEXT("Anims") : FString();
			}
			UAnimation* Anim = Path.Len() ? (UAnimation*)FRpgUi::LoadRooted( UAnimation::StaticClass(), *Path ) : NULL;
			if( Anim && Anim->AnimSeqs.Num() )
			{
				Item->SkelAnim = Anim;
				FName Pose = FindSeq( Anim, TEXT("Idle") );
				if( Pose==NAME_None ) Pose = FindSeq( Anim, TEXT("Breathe") );
				if( Pose==NAME_None ) Pose = FindSeq( Anim, TEXT("breath") );
				if( Pose==NAME_None ) Pose = FindSeq( Anim, TEXT("Fly") );
				if( Pose==NAME_None ) Pose = Anim->AnimSeqs(0).Name;
				Item->AnimSequence = Pose;
				Item->AnimFrame = 0.f;
				Item->AnimRate = 0.f;
			}
		}
		if( Ui.PlaceInRect( Item, R, Yaw, 1.f ) )
			Ui.DrawActorInRect( Item, 1 );
	}

	void DrawNpcPreview( UGameEngine* Engine, const FRpgRect& R, AActor* Npc, FLOAT Yaw )
	{
		AActor* Body = EnsurePreview( Engine, PreviewNpc );
		if( !Body || !Npc || !Npc->Mesh )
			return;
		FRpgClipScope Clip( Ui, R );
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
			case RSLOT_Hat:     Hat = Id; Framing = PREV_Hat; break;
			case RSLOT_Glasses: Glasses = Id; Framing = PREV_Head; break;
			case RSLOT_Pet:
			case RSLOT_Friend:
				// Pets and friends walk beside Harry: show them on their own.
				if( C.Mesh )
				{
					DrawMeshPreview( Engine, R, C.Mesh, C.Scale, Yaw, C.Slot==RSLOT_Friend ? C.Skin0 : NULL );
					return;
				}
				break;
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
		if( bPhoto )
			DrawPhoto();
		else if( Menu==MENU_None )
		{
			if( bGameplay )
			{
				DrawQuestHud( Harry );
				DrawLessonHud( Harry );
			}
			if( bGameplay || HouseHudTimer>0.f )
				DrawStatusHud( Harry, bGameplay );
			if( bGameplay )
				DrawTroublemakers( Harry );
			if( bGameplay || ArrestoLeft>0.f || RevealLeft>0.f )
				DrawSpellHud( Harry );
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
		if( IsTalking() )
			return;
		UCanvas* C = Ui.C;
		const FColor Text1( 240,238,230,255 ), Text2( 182,186,206,255 ), Line( 46,56,96,255 );
		const FColor GoldHi( 255,222,130,255 ), Gold( 201,162,74,255 ), XpCol( 120,190,255,255 );
		static const FColor BarTint[HOUSE_MAX] = { FColor(160,40,32,255), FColor(40,110,64,255), FColor(40,70,150,255), FColor(170,130,30,255) };

		// House strip, top centre: only while points change, in menus, or on Tab.
		if( HouseHudTimer>0.f || bShowAllHud )
		{
			FLOAT MaxNum = 0.f;
			for( INT i=0; i<HOUSE_MAX; i++ )
				MaxNum = Max( MaxNum, Ui.TextWidth(Ui.FontNum, *FString::Printf(TEXT("%i"), bBaselineValid ? LastHouse[i] : 0)) );
			const FLOAT H = Ui.TextHeight( Ui.FontNum ) + Ui.P(14.f);
			const FLOAT Seg = H*0.9f + Ui.P(8.f) + MaxNum + Ui.P(22.f);
			const FLOAT W = Seg*HOUSE_MAX;
			const FRpgRect Strip( (C->ClipX-W)*0.5f, Ui.P(20.f), W, H );
			Ui.Glass( Strip );
			INT Lead = 1;
			for( INT i=0; i<HOUSE_MAX; i++ )
				Lead = Max( Lead, bBaselineValid ? LastHouse[i] : 0 );
			for( INT i=0; i<HOUSE_MAX; i++ )
			{
				const FLOAT X = Strip.X + Seg*i;
				const UBOOL bOurs = i==HOUSE_Gryffindor;
				const FLOAT Crest = H*0.72f;
				const FString Value = FString::Printf( TEXT("%i"), bBaselineValid ? LastHouse[i] : 0 );
				const FLOAT TW = Crest + Ui.P(8.f) + Ui.TextWidth( Ui.FontNum, *Value );
				const FLOAT CX = X + (Seg-TW)*0.5f;
				if( Crests[i] )
					Ui.Icon( Crests[i], FRpgRect(CX, Strip.Y+(H-Crest)*0.5f - Ui.P(2.f), Crest, Crest), bOurs ? 1.f : 0.7f );
				Ui.Text( Ui.FontNum, CX+Crest+Ui.P(8.f), Strip.Y+(H-Ui.TextHeight(Ui.FontNum))*0.5f - Ui.P(2.f), bOurs ? Text1 : Text2, *Value, RALIGN_Left, 0 );
				// Standing bar: width relative to the leader.
				const FLOAT BarW = (Seg-Ui.P(24.f)) * Clamp( (FLOAT)(bBaselineValid ? LastHouse[i] : 0)/Lead, 0.f, 1.f );
				Ui.Rect( FRpgRect(X+Ui.P(12.f), Strip.Y+H-Ui.P(6.f), BarW, Max(1.f,Ui.P(3.f))), BarTint[i] );
				if( bOurs )
					Ui.Brackets( FRpgRect(X+Ui.P(3.f), Strip.Y+Ui.P(3.f), Seg-Ui.P(6.f), H-Ui.P(6.f)), Ui.P(8.f), Max(1.f,Ui.P(2.f)), Gold );
				// "+5" rising over the crest.
				if( HouseFloat[i] && HouseFloatAge[i]<1.4f )
				{
					const FLOAT Rise = HouseFloatAge[i]*Ui.P(18.f);
					Ui.Text( Ui.FontModernBold, X+Seg*0.5f, Strip.Y+H+Ui.P(4.f)-Rise+Ui.P(18.f), HouseFloat[i]>0 ? GHouseColors[i] : FColor(232,92,80,255),
						*FString::Printf(TEXT("%s%i"), HouseFloat[i]>0 ? TEXT("+") : TEXT(""), HouseFloat[i]), RALIGN_Center );
				}
			}
		}

		// Player plate, bottom left: only after XP, with skill points to
		// spend, a status effect, or on Tab.
		if( XpShowTimer>0.f || (XpFloat && XpFloatAge<1.6f) || State.DetentionSeconds>0 || State.FelixSeconds>0 || bShowAllHud )
		{
			const FLOAT B = Ui.TextHeight( Ui.FontNum ) + Ui.P(18.f);
			const FRpgRect Badge( Ui.P(48.f), C->ClipY-Ui.P(48.f)-B, B, B );
			Ui.Glass( Badge );
			Ui.Border( Badge, Max(1.f,Ui.P(2.f)), Gold );
			const FString Lv = FString::Printf( TEXT("%i"), State.Level );
			Ui.Text( Ui.FontNum, Badge.X+B*0.5f, Badge.Y+(B-Ui.TextHeight(Ui.FontNum))*0.5f, GoldHi, *Lv, RALIGN_Center, 0 );
			const FRpgRect Bar( Badge.X+B+Ui.P(12.f), Badge.Y+B*0.5f-Ui.P(3.f), Ui.P(200.f), Max(2.f,Ui.P(5.f)) );
			Ui.Scrim( Bar, 0.35f );
			Ui.Rect( FRpgRect(Bar.X, Bar.Y, Bar.W*Clamp((FLOAT)State.Xp/XpForNextLevel(State.Level),0.f,1.f), Bar.H), XpCol );
			Ui.Border( Bar, Max(1.f,Ui.P(1.f)), Line );
			if( XpShowTimer>0.f )
				Ui.Text( Ui.FontCaption, Bar.X, Bar.Y-Ui.TextHeight(Ui.FontCaption)-Ui.P(4.f), Text2,
					*FString::Printf(TEXT("Level %i   %i / %i XP"), State.Level, State.Xp, XpForNextLevel(State.Level)), RALIGN_Left );
			if( XpFloat && XpFloatAge<1.6f )
				Ui.Text( Ui.FontModernBold, Bar.X+Bar.W+Ui.P(10.f), Bar.Y-Ui.P(10.f)-XpFloatAge*Ui.P(14.f), XpCol, *FString::Printf(TEXT("+%i XP"), XpFloat), RALIGN_Left );
			if( State.SkillPoints>0 )
			{
				const FLOAT Dot = Ui.P(14.f);
				Ui.Chamfer( FRpgRect(Badge.X+B-Dot*0.6f, Badge.Y-Dot*0.4f, Dot, Dot), Ui.P(3.f), FColor(120,220,140,255) );
				const FLOAT KX = Bar.X;
				const FLOAT KY = Bar.Y+Bar.H+Ui.P(8.f);
				const FLOAT KW = Ui.Keycap( KX, KY, TEXT("K") );
				Ui.Text( Ui.FontCaption, KX+KW+Ui.P(8.f), KY+(Ui.P(26.f)-Ui.TextHeight(Ui.FontCaption))*0.5f, FColor(120,220,140,255),
					*FString::Printf(TEXT("%i skill point%s"), State.SkillPoints, State.SkillPoints>1 ? TEXT("s") : TEXT("")), RALIGN_Left, 0 );
			}

			// Status chips above the plate.
			FLOAT ChipY = Badge.Y - Ui.P(40.f);
			if( State.DetentionSeconds>0 )
			{
				const FString T = FString::Printf( TEXT("Detention %i:%02i"), State.DetentionSeconds/60, State.DetentionSeconds%60 );
				const FRpgRect Chip( Badge.X, ChipY, Ui.TextWidth(Ui.FontCaption,*T)+Ui.P(24.f), Ui.P(28.f) );
				Ui.Glass( Chip );
				Ui.Rect( FRpgRect(Chip.X, Chip.Y, Max(1.f,Ui.P(3.f)), Chip.H), FColor(232,92,80,255) );
				Ui.Text( Ui.FontCaption, Chip.X+Ui.P(12.f), Chip.Y+(Chip.H-Ui.TextHeight(Ui.FontCaption))*0.5f, FColor(232,92,80,255), *T, RALIGN_Left, 0 );
				ChipY -= Ui.P(36.f);
			}
			if( State.FelixSeconds>0 )
			{
				const FString T = FString::Printf( TEXT("Felix Felicis %i:%02i"), State.FelixSeconds/60, State.FelixSeconds%60 );
				const FRpgRect Chip( Badge.X, ChipY, Ui.TextWidth(Ui.FontCaption,*T)+Ui.P(24.f), Ui.P(28.f) );
				Ui.Glass( Chip );
				Ui.Rect( FRpgRect(Chip.X, Chip.Y, Max(1.f,Ui.P(3.f)), Chip.H), GoldHi );
				Ui.Text( Ui.FontCaption, Chip.X+Ui.P(12.f), Chip.Y+(Chip.H-Ui.TextHeight(Ui.FontCaption))*0.5f, GoldHi, *T, RALIGN_Left, 0 );
			}
		}

		// Interaction prompt: one slot above the hotbar.
		if( bGameplay && NearNpc && IsInLevel(NearNpc) && !IsTalking() && !bAiming )
		{
			const TCHAR* Keys[2] = { TEXT("E"), TEXT("B") };
			const TCHAR* Verbs[2] = { TEXT("Talk"), TEXT("Trade") };
			Ui.Prompt( C->ClipX*0.5f, C->ClipY-Ui.P(160.f), Keys, Verbs, NearVendor>=0 ? 2 : 1, *NpcName(NearNpc) );
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

	// Notification cards on the right (max 3) and the centre banner.
	void DrawFeed()
	{
		// Dialogue and scripted scenes own the screen. Tick keeps queued
		// notifications alive until they can be read again.
		if( Menu==MENU_None && (IsTalking() || !IsFreeGameplay(Ui.Engine,CurrentHarry())) )
			return;
		UCanvas* C = Ui.C;
		UFont* Title = Ui.FontModernBold;
		UFont* Sub = Ui.FontCaption;
		const FLOAT TitleH = Ui.TextHeight( Title ), SubH = Ui.TextHeight( Sub );
		const FLOAT W = Min( Ui.P(360.f), C->ClipX*0.3f );
		FLOAT Y = Ui.P(260.f);
		if( Menu==MENU_None && (TrackerTimer>0.f || bShowAllHud) )
			Y = Max( Y, Ui.P(120.f) + 2.f*(Ui.TextHeight(Ui.FontModernBold)+Ui.TextHeight(Ui.FontCaption)+Ui.P(8.f)) + Ui.TextHeight(Ui.FontCaption)+Ui.P(24.f) );
		for( INT i=Feed.Num()-1; i>=0; i-- )
		{
			const FRpgFeed& Entry = Feed(i);
			const FLOAT IconBox = Ui.P(56.f);
			const FLOAT TextW = W - IconBox - Ui.P(24.f);
			// The sub line wraps to two lines instead of being cut off.
			FString Sub1 = Entry.Sub, Sub2;
			if( Ui.TextWidth(Sub,*Sub1) > TextW )
			{
				INT Cut = Sub1.Len();
				while( Cut>0 && (Ui.TextWidth(Sub,*Sub1.Left(Cut))>TextW || (*Sub1)[Cut-1]!=' ') )
					Cut--;
				if( Cut>0 )
				{
					Sub2 = Ui.FitText( Sub, *Sub1.Mid(Cut), TextW );
					Sub1 = Sub1.Left( Cut-1 );
				}
				else
					Sub1 = Ui.FitText( Sub, *Sub1, TextW );
			}
			const FLOAT H = Max( IconBox, Ui.P(12.f) + TitleH + (Sub1.Len() ? SubH : 0.f) + (Sub2.Len() ? SubH : 0.f) + Ui.P(10.f) );
			FLOAT Slide = 0.f;
			if( Entry.Age<0.18f )
				Slide = 1.f - Entry.Age/0.18f;
			else if( Entry.Age>3.8f )
				Slide = Min( 1.f, (Entry.Age-3.8f)/0.35f );
			const FRpgRect Card( C->ClipX - W - Ui.P(48.f) + Slide*(W+Ui.P(60.f)), Y, W, H );
			Ui.Glass( Card );
			Ui.Rect( FRpgRect(Card.X, Card.Y, Max(1.f,Ui.P(3.f)), Card.H), Entry.Color );
			UTexture* Icon = ToastIcon( Entry );
			if( Icon )
			{
				const FLOAT IS = Ui.P(36.f);
				Ui.Icon( Icon, FRpgRect(Card.X+Ui.P(3.f)+(IconBox-IS)*0.5f, Card.Y+(H-IS)*0.5f, IS, IS) );
			}
			const FLOAT TX = Card.X + IconBox + Ui.P(8.f);
			FLOAT TY = Card.Y + Ui.P(8.f);
			Ui.Text( Title, TX, TY, FColor(240,238,230,255), *Ui.FitText(Title,*Entry.Text,TextW), RALIGN_Left, 0 );
			TY += TitleH + Ui.P(2.f);
			if( Sub1.Len() )
				Ui.Text( Sub, TX, TY, FColor(182,186,206,255), *Sub1, RALIGN_Left, 0 );
			if( Sub2.Len() )
				Ui.Text( Sub, TX, TY+SubH, FColor(182,186,206,255), *Sub2, RALIGN_Left, 0 );
			Y += H + Ui.P(8.f);
		}

		// Centre banner: level up, new school day.
		if( BannerText.Len() && BannerAge<3.5f )
		{
			const FLOAT Grow = Min( 1.f, BannerAge/0.2f );
			const FLOAT BigH = Ui.TextHeight( Ui.FontTitle );
			const FLOAT BW = Min(Ui.P(620.f),C->ClipX*0.48f)*Grow, BH = Ui.P(12.f) + BigH + Ui.P(8.f) + SubH + Ui.P(12.f);
			const FRpgRect B( Ui.P(24.f), Max(Ui.P(260.f),C->ClipY*0.25f), BW, BH );
			Ui.Glass( B );
			const FLOAT T = Max( 1.f, Ui.P(2.f) );
			// Gold hairlines that fade towards the ends.
			for( INT k=0; k<3; k++ )
			{
				const FLOAT Inset = B.W*0.12f*k;
				const FColor Col( 255-60*k, 222-50*k, 130-40*k, 255 );
				Ui.Rect( FRpgRect(B.X+Inset*0.5f, B.Y, B.W-Inset, T), Col );
				Ui.Rect( FRpgRect(B.X+Inset*0.5f, B.Y+B.H-T, B.W-Inset, T), Col );
			}
			if( Grow>=1.f )
			{
				Ui.Text( Ui.FontTitle, B.X+Ui.P(16.f), B.Y+Ui.P(12.f), RpgGoldBright, *Ui.FitText(Ui.FontTitle,*BannerText,B.W-Ui.P(32.f)), RALIGN_Left, 0 );
				Ui.Text( Ui.FontModern, B.X+Ui.P(16.f), B.Y+Ui.P(20.f)+BigH, RpgParchment, *Ui.FitText(Ui.FontModern,*BannerSub,B.W-Ui.P(32.f)), RALIGN_Left, 0 );
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
		const FLOAT Gap = Ui.P(16.f);
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
			// Keep selection inside the newly visible page; otherwise
			// ClampSelection immediately snaps wheel scrolling back.
			if( Count>0 )
				Sel = Clamp( Sel, Scroll*Cols, Min(Count-1,(Scroll+Rows)*Cols-1) );
			ConfirmIndex = -1;
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

	// What the buy button can do for an offer, and the reason if not.
	FString OfferAction( APlayerPawn* Harry, const FOffer& O, UBOOL& bEnabled, FColor& StatusColor, FString& Status )
	{
		const INT Price = OfferPrice( O ), Beans = GetBeans( Harry );
		bEnabled = 1;
		StatusColor = RpgParchment;
		Status = FString::Printf( TEXT("%i beans"), Price );
		if( VendorRefuses() && !(O.Kind==OFFER_Consumable && O.Id==CON_Pardon) )
		{
			bEnabled = 0;
			return TEXT("In detention");
		}
		if( O.Kind==OFFER_Cosmetic && IsOwned(O.Id) )
		{
			const UBOOL bWearing = State.Equip[GCosmetics[O.Id].Slot]==O.Id;
			StatusColor = RpgGreen;
			Status = bWearing ? TEXT("Wearing") : TEXT("Owned");
			bEnabled = !bWearing;
			return bWearing ? TEXT("Wearing") : TEXT("Wear");
		}
		if( O.Kind==OFFER_Sell )
		{
			const INT Have = SellCount( Harry, O.Id );
			StatusColor = RpgGreen;
			Status = FString::Printf( TEXT("+%i beans  (have %i)"), Price, Have );
			bEnabled = Have>0;
			return Have>0 ? FString::Printf( TEXT("Sell for %i"), Price ) : FString(TEXT("None to sell"));
		}
		if( O.Kind==OFFER_Consumable && OwnsGameItem(O.Id) )
		{
			StatusColor = RpgGreen;
			Status = TEXT("Owned");
			bEnabled = 0;
			return TEXT("Owned");
		}
		if( O.Kind==OFFER_Consumable && StockLeft(O)<=0 )
		{
			StatusColor = RpgMuted;
			Status = TEXT("Sold out");
			bEnabled = 0;
			return TEXT("Sold out today");
		}
		if( Beans<Price )
		{
			StatusColor = RpgRed;
			bEnabled = 0;
			return FString::Printf( TEXT("Need %i more beans"), Price-Beans );
		}
		return TEXT("Buy");
	}

	void DrawShop( UGameEngine* Engine, APlayerPawn* Harry )
	{
		const FLOAT S = Ui.S;
		FRpgRect Left, Right;
		Ui.Screen( Left, Right );
		// Night sky (drawn by Screen), framed sections like the book.
		Ui.Panel( FRpgRect(Left.X-Ui.P(10.f), Left.Y-Ui.P(10.f), Left.W+Ui.P(20.f), Left.H+Ui.P(20.f)) );
		Ui.Panel( FRpgRect(Right.X-Ui.P(10.f), Right.Y-Ui.P(10.f), Right.W+Ui.P(20.f), Right.H+Ui.P(20.f)) );
		const FVendorDef& V = GVendors[ShopVendorDef];

		// Left: the vendor in 3D, their name and what they say.
		const FRpgRect Stage( Left.X, Left.Y, Left.W, Left.H*0.66f );
		Ui.Rect( Stage, FColor(22,30,66,255) );
		if( ShopVendor && !IsInLevel(ShopVendor) )
			ShopVendor = NULL;
		// Try-on: a selected outfit, hat or pair of glasses is shown on Harry.
		if( TryOn>=0 && TryOn<GNumCosmetics && (GCosmetics[TryOn].Slot==RSLOT_Outfit || GCosmetics[TryOn].Slot==RSLOT_Hat || GCosmetics[TryOn].Slot==RSLOT_Glasses) )
		{
			const FCosmetic& T = GCosmetics[TryOn];
			DrawHarryPreview( Engine, Stage,
				T.Slot==RSLOT_Outfit ? TryOn : State.Equip[RSLOT_Outfit],
				T.Slot==RSLOT_Glasses ? TryOn : State.Equip[RSLOT_Glasses],
				T.Slot==RSLOT_Hat ? TryOn : State.Equip[RSLOT_Hat],
				DefaultCosmetic(RSLOT_Pet), PREV_Body, PreviewYaw*0.15f );
			Ui.Text( Ui.FontModern, Stage.X+Stage.W*0.5f, Stage.Y+Stage.H-Ui.TextHeight(Ui.FontModern)-6*S, FColor(182,186,206,255), TEXT("Trying it on"), RALIGN_Center );
		}
		else if( ShopVendor )
			DrawNpcPreview( Engine, Stage, ShopVendor, PreviewYaw*0.15f );
		FLOAT Y = Stage.Y + Stage.H + 6*S;
		const FString VendorName = ShopVendorDef>=VEN_StudentGryffindor && ShopVendor ? NpcName(ShopVendor) : FString(V.Name);
		Ui.Text( Ui.FontTitle, Left.X+Left.W*0.5f, Y, RpgGoldBright, *VendorName, RALIGN_Center );
		Y += Ui.TextHeight( Ui.FontTitle ) + 6*S;
		const FRpgRect Bubble( Left.X, Y, Left.W, Left.Y+Left.H - Y - 36*S );
		Ui.Rect( Bubble, FColor(18,24,54,255) );
		Ui.TextWrapped( Ui.FontBody, Bubble.Inset(8*S), RpgParchment, VendorRefuses() ? TEXT("You're in detention, Potter. No trading until it's over.") : *Speech );
		DrawBeansLine( Harry, Left.X, Left.Y+Left.H-24*S );

		// Right: title, tabs, grid.
		FLOAT RY = Right.Y;
		Ui.Text( Ui.FontInk ? Ui.FontInk : Ui.FontTitle, Right.X, RY, RpgGoldBright, TEXT("Shop") );
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
			if( Index==Sel )
				TryOn = O.Kind==OFFER_Cosmetic ? O.Id : -1;
			const FLOAT LineH = Ui.TextHeight( Ui.FontBody ), Pad = Ui.P(8.f);
			const FLOAT LabelH = LineH*2 + Ui.P(10.f);
			const FRpgRect Pic( Cell.X+Pad, Cell.Y+Pad, Cell.W-Pad*2, Max(LineH, Cell.H-LabelH-Pad*2) );
			if( O.Kind==OFFER_Cosmetic )
				DrawCosmeticPreview( Engine, Pic, O.Id, 0.f );
			else
				DrawConsumablePreview( Engine, Pic, O.Id, 0.f );
			UBOOL bCan;
			FColor StatusColor;
			FString Status;
			OfferAction( Harry, O, bCan, StatusColor, Status );
			const FLOAT LY = Cell.Y + Cell.H - LabelH;
			Ui.Text( Ui.FontBody, Cell.X+Pad, LY, RpgParchment, *Ui.FitText(Ui.FontBody,OfferName(O),Cell.W-Pad*2), RALIGN_Left, 0 );
			Ui.Text( Ui.FontBody, Cell.X+Pad, LY+LineH, StatusColor, *Ui.FitText(Ui.FontBody,*Status,Cell.W-Pad*2), RALIGN_Left, 0 );
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
			UBOOL bCan;
			FColor StatusColor;
			FString Status;
			FString Label = OfferAction( Harry, O, bCan, StatusColor, Status );
			if( bCan && Label==TEXT("Buy") && ConfirmIndex==Sel )
				Label = FString::Printf( TEXT("Confirm - %i beans"), OfferPrice(O) );
			const FLOAT BH = Ui.TextHeight(Ui.FontBody) + Ui.P(16.f);
			const FLOAT CloseW = Ui.TextWidth(Ui.FontBody,TEXT("Close")) + Ui.P(40.f);
			const FLOAT BuyW = Ui.TextWidth(Ui.FontBody,*Label) + Ui.P(40.f);
			const FRpgRect BuyRect( Right.X+Right.W-CloseW-Ui.P(12.f)-BuyW, Right.Y+Right.H-BH, BuyW, BH );
			if( Ui.Button( BuyRect, *Label, bCan, &Ui ) )
				bActivate = 1;
			if( bActivate && !bCan )
			{
				Ui.Play( RSND_Deny, Harry );
				bActivate = 0;
			}
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
		{
			const FLOAT BH = Ui.TextHeight(Ui.FontBody) + Ui.P(16.f);
			const FLOAT CloseW = Ui.TextWidth(Ui.FontBody,TEXT("Close")) + Ui.P(40.f);
			if( Ui.Button( FRpgRect(Right.X+Right.W-CloseW, Right.Y+Right.H-BH, CloseW, BH), TEXT("Close"), 1, &Offers ) )
				CloseMenu( Engine );
		}
		Ui.Footer( TEXT("Q/E: tab    Arrows / wheel: choose    Enter: buy    Esc/B: close") );
	}

	/*-------------------------------------------------------------------------
		Character book.
	-------------------------------------------------------------------------*/


	void DrawBook( UGameEngine* Engine, APlayerPawn* Harry )
	{
		const FLOAT Pad = Ui.P(24.f), Gap = Ui.P(20.f);
		const FLOAT LineH = Ui.TextHeight(Ui.FontBody);
		UFont* TitleFont = Ui.FontInk ? Ui.FontInk : Ui.FontTitle;
		const FLOAT TitleH = Ui.TextHeight(TitleFont);
		const FLOAT HeaderH = TitleH+LineH+Ui.P(24.f);
		const FLOAT FooterH = LineH*2.f+Ui.P(32.f);
		Ui.Sky(FRpgRect(0,0,Ui.C->ClipX,Ui.C->ClipY));
		FLOAT NavW = 0.f;
		for( INT t=0; t<BOOK_MAX; t++ )
			NavW = Max(NavW,Ui.TextWidth(Ui.FontBody,GBookTabNames[t]));
		NavW += Ui.P(32.f);
		const FLOAT Top = Pad+HeaderH;
		const FLOAT RowH = Ui.TextHeight(Ui.FontBody)+Ui.P(12.f);
		// Framed navigation and content sections on the night sky.
		Ui.Panel(FRpgRect(Pad-Ui.P(8.f),Top-Ui.P(8.f),NavW+Ui.P(16.f),BOOK_MAX*RowH+Ui.P(16.f)));
		Ui.Panel(FRpgRect(Pad+NavW+Gap-Ui.P(12.f),Top-Ui.P(12.f),Ui.C->ClipX-Pad*2-NavW-Gap+Ui.P(24.f),Ui.C->ClipY-Top-Pad-FooterH+Ui.P(24.f)));
		for( INT t=0; t<BOOK_MAX; t++ )
		{
			const FRpgRect R(Pad,Top+t*RowH,NavW,RowH);
			const UBOOL bHover = Ui.Hovered(R,&GBookTabNames[t]);
			if( BookTab==t || bHover )
				Ui.Rect(R,BookTab==t ? FColor(34,44,96,255) : FColor(22,30,66,255));
			if( BookTab==t )
				Ui.Rect(FRpgRect(R.X,R.Y,Ui.P(2.f),R.H),RpgGold);
			Ui.Text(Ui.FontBody,R.X+Ui.P(12.f),R.Y+Ui.P(6.f),BookTab==t ? RpgGoldBright : RpgMuted,GBookTabNames[t],RALIGN_Left,0);
			if( Ui.Clicked(R,&GBookTabNames[t]) && BookTab!=t )
			{
				BookTab = t;
				Sel = Scroll = 0;
				TryOn = -1;
				bActivate = 0;
				Ui.Play(RSND_Tab,Harry);
			}
		}
		const FRpgRect Body(Pad+NavW+Gap,Top,Max(Ui.P(80.f),Ui.C->ClipX-Pad*2-NavW-Gap),Max(Ui.P(80.f),Ui.C->ClipY-Top-Pad-FooterH));
		Ui.Text(TitleFont,Body.X,Pad-Ui.P(6.f),RpgGoldBright,GBookTabNames[BookTab],RALIGN_Left);
		Ui.Text(Ui.FontBody,Body.X,Pad+TitleH,RpgMuted,
			*Ui.FitText(Ui.FontBody,*FString::Printf(TEXT("Level %i   |   %i skill points   |   School day %i"),State.Level,State.SkillPoints,State.Day),Body.W),RALIGN_Left,0);
		Ui.Rect(FRpgRect(Body.X,Top-Ui.P(10.f),Body.W,Max(1.f,Ui.P(1.f))),FColor(70,62,40,255));

		switch( BookTab )
		{
			case BOOK_Skills: DrawSkillsTab(Harry,Body); break;
			case BOOK_Spells: DrawSpellsTab(Harry,Body); break;
			case BOOK_Wardrobe:
			{
				const FLOAT StageW = Body.W*0.30f;
				const FRpgRect Items(Body.X+StageW+Gap,Body.Y,Body.W-StageW-Gap,Body.H);
				// Resolve the current selection before drawing its large preview.
				DrawWardrobeTab(Engine,Harry,Items);
				const FRpgRect Stage(Body.X,Body.Y,StageW,Body.H-LineH*2-Ui.P(12.f));
				Ui.Rect(Stage,FColor(22,30,66,255));
				if( TryOn>=0 && TryOn<GNumCosmetics )
					DrawCosmeticPreview(Engine,Stage.Inset(Ui.P(12.f)),TryOn,0.f);
				Ui.Text(Ui.FontBody,Body.X,Stage.Y+Stage.H+Ui.P(8.f),RpgMuted,TEXT("Preview"),RALIGN_Left,0);
				break;
			}
			case BOOK_Collection: DrawCollectionTab(Engine,Harry,Body); break;
			case BOOK_Journal: DrawJournalTab(Harry,Body); break;
			case BOOK_Room: DrawRoomTab(Engine,Harry,Body); break;
			case BOOK_Trophies: DrawTrophiesTab(Harry,Body); break;
			case BOOK_HouseCup: DrawHouseCupTab(Harry,Body); break;
			case BOOK_Letters: DrawLettersTab(Harry,Body); break;
			default: DrawStatusTab(Harry,Body); break;
		}
		bActivate = 0;
		const FLOAT ButtonH = LineH+Ui.P(16.f);
		const FLOAT ButtonW = Ui.TextWidth(Ui.FontBody,TEXT("Close"))+Ui.P(40.f);
		const FLOAT BottomY = Ui.C->ClipY-Pad-ButtonH-LineH-Ui.P(8.f);
		if( NoticeTime>0.f && Notice.Len() )
			Ui.Text(Ui.FontBody,Body.X,BottomY,RpgGoldBright,*Ui.FitText(Ui.FontBody,*Notice,Body.W-ButtonW-Gap),RALIGN_Left,0);
		if( Ui.Button(FRpgRect(Ui.C->ClipX-Pad-ButtonW,BottomY,ButtonW,ButtonH),TEXT("Close"),1,&Menu) )
			CloseMenu(Engine);
		{
			// Save (F5) and, inside a challenge, the way back to the hub.
			const FLOAT SaveW = Ui.TextWidth(Ui.FontBody,TEXT("Save game"))+Ui.P(40.f);
			FLOAT BX = Ui.C->ClipX-Pad-ButtonW-Ui.P(12.f)-SaveW;
			if( Ui.Button(FRpgRect(BX,BottomY,SaveW,ButtonH),TEXT("Save game"),1,&State.Day) )
			{
				CloseMenu(Engine);
				ManualSave(Engine,Harry);
				return;
			}
			if( HubExit(Engine) )
			{
				const TCHAR* Leave = bConfirmLeave ? TEXT("Click again to leave") : TEXT("Leave challenge");
				const FLOAT LeaveW = Ui.TextWidth(Ui.FontBody,Leave)+Ui.P(40.f);
				BX -= Ui.P(12.f)+LeaveW;
				if( Ui.Button(FRpgRect(BX,BottomY,LeaveW,ButtonH),Leave,1,&State.LessonDay) )
				{
					if( bConfirmLeave )
					{
						bConfirmLeave = 0;
						LeaveChallenge(Engine,Harry);
						return;
					}
					bConfirmLeave = 1;
				}
			}
		}
		Ui.Footer(TEXT("Q/E: section    Tab: category    Arrows / wheel: choose    Enter: act    K/Esc: close"));
	}

	UBOOL RowActivated( const FRpgRect& Row, INT Index, const void* Id )
	{
		const UBOOL bHover = Ui.Hovered( Row, Id );
		if( Index==Sel || bHover )
			Ui.Rect( Row, Index==Sel ? FColor(34,44,96,255) : FColor(22,30,66,255) );
		if( Ui.Clicked(Row,Id) )
		{
			Sel = Index;
			return 1;
		}
		return bActivate && Index==Sel;
	}


	void DrawSkillsTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		Sel = Clamp(Sel,0,SKILL_MAX-1);
		const FLOAT ButtonH = Ui.TextHeight(Ui.FontBody)+Ui.P(16.f);
		const FLOAT RowH = Min( Ui.TextHeight(Ui.FontItem)+Ui.TextHeight(Ui.FontBody)+Ui.P(20.f), (Area.H-ButtonH-Ui.P(16.f))/SKILL_MAX );
		for( INT i=0; i<SKILL_MAX; i++ )
		{
			const FRpgRect Row(Area.X,Area.Y+i*RowH,Area.W,RowH-Ui.P(6.f));
			const UBOOL bHover = Ui.Hovered(Row,&GSkillNames[i]);
			Ui.Rect(Row,i==Sel ? FColor(34,44,96,255) : FColor(22,30,66,255));
			if( Ui.Clicked(Row,&GSkillNames[i]) )
			{
				Sel = i;
				bActivate = 0; // inspecting a row never spends a point
			}
			Ui.Text(Ui.FontItem,Row.X+Ui.P(12.f),Row.Y+Ui.P(6.f),i==Sel || bHover ? RpgGoldBright : RpgParchment,GSkillNames[i],RALIGN_Left,0);
			{
				// Gold pips: one per learned point.
				const FLOAT Pip = Ui.TextHeight(Ui.FontSmall)*0.8f;
				Ui.Pips( Row.X+Row.W-Ui.P(12.f)-SKILL_MAX_PIPS*(Pip+Ui.P(6.f)), Row.Y+Ui.P(8.f), State.Skill[i], SKILL_MAX_PIPS, Pip );
			}
			Ui.Text(Ui.FontBody,Row.X+Ui.P(12.f),Row.Y+Ui.TextHeight(Ui.FontItem)+Ui.P(8.f),RpgMuted,*Ui.FitText(Ui.FontBody,GSkillDescs[i],Row.W-Ui.P(24.f)),RALIGN_Left,0);
		}
		const FLOAT H = Ui.TextHeight(Ui.FontBody)+Ui.P(16.f);
		const UBOOL bCanLearn = State.SkillPoints>0 && State.Skill[Sel]<SKILL_MAX_PIPS;
		const FString Label = State.Skill[Sel]>=SKILL_MAX_PIPS ? FString(TEXT("Mastered")) :
			State.SkillPoints<=0 ? FString(TEXT("No skill points")) : FString::Printf(TEXT("Improve %s - 1 point"),GSkillNames[Sel]);
		const FRpgRect B(Area.X,Area.Y+SKILL_MAX*RowH+Ui.P(12.f),Min(Area.W,Ui.TextWidth(Ui.FontBody,*Label)+Ui.P(32.f)),H);
		if( (Ui.Button(B,*Label,bCanLearn,&State.SkillPoints) || bActivate) && bCanLearn )
		{
			State.SkillPoints--;
			State.Skill[Sel]++;
			ApplySkills(Harry);
			SetNotice(FString::Printf(TEXT("%s improved!"),GSkillNames[Sel]));
			Ui.Play(RSND_Buy,Harry);
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
		const FLOAT S = Ui.P(2.f);
		Sel = Clamp( Sel, 0, SP_MAX-1 );

		// Spell picker: the game's spells, then the wheel spells (two rows).
		const INT PerRow = (SP_MAX+1)/2;
		const FLOAT CellW = Area.W/PerRow;
		const FLOAT IconS = Min( CellW*0.45f, 30*S );
		const FLOAT CellH = IconS + Ui.TextHeight(Ui.FontSmall) + 10*S;
		const FLOAT PickH = CellH*2 + 4*S;
		for( INT sp=0; sp<SP_MAX; sp++ )
		{
			const FRpgRect Cell( Area.X + CellW*(sp%PerRow), Area.Y + (sp/PerRow)*(CellH+4*S), CellW-4*S, CellH );
			const UBOOL bHover = Ui.Hovered( Cell, &GSpellNames[sp] );
			if( sp==Sel )
			{
				Ui.Rect( Cell, FColor(34,44,96,255) );
				Ui.Border( Cell, Max(1.f,S), RpgGoldBright );
			}
			else if( bHover )
				Ui.Rect( Cell, FColor(22,30,66,255) );
			UTexture* Icon = LoadTex( GSpellIcons[sp] );
			if( Icon )
				Ui.Icon( Icon, FRpgRect(Cell.X+(Cell.W-IconS)*0.5f, Cell.Y+4*S, IconS, IconS), sp==Sel ? 1.f : 0.6f );
			Ui.Text( Ui.FontSmall, Cell.X+Cell.W*0.5f, Cell.Y+IconS+6*S, sp==Sel ? RpgGoldBright : RpgMuted,
				*Ui.FitText(Ui.FontSmall,GSpellNames[sp],Cell.W-4*S), RALIGN_Center );
			if( Ui.Clicked( Cell, &GSpellNames[sp] ) )
			{
				Sel = sp;
				Ui.Play( RSND_Tab, Harry );
			}
		}

		// Upgrade tree of the selected spell.
		INT Chain[PERK_MAX];
		const INT Count = SpellChain( Sel, Chain );
		INT Level = 0;
		while( Level<Count && HasPerk(Chain[Level]) )
			Level++;
		FLOAT Y = Area.Y + PickH + 12*S;
		const FLOAT Link = 10*S;
		const FLOAT NodeH = Count ? Max( Ui.TextHeight(Ui.FontItem)+Ui.TextHeight(Ui.FontBody)*2+18*S, Min( (Area.Y+Area.H - Y - Link*(Count-1))/Count, 110*S ) ) : 0.f;
		for( INT n=0; n<Count; n++ )
		{
			const FPerkDef& D = GPerks[Chain[n]];
			const UBOOL bLearned = n<Level;
			const UBOOL bNext = n==Level;
			const FColor Edge = bLearned ? RpgGreen : (bNext ? RpgGoldBright : FColor(70,74,104,255));
			const FRpgRect Node( Area.X + 18*S, Y, Area.W - 18*S, NodeH );
			const UBOOL bHover = bNext && Ui.Hovered( Node, &GPerks[Chain[n]] );
			Ui.Rect( Node, bHover ? FColor(36,46,98,255) : (bNext ? FColor(28,36,80,255) : FColor(18,24,54,255)) );
			Ui.Border( Node, Max(1.f,S*(bNext?1.5f:1.f)), Edge );

			// Connector dot and line on the left.
			const FLOAT Dot = 10*S;
			Ui.Rect( FRpgRect(Area.X+4*S, Node.Y+NodeH*0.5f-Dot*0.5f, Dot, Dot), Edge );
			if( n<Count-1 )
				Ui.Rect( FRpgRect(Area.X+4*S+Dot*0.5f-S, Node.Y+NodeH*0.5f+Dot*0.5f, 2*S, NodeH*0.5f+Link+NodeH*0.5f-Dot), n<Level-1 ? RpgGreen : FColor(70,74,104,255) );

			const FString Head = FString::Printf( TEXT("Level %i  -  %s"), n+1, D.Name );
			Ui.Text( Ui.FontItem, Node.X+10*S, Node.Y+6*S, bLearned ? RpgGreen : (bNext ? RpgGoldBright : RpgMuted), *Ui.FitText(Ui.FontItem,*Head,Node.W-120*S) );
			const TCHAR* Status = bLearned ? TEXT("Learned") : (bNext ? (State.SkillPoints>=D.Cost ? TEXT("Click to learn") : TEXT("Not enough points")) : TEXT("Locked"));
			Ui.Text( Ui.FontSmall, Node.X+Node.W-10*S, Node.Y+8*S, bLearned ? RpgGreen : (bNext && State.SkillPoints>=D.Cost ? RpgGoldBright : RpgMuted), Status, RALIGN_Right );
			Ui.Text( Ui.FontSmall, Node.X+Node.W-10*S, Node.Y+8*S+Ui.TextHeight(Ui.FontSmall), RpgParchment,
				*FString::Printf(TEXT("%i skill point%s"), D.Cost, D.Cost>1 ? TEXT("s") : TEXT("")), RALIGN_Right );
			Ui.TextWrapped( Ui.FontBody, FRpgRect(Node.X+10*S, Node.Y+Ui.TextHeight(Ui.FontItem)+8*S, Node.W-20*S, Max(Ui.TextHeight(Ui.FontBody)*2, NodeH-Ui.TextHeight(Ui.FontItem)-10*S)),
				bNext || bLearned ? RpgParchment : RpgMuted, D.Desc );

			if( bNext && (Ui.Clicked(Node,&GPerks[Chain[n]]) || bActivate) )
			{
				bActivate = 0;
				if( State.SkillPoints < D.Cost )
				{
					SetNotice( FString::Printf(TEXT("%s needs %i skill point%s."), D.Name, D.Cost, D.Cost>1 ? TEXT("s") : TEXT("")) );
					Ui.Play( RSND_Deny, Harry );
				}
				else
				{
					State.SkillPoints -= D.Cost;
					State.Perks |= (1<<Chain[n]);
					SetNotice( FString::Printf(TEXT("%s level %i: %s!"), GSpellNames[Sel], n+1, D.Name) );
					Ui.Play( RSND_Reward, Harry );
					ApplySkills( Harry );
				}
			}
			Y += NodeH + Link;
		}
		if( Level>=Count && bActivate )
		{
			bActivate = 0;
			SetNotice( FString::Printf(TEXT("%s is mastered."), GSpellNames[Sel]) );
		}
	}


	void DrawWardrobeTab( UGameEngine* Engine, APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT LineH = Ui.TextHeight(Ui.FontBody), Gap = Ui.P(8.f);
		const FLOAT TabH = LineH+Ui.P(12.f);
		FLOAT TX = Area.X, TY = Area.Y;
		for( INT t=0; t<RSLOT_MAX; t++ )
		{
			const FLOAT TW = Ui.TextWidth(Ui.FontBody,GSlotNames[t])+Ui.P(24.f);
			if( TX>Area.X && TX+TW>Area.X+Area.W )
			{
				TX = Area.X;
				TY += TabH+Gap;
			}
			const FRpgRect R(TX,TY,TW,TabH);
			if( WardrobeSlot==t || Ui.Hovered(R,&GSlotNames[t]) )
				Ui.Rect(R,FColor(34,44,96,255));
			Ui.Text(Ui.FontBody,TX+Ui.P(12.f),TY+Ui.P(6.f),WardrobeSlot==t ? RpgGoldBright : RpgMuted,GSlotNames[t],RALIGN_Left,0);
			if( Ui.Clicked(R,&GSlotNames[t]) && WardrobeSlot!=t )
			{
				WardrobeSlot = t;
				Sel = Scroll = 0;
				bActivate = 0;
			}
			TX += TW+Gap;
		}
		INT Items[MAX_COSMETICS], Count = 0;
		for( INT Pass=0; Pass<2; Pass++ )
			for( INT i=0; i<GNumCosmetics; i++ )
				if( GCosmetics[i].Slot==WardrobeSlot && (IsOwned(i)==(Pass==0)) )
					Items[Count++] = i;
		const FLOAT GridY = TY+TabH+Gap;
		const FLOAT DetailH = Ui.TextHeight(Ui.FontItem)+LineH*4+Ui.P(44.f);
		const FLOAT GridH = Max(LineH*4,Area.Y+Area.H-GridY-DetailH);
		const INT Cols = 3, Rows = GridH>=LineH*10 ? 2 : 1;
		ApplyWheel(Count,Cols,Rows);
		ClampSelection(Count,Cols,Rows);
		const FRpgRect GridArea(Area.X,GridY,Area.W,GridH);
		// Resolve clicks before drawing: preview, label and detail always agree.
		for( INT v=0; v<Cols*Rows && Scroll*Cols+v<Count; v++ )
		{
			const INT Index = Scroll*Cols+v;
			if( Ui.Clicked(GridCell(GridArea,Cols,Rows,v),&GCosmetics[Items[Index]]) )
			{
				Sel = Index;
				bActivate = 0;
				Ui.Play(RSND_Click,Harry);
			}
		}
		if( !Count )
			return;
		TryOn = Items[Sel];
		for( INT v=0; v<Cols*Rows && Scroll*Cols+v<Count; v++ )
		{
			const INT Index = Scroll*Cols+v, Id = Items[Index];
			const FCosmetic& Cos = GCosmetics[Id];
			const FRpgRect Cell = GridCell(GridArea,Cols,Rows,v);
			Ui.Tile(Cell,Index==Sel,Ui.Hovered(Cell,&GCosmetics[Id]),GRarityColors[Cos.Rarity]);
			// One label line; its colour tells the status (gold worn, green owned).
			const FLOAT LabelH = LineH+Ui.P(10.f);
			const FRpgRect Pic(Cell.X+Gap,Cell.Y+Gap,Cell.W-Gap*2,Max(LineH,Cell.H-LabelH-Gap));
			DrawCosmeticPreview(Engine,Pic,Id,0.f);
			const FLOAT LY = Cell.Y+Cell.H-LabelH;
			Ui.Rect(FRpgRect(Cell.X+1,LY,Cell.W-2,LabelH-1),FColor(12,16,40,255));
			const FColor NameCol = State.Equip[WardrobeSlot]==Id ? RpgGoldBright : IsOwned(Id) ? RpgGreen : RpgMuted;
			Ui.Text(Ui.FontBody,Cell.X+Cell.W*0.5f,LY+Ui.P(5.f),NameCol,*Ui.FitText(Ui.FontBody,Cos.Name,Cell.W-Gap*2),RALIGN_Center,0);
		}
		DrawScrollHints(GridArea,Count,Cols,Rows);
		const INT Id = Items[Sel];
		const FCosmetic& Cos = GCosmetics[Id];
		FLOAT Y = GridY+GridH+Gap;
		Ui.Text(Ui.FontItem,Area.X,Y,RpgGoldBright,*Ui.FitText(Ui.FontItem,Cos.Name,Area.W),RALIGN_Left,0);
		Y += Ui.TextHeight(Ui.FontItem)+Ui.P(4.f);
		FString Meta = FString::Printf(TEXT("%s %s"),GRarityNames[Cos.Rarity],GSlotNames[Cos.Slot]);
		if( Cos.Stat!=STAT_None )
			Meta += FString(TEXT("  ")) + StatText(Cos.Stat,Cos.StatValue);
		Ui.Text(Ui.FontBody,Area.X,Y,RpgMuted,*Ui.FitText(Ui.FontBody,*Meta,Area.W),RALIGN_Left,0);
		Y += LineH;
		Ui.TextWrapped(Ui.FontBody,FRpgRect(Area.X,Y,Area.W,LineH*2),RpgParchment,Cos.Desc);
		const UBOOL bCanEquip = IsOwned(Id) && State.Equip[WardrobeSlot]!=Id;
		const TCHAR* Action = !IsOwned(Id) ? TEXT("Locked - visit a vendor") : bCanEquip ? TEXT("Equip") : TEXT("Equipped");
		const FRpgRect B(Area.X,Area.Y+Area.H-LineH-Ui.P(16.f),Min(Area.W,Ui.TextWidth(Ui.FontBody,Action)+Ui.P(32.f)),LineH+Ui.P(16.f));
		if( (Ui.Button(B,Action,bCanEquip,&TryOn) || bActivate) && bCanEquip )
		{
			State.Equip[WardrobeSlot] = Id;
			SetNotice(FString::Printf(TEXT("Now wearing: %s"),Cos.Name));
			Ui.Play(RSND_Buy,Harry);
		}
		bActivate = 0;
	}

	void DrawCollectionTab( UGameEngine* Engine, APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.P(2.f);
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
				Ui.Text( Ui.FontBody, Row.X+Row.W-4*S, Row.Y, Tier ? RpgGreen : RpgParchment, *FString::Printf(TEXT("%i defeated  %s"), State.Kills[c], Tier ? *FString::Printf(TEXT("Rank %s"), Tier==1 ? TEXT("I") : Tier==2 ? TEXT("II") : TEXT("III")) : TEXT("")), RALIGN_Right );
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
		const FLOAT S = Ui.P(2.f);
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

	void DrawHouseCupTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.P(2.f);
		FLOAT Y = Area.Y;
		// Standings, highest first.
		INT Order[HOUSE_MAX] = { 0, 1, 2, 3 };
		INT Pts[HOUSE_MAX];
		INT Top = 1;
		for( INT i=0; i<HOUSE_MAX; i++ )
		{
			Pts[i] = bBaselineValid ? LastHouse[i] : 0;
			Top = Max( Top, Pts[i] );
		}
		for( INT a=0; a<HOUSE_MAX; a++ )
			for( INT b=a+1; b<HOUSE_MAX; b++ )
				if( Pts[Order[b]] > Pts[Order[a]] )
					Exchange( Order[a], Order[b] );
		const FLOAT RowH = Ui.TextHeight( Ui.FontItem ) + 8*S;
		for( INT r=0; r<HOUSE_MAX; r++ )
		{
			const INT h = Order[r];
			const FLOAT CS = RowH - 6*S;
			if( Crests[h] )
				Ui.Icon( Crests[h], FRpgRect(Area.X, Y, CS, CS) );
			Ui.Text( Ui.FontItem, Area.X+CS+8*S, Y, GHouseColors[h], *FString::Printf(TEXT("%i. %s"), r+1, GHouseNames[h]) );
			Ui.Text( Ui.FontItem, Area.X+Area.W, Y, RpgParchment, *FString::Printf(TEXT("%i"), Pts[h]), RALIGN_Right );
			Ui.Bar( FRpgRect(Area.X+CS+8*S, Y+RowH-6*S, Area.W-CS-8*S, 4*S), (FLOAT)Pts[h]/Top, GHouseColors[h] );
			Y += RowH + 4*S;
		}

		// Give every house its own readable reputation cell.
		const FLOAT RepH = Ui.TextHeight(Ui.FontSmall)+Ui.P(6.f);
		for( INT h=0; h<HOUSE_MAX; h++ )
		{
			const FLOAT RX = Area.X+(h%2)*(Area.W*0.5f);
			const FString Rep = FString::Printf(TEXT("%s: %s"),GHouseNames[h],RepTier(State.Rep[h]));
			Ui.Text(Ui.FontSmall,RX,Y+(h/2)*RepH,RpgMuted,*Ui.FitText(Ui.FontSmall,*Rep,Area.W*0.5f-Ui.P(12.f)),RALIGN_Left,0);
		}
		Y += RepH*2+Ui.P(12.f);
		Ui.Text( Ui.FontItem, Area.X, Y, RpgGoldBright, *FString::Printf(TEXT("Today's tasks  -  School day %i"), State.Day) );
		Y += Ui.TextHeight( Ui.FontItem ) + 6*S;
		const FLOAT TaskH = Ui.TextHeight(Ui.FontBody) + Ui.TextHeight(Ui.FontSmall) + 16*S;
		for( INT t=0; t<TASKS_PER_DAY && State.TaskDay==State.Day; t++ )
		{
			const FTaskDef& T = GTasks[Clamp(State.Tasks[t*3],0,GNumTasks-1)];
			const INT Goal = Max( 1, State.Tasks[t*3+1] );
			const UBOOL bDone = (State.TasksDone & (1<<t))!=0;
			const INT Have = bDone ? Goal : Clamp( CounterValue(Harry,T.Counter) - State.Tasks[t*3+2], 0, Goal );
			const FRpgRect Row( Area.X, Y, Area.W, TaskH-4*S );
			Ui.Rect( Row, bDone ? FColor(20,46,32,255) : FColor(18,24,54,255) );
			Ui.Border( Row, Max(1.f,S), bDone ? RpgGreen : FColor(60,66,104,255) );
			Ui.Text( Ui.FontBody, Row.X+8*S, Row.Y+4*S, bDone ? RpgGreen : RpgParchment, *Ui.FitText(Ui.FontBody,*FString::Printf(T.Desc,Goal),Row.W*0.62f) );
			Ui.Text( Ui.FontSmall, Row.X+Row.W-8*S, Row.Y+5*S, RpgGoldBright, *FString::Printf(TEXT("+%i pts  +%i beans"), T.Points, T.Beans), RALIGN_Right );
			Ui.Bar( FRpgRect(Row.X+8*S, Row.Y+Ui.TextHeight(Ui.FontBody)+8*S, Row.W*0.62f, 5*S), (FLOAT)Have/Goal, bDone ? RpgGreen : FColor(120,190,255,255) );
			Ui.Text( Ui.FontSmall, Row.X+Row.W*0.62f+16*S, Row.Y+Ui.TextHeight(Ui.FontBody)+3*S, RpgMuted, bDone ? TEXT("Done!") : *FString::Printf(TEXT("%i / %i"), Have, Goal) );
			Y += TaskH;
		}
		Ui.Text( Ui.FontSmall, Area.X, Area.Y+Area.H-Ui.TextHeight(Ui.FontSmall), RpgMuted, TEXT("A new school day starts when you return to a hub after a while.") );
	}

	void DrawLettersTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.P(2.f);
		INT Count = 0;
		while( Count<MAX_INBOX && State.Inbox[Count]>0 )
			Count++;
		if( !Count )
		{
			Ui.TextWrapped( Ui.FontBody, Area, RpgMuted, TEXT("No letters yet. An owl arrives with every new school day.") );
			return;
		}
		const FLOAT ListW = Area.W*0.38f;
		const FLOAT RowH = Ui.TextHeight(Ui.FontBody) + Ui.TextHeight(Ui.FontSmall) + 8*S;
		const INT Visible = Max( 1, appFloor(Area.H/RowH) );
		ApplyWheel( Count, 1, Visible );
		ClampSelection( Count, 1, Visible );
		for( INT v=0; v<Visible && Scroll+v<Count; v++ )
		{
			const INT i = Scroll+v;
			const FLetterDef& L = GLetters[State.Inbox[i]-1];
			const FRpgRect Row( Area.X, Area.Y + v*RowH, ListW, RowH-2*S );
			if( Row.Y+Row.H > Area.Y+Area.H )
				break;
			if( RowActivated( Row, i, &State.Inbox[i] ) )
				State.LettersRead |= (1<<i);
			const UBOOL bRead = (State.LettersRead & (1<<i))!=0;
			Ui.Text( Ui.FontBody, Row.X+6*S, Row.Y+2*S, i==Sel ? RpgGoldBright : (bRead ? RpgParchment : FColor(255,255,255,255)), *Ui.FitText(Ui.FontBody,L.From,Row.W-12*S) );
			Ui.Text( Ui.FontSmall, Row.X+6*S, Row.Y+Ui.TextHeight(Ui.FontBody)+2*S, bRead ? RpgMuted : RpgGoldBright,
				*Ui.FitText(Ui.FontSmall,*((State.GiftsPending & (1<<i)) ? FString::Printf(TEXT("Gift!  %s"),L.Subject) : FString(L.Subject)),Row.W-12*S) );
		}
		if( Ui.Hovered( FRpgRect(Area.X+ListW, Area.Y, Area.W-ListW, Area.H), &State.LettersRead ) )
			State.LettersRead |= (1<<Sel);
		// The letter itself, on parchment.
		const FLetterDef& L = GLetters[State.Inbox[Sel]-1];
		const FRpgRect Paper( Area.X+ListW+12*S, Area.Y, Area.W-ListW-12*S, Area.H );
		Ui.Rect( Paper, FColor(228,214,180,255) );
		Ui.Border( Paper, Max(1.f,S*1.5f), FColor(150,120,70,255) );
		Ui.Text( Ui.FontItem, Paper.X+12*S, Paper.Y+8*S, FColor(90,50,20,255), *Ui.FitText(Ui.FontItem,L.Subject,Paper.W-24*S), RALIGN_Left, 0 );
		// A gift still in the envelope: open it when you like.
		const UBOOL bGift = (State.GiftsPending & (1<<Sel))!=0;
		const FLOAT GiftH = bGift ? Ui.TextHeight(Ui.FontBody)+Ui.P(16.f) : 0.f;
		if( bGift )
		{
			const FString Label = FString::Printf( TEXT("Open gift: %s"), *GiftText(L) );
			const FRpgRect GB( Paper.X+12*S, Paper.Y+Paper.H-GiftH-10*S, Min(Paper.W-24*S, Ui.TextWidth(Ui.FontBody,*Label)+Ui.P(40.f)), GiftH );
			if( Ui.Button( GB, *Label, 1, &State.GiftsPending ) )
			{
				State.GiftsPending &= ~(1<<Sel);
				GrantGift( Harry, L );
			}
		}
		const FRpgRect BodyR( Paper.X+12*S, Paper.Y+Ui.TextHeight(Ui.FontItem)+16*S, Paper.W-24*S, Paper.H-Ui.TextHeight(Ui.FontItem)-24*S-GiftH-(bGift ? 10*S : 0.f) );
		// Dark ink on parchment: draw without the drop shadow.
		FString Line, Word;
		FLOAT Y = BodyR.Y;
		const FLOAT LineH = Ui.TextHeight( Ui.FontBody );
		for( const TCHAR* P=L.Body; ; P++ )
		{
			const UBOOL bEnd = *P==0;
			if( bEnd || *P==' ' || *P=='\n' )
			{
				FString Candidate = Line.Len() ? Line + TEXT(" ") + Word : Word;
				if( Line.Len() && Ui.TextWidth(Ui.FontBody,*Candidate) > BodyR.W )
				{
					Ui.Text( Ui.FontBody, BodyR.X, Y, FColor(50,34,20,255), *Line, RALIGN_Left, 0 );
					Y += LineH;
					Line = Word;
				}
				else
					Line = Candidate;
				Word = TEXT("");
				if( *P=='\n' || bEnd )
				{
					Ui.Text( Ui.FontBody, BodyR.X, Y, FColor(50,34,20,255), *Line, RALIGN_Left, 0 );
					Y += LineH;
					Line = TEXT("");
				}
				if( bEnd || Y > BodyR.Y+BodyR.H-LineH )
					break;
			}
			else
			{
				TCHAR Ch[2] = { *P, 0 };
				Word += Ch;
			}
		}
	}

	void DrawStatusTab( APlayerPawn* Harry, const FRpgRect& Area )
	{
		const FLOAT S = Ui.P(2.f);
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
		// Difficulty.
		{
			Ui.Text( Ui.FontBody, Area.X, Y, RpgParchment, TEXT("Difficulty") );
			const FLOAT BW = (Area.W*0.62f)/DIFF_MAX;
			for( INT d=0; d<DIFF_MAX; d++ )
			{
				static const INT Order[DIFF_MAX] = { DIFF_Easy, DIFF_Normal, DIFF_Hard, DIFF_Expert };
				const INT Df = Order[d];
				const FRpgRect B( Area.X+Area.W*0.38f + d*BW, Y-2*S, BW-4*S, LineH+4*S );
				Ui.Tile( B, State.Difficulty==Df, Ui.Hovered(B,&GDifficultyNames[Df]), FColor(46,56,96,255) );
				Ui.Text( Ui.FontModern, B.X+B.W*0.5f, B.Y+(B.H-Ui.TextHeight(Ui.FontModern))*0.5f, State.Difficulty==Df ? RpgGoldBright : RpgParchment, GDifficultyNames[Df], RALIGN_Center, 0 );
				if( Ui.Clicked( B, &GDifficultyNames[Df] ) )
				{
					State.Difficulty = Df;
					Ui.Play( RSND_Click, Harry );
				}
			}
			Y += LineH + 6*S;
			Ui.Text( Ui.FontSmall, Area.X, Y, RpgMuted, *FString::Printf(TEXT("Damage taken %i%%, XP %+i%%"), GDifficultyDamage[State.Difficulty], GDifficultyXp[State.Difficulty]) );
			Y += Ui.TextHeight( Ui.FontSmall ) + 6*S;
		}
		FString Extra = FString::Printf( TEXT("School day %i"), State.Day );
		if( State.FelixSeconds>0 )
			Extra += FString::Printf( TEXT("   Felix Felicis %i:%02i"), State.FelixSeconds/60, State.FelixSeconds%60 );
		Ui.Text( Ui.FontBody, Area.X, Y, RpgParchment, *Extra );
	}

	/*-------------------------------------------------------------------------
		Input.
	-------------------------------------------------------------------------*/

	// True while an NPC speaks a line (the game shows its subtitle then).
	UBOOL IsTalking()
	{
		if( DialogLeft>0.f && DialogText.Len() )
			return 1;
		static FName DoingBumpLine( TEXT("DoingBumpLine") );
		if( RpgClock-LastTalkTime < 4.f )
			return 1;
		return NearNpc && IsInLevel(NearNpc) && GetStateName(NearNpc)==DoingBumpLine;
	}

	UBOOL CanCastNow( UGameEngine* Engine, APlayerPawn* Harry )
	{
		if( IsFreeGameplay(Engine,Harry) )
			return 1;
		if( !Harry || Harry->Health<=0 )
			return 0;
		ALevelInfo* Info = Engine->GLevel->GetLevelInfo();
		if( Info->Pauser.Len() || Info->LevelAction!=LEVACT_None )
			return 0;
		AActor* Hud = Harry->myHUD;
		if( Hud && (GetBoolProp(Hud,TEXT("bCutSceneMode")) || GetBoolProp(Hud,TEXT("bCutPopupMode"))) )
			return 0;
		if( GetBoolProp(Harry,TEXT("bIsCaptured")) || GetObjProp(Harry,TEXT("CurrVendorManager")) )
			return 0;
		UObject* Console = Harry->Player ? Harry->Player->Console : NULL;
		UObject* Book = GetObjProp( Console, TEXT("menuBook") );
		if( (Book && GetBoolProp(Book,TEXT("bIsOpen"))) || (Console && GetBoolProp(Console,TEXT("bTyping"))) )
			return 0;
		// Any other walking-type state (casting, sneaking, ...) is fine.
		const FString S = FString( *GetStateName( Harry ) ).Caps();
		return appStrstr( *S, TEXT("PLAYER") ) || appStrstr( *S, TEXT("CAST") ) || appStrstr( *S, TEXT("WALK") );
	}

	void TalkTo( APlayerPawn* Harry, AActor* Npc )
	{
		if( !Npc || RpgClock-LastTalkTime < 1.f )
			return;
		LastTalkTime = RpgClock;
		// Teachers start today's class; quest givers speak about their quest.
		if( !LessonTalk( Ui.Engine, Harry, Npc ) && !QuestTalk( Harry, Npc ) )
		{
			if( FStandIn* S = FindStandIn(Npc) )
			{
				Say( StandInDef(S->Def).Name, StandInDef(S->Def).Hello );
				return;
			}
			FRpgArg JustTalk = FRpgArg::I( 1 );
			CallScript( Npc, TEXT("DoBumpLine"), &JustTalk, 1 );
		}
		const FString Key = (LastLevel ? LastLevel->URL.Map : FString()) + TEXT(":") + Npc->GetName();
		if( TalkedToday.FindItemIndex(Key)==INDEX_NONE )
		{
			TalkedToday.AddItem( Key );
			State.Counters[CNT_Talks]++;
			GiveXp( 3, TEXT("new friend") );
			if( IsStudent(Npc) )
				AddRep( StudentHouse(Npc), 3 );
		}
	}

	void OpenShop( UGameEngine* Engine, AActor* Vendor, INT Def )
	{
		ShopVendor = Vendor;
		ShopVendorDef = Def;
		TryOn = -1;
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
			if( !Harry || Harry!=LastHarry )
				return 0;
			if( PhotoInput( Engine, Harry, Key, Action, Delta ) )
				return 1;
			if( AimInput( Engine, Harry, Key, Action ) )
				return 1;
			if( LessonInput( Engine, Harry, Key, Action ) )
				return 1;
			if( Key==IK_E && Action==IST_Press && !NearNpc && IsFreeGameplay(Engine,Harry) && BrewInput( Engine, Harry ) )
				return 1;
			if( Key==IK_F5 && Action==IST_Press )
			{
				ManualSave( Engine, Harry );
				return 1;
			}
			if( Key==IK_P && Action==IST_Press && IsFreeGameplay(Engine,Harry) )
			{
				SetPhoto( Engine, Harry, 1 );
				return 1;
			}
			if( Action!=IST_Press )
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
				{
					LogNearby( Engine, Harry );
					AddFeed( TEXT("No vendor nearby"), FColor(180,180,200,255), TOAST_Info, TEXT("Look for [B] Trade near a vendor") );
				}
				return 1;
			}
			// Number keys pick a spell (4-9, then 0); casting is aimed with the mouse.
			if( (Key>=IK_4 && Key<=IK_9) || Key==IK_0 )
			{
				const INT i = Key==IK_0 ? 6 : Key-IK_4;
				if( i<SP2_MAX && CanCastNow(Engine,Harry) )
				{
					if( bSelOurs && SelSpell==i )
						SelectWand();
					else
						SelectSpell( i );
					return 1;
				}
				return 0;
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
			if( BookTab==BOOK_Journal ) JournalSection = (JournalSection+1) % 3;
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

