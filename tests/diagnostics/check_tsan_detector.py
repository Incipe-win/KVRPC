"""The intentionally racy child must be rejected, not suppressed or ignored."""
import os
import subprocess
import sys

env = os.environ.copy()
# Raw PCs avoid external symbolizer/network dependencies in this deliberate error.
# No ignore/suppression flags are enabled; the exit code and diagnostic are checked.
env['TSAN_OPTIONS'] = 'symbolize=0:halt_on_error=1:exitcode=66'
result = subprocess.run([sys.argv[1]], env=env, capture_output=True, text=True, timeout=20)
if result.returncode != 66 or 'WARNING: ThreadSanitizer: data race' not in result.stderr:
    print(result.stdout, result.stderr)
    raise SystemExit('TSan did not detect the intentional race')
print('TSan detected the intentional race')
