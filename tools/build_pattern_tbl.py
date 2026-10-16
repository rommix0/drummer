'''

opcodes for drum machine:

0xFC -> pattern opcode (32-bit pattern comes after)
0xFD -> end of pattern (for both main and break patterns)
0xFE -> start of break section
0xFF -> end of style partition


each step is one 32-bit word (little endian) made of 2-bit instrument
slots. for each slot, 00 is silent, 01 is hit, and 10 is flam

  bits  0-1  -               (unused, always 0)
  bits  2-3  BD              (bass/kick)
  bits  4-5  CB              (cowbell)
  bits  6-7  TMB             (tambourine)
  bits  8-9  CPS             (hand clap)
  bits 10-11 LT              (low tom)
  bits 12-13 RS              (rimshot)
  bits 14-15 SD              (snare)
  bits 16-17 MT              (mid tom)
  bits 18-19 HT              (high tom)
  bits 20-21 OH              (open hi-hat)
  bits 22-23 CH              (closed hi-hat)
  bits 24-25 CY.C            (crash cymbal)
  bits 26-27 CY.R            (ride cymbal)
  bits 28-29 AC              (accent)

the pattern-final bit (bit 31) from the json is dropped here,
0xFD marks the end of a pattern instead.

to regenerate everything after editing drum_patterns.all.txt (run from tools/):
    python drum_patterns_to_bytes.py    -> drum_patterns.all.json
    python build_pattern_tbl.py         -> ../patterns.h (+ patterns.bin, patterns.jmp.tbl)
    python construct_trainer.py         -> drum_patterns.fst.trainer

'''

from struct import pack
from io import BytesIO
import json

bin_img = BytesIO()
with open('drum_patterns.all.json', 'r') as f:
    model = json.load(f)

jmp_tbl = {}
for genre in model.keys():
    jmp_tbl[genre] = bin_img.tell()
    for section in ('main', 'break'):
        patterns = model[genre].get(section)
        if patterns is None:
            if section == 'main': break
            continue
        if section == 'break': bin_img.write(b'\xFE')
        for p_id, pattern in patterns.items():
            for note in pattern:
                bin_img.write(b'\xFC')
                bin_img.write(pack('<I', note & 0x7FFFFFFF))
            bin_img.write(b'\xFD')
    bin_img.write(b'\xFF')

with open('patterns.bin', 'wb') as f:
    bin_img.seek(0)
    f.write(bin_img.read())
with open('patterns.jmp.tbl', 'w', newline='\n') as f:
    json.dump(jmp_tbl, f, indent=4)

# C header for main.c, 16 bytes per line (CRLF, like the original file)
data = bin_img.getvalue()
rows = [','.join('0x%02x' % b for b in data[i:i+16]) for i in range(0, len(data), 16)]
header = '/*\n\n' + __doc__.strip('\n').split('\n\nto regenerate')[0] + '\n\n*/\n\n'
header += 'static unsigned char patterns[] = {\n' + ',\n'.join(rows) + '\n};'
with open('../patterns.h', 'w', newline='\r\n') as f:
    f.write(header)
