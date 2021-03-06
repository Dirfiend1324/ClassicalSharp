#ifndef CC_OPTIONS_H
#define CC_OPTIONS_H
#include "String.h"
/* Manages loading and saving options.
   Copyright 2014-2017 ClassicalSharp | Licensed under BSD-3
*/

typedef enum FpsLimit_ {
	FpsLimit_VSync, FpsLimit_30FPS, FpsLimit_60FPS, FpsLimit_120FPS, FpsLimit_None, FpsLimit_Count,
} FpsLimit;
extern const UChar* FpsLimit_Names[FpsLimit_Count];

#define OPT_USE_MUSIC "usemusic"
#define OPT_USE_SOUND "usesound"
#define OPT_MUSIC_VOLUME "musicvolume"
#define OPT_SOUND_VOLUME "soundvolume"
#define OPT_FORCE_OPENAL "forceopenal"
#define OPT_FORCE_OLD_OPENGL "force-oldgl"

#define OPT_VIEW_DISTANCE "viewdist"
#define OPT_BLOCK_PHYSICS "singleplayerphysics"
#define OPT_NAMES_MODE "namesmode"
#define OPT_INVERT_MOUSE "invertmouse"
#define OPT_SENSITIVITY "mousesensitivity"
#define OPT_FPS_LIMIT "fpslimit"
#define OPT_DEFAULT_TEX_PACK "defaulttexpack"
#define OPT_AUTO_CLOSE_LAUNCHER "autocloselauncher"
#define OPT_VIEW_BOBBING "viewbobbing"
#define OPT_ENTITY_SHADOW "entityshadow"
#define OPT_RENDER_TYPE "normal"
#define OPT_SMOOTH_LIGHTING "gfx-smoothlighting"
#define OPT_MIPMAPS "gfx-mipmaps"
#define OPT_SURVIVAL_MODE "game-survival"
#define OPT_CHAT_LOGGING "chat-logging"
#define OPT_WINDOW_WIDTH "window-width"
#define OPT_WINDOW_HEIGHT "window-height"

#define OPT_HACKS_ENABLED "hacks-hacksenabled"
#define OPT_FIELD_OF_VIEW "hacks-fov"
#define OPT_SPEED_FACTOR "hacks-speedmultiplier"
#define OPT_JUMP_VELOCITY "hacks-jumpvelocity"
#define OPT_MODIFIABLE_LIQUIDS "hacks-liquidsbreakable"
#define OPT_PUSHBACK_PLACING "hacks-pushbackplacing"
#define OPT_NOCLIP_SLIDE "hacks-noclipslide"
#define OPT_CAMERA_CLIPPING "hacks-cameraclipping"
#define OPT_WOM_STYLE_HACKS "hacks-womstylehacks"
#define OPT_FULL_BLOCK_STEP "hacks-fullblockstep"

#define OPT_TAB_AUTOCOMPLETE "gui-tab-autocomplete"
#define OPT_SHOW_BLOCK_IN_HAND "gui-blockinhand"
#define OPT_CHATLINES "gui-chatlines"
#define OPT_CLICKABLE_CHAT "gui-chatclickable"
#define OPT_USE_CHAT_FONT "gui-arialchatfont"
#define OPT_HOTBAR_SCALE "gui-hotbarscale"
#define OPT_INVENTORY_SCALE "gui-inventoryscale"
#define OPT_CHAT_SCALE "gui-chatscale"
#define OPT_SHOW_FPS "gui-showfps"
#define OPT_FONT_NAME "gui-fontname"
#define OPT_BLACK_TEXT "gui-blacktextshadows"

#define OPT_CLASSIC_MODE "mode-classic"
#define OPT_CUSTOM_BLOCKS "nostalgia-customblocks"
#define OPT_CPE "nostalgia-usecpe"
#define OPT_SERVER_TEXTURES "nostalgia-servertextures"
#define OPT_CLASSIC_GUI "nostalgia-classicgui"
#define OPT_SIMPLE_ARMS_ANIM "nostalgia-simplearms"
#define OPT_CLASSIC_TABLIST "nostalgia-classictablist"
#define OPT_CLASSIC_OPTIONS "nostalgia-classicoptions"
#define OPT_CLASSIC_HACKS "nostalgia-hacks"
#define OPT_CLASSIC_ARM_MODEL "nostalgia-classicarm"
#define OPT_MAX_CHUNK_UPDATES "gfx-maxchunkupdates"

StringsBuffer Options_Keys;
StringsBuffer Options_Values;

bool Options_HasAnyChanged(void);
void Options_Free(void);

void Options_Get(const UChar* key, STRING_TRANSIENT String* value, const UChar* defValue);
Int32 Options_GetInt(const UChar* key, Int32 min, Int32 max, Int32 defValue);
bool Options_GetBool(const UChar* key, bool defValue);
Real32 Options_GetFloat(const UChar* key, Real32 min, Real32 max, Real32 defValue);
UInt32 Options_GetEnum(const UChar* key, UInt32 defValue, const UChar** names, UInt32 namesCount);

void Options_SetBool(const UChar* keyRaw, bool value);
void Options_SetInt32(const UChar* keyRaw, Int32 value);
void Options_Set(const UChar* keyRaw, STRING_PURE String* value);
void Options_Load(void);
void Options_Save(void);
#endif
