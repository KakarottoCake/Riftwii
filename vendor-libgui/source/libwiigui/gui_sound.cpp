/****************************************************************************
 * libwiigui
 *
 * Tantric 2009
 *
 * gui_sound.cpp
 *
 * GUI class definitions
 ***************************************************************************/
/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: separate volumes for the hover sound and the others;
 * a menu theme's sounds in place of the built-in ones, at their own rate;
 * a built-in sound that was not decoded plays nothing;
 * resolve replacements at playback, so changing the sound source is live.
 * Every change is in RiftWii's git history; NOTICE.md lists the origin. */

#include "gui.h"
#include "../filelist.h"

int GuiSound::hoverPercent = 100;
int GuiSound::otherPercent = 100;
void (*GuiSound::substitute)(const u8*& sound, s32& length, s32& rate) = nullptr;

/**
 * Constructor for the GuiSound class.
 */
GuiSound::GuiSound(const u8 * s, s32 l, SOUND t)
{
	sound = s;
	length = l;
	type = t;
	voice = -1;
	volume = 100;
	loop = false;
	rate = 48000;
	hover = s == button_over_pcm;
}

/**
 * Destructor for the GuiSound class.
 */
GuiSound::~GuiSound()
{
	#ifndef NO_SOUND
	if(type == SOUND::OGG)
		StopOgg();
	#endif
}

void GuiSound::Play()
{
	#ifndef NO_SOUND
	int vol;

	switch(type)
	{
		case SOUND::PCM: {
		const u8* playingSound = sound;
		s32 playingLength = length;
		s32 playingRate = rate;
		if (substitute) substitute(playingSound, playingLength, playingRate);
		vol = 255*(volume/100.0) * (hover ? hoverPercent : otherPercent) / 100;
		if (vol <= 0)
			break;
		// RiftWii: a built-in sound that is not decoded (no memory) plays nothing.
		if (!playingSound || playingLength <= 0)
			break;
		voice = ASND_GetFirstUnusedVoice();
		if(voice >= 0)
			ASND_SetVoice(voice, VOICE_STEREO_16BIT, playingRate, 0,
				(u8 *)playingSound, playingLength, vol, vol, nullptr);
		break;
		}

		case SOUND::OGG:
		voice = 0;
		if(loop)
			PlayOgg((char *)sound, length, 0, OGG_INFINITE_TIME);
		else
			PlayOgg((char *)sound, length, 0, OGG_ONE_TIME);
		SetVolumeOgg(255*(volume/100.0));
		break;
	}
	#endif
}

void GuiSound::Stop()
{
	#ifndef NO_SOUND
	if(voice < 0)
		return;

	switch(type)
	{
		case SOUND::PCM:
		ASND_StopVoice(voice);
		break;

		case SOUND::OGG:
		StopOgg();
		break;
	}
	#endif
}

void GuiSound::Pause()
{
	#ifndef NO_SOUND
	if(voice < 0)
		return;

	switch(type)
	{
		case SOUND::PCM:
		ASND_PauseVoice(voice, 1);
		break;

		case SOUND::OGG:
		PauseOgg(1);
		break;
	}
	#endif
}

void GuiSound::Resume()
{
	#ifndef NO_SOUND
	if(voice < 0)
		return;

	switch(type)
	{
		case SOUND::PCM:
		ASND_PauseVoice(voice, 0);
		break;

		case SOUND::OGG:
		PauseOgg(0);
		break;
	}
	#endif
}

bool GuiSound::IsPlaying()
{
	if(ASND_StatusVoice(voice) == SND_WORKING || ASND_StatusVoice(voice) == SND_WAITING)
		return true;
	else
		return false;
}

void GuiSound::SetVolume(int vol)
{
	#ifndef NO_SOUND
	volume = vol;

	if(voice < 0)
		return;

	int newvol = 255*(volume/100.0);

	switch(type)
	{
		case SOUND::PCM:
		ASND_ChangeVolumeVoice(voice, newvol, newvol);
		break;

		case SOUND::OGG:
		SetVolumeOgg(255*(volume/100.0));
		break;
	}
	#endif
}

void GuiSound::SetLoop(bool l)
{
	loop = l;
}
