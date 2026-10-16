from statistics import stdev
import sys

weights = []
for line in sys.stdin:
    line = line.rstrip().split('\t')[-1]
    weights.append(float(line))

sd = stdev(weights)
#print(1.96 * sd)  # 95% coverage
print(3.0 * sd)
