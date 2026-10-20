/*=============================================================================
	HP2RpgUi.cpp: Immediate-mode menu toolkit for the RPG layer.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnSkeletalMesh.h"
#include "HP2RpgUi.h"

const FColor RpgGold( 201, 162, 74, 255 );
const FColor RpgGoldBright( 255, 222, 130, 255 );
const FColor RpgParchment( 236, 228, 206, 255 );
const FColor RpgMuted( 160, 164, 196, 255 );
const FColor RpgRed( 230, 80, 72, 255 );
const FColor RpgGreen( 120, 220, 140, 255 );
const FColor RpgTileFill( 16, 24, 58, 255 );
const FColor RpgTileFillHover( 28, 40, 92, 255 );
const FColor RpgTileFillSelected( 40, 52, 112, 255 );

static const TCHAR* GSoundPaths[RSND_MAX][3] =
{
	{ TEXT("HPSounds.menu_sfx.GUI_Esc_Rollover1"), TEXT("HPSounds.menu_sfx.gui_rollover1"), NULL },
	{ TEXT("HPSounds.menu_sfx.GUI_Esc_Click1"), TEXT("HPSounds.menu_sfx.menu_click"), NULL },
	{ TEXT("HPSounds.menu_sfx.ss_gui_rotatebut_0002"), TEXT("HPSounds.menu_sfx.GUI_Esc_Click2"), NULL },
	{ TEXT("HPSounds.Magic_sfx.spawn_bean01"), TEXT("HPSounds.menu_sfx.add_item_to_HUD"), NULL },
	{ TEXT("HPSounds.Music_Events.sm_bur_PlayfulFail_01"), TEXT("HPSounds.menu_sfx.GUI_Esc_Click5"), NULL },
	{ TEXT("HPSounds.Music_Events.sm_dia_DiagonReward_01"), TEXT("HPSounds.menu_sfx.score_tally_up"), NULL },
	{ TEXT("HPSounds.menu_sfx.Pop_up_in-game"), TEXT("HPSounds.menu_sfx.GUI_Esc_Click3"), NULL },
};

FRpgUi::FRpgUi()
{
	appMemzero( this, sizeof(*this) );
	S = 1.f;
}

UObject* FRpgUi::LoadRooted( UClass* Class, const TCHAR* Path )
{
	UObject* Obj = UObject::StaticLoadObject( Class, NULL, Path, NULL, LOAD_NoWarn|LOAD_Quiet, NULL );
	if( !Obj )
	{
		// Fall back to the bare object name in any loaded package.
		const TCHAR* Dot = Path;
		for( const TCHAR* P=Path; *P; P++ )
			if( *P=='.' )
				Dot = P+1;
		Obj = UObject::StaticFindObject( Class, ANY_PACKAGE, Dot );
	}
	if( Obj )
		Obj->AddToRoot();
	return Obj;
}

void FRpgUi::Resolve()
{
	if( bResolved )
		return;
	bResolved = 1;
	White = LoadTexture( TEXT("UWindow.WhiteTexture") );
	for( INT i=0; i<6; i++ )
	{
		SkyTiles[i] = LoadTexture( *FString::Printf(TEXT("HGame.Icons.FEBack1Texture%i"), i+1) );
	}
	GlowGold = LoadTexture( TEXT("HGame.Icons.CardGlowGold") );
	GlowSilver = LoadTexture( TEXT("HGame.Icons.CardGlowSilver") );
	FontTitle = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.HugeInkFont") );
	FontItem = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.BigInkFont") );
	FontBody = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.MedInkFont") );
	FontSmall = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.SmallInkFont") );
	for( INT i=0; i<RSND_MAX; i++ )
		for( INT p=0; p<3 && GSoundPaths[i][p] && !Sounds[i]; p++ )
			Sounds[i] = (USound*)LoadRooted( USound::StaticClass(), GSoundPaths[i][p] );
}

void FRpgUi::Begin( UGameEngine* InEngine, UViewport* InViewport, UCanvas* InCanvas, UBOOL bMenuOpen )
{
	Resolve();
	Engine = InEngine;
	Viewport = InViewport;
	C = InCanvas;
	S = Max( 1.f, C->ClipY/480.f );
	if( !FontTitle ) FontTitle = C->BigFont;
	if( !FontItem )  FontItem = C->MedFont;
	if( !FontBody )  FontBody = C->MedFont;
	if( !FontSmall ) FontSmall = C->SmallFont;
	bMouseActive = bMenuOpen && Viewport && Viewport->bShowWindowsMouse;
	MouseX = Viewport ? Viewport->WindowsMouseX : -1.f;
	MouseY = Viewport ? Viewport->WindowsMouseY : -1.f;
	bClickUsed = 0;
}

void FRpgUi::End()
{
	// A click that no widget consumed is dropped so it can't fire later.
	bClickPending = 0;
	WheelDelta = 0;
}

void FRpgUi::SetMenuMouse( UViewport* InViewport, UBOOL bShow )
{
	if( InViewport )
		InViewport->bShowWindowsMouse = bShow ? 1 : 0;
	if( !bShow )
	{
		bClickPending = 0;
		HoverId = NULL;
	}
}

/*-----------------------------------------------------------------------------
	Primitives.
-----------------------------------------------------------------------------*/

