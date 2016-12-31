// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//		Refresh of things, i.e. objects represented by sprites.
//
// This file contains some code from the Build Engine.
//
// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

#include "p_lnspec.h"
#include "templates.h"
#include "doomdef.h"
#include "m_swap.h"
#include "i_system.h"
#include "w_wad.h"
#include "swrenderer/r_main.h"
#include "swrenderer/scene/r_things.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomstat.h"
#include "v_video.h"
#include "sc_man.h"
#include "s_sound.h"
#include "sbar.h"
#include "gi.h"
#include "r_sky.h"
#include "cmdlib.h"
#include "g_level.h"
#include "d_net.h"
#include "colormatcher.h"
#include "d_netinf.h"
#include "p_effect.h"
#include "r_bsp.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_3dfloors.h"
#include "swrenderer/drawers/r_draw_rgba.h"
#include "swrenderer/drawers/r_draw_pal.h"
#include "v_palette.h"
#include "r_data/r_translate.h"
#include "r_data/colormaps.h"
#include "r_data/voxels.h"
#include "p_local.h"
#include "p_maputl.h"
#include "r_voxel.h"
#include "r_draw_segment.h"
#include "r_portal.h"
#include "r_particle.h"
#include "r_playersprite.h"
#include "r_wallsprite.h"
#include "swrenderer/r_memory.h"

EXTERN_CVAR(Int, r_drawfuzz)
EXTERN_CVAR(Bool, r_drawvoxels)
EXTERN_CVAR(Bool, r_blendmethod)

CVAR(Bool, r_fullbrightignoresectorcolor, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Bool, r_splitsprites, true, CVAR_ARCHIVE)

namespace swrenderer
{
	using namespace drawerargs;

// [RH] A c-buffer. Used for keeping track of offscreen voxel spans.

struct FCoverageBuffer
{
	struct Span
	{
		Span *NextSpan;
		short Start, Stop;
	};

	FCoverageBuffer(int size);
	~FCoverageBuffer();

	void Clear();
	void InsertSpan(int listnum, int start, int stop);
	Span *AllocSpan();

