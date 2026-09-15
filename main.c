/*

Notes for future me (@rommix0), and others wanting to replicate the make process.

do this to build the program:
	-> gcc main.c -o program -lwinmm

*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include "drums.h"
#include "patterns.h"

int getOffset(int select) {
	int offset = 0;
	switch (select) {
		case 0:
			//tango
			offset = 0;
			break;
		case 1:
			//charleston
			offset = 164;
			break;
		case 2:
			//paso
			offset = 328;
			break;
		case 3:
			//waltz
			offset = 654;
			break;
		case 4:
			//twist
			offset = 1022;
			break;
		case 5:
			//ska
			offset = 1510;
			break;
		case 6:
			//march
			offset = 1998;
			break;
		case 7:
			//cha cha
			offset = 2324;
			break;
		case 8:
			//boogie
			offset = 2812;
			break;
		case 9:
			//blues
			offset = 3240;
			break;
		case 10:
			//swing
			offset = 3791;
			break;
		case 11:
			//shuffle
			offset = 4342;
			break;
		case 12:
			//samba
			offset = 4893;
			break;
		case 13:
			//jazz
			offset = 5624;
			break;
		case 14:
			//bossa
			offset = 6175;
			break;
		case 15:
			//afro cuban
			offset = 6906;
			break;
		case 16:
			//disco
			offset = 8123;
			break;
		case 17:
			//funk
			offset = 9826;
			break;
		case 18:
			//pop
			offset = 12258;
			break;
		case 19:
			//r&b
			offset = 13718;
			break;
		case 20:
			//reggae
			offset = 15178;
			break;
		case 21:
			//rock
			offset = 16761;
			break;
		case 22:
			//slow
			offset = 18950;
			break;
		default:
			// ending
			offset = 20410;
	}
	return offset;
}

int BPMToSixteenth(int bpm) {
	int x = bpm;
	if (x < 60) {
		x = 60;
	} else if (x > 300) {
		x = 300;
	}
	return (60000 / x) / 4;
}

UINT sendMIDIEvent(HMIDIOUT hmo, BYTE bStatus, BYTE bData1, BYTE bData2) {
    union {
        DWORD dwData;
        BYTE bData[4];
    } u;

    // Construct the MIDI message.
    u.bData[0] = bStatus;  // MIDI status byte
    u.bData[1] = bData1;   // first MIDI data byte
    u.bData[2] = bData2;   // second MIDI data byte
    u.bData[3] = 0;

    // Send the message.
    return midiOutShortMsg(hmo, u.dwData);
}

void triggerDrum(HMIDIOUT hmo, int note, int bpm, int flam, int accent) {
	int velocity = VELO_DEF;
	int delay = BPMToSixteenth(bpm);
	if (accent != 0) { velocity = VELO_ACC; }
	
	sendMIDIEvent(hmo, DRUM_NOTE_ON, note, velocity);
	if (flam != 0) {
		Sleep(FLAM_DELAY);
		sendMIDIEvent(hmo, DRUM_NOTE_OFF, note, 0);
		sendMIDIEvent(hmo, DRUM_NOTE_ON, note, velocity);
		Sleep(delay-FLAM_DELAY);
	} else {
		Sleep(delay);
	}
	sendMIDIEvent(hmo, DRUM_NOTE_OFF, note, 0);
}

int main(int argc, char* argv[])
{
	// attempt to open MIDI output device
	int bpm = 90;
	HMIDIOUT midi_device;
	if (midiOutOpen(&midi_device, 0, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
		printf("midi device couldn\'t be opened!\n");
		return 1;
	}

	// test MIDI setup
	printf("you should hear some drums now!\n");
	for (int i = 0; i < 4; i++) {
		triggerDrum(midi_device, BASS_DRUM, bpm, 0, 1);
		triggerDrum(midi_device, BASS_DRUM, bpm, 0, 0);
		triggerDrum(midi_device, ACOUSTIC_SNARE, bpm, 0, 0);
		triggerDrum(midi_device, BASS_DRUM, bpm, 0, 0);
		triggerDrum(midi_device, BASS_DRUM, bpm, 0, 1);
		triggerDrum(midi_device, BASS_DRUM, bpm, 0, 0);
		triggerDrum(midi_device, ACOUSTIC_SNARE, bpm, 1, 0);
		triggerDrum(midi_device, BASS_DRUM, bpm, 0, 0);
	}

	// close the MIDI output device
	midiOutReset(midi_device);
	midiOutClose(midi_device);
	return 0;
}
