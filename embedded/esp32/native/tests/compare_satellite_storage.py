"""Compare the same complete synthetic satellite projections in both layouts."""
import subprocess
import sys

outputs = [subprocess.run([executable], check=True, capture_output=True, timeout=15).stdout
           for executable in sys.argv[1:]]
assert len(outputs) == 2 and outputs[0], 'missing satellite corpus output'
assert outputs[0] == outputs[1], 'inline/external satellite semantics differ'
print('Inline/external satellite corpus projections identical')
