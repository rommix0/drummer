# drum patterns to 32-bit unsigned int data
# with pattern-final marker bit (bit 31)
#
# each step is one 32-bit word (little endian in patterns.h) made of
# 2-bit instrument slots. for each slot, 00 is silent, 01 is hit, and 10 is flam
#
#   bits  0-1  -               (unused, always 0)
#   bits  2-3  BD              (bass/kick)
#   bits  4-5  CB              (cowbell)
#   bits  6-7  TMB             (tambourine)
#   bits  8-9  CPS             (hand clap)
#   bits 10-11 LT              (low tom)
#   bits 12-13 RS              (rimshot)
#   bits 14-15 SD              (snare)
#   bits 16-17 MT              (mid tom)
#   bits 18-19 HT              (high tom)
#   bits 20-21 OH              (open hi-hat)
#   bits 22-23 CH              (closed hi-hat)
#   bits 24-25 CY.C            (crash cymbal)
#   bits 26-27 CY.R            (ride cymbal)
#   bits 28-29 AC              (accent)
#   bit  31    pattern-final   (last step of a pattern, json only;
#                               patterns.h uses 0xFD instead)
#
# rows are read in drum_patterns.all.txt order (AC first, BD last), so each row
# lands 2 bits below the one before it

import re, json
pattern_grab = re.compile(r'([A-Z &]+)(\d*)\:\n\n(.*?)\n\n', re.DOTALL)
break_detect = re.compile(r' *BREAKS? *$')

with open('drum_patterns.all.txt', 'r') as f:
    # make sure the last pattern is also followed by a blank line,
    # otherwise pattern_grab misses it (this dropped ENDING 3)
    data = f.read().rstrip('\n') + '\n\n'

model = {}
for ent in pattern_grab.finditer(data):
    p_name, p_num, p = ent.groups()
    is_break = False
    break_find = break_detect.search(p_name)
    if break_find:
        is_break = True
        p_name = p_name[:break_find.start()]
    if p_num == '': p_num = '1'
    p_num = int(p_num)
    tmp = {}; seq_len = 0
    for subent in p.split('\n'):
        subent = subent.split('\t')
        perc_name = subent[0]
        perc_seq  = [x for x in subent[1:] if x]
        if seq_len < len(perc_seq):
            seq_len = len(perc_seq)
        tmp[perc_name] = perc_seq
    
    byte_seq = []
    for i in range(seq_len):
        byte_out = 0
        for j in tmp.keys():
            token = tmp[j][i]
            if token == '#':
                # hit
                byte_out |= 0x1
            elif token == 'F':
                # flam
                byte_out |= 0x2
            elif token not in ('#', 'F', '-'):
                raise Exception(f'whats is {token} doing here?')
            byte_out <<= 2
        if i >= seq_len - 1:
            # pattern final bit at MSB
            byte_out |= 0x80000000
        byte_seq.append(byte_out)
    
    p_name = p_name.lower().strip()
    if p_name not in model:
        model[p_name] = {}
    if is_break:
        if 'break' not in model[p_name]:
            model[p_name]['break'] = {}
        if p_num not in model[p_name]['break']:
            model[p_name]['break'][p_num] = byte_seq
    else:
        if 'main' not in model[p_name]:
            model[p_name]['main'] = {}
        if p_num not in model[p_name]['main']:
            model[p_name]['main'][p_num] = byte_seq

with open('drum_patterns.all.json', 'w', newline='\n') as f:
    json.dump(model, f, indent=4)
