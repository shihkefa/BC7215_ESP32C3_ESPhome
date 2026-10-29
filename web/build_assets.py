"""Bundle the vendored QR encoder and dashboard without external build dependencies."""
from pathlib import Path
root=Path(__file__).resolve().parent
(root/'dashboard.bundle.js').write_text((root/'qrcodegen.js').read_text(encoding='utf-8')+'\n'+(root/'dashboard.js').read_text(encoding='utf-8'),encoding='utf-8')
print('Updated dashboard.bundle.js')
