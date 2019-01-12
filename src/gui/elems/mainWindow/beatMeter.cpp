/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * beatMeter
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2019 Giovanni A. Zuliani | Monocasual
 *
 * This file is part of Giada - Your Hardcore Loopmachine.
 *
 * Giada - Your Hardcore Loopmachine is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * Giada - Your Hardcore Loopmachine is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Giada - Your Hardcore Loopmachine. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------- */


#include <FL/fl_draw.H>
#include "../../../core/const.h"
#include "../../../core/mixer.h"
#include "../../../core/clock.h"
#include "beatMeter.h"


namespace giada {
namespace v
{
geBeatMeter::geBeatMeter(int x, int y, int w, int h, const char* l)
: Fl_Box(x, y, w, h, l)
{
}


/* -------------------------------------------------------------------------- */


void geBeatMeter::draw()
{
	using namespace giada::m;

	int cursorW = w() / G_MAX_BEATS;
	int greyX   = clock::getBeats() * cursorW;

	fl_rect(x(), y(), w(), h(), G_COLOR_GREY_4);                        // border
	fl_rectf(x()+1, y()+1, w()-2, h()-2, FL_BACKGROUND_COLOR);          // bg
	fl_rectf(x()+(clock::getCurrentBeat()*cursorW)+3, y()+3, cursorW-5, h()-6,
		G_COLOR_LIGHT_1); // cursor

	/* beat cells */

	fl_color(G_COLOR_GREY_4);
	for (int i=1; i<=clock::getBeats(); i++)
		fl_line(x()+cursorW*i, y()+1, x()+cursorW*i, y()+h()-2);

	/* bar line */

	fl_color(G_COLOR_LIGHT_1);
	int delta = clock::getBeats() / clock::getBars();
	for (int i=1; i<clock::getBars(); i++)
		fl_line(x()+cursorW*(i*delta), y()+1, x()+cursorW*(i*delta), y()+h()-2);

	/* unused grey area */

	fl_rectf(x()+greyX+1, y()+1, w()-greyX-1,  h()-2, G_COLOR_GREY_4);
}

}} // giada::v::