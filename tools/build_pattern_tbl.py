'''

opcodes for drum machine:

0xFC -> pattern opcode (32-bit pattern comes after)
0xFD -> end of pattern (for both main and break patterns)
0xFE -> start of break section
0xFF -> end of style partition


drum patterns to 32-bit unsigned int data
with pattern-final marker bit (32th bit)

bits 1-28 (little endian)

for each 2-bit instrument, 00 is silent, 01 is hit, and 10 is flam

   1 - AC                 (accent)
   3 - CY.R               (ride cymbal)
   5 - CY.C               (crash cymbal)
   7 - CH                 (closed hi-hat)
   9 - OH                 (open hi-hat)
  11 - HT                 (high tom)
  13 - MT                 (mid tom)
  15 - SD                 (snare)
  17 - RS                 (rimshot)
  19 - LT                 (low tom)
  21 - CPS                (hand clap)
  23 - TMB                (tambourine)
  25 - CB                 (cowbell)
  27 - BD                 (bass/kick)
  32 - pattern-final

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
