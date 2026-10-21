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

struct FRpgLoadCacheEntry
{
	UClass* Class;
	FString Path;
	UObject* Obj;
};
static TArray<FRpgLoadCacheEntry> GRpgLoadCache;

UObject* FRpgUi::LoadRooted( UClass* Class, const TCHAR* Path )
{
	// Cached, including failures, so per-frame menu drawing never re-searches.
	for( INT i=0; i<GRpgLoadCache.Num(); i++ )
		if( GRpgLoadCache(i).Class==Class && GRpgLoadCache(i).Path==Path )
			return GRpgLoadCache(i).Obj;
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
	FRpgLoadCacheEntry* Entry = new(GRpgLoadCache) FRpgLoadCacheEntry;
	Entry->Class = Class;
	Entry->Path = Path;
	Entry->Obj = Obj;
	return Obj;
}

// A font that native text renders with a macOS typeface; it copies a game
// font's glyph pages so bitmap-text mode still has something to draw.
UFont* FRpgUi::MakeNativeFont( UFont* Fallback, const TCHAR* FaceName, INT Height )
{
	if( !Fallback )
		return NULL;
	UFont* Font = ConstructObject<UFont>( UFont::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Transient );
	Font->CharactersPerPage = Fallback->CharactersPerPage;
	Font->Pages = Fallback->Pages;
	Font->CharRemap = Fallback->CharRemap;
	Font->IsRemapped = Fallback->IsRemapped;
	Font->FontName = FaceName;
	Font->FontHeight = Height;
	Font->AddToRoot();
	return Font;
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
	// One clean typeface (Avenir Next) at a small, calm scale. The game's ink
	// fonts only supply fallback glyph pages.
	UFont* InkBig = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.BigInkFont") );
	UFont* InkMed = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.MedInkFont") );
	UFont* InkSmall = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.SmallInkFont") );
	UFont* InkTiny = (UFont*)LoadRooted( UFont::StaticClass(), TEXT("HGame.TinyInkFont") );
	FontTitle = MakeNativeFont( InkBig, TEXT("AvenirNext-DemiBold"), 15 );
	FontItem = MakeNativeFont( InkMed, TEXT("AvenirNext-DemiBold"), 10 );
	FontBody = MakeNativeFont( InkSmall, TEXT("AvenirNext-Medium"), 9 );
	FontSmall = MakeNativeFont( InkTiny, TEXT("AvenirNext-Medium"), 8 );
	FontModern = MakeNativeFont( InkSmall, TEXT("AvenirNext-Medium"), 9 );
	FontModernBold = MakeNativeFont( InkSmall, TEXT("AvenirNext-DemiBold"), 9 );
	FontModernBig = MakeNativeFont( InkMed, TEXT("AvenirNext-Bold"), 15 );
	FontCaption = MakeNativeFont( InkTiny, TEXT("AvenirNext-Medium"), 8 );
	FontNum = MakeNativeFont( InkSmall, TEXT("AvenirNext-DemiBold"), 10 );
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
	if( !FontModern ) FontModern = FontBody;
	if( !FontModernBold ) FontModernBold = FontBody;
	// Design sizes assume 20 px body text; native text follows the display.
	{
		const FLOAT BodyH = TextHeight( FontModern );
		Unit = BodyH>0.f ? BodyH/20.f : S/2.25f;
	}
	if( !FontModernBig ) FontModernBig = FontTitle;
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

void FRpgUi::Scrim( const FRpgRect& R, FLOAT Keep )
{
	if( !White || R.W<=0 || R.H<=0 )
		return;
	C->DrawTile( White, R.X, R.Y, R.W, R.H, 0, 0, 1, 1, NULL, 1.f,
		FPlane(Keep,Keep,Keep*1.08f,1.f), FPlane(0,0,0,0), PF_Modulated|PF_NoSmooth );
}

// Flat dark panel (the renderer's blend modes can't darken cleanly).
void FRpgUi::Glass( const FRpgRect& R, UBOOL bAccent )
{
	Rect( R, FColor(12,16,34,255) );
	const FLOAT T = Max( 1.f, P(1.f) );
	Border( R, T, FColor(46,56,96,255) );
	if( bAccent )
		Rect( FRpgRect(R.X, R.Y, R.W, Max(1.f,P(2.f))), FColor(201,162,74,255) );
}

void FRpgUi::Chamfer( const FRpgRect& R, FLOAT Cut, FColor Color )
{
	Rect( FRpgRect(R.X+Cut, R.Y, R.W-2*Cut, R.H), Color );
	Rect( FRpgRect(R.X, R.Y+Cut, Cut, R.H-2*Cut), Color );
	Rect( FRpgRect(R.X+R.W-Cut, R.Y+Cut, Cut, R.H-2*Cut), Color );
}

void FRpgUi::Brackets( const FRpgRect& R, FLOAT Len, FLOAT T, FColor Color )
{
	Len = Min( Len, Min(R.W,R.H)*0.5f );
	Rect( FRpgRect(R.X, R.Y, Len, T), Color );                     Rect( FRpgRect(R.X, R.Y, T, Len), Color );
	Rect( FRpgRect(R.X+R.W-Len, R.Y, Len, T), Color );             Rect( FRpgRect(R.X+R.W-T, R.Y, T, Len), Color );
	Rect( FRpgRect(R.X, R.Y+R.H-T, Len, T), Color );               Rect( FRpgRect(R.X, R.Y+R.H-Len, T, Len), Color );
	Rect( FRpgRect(R.X+R.W-Len, R.Y+R.H-T, Len, T), Color );       Rect( FRpgRect(R.X+R.W-T, R.Y+R.H-Len, T, Len), Color );
}

FLOAT FRpgUi::Keycap( FLOAT X, FLOAT Y, const TCHAR* Key, UBOOL bDraw )
{
	UFont* F = FontCaption ? FontCaption : FontModern;
	const FLOAT H = P(26.f);
	const FLOAT W = Max( H, TextWidth(F,Key) + P(14.f) );
	if( bDraw )
	{
		const FRpgRect R( X, Y, W, H );
		Chamfer( R, P(3.f), FColor(46,56,96,255) );
		Chamfer( R.Inset(Max(1.f,P(1.f))), P(2.f), FColor(20,27,54,255) );
		Text( F, X+W*0.5f, Y+(H-TextHeight(F))*0.5f, FColor(240,238,230,255), Key, RALIGN_Center, 0 );
	}
	return W;
}

FRpgRect FRpgUi::Prompt( FLOAT CX, FLOAT Y, const TCHAR* const* Keys, const TCHAR* const* Verbs, INT Count, const TCHAR* Lead )
{
	UFont* F = FontModern;
	const FLOAT Pad = P(12.f), Gap = P(8.f), Sep = P(20.f), H = P(40.f);
	FLOAT W = Pad*2;
	if( Lead && *Lead )
		W += TextWidth( FontModernBold, Lead ) + Sep;
	for( INT i=0; i<Count; i++ )
		W += Keycap( 0, 0, Keys[i], 0 ) + Gap + TextWidth( F, Verbs[i] ) + (i<Count-1 ? Sep : 0.f);
	const FRpgRect R( CX-W*0.5f, Y, W, H );
	Glass( R );
	FLOAT X = R.X + Pad;
	if( Lead && *Lead )
	{
		Text( FontModernBold, X, Y+(H-TextHeight(FontModernBold))*0.5f, FColor(240,238,230,255), Lead, RALIGN_Left, 0 );
		X += TextWidth( FontModernBold, Lead ) + Sep;
	}
	for( INT i=0; i<Count; i++ )
	{
		X += Keycap( X, Y+(H-P(26.f))*0.5f, Keys[i] ) + Gap;
		Text( F, X, Y+(H-TextHeight(F))*0.5f, FColor(182,186,206,255), Verbs[i], RALIGN_Left, 0 );
		X += TextWidth( F, Verbs[i] ) + Sep;
	}
	return R;
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
		// Overlap the tiles by a pixel and sample one texel inside each edge,
		// so filtering never shows the green colour key as seams.
		C->DrawTile( Tex,
			R.X + SrcX/640.f*R.W, R.Y + SrcY/480.f*R.H, SrcW/640.f*R.W + 1.f, SrcH/480.f*R.H + 1.f,
			ScaleU, ScaleV, (SrcW-2.f)*ScaleU, (SrcH-2.f)*ScaleV, NULL, 1.f, FPlane(1,1,1,1), FPlane(0,0,0,0), 0 );
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
	// Use the width of a 16:10 screen, centred on wider ones.
	FLOAT W = Min( Full.W, Full.H*1.6f );
	const FLOAT FooterH = TextHeight( FontSmall ) + 10*S;
	const FRpgRect Area( (Full.W-W)*0.5f + 16*S, 12*S, W - 32*S, Full.H - 24*S - FooterH );
	Left = FRpgRect( Area.X, Area.Y, Area.W*0.38f, Area.H );
	Right = FRpgRect( Area.X + Area.W*0.41f, Area.Y, Area.W*0.59f, Area.H );
	return Area;
}

void FRpgUi::Footer( const TCHAR* Hint )
{
	Text( FontSmall, C->ClipX*0.5f, C->ClipY - TextHeight(FontSmall) - 6*S, RpgMuted, Hint, RALIGN_Center );
}

FString FRpgUi::FitText( UFont* Font, const TCHAR* Str, FLOAT MaxW )
{
	FString Out( Str );
	if( MaxW<=0.f || TextWidth(Font,*Out) <= MaxW )
		return Out;
	while( Out.Len()>1 && TextWidth(Font,*(Out+TEXT("..."))) > MaxW )
		Out = Out.Left( Out.Len()-1 );
	return Out + TEXT("...");
}

UFont* FRpgUi::FontToFit( const TCHAR* const* Labels, INT Count, FLOAT MaxW, FLOAT Pad )
{
	UFont* Fonts[4] = { FontItem, FontBody, FontSmall, C->SmallFont };
	for( INT f=0; f<4; f++ )
	{
		if( !Fonts[f] )
			continue;
		FLOAT Total = 0.f;
		for( INT i=0; i<Count; i++ )
			Total += TextWidth( Fonts[f], Labels[i] ) + Pad;
		if( Total <= MaxW )
			return Fonts[f];
	}
	return FontSmall;
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
	Text( FontBody, R.X+R.W*0.5f, R.Y+(R.H-TextHeight(FontBody))*0.5f, bEnabled ? RpgGoldBright : RpgMuted, Label, RALIGN_Center );
	return bEnabled && Clicked( R, Id );
}

UBOOL FRpgUi::Tab( const FRpgRect& R, const TCHAR* Label, UBOOL bActive, const void* Id, UFont* Font )
{
	// Text tabs with a gold underline on the active one, like the in-game menus.
	if( !Font )
		Font = FontItem;
	const UBOOL bHover = Hovered( R, Id );
	Text( Font, R.X+R.W*0.5f, R.Y+(R.H-TextHeight(Font))*0.5f, bActive ? RpgGoldBright : (bHover ? RpgParchment : RpgMuted), Label, RALIGN_Center );
	if( bActive )
	{
		const FLOAT LW = Min( R.W*0.9f, TextWidth(Font,Label)+6*S );
		Rect( FRpgRect(R.X+(R.W-LW)*0.5f, R.Y+R.H-3*S, LW, 2*S), RpgGoldBright );
	}
	return Clicked( R, Id );
}

// Flat tile: rarity/accent as a strip on top, gold border and corner
// brackets when selected.
void FRpgUi::Tile( const FRpgRect& R, UBOOL bSelected, UBOOL bHover, FColor Accent )
{
	Rect( R, bSelected ? FColor(32,42,82,255) : (bHover ? FColor(26,34,66,255) : FColor(20,27,54,255)) );
	Border( R, bSelected ? Max(1.f,P(2.f)) : Max(1.f,P(1.f)), bSelected ? FColor(255,222,130,255) : (bHover ? FColor(201,162,74,255) : FColor(46,56,96,255)) );
	Rect( FRpgRect(R.X+P(2.f), R.Y+P(2.f), R.W-P(4.f), Max(1.f,P(3.f))), Accent );
	if( bSelected )
		Brackets( FRpgRect(R.X-P(4.f), R.Y-P(4.f), R.W+P(8.f), R.H+P(8.f)), P(10.f), Max(1.f,P(2.f)), FColor(255,222,130,255) );
}

void FRpgUi::TileLabel( const FRpgRect& R, const TCHAR* Label, FColor Color )
{
	UFont* F = FontModern;
	const FLOAT H = TextHeight( F ) + P(8.f);
	const FRpgRect Band( R.X+P(2.f), R.Y+R.H-H-P(2.f), R.W-P(4.f), H );
	Rect( Band, FColor(12,16,34,255) );
	Text( F, Band.X+Band.W*0.5f, Band.Y+P(4.f), Color, *FitText(F,Label,Band.W-P(8.f)), RALIGN_Center, 0 );
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

// Previews are drawn the way the game draws pickups flying to the HUD
// (hprop.RenderHud -> Canvas.DrawActor): the actor is placed in the world just
// in front of the real camera, exactly behind the target rectangle, and drawn
// with the unchanged scene frame after clearing depth.

static const FLOAT GPreviewDepth = 120.f;

UBOOL FRpgUi::ScreenToWorld( FLOAT SX, FLOAT SY, FLOAT Depth, FVector& Out )
{
	FSceneNode* F = View();
	if( !F || F->RProj.Z==0.f )
		return 0;
	if( bClipping )
	{
		SX -= ClipRect.X;
		SY -= ClipRect.Y;
	}
	Out = F->Coords.Origin
		+ F->Coords.ZAxis*Depth
		+ F->Coords.XAxis*((SX - F->FX2)*Depth*F->RProj.Z)
		+ F->Coords.YAxis*((SY - F->FY2)*Depth*F->RProj.Z);
	return 1;
}

// World-space bounds of the actor's current render box.
static void ActorBounds( AActor* Actor, FVector& Lo, FVector& Hi )
{
	const FCoords Box = Actor->GetRenderBoundingBox( 1 );
	Lo = FVector( 1e9f, 1e9f, 1e9f );
	Hi = FVector( -1e9f, -1e9f, -1e9f );
	for( INT i=0; i<8; i++ )
	{
		const FVector P = Box.Origin
			+ ((i&1) ? Box.XAxis : FVector(0,0,0))
			+ ((i&2) ? Box.YAxis : FVector(0,0,0))
			+ ((i&4) ? Box.ZAxis : FVector(0,0,0));
		Lo.X = Min(Lo.X,P.X); Lo.Y = Min(Lo.Y,P.Y); Lo.Z = Min(Lo.Z,P.Z);
		Hi.X = Max(Hi.X,P.X); Hi.Y = Max(Hi.Y,P.Y); Hi.Z = Max(Hi.Z,P.Z);
	}
}

UBOOL FRpgUi::PlaceInRect( AActor* Actor, const FRpgRect& R, FLOAT Yaw, FLOAT Zoom )
{
	FSceneNode* F = View();
	if( !Actor || !Actor->Mesh || !F )
		return 0;
	// Face the camera.
	const FVector Fwd = F->Coords.ZAxis;
	const INT CamYaw = appRound( (FLOAT)appAtan2(Fwd.Y,Fwd.X)*32768.f/PI );
	Actor->Rotation = FRotator( 0, CamYaw + 32768 + appRound(Yaw), 0 );

	// Measure at scale 1 at the origin.
	Actor->DrawScale = 1.f;
	Actor->Location = FVector( 0, 0, 0 );
	FVector Lo, Hi;
	ActorBounds( Actor, Lo, Hi );
	const FVector Size = Hi - Lo;
	const FLOAT Height = Max( Size.Z, 1.f );
	const FLOAT Width = Max( Max(Size.X,Size.Y), 1.f );
	const FLOAT ToWorld = GPreviewDepth*F->RProj.Z;
	const FLOAT Scale = Min( R.H*0.86f*ToWorld/Height, R.W*0.86f*ToWorld/Width )*Zoom;
	const FVector Centre = (Lo+Hi)*0.5f*Scale;

	FVector Target;
	if( !ScreenToWorld( R.X + R.W*0.5f, R.Y + R.H*0.5f, GPreviewDepth, Target ) )
		return 0;
	Actor->DrawScale = Scale;
	Actor->Location = Target - Centre;
	if( PreviewLogsLeft>0 )
		DiagLines.AddItem( FString::Printf( TEXT("preview place %s mesh=%s size=(%.1f %.1f %.1f) scale=%.3f target=(%.0f %.0f %.0f) cam=(%.0f %.0f %.0f) fwd=(%.2f %.2f %.2f) rect=(%.0f %.0f %.0f %.0f) frame=%ix%i rprojz=%.4f"),
			Actor->GetName(), Actor->Mesh->GetName(), Size.X, Size.Y, Size.Z, Scale, Target.X, Target.Y, Target.Z,
			F->Coords.Origin.X, F->Coords.Origin.Y, F->Coords.Origin.Z, Fwd.X, Fwd.Y, Fwd.Z, R.X, R.Y, R.W, R.H, F->X, F->Y, F->RProj.Z ) );
	return 1;
}

// Shifts an already placed actor so World (a point on it) lands on the
// screen point (SX,SY).
void FRpgUi::FocusPoint( AActor* Actor, const FVector& World, FLOAT SX, FLOAT SY )
{
	FSceneNode* F = View();
	if( !F || !Actor )
		return;
	const FLOAT Depth = (World - F->Coords.Origin) | F->Coords.ZAxis;
	FVector Target;
	if( Depth>1.f && ScreenToWorld( SX, SY, Depth, Target ) )
		Actor->Location += Target - World;
}

UBOOL FRpgUi::BeginClip( const FRpgRect& R )
{
	FSceneNode* F = C ? C->Frame : NULL;
	if( !F || !Viewport || !Viewport->RenDev || R.W<4.f || R.H<4.f )
		return 0;
	ClipFrame = *F;
	ClipFrame.XB = F->XB + appRound( R.X );
	ClipFrame.YB = F->YB + appRound( R.Y );
	ClipFrame.X = appRound( R.W );
	ClipFrame.Y = appRound( R.H );
	ClipFrame.ComputeRenderSize();
	ClipRect = R;
	bClipping = 1;
	Viewport->RenDev->SetSceneNode( &ClipFrame );
	return 1;
}

void FRpgUi::EndClip()
{
	if( !bClipping )
		return;
	bClipping = 0;
	if( C && C->Frame && Viewport && Viewport->RenDev )
		Viewport->RenDev->SetSceneNode( C->Frame );
}

void FRpgUi::DrawActorInRect( AActor* Actor, UBOOL bClearZ )
{
	FSceneNode* F = View();
	if( !F || !C->Render || !Actor || !Actor->Mesh || !Viewport || !Viewport->RenDev )
		return;
	if( bClearZ )
		Viewport->RenDev->ClearZ( F );
	const UBOOL OldHidden = Actor->bHidden;
	Actor->bHidden = 0;
	if( PreviewLogsLeft>0 )
	{
		PreviewLogsLeft--;
		const FCoords Box = Actor->GetRenderBoundingBox( 1 );
		DiagLines.AddItem( FString::Printf( TEXT("preview draw %s loc=(%.0f %.0f %.0f) scale=%.3f box=(%.0f %.0f %.0f) axes=%.1f/%.1f/%.1f drawtype=%i style=%i physics=%i role=%i render=%s"),
			Actor->GetName(), Actor->Location.X, Actor->Location.Y, Actor->Location.Z, Actor->DrawScale,
			Box.Origin.X, Box.Origin.Y, Box.Origin.Z, Box.XAxis.Size(), Box.YAxis.Size(), Box.ZAxis.Size(),
			Actor->DrawType, Actor->Style, Actor->Physics, Actor->Role, C->Render->GetClass()->GetName() ) );
	}
	C->Render->DrawActor( F, Actor );
	Actor->bHidden = OldHidden;
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
