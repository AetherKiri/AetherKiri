//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "tTVPFont" definition
//---------------------------------------------------------------------------

#ifndef __TVPFONTSTRUC_H__
#define __TVPFONTSTRUC_H__

#include "tjsCommHead.h"

// Emoji rendering modes follow the krkrz script contract.  The default value
// keeps existing Aether games on the historical monochrome FreeType path;
// applications can opt into a registered mono/color emoji fallback through
// Font.emojiMode or Font.defaultEmojiMode.
#define TVP_EMOJI_DEFAULT (-1)
#define TVP_EMOJI_NONE 0
#define TVP_EMOJI_MONO 1
#define TVP_EMOJI_COLOR 2

// Explicit Unicode variation-selector presentation.  VS15/VS16 are kept out
// of glyph lookup and only affect which fallback face is preferred.
#define TVP_EMOJI_PRESENTATION_DEFAULT 0
#define TVP_EMOJI_PRESENTATION_EMOJI 1
#define TVP_EMOJI_PRESENTATION_TEXT 2
#define TVP_EMOJI_VS15 0xFE0E
#define TVP_EMOJI_VS16 0xFE0F

//---------------------------------------------------------------------------
// tTVPFont definition
//---------------------------------------------------------------------------
struct tTVPFont {
    tjs_int Height; // height of text
    tjs_uint32 Flags;
    tjs_int Angle; // rotation angle ( in tenths of degrees ) 0 ..
                   // 1800 .. 3600

    ttstr Face; // font name

    // Emoji fallback policy.  -1 follows Font.defaultEmojiMode; 0 preserves
    // the pre-emoji Aether behaviour.
    tjs_int EmojiMode = TVP_EMOJI_DEFAULT;

    // Variable-font controls.  -1 keeps the face's native/default weight;
    // an explicit value is mapped to the OpenType ``wght`` axis when the
    // selected face exposes one.  Variations is normalized by the Font TJS
    // property (for example ``wdth=87.5,wght=700``).  Keeping these fields in
    // the shared font value makes them part of the existing glyph-cache key,
    // so changing an axis can never reuse a glyph rendered at another axis.
    tjs_int Weight = -1;
    ttstr Variations;

    bool operator==(const tTVPFont &rhs) const {
        return Height == rhs.Height && Flags == rhs.Flags &&
            Angle == rhs.Angle && Face == rhs.Face &&
            EmojiMode == rhs.EmojiMode &&
            Weight == rhs.Weight && Variations == rhs.Variations;
    }
    bool operator!=(const tTVPFont &rhs) const { return !(operator==(rhs)); }
};

/*[*/
//---------------------------------------------------------------------------
// font ralated constants
//---------------------------------------------------------------------------
#define TVP_TF_ITALIC 0x0100
#define TVP_TF_BOLD 0x0200
#define TVP_TF_UNDERLINE 0x0400
#define TVP_TF_STRIKEOUT 0x0800
#define TVP_TF_FONTFILE 0x1000

//---------------------------------------------------------------------------
#define TVP_FSF_FIXEDPITCH 0x01 // fsfFixedPitch
#define TVP_FSF_SAMECHARSET 0x02 // fsfSameCharSet
#define TVP_FSF_NOVERTICAL 0x04 // fsfNoVertical
#define TVP_FSF_TRUETYPEONLY 0x08 // fsfTrueTypeOnly
#define TVP_FSF_IGNORESYMBOL 0x10 // fsfIgnoreSymbol
#define TVP_FSF_USEFONTFACE 0x100 // fsfUseFontFace

/*]*/

//---------------------------------------------------------------------------
#endif
