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
// DESCRIPTION:
//		All the clipping: columns, horizontal spans, sky columns.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <stddef.h>

#include "templates.h"
#include "i_system.h"

#include "doomdef.h"
#include "doomstat.h"
#include "doomdata.h"
#include "p_lnspec.h"

#include "swrenderer/r_main.h"
#include "swrenderer/scene/r_things.h"
#include "r_sky.h"
#include "v_video.h"

#include "m_swap.h"
#include "w_wad.h"
#include "stats.h"
#include "a_sharedglobal.h"
#include "d_net.h"
#include "g_level.h"
#include "r_bsp.h"
#include "r_plane.h"
#include "r_fogboundary.h"
#include "r_segs.h"
#include "r_3dfloors.h"
#include "swrenderer/drawers/r_draw.h"
#include "v_palette.h"
#include "r_data/colormaps.h"
#include "r_walldraw.h"
#include "r_draw_segment.h"
#include "r_portal.h"
#include "r_wallsprite.h"
#include "r_decal.h"
#include "swrenderer/r_memory.h"

#define WALLYREPEAT 8


CVAR(Bool, r_fogboundary, true, 0)
CVAR(Bool, r_drawmirrors, true, 0)
EXTERN_CVAR(Bool, r_fullbrightignoresectorcolor);
EXTERN_CVAR(Bool, r_mipmap)

namespace swrenderer
{
	using namespace drawerargs;

	void R_DrawDrawSeg(drawseg_t *ds, int x1, int x2, short *uwal, short *dwal, float *swal, fixed_t *lwal, double yrepeat);

#define HEIGHTBITS 12
#define HEIGHTSHIFT (FRACBITS-HEIGHTBITS)

extern double globaluclip, globaldclip;


// OPTIMIZE: closed two sided lines as single sided

// killough 1/6/98: replaced globals with statics where appropriate

static bool		segtextured;	// True if any of the segs textures might be visible.
bool		markfloor;		// False if the back side is the same plane.
bool		markceiling;
FTexture *toptexture;
FTexture *bottomtexture;
FTexture *midtexture;
fixed_t rw_offset_top;
fixed_t rw_offset_mid;
fixed_t rw_offset_bottom;


int		wallshade;

short	walltop[MAXWIDTH];	// [RH] record max extents of wall
short	wallbottom[MAXWIDTH];
short	wallupper[MAXWIDTH];
short	walllower[MAXWIDTH];
float	swall[MAXWIDTH];
fixed_t	lwall[MAXWIDTH];
double	lwallscale;

//
// regular wall
//
extern double	rw_backcz1, rw_backcz2;
extern double	rw_backfz1, rw_backfz2;
extern double	rw_frontcz1, rw_frontcz2;
extern double	rw_frontfz1, rw_frontfz2;

int				rw_ceilstat, rw_floorstat;
bool			rw_mustmarkfloor, rw_mustmarkceiling;
bool			rw_prepped;
bool			rw_markportal;
bool			rw_havehigh;
bool			rw_havelow;

float			rw_light;		// [RH] Scale lights with viewsize adjustments
float			rw_lightstep;
float			rw_lightleft;

static double	rw_frontlowertop;

static int		rw_x;
static int		rw_stopx;
fixed_t			rw_offset;
static double	rw_scalestep;
static double	rw_midtexturemid;
static double	rw_toptexturemid;
static double	rw_bottomtexturemid;
static double	rw_midtexturescalex;
static double	rw_midtexturescaley;
static double	rw_toptexturescalex;
static double	rw_toptexturescaley;
static double	rw_bottomtexturescalex;
static double	rw_bottomtexturescaley;

FTexture		*rw_pic;

static fixed_t	*maskedtexturecol;


inline bool IsFogBoundary (sector_t *front, sector_t *back)
{
	return r_fogboundary && fixedcolormap == NULL && front->ColorMap->Fade &&
		front->ColorMap->Fade != back->ColorMap->Fade &&
		(front->GetTexture(sector_t::ceiling) != skyflatnum || back->GetTexture(sector_t::ceiling) != skyflatnum);
}


//
// R_RenderMaskedSegRange
//
float *MaskedSWall;
float MaskedScaleY;

static void BlastMaskedColumn (FTexture *tex)
{
	// calculate lighting
	if (fixedcolormap == NULL && fixedlightlev < 0)
	{
		R_SetColorMapLight(basecolormap, rw_light, wallshade);
	}

	dc_iscale = xs_Fix<16>::ToFix(MaskedSWall[dc_x] * MaskedScaleY);
 	if (sprflipvert)
		sprtopscreen = CenterY + dc_texturemid * spryscale;
	else
		sprtopscreen = CenterY - dc_texturemid * spryscale;
	
	// killough 1/25/98: here's where Medusa came in, because
	// it implicitly assumed that the column was all one patch.
	// Originally, Doom did not construct complete columns for
	// multipatched textures, so there were no header or trailer
	// bytes in the column referred to below, which explains
	// the Medusa effect. The fix is to construct true columns
	// when forming multipatched textures (see r_data.c).

	// draw the texture
	R_DrawMaskedColumn(tex, maskedtexturecol[dc_x]);
	rw_light += rw_lightstep;
	spryscale += rw_scalestep;
}

// Clip a midtexture to the floor and ceiling of the sector in front of it.
void ClipMidtex(int x1, int x2)
{
	short most[MAXWIDTH];

	R_CreateWallSegmentYSloped(most, curline->frontsector->ceilingplane, &WallC);
	for (int i = x1; i < x2; ++i)
	{
		if (wallupper[i] < most[i])
			wallupper[i] = most[i];
	}
	R_CreateWallSegmentYSloped(most, curline->frontsector->floorplane, &WallC);
	for (int i = x1; i < x2; ++i)
	{
		if (walllower[i] > most[i])
			walllower[i] = most[i];
	}
}

void R_RenderFakeWallRange(drawseg_t *ds, int x1, int x2);

void R_RenderMaskedSegRange (drawseg_t *ds, int x1, int x2)
{
	FTexture	*tex;
	int			i;
	sector_t	tempsec;		// killough 4/13/98
	double		texheight, texheightscale;
	bool		notrelevant = false;
	double		rowoffset;
	bool		wrap = false;

	const sector_t *sec;

	sprflipvert = false;

	curline = ds->curline;

	bool visible = R_SetPatchStyle (LegacyRenderStyles[curline->linedef->flags & ML_ADDTRANS ? STYLE_Add : STYLE_Translucent],
		(float)MIN(curline->linedef->alpha, 1.),	0, 0);

	if (!visible && !ds->bFogBoundary && !ds->bFakeBoundary)
	{
		return;
	}

	NetUpdate ();

	frontsector = curline->frontsector;
	backsector = curline->backsector;

	tex = TexMan(curline->sidedef->GetTexture(side_t::mid), true);
	if (i_compatflags & COMPATF_MASKEDMIDTEX)
	{
		tex = tex->GetRawTexture();
	}

	// killough 4/13/98: get correct lightlevel for 2s normal textures
	sec = R_FakeFlat (frontsector, &tempsec, NULL, NULL, false);

	basecolormap = sec->ColorMap;	// [RH] Set basecolormap

	wallshade = ds->shade;
	rw_lightstep = ds->lightstep;
	rw_light = ds->light + (x1 - ds->x1) * rw_lightstep;

	if (fixedlightlev < 0)
	{
		if (!(fake3D & FAKE3D_CLIPTOP))
		{
			sclipTop = sec->ceilingplane.ZatPoint(ViewPos);
		}
		for (i = frontsector->e->XFloor.lightlist.Size() - 1; i >= 0; i--)
		{
			if (sclipTop <= frontsector->e->XFloor.lightlist[i].plane.Zat0())
			{
				lightlist_t *lit = &frontsector->e->XFloor.lightlist[i];
				basecolormap = lit->extra_colormap;
				wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
				break;
			}
		}
	}

	mfloorclip = openings + ds->sprbottomclip - ds->x1;
	mceilingclip = openings + ds->sprtopclip - ds->x1;

	// [RH] Draw fog partition
	if (ds->bFogBoundary)
	{
		R_DrawFogBoundary (x1, x2, mceilingclip, mfloorclip);
		if (ds->maskedtexturecol == -1)
		{
			goto clearfog;
		}
	}
	if ((ds->bFakeBoundary && !(ds->bFakeBoundary & 4)) || !visible)
	{
		goto clearfog;
	}

	MaskedSWall = (float *)(openings + ds->swall) - ds->x1;
	MaskedScaleY = ds->yscale;
	maskedtexturecol = (fixed_t *)(openings + ds->maskedtexturecol) - ds->x1;
	spryscale = ds->iscale + ds->iscalestep * (x1 - ds->x1);
	rw_scalestep = ds->iscalestep;

	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : basecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);

