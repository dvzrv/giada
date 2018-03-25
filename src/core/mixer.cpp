/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2018 Giovanni A. Zuliani | Monocasual
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
#include <cstring>
#include "../deps/rtaudio-mod/RtAudio.h"
#include "../utils/log.h"
#include "wave.h"
#include "kernelAudio.h"
#include "recorder.h"
#include "pluginHost.h"
#include "conf.h"
#include "mixerHandler.h"
#include "clock.h"
#include "const.h"
#include "channel.h"
#include "sampleChannel.h"
#include "midiChannel.h"
#include "audioBuffer.h"
#include "mixer.h"


namespace giada {
namespace m {
namespace mixer
{
namespace
{
#define TICKSIZE 38


float tock[TICKSIZE] = {
	0.059033,  0.117240,  0.173807,  0.227943,  0.278890,  0.325936,
	0.368423,  0.405755,  0.437413,  0.462951,  0.482013,  0.494333,
	0.499738,  0.498153,  0.489598,  0.474195,  0.452159,  0.423798,
	0.389509,  0.349771,  0.289883,  0.230617,  0.173194,  0.118739,
	0.068260,  0.022631, -0.017423, -0.051339,	-0.078721, -0.099345,
 -0.113163, -0.120295, -0.121028, -0.115804, -0.105209, -0.089954,
 -0.070862, -0.048844
};


float tick[TICKSIZE] = {
	0.175860,  0.341914,  0.488904,  0.608633,  0.694426,  0.741500,
	0.747229,  0.711293,	0.635697,  0.524656,  0.384362,  0.222636,
	0.048496, -0.128348, -0.298035, -0.451105, -0.579021, -0.674653,
 -0.732667, -0.749830, -0.688924, -0.594091, -0.474481, -0.340160,
 -0.201360, -0.067752,  0.052194,  0.151746,  0.226280,  0.273493,
	0.293425,  0.288307,  0.262252,  0.220811,  0.170435,  0.117887,
	0.069639,  0.031320
};


AudioBuffer vChanInput;   // virtual channel for recording
AudioBuffer vChanInToOut; // virtual channel in->out bridge (hear what you're playin)

int  tickTracker, tockTracker = 0;
bool tickPlay, tockPlay = false; // 1 = play, 0 = stop

/* inputTracker
Sample position while recording. */

int inputTracker = 0;


/* -------------------------------------------------------------------------- */


bool isChannelAudible(Channel* ch)
{
	return !hasSolos || (hasSolos && ch->solo);
}


/* -------------------------------------------------------------------------- */

/* computePeak */

void computePeak(const AudioBuffer& buf, float& peak, unsigned frame)
{
	for (int i=0; i<buf.countChannels(); i++)
		if (buf[frame][i] > peak)
			peak = buf[frame][i];
}


/* -------------------------------------------------------------------------- */


/* lineInRec
Records from line in. */

void lineInRec(const AudioBuffer& inBuf, unsigned frame)
{
	if (!mh::hasArmedSampleChannels() || !kernelAudio::isInputEnabled() || !recording)
		return;

	/* Delay comp: wait until waitRec reaches delayComp. WaitRec returns to 0 in 
	mixerHandler, as soon as the recording ends. */

	if (waitRec < conf::delayComp) {
		waitRec++;
		return;
	}

	for (int i=0; i<vChanInput.countChannels(); i++)
		vChanInput[inputTracker][i] += inBuf[frame][i] * inVol;  // adding: overdub!

	inputTracker++;
	if (inputTracker >= clock::getFramesInLoop())
		inputTracker = 0;
}


/* -------------------------------------------------------------------------- */

/* ProcessLineIn
Computes line in peaks, plus handles "hear what you're playin'" thing. */

void processLineIn(const AudioBuffer& inBuf, unsigned frame)
{
	if (!kernelAudio::isInputEnabled())
		return;

	computePeak(inBuf, peakIn, frame);

	/* "hear what you're playing" - process, copy and paste the input buffer onto 
	the output buffer. */

	if (inToOut)
		for (int i=0; i<vChanInToOut.countChannels(); i++)
			vChanInToOut[frame][i] = inBuf[frame][i] * inVol;
}


/* -------------------------------------------------------------------------- */

/* clearAllBuffers
Cleans up every buffer, both in Mixer and in channels. */

void clearAllBuffers(AudioBuffer& outBuf)
{
	outBuf.clear();
	vChanInToOut.clear();

	pthread_mutex_lock(&mutex_chans);
	for (Channel* channel : channels)
		channel->clear();
	pthread_mutex_unlock(&mutex_chans);
}


/* -------------------------------------------------------------------------- */

/* readActions
Reads all recorded actions. */

void readActions(unsigned frame)
{
	pthread_mutex_lock(&mutex_recs);
	for (unsigned i=0; i<recorder::frames.size(); i++) {
		if (recorder::frames.at(i) != clock::getCurrentFrame())
			continue;
		for (recorder::action* action : recorder::global.at(i)) {
			Channel* ch = mh::getChannelByIndex(action->chan);
			ch->parseAction(action, frame, clock::getCurrentFrame(), 
				clock::getQuantize(), clock::isRunning());
		}
		break;
	}
	pthread_mutex_unlock(&mutex_recs);
}


/* -------------------------------------------------------------------------- */

/* doQuantize
Computes quantization on 'rewind' button and all channels. */

void doQuantize(unsigned frame)
{
	/* Nothing to do if quantizer disabled or a quanto has not passed yet. */

	if (clock::getQuantize() == 0 || !clock::quantoHasPassed())
		return;

	if (rewindWait) {
		rewindWait = false;
		rewind();
	}

	pthread_mutex_lock(&mutex_chans);
	for (unsigned i=0; i<channels.size(); i++)
		channels.at(i)->quantize(i, frame, clock::getCurrentFrame());
	pthread_mutex_unlock(&mutex_chans);
}


/* -------------------------------------------------------------------------- */

/* sumChannels
Sums channels, i.e. lets them add sample frames to their virtual channels.
This is required for G_CHANNEL_SAMPLE only */

void sumChannels(unsigned frame)
{
	pthread_mutex_lock(&mutex_chans);
	for (Channel* ch : channels)
		if (ch->type == G_CHANNEL_SAMPLE)
			static_cast<SampleChannel*>(ch)->sum(frame, clock::isRunning());
	pthread_mutex_unlock(&mutex_chans);
}


/* -------------------------------------------------------------------------- */

/* renderMetronome
Generates metronome when needed and pastes it to the output buffer. */

void renderMetronome(AudioBuffer& outBuf, unsigned frame)
{
	if (tockPlay) {
		for (int i=0; i<outBuf.countChannels(); i++)
			outBuf[frame][i] += tock[tockTracker];
		tockTracker++;
		if (tockTracker >= TICKSIZE-1) {
			tockPlay    = false;
			tockTracker = 0;
		}
	}
	if (tickPlay) {
		for (int i=0; i<outBuf.countChannels(); i++)
			outBuf[frame][i] += tick[tickTracker];
		tickTracker++;
		if (tickTracker >= TICKSIZE-1) {
			tickPlay    = false;
			tickTracker = 0;
		}
	}
}


/* -------------------------------------------------------------------------- */

/* renderIO
Final processing stage. Take each channel and process it (i.e. copy its
content to the output buffer). Process plugins too, if any. */

void renderIO(AudioBuffer& outBuf, const AudioBuffer& inBuf)
{
	pthread_mutex_lock(&mutex_chans);
	for (Channel* ch : channels) {
		if (isChannelAudible(ch))
			ch->process(outBuf, inBuf);
		ch->preview(outBuf);
	}
	pthread_mutex_unlock(&mutex_chans);

#ifdef WITH_VST
	pthread_mutex_lock(&mutex_plugins);
	pluginHost::processStack(outBuf, pluginHost::MASTER_OUT);
	pluginHost::processStack(vChanInToOut, pluginHost::MASTER_IN);
	pthread_mutex_unlock(&mutex_plugins);
#endif
}


/* -------------------------------------------------------------------------- */

/* limitOutput
Applies a very dumb hard limiter. */

void limitOutput(AudioBuffer& outBuf, unsigned frame)
{
	for (int i=0; i<outBuf.countChannels(); i++)
		if      (outBuf[frame][i] > 1.0f)
			outBuf[frame][i] = 1.0f;
		else if (outBuf[frame][i] < -1.0f)	
			outBuf[frame][i] = -1.0f;	
}


/* -------------------------------------------------------------------------- */

/* finalizeOutput
Last touches after the output has been rendered: apply inToOut if any, apply
output volume. */

void finalizeOutput(AudioBuffer& outBuf, unsigned frame)
{
	/* Merge vChanInToOut, if enabled. */

	if (inToOut)
		outBuf.copyFrame(frame, vChanInToOut[frame]); 

	for (int i=0; i<outBuf.countChannels(); i++)
		outBuf[frame][i] *= outVol; 
}


/* -------------------------------------------------------------------------- */

/* test*
Checks if the sequencer has reached a specific point (bar, first beat or
last frame). */

void testBar(unsigned frame)
{
	if (!clock::isOnBar())
		return;

	if (metronome)
		tickPlay = true;

	pthread_mutex_lock(&mutex_chans);
	for (Channel* ch : channels)
		ch->onBar(frame);
	pthread_mutex_unlock(&mutex_chans);
}


/* -------------------------------------------------------------------------- */


void testFirstBeat(unsigned frame)
{
	if (!clock::isOnFirstBeat())
		return;
	pthread_mutex_lock(&mutex_chans);
	for (Channel* ch : channels)
		ch->onZero(frame, conf::recsStopOnChanHalt);
	pthread_mutex_unlock(&mutex_chans);
}


/* -------------------------------------------------------------------------- */


void testLastBeat()
{
	if (clock::isOnBeat())
		if (metronome && !tickPlay)
			tockPlay = true;
}

}; // {anonymous}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */


std::vector<Channel*> channels;

bool   recording    = false;
bool   ready        = true;
float  outVol       = G_DEFAULT_OUT_VOL;
float  inVol        = G_DEFAULT_IN_VOL;
float  peakOut      = 0.0f;
float  peakIn       = 0.0f;
bool	 metronome    = false;
int    waitRec      = 0;
bool   rewindWait   = false;
bool   hasSolos     = false;
bool   inToOut      = false;

pthread_mutex_t mutex_recs;
pthread_mutex_t mutex_chans;
pthread_mutex_t mutex_plugins;


/* -------------------------------------------------------------------------- */


void init(int framesInSeq, int framesInBuffer)
{
	/* Allocate virtual input channels. vChanInput has variable size: it depends
	on how many frames there are in sequencer. */
	if (!allocVirtualInput(framesInSeq)) {
		gu_log("[Mixer::init] vChanInput alloc error!\n");	
		return;
	}
	if (!vChanInToOut.alloc(framesInBuffer, G_MAX_IO_CHANS)) {
		gu_log("[Mixer::init] vChanInToOut alloc error!\n");	
		return;
	}

	gu_log("[Mixer::init] buffers ready - framesInSeq=%d, framesInBuffer=%d\n", 
		framesInSeq, framesInBuffer);	

	hasSolos = false;

	pthread_mutex_init(&mutex_recs, nullptr);
	pthread_mutex_init(&mutex_chans, nullptr);
	pthread_mutex_init(&mutex_plugins, nullptr);

	rewind();
}


/* -------------------------------------------------------------------------- */


bool allocVirtualInput(int frames)
{
	return vChanInput.alloc(frames, G_MAX_IO_CHANS);
}


/* -------------------------------------------------------------------------- */


int masterPlay(void* outBuf, void* inBuf, unsigned bufferSize, 
	double streamTime, RtAudioStreamStatus status, void* userData)
{
	if (!ready)
		return 0;

#ifdef __linux__
	clock::recvJackSync();
#endif

	AudioBuffer out, in;
	out.setData((float*) outBuf, bufferSize, G_MAX_IO_CHANS);
	if (kernelAudio::isInputEnabled())
		in.setData((float*) inBuf, bufferSize, G_MAX_IO_CHANS);

	peakOut = 0.0f;  // reset peak calculator
	peakIn  = 0.0f;  // reset peak calculator

	clearAllBuffers(out);

	for (unsigned j=0; j<bufferSize; j++) {
		processLineIn(in, j);
		if (clock::isRunning()) {
			lineInRec(in, j);
			doQuantize(j);
			testBar(j);
			testFirstBeat(j);
			readActions(j);
			clock::incrCurrentFrame();
			testLastBeat();  // this test must be the last one
			clock::sendMIDIsync();
		}
		sumChannels(j);
	}

	renderIO(out, in);

	/* Post processing. */
	for (unsigned j=0; j<bufferSize; j++) {
		finalizeOutput(out, j); 
		if (conf::limitOutput)
			limitOutput(out, j);
		computePeak(out, peakOut, j); 
		renderMetronome(out, j);
	}

	/* Unset data in buffers. If you don't do this, buffers go out of scope and
	destroy memory allocated by RtAudio ---> havoc. */
	out.setData(nullptr, 0, 0);
	in.setData(nullptr, 0, 0);

	return 0;
}


/* -------------------------------------------------------------------------- */


void close()
{
	clock::stop();
	while (channels.size() > 0)
		mh::deleteChannel(channels.at(0));
}


/* -------------------------------------------------------------------------- */


bool isSilent()
{
	for (const Channel* ch : channels)
		if (ch->status == STATUS_PLAY)
			return false;
	return true;
}


/* -------------------------------------------------------------------------- */


void rewind()
{
	clock::rewind();
	if (clock::isRunning())
		for (Channel* ch : channels)
			ch->rewind();
}


/* -------------------------------------------------------------------------- */


void startInputRec()
{
	/* Start inputTracker from the current frame, not the beginning. */
	recording    = true;
	inputTracker = clock::getCurrentFrame();
}

/* -------------------------------------------------------------------------- */


void mergeVirtualInput()
{
	for (Channel* ch : channels) {
		if (ch->type == G_CHANNEL_MIDI)
			continue;
		SampleChannel* sch = static_cast<SampleChannel*>(ch);
		if (sch->armed)
			sch->wave->copyData(vChanInput[0], vChanInput.countFrames());
	}
	vChanInput.clear();
}


}}}; // giada::m::mixer::
