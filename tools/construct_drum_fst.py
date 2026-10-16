# construct finite state machine mappings that can
# act like an actual drum machine once put together and running

from sys import exit
import json

ssyms = set()       # state symbols
isyms = set()       # input symbols
osyms = set()       # output symbols
fsyms = set()       # final symbols

eps       = '<eps>'

def make_final(state):
    # make final state
    fsyms.add(state)
def make_arc(state_from, state_to, isym, osym):
    # add states and symbols to sets
    ssyms.add(state_from)
    ssyms.add(state_to)
    isyms.add(isym)
    osyms.add(osym)

    # print out state transition info
    print(f'{state_from} {state_to} {isym} {osym}')
def write_symbol_file(fname, sset):
    # prune out epsilon first (if present)
    sset.discard(eps)
    # write out symbol map
    with open(fname, 'w', newline='\n') as f:
        f.write(f'{eps} 0\n')
        kk = list(sorted(sset))
        for i, sym in enumerate(kk, 1):
            f.write(f'{sym} {i}\n')

# finish code down here