void FRpgUi::Rect( const FRpgRect& R, FColor Color )
{
	if( !White || R.W<=0 || R.H<=0 )
		return;
	C->DrawTile( White, R.X, R.Y, R.W, R.H, 0, 0, 1, 1, NULL, 1.f,
		FPlane(Color.R/255.f,Color.G/255.f,Color.B/255.f,1.f), FPlane(0,0,0,0), PF_NoSmooth );
}

void FRpgUi::Border( const FRpgRect& R, FLOAT T, FColor Color )
{
	Rect( FRpgRect(R.X, R.Y, R.W, T), Color );
	Rect( FRpgRect(R.X, R.Y+R.H-T, R.W, T), Color );
	Rect( FRpgRect(R.X, R.Y, T, R.H), Color );
	Rect( FRpgRect(R.X+R.W-T, R.Y, T, R.H), Color );
}

void FRpgUi::Glow( UTexture* Tex, const FRpgRect& R, FLOAT Strength )
{
	if( !Tex )
		return;
	C->DrawTile( Tex, R.X, R.Y, R.W, R.H, 0, 0, Tex->USize, Tex->VSize, NULL, 1.f,
		FPlane(Strength,Strength,Strength,1.f), FPlane(0,0,0,0), PF_Translucent );
}

void FRpgUi::Icon( UTexture* Tex, const FRpgRect& R, FLOAT Bright, UBOOL bMasked )
{
	if( !Tex )
		return;
	C->DrawTile( Tex, R.X, R.Y, R.W, R.H, 0, 0, Tex->USize, Tex->VSize, NULL, 1.f,
		FPlane(Bright,Bright,Bright,1.f), FPlane(0,0,0,0), bMasked ? PF_Masked : 0 );
}

// Draws a 3x2 tile set that covers 640x480 of a 768x512 canvas into R.
static void DrawTileSet( UCanvas* C, UTexture* const* Tiles, const FRpgRect& R )
{
	for( INT i=0; i<6; i++ )
	{
		UTexture* Tex = Tiles[i];
		if( !Tex )
			continue;
		const INT Col = i%3, Row = i/3;
		const FLOAT SrcX = Col*256.f, SrcY = Row*256.f;
		const FLOAT SrcW = Min( 256.f, 640.f-SrcX ), SrcH = Min( 256.f, 480.f-SrcY );
		const FLOAT ScaleU = Tex->USize/256.f, ScaleV = Tex->VSize/256.f;
		C->DrawTile( Tex,
			R.X + SrcX/640.f*R.W, R.Y + SrcY/480.f*R.H, SrcW/640.f*R.W, SrcH/480.f*R.H,
			0, 0, SrcW*ScaleU, SrcH*ScaleV, NULL, 1.f, FPlane(1,1,1,1), FPlane(0,0,0,0), 0 );
	}
}

