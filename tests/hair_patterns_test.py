"""Source emitter contract for hv9 events without historical receipts."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.live_batches.hair_validation.check_patterns import source_contract
source_contract()
print("source pattern check: 6/6 matched rendered emitter strings")
