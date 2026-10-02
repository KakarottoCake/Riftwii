/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: separate volumes for the hover sound and the others.
 * Every change is in RiftWii's git history; NOTICE.md lists the origin. */
#ifndef LIBWIIGUI_SOUND_H
#define LIBWIIGUI_SOUND_H

enum class SOUND {
	PCM,
	OGG
};

//!Sound conversion and playback. A wrapper for other sound libraries - ASND, libmad, ltremor, etc
class GuiSound {
public:
	//!Constructor
	//!\param s Pointer to the sound data
	//!\param l Length of sound data
	//!\param t Sound format type (PCM or OGG)
	GuiSound(const u8 * s, s32 l, SOUND t);
	//!Destructor
	~GuiSound();
	//!Start sound playback
	void Play();
	//!Stop sound playback
	void Stop();
	//!Pause sound playback
	void Pause();
	//!Resume sound playback
	void Resume();
	//!Checks if the sound is currently playing
	//!\return true if sound is playing, false otherwise
	bool IsPlaying();
	//!Set sound volume
	//!\param v Sound volume (0-100)
	void SetVolume(int v);
	//!Set the sound to loop playback (only applies to OGG)
	//!\param l Loop (true to loop)
	void SetLoop(bool l);
	//!Riftwii: every PCM sound's volume is scaled by these (percent): the
	//!hover tick (button_over_pcm) by hoverPercent, the rest by otherPercent.
	static int hoverPercent;
	static int otherPercent;
protected:
	const u8 * sound; //!< Pointer to the sound data
	SOUND type; //!< Sound format type (PCM or OGG)
	s32 length; //!< Length of sound data
	s32 voice; //!< Currently assigned ASND voice channel
	s32 volume; //!< Sound volume (0-100)
	bool loop; //!< Loop sound playback
};

#endif
