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
//		BSP traversal, handling of LineSegs for rendering.
//
//-----------------------------------------------------------------------------


#include <stdlib.h>

#include "templates.h"

#include "doomdef.h"

#include "m_bbox.h"

#include "i_system.h"
#include "p_lnspec.h"
#include "p_setup.h"

#include "swrenderer/r_main.h"
#include "swrenderer/drawers/r_draw.h"
#include "swrenderer/plane/r_visibleplane.h"
#include "swrenderer/things/r_particle.h"
#include "swrenderer/segments/r_clipsegment.h"
#include "swrenderer/line/r_wallsetup.h"
#include "r_things.h"
#include "r_3dfloors.h"
#include "r_portal.h"
#include "a_sharedglobal.h"
#include "g_level.h"
#include "p_effect.h"
#include "c_console.h"

// State.
#include "doomstat.h"
#include "r_state.h"
#include "r_bsp.h"
#include "v_palette.h"
#include "r_sky.h"
#include "po_man.h"
#include "r_data/colormaps.h"

EXTERN_CVAR(Bool, r_fullbrightignoresectorcolor);

namespace swrenderer
{
	RenderBSP *RenderBSP::Instance()
	{
		static RenderBSP bsp;
		return &bsp;
	}

	sector_t *RenderBSP::FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, seg_t *backline, int backx1, int backx2, double frontcz1, double frontcz2)
	{
		// If player's view height is underneath fake floor, lower the
		// drawn ceiling to be just under the floor height, and replace
		// the drawn floor and ceiling textures, and light level, with
		// the control sector's.
		//
		// Similar for ceiling, only reflected.

		// [RH] allow per-plane lighting
		if (floorlightlevel != NULL)
		{
			*floorlightlevel = sec->GetFloorLight();
		}

		if (ceilinglightlevel != NULL)
		{
			*ceilinglightlevel = sec->GetCeilingLight();
		}

		FakeSide = WaterFakeSide::Center;

		const sector_t *s = sec->GetHeightSec();
		if (s != NULL)
		{
			sector_t *heightsec = viewsector->heightsec;
			bool underwater = r_fakingunderwater ||
				(heightsec && heightsec->floorplane.PointOnSide(ViewPos) <= 0);
			bool doorunderwater = false;
			int diffTex = (s->MoreFlags & SECF_CLIPFAKEPLANES);

			// Replace sector being drawn with a copy to be hacked
			*tempsec = *sec;

			// Replace floor and ceiling height with control sector's heights.
			if (diffTex)
			{
				if (s->floorplane.CopyPlaneIfValid(&tempsec->floorplane, &sec->ceilingplane))
				{
					tempsec->SetTexture(sector_t::floor, s->GetTexture(sector_t::floor), false);
				}
				else if (s->MoreFlags & SECF_FAKEFLOORONLY)
				{
					if (underwater)
					{
						tempsec->ColorMap = s->ColorMap;
						if (!(s->MoreFlags & SECF_NOFAKELIGHT))
						{
							tempsec->lightlevel = s->lightlevel;

							if (floorlightlevel != NULL)
							{
								*floorlightlevel = s->GetFloorLight();
							}

							if (ceilinglightlevel != NULL)
							{
								*ceilinglightlevel = s->GetCeilingLight();
							}
						}
						FakeSide = WaterFakeSide::BelowFloor;
						return tempsec;
					}
					return sec;
				}
			}
			else
			{
				tempsec->floorplane = s->floorplane;
			}

			if (!(s->MoreFlags & SECF_FAKEFLOORONLY))
			{
				if (diffTex)
				{
					if (s->ceilingplane.CopyPlaneIfValid(&tempsec->ceilingplane, &sec->floorplane))
					{
						tempsec->SetTexture(sector_t::ceiling, s->GetTexture(sector_t::ceiling), false);
					}
				}
				else
				{
					tempsec->ceilingplane = s->ceilingplane;
				}
			}

			double refceilz = s->ceilingplane.ZatPoint(ViewPos);
			double orgceilz = sec->ceilingplane.ZatPoint(ViewPos);

#if 1
			// [RH] Allow viewing underwater areas through doors/windows that
			// are underwater but not in a water sector themselves.
			// Only works if you cannot see the top surface of any deep water
			// sectors at the same time.
			if (backline && !r_fakingunderwater && backline->frontsector->heightsec == NULL)
			{
				if (frontcz1 <= s->floorplane.ZatPoint(backline->v1) &&
					frontcz2 <= s->floorplane.ZatPoint(backline->v2))
				{
					// Check that the window is actually visible
					for (int z = backx1; z < backx2; ++z)
					{
						if (floorclip[z] > ceilingclip[z])
						{
							doorunderwater = true;
							r_fakingunderwater = true;
							break;
						}
					}
				}
			}
#endif

			if (underwater || doorunderwater)
			{
				tempsec->floorplane = sec->floorplane;
				tempsec->ceilingplane = s->floorplane;
				tempsec->ceilingplane.FlipVert();
				tempsec->ceilingplane.ChangeHeight(-1 / 65536.);
				tempsec->ColorMap = s->ColorMap;
			}

			// killough 11/98: prevent sudden light changes from non-water sectors:
			if ((underwater && !backline) || doorunderwater)
			{					// head-below-floor hack
				tempsec->SetTexture(sector_t::floor, diffTex ? sec->GetTexture(sector_t::floor) : s->GetTexture(sector_t::floor), false);
				tempsec->planes[sector_t::floor].xform = s->planes[sector_t::floor].xform;

				tempsec->ceilingplane = s->floorplane;
				tempsec->ceilingplane.FlipVert();
				tempsec->ceilingplane.ChangeHeight(-1 / 65536.);
				if (s->GetTexture(sector_t::ceiling) == skyflatnum)
				{
					tempsec->floorplane = tempsec->ceilingplane;
					tempsec->floorplane.FlipVert();
					tempsec->floorplane.ChangeHeight(+1 / 65536.);
					tempsec->SetTexture(sector_t::ceiling, tempsec->GetTexture(sector_t::floor), false);
					tempsec->planes[sector_t::ceiling].xform = tempsec->planes[sector_t::floor].xform;
				}
				else
				{
					tempsec->SetTexture(sector_t::ceiling, diffTex ? s->GetTexture(sector_t::floor) : s->GetTexture(sector_t::ceiling), false);
					tempsec->planes[sector_t::ceiling].xform = s->planes[sector_t::ceiling].xform;
				}

				if (!(s->MoreFlags & SECF_NOFAKELIGHT))
				{
					tempsec->lightlevel = s->lightlevel;

					if (floorlightlevel != NULL)
					{
						*floorlightlevel = s->GetFloorLight();
					}

					if (ceilinglightlevel != NULL)
					{
						*ceilinglightlevel = s->GetCeilingLight();
					}
				}
				FakeSide = WaterFakeSide::BelowFloor;
			}
			else if (heightsec && heightsec->ceilingplane.PointOnSide(ViewPos) <= 0 &&
				orgceilz > refceilz && !(s->MoreFlags & SECF_FAKEFLOORONLY))
			{	// Above-ceiling hack
				tempsec->ceilingplane = s->ceilingplane;
				tempsec->floorplane = s->ceilingplane;
				tempsec->floorplane.FlipVert();
				tempsec->floorplane.ChangeHeight(+1 / 65536.);
				tempsec->ColorMap = s->ColorMap;
				tempsec->ColorMap = s->ColorMap;

				tempsec->SetTexture(sector_t::ceiling, diffTex ? sec->GetTexture(sector_t::ceiling) : s->GetTexture(sector_t::ceiling), false);
				tempsec->SetTexture(sector_t::floor, s->GetTexture(sector_t::ceiling), false);
				tempsec->planes[sector_t::ceiling].xform = tempsec->planes[sector_t::floor].xform = s->planes[sector_t::ceiling].xform;

				if (s->GetTexture(sector_t::floor) != skyflatnum)
				{
					tempsec->ceilingplane = sec->ceilingplane;
					tempsec->SetTexture(sector_t::floor, s->GetTexture(sector_t::floor), false);
					tempsec->planes[sector_t::floor].xform = s->planes[sector_t::floor].xform;
				}

				if (!(s->MoreFlags & SECF_NOFAKELIGHT))
				{
					tempsec->lightlevel = s->lightlevel;

					if (floorlightlevel != NULL)
					{
						*floorlightlevel = s->GetFloorLight();
					}

					if (ceilinglightlevel != NULL)
					{
						*ceilinglightlevel = s->GetCeilingLight();
					}
				}
				FakeSide = WaterFakeSide::AboveCeiling;
			}
			sec = tempsec;					// Use other sector
		}
		return sec;
	}



	// Checks BSP node/subtree bounding box.
	// Returns true if some part of the bbox might be visible.
	bool RenderBSP::CheckBBox(float *bspcoord)
	{
		static const int checkcoord[12][4] =
		{
			{ 3,0,2,1 },
			{ 3,0,2,0 },
			{ 3,1,2,0 },
			{ 0 },
			{ 2,0,2,1 },
			{ 0,0,0,0 },
			{ 3,1,3,0 },
			{ 0 },
			{ 2,0,3,1 },
			{ 2,1,3,1 },
			{ 2,1,3,0 }
		};

		int 				boxx;
		int 				boxy;
		int 				boxpos;

		double	 			x1, y1, x2, y2;
		double				rx1, ry1, rx2, ry2;
		int					sx1, sx2;

		// Find the corners of the box
		// that define the edges from current viewpoint.
		if (ViewPos.X <= bspcoord[BOXLEFT])
			boxx = 0;
		else if (ViewPos.X < bspcoord[BOXRIGHT])
			boxx = 1;
		else
			boxx = 2;

		if (ViewPos.Y >= bspcoord[BOXTOP])
			boxy = 0;
		else if (ViewPos.Y > bspcoord[BOXBOTTOM])
			boxy = 1;
		else
			boxy = 2;

		boxpos = (boxy << 2) + boxx;
		if (boxpos == 5)
			return true;

		x1 = bspcoord[checkcoord[boxpos][0]] - ViewPos.X;
		y1 = bspcoord[checkcoord[boxpos][1]] - ViewPos.Y;
		x2 = bspcoord[checkcoord[boxpos][2]] - ViewPos.X;
		y2 = bspcoord[checkcoord[boxpos][3]] - ViewPos.Y;

		// check clip list for an open space

		// Sitting on a line?
		if (y1 * (x1 - x2) + x1 * (y2 - y1) >= -EQUAL_EPSILON)
			return true;

		rx1 = x1 * ViewSin - y1 * ViewCos;
		rx2 = x2 * ViewSin - y2 * ViewCos;
		ry1 = x1 * ViewTanCos + y1 * ViewTanSin;
		ry2 = x2 * ViewTanCos + y2 * ViewTanSin;

		if (RenderPortal::Instance()->MirrorFlags & RF_XFLIP)
		{
			double t = -rx1;
			rx1 = -rx2;
			rx2 = t;
			swapvalues(ry1, ry2);
		}

		if (rx1 >= -ry1)
		{
			if (rx1 > ry1) return false;	// left edge is off the right side
			if (ry1 == 0) return false;
			sx1 = xs_RoundToInt(CenterX + rx1 * CenterX / ry1);
		}
		else
		{
			if (rx2 < -ry2) return false;	// wall is off the left side
			if (rx1 - rx2 - ry2 + ry1 == 0) return false;	// wall does not intersect view volume
			sx1 = 0;
		}

		if (rx2 <= ry2)
		{
			if (rx2 < -ry2) return false;	// right edge is off the left side
			if (ry2 == 0) return false;
			sx2 = xs_RoundToInt(CenterX + rx2 * CenterX / ry2);
		}
		else
		{
			if (rx1 > ry1) return false;	// wall is off the right side
			if (ry2 - ry1 - rx2 + rx1 == 0) return false;	// wall does not intersect view volume
			sx2 = viewwidth;
		}

		// Find the first clippost that touches the source post
		//	(adjacent pixels are touching).

		return R_IsWallSegmentVisible(sx1, sx2);
	}

	void RenderBSP::AddPolyobjs(subsector_t *sub)
	{
		if (sub->BSP == NULL || sub->BSP->bDirty)
		{
			sub->BuildPolyBSP();
		}
		if (sub->BSP->Nodes.Size() == 0)
		{
			RenderSubsector(&sub->BSP->Subsectors[0]);
		}
		else
		{
			RenderBSPNode(&sub->BSP->Nodes.Last());
		}
	}

	// kg3D - add fake segs, never rendered
	void RenderBSP::FakeDrawLoop(subsector_t *sub, visplane_t *floorplane, visplane_t *ceilingplane)
	{
		int 		 count;
		seg_t*		 line;

		count = sub->numlines;
		line = sub->firstline;

		while (count--)
		{
			if ((line->sidedef) && !(line->sidedef->Flags & WALLF_POLYOBJ))
			{
				renderline.Render(line, InSubsector, frontsector, nullptr, floorplane, ceilingplane);
			}
			line++;
		}
	}

	void RenderBSP::RenderSubsector(subsector_t *sub)
	{
		// Determine floor/ceiling planes.
		// Add sprites of things in sector.
		// Draw one or more line segments.

		int 		 count;
		seg_t*		 line;
		sector_t     tempsec;				// killough 3/7/98: deep water hack
		int          floorlightlevel;		// killough 3/16/98: set floor lightlevel
		int          ceilinglightlevel;		// killough 4/11/98
		bool		 outersubsector;
		int	fll, cll, position;
		FSectorPortal *portal;

		// kg3D - fake floor stuff
		visplane_t *backupfp;
		visplane_t *backupcp;
		//secplane_t templane;
		lightlist_t *light;

		if (InSubsector != NULL)
		{ // InSubsector is not NULL. This means we are rendering from a mini-BSP.
			outersubsector = false;
		}
		else
		{
			outersubsector = true;
			InSubsector = sub;
		}

#ifdef RANGECHECK
		if (outersubsector && sub - subsectors >= (ptrdiff_t)numsubsectors)
			I_Error("RenderSubsector: ss %ti with numss = %i", sub - subsectors, numsubsectors);
#endif

		assert(sub->sector != NULL);

		if (sub->polys)
		{ // Render the polyobjs in the subsector first
			AddPolyobjs(sub);
			if (outersubsector)
			{
				InSubsector = NULL;
			}
			return;
		}

		frontsector = sub->sector;
		frontsector->MoreFlags |= SECF_DRAWN;
		count = sub->numlines;
		line = sub->firstline;

		// killough 3/8/98, 4/4/98: Deep water / fake ceiling effect
		frontsector = FakeFlat(frontsector, &tempsec, &floorlightlevel, &ceilinglightlevel, nullptr, 0, 0, 0, 0);

		fll = floorlightlevel;
		cll = ceilinglightlevel;

		// [RH] set foggy flag
		foggy = level.fadeto || frontsector->ColorMap->Fade || (level.flags & LEVEL_HASFADETABLE);
		r_actualextralight = foggy ? 0 : extralight << 4;

		// kg3D - fake lights
		if (fixedlightlev < 0 && frontsector->e && frontsector->e->XFloor.lightlist.Size())
		{
			light = P_GetPlaneLight(frontsector, &frontsector->ceilingplane, false);
			basecolormap = light->extra_colormap;
			// If this is the real ceiling, don't discard plane lighting R_FakeFlat()
			// accounted for.
			if (light->p_lightlevel != &frontsector->lightlevel)
			{
				ceilinglightlevel = *light->p_lightlevel;
			}
		}
		else
		{
			basecolormap = (r_fullbrightignoresectorcolor && fixedlightlev >= 0) ? &FullNormalLight : frontsector->ColorMap;
		}

		portal = frontsector->ValidatePortal(sector_t::ceiling);

		visplane_t *ceilingplane = frontsector->ceilingplane.PointOnSide(ViewPos) > 0 ||
			frontsector->GetTexture(sector_t::ceiling) == skyflatnum ||
			portal != NULL ||
			(frontsector->heightsec &&
				!(frontsector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC) &&
				frontsector->heightsec->GetTexture(sector_t::floor) == skyflatnum) ?
			R_FindPlane(frontsector->ceilingplane,		// killough 3/8/98
				frontsector->GetTexture(sector_t::ceiling),
				ceilinglightlevel + r_actualextralight,				// killough 4/11/98
				frontsector->GetAlpha(sector_t::ceiling),
				!!(frontsector->GetFlags(sector_t::ceiling) & PLANEF_ADDITIVE),
				frontsector->planes[sector_t::ceiling].xform,
				frontsector->sky,
				portal
			) : NULL;

		if (ceilingplane)
			R_AddPlaneLights(ceilingplane, frontsector->lighthead);

		if (fixedlightlev < 0 && frontsector->e && frontsector->e->XFloor.lightlist.Size())
		{
			light = P_GetPlaneLight(frontsector, &frontsector->floorplane, false);
			basecolormap = light->extra_colormap;
			// If this is the real floor, don't discard plane lighting R_FakeFlat()
			// accounted for.
			if (light->p_lightlevel != &frontsector->lightlevel)
			{
				floorlightlevel = *light->p_lightlevel;
			}
		}
		else
		{
			basecolormap = (r_fullbrightignoresectorcolor && fixedlightlev >= 0) ? &FullNormalLight : frontsector->ColorMap;
		}

		// killough 3/7/98: Add (x,y) offsets to flats, add deep water check
		// killough 3/16/98: add floorlightlevel
		// killough 10/98: add support for skies transferred from sidedefs
		portal = frontsector->ValidatePortal(sector_t::floor);

		visplane_t *floorplane = frontsector->floorplane.PointOnSide(ViewPos) > 0 || // killough 3/7/98
			frontsector->GetTexture(sector_t::floor) == skyflatnum ||
			portal != NULL ||
			(frontsector->heightsec &&
				!(frontsector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC) &&
				frontsector->heightsec->GetTexture(sector_t::ceiling) == skyflatnum) ?
			R_FindPlane(frontsector->floorplane,
				frontsector->GetTexture(sector_t::floor),
				floorlightlevel + r_actualextralight,				// killough 3/16/98
				frontsector->GetAlpha(sector_t::floor),
				!!(frontsector->GetFlags(sector_t::floor) & PLANEF_ADDITIVE),
				frontsector->planes[sector_t::floor].xform,
				frontsector->sky,
				portal
			) : NULL;

		if (floorplane)
			R_AddPlaneLights(floorplane, frontsector->lighthead);

		// kg3D - fake planes rendering
		if (r_3dfloors && frontsector->e && frontsector->e->XFloor.ffloors.Size())
		{
			backupfp = floorplane;
			backupcp = ceilingplane;

			Clip3DFloors *clip3d = Clip3DFloors::Instance();

			// first check all floors
			for (int i = 0; i < (int)frontsector->e->XFloor.ffloors.Size(); i++)
			{
				clip3d->fakeFloor = frontsector->e->XFloor.ffloors[i];
				if (!(clip3d->fakeFloor->flags & FF_EXISTS)) continue;
				if (!clip3d->fakeFloor->model) continue;
				if (clip3d->fakeFloor->bottom.plane->isSlope()) continue;
				if (!(clip3d->fakeFloor->flags & FF_NOSHADE) || (clip3d->fakeFloor->flags & (FF_RENDERPLANES | FF_RENDERSIDES)))
				{
					clip3d->AddHeight(clip3d->fakeFloor->top.plane, frontsector);
				}
				if (!(clip3d->fakeFloor->flags & FF_RENDERPLANES)) continue;
				if (clip3d->fakeFloor->alpha == 0) continue;
				if (clip3d->fakeFloor->flags & FF_THISINSIDE && clip3d->fakeFloor->flags & FF_INVERTSECTOR) continue;
				clip3d->fakeAlpha = MIN<fixed_t>(Scale(clip3d->fakeFloor->alpha, OPAQUE, 255), OPAQUE);
				if (clip3d->fakeFloor->validcount != validcount)
				{
					clip3d->fakeFloor->validcount = validcount;
					clip3d->NewClip();
				}
				double fakeHeight = clip3d->fakeFloor->top.plane->ZatPoint(frontsector->centerspot);
				if (fakeHeight < ViewPos.Z &&
					fakeHeight > frontsector->floorplane.ZatPoint(frontsector->centerspot))
				{
					clip3d->fake3D = FAKE3D_FAKEFLOOR;
					tempsec = *clip3d->fakeFloor->model;
					tempsec.floorplane = *clip3d->fakeFloor->top.plane;
					tempsec.ceilingplane = *clip3d->fakeFloor->bottom.plane;
					if (!(clip3d->fakeFloor->flags & FF_THISINSIDE) && !(clip3d->fakeFloor->flags & FF_INVERTSECTOR))
					{
						tempsec.SetTexture(sector_t::floor, tempsec.GetTexture(sector_t::ceiling));
						position = sector_t::ceiling;
					}
					else position = sector_t::floor;
					frontsector = &tempsec;

					if (fixedlightlev < 0 && sub->sector->e->XFloor.lightlist.Size())
					{
						light = P_GetPlaneLight(sub->sector, &frontsector->floorplane, false);
						basecolormap = light->extra_colormap;
						floorlightlevel = *light->p_lightlevel;
					}

					ceilingplane = NULL;
					floorplane = R_FindPlane(frontsector->floorplane,
						frontsector->GetTexture(sector_t::floor),
						floorlightlevel + r_actualextralight,				// killough 3/16/98
						frontsector->GetAlpha(sector_t::floor),
						!!(clip3d->fakeFloor->flags & FF_ADDITIVETRANS),
						frontsector->planes[position].xform,
						frontsector->sky,
						NULL);

					if (floorplane)
						R_AddPlaneLights(floorplane, frontsector->lighthead);

					FakeDrawLoop(sub, floorplane, ceilingplane);
					clip3d->fake3D = 0;
					frontsector = sub->sector;
				}
			}
			// and now ceilings
			for (unsigned int i = 0; i < frontsector->e->XFloor.ffloors.Size(); i++)
			{
				clip3d->fakeFloor = frontsector->e->XFloor.ffloors[i];
				if (!(clip3d->fakeFloor->flags & FF_EXISTS)) continue;
				if (!clip3d->fakeFloor->model) continue;
				if (clip3d->fakeFloor->top.plane->isSlope()) continue;
				if (!(clip3d->fakeFloor->flags & FF_NOSHADE) || (clip3d->fakeFloor->flags & (FF_RENDERPLANES | FF_RENDERSIDES)))
				{
					clip3d->AddHeight(clip3d->fakeFloor->bottom.plane, frontsector);
				}
				if (!(clip3d->fakeFloor->flags & FF_RENDERPLANES)) continue;
				if (clip3d->fakeFloor->alpha == 0) continue;
				if (!(clip3d->fakeFloor->flags & FF_THISINSIDE) && (clip3d->fakeFloor->flags & (FF_SWIMMABLE | FF_INVERTSECTOR)) == (FF_SWIMMABLE | FF_INVERTSECTOR)) continue;
				clip3d->fakeAlpha = MIN<fixed_t>(Scale(clip3d->fakeFloor->alpha, OPAQUE, 255), OPAQUE);

				if (clip3d->fakeFloor->validcount != validcount)
				{
					clip3d->fakeFloor->validcount = validcount;
					clip3d->NewClip();
				}
				double fakeHeight = clip3d->fakeFloor->bottom.plane->ZatPoint(frontsector->centerspot);
				if (fakeHeight > ViewPos.Z &&
					fakeHeight < frontsector->ceilingplane.ZatPoint(frontsector->centerspot))
				{
					clip3d->fake3D = FAKE3D_FAKECEILING;
					tempsec = *clip3d->fakeFloor->model;
					tempsec.floorplane = *clip3d->fakeFloor->top.plane;
					tempsec.ceilingplane = *clip3d->fakeFloor->bottom.plane;
					if ((!(clip3d->fakeFloor->flags & FF_THISINSIDE) && !(clip3d->fakeFloor->flags & FF_INVERTSECTOR)) ||
						(clip3d->fakeFloor->flags & FF_THISINSIDE && clip3d->fakeFloor->flags & FF_INVERTSECTOR))
					{
						tempsec.SetTexture(sector_t::ceiling, tempsec.GetTexture(sector_t::floor));
						position = sector_t::floor;
					}
					else position = sector_t::ceiling;
					frontsector = &tempsec;

					tempsec.ceilingplane.ChangeHeight(-1 / 65536.);
					if (fixedlightlev < 0 && sub->sector->e->XFloor.lightlist.Size())
					{
						light = P_GetPlaneLight(sub->sector, &frontsector->ceilingplane, false);
						basecolormap = light->extra_colormap;
						ceilinglightlevel = *light->p_lightlevel;
					}
					tempsec.ceilingplane.ChangeHeight(1 / 65536.);

					floorplane = NULL;
					ceilingplane = R_FindPlane(frontsector->ceilingplane,		// killough 3/8/98
						frontsector->GetTexture(sector_t::ceiling),
						ceilinglightlevel + r_actualextralight,				// killough 4/11/98
						frontsector->GetAlpha(sector_t::ceiling),
						!!(clip3d->fakeFloor->flags & FF_ADDITIVETRANS),
						frontsector->planes[position].xform,
						frontsector->sky,
						NULL);

					if (ceilingplane)
						R_AddPlaneLights(ceilingplane, frontsector->lighthead);

					FakeDrawLoop(sub, floorplane, ceilingplane);
					clip3d->fake3D = 0;
					frontsector = sub->sector;
				}
			}
			clip3d->fakeFloor = NULL;
			floorplane = backupfp;
			ceilingplane = backupcp;
		}

		basecolormap = frontsector->ColorMap;
		floorlightlevel = fll;
		ceilinglightlevel = cll;

		// killough 9/18/98: Fix underwater slowdown, by passing real sector 
		// instead of fake one. Improve sprite lighting by basing sprite
		// lightlevels on floor & ceiling lightlevels in the surrounding area.
		// [RH] Handle sprite lighting like Duke 3D: If the ceiling is a sky, sprites are lit by
		// it, otherwise they are lit by the floor.
		R_AddSprites(sub->sector, frontsector->GetTexture(sector_t::ceiling) == skyflatnum ? ceilinglightlevel : floorlightlevel, FakeSide);

		// [RH] Add particles
		if ((unsigned int)(sub - subsectors) < (unsigned int)numsubsectors)
		{ // Only do it for the main BSP.
			int shade = LIGHT2SHADE((floorlightlevel + ceilinglightlevel) / 2 + r_actualextralight);
			for (WORD i = ParticlesInSubsec[(unsigned int)(sub - subsectors)]; i != NO_PARTICLE; i = Particles[i].snext)
			{
				R_ProjectParticle(Particles + i, subsectors[sub - subsectors].sector, shade, FakeSide);
			}
		}

		count = sub->numlines;
		line = sub->firstline;

		while (count--)
		{
			if (!outersubsector || line->sidedef == NULL || !(line->sidedef->Flags & WALLF_POLYOBJ))
			{
				// kg3D - fake planes bounding calculation
				if (r_3dfloors && line->backsector && frontsector->e && line->backsector->e->XFloor.ffloors.Size())
				{
					backupfp = floorplane;
					backupcp = ceilingplane;
					floorplane = NULL;
					ceilingplane = NULL;
					Clip3DFloors *clip3d = Clip3DFloors::Instance();
					for (unsigned int i = 0; i < line->backsector->e->XFloor.ffloors.Size(); i++)
					{
						clip3d->fakeFloor = line->backsector->e->XFloor.ffloors[i];
						if (!(clip3d->fakeFloor->flags & FF_EXISTS)) continue;
						if (!(clip3d->fakeFloor->flags & FF_RENDERPLANES)) continue;
						if (!clip3d->fakeFloor->model) continue;
						clip3d->fake3D = FAKE3D_FAKEBACK;
						tempsec = *clip3d->fakeFloor->model;
						tempsec.floorplane = *clip3d->fakeFloor->top.plane;
						tempsec.ceilingplane = *clip3d->fakeFloor->bottom.plane;
						if (clip3d->fakeFloor->validcount != validcount)
						{
							clip3d->fakeFloor->validcount = validcount;
							clip3d->NewClip();
						}
						renderline.Render(line, InSubsector, frontsector, &tempsec, floorplane, ceilingplane); // fake
					}
					clip3d->fakeFloor = NULL;
					clip3d->fake3D = 0;
					floorplane = backupfp;
					ceilingplane = backupcp;
				}
				renderline.Render(line, InSubsector, frontsector, nullptr, floorplane, ceilingplane); // now real
			}
			line++;
		}
		if (outersubsector)
		{
			InSubsector = NULL;
		}
	}

	void RenderBSP::RenderScene()
	{
		InSubsector = nullptr;
		RenderBSPNode(nodes + numnodes - 1);	// The head node is the last node output.
	}

	//
	// RenderBSPNode
	// Renders all subsectors below a given node, traversing subtree recursively.
	// Just call with BSP root and -1.
	// killough 5/2/98: reformatted, removed tail recursion

	void RenderBSP::RenderBSPNode(void *node)
	{
		if (numnodes == 0)
		{
			RenderSubsector(subsectors);
			return;
		}
		while (!((size_t)node & 1))  // Keep going until found a subsector
		{
			node_t *bsp = (node_t *)node;

			// Decide which side the view point is on.
			int side = R_PointOnSide(ViewPos, bsp);

			// Recursively divide front space (toward the viewer).
			RenderBSPNode(bsp->children[side]);

			// Possibly divide back space (away from the viewer).
			side ^= 1;
			if (!CheckBBox(bsp->bbox[side]))
				return;

			node = bsp->children[side];
		}
		RenderSubsector((subsector_t *)((BYTE *)node - 1));
	}

	void RenderBSP::ClearClip()
	{
		// clip ceiling to console bottom
		fillshort(floorclip, viewwidth, viewheight);
		fillshort(ceilingclip, viewwidth, !screen->Accel2D && ConBottom > viewwindowy && !bRenderingToCanvas ? (ConBottom - viewwindowy) : 0);
	}
}