void FRpgUi::Sky( const FRpgRect& R )
{
	if( SkyTiles[0] )
		DrawTileSet( C, SkyTiles, R );
	else
		Rect( R, FColor(13,20,48,255) );
}

FLOAT FRpgUi::TextWidth( UFont* Font, const TCHAR* Text )
{
	if( !Font || !Text || !*Text )
		return 0.f;
	INT XL=0, YL=0;
	C->CurX = 0.f;
	C->CurY = 0.f;
	C->WrappedStrLenf( Font, XL, YL, TEXT("%s"), Text );
	return XL;
}

FLOAT FRpgUi::TextHeight( UFont* Font )
{
	if( !Font )
		return 12.f*S;
	INT XL=0, YL=0;
	C->CurX = 0.f;
	C->CurY = 0.f;
	C->WrappedStrLenf( Font, XL, YL, TEXT("Ag") );
	return YL>0 ? (FLOAT)YL : 12.f*S;
}

void FRpgUi::Text( UFont* Font, FLOAT X, FLOAT Y, FColor Color, const TCHAR* Str, INT Align, UBOOL bShadow )
{
	if( !Font || !Str || !*Str )
		return;
	if( Align==RALIGN_Center )
		X -= TextWidth( Font, Str )*0.5f;
	else if( Align==RALIGN_Right )
		X -= TextWidth( Font, Str );
	const FLOAT SavedClipX = C->ClipX;
	C->ClipX = C->X;
	if( bShadow )
	{
		C->CurX = X + Max(1.f,S);
		C->CurY = Y + Max(1.f,S);
		C->Color = FColor( 0, 0, 0, Color.A );
		C->WrappedPrintf( Font, 0, TEXT("%s"), Str );
	}
	C->CurX = X;
	C->CurY = Y;
	C->Color = Color;
	C->WrappedPrintf( Font, 0, TEXT("%s"), Str );
	C->ClipX = SavedClipX;
}

void FRpgUi::TextWrapped( UFont* Font, const FRpgRect& R, FColor Color, const TCHAR* Str )
{
	if( !Font || !Str || !*Str )
		return;
	// Word wrap by hand so it works with both bitmap and native text.
	FString Line, Word;
	FLOAT Y = R.Y;
	const FLOAT LineH = TextHeight( Font );
	const TCHAR* P = Str;
	while( 1 )
	{
		const UBOOL bEnd = (*P==0);
		if( bEnd || *P==' ' || *P=='\n' )
		{
			FString Candidate = Line.Len() ? Line + TEXT(" ") + Word : Word;
			if( Line.Len() && TextWidth(Font,*Candidate) > R.W )
			{
				Text( Font, R.X, Y, Color, *Line );
				Y += LineH;
				Line = Word;
			}
			else
				Line = Candidate;
			Word = TEXT("");
			if( *P=='\n' )
			{
				Text( Font, R.X, Y, Color, *Line );
				Y += LineH;
				Line = TEXT("");
			}
			if( bEnd || Y > R.Y+R.H-LineH*0.5f )
				break;
		}
		else
		{
			TCHAR Ch[2] = { *P, 0 };
			Word += Ch;
		}
		P++;
	}
	if( Line.Len() && Y <= R.Y+R.H-LineH*0.5f )
		Text( Font, R.X, Y, Color, *Line );
}

/*-----------------------------------------------------------------------------
	Widgets.
-----------------------------------------------------------------------------*/

