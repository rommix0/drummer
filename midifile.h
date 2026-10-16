// records what the drum machine plays and saves it as a standard MIDI file
// (type 0, one track, drums on channel 10) that can be opened in a DAW.
//
// times are in ticks, not milliseconds, so the notes sit right on the DAW's grid.
#ifndef MIDIFILE
#define MIDIFILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICKS_PER_BEAT 480

typedef struct {
	unsigned long tick;
	unsigned long order;      // keeps events on the same tick in the order they were added
	int len;
	unsigned char data[40];
} MidiEvent;

typedef struct {
	MidiEvent *events;
	int count;
	int capacity;
	unsigned long order;
	int noteOff[128];         // index of the note-off scheduled for each note, -1 if none
} MidiLog;

static void midiLogInit(MidiLog *log) {
	memset(log, 0, sizeof(*log));
	for (int n = 0; n < 128; n++) {
		log->noteOff[n] = -1;
	}
}

static void midiLogFree(MidiLog *log) {
	free(log->events);
	log->events = NULL;
	log->count = log->capacity = 0;
}

static int midiLogAdd(MidiLog *log, unsigned long tick, const unsigned char *data, int len) {
	MidiEvent *e;
	if (log->count == log->capacity) {
		int capacity = log->capacity ? log->capacity * 2 : 1024;
		MidiEvent *grown = realloc(log->events, capacity * sizeof(MidiEvent));
		if (grown == NULL) {
			return -1;
		}
		log->events = grown;
		log->capacity = capacity;
	}
	e = &log->events[log->count];
	e->tick = tick;
	e->order = log->order++;
	e->len = len;
	memcpy(e->data, data, len);
	return log->count++;
}

// a drum hit with a note-off `length` ticks later. if the same drum is hit again
// before that, its earlier note is cut off where the new one starts.
static void midiLogNote(MidiLog *log, unsigned long tick, int note, int velocity, unsigned long length) {
	unsigned char on[3] = { 0x99, (unsigned char)note, (unsigned char)velocity };
	unsigned char off[3] = { 0x89, (unsigned char)note, 0 };
	int pending = log->noteOff[note];

	if (pending >= 0 && log->events[pending].tick > tick) {
		log->events[pending].tick = tick;
	}
	midiLogAdd(log, tick, on, 3);
	log->noteOff[note] = midiLogAdd(log, tick + length, off, 3);
}

static void midiLogTempo(MidiLog *log, unsigned long tick, int bpm) {
	unsigned long usPerBeat = 60000000UL / bpm;
	unsigned char e[6] = { 0xFF, 0x51, 0x03,
		(unsigned char)(usPerBeat >> 16), (unsigned char)(usPerBeat >> 8), (unsigned char)usPerBeat };
	midiLogAdd(log, tick, e, 6);
}

// beats per bar in quarter notes (4 for 4/4, 3 for 3/4)
static void midiLogTimeSig(MidiLog *log, unsigned long tick, int beats) {
	unsigned char e[7] = { 0xFF, 0x58, 0x04, (unsigned char)beats, 2, 24, 8 };
	midiLogAdd(log, tick, e, 7);
}

// a marker shows up as a named flag on the DAW's timeline
static void midiLogMarker(MidiLog *log, unsigned long tick, const char *text) {
	unsigned char e[40] = { 0xFF, 0x06 };
	int len = (int)strlen(text);
	if (len > 36) {
		len = 36;
	}
	e[2] = (unsigned char)len;
	memcpy(e + 3, text, len);
	midiLogAdd(log, tick, e, 3 + len);
}

static int midiEventCompare(const void *a, const void *b) {
	const MidiEvent *x = a, *y = b;
	if (x->tick != y->tick) {
		return x->tick < y->tick ? -1 : 1;
	}
	return x->order < y->order ? -1 : (x->order > y->order);
}

static void midiWriteVarLen(FILE *f, unsigned long value) {
	unsigned char bytes[5];
	int n = 0;
	do {
		bytes[n++] = value & 0x7F;
		value >>= 7;
	} while (value != 0);
	while (n-- > 0) {
		fputc(bytes[n] | (n > 0 ? 0x80 : 0), f);
	}
}

static void midiWrite32(FILE *f, unsigned long value) {
	fputc((value >> 24) & 0xFF, f);
	fputc((value >> 16) & 0xFF, f);
	fputc((value >> 8) & 0xFF, f);
	fputc(value & 0xFF, f);
}

// sorts the events and writes the file. the track ends at endTick (or at the
// last event, if that's later). returns 0 on success.
static int midiLogSave(MidiLog *log, const char *path, unsigned long endTick) {
	static const unsigned char header[] = {
		'M', 'T', 'h', 'd', 0, 0, 0, 6,
		0, 0,                                         // type 0
		0, 1,                                         // one track
		TICKS_PER_BEAT >> 8, TICKS_PER_BEAT & 0xFF
	};
	unsigned long last = 0;
	long lengthAt, end;
	FILE *f = fopen(path, "wb");

	if (f == NULL) {
		return -1;
	}
	qsort(log->events, log->count, sizeof(MidiEvent), midiEventCompare);

	fwrite(header, 1, sizeof(header), f);
	fwrite("MTrk", 1, 4, f);
	lengthAt = ftell(f);
	midiWrite32(f, 0); // track length, filled in below
	for (int i = 0; i < log->count; i++) {
		midiWriteVarLen(f, log->events[i].tick - last);
		fwrite(log->events[i].data, 1, log->events[i].len, f);
		last = log->events[i].tick;
	}
	midiWriteVarLen(f, endTick > last ? endTick - last : 0);
	fwrite("\xFF\x2F\x00", 1, 3, f); // end of track

	end = ftell(f);
	fseek(f, lengthAt, SEEK_SET);
	midiWrite32(f, (unsigned long)(end - lengthAt - 4));
	fseek(f, end, SEEK_SET);
	return fclose(f) == 0 ? 0 : -1;
}

#endif
