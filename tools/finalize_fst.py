import sys

final_states = set()
transitions = []

for line in sys.stdin:
    line = line.rstrip().split('\t')
    if len(line) > 4:
        line = line[:-1]
    if len(line) < 3:
        line = line[0]
    if not isinstance(line, list):
        final_states.add(int(line))
    else:
        transitions.append('\t'.join(line))

final_states = list(sorted(final_states))
for entry in transitions:
    print(entry)
for entry in final_states:
    print(entry)