FRpgRect FRpgUi::Screen( FRpgRect& Left, FRpgRect& Right )
{
	// Full-screen night sky, split like the in-game wardrobe: a 3D stage on
	// the left (45%) and the menu column on the right.
	const FRpgRect Full( 0, 0, C->ClipX, C->ClipY );
	Sky( Full );
	// Keep the layout 4:3 and centred on wide screens.
	FLOAT W = Min( Full.W, Full.H*4.f/3.f );
	const FRpgRect Area( (Full.W-W)*0.5f + 16*S, 12*S, W - 32*S, Full.H - 24*S );
	Left = FRpgRect( Area.X, Area.Y, Area.W*0.44f, Area.H );
	Right = FRpgRect( Area.X + Area.W*0.47f, Area.Y, Area.W*0.53f, Area.H );
	return Area;
}

UBOOL FRpgUi::Hovered( const FRpgRect& R, const void* Id )
{
	if( !bMouseActive || !R.Contains(MouseX,MouseY) )
		return 0;
	if( HoverId!=Id )
	{
		HoverId = Id;
		Play( RSND_Hover, Viewport ? Viewport->Actor : NULL );
	}
	return 1;
}

UBOOL FRpgUi::Clicked( const FRpgRect& R, const void* Id )
{
	if( !Hovered(R,Id) || !bClickPending || bClickUsed )
		return 0;
	bClickUsed = 1;
	bClickPending = 0;
	return 1;
}

UBOOL FRpgUi::Button( const FRpgRect& R, const TCHAR* Label, UBOOL bEnabled, const void* Id )
{
	const UBOOL bHover = bEnabled && Hovered( R, Id );
	Rect( R, bHover ? FColor(70,56,20,255) : FColor(34,30,22,255) );
	Border( R, Max(1.f,S), bEnabled ? (bHover ? RpgGoldBright : RpgGold) : FColor(90,84,70,255) );
	if( bHover && GlowGold )
		Glow( GlowGold, R.Inset(-4*S), 0.35f );
	Text( FontBody, R.X+R.W*0.5f, R.Y+(R.H-TextHeight(FontBody))*0.5f, bEnabled ? RpgGoldBright : RpgMuted, Label, RALIGN_Center );
	return bEnabled && Clicked( R, Id );
}

UBOOL FRpgUi::Tab( const FRpgRect& R, const TCHAR* Label, UBOOL bActive, const void* Id )
{
	// Text tabs with a gold underline on the active one, like the in-game menus.
	const UBOOL bHover = Hovered( R, Id );
	Text( FontItem, R.X+R.W*0.5f, R.Y+(R.H-TextHeight(FontItem))*0.5f, bActive ? RpgGoldBright : (bHover ? RpgParchment : RpgMuted), Label, RALIGN_Center );
	if( bActive )
	{
		const FLOAT LW = Min( R.W*0.9f, TextWidth(FontItem,Label)+6*S );
		Rect( FRpgRect(R.X+(R.W-LW)*0.5f, R.Y+R.H-3*S, LW, 2*S), RpgGoldBright );
	}
	return Clicked( R, Id );
}

void FRpgUi::Tile( const FRpgRect& R, UBOOL bSelected, UBOOL bHover, FColor Accent )
{
	Rect( R, bSelected ? RpgTileFillSelected : (bHover ? RpgTileFillHover : RpgTileFill) );
	Border( R, Max(1.f,S*(bSelected?2.f:1.f)), bSelected ? RpgParchment : Accent );
	if( bSelected && GlowGold )
		Glow( GlowGold, R.Inset(-6*S), 0.45f );
}

void FRpgUi::Pips( FLOAT X, FLOAT Y, INT Filled, INT Total, FLOAT Size )
{
	for( INT p=0; p<Total; p++ )
	{
		const FRpgRect R( X + p*(Size+4*S), Y, Size, Size );
		Rect( R, p<Filled ? FColor(226,184,70,255) : FColor(24,30,64,255) );
		Border( R, Max(1.f,S*0.75f), FColor(150,128,64,255) );
	}
}