	// find positioning
	texheight = tex->GetScaledHeightDouble();
	texheightscale = fabs(curline->sidedef->GetTextureYScale(side_t::mid));
	if (texheightscale != 1)
	{
		texheight = texheight / texheightscale;
	}
	if (curline->linedef->flags & ML_DONTPEGBOTTOM)
	{
		dc_texturemid = MAX(frontsector->GetPlaneTexZ(sector_t::floor), backsector->GetPlaneTexZ(sector_t::floor)) + texheight;
	}
	else
	{
		dc_texturemid = MIN(frontsector->GetPlaneTexZ(sector_t::ceiling), backsector->GetPlaneTexZ(sector_t::ceiling));
	}

	rowoffset = curline->sidedef->GetTextureYOffset(side_t::mid);

	wrap = (curline->linedef->flags & ML_WRAP_MIDTEX) || (curline->sidedef->Flags & WALLF_WRAP_MIDTEX);
	if (!wrap)
	{ // Texture does not wrap vertically.
		double textop;

		if (MaskedScaleY < 0)
		{
			MaskedScaleY = -MaskedScaleY;
			sprflipvert = true;
		}
		if (tex->bWorldPanning)
		{
			// rowoffset is added before the multiply so that the masked texture will
			// still be positioned in world units rather than texels.
			dc_texturemid += rowoffset - ViewPos.Z;
			textop = dc_texturemid;
			dc_texturemid *= MaskedScaleY;
		}
		else
		{
			// rowoffset is added outside the multiply so that it positions the texture
			// by texels instead of world units.
			textop = dc_texturemid + rowoffset / MaskedScaleY - ViewPos.Z;
			dc_texturemid = (dc_texturemid - ViewPos.Z) * MaskedScaleY + rowoffset;
		}
		if (sprflipvert)
		{
			MaskedScaleY = -MaskedScaleY;
			dc_texturemid -= tex->GetHeight() << FRACBITS;
		}

		// [RH] Don't bother drawing segs that are completely offscreen
		if (globaldclip * ds->sz1 < -textop && globaldclip * ds->sz2 < -textop)
		{ // Texture top is below the bottom of the screen
			goto clearfog;
		}

		if (globaluclip * ds->sz1 > texheight - textop && globaluclip * ds->sz2 > texheight - textop)
		{ // Texture bottom is above the top of the screen
			goto clearfog;
		}

		if ((fake3D & FAKE3D_CLIPBOTTOM) && textop < sclipBottom - ViewPos.Z)
		{
			notrelevant = true;
			goto clearfog;
		}
		if ((fake3D & FAKE3D_CLIPTOP) && textop - texheight > sclipTop - ViewPos.Z)
		{
			notrelevant = true;
			goto clearfog;
		}

		WallC.sz1 = ds->sz1;
		WallC.sz2 = ds->sz2;
		WallC.sx1 = ds->sx1;
		WallC.sx2 = ds->sx2;

		if (fake3D & FAKE3D_CLIPTOP)
		{
			R_CreateWallSegmentY(wallupper, textop < sclipTop - ViewPos.Z ? textop : sclipTop - ViewPos.Z, &WallC);
		}
		else
		{
			R_CreateWallSegmentY(wallupper, textop, &WallC);
		}
		if (fake3D & FAKE3D_CLIPBOTTOM)
		{
			R_CreateWallSegmentY(walllower, textop - texheight > sclipBottom - ViewPos.Z ? textop - texheight : sclipBottom - ViewPos.Z, &WallC);
		}
		else
		{
			R_CreateWallSegmentY(walllower, textop - texheight, &WallC);
		}

		for (i = x1; i < x2; i++)
		{
			if (wallupper[i] < mceilingclip[i])
				wallupper[i] = mceilingclip[i];
		}
		for (i = x1; i < x2; i++)
		{
			if (walllower[i] > mfloorclip[i])
				walllower[i] = mfloorclip[i];
		}

		if (CurrentSkybox)
		{ // Midtex clipping doesn't work properly with skyboxes, since you're normally below the floor
		  // or above the ceiling, so the appropriate end won't be clipped automatically when adding
		  // this drawseg.
			if ((curline->linedef->flags & ML_CLIP_MIDTEX) ||
				(curline->sidedef->Flags & WALLF_CLIP_MIDTEX))
			{
				ClipMidtex(x1, x2);
			}
		}

		mfloorclip = walllower;
		mceilingclip = wallupper;

		// draw the columns one at a time
		if (visible)
		{
			for (dc_x = x1; dc_x < x2; ++dc_x)
			{
				BlastMaskedColumn (tex);
			}
		}
	}
	else
	{ // Texture does wrap vertically.
		if (tex->bWorldPanning)
		{
			// rowoffset is added before the multiply so that the masked texture will
			// still be positioned in world units rather than texels.
			dc_texturemid = (dc_texturemid - ViewPos.Z + rowoffset) * MaskedScaleY;
		}
		else
		{
			// rowoffset is added outside the multiply so that it positions the texture
			// by texels instead of world units.
			dc_texturemid = (dc_texturemid - ViewPos.Z) * MaskedScaleY + rowoffset;
		}

		WallC.sz1 = ds->sz1;
		WallC.sz2 = ds->sz2;
		WallC.sx1 = ds->sx1;
		WallC.sx2 = ds->sx2;

		if (CurrentSkybox)
		{ // Midtex clipping doesn't work properly with skyboxes, since you're normally below the floor
		  // or above the ceiling, so the appropriate end won't be clipped automatically when adding
		  // this drawseg.
			if ((curline->linedef->flags & ML_CLIP_MIDTEX) ||
				(curline->sidedef->Flags & WALLF_CLIP_MIDTEX))
			{
				ClipMidtex(x1, x2);
			}
		}

		if (fake3D & FAKE3D_CLIPTOP)
		{
			R_CreateWallSegmentY(wallupper, sclipTop - ViewPos.Z, &WallC);
			for (i = x1; i < x2; i++)
			{
				if (wallupper[i] < mceilingclip[i])
					wallupper[i] = mceilingclip[i];
			}
			mceilingclip = wallupper;
		}
		if (fake3D & FAKE3D_CLIPBOTTOM)
		{
			R_CreateWallSegmentY(walllower, sclipBottom - ViewPos.Z, &WallC);
			for (i = x1; i < x2; i++)
			{
				if (walllower[i] > mfloorclip[i])
					walllower[i] = mfloorclip[i];
			}
			mfloorclip = walllower;
		}

		rw_offset = 0;
		rw_pic = tex;
		R_DrawDrawSeg(ds, x1, x2, mceilingclip, mfloorclip, MaskedSWall, maskedtexturecol, ds->yscale);
	}

clearfog:
	R_FinishSetPatchStyle ();
	if (ds->bFakeBoundary & 3)
	{
		R_RenderFakeWallRange(ds, x1, x2);
	}
	if (!notrelevant)
	{
		if (fake3D & FAKE3D_REFRESHCLIP)
		{
			if (!wrap)
			{
				assert(ds->bkup >= 0);
				memcpy(openings + ds->sprtopclip, openings + ds->bkup, (ds->x2 - ds->x1) * 2);
			}
		}
		else
		{
			fillshort(openings + ds->sprtopclip - ds->x1 + x1, x2 - x1, viewheight);
		}
	}
	return;
}

