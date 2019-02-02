/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
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


#include <FL/Fl.H>
#include "../../../core/sampleChannel.h"
#include "../../../core/wave.h"
#include "../../../glue/channel.h"
#include "../../../glue/sampleEditor.h"
#include "../../../utils/gui.h"
#include "../../../utils/string.h"
#include "../../dialogs/sampleEditor.h"
#include "../basics/input.h"
#include "../basics/box.h"
#include "../basics/button.h"
#include "waveTools.h"
#include "rangeTool.h"


using namespace giada;


geRangeTool::geRangeTool(int x, int y, giada::m::SampleChannel* ch)
	: Fl_Group(x, y, 280, G_GUI_UNIT),
		m_ch    (ch)
{
	begin();
		m_label = new geBox(x, y, u::gui::getStringWidth("Range"), G_GUI_UNIT, "Range", FL_ALIGN_RIGHT);
		m_begin = new geInput(m_label->x()+m_label->w()+G_GUI_INNER_MARGIN, y, 70, G_GUI_UNIT);
		m_end   = new geInput(m_begin->x()+m_begin->w()+G_GUI_INNER_MARGIN, y, 70, G_GUI_UNIT);
		m_reset = new geButton(m_end->x()+m_end->w()+G_GUI_INNER_MARGIN, y, 70, G_GUI_UNIT, "Reset");
	end();

	m_begin->type(FL_INT_INPUT);
	m_begin->when(FL_WHEN_RELEASE | FL_WHEN_ENTER_KEY); // on focus lost or enter key
	m_begin->callback(cb_setChanPos, this);
	
	m_end->type(FL_INT_INPUT);
	m_end->when(FL_WHEN_RELEASE | FL_WHEN_ENTER_KEY); // on focus lost or enter key
	m_end->callback(cb_setChanPos, this);

	m_reset->callback(cb_resetStartEnd, this);

	refresh();
}


/* -------------------------------------------------------------------------- */


void geRangeTool::refresh()
{
	m_begin->value(u::string::iToString(m_ch->getBegin()).c_str());
	m_end->value(u::string::iToString(m_ch->getEnd()).c_str());
}


/* -------------------------------------------------------------------------- */


void geRangeTool::cb_setChanPos   (Fl_Widget* w, void* p) { ((geRangeTool*)p)->cb_setChanPos(); }
void geRangeTool::cb_resetStartEnd(Fl_Widget* w, void* p) { ((geRangeTool*)p)->cb_resetStartEnd(); }


/* -------------------------------------------------------------------------- */


void geRangeTool::cb_setChanPos()
{
	c::sampleEditor::setBeginEnd(m_ch, atoi(m_begin->value()), atoi(m_end->value()));
	static_cast<gdSampleEditor*>(window())->waveTools->updateWaveform(); // TODO - glue's business!
}


/* -------------------------------------------------------------------------- */


void geRangeTool::cb_resetStartEnd()
{
	c::sampleEditor::setBeginEnd(m_ch, 0, m_ch->wave->getSize() - 1);
	static_cast<gdSampleEditor*>(window())->waveTools->updateWaveform(); // TODO - glue's business!
}
