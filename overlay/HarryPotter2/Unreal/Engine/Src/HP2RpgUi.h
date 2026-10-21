/*=============================================================================
	HP2RpgUi.h: Immediate-mode menu toolkit for the RPG layer.

	Draws with the game's own menu art (HGame.Icons.FEInGameBackTexture*,
	HP2_Menu.Icons.*), Ink fonts and menu sounds, supports mouse hover and
	clicks, and renders live 3D actors into screen rectangles.
	Internal to Engine/Src; include after EnginePrivate.h.
=============================================================================*/

#ifndef HP2RPGUI_H
#define HP2RPGUI_H

struct FRpgRect
{
	FLOAT X, Y, W, H;
	FRpgRect() : X(0), Y(0), W(0), H(0) {}
	FRpgRect( FLOAT InX, FLOAT InY, FLOAT InW, FLOAT InH ) : X(InX), Y(InY), W(InW), H(InH) {}
	UBOOL Contains( FLOAT PX, FLOAT PY ) const { return PX>=X && PY>=Y && PX<X+W && PY<Y+H; }
	FRpgRect Inset( FLOAT D ) const { return FRpgRect( X+D, Y+D, W-2*D, H-2*D ); }
};

enum ERpgAlign { RALIGN_Left, RALIGN_Center, RALIGN_Right };

enum ERpgSound { RSND_Hover, RSND_Click, RSND_Tab, RSND_Buy, RSND_Deny, RSND_Reward, RSND_Open, RSND_MAX };

// Colours shared by every menu.
extern const FColor RpgGold;
extern const FColor RpgGoldBright;
extern const FColor RpgParchment;
extern const FColor RpgMuted;
extern const FColor RpgRed;
extern const FColor RpgGreen;
extern const FColor RpgTileFill;
extern const FColor RpgTileFillHover;
extern const FColor RpgTileFillSelected;

class FRpgUi
{
public:
	// Frame state (valid between Begin and End).
	UGameEngine* Engine;
	UViewport* Viewport;
	UCanvas* C;
	FLOAT S;                 // UI scale (1 at 640x480)
	FLOAT MouseX, MouseY;
	UBOOL bMouseActive;      // the OS cursor is shown for an open menu
	UBOOL bClickPending;     // a left click arrived since the last frame
	UBOOL bClickUsed;
	INT WheelDelta;
	INT PreviewNonce;
	INT PreviewLogsLeft;     // diagnostics: log the first previews
	TArray<FString> DiagLines; // flushed into HP2Rpg.log by the mod
	const void* HoverId;     // widget hovered last frame, for rollover sounds

	// Assets.
	UBOOL bResolved;
	UTexture* White;
	UTexture* SkyTiles[6];
	UTexture* GlowGold;
	UTexture* GlowSilver;
	UFont* FontTitle;
	UFont* FontItem;
	UFont* FontBody;
	UFont* FontSmall;
	UFont* FontModern;      // Avenir Next via native text, Ink font fallback
	UFont* FontModernBold;
	UFont* FontModernBig;
	USound* Sounds[RSND_MAX];

	FRpgUi();

	void Resolve();
	void Begin( UGameEngine* InEngine, UViewport* InViewport, UCanvas* InCanvas, UBOOL bMenuOpen );
	void End();
	void SetMenuMouse( UViewport* InViewport, UBOOL bShow );

	// Primitives.
	void Rect( const FRpgRect& R, FColor Color );
	void Border( const FRpgRect& R, FLOAT Thickness, FColor Color );
	void Glow( UTexture* Tex, const FRpgRect& R, FLOAT Strength );
	void Icon( UTexture* Tex, const FRpgRect& R, FLOAT Bright=1.f, UBOOL bMasked=1 );
	void Sky( const FRpgRect& R );
	FLOAT TextWidth( UFont* Font, const TCHAR* Text );
	FLOAT TextHeight( UFont* Font );
	void Text( UFont* Font, FLOAT X, FLOAT Y, FColor Color, const TCHAR* Str, INT Align=RALIGN_Left, UBOOL bShadow=1 );
	void TextWrapped( UFont* Font, const FRpgRect& R, FColor Color, const TCHAR* Str );