// kg3D - render one fake wall
void R_RenderFakeWall(drawseg_t *ds, int x1, int x2, F3DFloor *rover)
{
	int i;
	double xscale;
	double yscale;

	fixed_t Alpha = Scale(rover->alpha, OPAQUE, 255);
	bool visible = R_SetPatchStyle (LegacyRenderStyles[rover->flags & FF_ADDITIVETRANS ? STYLE_Add : STYLE_Translucent],
		Alpha, 0, 0);

	if(!visible) {
		R_FinishSetPatchStyle();
		return;
	}

	rw_lightstep = ds->lightstep;
	rw_light = ds->light + (x1 - ds->x1) * rw_lightstep;

	mfloorclip = openings + ds->sprbottomclip - ds->x1;
	mceilingclip = openings + ds->sprtopclip - ds->x1;

	spryscale = ds->iscale + ds->iscalestep * (x1 - ds->x1);
	rw_scalestep = ds->iscalestep;
	MaskedSWall = (float *)(openings + ds->swall) - ds->x1;

	// find positioning
	side_t *scaledside;
	side_t::ETexpart scaledpart;
	if (rover->flags & FF_UPPERTEXTURE)
	{
		scaledside = curline->sidedef;
		scaledpart = side_t::top;
	}
	else if (rover->flags & FF_LOWERTEXTURE)
	{
		scaledside = curline->sidedef;
		scaledpart = side_t::bottom;
	}
	else
	{
		scaledside = rover->master->sidedef[0];
		scaledpart = side_t::mid;
	}
	xscale = rw_pic->Scale.X * scaledside->GetTextureXScale(scaledpart);
	yscale = rw_pic->Scale.Y * scaledside->GetTextureYScale(scaledpart);

	double rowoffset = curline->sidedef->GetTextureYOffset(side_t::mid) + rover->master->sidedef[0]->GetTextureYOffset(side_t::mid);
	double planez = rover->model->GetPlaneTexZ(sector_t::ceiling);
	rw_offset = FLOAT2FIXED(curline->sidedef->GetTextureXOffset(side_t::mid) + rover->master->sidedef[0]->GetTextureXOffset(side_t::mid));
	if (rowoffset < 0)
	{
		rowoffset += rw_pic->GetHeight();
	}
	dc_texturemid = (planez - ViewPos.Z) * yscale;
	if (rw_pic->bWorldPanning)
	{
		// rowoffset is added before the multiply so that the masked texture will
		// still be positioned in world units rather than texels.

		dc_texturemid = dc_texturemid + rowoffset * yscale;
		rw_offset = xs_RoundToInt(rw_offset * xscale);
	}
	else
	{
		// rowoffset is added outside the multiply so that it positions the texture
		// by texels instead of world units.
		dc_texturemid += rowoffset;
	}

	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : basecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);

	WallC.sz1 = ds->sz1;
	WallC.sz2 = ds->sz2;
	WallC.sx1 = ds->sx1;
	WallC.sx2 = ds->sx2;
	WallC.tleft.X = ds->cx;
	WallC.tleft.Y = ds->cy;
	WallC.tright.X = ds->cx + ds->cdx;
	WallC.tright.Y = ds->cy + ds->cdy;
	WallT = ds->tmapvals;

	R_CreateWallSegmentY(wallupper, sclipTop - ViewPos.Z, &WallC);
	R_CreateWallSegmentY(walllower, sclipBottom - ViewPos.Z, &WallC);

	for (i = x1; i < x2; i++)
	{
		if (wallupper[i] < mceilingclip[i])
			wallupper[i] = mceilingclip[i];
	}
	for (i = x1; i < x2; i++)
	{
		if (walllower[i] > mfloorclip[i])
			walllower[i] = mfloorclip[i];
	}

	PrepLWall (lwall, curline->sidedef->TexelLength*xscale, ds->sx1, ds->sx2);
	R_DrawDrawSeg(ds, x1, x2, wallupper, walllower, MaskedSWall, lwall, yscale);
	R_FinishSetPatchStyle();
}

