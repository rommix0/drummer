/*

Notes for future me (@rommix0), and others wanting to replicate the make process.

do this to build the program:
	-> gcc main.c -o program -lwinmm

usage:
	-> program [style] [bpm]

	style is a number (0-22) or a name ("rock", "cha cha", "r&b", ...)
	bpm defaults to 120 and is clamped to 60-300

keys while playing:
	left / right    previous / next variation (switches at the end of the bar)
	up / down       next / previous style     (switches at the end of the bar)
	- / +           tempo down / up
	b or space      play a break on the next bar
	e               play an ending, then stop
	q or esc        quit right away

*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "drums.h"
#include "patterns.h"

// opcodes of the pattern table (see patterns.h)
#define OP_STEP        0xFC
#define OP_END_PATTERN 0xFD
#define OP_BREAK       0xFE
#define OP_END_STYLE   0xFF

#define NUM_STYLES   23
#define STYLE_WALTZ  3
#define STYLE_ENDING 23
#define MAX_PATTERNS 16

#define BPM_MIN  60
#define BPM_MAX  300
#define BPM_STEP 5

static const char *styleNames[] = {
	"tango", "charleston", "paso", "waltz", "twist", "ska", "march", "cha cha",
	"boogie", "blues", "swing", "shuffle", "samba", "jazz", "bossa", "afro cuban",
	"disco", "funk", "pop", "r&b", "reggae", "rock", "slow", "ending"
};

// each step is a 32-bit word made of 2-bit instrument slots (00 silent, 01 hit, 10 flam).
// slot n lives at bits 2+2n, starting from the bass drum. the accent slot sits on top.
#define NUM_SOUNDS   13
#define ACCENT_SHIFT 28
static const int slotNotes[NUM_SOUNDS] = {
	BASS_DRUM,       // BD
	COWBELL,         // CB
	TAMBOURINE,      // TMB
	HAND_CLAP,       // CPS
	HIGH_FLOOR_TOM,  // LT
	SIDE_STICK,      // RS
	ACOUSTIC_SNARE,  // SD
	LOW_MID_TOM,     // MT
	HIGH_TOM,        // HT
	OPEN_HI_HAT,     // OH
	CLOSED_HI_HAT,   // CH
	CRASH_CYMBAL_A,  // CY.C
	RIDE_CYMBAL_A    // CY.R
};

// where every main and break pattern of a style starts in the pattern table
typedef struct {
	const unsigned char *mains[MAX_PATTERNS];
	const unsigned char *breaks[MAX_PATTERNS];
	int numMains;
	int numBreaks;
} Style;

// what the keyboard asks the player to do
typedef struct {
	int style;
	int variation;
	int bpm;
	int breakQueued;
	int endingQueued;
	int quit;
	const char *state; // shown on the status line
} Controls;

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

int clampBPM(int bpm) {
	if (bpm < BPM_MIN) {
		return BPM_MIN;
	} else if (bpm > BPM_MAX) {
		return BPM_MAX;
	}
	return bpm;
}

// walk one style partition of the pattern table and remember where its patterns start
void loadStyle(int select, Style *style) {
	const unsigned char *p = patterns + getOffset(select);
	const unsigned char **list = style->mains;
	int *count = &style->numMains;

	style->numMains = 0;
	style->numBreaks = 0;
	while (*p != OP_END_STYLE) {
		if (*p == OP_BREAK) {
			list = style->breaks;
			count = &style->numBreaks;
			p++;
			continue;
		}
		if (*count < MAX_PATTERNS) {
			list[(*count)++] = p;
		}
		while (*p == OP_STEP) {
			p += 5;
		}
		p++; // OP_END_PATTERN
	}
}

int patternLength(const unsigned char *pattern) {
	int steps = 0;
	while (pattern[steps * 5] == OP_STEP) {
		steps++;
	}
	return steps;
}

DWORD patternStep(const unsigned char *pattern, int step) {
	const unsigned char *b = pattern + step * 5 + 1;
	return b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD)b[3] << 24);
}

// a pattern is always one bar: 16 steps are sixteenths, 12 steps are triplet eighths.
// waltz is the odd one out, its 12 steps are sixteenths in a bar of 3/4.
double stepMs(int style, int bpm, int steps) {
	int beats = (style == STYLE_WALTZ) ? 3 : 4;
	return 60000.0 * beats / ((double)bpm * steps);
}

double nowMs(void) {
	static LARGE_INTEGER freq;
	LARGE_INTEGER t;
	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
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

void printStatus(const Controls *c) {
	Style style;
	loadStyle(c->style, &style);
	printf("\r%-10s  variation %2d/%-2d  %3d bpm  %-8s",
		styleNames[c->style], c->variation + 1, style.numMains, c->bpm, c->state);
	fflush(stdout);
}

void handleKeys(Controls *c) {
	while (_kbhit()) {
		int key = _getch();
		int styleChanged = 0;
		Style next;

		if (key == 0 || key == 0xE0) {
			// arrow keys come in as two codes
			loadStyle(c->style, &next);
			switch (_getch()) {
				case 72: // up
					c->style = (c->style + 1) % NUM_STYLES;
					styleChanged = 1;
					break;
				case 80: // down
					c->style = (c->style + NUM_STYLES - 1) % NUM_STYLES;
					styleChanged = 1;
					break;
				case 75: // left
					c->variation = (c->variation + next.numMains - 1) % next.numMains;
					break;
				case 77: // right
					c->variation = (c->variation + 1) % next.numMains;
					break;
			}
			if (styleChanged) {
				c->variation = 0;
			}
			printStatus(c);
			continue;
		}

		switch (tolower(key)) {
			case '+':
			case '=':
				c->bpm = clampBPM(c->bpm + BPM_STEP);
				break;
			case '-':
			case '_':
				c->bpm = clampBPM(c->bpm - BPM_STEP);
				break;
			case 'b':
			case ' ':
				c->breakQueued = 1;
				break;
			case 'e':
				c->endingQueued = 1;
				break;
			case 'q':
			case 27: // esc
				c->quit = 1;
				break;
		}
		printStatus(c);
	}
}

// wait until the given time while still listening to the keyboard
void waitUntil(double target, Controls *c) {
	double left;
	while (!c->quit) {
		handleKeys(c);
		left = target - nowMs();
		if (left <= 0) {
			break;
		}
		Sleep(left > 2.0 ? 1 : 0);
	}
}

// play one bar of a pattern. hits go out at the start of the step,
// flams get their second stroke FLAM_DELAY later.
void playPattern(HMIDIOUT hmo, const unsigned char *pattern, int style, Controls *c) {
	int steps = patternLength(pattern);
	double t = nowMs();

	for (int i = 0; i < steps && !c->quit; i++) {
		DWORD step = patternStep(pattern, i);
		int velocity = ((step >> ACCENT_SHIFT) & 3) ? VELO_ACC : VELO_DEF;
		int flams = 0;

		for (int n = 0; n < NUM_SOUNDS; n++) {
			int hit = (step >> (2 + 2 * n)) & 3;
			if (hit != 0) {
				sendMIDIEvent(hmo, DRUM_NOTE_OFF, slotNotes[n], 0);
				sendMIDIEvent(hmo, DRUM_NOTE_ON, slotNotes[n], velocity);
			}
			if (hit & 2) {
				flams = 1;
			}
		}

		if (flams) {
			waitUntil(t + FLAM_DELAY, c);
			for (int n = 0; n < NUM_SOUNDS; n++) {
				if ((step >> (2 + 2 * n)) & 2) {
					sendMIDIEvent(hmo, DRUM_NOTE_OFF, slotNotes[n], 0);
					sendMIDIEvent(hmo, DRUM_NOTE_ON, slotNotes[n], velocity);
				}
			}
		}

		// read the tempo every step, so tempo changes are heard right away
		t += stepMs(style, c->bpm, steps);
		waitUntil(t, c);
	}
}

int parseStyle(const char *arg) {
	char *end;
	long n = strtol(arg, &end, 10);
	if (*arg != '\0' && *end == '\0') {
		return (n >= 0 && n < NUM_STYLES) ? (int)n : -1;
	}
	// match names ignoring case, spaces, dashes and underscores ("cha-cha", "ChaCha", ...)
	for (int s = 0; s < NUM_STYLES; s++) {
		const char *a = arg;
		const char *b = styleNames[s];
		for (;;) {
			while (*a == ' ' || *a == '-' || *a == '_') a++;
			while (*b == ' ') b++;
			if (*a == '\0' || *b == '\0' || tolower((unsigned char)*a) != *b) break;
			a++;
			b++;
		}
		if (*a == '\0' && *b == '\0') {
			return s;
		}
	}
	return -1;
}

void printUsage(void) {
	printf("usage: program [style] [bpm]\n\nstyles:\n");
	for (int s = 0; s < NUM_STYLES; s++) {
		printf("  %2d  %-12s%s", s, styleNames[s], (s % 4 == 3) ? "\n" : "");
	}
	printf("\n");
}

int main(int argc, char* argv[])
{
	Controls c = { 0, 0, 120, 0, 0, 0, "" };
	Style style, ending;
	int loaded;
	HMIDIOUT midi_device;

	if (argc > 1) {
		c.style = parseStyle(argv[1]);
		if (c.style < 0) {
			printf("unknown style \"%s\"\n\n", argv[1]);
			printUsage();
			return 1;
		}
	}
	if (argc > 2) {
		c.bpm = clampBPM(atoi(argv[2]));
	}

	// attempt to open MIDI output device
	if (midiOutOpen(&midi_device, 0, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
		printf("midi device couldn\'t be opened!\n");
		return 1;
	}
	timeBeginPeriod(1);

	printUsage();
	printf("keys: left/right variation, up/down style, -/+ tempo, b break, e ending, q quit\n\n");

	loadStyle(STYLE_ENDING, &ending);
	loaded = -1;
	while (!c.quit) {
		// style and variation changes take effect here, at the start of a bar
		if (c.style != loaded) {
			loadStyle(c.style, &style);
			loaded = c.style;
		}
		if (c.variation >= style.numMains) {
			c.variation = 0;
		}

		if (c.endingQueued) {
			c.state = "ending";
			printStatus(&c);
			playPattern(midi_device, ending.mains[c.variation % ending.numMains], STYLE_ENDING, &c);
			break;
		} else if (c.breakQueued && style.numBreaks > 0) {
			c.breakQueued = 0;
			c.state = "break";
			printStatus(&c);
			playPattern(midi_device, style.breaks[c.variation % style.numBreaks], c.style, &c);
		} else {
			c.breakQueued = 0;
			c.state = "playing";
			printStatus(&c);
			playPattern(midi_device, style.mains[c.variation], c.style, &c);
		}
	}
	printf("\n");

	// close the MIDI output device
	timeEndPeriod(1);
	midiOutReset(midi_device);
	midiOutClose(midi_device);
	return 0;
}