	// Widgets.
	// Full-screen sky; returns the 4:3 work area and its left/right columns.
	FRpgRect Screen( FRpgRect& Left, FRpgRect& Right );
	UBOOL Hovered( const FRpgRect& R, const void* Id );
	UBOOL Clicked( const FRpgRect& R, const void* Id );
	UBOOL Button( const FRpgRect& R, const TCHAR* Label, UBOOL bEnabled, const void* Id );
	UBOOL Tab( const FRpgRect& R, const TCHAR* Label, UBOOL bActive, const void* Id, UFont* Font=NULL );
	void Footer( const TCHAR* Hint );
	FString FitText( UFont* Font, const TCHAR* Str, FLOAT MaxW );
	UFont* FontToFit( const TCHAR* const* Labels, INT Count, FLOAT MaxW, FLOAT Pad );
	void Tile( const FRpgRect& R, UBOOL bSelected, UBOOL bHover, FColor Accent );
	void TileLabel( const FRpgRect& R, const TCHAR* Label, FColor Color );
	void Pips( FLOAT X, FLOAT Y, INT Filled, INT Total, FLOAT Size );
	void Bar( const FRpgRect& R, FLOAT Pct, FColor Fill );

	// Modern overlay kit (dark glass, gold hairlines). Sizes are given in
	// 1080p pixels through P().
	FLOAT P( FLOAT Px1080 ) const { return Px1080*Unit; }
	FLOAT Unit;              // pixels per design pixel, from the body text height
	void Scrim( const FRpgRect& R, FLOAT Keep );              // darken what is behind
	void Glass( const FRpgRect& R, UBOOL bAccent=0 );          // scrim + tint + hairline
	void Panel( const FRpgRect& R );                          // framed menu section
	void Chamfer( const FRpgRect& R, FLOAT Cut, FColor Color ); // "rounded" filled rect
	void Brackets( const FRpgRect& R, FLOAT Len, FLOAT Thick, FColor Color );
	FLOAT Keycap( FLOAT X, FLOAT Y, const TCHAR* Key, UBOOL bDraw=1 ); // returns width
	// "[Key] Verb   [Key] Verb" in one glass pill centred on CX; returns its rect.
	FRpgRect Prompt( FLOAT CX, FLOAT Y, const TCHAR* const* Keys, const TCHAR* const* Verbs, INT Count, const TCHAR* Lead=NULL );
	UFont* FontCaption;     // Avenir Next Medium, small
	UFont* FontInk;         // the game's ink lettering: page titles
	UFont* FontInkMed;      // ink lettering: item names and headings
	UFont* FontNum;         // Avenir Next Bold, numbers

	// 3D.
	UBOOL ScreenToWorld( FLOAT SX, FLOAT SY, FLOAT Depth, FVector& Out );
	UBOOL PlaceInRect( AActor* Actor, const FRpgRect& R, FLOAT Yaw, FLOAT Zoom=1.f );
	void FocusPoint( AActor* Actor, const FVector& World, FLOAT SX, FLOAT SY );
	void DrawActorInRect( AActor* Actor, UBOOL bClearZ=1 );
	// Restricts 3D drawing to R: a sub-frame whose viewport is R, so models
	// never spill out of their tile. Coordinates stay in canvas pixels.
	UBOOL BeginClip( const FRpgRect& R );
	void EndClip();
	FSceneNode* View() { return bClipping ? &ClipFrame : (C ? C->Frame : NULL); }
	FSceneNode ClipFrame;
	FRpgRect ClipRect;
	UBOOL bClipping;

	// Sound.
	void Play( INT Sound, AActor* Listener );

	// Asset loading (rooted so caches survive level changes).
	static UObject* LoadRooted( UClass* Class, const TCHAR* Path );
	static UFont* MakeNativeFont( UFont* Fallback, const TCHAR* FaceName, INT Height );
	static UTexture* LoadTexture( const TCHAR* Path ) { return (UTexture*)LoadRooted( UTexture::StaticClass(), Path ); }
};

// Clips 3D drawing to a rectangle for the lifetime of the scope.
struct FRpgClipScope
{
	FRpgUi& Ui;
	FRpgClipScope( FRpgUi& InUi, const FRpgRect& R ) : Ui(InUi) { Ui.BeginClip( R ); }
	~FRpgClipScope() { Ui.EndClip(); }
};


#endif
