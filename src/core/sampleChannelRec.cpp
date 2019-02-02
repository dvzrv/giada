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


#include <cassert>
#include "../utils/math.h"
#include "recorder.h"
#include "recorderHandler.h"
#include "const.h"
#include "conf.h"
#include "clock.h"
#include "action.h"
#include "kernelAudio.h"
#include "sampleChannel.h"
#include "sampleChannelRec.h"


namespace giada {
namespace m {
namespace sampleChannelRec
{
namespace
{
/* onFirstBeat
Things to do when the sequencer is on the first beat. */

void onFirstBeat_(SampleChannel* ch, bool recsStopOnChanHalt)
{
	if (ch->wave == nullptr)
		return;

	switch (ch->recStatus) {
		case ChannelStatus::ENDING:
			ch->recStatus = ChannelStatus::OFF;
			setReadActions(ch, false, recsStopOnChanHalt);  // rec stop
			break;

		case ChannelStatus::WAIT:
			ch->recStatus = ChannelStatus::PLAY;
			setReadActions(ch, true, recsStopOnChanHalt);   // rec start			
			break;

		default: break;
	}
}


/* -------------------------------------------------------------------------- */


bool recorderCanRec_(SampleChannel* ch)
{
	/* Can record on a channel if:
		- recorder is on
		- mixer is running
		- mixer is not recording a take somewhere
		- channel is MIDI or SAMPLE type with data in it  */

	return recorder::isActive() && clock::isRunning() && !mixer::recording && 
		(ch->type == ChannelType::MIDI || (ch->type == ChannelType::SAMPLE && ch->hasData()));
}


/* -------------------------------------------------------------------------- */


/* calcVolumeEnv
Computes any changes in volume done via envelope tool. */

void calcVolumeEnv_(SampleChannel* ch, const Action* a1)
{
	assert(a1 != nullptr);
	assert(a1->next != nullptr);

	const Action* a2 = a1->next;

	double vf1 = u::math::map<int, double>(a1->event.getVelocity(), 0, G_MAX_VELOCITY, 0, 1.0);
	double vf2 = u::math::map<int, double>(a2->event.getVelocity(), 0, G_MAX_VELOCITY, 0, 1.0);

	ch->volume_i = vf1;
	ch->volume_d = a2->frame == a1->frame ? 0 : (vf2 - vf1) / (a2->frame - a1->frame);
}


/* -------------------------------------------------------------------------- */


void parseAction_(SampleChannel* ch, const Action* a, int localFrame, int globalFrame)
{
	if (!ch->readActions)
		return;

	switch (a->event.getStatus()) {
		case MidiEvent::NOTE_ON:
			if (ch->isAnySingleMode()) {
				ch->start(localFrame, false, 0);
				/* This is not a user-generated event, so fill the first chunk of buffer. 
				Then, sampleChannelProc::prepareBuffer will take care of filling the
				subsequent buffers from the next cycle on. */
				if (ch->status == ChannelStatus::PLAY)
					ch->tracker += ch->fillBuffer(ch->buffer, ch->tracker, localFrame);
			}
			break;
		case MidiEvent::NOTE_OFF:
			if (ch->isAnySingleMode())
				ch->stop();
			break;
		case MidiEvent::NOTE_KILL:
			if (ch->isAnySingleMode())
				ch->kill(localFrame);
			break;
		case MidiEvent::ENVELOPE:
			calcVolumeEnv_(ch, a);
			break;
	}
}


/* -------------------------------------------------------------------------- */


void recordKeyPressAction_(SampleChannel* ch)
{
	if (!recorderCanRec_(ch))
		return;

	/* Disable reading actions while recording SINGLE_PRESS mode. */
	if (ch->mode == ChannelMode::SINGLE_PRESS)
		ch->readActions = false;
	
	recorderHandler::liveRec(ch->index, MidiEvent(MidiEvent::NOTE_ON, 0, 0));
	ch->hasActions = true;
}


/* -------------------------------------------------------------------------- */


void quantize_(SampleChannel* ch, bool quantoPassed)
{
	/* Skip if LOOP_ANY or not in quantizer-wait mode. Otherwise the quantize wait 
	has expired: record the keypress.  */

	if (ch->isAnyLoopMode() || !ch->qWait || !quantoPassed)
		return;
	recordKeyPressAction_(ch);
}
}; // {anonymous}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */


void parseEvents(SampleChannel* ch, mixer::FrameEvents fe)
{
	quantize_(ch, fe.quantoPassed);
	if (fe.onFirstBeat)
		onFirstBeat_(ch, conf::recsStopOnChanHalt);
	for (const Action* action : fe.actions)
		if (action->channel == ch->index)
			parseAction_(ch, action, fe.frameLocal, fe.frameGlobal);
}


/* -------------------------------------------------------------------------- */


bool recordStart(SampleChannel* ch, bool canQuantize)
{
	/* Record a 'start' event if the quantizer is off, otherwise let mixer to 
	handle it when a quantoWait has passed (see quantize_()). Also skip if 
	channel is in any loop mode, where KEYPRESS and KEYREL are meaningless. */

	if (!canQuantize && !ch->isAnyLoopMode() && recorderCanRec_(ch))
		recordKeyPressAction_(ch);
	return true;
}


/* -------------------------------------------------------------------------- */


bool recordKill(SampleChannel* ch)
{
	/* Don't record NOTE_KILL actions for LOOP channels. */
	if (recorderCanRec_(ch) && !ch->isAnyLoopMode()) {
		recorder::rec(ch->index, clock::getCurrentFrame(), MidiEvent(MidiEvent::NOTE_KILL, 0, 0));
		ch->hasActions = true;
	}
	return true;
}


/* -------------------------------------------------------------------------- */


void recordStop(SampleChannel* ch)
{
	/* Record a stop event only if channel is SINGLE_PRESS. For any other mode 
	the stop event is meaningless. */
	if (recorderCanRec_(ch) && ch->mode == ChannelMode::SINGLE_PRESS)
		recorderHandler::liveRec(ch->index, MidiEvent(MidiEvent::NOTE_OFF, 0, 0));
}


/* -------------------------------------------------------------------------- */


void setReadActions(SampleChannel* ch, bool v, bool recsStopOnChanHalt)
{
	ch->readActions = v;
	if (!ch->readActions && recsStopOnChanHalt)
		ch->kill(0); // FIXME - wrong frame value
}


/* -------------------------------------------------------------------------- */


void startReadingActions(SampleChannel* ch, bool treatRecsAsLoops, bool recsStopOnChanHalt)
{
	if (treatRecsAsLoops)
		ch->recStatus = ChannelStatus::WAIT;
	else
		setReadActions(ch, true, recsStopOnChanHalt);
}


/* -------------------------------------------------------------------------- */


void stopReadingActions(SampleChannel* ch, bool isClockRunning, bool treatRecsAsLoops, 
		bool recsStopOnChanHalt)
{
	/* First of all, if the clock is not running just stop and disable everything.
	Then if "treatRecsAsLoop" wait until the sequencer reaches beat 0, so put the
	channel in REC_ENDING status. */

	if (!isClockRunning) {
		ch->recStatus = ChannelStatus::OFF;
		setReadActions(ch, false, false);
	}
	else
	if (ch->recStatus == ChannelStatus::WAIT)
		ch->recStatus = ChannelStatus::OFF;
	else
	if (ch->recStatus == ChannelStatus::ENDING)
		ch->recStatus = ChannelStatus::PLAY;
	else
	if (treatRecsAsLoops)
		ch->recStatus = ChannelStatus::ENDING;
	else
		setReadActions(ch, false, recsStopOnChanHalt);
}
}}};