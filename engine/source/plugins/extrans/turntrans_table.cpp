// kk4s: the 'turn' transition's deformation table, computed at start-up with
// the same arithmetic as upstream extrans' maketable/mkturntranstable.pl
// (W.Dee, Kirikiri license) instead of carrying its 422 KB generated output.
// A 64x64 square is deformed into a rhombus, one scan line at a time; Perl's
// int() truncates toward zero, as the casts below do (all values here are
// non-negative where it matters).
#include "turntrans_table.h"

#include <math.h>

tTurnTransParams TurnTransParams[64][64];

void TVPInitTurnTransParams()
{
	static bool done = false;
	if(done) return;
	done = true;

	for(int n = 0; n < 64; n++)
		for(int y = 0; y < 64; y++)
			TurnTransParams[n][y] = tTurnTransParams{0, 0, 0, 0, 0, 0, 0, 0}; // n = 0 and 63 are not used

	for(int n = 1; n <= 62; n++)
	{
		int a_x, a_y; // point alpha
		if(n < 32)
		{
			int p = n * n / 31;
			int d = (int)(sin(p * 3.14159265358979 / 64) * 4);
			a_x = p - d;
			a_y = 63 - p - d;
		}
		else
		{
			int p = 63 - (63 - n) * (63 - n) / 31;
			int d = (int)(sin((63 - p) * 3.14159265358979 / 64) * 4);
			a_x = 63 - p + d;
			a_y = p + d;
		}
		int b_x = 63 - a_x, b_y = 63 - a_y; // point beta

		for(int y = 0; y < 64; y++)
		{
			int l, r;
			if(y <= a_y)
				l = a_y == 0 ? 0 : a_x * y / a_y;
			else
				l = a_y != 63 ? a_x + (63 - a_x) * (y - a_y) / (63 - a_y) : 63;
			if(y <= b_y)
				r = b_y == 0 ? 63 : b_x * y / b_y;
			else
				r = b_y != 63 ? b_x + (63 - b_x) * (y - b_y) / (63 - b_y) : 63;
			if(l > r) r = l;
			int len = r - l + 1;

			int sx, sy, ex, ey;
			if(y <= a_y)
			{
				sx = 0;
				sy = a_y ? 63 * y / a_y : 0;
			}
			else
			{
				sx = 63 * (y - a_y) / (63 - a_y);
				sy = 63;
			}
			if(y <= b_y)
			{
				ex = b_y ? 63 * y / b_y : 63;
				ey = 0;
			}
			else
			{
				ex = 63;
				ey = 63 * (y - b_y) / (63 - b_y);
			}

			int stepx = 0, stepy = 0;
			if(len >= 2)
			{
				stepx = (int)((double)(ex - sx) / (len - 1) * 65536);
				stepy = (int)((double)(ey - sy) / (len - 1) * 65536);
			}

			TurnTransParams[n][y] = tTurnTransParams{l, len, sx << 16, sy << 16, ex << 16, ey << 16, stepx, stepy};
		}
	}
}
