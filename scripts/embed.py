Import("env")
from pathlib import Path
import gzip
root = Path(env['PROJECT_DIR'])
data = gzip.compress((root/'web/dashboard.html').read_bytes(), mtime=0)
(root/'include/web.h').write_text('#pragma once\nconst unsigned char PAGE[] PROGMEM = {' + ','.join(map(str,data)) + '};\n')
