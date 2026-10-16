/*

Notes for future me (@rommix0), and others wanting to replicate the make process.

do this to build the program:
	-> gcc main.c -o drummer -lwinmm

to regenerate patterns.h after editing tools/drum_patterns.all.txt, see the
steps at the top of tools/build_pattern_tbl.py

usage:
	-> drummer [options] [style] [bpm]

	style is a number (0-22) or a name ("rock", "cha cha", "r&b", ...)
	bpm defaults to 120 and is clamped to 60-300

options:
	-l          list the MIDI output devices
	-d N        play on MIDI output device N (default 0)
	-o FILE     also record what's played to a MIDI file, saved on exit
	-s N        swing in percent, 50 (straight) to 75 (default 50)

keys while playing:
	left / right    previous / next variation (switches at the end of the bar)
	up / down       next / previous style     (switches at the end of the bar)
	- / +           tempo down / up
	[ / ]           swing down / up
	b or space      play a break on the next bar
	e               play an ending, then stop
	q, esc, ctrl+c  quit right away

*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drums.h"
#include "patterns.h"
#include "midifile.h"

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

#define SWING_MIN  50
#define SWING_MAX  75
#define SWING_STEP 5

// how long each hit lasts in a recording (a 32nd note)
#define NOTE_TICKS (TICKS_PER_BEAT / 8)

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
	int swing;
	int breakQueued;
	int endingQueued;
	int quit;
	int recording;
	const char *state; // shown on the status line
} Controls;

// where the music is, both in real time and in recording ticks
typedef struct {
	HMIDIOUT out;
	Controls *c;
	MidiLog *log;            // NULL when not recording
	double barStartMs;       // bars are scheduled back to back, so the tempo doesn't drift
	unsigned long barTick;
	int loggedBpm;
	int loggedBeats;
	char lastMarker[40];
} Player;

// set by ctrl+c or closing the console window
static volatile LONG stopRequested = 0;

// where each style partition starts in the pattern table, found by findStyles()
static int styleOffsets[NUM_STYLES + 1];

// walk the whole pattern table once. returns 0 if it holds exactly one
// partition for every entry in styleNames.
int findStyles(void) {
	int size = (int)sizeof(patterns);
	int pos = 0;

	for (int s = 0; s <= NUM_STYLES; s++) {
		if (pos >= size) {
			return -1;
		}
		styleOffsets[s] = pos;
		while (pos < size && patterns[pos] != OP_END_STYLE) {
			pos += (patterns[pos] == OP_STEP) ? 5 : 1;
		}
		pos++;
	}
	return (pos == size) ? 0 : -1;
}

int getOffset(int select) {
	if (select < 0 || select > STYLE_ENDING) {
		select = STYLE_ENDING;
	}
	return styleOffsets[select];
}

int clampBPM(int bpm) {
	if (bpm < BPM_MIN) {
		return BPM_MIN;
	} else if (bpm > BPM_MAX) {
		return BPM_MAX;
	}
	return bpm;
}

int clampSwing(int swing) {
	if (swing < SWING_MIN) {
		return SWING_MIN;
	} else if (swing > SWING_MAX) {
		return SWING_MAX;
	}
	return swing;
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
int beatsPerBar(int style) {
	return (style == STYLE_WALTZ) ? 3 : 4;
}

// where a step starts inside its bar, in ticks. swing pushes every second sixteenth
// late: 50% is straight, 66% is close to a triplet shuffle, 75% is a dotted feel.
// triplet patterns already swing, so they're left alone.
int stepTick(int step, int steps, int barTicks, int swing) {
	int len = barTicks / steps;
	if (len == TICKS_PER_BEAT / 4 && (step & 1)) {
		return (step - 1) * len + (2 * len * swing + 50) / 100;
	}
	return step * len;
}

double msPerTick(int bpm) {
	return 60000.0 / ((double)bpm * TICKS_PER_BEAT);
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
	printf("\r%-10s  variation %2d/%-2d  %3d bpm  swing %2d%%  %-8s%s",
		styleNames[c->style], c->variation + 1, style.numMains, c->bpm, c->swing,
		c->state, c->recording ? "  rec" : "");
	fflush(stdout);
}

void handleKeys(Controls *c) {
	if (stopRequested) {
		c->quit = 1;
	}
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
			case ']':
				c->swing = clampSwing(c->swing + SWING_STEP);
				break;
			case '[':
				c->swing = clampSwing(c->swing - SWING_STEP);
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

void hitDrum(Player *p, unsigned long tick, int note, int velocity) {
	sendMIDIEvent(p->out, DRUM_NOTE_OFF, note, 0);
	sendMIDIEvent(p->out, DRUM_NOTE_ON, note, velocity);
	if (p->log != NULL) {
		midiLogNote(p->log, tick, note, velocity, NOTE_TICKS);
	}
}

// hits go out at the start of the step, flams get their second stroke FLAM_DELAY later
void playStep(Player *p, DWORD step, unsigned long tick, double t) {
	int velocity = ((step >> ACCENT_SHIFT) & 3) ? VELO_ACC : VELO_DEF;
	int flams = 0;

	for (int n = 0; n < NUM_SOUNDS; n++) {
		int hit = (step >> (2 + 2 * n)) & 3;
		if (hit != 0) {
			hitDrum(p, tick, slotNotes[n], velocity);
		}
		if (hit & 2) {
			flams = 1;
		}
	}

	if (flams) {
		unsigned long flamTick = tick + (unsigned long)(FLAM_DELAY / msPerTick(p->c->bpm) + 0.5);
		waitUntil(t + FLAM_DELAY, p->c);
		if (p->c->quit) {
			return;
		}
		for (int n = 0; n < NUM_SOUNDS; n++) {
			if ((step >> (2 + 2 * n)) & 2) {
				hitDrum(p, flamTick, slotNotes[n], velocity);
			}
		}
	}
}

// play one bar of a pattern. marker names the bar in the recording.
void playPattern(Player *p, const unsigned char *pattern, int style, const char *marker) {
	Controls *c = p->c;
	int steps = patternLength(pattern);
	int beats = beatsPerBar(style);
	int barTicks = beats * TICKS_PER_BEAT;
	int tick = 0;
	double t = p->barStartMs;

	if (p->log != NULL) {
		if (beats != p->loggedBeats) {
			midiLogTimeSig(p->log, p->barTick, beats);
			p->loggedBeats = beats;
		}
		if (strcmp(marker, p->lastMarker) != 0) {
			midiLogMarker(p->log, p->barTick, marker);
			snprintf(p->lastMarker, sizeof(p->lastMarker), "%s", marker);
		}
	}

	for (int i = 0; i < steps; i++) {
		// read tempo and swing every step, so changes are heard right away
		int next = stepTick(i, steps, barTicks, c->swing);
		t += (next - tick) * msPerTick(c->bpm);
		tick = next;
		waitUntil(t, c);
		if (c->quit) {
			p->barTick += barTicks; // so a recording still ends on a bar line
			return;
		}
		if (p->log != NULL && c->bpm != p->loggedBpm) {
			midiLogTempo(p->log, p->barTick + tick, c->bpm);
			p->loggedBpm = c->bpm;
		}
		playStep(p, patternStep(pattern, i), p->barTick + tick, t);
	}

	// let the last step ring until the bar is over
	t += (barTicks - tick) * msPerTick(c->bpm);
	waitUntil(t, c);
	p->barStartMs = t;
	p->barTick += barTicks;
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
	printf("usage: drummer [options] [style] [bpm]\n\n");
	printf("options:\n");
	printf("  -l          list the MIDI output devices\n");
	printf("  -d N        play on MIDI output device N (default 0)\n");
	printf("  -o FILE     also record what's played to a MIDI file\n");
	printf("  -s N        swing in percent, 50 (straight) to 75 (default 50)\n\n");
	printf("styles:\n");
	for (int s = 0; s < NUM_STYLES; s++) {
		printf("  %2d  %-12s%s", s, styleNames[s], (s % 4 == 3) ? "\n" : "");
	}
	printf("\n");
}

void listDevices(void) {
	UINT count = midiOutGetNumDevs();
	MIDIOUTCAPSA caps;

	if (count == 0) {
		printf("no MIDI output devices found\n");
		return;
	}
	printf("MIDI output devices:\n");
	for (UINT d = 0; d < count; d++) {
		if (midiOutGetDevCapsA(d, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			printf("  %u  %s\n", d, caps.szPname);
		}
	}
}

BOOL WINAPI onConsoleEvent(DWORD event) {
	stopRequested = 1;
	if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
		return TRUE;
	}
	// the window is closing: hold this thread so main can still save the recording.
	// the process ends once main returns (or when windows runs out of patience).
	Sleep(INFINITE);
	return TRUE;
}

int main(int argc, char* argv[])
{
	Controls c = { .bpm = 120, .swing = SWING_MIN, .state = "" };
	Player p = { 0 };
	MidiLog log;
	Style style, ending;
	const char *recordPath = NULL;
	char marker[40];
	int device = 0;
	int positional = 0;
	int loaded;

	if (findStyles() != 0) {
		printf("patterns.h doesn't have one partition per style, was it regenerated?\n");
		return 1;
	}

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "-l") == 0) {
			listDevices();
			return 0;
		} else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			printUsage();
			return 0;
		} else if ((strcmp(arg, "-d") == 0 || strcmp(arg, "-o") == 0 || strcmp(arg, "-s") == 0) && i + 1 < argc) {
			const char *value = argv[++i];
			if (arg[1] == 'd') {
				device = atoi(value);
			} else if (arg[1] == 'o') {
				recordPath = value;
			} else {
				c.swing = clampSwing(atoi(value));
			}
		} else if (arg[0] == '-') {
			printf("unknown option \"%s\"\n\n", arg);
			printUsage();
			return 1;
		} else if (positional == 0) {
			c.style = parseStyle(arg);
			if (c.style < 0) {
				printf("unknown style \"%s\"\n\n", arg);
				printUsage();
				return 1;
			}
			positional++;
		} else if (positional == 1) {
			c.bpm = clampBPM(atoi(arg));
			positional++;
		} else {
			printf("too many arguments\n\n");
			printUsage();
			return 1;
		}
	}

	// attempt to open MIDI output device
	if (midiOutOpen(&p.out, device, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
		printf("midi device %d couldn\'t be opened!\n\n", device);
		listDevices();
		return 1;
	}
	timeBeginPeriod(1);
	SetConsoleCtrlHandler(onConsoleEvent, TRUE);
	// keeps the timing steady when the PC is busy with other things
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	if (recordPath != NULL) {
		midiLogInit(&log);
		p.log = &log;
		c.recording = 1;
	}
	p.c = &c;

	printUsage();
	printf("keys: left/right variation, up/down style, -/+ tempo, [/] swing, b break, e ending, q quit\n\n");

	loadStyle(STYLE_ENDING, &ending);
	loaded = -1;
	p.barStartMs = nowMs();
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
			int e = c.variation % ending.numMains;
			c.state = "ending";
			printStatus(&c);
			snprintf(marker, sizeof(marker), "ending %d", e + 1);
			playPattern(&p, ending.mains[e], STYLE_ENDING, marker);
			break;
		} else if (c.breakQueued && style.numBreaks > 0) {
			int b = c.variation % style.numBreaks;
			c.breakQueued = 0;
			c.state = "break";
			printStatus(&c);
			snprintf(marker, sizeof(marker), "%s break %d", styleNames[c.style], b + 1);
			playPattern(&p, style.breaks[b], c.style, marker);
		} else {
			c.breakQueued = 0;
			c.state = "playing";
			printStatus(&c);
			snprintf(marker, sizeof(marker), "%s %d", styleNames[c.style], c.variation + 1);
			playPattern(&p, style.mains[c.variation], c.style, marker);
		}
	}
	printf("\n");

	if (p.log != NULL) {
		// end the track on a bar line, so it loops cleanly in a DAW
		if (midiLogSave(p.log, recordPath, p.barTick) == 0) {
			printf("recording saved to %s\n", recordPath);
		} else {
			printf("couldn't save the recording to %s\n", recordPath);
		}
		midiLogFree(p.log);
	}

	// close the MIDI output device
	timeEndPeriod(1);
	midiOutReset(p.out);
	midiOutClose(p.out);
	return 0;
}
