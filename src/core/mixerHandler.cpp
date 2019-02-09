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
#include <vector>
#include <algorithm>
#include "../utils/fs.h"
#include "../utils/string.h"
#include "../utils/log.h"
#include "../utils/vector.h"
#include "../glue/main.h"
#include "../glue/channel.h"
#include "kernelMidi.h"
#include "mixer.h"
#include "const.h"
#include "init.h"
#include "pluginHost.h"
#include "pluginManager.h"
#include "plugin.h"
#include "waveFx.h"
#include "conf.h"
#include "patch.h"
#include "recorder.h"
#include "clock.h"
#include "channel.h"
#include "kernelAudio.h"
#include "midiMapConf.h"
#include "sampleChannel.h"
#include "midiChannel.h"
#include "wave.h"
#include "waveManager.h"
#include "channelManager.h"
#include "mixerHandler.h"


using std::vector;
using std::string;


namespace giada {
namespace m {
namespace mh
{
namespace
{
#ifdef WITH_VST

int readPatchPlugins_(const vector<patch::plugin_t>& list, pluginHost::StackType t)
{
	int ret = 1;
	for (const patch::plugin_t& ppl : list) {
		std::unique_ptr<Plugin> p = pluginManager::makePlugin(ppl.path);
		if (p != nullptr) {
			p->setBypass(ppl.bypass);
			for (unsigned j=0; j<ppl.params.size(); j++)
				p->setParameter(j, ppl.params.at(j));
			pluginHost::addPlugin(std::move(p), t, &mixer::mutex, nullptr);
			ret &= 1;
		}
		else
			ret &= 0;
	}
	return ret;
}

#endif


/* -------------------------------------------------------------------------- */


int getNewChanIndex()
{
	/* always skip last channel: it's the last one just added */

	if (mixer::channels.size() == 1)
		return 0;

	int index = 0;
	for (unsigned i=0; i<mixer::channels.size()-1; i++) {
		if (mixer::channels.at(i)->index > index)
			index = mixer::channels.at(i)->index;
		}
	index += 1;
	return index;
}


}; // {anonymous}


/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */


bool uniqueSamplePath(const SampleChannel* skip, const string& path)
{
	for (const Channel* ch : mixer::channels) {
		if (skip == ch || ch->type != ChannelType::SAMPLE) // skip itself and MIDI channels
			continue;
		const SampleChannel* sch = static_cast<const SampleChannel*>(ch);
		if (sch->wave != nullptr && path == sch->wave->getPath())
			return false;
	}
	return true;
}


/* -------------------------------------------------------------------------- */


Channel* addChannel(ChannelType type)
{
	Channel* ch = channelManager::create(type, kernelAudio::getRealBufSize(), 
		conf::inputMonitorDefaultOn);

	pthread_mutex_lock(&mixer::mutex);
	mixer::channels.push_back(ch);
	pthread_mutex_unlock(&mixer::mutex);

	ch->index = getNewChanIndex();
	gu_log("[addChannel] channel index=%d added, type=%d, total=%d\n",
		ch->index, ch->type, mixer::channels.size());
	return ch;
}


/* -------------------------------------------------------------------------- */


void deleteChannel(Channel* target)
{
	int index = u::vector::indexOf(mixer::channels, target);
	assert(index != -1);
	
	pthread_mutex_lock(&mixer::mutex);
	delete mixer::channels.at(index);
	mixer::channels.erase(mixer::channels.begin() + index);
	pthread_mutex_unlock(&mixer::mutex);
}


/* -------------------------------------------------------------------------- */


Channel* getChannelByIndex(int index)
{
	for (Channel* ch : mixer::channels)
		if (ch->index == index)
			return ch;
	gu_log("[getChannelByIndex] channel at index %d not found!\n", index);
	return nullptr;
}


/* -------------------------------------------------------------------------- */


bool hasLogicalSamples()
{
	for (const Channel* ch : mixer::channels)
		if (ch->hasLogicalData())
			return true;
	return false;
}


/* -------------------------------------------------------------------------- */


bool hasEditedSamples()
{
	for (const Channel* ch : mixer::channels)
		if (ch->hasEditedData())
			return true;
	return false;
}


/* -------------------------------------------------------------------------- */


void stopSequencer()
{
	clock::stop();
	for (Channel* ch : mixer::channels)
		ch->stopBySeq(conf::chansStopOnSeqHalt);
}


/* -------------------------------------------------------------------------- */


void updateSoloCount()
{
	for (const Channel* ch : mixer::channels)
		if (ch->solo) {
			mixer::hasSolos = true;
			return;
		}
	mixer::hasSolos = false;
}


/* -------------------------------------------------------------------------- */


void readPatch()
{
	mixer::ready = false;

	mixer::outVol.store(patch::masterVolOut);
	mixer::inVol.store(patch::masterVolIn);
	clock::setBpm(patch::bpm);
	clock::setBars(patch::bars);
	clock::setBeats(patch::beats);
	clock::setQuantize(patch::quantize);
	clock::updateFrameBars();
	mixer::setMetronome(patch::metronome);

#ifdef WITH_VST

	readPatchPlugins_(patch::masterInPlugins, pluginHost::StackType::MASTER_IN);
	readPatchPlugins_(patch::masterOutPlugins, pluginHost::StackType::MASTER_OUT);

#endif

	/* Rewind and update frames in Mixer. Also alloc new space in the virtual
	input buffer, in case the patch has a sequencer size != default one (which is
	very likely). */

	mixer::rewind();
	mixer::allocVirtualInput(clock::getFramesInLoop());
	mixer::ready = true;
}


/* -------------------------------------------------------------------------- */


void rewindSequencer()
{
	if (clock::getQuantize() > 0 && clock::isRunning())   // quantize rewind
		mixer::rewindWait = true;
	else
		mixer::rewind();
}


/* -------------------------------------------------------------------------- */


bool startInputRec()
{
	int channelsReady = 0;

	for (Channel* ch : mixer::channels) {

		if (!ch->canInputRec())
			continue;

		SampleChannel* sch = static_cast<SampleChannel*>(ch);

		/* Allocate empty sample for the current channel. */

		string name    = string("TAKE-" + u::string::iToString(patch::lastTakeId++));
		string nameExt = name + ".wav";

		sch->pushWave(waveManager::createEmpty(clock::getFramesInLoop(), 
			G_MAX_IO_CHANS, conf::samplerate, nameExt));
		sch->name = name; 
		channelsReady++;

		gu_log("[startInputRec] start input recs using chan %d with size %d "
			"on frame=%d\n", sch->index, clock::getFramesInLoop(), clock::getCurrentFrame());
	}
	
	if (channelsReady == 0)
		return false;

	mixer::startInputRec();
	return true;
}


/* -------------------------------------------------------------------------- */


void stopInputRec()
{
	mixer::mergeVirtualInput();
	mixer::recording = false;

	for (Channel* ch : mixer::channels)
		ch->stopInputRec(clock::getCurrentFrame());

	gu_log("[mh] stop input recs\n");
}


/* -------------------------------------------------------------------------- */


bool hasArmedSampleChannels()
{
	for (const Channel* ch : mixer::channels)
		if (ch->type == ChannelType::SAMPLE && ch->armed)
			return true;
	return false;
}


}}}; // giada::m::mh::