	FMemArena SpanArena;
	Span **Spans;	// [0..NumLists-1] span lists
	Span *FreeSpans;
	unsigned int NumLists;
};

extern double globaluclip, globaldclip;

//
// Sprite rotation 0 is facing the viewer,
//	rotation 1 is one angle turn CLOCKWISE around the axis.
// This is not the same as the angle,
//	which increases counter clockwise (protractor).
//
double 			pspritexscale;
double	 		pspritexiscale;
double			pspriteyscale;
fixed_t			sky1scale;			// [RH] Sky 1 scale factor
fixed_t			sky2scale;			// [RH] Sky 2 scale factor

int		spriteshade;

FTexture		*WallSpriteTile;

// constant arrays
//	used for psprite clipping and initializing clipping
short			zeroarray[MAXWIDTH];
short			screenheightarray[MAXWIDTH];


//
// INITIALIZATION FUNCTIONS
//

int OffscreenBufferWidth, OffscreenBufferHeight;
BYTE *OffscreenColorBuffer;
FCoverageBuffer *OffscreenCoverageBuffer;

//

// GAME FUNCTIONS
//
bool			DrewAVoxel;

static vissprite_t **spritesorter;
static int spritesortersize = 0;
static int vsprcount;




void R_DeinitSprites()
{
	R_DeinitVisSprites();

	// Free vissprites sorter
	if (spritesorter != NULL)
	{
		delete[] spritesorter;
		spritesortersize = 0;
		spritesorter = NULL;
	}

	// Free offscreen buffer
	if (OffscreenColorBuffer != NULL)
	{
		delete[] OffscreenColorBuffer;
		OffscreenColorBuffer = NULL;
	}
	if (OffscreenCoverageBuffer != NULL)
	{
		delete OffscreenCoverageBuffer;
		OffscreenCoverageBuffer = NULL;
	}
	OffscreenBufferHeight = OffscreenBufferWidth = 0;
}

//
// R_ClearSprites
// Called at frame start.
//
void R_ClearSprites (void)
{
	R_ClearVisSprites();
	DrewAVoxel = false;
}

//
// R_DrawMaskedColumn
// Used for sprites and masked mid textures.
// Masked means: partly transparent, i.e. stored
//	in posts/runs of opaque pixels.
//
short*			mfloorclip;
short*			mceilingclip;

double	 		spryscale;
double	 		sprtopscreen;

bool			sprflipvert;

void R_DrawMaskedColumnBgra(FTexture *tex, fixed_t col, bool unmasked)
{
	fixed_t saved_iscale = dc_iscale; // Save this because we need to modify it for mipmaps

	// Normalize to 0-1 range:
	double uv_stepd = FIXED2DBL(dc_iscale);
	double v_step = uv_stepd / tex->GetHeight();

	// Convert to uint32:
	dc_iscale = (uint32_t)(v_step * (1 << 30));

	// Texture mipmap and filter selection:
	fixed_t xoffset = col;

	double xmagnitude = 1.0; // To do: pass this into R_DrawMaskedColumn
	double ymagnitude = fabs(uv_stepd);
	double magnitude = MAX(ymagnitude, xmagnitude);
	double min_lod = -1000.0;
	double lod = MAX(log2(magnitude) + r_lod_bias, min_lod);
	bool magnifying = lod < 0.0f;

	int mipmap_offset = 0;
	int mip_width = tex->GetWidth();
	int mip_height = tex->GetHeight();
	uint32_t xpos = (uint32_t)((((uint64_t)xoffset) << FRACBITS) / mip_width);
	if (r_mipmap && tex->Mipmapped() && mip_width > 1 && mip_height > 1)
	{
		int level = (int)lod;
		while (level > 0 && mip_width > 1 && mip_height > 1)
		{
			mipmap_offset += mip_width * mip_height;
			level--;
			mip_width = MAX(mip_width >> 1, 1);
			mip_height = MAX(mip_height >> 1, 1);
		}
	}
	xoffset = (xpos >> FRACBITS) * mip_width;

	const uint32_t *pixels = tex->GetPixelsBgra() + mipmap_offset;

	bool filter_nearest = (magnifying && !r_magfilter) || (!magnifying && !r_minfilter);
	if (filter_nearest)
	{
		xoffset = MAX(MIN(xoffset, (mip_width << FRACBITS) - 1), 0);

		int tx = xoffset >> FRACBITS;
		dc_source = (BYTE*)(pixels + tx * mip_height);
		dc_source2 = nullptr;
		dc_textureheight = mip_height;
		dc_texturefracx = 0;
	}
	else
	{
		xoffset = MAX(MIN(xoffset - (FRACUNIT / 2), (mip_width << FRACBITS) - 1), 0);

		int tx0 = xoffset >> FRACBITS;
		int tx1 = MIN(tx0 + 1, mip_width - 1);
		dc_source = (BYTE*)(pixels + tx0 * mip_height);
		dc_source2 = (BYTE*)(pixels + tx1 * mip_height);
		dc_textureheight = mip_height;
		dc_texturefracx = (xoffset >> (FRACBITS - 4)) & 15;
	}

	// Grab the posts we need to draw
	const FTexture::Span *span;
	tex->GetColumnBgra(col >> FRACBITS, &span);
	FTexture::Span unmaskedSpan[2];
	if (unmasked)
	{
		span = unmaskedSpan;
		unmaskedSpan[0].TopOffset = 0;
		unmaskedSpan[0].Length = tex->GetHeight();
		unmaskedSpan[1].TopOffset = 0;
		unmaskedSpan[1].Length = 0;
	}

	// Draw each span post
	while (span->Length != 0)
	{
		const int length = span->Length;
		const int top = span->TopOffset;

		// calculate unclipped screen coordinates for post
		dc_yl = (int)(sprtopscreen + spryscale * top + 0.5);
		dc_yh = (int)(sprtopscreen + spryscale * (top + length) + 0.5) - 1;

		if (sprflipvert)
		{
			swapvalues(dc_yl, dc_yh);
		}

		if (dc_yh >= mfloorclip[dc_x])
		{
			dc_yh = mfloorclip[dc_x] - 1;
		}
		if (dc_yl < mceilingclip[dc_x])
		{
			dc_yl = mceilingclip[dc_x];
		}

		if (dc_yl <= dc_yh)
		{
			dc_dest = (ylookup[dc_yl] + dc_x) * 4 + dc_destorg;
			dc_count = dc_yh - dc_yl + 1;

			double v = ((dc_yl + 0.5 - sprtopscreen) / spryscale) / tex->GetHeight();
			dc_texturefrac = (uint32_t)(v * (1 << 30));

			(R_Drawers()->*colfunc)();
		}
		span++;
	}

	dc_iscale = saved_iscale;
}

void R_DrawMaskedColumn (FTexture *tex, fixed_t col, bool unmasked)
{
	// Handle the linear filtered version in a different function to reduce chances of merge conflicts from zdoom.
	if (r_swtruecolor && !drawer_needs_pal_input) // To do: add support to R_DrawColumnHoriz_rgba
	{
		R_DrawMaskedColumnBgra(tex, col, unmasked);
		return;
	}

	const FTexture::Span *span;
	const BYTE *column;
	if (r_swtruecolor && !drawer_needs_pal_input)
		column = (const BYTE *)tex->GetColumnBgra(col >> FRACBITS, &span);
	else
		column = tex->GetColumn(col >> FRACBITS, &span);

	FTexture::Span unmaskedSpan[2];
	if (unmasked)
	{
		span = unmaskedSpan;
		unmaskedSpan[0].TopOffset = 0;
		unmaskedSpan[0].Length = tex->GetHeight();
		unmaskedSpan[1].TopOffset = 0;
		unmaskedSpan[1].Length = 0;
	}

	int pixelsize = r_swtruecolor ? 4 : 1;

	while (span->Length != 0)
	{
		const int length = span->Length;
		const int top = span->TopOffset;

		// calculate unclipped screen coordinates for post
		dc_yl = (int)(sprtopscreen + spryscale * top + 0.5);
		dc_yh = (int)(sprtopscreen + spryscale * (top + length) + 0.5) - 1;

		if (sprflipvert)
		{
			swapvalues (dc_yl, dc_yh);
		}

		if (dc_yh >= mfloorclip[dc_x])
		{
			dc_yh = mfloorclip[dc_x] - 1;
		}
		if (dc_yl < mceilingclip[dc_x])
		{
			dc_yl = mceilingclip[dc_x];
		}

		if (dc_yl <= dc_yh)
		{
			dc_texturefrac = FLOAT2FIXED((dc_yl + 0.5 - sprtopscreen) / spryscale);
			dc_source = column;
			dc_source2 = nullptr;
			dc_dest = (ylookup[dc_yl] + dc_x) * pixelsize + dc_destorg;
			dc_count = dc_yh - dc_yl + 1;

			fixed_t maxfrac = ((top + length) << FRACBITS) - 1;
			dc_texturefrac = MAX(dc_texturefrac, 0);
			dc_texturefrac = MIN(dc_texturefrac, maxfrac);
			if (dc_iscale > 0)
				dc_count = MIN(dc_count, (maxfrac - dc_texturefrac + dc_iscale - 1) / dc_iscale);
			else if (dc_iscale < 0)
				dc_count = MIN(dc_count, (dc_texturefrac - dc_iscale) / (-dc_iscale));

			(R_Drawers()->*colfunc)();
		}
		span++;
	}
}

// [ZZ]
// R_ClipSpriteColumnWithPortals
//

static TArray<drawseg_t *> portaldrawsegs;

static inline void R_CollectPortals()
{
	// This function collects all drawsegs that may be of interest to R_ClipSpriteColumnWithPortals 
	// Having that function over the entire list of drawsegs can break down performance quite drastically.
	// This is doing the costly stuff only once so that R_ClipSpriteColumnWithPortals can 
	// a) exit early if no relevant info is found and
	// b) skip most of the collected drawsegs which have no portal attached.
	portaldrawsegs.Clear();
	for (drawseg_t* seg = ds_p; seg-- > firstdrawseg; ) // copied code from killough below
	{
		// I don't know what makes this happen (some old top-down portal code or possibly skybox code? something adds null lines...)
		// crashes at the first frame of the first map of Action2.wad
		if (!seg->curline) continue;

		line_t* line = seg->curline->linedef;
		// ignore minisegs from GL nodes.
		if (!line) continue;

		// check if this line will clip sprites to itself
		if (!line->isVisualPortal() && line->special != Line_Mirror)
			continue;

		// don't clip sprites with portal's back side (it's transparent)
		if (seg->curline->sidedef != line->sidedef[0])
			continue;

		portaldrawsegs.Push(seg);
	}
}

bool R_ClipSpriteColumnWithPortals(vissprite_t* spr)
{
	// [ZZ] 10.01.2016: don't clip sprites from the root of a skybox.
	if (CurrentPortalInSkybox)
		return false;

	for (drawseg_t *seg : portaldrawsegs)
	{
		// ignore segs from other portals
		if (seg->CurrentPortalUniq != CurrentPortalUniq)
			continue;

		// (all checks that are already done in R_CollectPortals have been removed for performance reasons.)

		// don't clip if the sprite is in front of the portal
		if (!P_PointOnLineSidePrecise(spr->gpos.X, spr->gpos.Y, seg->curline->linedef))
			continue;

		// now if current column is covered by this drawseg, we clip it away
		if ((dc_x >= seg->x1) && (dc_x < seg->x2))
			return true;
	}

	return false;
}


//
// R_DrawVisSprite
//	mfloorclip and mceilingclip should also be set.
//
void R_DrawVisSprite (vissprite_t *vis)
{
	fixed_t 		frac;
	FTexture		*tex;
	int				x2;
	fixed_t			xiscale;
	bool			ispsprite = (!vis->sector && vis->gpos != FVector3(0, 0, 0));

	if (vis->xscale == 0 || fabs(vis->yscale) < (1.0f / 32000.0f))
	{ // scaled to 0; can't see
		return;
	}

	fixed_t centeryfrac = FLOAT2FIXED(CenterY);
	R_SetColorMapLight(vis->Style.BaseColormap, 0, vis->Style.ColormapNum << FRACBITS);

	bool visible = R_SetPatchStyle (vis->Style.RenderStyle, vis->Style.Alpha, vis->Translation, vis->FillColor);

	if (vis->Style.RenderStyle == LegacyRenderStyles[STYLE_Shaded])
	{ // For shaded sprites, R_SetPatchStyle sets a dc_colormap to an alpha table, but
	  // it is the brightest one. We need to get back to the proper light level for
	  // this sprite.
		R_SetColorMapLight(dc_fcolormap, 0, vis->Style.ColormapNum << FRACBITS);
	}

	if (visible)
	{
		tex = vis->pic;
		spryscale = vis->yscale;
		sprflipvert = false;
		dc_iscale = FLOAT2FIXED(1 / vis->yscale);
		frac = vis->startfrac;
		xiscale = vis->xiscale;
		dc_texturemid = vis->texturemid;

		if (vis->renderflags & RF_YFLIP)
		{
			sprflipvert = true;
			spryscale = -spryscale;
			dc_iscale = -dc_iscale;
			dc_texturemid -= vis->pic->GetHeight();
			sprtopscreen = CenterY + dc_texturemid * spryscale;
		}
		else
		{
			sprflipvert = false;
			sprtopscreen = CenterY - dc_texturemid * spryscale;
		}

		dc_x = vis->x1;
		x2 = vis->x2;

		if (dc_x < x2)
		{
			while (dc_x < x2)
			{
				if (ispsprite || !R_ClipSpriteColumnWithPortals(vis))
					R_DrawMaskedColumn (tex, frac, false);
				dc_x++;
				frac += xiscale;
			}
		}
	}

	R_FinishSetPatchStyle ();

	NetUpdate ();
}

#if 0
void R_DrawVisVoxel(vissprite_t *spr, int minslabz, int maxslabz, short *cliptop, short *clipbot)
{
	int flags = 0;

	// Do setup for blending.
	R_SetColorMapLight(spr->Style.BaseColormap, 0, spr->Style.ColormapNum << FRACBITS);
	bool visible = R_SetPatchStyle(spr->Style.RenderStyle, spr->Style.Alpha, spr->Translation, spr->FillColor);

	if (!visible)
	{
		return;
	}
	if (colfunc == fuzzcolfunc || colfunc == R_FillColumn)
	{
		flags = DVF_OFFSCREEN | DVF_SPANSONLY;
	}
	else if (colfunc != basecolfunc)
	{
		flags = DVF_OFFSCREEN;
	}
	if (flags != 0)
	{
		R_CheckOffscreenBuffer(RenderTarget->GetWidth(), RenderTarget->GetHeight(), !!(flags & DVF_SPANSONLY));
	}
	if (spr->bInMirror)
	{
		flags |= DVF_MIRRORED;
	}

	// Render the voxel, either directly to the screen or offscreen.
	R_DrawVoxel(spr->pa.vpos, spr->pa.vang, spr->gpos, spr->Angle,
		spr->xscale, FLOAT2FIXED(spr->yscale), spr->voxel, spr->Style.BaseColormap, spr->Style.ColormapNum, cliptop, clipbot,
		minslabz, maxslabz, flags);

	// Blend the voxel, if that's what we need to do.
	if ((flags & ~DVF_MIRRORED) != 0)
	{
		int pixelsize = r_swtruecolor ? 4 : 1;
		for (int x = 0; x < viewwidth; ++x)
		{
			if (!(flags & DVF_SPANSONLY) && (x & 3) == 0)
			{
				rt_initcols(OffscreenColorBuffer + x * OffscreenBufferHeight);
			}
			for (FCoverageBuffer::Span *span = OffscreenCoverageBuffer->Spans[x]; span != NULL; span = span->NextSpan)
			{
				if (flags & DVF_SPANSONLY)
				{
					dc_x = x;
					dc_yl = span->Start;
					dc_yh = span->Stop - 1;
					dc_count = span->Stop - span->Start;
					dc_dest = (ylookup[span->Start] + x) * pixelsize + dc_destorg;
					colfunc();
				}
				else
				{
					rt_span_coverage(x, span->Start, span->Stop - 1);
				}
			}
			if (!(flags & DVF_SPANSONLY) && (x & 3) == 3)
			{
				rt_draw4cols(x - 3);
			}
		}
	}

	R_FinishSetPatchStyle();
	NetUpdate();
}
#endif

//
// R_ProjectSprite
// Generates a vissprite for a thing if it might be visible.
//
void R_ProjectSprite (AActor *thing, int fakeside, F3DFloor *fakefloor, F3DFloor *fakeceiling, sector_t *current_sector)
{
	double 			tr_x;
	double 			tr_y;
	
	double				gzt;				// killough 3/27/98
	double				gzb;				// [RH] use bottom of sprite, not actor
	double	 			tx;// , tx2;
	double 				tz;

	double 				xscale = 1, yscale = 1;
	
	int 				x1;
	int 				x2;

	FTextureID			picnum;
	FTexture			*tex;
	FVoxelDef			*voxel;
	
	vissprite_t*		vis;
	
	fixed_t 			iscale;

	sector_t*			heightsec;			// killough 3/27/98

	// Don't waste time projecting sprites that are definitely not visible.
	if (thing == NULL ||
		(thing->renderflags & RF_INVISIBLE) ||
		!thing->RenderStyle.IsVisible(thing->Alpha) ||
		!thing->IsVisibleToPlayer() ||
		!thing->IsInsideVisibleAngles())
	{
		return;
	}

	// [ZZ] Or less definitely not visible (hue)
	// [ZZ] 10.01.2016: don't try to clip stuff inside a skybox against the current portal.
	if (!CurrentPortalInSkybox && CurrentPortal && !!P_PointOnLineSidePrecise(thing->Pos(), CurrentPortal->dst))
		return;

	// [RH] Interpolate the sprite's position to make it look smooth
	DVector3 pos = thing->InterpolatedPosition(r_TicFracF);
	pos.Z += thing->GetBobOffset(r_TicFracF);

	tex = NULL;
	voxel = NULL;

	int spritenum = thing->sprite;
	DVector2 spriteScale = thing->Scale;
	int renderflags = thing->renderflags;
	if (spriteScale.Y < 0)
	{
		spriteScale.Y = -spriteScale.Y;
		renderflags ^= RF_YFLIP;
	}
	if (thing->player != NULL)
	{
		P_CheckPlayerSprite(thing, spritenum, spriteScale);
	}

	if (thing->picnum.isValid())
	{
		picnum = thing->picnum;

		tex = TexMan(picnum);
		if (tex->UseType == FTexture::TEX_Null)
		{
			return;
		}

		if (tex->Rotations != 0xFFFF)
		{
			// choose a different rotation based on player view
			spriteframe_t *sprframe = &SpriteFrames[tex->Rotations];
			DAngle ang = (pos - ViewPos).Angle();
			angle_t rot;
			if (sprframe->Texture[0] == sprframe->Texture[1])
			{
				if (thing->flags7 & MF7_SPRITEANGLE)
					rot = (thing->SpriteAngle + 45.0 / 2 * 9).BAMs() >> 28;
				else
					rot = (ang - (thing->Angles.Yaw + thing->SpriteRotation) + 45.0 / 2 * 9).BAMs() >> 28;
			}
			else
			{
				if (thing->flags7 & MF7_SPRITEANGLE)
					rot = (thing->SpriteAngle + (45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
				else
					rot = (ang - (thing->Angles.Yaw + thing->SpriteRotation) + (45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
			}
			picnum = sprframe->Texture[rot];
			if (sprframe->Flip & (1 << rot))
			{
				renderflags ^= RF_XFLIP;
			}
			tex = TexMan[picnum];	// Do not animate the rotation
		}
	}
	else
	{
		// decide which texture to use for the sprite
		if ((unsigned)spritenum >= sprites.Size ())
		{
			DPrintf (DMSG_ERROR, "R_ProjectSprite: invalid sprite number %u\n", spritenum);
			return;
		}
		spritedef_t *sprdef = &sprites[spritenum];
		if (thing->frame >= sprdef->numframes)
		{
			// If there are no frames at all for this sprite, don't draw it.
			return;
		}
		else
		{
			//picnum = SpriteFrames[sprdef->spriteframes + thing->frame].Texture[0];
			// choose a different rotation based on player view
			spriteframe_t *sprframe = &SpriteFrames[sprdef->spriteframes + thing->frame];
			DAngle ang = (pos - ViewPos).Angle();
			angle_t rot;
			if (sprframe->Texture[0] == sprframe->Texture[1])
			{
				if (thing->flags7 & MF7_SPRITEANGLE)
					rot = (thing->SpriteAngle + 45.0 / 2 * 9).BAMs() >> 28;
				else
					rot = (ang - (thing->Angles.Yaw + thing->SpriteRotation) + 45.0 / 2 * 9).BAMs() >> 28;
			}
			else
			{
				if (thing->flags7 & MF7_SPRITEANGLE)
					rot = (thing->SpriteAngle + (45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
				else
					rot = (ang - (thing->Angles.Yaw + thing->SpriteRotation) + (45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
			}
			picnum = sprframe->Texture[rot];
			if (sprframe->Flip & (1 << rot))
			{
				renderflags ^= RF_XFLIP;
			}
			tex = TexMan[picnum];	// Do not animate the rotation
			if (r_drawvoxels)
			{
				voxel = sprframe->Voxel;
			}
		}
	}
	if (spriteScale.X < 0)
	{
		spriteScale.X = -spriteScale.X;
		renderflags ^= RF_XFLIP;
	}
	if (voxel == NULL && (tex == NULL || tex->UseType == FTexture::TEX_Null))
	{
		return;
	}

	if ((renderflags & RF_SPRITETYPEMASK) == RF_WALLSPRITE)
	{
		R_ProjectWallSprite(thing, pos, picnum, spriteScale, renderflags);
		return;
	}

	// transform the origin point
	tr_x = pos.X - ViewPos.X;
	tr_y = pos.Y - ViewPos.Y;

	tz = tr_x * ViewTanCos + tr_y * ViewTanSin;

	// thing is behind view plane?
	if (voxel == NULL && tz < MINZ)
		return;

	tx = tr_x * ViewSin - tr_y * ViewCos;

	// [RH] Flip for mirrors
	if (MirrorFlags & RF_XFLIP)
	{
		tx = -tx;
	}
	//tx2 = tx >> 4;

	// too far off the side?
	// if it's a voxel, it can be further off the side
	if ((voxel == NULL && (fabs(tx / 64) > fabs(tz))) ||
		(voxel != NULL && (fabs(tx / 128) > fabs(tz))))
	{
		return;
	}

	if (voxel == NULL)
	{
		// [RH] Added scaling
		int scaled_to = tex->GetScaledTopOffset();
		int scaled_bo = scaled_to - tex->GetScaledHeight();
		gzt = pos.Z + spriteScale.Y * scaled_to;
		gzb = pos.Z + spriteScale.Y * scaled_bo;
	}
	else
	{
		xscale = spriteScale.X * voxel->Scale;
		yscale = spriteScale.Y * voxel->Scale;
		double piv = voxel->Voxel->Mips[0].Pivot.Z;
		gzt = pos.Z + yscale * piv - thing->Floorclip;
		gzb = pos.Z + yscale * (piv - voxel->Voxel->Mips[0].SizeZ);
		if (gzt <= gzb)
			return;
	}

	// killough 3/27/98: exclude things totally separated
	// from the viewer, by either water or fake ceilings
	// killough 4/11/98: improve sprite clipping for underwater/fake ceilings

	heightsec = thing->Sector->GetHeightSec();

	if (heightsec != NULL)	// only clip things which are in special sectors
	{
		if (fakeside == FAKED_AboveCeiling)
		{
			if (gzt < heightsec->ceilingplane.ZatPoint(pos))
				return;
		}
		else if (fakeside == FAKED_BelowFloor)
		{
			if (gzb >= heightsec->floorplane.ZatPoint(pos))
				return;
		}
		else
		{
			if (gzt < heightsec->floorplane.ZatPoint(pos))
				return;
			if (!(heightsec->MoreFlags & SECF_FAKEFLOORONLY) && gzb >= heightsec->ceilingplane.ZatPoint(pos))
				return;
		}
	}

	if (voxel == NULL)
	{
		xscale = CenterX / tz;

		// [RH] Reject sprites that are off the top or bottom of the screen
		if (globaluclip * tz > ViewPos.Z - gzb || globaldclip * tz < ViewPos.Z - gzt)
		{
			return;
		}

		// [RH] Flip for mirrors
		renderflags ^= MirrorFlags & RF_XFLIP;

		// calculate edges of the shape
		const double thingxscalemul = spriteScale.X / tex->Scale.X;

		tx -= ((renderflags & RF_XFLIP) ? (tex->GetWidth() - tex->LeftOffset - 1) : tex->LeftOffset) * thingxscalemul;
		double dtx1 = tx * xscale;
		x1 = centerx + xs_RoundToInt(dtx1);

		// off the right side?
		if (x1 >= WindowRight)
			return;

		tx += tex->GetWidth() * thingxscalemul;
		x2 = centerx + xs_RoundToInt(tx * xscale);

		// off the left side or too small?
		if ((x2 < WindowLeft || x2 <= x1))
			return;

		xscale = spriteScale.X * xscale / tex->Scale.X;
		iscale = (fixed_t)(FRACUNIT / xscale); // Round towards zero to avoid wrapping in edge cases

		double yscale = spriteScale.Y / tex->Scale.Y;

		// store information in a vissprite
		vis = R_NewVisSprite();

		vis->CurrentPortalUniq = CurrentPortalUniq;
		vis->xscale = FLOAT2FIXED(xscale);
		vis->yscale = float(InvZtoScale * yscale / tz);
		vis->idepth = float(1 / tz);
		vis->floorclip = thing->Floorclip / yscale;
		vis->texturemid = tex->TopOffset - (ViewPos.Z - pos.Z + thing->Floorclip) / yscale;
		vis->x1 = x1 < WindowLeft ? WindowLeft : x1;
		vis->x2 = x2 > WindowRight ? WindowRight : x2;
		vis->Angle = thing->Angles.Yaw;

		if (renderflags & RF_XFLIP)
		{
			vis->startfrac = (tex->GetWidth() << FRACBITS) - 1;
			vis->xiscale = -iscale;
		}
		else
		{
			vis->startfrac = 0;
			vis->xiscale = iscale;
		}

		vis->startfrac += (fixed_t)(vis->xiscale * (vis->x1 - centerx + 0.5 - dtx1));
	}
	else
	{
		vis = R_NewVisSprite();

		vis->CurrentPortalUniq = CurrentPortalUniq;
		vis->xscale = FLOAT2FIXED(xscale);
		vis->yscale = (float)yscale;
		vis->x1 = WindowLeft;
		vis->x2 = WindowRight;
		vis->idepth = 1 / MINZ;
		vis->floorclip = thing->Floorclip;

		pos.Z -= thing->Floorclip;

		vis->Angle = thing->Angles.Yaw + voxel->AngleOffset;

		int voxelspin = (thing->flags & MF_DROPPED) ? voxel->DroppedSpin : voxel->PlacedSpin;
		if (voxelspin != 0)
		{
			DAngle ang = double(I_FPSTime()) * voxelspin / 1000;
			vis->Angle -= ang;
		}

		vis->pa.vpos = { (float)ViewPos.X, (float)ViewPos.Y, (float)ViewPos.Z };
		vis->pa.vang = FAngle((float)ViewAngle.Degrees);
	}

	// killough 3/27/98: save sector for special clipping later
	vis->heightsec = heightsec;
	vis->sector = thing->Sector;

	vis->depth = (float)tz;
	vis->gpos = { (float)pos.X, (float)pos.Y, (float)pos.Z };
	vis->gzb = (float)gzb;		// [RH] use gzb, not thing->z
	vis->gzt = (float)gzt;		// killough 3/27/98
	vis->deltax = float(pos.X - ViewPos.X);
	vis->deltay = float(pos.Y - ViewPos.Y);
	vis->renderflags = renderflags;
	if(thing->flags5 & MF5_BRIGHT)
		vis->renderflags |= RF_FULLBRIGHT; // kg3D
	vis->Style.RenderStyle = thing->RenderStyle;
	vis->FillColor = thing->fillcolor;
	vis->Translation = thing->Translation;		// [RH] thing translation table
	vis->FakeFlatStat = fakeside;
	vis->Style.Alpha = float(thing->Alpha);
	vis->fakefloor = fakefloor;
	vis->fakeceiling = fakeceiling;
	vis->Style.ColormapNum = 0;
	vis->bInMirror = MirrorFlags & RF_XFLIP;
	vis->bSplitSprite = false;

	if (voxel != NULL)
	{
		vis->voxel = voxel->Voxel;
		vis->bIsVoxel = true;
		vis->bWallSprite = false;
		DrewAVoxel = true;
	}
	else
	{
		vis->pic = tex;
		vis->bIsVoxel = false;
		vis->bWallSprite = false;
	}

	// The software renderer cannot invert the source without inverting the overlay
	// too. That means if the source is inverted, we need to do the reverse of what
	// the invert overlay flag says to do.
	INTBOOL invertcolormap = (vis->Style.RenderStyle.Flags & STYLEF_InvertOverlay);

	if (vis->Style.RenderStyle.Flags & STYLEF_InvertSource)
	{
		invertcolormap = !invertcolormap;
	}

	FDynamicColormap *mybasecolormap = basecolormap;
	if (current_sector->sectornum != thing->Sector->sectornum)	// compare sectornums to account for R_FakeFlat copies.
	{
		// Todo: The actor is from a different sector so we have to retrieve the proper basecolormap for that sector.
	}

	// Sprites that are added to the scene must fade to black.
	if (vis->Style.RenderStyle == LegacyRenderStyles[STYLE_Add] && mybasecolormap->Fade != 0)
	{
		mybasecolormap = GetSpecialLights(mybasecolormap->Color, 0, mybasecolormap->Desaturate);
	}

	if (vis->Style.RenderStyle.Flags & STYLEF_FadeToBlack)
	{
		if (invertcolormap)
		{ // Fade to white
			mybasecolormap = GetSpecialLights(mybasecolormap->Color, MAKERGB(255,255,255), mybasecolormap->Desaturate);
			invertcolormap = false;
		}
		else
		{ // Fade to black
			mybasecolormap = GetSpecialLights(mybasecolormap->Color, MAKERGB(0,0,0), mybasecolormap->Desaturate);
		}
	}

	// get light level
	if (fixedcolormap != NULL)
	{ // fixed map
		vis->Style.BaseColormap = fixedcolormap;
		vis->Style.ColormapNum = 0;
	}
	else
	{
		if (invertcolormap)
		{
			mybasecolormap = GetSpecialLights(mybasecolormap->Color, mybasecolormap->Fade.InverseColor(), mybasecolormap->Desaturate);
		}
		if (fixedlightlev >= 0)
		{
			vis->Style.BaseColormap = mybasecolormap;
			vis->Style.ColormapNum = fixedlightlev >> COLORMAPSHIFT;
		}
		else if (!foggy && ((renderflags & RF_FULLBRIGHT) || (thing->flags5 & MF5_BRIGHT)))
		{ // full bright
			vis->Style.BaseColormap = (r_fullbrightignoresectorcolor) ? &FullNormalLight : mybasecolormap;
			vis->Style.ColormapNum = 0;
		}
		else
		{ // diminished light
			vis->Style.ColormapNum = GETPALOOKUP(
				r_SpriteVisibility / MAX(tz, MINZ), spriteshade);
			vis->Style.BaseColormap = mybasecolormap;
		}
	}
}

//
// R_AddSprites
// During BSP traversal, this adds sprites by sector.
//
// killough 9/18/98: add lightlevel as parameter, fixing underwater lighting
// [RH] Save which side of heightsec sprite is on here.
void R_AddSprites (sector_t *sec, int lightlevel, int fakeside)
{
	F3DFloor *fakeceiling = NULL;
	F3DFloor *fakefloor = NULL;

	// BSP is traversed by subsector.
	// A sector might have been split into several
	//	subsectors during BSP building.
	// Thus we check whether it was already added.
	if (sec->touching_renderthings == nullptr || sec->validcount == validcount)
		return;

	// Well, now it will be done.
	sec->validcount = validcount;

	spriteshade = LIGHT2SHADE(lightlevel + r_actualextralight);

	// Handle all things in sector.
	for(auto p = sec->touching_renderthings; p != nullptr; p = p->m_snext)
	{
		auto thing = p->m_thing;
		if (thing->validcount == validcount) continue;
		thing->validcount = validcount;
		
		FIntCVar *cvar = thing->GetClass()->distancecheck;
		if (cvar != NULL && *cvar >= 0)
		{
			double dist = (thing->Pos() - ViewPos).LengthSquared();
			double check = (double)**cvar;
			if (dist >= check * check)
			{
				continue;
			}
		}

		// find fake level
		for(auto rover : thing->Sector->e->XFloor.ffloors) 
		{
			if(!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERPLANES)) continue;
			if(!(rover->flags & FF_SOLID) || rover->alpha != 255) continue;
			if(!fakefloor)
			{
				if(!rover->top.plane->isSlope())
				{
					if(rover->top.plane->ZatPoint(0., 0.) <= thing->Z()) fakefloor = rover;
				}
			}
			if(!rover->bottom.plane->isSlope())
			{
				if(rover->bottom.plane->ZatPoint(0., 0.) >= thing->Top()) fakeceiling = rover;
			}
		}	
		R_ProjectSprite (thing, fakeside, fakefloor, fakeceiling, sec);
		fakeceiling = NULL;
		fakefloor = NULL;
	}
}


//
// R_SortVisSprites
//
// [RH] The old code for this function used a bubble sort, which was far less
//		than optimal with large numbers of sprites. I changed it to use the
//		stdlib qsort() function instead, and now it is a *lot* faster; the
//		more vissprites that need to be sorted, the better the performance
//		gain compared to the old function.
//
// Sort vissprites by depth, far to near

// This is the standard version, which does a simple test based on depth.
static bool sv_compare(vissprite_t *a, vissprite_t *b)
{
	return a->idepth > b->idepth;
}

// This is an alternate version, for when one or more voxel is in view.
// It does a 2D distance test based on whichever one is furthest from
// the viewpoint.
static bool sv_compare2d(vissprite_t *a, vissprite_t *b)
{
	return DVector2(a->deltax, a->deltay).LengthSquared() <
		   DVector2(b->deltax, b->deltay).LengthSquared();
}

#if 0
static drawseg_t **drawsegsorter;
static int drawsegsortersize = 0;

// Sort vissprites by leftmost column, left to right
static int sv_comparex (const void *arg1, const void *arg2)
{
	return (*(vissprite_t **)arg2)->x1 - (*(vissprite_t **)arg1)->x1;
}

// Sort drawsegs by rightmost column, left to right
static int sd_comparex (const void *arg1, const void *arg2)
{
	return (*(drawseg_t **)arg2)->x2 - (*(drawseg_t **)arg1)->x2;
}

// Split up vissprites that intersect drawsegs
void R_SplitVisSprites ()
{
	size_t start, stop;
	size_t numdrawsegs = ds_p - firstdrawseg;
	size_t numsprites;
	size_t spr, dseg, dseg2;

	if (!r_splitsprites)
		return;

	if (numdrawsegs == 0 || vissprite_p - firstvissprite == 0)
		return;

	// Sort drawsegs from left to right
	if (numdrawsegs > drawsegsortersize)
	{
		if (drawsegsorter != NULL)
			delete[] drawsegsorter;
		drawsegsortersize = numdrawsegs * 2;
		drawsegsorter = new drawseg_t *[drawsegsortersize];
	}
	for (dseg = dseg2 = 0; dseg < numdrawsegs; ++dseg)
	{
		// Drawsegs that don't clip any sprites don't need to be considered.
		if (firstdrawseg[dseg].silhouette)
		{
			drawsegsorter[dseg2++] = &firstdrawseg[dseg];
		}
	}
	numdrawsegs = dseg2;
	if (numdrawsegs == 0)
	{
		return;
	}
	qsort (drawsegsorter, numdrawsegs, sizeof(drawseg_t *), sd_comparex);

	// Now sort vissprites from left to right, and walk them simultaneously
	// with the drawsegs, splitting any that intersect.
	start = firstvissprite - vissprites;

	int p = 0;
	do
	{
		p++;
		R_SortVisSprites (sv_comparex, start);
		stop = vissprite_p - vissprites;
		numsprites = stop - start;

		spr = dseg = 0;
		do
		{
			vissprite_t *vis = spritesorter[spr], *vis2;

			// Skip drawsegs until we get to one that doesn't end before the sprite
			// begins.
			while (dseg < numdrawsegs && drawsegsorter[dseg]->x2 <= vis->x1)
			{
				dseg++;
			}
			// Now split the sprite against any drawsegs it intersects
			for (dseg2 = dseg; dseg2 < numdrawsegs; dseg2++)
			{
				drawseg_t *ds = drawsegsorter[dseg2];

				if (ds->x1 > vis->x2 || ds->x2 < vis->x1)
					continue;

				if ((vis->idepth < ds->siz1) != (vis->idepth < ds->siz2))
				{ // The drawseg is crossed; find the x where the intersection occurs
					int cross = Scale (vis->idepth - ds->siz1, ds->sx2 - ds->sx1, ds->siz2 - ds->siz1) + ds->sx1 + 1;

/*					if (cross < ds->x1 || cross > ds->x2)
					{ // The original seg is crossed, but the drawseg is not
						continue;
					}
*/					if (cross <= vis->x1 || cross >= vis->x2)
					{ // Don't create 0-sized sprites
						continue;
					}

					vis->bSplitSprite = true;

					// Create a new vissprite for the right part of the sprite
					vis2 = R_NewVisSprite ();
					*vis2 = *vis;
					vis2->startfrac += vis2->xiscale * (cross - vis2->x1);
					vis->x2 = cross-1;
					vis2->x1 = cross;
					//vis2->alpha /= 2;
					//vis2->RenderStyle = STYLE_Add;

					if (vis->idepth < ds->siz1)
					{ // Left is in back, right is in front
						vis->sector  = ds->curline->backsector;
						vis2->sector = ds->curline->frontsector;
					}
					else
					{ // Right is in front, left is in back
						vis->sector  = ds->curline->frontsector;
						vis2->sector = ds->curline->backsector;
					}
				}
			}
		}
		while (dseg < numdrawsegs && ++spr < numsprites);

		// Repeat for any new sprites that were added.
	}
	while (start = stop, stop != vissprite_p - vissprites);
}
#endif

#ifdef __GNUC__
static void swap(vissprite_t *&a, vissprite_t *&b)
{
	vissprite_t *t = a;
	a = b;
	b = t;
}
#endif

void R_SortVisSprites (bool (*compare)(vissprite_t *, vissprite_t *), size_t first)
{
	int i;
	vissprite_t **spr;

	vsprcount = int(vissprite_p - &vissprites[first]);

	if (vsprcount == 0)
		return;

	if (spritesortersize < MaxVisSprites)
	{
		if (spritesorter != NULL)
			delete[] spritesorter;
		spritesorter = new vissprite_t *[MaxVisSprites];
		spritesortersize = MaxVisSprites;
	}

	if (!(i_compatflags & COMPATF_SPRITESORT))
	{
		for (i = 0, spr = firstvissprite; i < vsprcount; i++, spr++)
		{
			spritesorter[i] = *spr;
		}
	}
	else
	{
		// If the compatibility option is on sprites of equal distance need to
		// be sorted in inverse order. This is most easily achieved by
		// filling the sort array backwards before the sort.
		for (i = 0, spr = firstvissprite + vsprcount-1; i < vsprcount; i++, spr--)
		{
			spritesorter[i] = *spr;
		}
	}

	std::stable_sort(&spritesorter[0], &spritesorter[vsprcount], compare);
}

//
// R_DrawSprite
//
void R_DrawSprite (vissprite_t *spr)
{
	static short clipbot[MAXWIDTH];
	static short cliptop[MAXWIDTH];
	drawseg_t *ds;
	int i;
	int x1, x2;
	int r1, r2;
	short topclip, botclip;
	short *clip1, *clip2;
	FSWColormap *colormap = spr->Style.BaseColormap;
	int colormapnum = spr->Style.ColormapNum;
	F3DFloor *rover;
	FDynamicColormap *mybasecolormap;

	// [RH] Check for particles
	if (!spr->bIsVoxel && spr->pic == NULL)
	{
		// kg3D - reject invisible parts
		if ((fake3D & FAKE3D_CLIPBOTTOM) && spr->gpos.Z <= sclipBottom) return;
		if ((fake3D & FAKE3D_CLIPTOP)    && spr->gpos.Z >= sclipTop) return;
		R_DrawParticle (spr);
		return;
	}

	x1 = spr->x1;
	x2 = spr->x2;

	// [RH] Quickly reject sprites with bad x ranges.
	if (x1 >= x2)
		return;

	// [RH] Sprites split behind a one-sided line can also be discarded.
	if (spr->sector == NULL)
		return;

	// kg3D - reject invisible parts
	if ((fake3D & FAKE3D_CLIPBOTTOM) && spr->gzt <= sclipBottom) return;
	if ((fake3D & FAKE3D_CLIPTOP)    && spr->gzb >= sclipTop) return;

	// kg3D - correct colors now
	if (!fixedcolormap && fixedlightlev < 0 && spr->sector->e && spr->sector->e->XFloor.lightlist.Size()) 
	{
		if (!(fake3D & FAKE3D_CLIPTOP))
		{
			sclipTop = spr->sector->ceilingplane.ZatPoint(ViewPos);
		}
		sector_t *sec = NULL;
		for (i = spr->sector->e->XFloor.lightlist.Size() - 1; i >= 0; i--)
		{
			if (sclipTop <= spr->sector->e->XFloor.lightlist[i].plane.Zat0()) 
			{
				rover = spr->sector->e->XFloor.lightlist[i].caster;
				if (rover) 
				{
					if (rover->flags & FF_DOUBLESHADOW && sclipTop <= rover->bottom.plane->Zat0())
					{
						break;
					}
					sec = rover->model;
					if (rover->flags & FF_FADEWALLS)
					{
						mybasecolormap = sec->ColorMap;
					}
					else
					{
						mybasecolormap = spr->sector->e->XFloor.lightlist[i].extra_colormap;
					}
				}
				break;
			}
		}
		// found new values, recalculate
		if (sec) 
		{
			INTBOOL invertcolormap = (spr->Style.RenderStyle.Flags & STYLEF_InvertOverlay);

			if (spr->Style.RenderStyle.Flags & STYLEF_InvertSource)
			{
				invertcolormap = !invertcolormap;
			}

			// Sprites that are added to the scene must fade to black.
			if (spr->Style.RenderStyle == LegacyRenderStyles[STYLE_Add] && mybasecolormap->Fade != 0)
			{
				mybasecolormap = GetSpecialLights(mybasecolormap->Color, 0, mybasecolormap->Desaturate);
			}

			if (spr->Style.RenderStyle.Flags & STYLEF_FadeToBlack)
			{
				if (invertcolormap)
				{ // Fade to white
					mybasecolormap = GetSpecialLights(mybasecolormap->Color, MAKERGB(255,255,255), mybasecolormap->Desaturate);
					invertcolormap = false;
				}
				else
				{ // Fade to black
					mybasecolormap = GetSpecialLights(mybasecolormap->Color, MAKERGB(0,0,0), mybasecolormap->Desaturate);
				}
			}

			// get light level
			if (invertcolormap)
			{
				mybasecolormap = GetSpecialLights(mybasecolormap->Color, mybasecolormap->Fade.InverseColor(), mybasecolormap->Desaturate);
			}
			if (fixedlightlev >= 0)
			{
				spr->Style.BaseColormap = mybasecolormap;
				spr->Style.ColormapNum = fixedlightlev >> COLORMAPSHIFT;
			}
			else if (!foggy && (spr->renderflags & RF_FULLBRIGHT))
			{ // full bright
				spr->Style.BaseColormap = (r_fullbrightignoresectorcolor) ? &FullNormalLight : mybasecolormap;
				spr->Style.ColormapNum = 0;
			}
			else
			{ // diminished light
				spriteshade = LIGHT2SHADE(sec->lightlevel + r_actualextralight);
				spr->Style.BaseColormap = mybasecolormap;
				spr->Style.ColormapNum = GETPALOOKUP(r_SpriteVisibility / MAX(MINZ, (double)spr->depth), spriteshade);
			}
		}
	}

	// [RH] Initialize the clipping arrays to their largest possible range
	// instead of using a special "not clipped" value. This eliminates
	// visual anomalies when looking down and should be faster, too.
	topclip = 0;
	botclip = viewheight;

	// killough 3/27/98:
	// Clip the sprite against deep water and/or fake ceilings.
	// [RH] rewrote this to be based on which part of the sector is really visible

	double scale = InvZtoScale * spr->idepth;
	double hzb = DBL_MIN, hzt = DBL_MAX;

	if (spr->bIsVoxel && spr->floorclip != 0)
	{
		hzb = spr->gzb;
	}

	if (spr->heightsec && !(spr->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC))
	{ // only things in specially marked sectors
		if (spr->FakeFlatStat != FAKED_AboveCeiling)
		{
			double hz = spr->heightsec->floorplane.ZatPoint(spr->gpos);
			int h = xs_RoundToInt(CenterY - (hz - ViewPos.Z) * scale);

			if (spr->FakeFlatStat == FAKED_BelowFloor)
			{ // seen below floor: clip top
				if (!spr->bIsVoxel && h > topclip)
				{
					topclip = short(MIN(h, viewheight));
				}
				hzt = MIN(hzt, hz);
			}
			else
			{ // seen in the middle: clip bottom
				if (!spr->bIsVoxel && h < botclip)
				{
					botclip = MAX<short> (0, h);
				}
				hzb = MAX(hzb, hz);
			}
		}
		if (spr->FakeFlatStat != FAKED_BelowFloor && !(spr->heightsec->MoreFlags & SECF_FAKEFLOORONLY))
		{
			double hz = spr->heightsec->ceilingplane.ZatPoint(spr->gpos);
			int h = xs_RoundToInt(CenterY - (hz - ViewPos.Z) * scale);

			if (spr->FakeFlatStat == FAKED_AboveCeiling)
			{ // seen above ceiling: clip bottom
				if (!spr->bIsVoxel && h < botclip)
				{
					botclip = MAX<short> (0, h);
				}
				hzb = MAX(hzb, hz);
			}
			else
			{ // seen in the middle: clip top
				if (!spr->bIsVoxel && h > topclip)
				{
					topclip = MIN(h, viewheight);
				}
				hzt = MIN(hzt, hz);
			}
		}
	}
	// killough 3/27/98: end special clipping for deep water / fake ceilings
	else if (!spr->bIsVoxel && spr->floorclip)
	{ // [RH] Move floorclip stuff from R_DrawVisSprite to here
		//int clip = ((FLOAT2FIXED(CenterY) - FixedMul (spr->texturemid - (spr->pic->GetHeight() << FRACBITS) + spr->floorclip, spr->yscale)) >> FRACBITS);
		int clip = xs_RoundToInt(CenterY - (spr->texturemid - spr->pic->GetHeight() + spr->floorclip) * spr->yscale);
		if (clip < botclip)
		{
			botclip = MAX<short>(0, clip);
		}
	}

	if (fake3D & FAKE3D_CLIPBOTTOM)
	{
		if (!spr->bIsVoxel)
		{
			double hz = sclipBottom;
			if (spr->fakefloor)
			{
				double floorz = spr->fakefloor->top.plane->Zat0();
				if (ViewPos.Z > floorz && floorz == sclipBottom )
				{
					hz = spr->fakefloor->bottom.plane->Zat0();
				}
			}
			int h = xs_RoundToInt(CenterY - (hz - ViewPos.Z) * scale);
			if (h < botclip)
			{
				botclip = MAX<short>(0, h);
			}
		}
		hzb = MAX(hzb, sclipBottom);
	}
	if (fake3D & FAKE3D_CLIPTOP)
	{
		if (!spr->bIsVoxel)
		{
			double hz = sclipTop;
			if (spr->fakeceiling != NULL)
			{
				double ceilingZ = spr->fakeceiling->bottom.plane->Zat0();
				if (ViewPos.Z < ceilingZ && ceilingZ == sclipTop)
				{
					hz = spr->fakeceiling->top.plane->Zat0();
				}
			}
			int h = xs_RoundToInt(CenterY - (hz - ViewPos.Z) * scale);
			if (h > topclip)
			{
				topclip = short(MIN(h, viewheight));
			}
		}
		hzt = MIN(hzt, sclipTop);
	}

#if 0
	// [RH] Sprites that were split by a drawseg should also be clipped
	// by the sector's floor and ceiling. (Not sure how/if to handle this
	// with fake floors, since those already do clipping.)
	if (spr->bSplitSprite &&
		(spr->heightsec == NULL || (spr->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC)))
	{
		fixed_t h = spr->sector->floorplane.ZatPoint (spr->gx, spr->gy);
		h = (centeryfrac - FixedMul (h-viewz, scale)) >> FRACBITS;
		if (h < botclip)
		{
			botclip = MAX<short> (0, h);
		}
		h = spr->sector->ceilingplane.ZatPoint (spr->gx, spr->gy);
		h = (centeryfrac - FixedMul (h-viewz, scale)) >> FRACBITS;
		if (h > topclip)
		{
			topclip = short(MIN(h, viewheight));
		}
	}
#endif

	if (topclip >= botclip)
	{
		spr->Style.BaseColormap = colormap;
		spr->Style.ColormapNum = colormapnum;
		return;
	}

	i = x2 - x1;
	clip1 = clipbot + x1;
	clip2 = cliptop + x1;
	do
	{
		*clip1++ = botclip;
		*clip2++ = topclip;
	} while (--i);

	// Scan drawsegs from end to start for obscuring segs.
	// The first drawseg that is closer than the sprite is the clip seg.

	// Modified by Lee Killough:
	// (pointer check was originally nonportable
	// and buggy, by going past LEFT end of array):

	//		for (ds=ds_p-1 ; ds >= drawsegs ; ds--)    old buggy code

	for (ds = ds_p; ds-- > firstdrawseg; )  // new -- killough
	{
		// [ZZ] portal handling here
		//if (ds->CurrentPortalUniq != spr->CurrentPortalUniq)
		//	continue;
		// [ZZ] WARNING: uncommenting the two above lines, totally breaks sprite clipping

		// kg3D - no clipping on fake segs
		if (ds->fake) continue;
		// determine if the drawseg obscures the sprite
		if (ds->x1 >= x2 || ds->x2 <= x1 ||
			(!(ds->silhouette & SIL_BOTH) && ds->maskedtexturecol == -1 &&
			 !ds->bFogBoundary) )
		{
			// does not cover sprite
			continue;
		}

		r1 = MAX<int> (ds->x1, x1);
		r2 = MIN<int> (ds->x2, x2);

		float neardepth, fardepth;
		if (!spr->bWallSprite)
		{
			if (ds->sz1 < ds->sz2)
			{
				neardepth = ds->sz1, fardepth = ds->sz2;
			}
			else
			{
				neardepth = ds->sz2, fardepth = ds->sz1;
			}
		}
		// Check if sprite is in front of draw seg:
		if ((!spr->bWallSprite && neardepth > spr->depth) || ((spr->bWallSprite || fardepth > spr->depth) &&
			(spr->gpos.Y - ds->curline->v1->fY()) * (ds->curline->v2->fX() - ds->curline->v1->fX()) -
			(spr->gpos.X - ds->curline->v1->fX()) * (ds->curline->v2->fY() - ds->curline->v1->fY()) <= 0))
		{
			// seg is behind sprite, so draw the mid texture if it has one
			if (ds->CurrentPortalUniq == CurrentPortalUniq && // [ZZ] instead, portal uniq check is made here
				(ds->maskedtexturecol != -1 || ds->bFogBoundary))
				R_RenderMaskedSegRange (ds, r1, r2);
			continue;
		}

		// clip this piece of the sprite
		// killough 3/27/98: optimized and made much shorter
		// [RH] Optimized further (at least for VC++;
		// other compilers should be at least as good as before)

		if (ds->silhouette & SIL_BOTTOM) //bottom sil
		{
			clip1 = clipbot + r1;
			clip2 = openings + ds->sprbottomclip + r1 - ds->x1;
			i = r2 - r1;
			do
			{
				if (*clip1 > *clip2)
					*clip1 = *clip2;
				clip1++;
				clip2++;
			} while (--i);
		}

		if (ds->silhouette & SIL_TOP)   // top sil
		{
			clip1 = cliptop + r1;
			clip2 = openings + ds->sprtopclip + r1 - ds->x1;
			i = r2 - r1;
			do
			{
				if (*clip1 < *clip2)
					*clip1 = *clip2;
				clip1++;
				clip2++;
			} while (--i);
		}
	}

	// all clipping has been performed, so draw the sprite

	if (!spr->bIsVoxel)
	{
		mfloorclip = clipbot;
		mceilingclip = cliptop;
		if (!spr->bWallSprite)
		{
			R_DrawVisSprite(spr);
		}
		else
		{
			R_DrawWallSprite(spr);
		}
	}
	else
	{
		// If it is completely clipped away, don't bother drawing it.
		if (cliptop[x2] >= clipbot[x2])
		{
			for (i = x1; i < x2; ++i)
			{
				if (cliptop[i] < clipbot[i])
				{
					break;
				}
			}
			if (i == x2)
			{
				spr->Style.BaseColormap = colormap;
				spr->Style.ColormapNum = colormapnum;
				return;
			}
		}
		// Add everything outside the left and right edges to the clipping array
		// for R_DrawVisVoxel().
		if (x1 > 0)
		{
			fillshort(cliptop, x1, viewheight);
		}
		if (x2 < viewwidth - 1)
		{
			fillshort(cliptop + x2, viewwidth - x2, viewheight);
		}
		int minvoxely = spr->gzt <= hzt ? 0 : xs_RoundToInt((spr->gzt - hzt) / spr->yscale);
		int maxvoxely = spr->gzb > hzb ? INT_MAX : xs_RoundToInt((spr->gzt - hzb) / spr->yscale);
		R_DrawVisVoxel(spr, minvoxely, maxvoxely, cliptop, clipbot);
	}
	spr->Style.BaseColormap = colormap;
	spr->Style.ColormapNum = colormapnum;
}

// kg3D:
// R_DrawMasked contains sorting
// original renamed to R_DrawMaskedSingle

void R_DrawMaskedSingle (bool renew)
{
	drawseg_t *ds;
	int i;

#if 0
	R_SplitVisSprites ();
#endif

	for (i = vsprcount; i > 0; i--)
	{
		if (spritesorter[i-1]->CurrentPortalUniq != CurrentPortalUniq)
			continue; // probably another time
		R_DrawSprite (spritesorter[i-1]);
	}

	// render any remaining masked mid textures

	// Modified by Lee Killough:
	// (pointer check was originally nonportable
	// and buggy, by going past LEFT end of array):

	//		for (ds=ds_p-1 ; ds >= drawsegs ; ds--)    old buggy code

	if (renew)
	{
		fake3D |= FAKE3D_REFRESHCLIP;
	}
	for (ds = ds_p; ds-- > firstdrawseg; )	// new -- killough
	{
		// [ZZ] the same as above
		if (ds->CurrentPortalUniq != CurrentPortalUniq)
			continue;
		// kg3D - no fake segs
		if (ds->fake) continue;
		if (ds->maskedtexturecol != -1 || ds->bFogBoundary)
		{
			R_RenderMaskedSegRange (ds, ds->x1, ds->x2);
		}
	}
}

void R_DrawHeightPlanes(double height); // kg3D - fake planes

void R_DrawMasked (void)
{
	R_CollectPortals();
	R_SortVisSprites (DrewAVoxel ? sv_compare2d : sv_compare, firstvissprite - vissprites);

	if (height_top == NULL)
	{ // kg3D - no visible 3D floors, normal rendering
		R_DrawMaskedSingle(false);
	}
	else
	{ // kg3D - correct sorting
		HeightLevel *hl;

		// ceilings
		for (hl = height_cur; hl != NULL && hl->height >= ViewPos.Z; hl = hl->prev)
		{
			if (hl->next)
			{
				fake3D = FAKE3D_CLIPBOTTOM | FAKE3D_CLIPTOP;
				sclipTop = hl->next->height;
			}
			else
			{
				fake3D = FAKE3D_CLIPBOTTOM;
			}
			sclipBottom = hl->height;
			R_DrawMaskedSingle(true);
			R_DrawHeightPlanes(hl->height);
		}

		// floors
		fake3D = FAKE3D_DOWN2UP | FAKE3D_CLIPTOP;
		sclipTop = height_top->height;
		R_DrawMaskedSingle(true);
		hl = height_top;
		for (hl = height_top; hl != NULL && hl->height < ViewPos.Z; hl = hl->next)
		{
			R_DrawHeightPlanes(hl->height);
			if (hl->next)
			{
				fake3D = FAKE3D_DOWN2UP | FAKE3D_CLIPTOP | FAKE3D_CLIPBOTTOM;
				sclipTop = hl->next->height;
			}
			else
			{
				fake3D = FAKE3D_DOWN2UP | FAKE3D_CLIPBOTTOM;
			}
			sclipBottom = hl->height;
			R_DrawMaskedSingle(true);
		}
		R_3D_DeleteHeights();
		fake3D = 0;
	}
	R_DrawPlayerSprites();
}

extern double BaseYaspectMul;;

inline int sgn(int v)
{
	return v < 0 ? -1 : v > 0 ? 1 : 0;
}

#if 0
void R_DrawVoxel(const FVector3 &globalpos, FAngle viewangle,
	const FVector3 &dasprpos, DAngle dasprang,
	fixed_t daxscale, fixed_t dayscale, FVoxel *voxobj,
	FSWColormap *colormap, int colormapnum, short *daumost, short *dadmost, int minslabz, int maxslabz, int flags)
{
	int i, j, k, x, y, syoff, ggxstart, ggystart, nxoff;
	fixed_t cosang, sinang, sprcosang, sprsinang;
	int backx, backy, gxinc, gyinc;
	int daxscalerecip, dayscalerecip, cnt, gxstart, gystart, dazscale;
	int lx, rx, nx, ny, x1=0, y1=0, x2=0, y2=0, yinc=0;
	int yoff, xs=0, ys=0, xe, ye, xi=0, yi=0, cbackx, cbacky, dagxinc, dagyinc;
	kvxslab_t *voxptr, *voxend;
	FVoxelMipLevel *mip;
	int z1a[64], z2a[64], yplc[64];

	const int nytooclose = centerxwide * 2100, nytoofar = 32768*32768 - 1048576;
	const int xdimenscale = FLOAT2FIXED(centerxwide * YaspectMul / 160);
	const double centerxwide_f = centerxwide;
	const double centerxwidebig_f = centerxwide_f * 65536*65536*8;

	// Convert to Build's coordinate system.
	fixed_t globalposx = xs_Fix<4>::ToFix(globalpos.X);
	fixed_t globalposy = xs_Fix<4>::ToFix(-globalpos.Y);
	fixed_t globalposz = xs_Fix<8>::ToFix(-globalpos.Z);

	fixed_t dasprx = xs_Fix<4>::ToFix(dasprpos.X);
	fixed_t daspry = xs_Fix<4>::ToFix(-dasprpos.Y);
	fixed_t dasprz = xs_Fix<8>::ToFix(-dasprpos.Z);

	// Shift the scales from 16 bits of fractional precision to 6.
	// Also do some magic voodoo scaling to make them the right size.
	daxscale = daxscale / (0xC000 >> 6);
	dayscale = dayscale / (0xC000 >> 6);
	if (daxscale <= 0 || dayscale <= 0)
	{
		// won't be visible.
		return;
	}

	angle_t viewang = viewangle.BAMs();
	cosang = FLOAT2FIXED(viewangle.Cos()) >> 2;
	sinang = FLOAT2FIXED(-viewangle.Sin()) >> 2;
	sprcosang = FLOAT2FIXED(dasprang.Cos()) >> 2;
	sprsinang = FLOAT2FIXED(-dasprang.Sin()) >> 2;

	R_SetupDrawSlab(colormap, 0.0f, colormapnum << FRACBITS);

	int pixelsize = r_swtruecolor ? 4 : 1;

	// Select mip level
	i = abs(DMulScale6(dasprx - globalposx, cosang, daspry - globalposy, sinang));
	i = DivScale6(i, MIN(daxscale, dayscale));
	j = xs_Fix<13>::ToFix(FocalLengthX);
	for (k = 0; i >= j && k < voxobj->NumMips; ++k)
	{
		i >>= 1;
	}
	if (k >= voxobj->NumMips) k = voxobj->NumMips - 1;

	mip = &voxobj->Mips[k];		if (mip->SlabData == NULL) return;

	minslabz >>= k;
	maxslabz >>= k;

	daxscale <<= (k+8); dayscale <<= (k+8);
	dazscale = FixedDiv(dayscale, FLOAT2FIXED(BaseYaspectMul));
	daxscale = fixed_t(daxscale / YaspectMul);
	daxscale = Scale(daxscale, xdimenscale, centerxwide << 9);
	dayscale = Scale(dayscale, FixedMul(xdimenscale, viewingrangerecip), centerxwide << 9);

	daxscalerecip = (1<<30) / daxscale;
	dayscalerecip = (1<<30) / dayscale;

	fixed_t piv_x = fixed_t(mip->Pivot.X*256.);
	fixed_t piv_y = fixed_t(mip->Pivot.Y*256.);
	fixed_t piv_z = fixed_t(mip->Pivot.Z*256.);

	x = FixedMul(globalposx - dasprx, daxscalerecip);
	y = FixedMul(globalposy - daspry, daxscalerecip);
	backx = (DMulScale10(x, sprcosang, y,  sprsinang) + piv_x) >> 8;
	backy = (DMulScale10(y, sprcosang, x, -sprsinang) + piv_y) >> 8;
	cbackx = clamp(backx, 0, mip->SizeX - 1);
	cbacky = clamp(backy, 0, mip->SizeY - 1);

	sprcosang = MulScale14(daxscale, sprcosang);
	sprsinang = MulScale14(daxscale, sprsinang);

	x = (dasprx - globalposx) - DMulScale18(piv_x, sprcosang, piv_y, -sprsinang);
	y = (daspry - globalposy) - DMulScale18(piv_y, sprcosang, piv_x,  sprsinang);

	cosang = FixedMul(cosang, dayscalerecip);
	sinang = FixedMul(sinang, dayscalerecip);

	gxstart = y*cosang - x*sinang;
	gystart = x*cosang + y*sinang;
	gxinc = DMulScale10(sprsinang, cosang, sprcosang, -sinang);
	gyinc = DMulScale10(sprcosang, cosang, sprsinang,  sinang);
	if ((abs(globalposz - dasprz) >> 10) >= abs(dazscale)) return;

	x = 0; y = 0; j = MAX(mip->SizeX, mip->SizeY);
	fixed_t *ggxinc = (fixed_t *)alloca((j + 1) * sizeof(fixed_t) * 2);
	fixed_t *ggyinc = ggxinc + (j + 1);
	for (i = 0; i <= j; i++)
	{
		ggxinc[i] = x; x += gxinc;
		ggyinc[i] = y; y += gyinc;
	}

	syoff = DivScale21(globalposz - dasprz, FixedMul(dazscale, 0xE800)) + (piv_z << 7);
	yoff = (abs(gxinc) + abs(gyinc)) >> 1;

	for (cnt = 0; cnt < 8; cnt++)
	{
		switch (cnt)
		{
			case 0: xs = 0;				ys = 0;				xi =  1; yi =  1; break;
			case 1: xs = mip->SizeX-1;	ys = 0;				xi = -1; yi =  1; break;
			case 2: xs = 0;				ys = mip->SizeY-1;	xi =  1; yi = -1; break;
			case 3: xs = mip->SizeX-1;	ys = mip->SizeY-1;	xi = -1; yi = -1; break;
			case 4: xs = 0;				ys = cbacky;		xi =  1; yi =  2; break;
			case 5: xs = mip->SizeX-1;	ys = cbacky;		xi = -1; yi =  2; break;
			case 6: xs = cbackx;		ys = 0;				xi =  2; yi =  1; break;
			case 7: xs = cbackx;		ys = mip->SizeY-1;	xi =  2; yi = -1; break;
		}
		xe = cbackx; ye = cbacky;
		if (cnt < 4)
		{
			if ((xi < 0) && (xe >= xs)) continue;
			if ((xi > 0) && (xe <= xs)) continue;
			if ((yi < 0) && (ye >= ys)) continue;
			if ((yi > 0) && (ye <= ys)) continue;
		}
		else
		{
			if ((xi < 0) && (xe > xs)) continue;
			if ((xi > 0) && (xe < xs)) continue;
			if ((yi < 0) && (ye > ys)) continue;
			if ((yi > 0) && (ye < ys)) continue;
			xe += xi; ye += yi;
		}

		i = sgn(ys - backy) + sgn(xs - backx) * 3 + 4;
		switch(i)
		{
			case 6: case 7: x1 = 0;				y1 = 0;				break;
			case 8: case 5: x1 = gxinc;			y1 = gyinc;			break;
			case 0: case 3: x1 = gyinc;			y1 = -gxinc;		break;
			case 2: case 1: x1 = gxinc+gyinc;	y1 = gyinc-gxinc;	break;
		}
		switch(i)
		{
			case 2: case 5: x2 = 0;				y2 = 0;				break;
			case 0: case 1: x2 = gxinc;			y2 = gyinc;			break;
			case 8: case 7: x2 = gyinc;			y2 = -gxinc;		break;
			case 6: case 3: x2 = gxinc+gyinc;	y2 = gyinc-gxinc;	break;
		}
		BYTE oand = (1 << int(xs<backx)) + (1 << (int(ys<backy)+2));
		BYTE oand16 = oand + 16;
		BYTE oand32 = oand + 32;

		if (yi > 0) { dagxinc =  gxinc; dagyinc =  FixedMul(gyinc, viewingrangerecip); }
			   else { dagxinc = -gxinc; dagyinc = -FixedMul(gyinc, viewingrangerecip); }

			/* Fix for non 90 degree viewing ranges */
		nxoff = FixedMul(x2 - x1, viewingrangerecip);
		x1 = FixedMul(x1, viewingrangerecip);

		ggxstart = gxstart + ggyinc[ys];
		ggystart = gystart - ggxinc[ys];

		for (x = xs; x != xe; x += xi)
		{
			BYTE *slabxoffs = &mip->SlabData[mip->OffsetX[x]];
			short *xyoffs = &mip->OffsetXY[x * (mip->SizeY + 1)];

			nx = FixedMul(ggxstart + ggxinc[x], viewingrangerecip) + x1;
			ny = ggystart + ggyinc[x];
			for (y = ys; y != ye; y += yi, nx += dagyinc, ny -= dagxinc)
			{
				if ((ny <= nytooclose) || (ny >= nytoofar)) continue;
				voxptr = (kvxslab_t *)(slabxoffs + xyoffs[y]);
				voxend = (kvxslab_t *)(slabxoffs + xyoffs[y+1]);
				if (voxptr >= voxend) continue;

				lx = xs_RoundToInt(nx * centerxwide_f / (ny + y1)) + centerx;
				if (lx < 0) lx = 0;
				rx = xs_RoundToInt((nx + nxoff) * centerxwide_f / (ny + y2)) + centerx;
				if (rx > viewwidth) rx = viewwidth;
				if (rx <= lx) continue;

				if (flags & DVF_MIRRORED)
				{
					int t = viewwidth - lx;
					lx = viewwidth - rx;
					rx = t;
				}

				fixed_t l1 = xs_RoundToInt(centerxwidebig_f / (ny - yoff));
				fixed_t l2 = xs_RoundToInt(centerxwidebig_f / (ny + yoff));
				for (; voxptr < voxend; voxptr = (kvxslab_t *)((BYTE *)voxptr + voxptr->zleng + 3))
				{
					const BYTE *col = voxptr->col;
					int zleng = voxptr->zleng;
					int ztop = voxptr->ztop;
					fixed_t z1, z2;

					if (ztop < minslabz)
					{
						int diff = minslabz - ztop;
						ztop = minslabz;
						col += diff;
						zleng -= diff;
					}
					if (ztop + zleng > maxslabz)
					{
						int diff = ztop + zleng - maxslabz;
						zleng -= diff;
					}
					if (zleng <= 0) continue;

					j = (ztop << 15) - syoff;
					if (j < 0)
					{
						k = j + (zleng << 15);
						if (k < 0)
						{
							if ((voxptr->backfacecull & oand32) == 0) continue;
							z2 = MulScale32(l2, k) + centery;					/* Below slab */
						}
						else
						{
							if ((voxptr->backfacecull & oand) == 0) continue;	/* Middle of slab */
							z2 = MulScale32(l1, k) + centery;
						}
						z1 = MulScale32(l1, j) + centery;
					}
					else
					{
						if ((voxptr->backfacecull & oand16) == 0) continue;
						z1 = MulScale32(l2, j) + centery;						/* Above slab */
						z2 = MulScale32(l1, j + (zleng << 15)) + centery;
					}

					if (z2 <= z1) continue;

					if (zleng == 1)
					{
						yinc = 0;
					}
					else
					{
						if (z2-z1 >= 1024) yinc = FixedDiv(zleng, z2 - z1);
						else yinc = (((1 << 24) - 1) / (z2 - z1)) * zleng >> 8;
					}
					// [RH] Clip each column separately, not just by the first one.
					for (int stripwidth = MIN<int>(countof(z1a), rx - lx), lxt = lx;
						lxt < rx;
						(lxt += countof(z1a)), stripwidth = MIN<int>(countof(z1a), rx - lxt))
					{
						// Calculate top and bottom pixels locations
						for (int xxx = 0; xxx < stripwidth; ++xxx)
						{
							if (zleng == 1)
							{
								yplc[xxx] = 0;
								z1a[xxx] = MAX<int>(z1, daumost[lxt + xxx]);
							}
							else
							{
								if (z1 < daumost[lxt + xxx])
								{
									yplc[xxx] = yinc * (daumost[lxt + xxx] - z1);
									z1a[xxx] = daumost[lxt + xxx];
								}
								else
								{
									yplc[xxx] = 0;
									z1a[xxx] = z1;
								}
							}
							z2a[xxx] = MIN<int>(z2, dadmost[lxt + xxx]);
						}
						// Find top and bottom pixels that match and draw them as one strip
						for (int xxl = 0, xxr; xxl < stripwidth; )
						{
							if (z1a[xxl] >= z2a[xxl])
							{ // No column here
								xxl++;
								continue;
							}
							int z1 = z1a[xxl];
							int z2 = z2a[xxl];
							// How many columns share the same extents?
							for (xxr = xxl + 1; xxr < stripwidth; ++xxr)
							{
								if (z1a[xxr] != z1 || z2a[xxr] != z2)
									break;
							}

							if (!(flags & DVF_OFFSCREEN))
							{
								// Draw directly to the screen.
								R_DrawSlab(xxr - xxl, yplc[xxl], z2 - z1, yinc, col, (ylookup[z1] + lxt + xxl) * pixelsize + dc_destorg);
							}
							else
							{
								// Record the area covered and possibly draw to an offscreen buffer.
								dc_yl = z1;
								dc_yh = z2 - 1;
								dc_count = z2 - z1;
								dc_iscale = yinc;
								for (int x = xxl; x < xxr; ++x)
								{
									OffscreenCoverageBuffer->InsertSpan(lxt + x, z1, z2);
									if (!(flags & DVF_SPANSONLY))
									{
										dc_x = lxt + x;
										rt_initcols(OffscreenColorBuffer + (dc_x & ~3) * OffscreenBufferHeight);
										dc_source = col;
										dc_source2 = nullptr;
										dc_texturefrac = yplc[xxl];
										hcolfunc_pre();
									}
								}
							}
							xxl = xxr;
						}
					}
				}
			}
		}
	}
}
#endif

//==========================================================================
//
// FCoverageBuffer Constructor
//
//==========================================================================

FCoverageBuffer::FCoverageBuffer(int lists)
	: Spans(NULL), FreeSpans(NULL)
{
	NumLists = lists;
	Spans = new Span *[lists];
	memset(Spans, 0, sizeof(Span*)*lists);
}

//==========================================================================
//
// FCoverageBuffer Destructor
//
//==========================================================================

FCoverageBuffer::~FCoverageBuffer()
{
	if (Spans != NULL)
	{
		delete[] Spans;
	}
}

//==========================================================================
//
// FCoverageBuffer :: Clear
//
//==========================================================================

void FCoverageBuffer::Clear()
{
	SpanArena.FreeAll();
	memset(Spans, 0, sizeof(Span*)*NumLists);
	FreeSpans = NULL;
}

//==========================================================================
//
// FCoverageBuffer :: InsertSpan
//
// start is inclusive.
// stop is exclusive.
//
//==========================================================================

void FCoverageBuffer::InsertSpan(int listnum, int start, int stop)
{
	assert(unsigned(listnum) < NumLists);
	assert(start < stop);

	Span **span_p = &Spans[listnum];
	Span *span;

	if (*span_p == NULL || (*span_p)->Start > stop)
	{ // This list is empty or the first entry is after this one, so we can just insert the span.
		goto addspan;
	}

	// Insert the new span in order, merging with existing ones.
	while (*span_p != NULL)
	{
		if ((*span_p)->Stop < start)							// =====		(existing span)
		{ // Span ends before this one starts.					//		  ++++	(new span)
			span_p = &(*span_p)->NextSpan;
			continue;
		}

		// Does the new span overlap or abut the existing one?
		if ((*span_p)->Start <= start)
		{
			if ((*span_p)->Stop >= stop)						// =============
			{ // The existing span completely covers this one.	//     +++++
				return;
			}
extend:		// Extend the existing span with the new one.		// ======
			span = *span_p;										//     +++++++
			span->Stop = stop;									// (or)  +++++

			// Free up any spans we just covered up.
			span_p = &(*span_p)->NextSpan;
			while (*span_p != NULL && (*span_p)->Start <= stop && (*span_p)->Stop <= stop)
			{
				Span *span = *span_p;							// ======  ======
				*span_p = span->NextSpan;						//     +++++++++++++
				span->NextSpan = FreeSpans;
				FreeSpans = span;
			}
			if (*span_p != NULL && (*span_p)->Start <= stop)	// =======         ========
			{ // Our new span connects two existing spans.		//     ++++++++++++++
			  // They should all be collapsed into a single span.
				span->Stop = (*span_p)->Stop;
				span = *span_p;
				*span_p = span->NextSpan;
				span->NextSpan = FreeSpans;
				FreeSpans = span;
			}
			goto check;
		}
		else if ((*span_p)->Start <= stop)						//        =====
		{ // The new span extends the existing span from		//    ++++
		  // the beginning.										// (or) ++++
			(*span_p)->Start = start;
			if ((*span_p)->Stop < stop)
			{ // The new span also extends the existing span	//     ======
			  // at the bottom									// ++++++++++++++
				goto extend;
			}
			goto check;
		}
		else													//         ======
		{ // No overlap, so insert a new span.					// +++++
			goto addspan;
		}
	}
	// Append a new span to the end of the list.
addspan:
	span = AllocSpan();
	span->NextSpan = *span_p;
	span->Start = start;
	span->Stop = stop;
	*span_p = span;
check:
#ifdef _DEBUG
	// Validate the span list: Spans must be in order, and there must be
	// at least one pixel between spans.
	for (span = Spans[listnum]; span != NULL; span = span->NextSpan)
	{
		assert(span->Start < span->Stop);
		if (span->NextSpan != NULL)
		{
			assert(span->Stop < span->NextSpan->Start);
		}
	}
#endif
	;
}

//==========================================================================
//
// FCoverageBuffer :: AllocSpan
//
//==========================================================================

FCoverageBuffer::Span *FCoverageBuffer::AllocSpan()
{
	Span *span;

	if (FreeSpans != NULL)
	{
		span = FreeSpans;
		FreeSpans = span->NextSpan;
	}
	else
	{
		span = (Span *)SpanArena.Alloc(sizeof(Span));
	}
	return span;
}

//==========================================================================
//
// R_CheckOffscreenBuffer
//
// Allocates the offscreen coverage buffer and optionally the offscreen
// color buffer. If they already exist but are the wrong size, they will
// be reallocated.
//
//==========================================================================

void R_CheckOffscreenBuffer(int width, int height, bool spansonly)
{
	if (OffscreenCoverageBuffer == NULL)
	{
		assert(OffscreenColorBuffer == NULL && "The color buffer cannot exist without the coverage buffer");
		OffscreenCoverageBuffer = new FCoverageBuffer(width);
	}
	else if (OffscreenCoverageBuffer->NumLists != (unsigned)width)
	{
		delete OffscreenCoverageBuffer;
		OffscreenCoverageBuffer = new FCoverageBuffer(width);
		if (OffscreenColorBuffer != NULL)
		{
			delete[] OffscreenColorBuffer;
			OffscreenColorBuffer = NULL;
		}
	}
	else
	{
		OffscreenCoverageBuffer->Clear();
	}

	if (!spansonly)
	{
		if (OffscreenColorBuffer == NULL)
		{
			OffscreenColorBuffer = new BYTE[width * height * 4];
		}
		else if (OffscreenBufferWidth != width || OffscreenBufferHeight != height)
		{
			delete[] OffscreenColorBuffer;
			OffscreenColorBuffer = new BYTE[width * height * 4];
		}
	}
	OffscreenBufferWidth = width;
	OffscreenBufferHeight = height;
}

}