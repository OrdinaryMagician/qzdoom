
#pragma once

#include "r_visible_plane.h"

namespace swrenderer
{
	void R_DrawFogBoundary(int x1, int x2, short *uclip, short *dclip);
	void R_DrawFogBoundarySection(int y, int y2, int x1);
}