// kg3D - walls of fake floors
void R_RenderFakeWallRange (drawseg_t *ds, int x1, int x2)
{
	FTexture *const DONT_DRAW = ((FTexture*)(intptr_t)-1);
	int i,j;
	F3DFloor *rover, *fover = NULL;
	int passed, last;
	double floorHeight;
	double ceilingHeight;

	sprflipvert = false;
	curline = ds->curline;

	frontsector = curline->frontsector;
	backsector = curline->backsector;

	if (backsector == NULL)
	{
		return;
	}
	if ((ds->bFakeBoundary & 3) == 2)
	{
		sector_t *sec = backsector;
		backsector = frontsector;
		frontsector = sec;
	}

	floorHeight = backsector->CenterFloor();
	ceilingHeight = backsector->CenterCeiling();

	// maybe fix clipheights
	if (!(fake3D & FAKE3D_CLIPBOTTOM)) sclipBottom = floorHeight;
	if (!(fake3D & FAKE3D_CLIPTOP))    sclipTop = ceilingHeight;

	// maybe not visible
	if (sclipBottom >= frontsector->CenterCeiling()) return;
	if (sclipTop <= frontsector->CenterFloor()) return;

	if (fake3D & FAKE3D_DOWN2UP)
	{ // bottom to viewz
		last = 0;
		for (i = backsector->e->XFloor.ffloors.Size() - 1; i >= 0; i--) 
		{
			rover = backsector->e->XFloor.ffloors[i];
			if (!(rover->flags & FF_EXISTS)) continue;

			// visible?
			passed = 0;
			if (!(rover->flags & FF_RENDERSIDES) || rover->top.plane->isSlope() || rover->bottom.plane->isSlope() ||
				rover->top.plane->Zat0() <= sclipBottom ||
				rover->bottom.plane->Zat0() >= ceilingHeight ||
				rover->top.plane->Zat0() <= floorHeight)
			{
				if (!i)
				{
					passed = 1;
				}
				else
				{
					continue;
				}
			}

			rw_pic = NULL;
			if (rover->bottom.plane->Zat0() >= sclipTop || passed) 
			{
				if (last)
				{
					break;
				}
				// maybe wall from inside rendering?
				fover = NULL;
				for (j = frontsector->e->XFloor.ffloors.Size() - 1; j >= 0; j--)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model)
					{ // never
						fover = NULL;
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, it's bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->top.plane->Zat0() <= sclipBottom) continue; // no
					if (fover->bottom.plane->Zat0() >= sclipTop)
					{ // no, last possible
 						fover = NULL;
						break;
					}
					// it is, render inside?
					if (!(fover->flags & (FF_BOTHPLANES|FF_INVERTPLANES)))
					{ // no
						fover = NULL;
					}
					break;
				}
				// nothing
				if (!fover || j == -1)
				{
					break;
				}
				// correct texture
				if (fover->flags & rover->flags & FF_SWIMMABLE)
				{	// don't ever draw (but treat as something has been found)
					rw_pic = DONT_DRAW;
				}
				else if(fover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if(fover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(fover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			} 
			else if (frontsector->e->XFloor.ffloors.Size()) 
			{
				// maybe not visible?
				fover = NULL;
				for (j = frontsector->e->XFloor.ffloors.Size() - 1; j >= 0; j--)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model) // never
					{
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, it's bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->top.plane->Zat0() <= sclipBottom) continue; // no
					if (fover->bottom.plane->Zat0() >= sclipTop)
					{ // visible, last possible
 						fover = NULL;
						break;
					}
					if ((fover->flags & FF_SOLID) == (rover->flags & FF_SOLID) &&
						!(!(fover->flags & FF_SOLID) && (fover->alpha == 255 || rover->alpha == 255))
					)
					{
						break;
					}
					if (fover->flags & rover->flags & FF_SWIMMABLE)
					{ // don't ever draw (but treat as something has been found)
						rw_pic = DONT_DRAW;
					}
					fover = NULL; // visible
					break;
				}
				if (fover && j != -1)
				{
					fover = NULL;
					last = 1;
					continue; // not visible
				}
			}
			if (!rw_pic) 
			{
				fover = NULL;
				if (rover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if(rover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(rover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			}
			// correct colors now
			basecolormap = frontsector->ColorMap;
			wallshade = ds->shade;
			if (fixedlightlev < 0)
			{
				if ((ds->bFakeBoundary & 3) == 2)
				{
					for (j = backsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if (sclipTop <= backsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &backsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
				else
				{
					for (j = frontsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if (sclipTop <= frontsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &frontsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
			}
			if (rw_pic != DONT_DRAW)
			{
				R_RenderFakeWall(ds, x1, x2, fover ? fover : rover);
			}
			else rw_pic = NULL;
			break;
		}
	}
	else
	{ // top to viewz
		for (i = 0; i < (int)backsector->e->XFloor.ffloors.Size(); i++)
		{
			rover = backsector->e->XFloor.ffloors[i];
			if (!(rover->flags & FF_EXISTS)) continue;

			// visible?
			passed = 0;
			if (!(rover->flags & FF_RENDERSIDES) ||
				rover->top.plane->isSlope() || rover->bottom.plane->isSlope() ||
				rover->bottom.plane->Zat0() >= sclipTop ||
				rover->top.plane->Zat0() <= floorHeight ||
				rover->bottom.plane->Zat0() >= ceilingHeight)
			{
				if ((unsigned)i == backsector->e->XFloor.ffloors.Size() - 1)
				{
					passed = 1;
				}
				else
				{
					continue;
				}
			}
			rw_pic = NULL;
			if (rover->top.plane->Zat0() <= sclipBottom || passed)
			{ // maybe wall from inside rendering?
				fover = NULL;
				for (j = 0; j < (int)frontsector->e->XFloor.ffloors.Size(); j++)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model)
					{ // never
						fover = NULL;
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, it's bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->bottom.plane->Zat0() >= sclipTop) continue; // no
					if (fover->top.plane->Zat0() <= sclipBottom)
					{ // no, last possible
 						fover = NULL;
						break;
					}
					// it is, render inside?
					if (!(fover->flags & (FF_BOTHPLANES|FF_INVERTPLANES)))
					{ // no
						fover = NULL;
					}
					break;
				}
				// nothing
				if (!fover || (unsigned)j == frontsector->e->XFloor.ffloors.Size())
				{
					break;
				}
				// correct texture
				if (fover->flags & rover->flags & FF_SWIMMABLE)
				{
					rw_pic = DONT_DRAW;	// don't ever draw (but treat as something has been found)
				}
				else if (fover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if (fover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(fover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			}
			else if (frontsector->e->XFloor.ffloors.Size())
			{ // maybe not visible?
				fover = NULL;
				for (j = 0; j < (int)frontsector->e->XFloor.ffloors.Size(); j++)
				{
					fover = frontsector->e->XFloor.ffloors[j];
					if (fover->model == rover->model)
					{ // never
						break;
					}
					if (!(fover->flags & FF_EXISTS)) continue;
					if (!(fover->flags & FF_RENDERSIDES)) continue;
					// no sloped walls, its bugged
					if (fover->top.plane->isSlope() || fover->bottom.plane->isSlope()) continue;

					// visible?
					if (fover->bottom.plane->Zat0() >= sclipTop) continue; // no
					if (fover->top.plane->Zat0() <= sclipBottom)
					{ // visible, last possible
 						fover = NULL;
						break;
					}
					if ((fover->flags & FF_SOLID) == (rover->flags & FF_SOLID) &&
						!(!(rover->flags & FF_SOLID) && (fover->alpha == 255 || rover->alpha == 255))
					)
					{
						break;
					}
					if (fover->flags & rover->flags & FF_SWIMMABLE)
					{ // don't ever draw (but treat as something has been found)
						rw_pic = DONT_DRAW;
					}
					fover = NULL; // visible
					break;
				}
				if (fover && (unsigned)j != frontsector->e->XFloor.ffloors.Size())
				{ // not visible
					break;
				}
			}
			if (rw_pic == NULL)
			{
				fover = NULL;
				if (rover->flags & FF_UPPERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::top), true);
				}
				else if (rover->flags & FF_LOWERTEXTURE)
				{
					rw_pic = TexMan(curline->sidedef->GetTexture(side_t::bottom), true);
				}
				else
				{
					rw_pic = TexMan(rover->master->sidedef[0]->GetTexture(side_t::mid), true);
				}
			}
			// correct colors now
			basecolormap = frontsector->ColorMap;
			wallshade = ds->shade;
			if (fixedlightlev < 0)
			{
				if ((ds->bFakeBoundary & 3) == 2)
				{
					for (j = backsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if (sclipTop <= backsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &backsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
				else
				{
					for (j = frontsector->e->XFloor.lightlist.Size() - 1; j >= 0; j--)
					{
						if(sclipTop <= frontsector->e->XFloor.lightlist[j].plane.Zat0())
						{
							lightlist_t *lit = &frontsector->e->XFloor.lightlist[j];
							basecolormap = lit->extra_colormap;
							wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, *lit->p_lightlevel, lit->lightsource != NULL) + r_actualextralight);
							break;
						}
					}
				}
			}

			if (rw_pic != DONT_DRAW)
			{
				R_RenderFakeWall(ds, x1, x2, fover ? fover : rover);
			}
			else
			{
				rw_pic = NULL;
			}
			break;
		}
	}
	return;
}

//
// R_RenderSegLoop
// Draws zero, one, or two textures for walls.
// Can draw or mark the starting pixel of floor and ceiling textures.
// CALLED: CORE LOOPING ROUTINE.
//

void R_RenderSegLoop ()
{
	int x1 = rw_x;
	int x2 = rw_stopx;
	int x;
	double xscale;
	double yscale;
	fixed_t xoffset = rw_offset;

	if (fixedlightlev >= 0)
		R_SetColorMapLight((r_fullbrightignoresectorcolor) ? &FullNormalLight : basecolormap, 0, FIXEDLIGHT2SHADE(fixedlightlev));
	else if (fixedcolormap != NULL)
		R_SetColorMapLight(fixedcolormap, 0, 0);

	// clip wall to the floor and ceiling
	for (x = x1; x < x2; ++x)
	{
		if (walltop[x] < ceilingclip[x])
		{
			walltop[x] = ceilingclip[x];
		}
		if (wallbottom[x] > floorclip[x])
		{
			wallbottom[x] = floorclip[x];
		}
	}

	// mark ceiling areas
	if (markceiling)
	{
		for (x = x1; x < x2; ++x)
		{
			short top = (fakeFloor && fake3D & 2) ? fakeFloor->ceilingclip[x] : ceilingclip[x];
			short bottom = MIN (walltop[x], floorclip[x]);
			if (top < bottom)
			{
				ceilingplane->top[x] = top;
				ceilingplane->bottom[x] = bottom;
			}
		}
	}

	// mark floor areas
	if (markfloor)
	{
		for (x = x1; x < x2; ++x)
		{
			short top = MAX (wallbottom[x], ceilingclip[x]);
			short bottom = (fakeFloor && fake3D & 1) ? fakeFloor->floorclip[x] : floorclip[x];
			if (top < bottom)
			{
				assert (bottom <= viewheight);
				floorplane->top[x] = top;
				floorplane->bottom[x] = bottom;
			}
		}
	}

	// kg3D - fake planes clipping
	if (fake3D & FAKE3D_REFRESHCLIP)
	{
		if (fake3D & FAKE3D_CLIPBOTFRONT)
		{
			memcpy (fakeFloor->floorclip+x1, wallbottom+x1, (x2-x1)*sizeof(short));
		}
		else
		{
			for (x = x1; x < x2; ++x)
			{
				walllower[x] = MIN (MAX (walllower[x], ceilingclip[x]), wallbottom[x]);
			}
			memcpy (fakeFloor->floorclip+x1, walllower+x1, (x2-x1)*sizeof(short));
		}
		if (fake3D & FAKE3D_CLIPTOPFRONT)
		{
			memcpy (fakeFloor->ceilingclip+x1, walltop+x1, (x2-x1)*sizeof(short));
		}
		else
		{
			for (x = x1; x < x2; ++x)
			{
				wallupper[x] = MAX (MIN (wallupper[x], floorclip[x]), walltop[x]);
			}
			memcpy (fakeFloor->ceilingclip+x1, wallupper+x1, (x2-x1)*sizeof(short));
		}
	}
	if(fake3D & 7) return;

	FLightNode *light_list = (curline && curline->sidedef) ? curline->sidedef->lighthead : nullptr;

	// draw the wall tiers
	if (midtexture)
	{ // one sided line
		if (midtexture->UseType != FTexture::TEX_Null && viewactive)
		{
			dc_texturemid = rw_midtexturemid;
			rw_pic = midtexture;
			xscale = rw_pic->Scale.X * rw_midtexturescalex;
			yscale = rw_pic->Scale.Y * rw_midtexturescaley;
			if (xscale != lwallscale)
			{
				PrepLWall (lwall, curline->sidedef->TexelLength*xscale, WallC.sx1, WallC.sx2);
				lwallscale = xscale;
			}
			if (midtexture->bWorldPanning)
			{
				rw_offset = xs_RoundToInt(rw_offset_mid * xscale);
			}
			else
			{
				rw_offset = rw_offset_mid;
			}
			if (xscale < 0)
			{
				rw_offset = -rw_offset;
			}
			R_DrawWallSegment(rw_pic, x1, x2, walltop, wallbottom, swall, lwall, yscale, MAX(rw_frontcz1, rw_frontcz2), MIN(rw_frontfz1, rw_frontfz2), false, light_list);
		}
		fillshort (ceilingclip+x1, x2-x1, viewheight);
		fillshort (floorclip+x1, x2-x1, 0xffff);
	}
	else
	{ // two sided line
		if (toptexture != NULL && toptexture->UseType != FTexture::TEX_Null)
		{ // top wall
			for (x = x1; x < x2; ++x)
			{
				wallupper[x] = MAX (MIN (wallupper[x], floorclip[x]), walltop[x]);
			}
			if (viewactive)
			{
				dc_texturemid = rw_toptexturemid;
				rw_pic = toptexture;
				xscale = rw_pic->Scale.X * rw_toptexturescalex;
				yscale = rw_pic->Scale.Y * rw_toptexturescaley;
				if (xscale != lwallscale)
				{
					PrepLWall (lwall, curline->sidedef->TexelLength*xscale, WallC.sx1, WallC.sx2);
					lwallscale = xscale;
				}
				if (toptexture->bWorldPanning)
				{
					rw_offset = xs_RoundToInt(rw_offset_top * xscale);
				}
				else
				{
					rw_offset = rw_offset_top;
				}
				if (xscale < 0)
				{
					rw_offset = -rw_offset;
				}
				R_DrawWallSegment(rw_pic, x1, x2, walltop, wallupper, swall, lwall, yscale, MAX(rw_frontcz1, rw_frontcz2), MIN(rw_backcz1, rw_backcz2), false, light_list);
			}
			memcpy (ceilingclip+x1, wallupper+x1, (x2-x1)*sizeof(short));
		}
		else if (markceiling)
		{ // no top wall
			memcpy (ceilingclip+x1, walltop+x1, (x2-x1)*sizeof(short));
		}

		
		if (bottomtexture != NULL && bottomtexture->UseType != FTexture::TEX_Null)
		{ // bottom wall
			for (x = x1; x < x2; ++x)
			{
				walllower[x] = MIN (MAX (walllower[x], ceilingclip[x]), wallbottom[x]);
			}
			if (viewactive)
			{
				dc_texturemid = rw_bottomtexturemid;
				rw_pic = bottomtexture;
				xscale = rw_pic->Scale.X * rw_bottomtexturescalex;
				yscale = rw_pic->Scale.Y * rw_bottomtexturescaley;
				if (xscale != lwallscale)
				{
					PrepLWall (lwall, curline->sidedef->TexelLength*xscale, WallC.sx1, WallC.sx2);
					lwallscale = xscale;
				}
				if (bottomtexture->bWorldPanning)
				{
					rw_offset = xs_RoundToInt(rw_offset_bottom * xscale);
				}
				else
				{
					rw_offset = rw_offset_bottom;
				}
				if (xscale < 0)
				{
					rw_offset = -rw_offset;
				}
				R_DrawWallSegment(rw_pic, x1, x2, walllower, wallbottom, swall, lwall, yscale, MAX(rw_backfz1, rw_backfz2), MIN(rw_frontfz1, rw_frontfz2), false, light_list);
			}
			memcpy (floorclip+x1, walllower+x1, (x2-x1)*sizeof(short));
		}
		else if (markfloor)
		{ // no bottom wall
			memcpy (floorclip+x1, wallbottom+x1, (x2-x1)*sizeof(short));
		}
	}
	rw_offset = xoffset;
}

void R_NewWall (bool needlights)
{
	double rowoffset;
	double yrepeat;

	rw_markportal = false;

	sidedef = curline->sidedef;
	linedef = curline->linedef;

	// mark the segment as visible for auto map
	if (!r_dontmaplines) linedef->flags |= ML_MAPPED;

	midtexture = toptexture = bottomtexture = 0;

	if (sidedef == linedef->sidedef[0] &&
		(linedef->special == Line_Mirror && r_drawmirrors)) // [ZZ] compatibility with r_drawmirrors cvar that existed way before portals
	{
		markfloor = markceiling = true; // act like a one-sided wall here (todo: check how does this work with transparency)
		rw_markportal = true;
	}
	else if (backsector == NULL)
	{
		// single sided line
		// a single sided line is terminal, so it must mark ends
		markfloor = markceiling = true;
		// [RH] Horizon lines do not need to be textured
		if (linedef->isVisualPortal())
		{
			rw_markportal = true;
		}
		else if (linedef->special != Line_Horizon)
		{
			midtexture = TexMan(sidedef->GetTexture(side_t::mid), true);
			rw_offset_mid = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::mid));
			rowoffset = sidedef->GetTextureYOffset(side_t::mid);
			rw_midtexturescalex = sidedef->GetTextureXScale(side_t::mid);
			rw_midtexturescaley = sidedef->GetTextureYScale(side_t::mid);
			yrepeat = midtexture->Scale.Y * rw_midtexturescaley;
			if (yrepeat >= 0)
			{ // normal orientation
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // bottom of texture at bottom
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat + midtexture->GetHeight();
				}
				else
				{ // top of texture at top
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat;
					if (rowoffset < 0 && midtexture != NULL)
					{
						rowoffset += midtexture->GetHeight();
					}
				}
			}
			else
			{ // upside down
				rowoffset = -rowoffset;
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // top of texture at bottom
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat;
				}
				else
				{ // bottom of texture at top
					rw_midtexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat + midtexture->GetHeight();
				}
			}
			if (midtexture->bWorldPanning)
			{
				rw_midtexturemid += rowoffset * yrepeat;
			}
			else
			{
				// rowoffset is added outside the multiply so that it positions the texture
				// by texels instead of world units.
				rw_midtexturemid += rowoffset;
			}
		}
	}
	else
	{ // two-sided line
		// hack to allow height changes in outdoor areas

		rw_frontlowertop = frontsector->GetPlaneTexZ(sector_t::ceiling);

		if (frontsector->GetTexture(sector_t::ceiling) == skyflatnum &&
			backsector->GetTexture(sector_t::ceiling) == skyflatnum)
		{
			if (rw_havehigh)
			{ // front ceiling is above back ceiling
				memcpy (&walltop[WallC.sx1], &wallupper[WallC.sx1], (WallC.sx2 - WallC.sx1)*sizeof(walltop[0]));
				rw_havehigh = false;
			}
			else if (rw_havelow && frontsector->ceilingplane != backsector->ceilingplane)
			{ // back ceiling is above front ceiling
				// The check for rw_havelow is not Doom-compliant, but it avoids HoM that
				// would otherwise occur because there is space made available for this
				// wall but nothing to draw for it.
				// Recalculate walltop so that the wall is clipped by the back sector's
				// ceiling instead of the front sector's ceiling.
				R_CreateWallSegmentYSloped (walltop, backsector->ceilingplane, &WallC);
			}
			// Putting sky ceilings on the front and back of a line alters the way unpegged
			// positioning works.
			rw_frontlowertop = backsector->GetPlaneTexZ(sector_t::ceiling);
		}

		if (linedef->isVisualPortal())
		{
			markceiling = markfloor = true;
		}
		else if ((rw_backcz1 <= rw_frontfz1 && rw_backcz2 <= rw_frontfz2) ||
				 (rw_backfz1 >= rw_frontcz1 && rw_backfz2 >= rw_frontcz2))
		{
			// closed door
			markceiling = markfloor = true;
		}
		else
		{
			markfloor = rw_mustmarkfloor
				|| backsector->floorplane != frontsector->floorplane
				|| backsector->lightlevel != frontsector->lightlevel
				|| backsector->GetTexture(sector_t::floor) != frontsector->GetTexture(sector_t::floor)
				|| backsector->GetPlaneLight(sector_t::floor) != frontsector->GetPlaneLight(sector_t::floor)

				// killough 3/7/98: Add checks for (x,y) offsets
				|| backsector->planes[sector_t::floor].xform != frontsector->planes[sector_t::floor].xform
				|| backsector->GetAlpha(sector_t::floor) != frontsector->GetAlpha(sector_t::floor)

				// killough 4/15/98: prevent 2s normals
				// from bleeding through deep water
				|| frontsector->heightsec

				|| backsector->GetVisFlags(sector_t::floor) != frontsector->GetVisFlags(sector_t::floor)

				// [RH] Add checks for colormaps
				|| backsector->ColorMap != frontsector->ColorMap


				// kg3D - add fake lights
				|| (frontsector->e && frontsector->e->XFloor.lightlist.Size())
				|| (backsector->e && backsector->e->XFloor.lightlist.Size())

				|| (sidedef->GetTexture(side_t::mid).isValid() &&
					((linedef->flags & (ML_CLIP_MIDTEX|ML_WRAP_MIDTEX)) ||
					 (sidedef->Flags & (WALLF_CLIP_MIDTEX|WALLF_WRAP_MIDTEX))))
				;

			markceiling = (frontsector->GetTexture(sector_t::ceiling) != skyflatnum ||
				backsector->GetTexture(sector_t::ceiling) != skyflatnum) &&
				(rw_mustmarkceiling
				|| backsector->ceilingplane != frontsector->ceilingplane
				|| backsector->lightlevel != frontsector->lightlevel
				|| backsector->GetTexture(sector_t::ceiling) != frontsector->GetTexture(sector_t::ceiling)

				// killough 3/7/98: Add checks for (x,y) offsets
				|| backsector->planes[sector_t::ceiling].xform != frontsector->planes[sector_t::ceiling].xform
				|| backsector->GetAlpha(sector_t::ceiling) != frontsector->GetAlpha(sector_t::ceiling)

				// killough 4/15/98: prevent 2s normals
				// from bleeding through fake ceilings
				|| (frontsector->heightsec && frontsector->GetTexture(sector_t::ceiling) != skyflatnum)

				|| backsector->GetPlaneLight(sector_t::ceiling) != frontsector->GetPlaneLight(sector_t::ceiling)
				|| backsector->GetFlags(sector_t::ceiling) != frontsector->GetFlags(sector_t::ceiling)

				// [RH] Add check for colormaps
				|| backsector->ColorMap != frontsector->ColorMap

				// kg3D - add fake lights
				|| (frontsector->e && frontsector->e->XFloor.lightlist.Size())
				|| (backsector->e && backsector->e->XFloor.lightlist.Size())

				|| (sidedef->GetTexture(side_t::mid).isValid() &&
					((linedef->flags & (ML_CLIP_MIDTEX|ML_WRAP_MIDTEX)) ||
					(sidedef->Flags & (WALLF_CLIP_MIDTEX|WALLF_WRAP_MIDTEX))))
				);
		}

		if (rw_havehigh)
		{ // top texture
			toptexture = TexMan(sidedef->GetTexture(side_t::top), true);

			rw_offset_top = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::top));
			rowoffset = sidedef->GetTextureYOffset(side_t::top);
			rw_toptexturescalex =sidedef->GetTextureXScale(side_t::top);
			rw_toptexturescaley =sidedef->GetTextureYScale(side_t::top);
			yrepeat = toptexture->Scale.Y * rw_toptexturescaley;
			if (yrepeat >= 0)
			{ // normal orientation
				if (linedef->flags & ML_DONTPEGTOP)
				{ // top of texture at top
					rw_toptexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat;
					if (rowoffset < 0 && toptexture != NULL)
					{
						rowoffset += toptexture->GetHeight();
					}
				}
				else
				{ // bottom of texture at bottom
					rw_toptexturemid = (backsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat + toptexture->GetHeight();
				}
			}
			else
			{ // upside down
				rowoffset = -rowoffset;
				if (linedef->flags & ML_DONTPEGTOP)
				{ // bottom of texture at top
					rw_toptexturemid = (frontsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat + toptexture->GetHeight();
				}
				else
				{ // top of texture at bottom
					rw_toptexturemid = (backsector->GetPlaneTexZ(sector_t::ceiling) - ViewPos.Z) * yrepeat;
				}
			}
			if (toptexture->bWorldPanning)
			{
				rw_toptexturemid += rowoffset * yrepeat;
			}
			else
			{
				rw_toptexturemid += rowoffset;
			}
		}
		if (rw_havelow)
		{ // bottom texture
			bottomtexture = TexMan(sidedef->GetTexture(side_t::bottom), true);

			rw_offset_bottom = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::bottom));
			rowoffset = sidedef->GetTextureYOffset(side_t::bottom);
			rw_bottomtexturescalex = sidedef->GetTextureXScale(side_t::bottom);
			rw_bottomtexturescaley = sidedef->GetTextureYScale(side_t::bottom);
			yrepeat = bottomtexture->Scale.Y * rw_bottomtexturescaley;
			if (yrepeat >= 0)
			{ // normal orientation
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // bottom of texture at bottom
					rw_bottomtexturemid = (rw_frontlowertop - ViewPos.Z) * yrepeat;
				}
				else
				{ // top of texture at top
					rw_bottomtexturemid = (backsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat;
					if (rowoffset < 0 && bottomtexture != NULL)
					{
						rowoffset += bottomtexture->GetHeight();
					}
				}
			}
			else
			{ // upside down
				rowoffset = -rowoffset;
				if (linedef->flags & ML_DONTPEGBOTTOM)
				{ // top of texture at bottom
					rw_bottomtexturemid = (rw_frontlowertop - ViewPos.Z) * yrepeat;
				}
				else
				{ // bottom of texture at top
					rw_bottomtexturemid = (backsector->GetPlaneTexZ(sector_t::floor) - ViewPos.Z) * yrepeat + bottomtexture->GetHeight();
				}
			}
			if (bottomtexture->bWorldPanning)
			{
				rw_bottomtexturemid += rowoffset * yrepeat;
			}
			else
			{
				rw_bottomtexturemid += rowoffset;
			}
		}
		rw_markportal = linedef->isVisualPortal();
	}

	// if a floor / ceiling plane is on the wrong side of the view plane,
	// it is definitely invisible and doesn't need to be marked.

	// killough 3/7/98: add deep water check
	if (frontsector->GetHeightSec() == NULL)
	{
		int planeside;

		planeside = frontsector->floorplane.PointOnSide(ViewPos);
		if (frontsector->floorplane.fC() < 0)	// 3D floors have the floor backwards
			planeside = -planeside;
		if (planeside <= 0)		// above view plane
			markfloor = false;

		if (frontsector->GetTexture(sector_t::ceiling) != skyflatnum)
		{
			planeside = frontsector->ceilingplane.PointOnSide(ViewPos);
			if (frontsector->ceilingplane.fC() > 0)	// 3D floors have the ceiling backwards
				planeside = -planeside;
			if (planeside <= 0)		// below view plane
				markceiling = false;
		}
	}

	FTexture *midtex = TexMan(sidedef->GetTexture(side_t::mid), true);

	segtextured = midtex != NULL || toptexture != NULL || bottomtexture != NULL;

	// calculate light table
	if (needlights && (segtextured || (backsector && IsFogBoundary(frontsector, backsector))))
	{
		lwallscale =
			midtex ? (midtex->Scale.X * sidedef->GetTextureXScale(side_t::mid)) :
			toptexture ? (toptexture->Scale.X * sidedef->GetTextureXScale(side_t::top)) :
			bottomtexture ? (bottomtexture->Scale.X * sidedef->GetTextureXScale(side_t::bottom)) :
			1.;

		PrepWall (swall, lwall, sidedef->TexelLength * lwallscale, WallC.sx1, WallC.sx2);

		if (fixedcolormap == NULL && fixedlightlev < 0)
		{
			wallshade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, frontsector->lightlevel)
				+ r_actualextralight);
			GlobVis = r_WallVisibility;
			rw_lightleft = float (GlobVis / WallC.sz1);
			rw_lightstep = float((GlobVis / WallC.sz2 - rw_lightleft) / (WallC.sx2 - WallC.sx1));
		}
		else
		{
			rw_lightleft = 1;
			rw_lightstep = 0;
		}
	}
}

//
// R_StoreWallRange
// A wall segment will be drawn between start and stop pixels (inclusive).
//

bool R_StoreWallRange (int start, int stop)
{
	int i;
	bool maskedtexture = false;

#ifdef RANGECHECK
	if (start >= viewwidth || start >= stop)
		I_FatalError ("Bad R_StoreWallRange: %i to %i", start , stop);
#endif

	drawseg_t *draw_segment = R_AddDrawSegment();

	if (!rw_prepped)
	{
		rw_prepped = true;
		R_NewWall (true);
	}

	rw_offset = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::mid));
	rw_light = rw_lightleft + rw_lightstep * (start - WallC.sx1);

	draw_segment->CurrentPortalUniq = CurrentPortalUniq;
	draw_segment->sx1 = WallC.sx1;
	draw_segment->sx2 = WallC.sx2;
	draw_segment->sz1 = WallC.sz1;
	draw_segment->sz2 = WallC.sz2;
	draw_segment->cx = WallC.tleft.X;;
	draw_segment->cy = WallC.tleft.Y;
	draw_segment->cdx = WallC.tright.X - WallC.tleft.X;
	draw_segment->cdy = WallC.tright.Y - WallC.tleft.Y;
	draw_segment->tmapvals = WallT;
	draw_segment->siz1 = 1 / WallC.sz1;
	draw_segment->siz2 = 1 / WallC.sz2;
	draw_segment->x1 = rw_x = start;
	draw_segment->x2 = stop;
	draw_segment->curline = curline;
	rw_stopx = stop;
	draw_segment->bFogBoundary = false;
	draw_segment->bFakeBoundary = false;
	if(fake3D & 7) draw_segment->fake = 1;
	else draw_segment->fake = 0;

	draw_segment->sprtopclip = draw_segment->sprbottomclip = draw_segment->maskedtexturecol = draw_segment->bkup = draw_segment->swall = -1;

	if (rw_markportal)
	{
		draw_segment->silhouette = SIL_BOTH;
	}
	else if (backsector == NULL)
	{
		draw_segment->sprtopclip = R_NewOpening (stop - start);
		draw_segment->sprbottomclip = R_NewOpening (stop - start);
		fillshort (openings + draw_segment->sprtopclip, stop-start, viewheight);
		memset (openings + draw_segment->sprbottomclip, -1, (stop-start)*sizeof(short));
		draw_segment->silhouette = SIL_BOTH;
	}
	else
	{
		// two sided line
		draw_segment->silhouette = 0;

		if (rw_frontfz1 > rw_backfz1 || rw_frontfz2 > rw_backfz2 ||
			backsector->floorplane.PointOnSide(ViewPos) < 0)
		{
			draw_segment->silhouette = SIL_BOTTOM;
		}

		if (rw_frontcz1 < rw_backcz1 || rw_frontcz2 < rw_backcz2 ||
			backsector->ceilingplane.PointOnSide(ViewPos) < 0)
		{
			draw_segment->silhouette |= SIL_TOP;
		}

		// killough 1/17/98: this test is required if the fix
		// for the automap bug (r_bsp.c) is used, or else some
		// sprites will be displayed behind closed doors. That
		// fix prevents lines behind closed doors with dropoffs
		// from being displayed on the automap.
		//
		// killough 4/7/98: make doorclosed external variable

		{
			extern int doorclosed;	// killough 1/17/98, 2/8/98, 4/7/98
			if (doorclosed || (rw_backcz1 <= rw_frontfz1 && rw_backcz2 <= rw_frontfz2))
			{
				draw_segment->sprbottomclip = R_NewOpening (stop - start);
				memset (openings + draw_segment->sprbottomclip, -1, (stop-start)*sizeof(short));
				draw_segment->silhouette |= SIL_BOTTOM;
			}
			if (doorclosed || (rw_backfz1 >= rw_frontcz1 && rw_backfz2 >= rw_frontcz2))
			{						// killough 1/17/98, 2/8/98
				draw_segment->sprtopclip = R_NewOpening (stop - start);
				fillshort (openings + draw_segment->sprtopclip, stop - start, viewheight);
				draw_segment->silhouette |= SIL_TOP;
			}
		}

		if(!draw_segment->fake && r_3dfloors && backsector->e && backsector->e->XFloor.ffloors.Size()) {
			for(i = 0; i < (int)backsector->e->XFloor.ffloors.Size(); i++) {
				F3DFloor *rover = backsector->e->XFloor.ffloors[i];
				if(rover->flags & FF_RENDERSIDES && (!(rover->flags & FF_INVERTSIDES) || rover->flags & FF_ALLSIDES)) {
					draw_segment->bFakeBoundary |= 1;
					break;
				}
			}
		}
		if(!draw_segment->fake && r_3dfloors && frontsector->e && frontsector->e->XFloor.ffloors.Size()) {
			for(i = 0; i < (int)frontsector->e->XFloor.ffloors.Size(); i++) {
				F3DFloor *rover = frontsector->e->XFloor.ffloors[i];
				if(rover->flags & FF_RENDERSIDES && (rover->flags & FF_ALLSIDES || rover->flags & FF_INVERTSIDES)) {
					draw_segment->bFakeBoundary |= 2;
					break;
				}
			}
		}
		// kg3D - no for fakes
		if(!draw_segment->fake)
		// allocate space for masked texture tables, if needed
		// [RH] Don't just allocate the space; fill it in too.
		if ((TexMan(sidedef->GetTexture(side_t::mid), true)->UseType != FTexture::TEX_Null || draw_segment->bFakeBoundary || IsFogBoundary (frontsector, backsector)) &&
			(rw_ceilstat != 12 || !sidedef->GetTexture(side_t::top).isValid()) &&
			(rw_floorstat != 3 || !sidedef->GetTexture(side_t::bottom).isValid()) &&
			(WallC.sz1 >= TOO_CLOSE_Z && WallC.sz2 >= TOO_CLOSE_Z))
		{
			float *swal;
			fixed_t *lwal;
			int i;

			maskedtexture = true;

			// kg3D - backup for mid and fake walls
			draw_segment->bkup = R_NewOpening(stop - start);
			memcpy(openings + draw_segment->bkup, &ceilingclip[start], sizeof(short)*(stop - start));

			draw_segment->bFogBoundary = IsFogBoundary (frontsector, backsector);
			if (sidedef->GetTexture(side_t::mid).isValid() || draw_segment->bFakeBoundary)
			{
				if(sidedef->GetTexture(side_t::mid).isValid())
					draw_segment->bFakeBoundary |= 4; // it is also mid texture

				// note: This should never have used the openings array to store its data!
				draw_segment->maskedtexturecol = R_NewOpening ((stop - start) * 2);
				draw_segment->swall = R_NewOpening ((stop - start) * 2);

				lwal = (fixed_t *)(openings + draw_segment->maskedtexturecol);
				swal = (float *)(openings + draw_segment->swall);
				FTexture *pic = TexMan(sidedef->GetTexture(side_t::mid), true);
				double yscale = pic->Scale.Y * sidedef->GetTextureYScale(side_t::mid);
				fixed_t xoffset = FLOAT2FIXED(sidedef->GetTextureXOffset(side_t::mid));

				if (pic->bWorldPanning)
				{
					xoffset = xs_RoundToInt(xoffset * lwallscale);
				}

				for (i = start; i < stop; i++)
				{
					*lwal++ = lwall[i] + xoffset;
					*swal++ = swall[i];
				}

				double istart = *((float *)(openings + draw_segment->swall)) * yscale;
				double iend = *(swal - 1) * yscale;
#if 0
				///This was for avoiding overflow when using fixed point. It might not be needed anymore.
				const double mini = 3 / 65536.0;
				if (istart < mini && istart >= 0) istart = mini;
				if (istart > -mini && istart < 0) istart = -mini;
				if (iend < mini && iend >= 0) iend = mini;
				if (iend > -mini && iend < 0) iend = -mini;
#endif
				istart = 1 / istart;
				iend = 1 / iend;
				draw_segment->yscale = (float)yscale;
				draw_segment->iscale = (float)istart;
				if (stop - start > 0)
				{
					draw_segment->iscalestep = float((iend - istart) / (stop - start));
				}
				else
				{
					draw_segment->iscalestep = 0;
				}
			}
			draw_segment->light = rw_light;
			draw_segment->lightstep = rw_lightstep;

			// Masked midtextures should get the light level from the sector they reference,
			// not from the current subsector, which is what the current wallshade value
			// comes from. We make an exeption for polyobjects, however, since their "home"
			// sector should be whichever one they move into.
			if (curline->sidedef->Flags & WALLF_POLYOBJ)
			{
				draw_segment->shade = wallshade;
			}
			else
			{
				draw_segment->shade = LIGHT2SHADE(curline->sidedef->GetLightLevel(foggy, curline->frontsector->lightlevel)
					+ r_actualextralight);
			}

			if (draw_segment->bFogBoundary || draw_segment->maskedtexturecol != -1)
			{
				size_t drawsegnum = draw_segment - drawsegs;
				InterestingDrawsegs.Push (drawsegnum);
			}
		}
	}
	
	// render it
	if (markceiling)
	{
		if (ceilingplane)
		{	// killough 4/11/98: add NULL ptr checks
			ceilingplane = R_CheckPlane (ceilingplane, start, stop);
		}
		else
		{
			markceiling = false;
		}
	}
	
	if (markfloor)
	{
		if (floorplane)
		{	// killough 4/11/98: add NULL ptr checks
			floorplane = R_CheckPlane (floorplane, start, stop);
		}
		else
		{
			markfloor = false;
		}
	}

	R_RenderSegLoop ();

	if(fake3D & 7) {
		return !(fake3D & FAKE3D_FAKEMASK);
	}

	// save sprite clipping info
	if ( ((draw_segment->silhouette & SIL_TOP) || maskedtexture) && draw_segment->sprtopclip == -1)
	{
		draw_segment->sprtopclip = R_NewOpening (stop - start);
		memcpy (openings + draw_segment->sprtopclip, &ceilingclip[start], sizeof(short)*(stop-start));
	}

	if ( ((draw_segment->silhouette & SIL_BOTTOM) || maskedtexture) && draw_segment->sprbottomclip == -1)
	{
		draw_segment->sprbottomclip = R_NewOpening (stop - start);
		memcpy (openings + draw_segment->sprbottomclip, &floorclip[start], sizeof(short)*(stop-start));
	}

	if (maskedtexture && curline->sidedef->GetTexture(side_t::mid).isValid())
	{
		draw_segment->silhouette |= SIL_TOP | SIL_BOTTOM;
	}

	// [RH] Draw any decals bound to the seg
	// [ZZ] Only if not an active mirror
	if (!rw_markportal)
	{
		R_RenderDecals(curline->sidedef, draw_segment);
	}

	if (rw_markportal)
	{
		PortalDrawseg pds;
		pds.src = curline->linedef;
		pds.dst = curline->linedef->special == Line_Mirror? curline->linedef : curline->linedef->getPortalDestination();
		pds.x1 = draw_segment->x1;
		pds.x2 = draw_segment->x2;
		pds.len = pds.x2 - pds.x1;
		pds.ceilingclip.Resize(pds.len);
		memcpy(&pds.ceilingclip[0], openings + draw_segment->sprtopclip, pds.len*sizeof(*openings));
		pds.floorclip.Resize(pds.len);
		memcpy(&pds.floorclip[0], openings + draw_segment->sprbottomclip, pds.len*sizeof(*openings));

		for (int i = 0; i < pds.x2-pds.x1; i++)
		{
			if (pds.ceilingclip[i] < 0)
				pds.ceilingclip[i] = 0;
			if (pds.ceilingclip[i] >= viewheight)
				pds.ceilingclip[i] = viewheight-1;
			if (pds.floorclip[i] < 0)
				pds.floorclip[i] = 0;
			if (pds.floorclip[i] >= viewheight)
				pds.floorclip[i] = viewheight-1;
		}

		pds.mirror = curline->linedef->special == Line_Mirror;
		WallPortals.Push(pds);
	}

	return !(fake3D & FAKE3D_FAKEMASK);
}

int R_CreateWallSegmentY(short *outbuf, double z1, double z2, const FWallCoords *wallc)
{
	float y1 = (float)(CenterY - z1 * InvZtoScale / wallc->sz1);
	float y2 = (float)(CenterY - z2 * InvZtoScale / wallc->sz2);

	if (y1 < 0 && y2 < 0) // entire line is above screen
	{
		memset(&outbuf[wallc->sx1], 0, (wallc->sx2 - wallc->sx1) * sizeof(outbuf[0]));
		return 3;
	}
	else if (y1 > viewheight && y2 > viewheight) // entire line is below screen
	{
		fillshort(&outbuf[wallc->sx1], wallc->sx2 - wallc->sx1, viewheight);
		return 12;
	}

	if (wallc->sx2 <= wallc->sx1)
		return 0;

	float rcp_delta = 1.0f / (wallc->sx2 - wallc->sx1);
	if (y1 >= 0.0f && y2 >= 0.0f && xs_RoundToInt(y1) <= viewheight && xs_RoundToInt(y2) <= viewheight)
	{
		for (int x = wallc->sx1; x < wallc->sx2; x++)
		{
			float t = (x - wallc->sx1) * rcp_delta;
			float y = y1 * (1.0f - t) + y2 * t;
			outbuf[x] = (short)xs_RoundToInt(y);
		}
	}
	else
	{
		for (int x = wallc->sx1; x < wallc->sx2; x++)
		{
			float t = (x - wallc->sx1) * rcp_delta;
			float y = y1 * (1.0f - t) + y2 * t;
			outbuf[x] = (short)clamp(xs_RoundToInt(y), 0, viewheight);
		}
	}

	return 0;
}

int R_CreateWallSegmentYSloped(short *outbuf, const secplane_t &plane, const FWallCoords *wallc)
{
	if (!plane.isSlope())
	{
		return R_CreateWallSegmentY(outbuf, plane.Zat0() - ViewPos.Z, wallc);
	}
	else
	{
		// Get Z coordinates at both ends of the line
		double x, y, den, z1, z2;
		if (MirrorFlags & RF_XFLIP)
		{
			x = curline->v2->fX();
			y = curline->v2->fY();
			if (wallc->sx1 == 0 && 0 != (den = wallc->tleft.X - wallc->tright.X + wallc->tleft.Y - wallc->tright.Y))
			{
				double frac = (wallc->tleft.Y + wallc->tleft.X) / den;
				x -= frac * (x - curline->v1->fX());
				y -= frac * (y - curline->v1->fY());
			}
			z1 = plane.ZatPoint(x, y) - ViewPos.Z;

			if (wallc->sx2 > wallc->sx1 + 1)
			{
				x = curline->v1->fX();
				y = curline->v1->fY();
				if (wallc->sx2 == viewwidth && 0 != (den = wallc->tleft.X - wallc->tright.X - wallc->tleft.Y + wallc->tright.Y))
				{
					double frac = (wallc->tright.Y - wallc->tright.X) / den;
					x += frac * (curline->v2->fX() - x);
					y += frac * (curline->v2->fY() - y);
				}
				z2 = plane.ZatPoint(x, y) - ViewPos.Z;
			}
			else
			{
				z2 = z1;
			}
		}
		else
		{
			x = curline->v1->fX();
			y = curline->v1->fY();
			if (wallc->sx1 == 0 && 0 != (den = wallc->tleft.X - wallc->tright.X + wallc->tleft.Y - wallc->tright.Y))
			{
				double frac = (wallc->tleft.Y + wallc->tleft.X) / den;
				x += frac * (curline->v2->fX() - x);
				y += frac * (curline->v2->fY() - y);
			}
			z1 = plane.ZatPoint(x, y) - ViewPos.Z;

			if (wallc->sx2 > wallc->sx1 + 1)
			{
				x = curline->v2->fX();
				y = curline->v2->fY();
				if (wallc->sx2 == viewwidth && 0 != (den = wallc->tleft.X - wallc->tright.X - wallc->tleft.Y + wallc->tright.Y))
				{
					double frac = (wallc->tright.Y - wallc->tright.X) / den;
					x -= frac * (x - curline->v1->fX());
					y -= frac * (y - curline->v1->fY());
				}
				z2 = plane.ZatPoint(x, y) - ViewPos.Z;
			}
			else
			{
				z2 = z1;
			}
		}

		return R_CreateWallSegmentY(outbuf, z1, z2, wallc);
	}
}

void PrepWall(float *vstep, fixed_t *upos, double walxrepeat, int x1, int x2)
{
	float uOverZ = WallT.UoverZorg + WallT.UoverZstep * (float)(x1 + 0.5 - CenterX);
	float invZ = WallT.InvZorg + WallT.InvZstep * (float)(x1 + 0.5 - CenterX);
	float uGradient = WallT.UoverZstep;
	float zGradient = WallT.InvZstep;
	float xrepeat = (float)walxrepeat;
	float depthScale = (float)(WallT.InvZstep * WallTMapScale2);
	float depthOrg = (float)(-WallT.UoverZstep * WallTMapScale2);

	if (xrepeat < 0.0f)
	{
		for (int x = x1; x < x2; x++)
		{
			float u = uOverZ / invZ;

			upos[x] = (fixed_t)((xrepeat - u * xrepeat) * FRACUNIT);
			vstep[x] = depthOrg + u * depthScale;

			uOverZ += uGradient;
			invZ += zGradient;
		}
	}
	else
	{
		for (int x = x1; x < x2; x++)
		{
			float u = uOverZ / invZ;

			upos[x] = (fixed_t)(u * xrepeat * FRACUNIT);
			vstep[x] = depthOrg + u * depthScale;

			uOverZ += uGradient;
			invZ += zGradient;
		}
	}
}

void PrepLWall(fixed_t *upos, double walxrepeat, int x1, int x2)
{
	float uOverZ = WallT.UoverZorg + WallT.UoverZstep * (float)(x1 + 0.5 - CenterX);
	float invZ = WallT.InvZorg + WallT.InvZstep * (float)(x1 + 0.5 - CenterX);
	float uGradient = WallT.UoverZstep;
	float zGradient = WallT.InvZstep;
	float xrepeat = (float)walxrepeat;

	if (xrepeat < 0.0f)
	{
		for (int x = x1; x < x2; x++)
		{
			float u = uOverZ / invZ * xrepeat - xrepeat;

			upos[x] = (fixed_t)(u * FRACUNIT);

			uOverZ += uGradient;
			invZ += zGradient;
		}
	}
	else
	{
		for (int x = x1; x < x2; x++)
		{
			float u = uOverZ / invZ * xrepeat;

			upos[x] = (fixed_t)(u * FRACUNIT);

			uOverZ += uGradient;
			invZ += zGradient;
		}
	}
}

}