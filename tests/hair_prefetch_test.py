"""Hair prefetch requests originate in successful shader_get calls, persisted
by layer/shaders.cpp in runtime requests.txt (not a hardcoded prefetch list).
Check the producer strings, registry and stale-manifest removal path.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'layer/shaders.cpp').read_text()
assert 'std::vector<Request> reqs = manifest_read();' in source
assert 'shader_get(r.file.c_str(), r.entry.c_str(), r.profile.c_str(), &blob)' in source
assert 'if (note && ok) manifest_note(e);' in source
assert 'if (!e.requested) manifest_drop(e, result);' in source
assert 'BuildResult::SourceRemoved' in source and 'BuildResult::EntryRemoved' in source
# Include local variables used by shader_get (G-buffer / velocity ternary),
# not only literals appearing directly in the call.
requests = set()
for path in (ROOT / 'layer').glob('*.cpp'):
    requests.update(re.findall(r'"(rt/hair_primary_[^"\s]+\.hlsl(?:\?[^"\s]*)?)"', path.read_text()))
manifest = {line.strip() for line in (ROOT / 'shaders/variants.txt').read_text().splitlines()
            if line.strip() and not line.startswith('#')}
assert requests, 'no hair producers found'
for request in requests:
    assert request in manifest, f'prefetch producer missing from variants.txt: {request}'
    assert (ROOT / 'shaders' / request.split('?')[0]).is_file(), request
print(f'hair prefetch: {len(requests)} producer requests in variants.txt, sources exist; rolling manifest stale-drop guarded')