void FRpgUi::Bar( const FRpgRect& R, FLOAT Pct, FColor Fill )
{
	Rect( R, FColor(20,22,40,255) );
	Rect( FRpgRect(R.X, R.Y, R.W*Clamp(Pct,0.f,1.f), R.H), Fill );
	Border( R, Max(1.f,S*0.5f), FColor(120,104,60,255) );
}

/*-----------------------------------------------------------------------------
	3D previews.
-----------------------------------------------------------------------------*/

void FRpgUi::PlaceForPreview( AActor* Actor, FLOAT FovDeg, FLOAT Yaw, FLOAT Zoom )
{
	// The preview camera sits at the origin looking down +X with Z up.
	if( !Actor || !Actor->Mesh )
		return;
	const FSphere& Sphere = Actor->Mesh->BoundingSphere;
	const FLOAT Radius = Max( 4.f, Sphere.W*Actor->DrawScale );
	const FLOAT HalfFov = Clamp( FovDeg, 10.f, 120.f )*0.5f*PI/180.f;
	const FLOAT Dist = Max( 2.f, Radius/(FLOAT)appTan(HalfFov)*1.1f*Zoom );
	Actor->Location = FVector( Dist, -Sphere.Y*Actor->DrawScale, -Sphere.Z*Actor->DrawScale );
	Actor->Rotation = FRotator( 0, 32768 + appRound(Yaw), 0 );
}

void FRpgUi::DrawActorInRect( AActor* Actor, const FRpgRect& R, FLOAT FovDeg )
{
	FSceneNode* Frame = C->Frame;
	if( !Frame || !C->Render || !Actor || !Actor->Mesh || !Viewport || !Viewport->Actor || !Viewport->RenDev )
		return;
	APlayerPawn* Player = Viewport->Actor;
	const INT OldX = Frame->X, OldY = Frame->Y, OldXB = Frame->XB, OldYB = Frame->YB;
	const FCoords OldCoords = Frame->Coords, OldUncoords = Frame->Uncoords;
	const FLOAT OldFov = Player->FovAngle;
	const INT OldRendMap = Player->RendMap;

	Frame->X = Max( 1, appRound(R.W) );
	Frame->Y = Max( 1, appRound(R.H) );
	Frame->XB = appRound( C->OrgX + R.X );
	Frame->YB = appRound( C->OrgY + R.Y );
	// XOpenGL only re-issues the viewport when FOV or size change, so make
	// every preview's FOV unique within the frame.
	Player->FovAngle = FovDeg + (++PreviewNonce % 64)*0.001f;
	Player->RendMap = REN_DynLight;
	FVector Loc( 0, 0, 0 );
	FRotator Rot( 0, 0, 0 );
	Frame->ComputeRenderCoords( Loc, Rot );
	Viewport->RenDev->ClearZ( Frame );

	const UBOOL OldHidden = Actor->bHidden;
	Actor->bHidden = 0;
	C->Render->DrawActor( Frame, Actor );
	Actor->bHidden = OldHidden;

	Player->FovAngle = OldFov;
	Player->RendMap = OldRendMap;
	Frame->X = OldX;
	Frame->Y = OldY;
	Frame->XB = OldXB;
	Frame->YB = OldYB;
	Frame->Coords = OldCoords;
	Frame->Uncoords = OldUncoords;
	Frame->ComputeRenderSize();
}

/*-----------------------------------------------------------------------------
	Sound.
-----------------------------------------------------------------------------*/

void FRpgUi::Play( INT Sound, AActor* Listener )
{
	if( Sound<0 || Sound>=RSND_MAX || !Sounds[Sound] || !Engine || !Engine->Audio || !Listener )
		return;
	Engine->Audio->PlaySound( Listener, (INT)Listener->GetIndex()*16 + SLOT_Interface*2, Sounds[Sound], Listener->Location,
		0.8f, GAudioDefaultRadius, 1.f, SF_No3D, 0.f );
}
