# Attach PINCE to the running DELTARUNE chapter first.
# Paste this entire file into Tools -> Libpince Engine and run it.
# Then use the [DR] stat rows and the [DR] No damage toolbar checkbox.

import importlib
import sys

# Set this to where you cloned the repo.
cheat_dir = "/path/to/deltarune-modloader/RE/cheatengine"
if cheat_dir not in sys.path:
    sys.path.insert(0, cheat_dir)

import deltarune_stats_pince as dr

dr.stop()  # Stop the previous worker and remove its controls before reloading.
dr = importlib.reload(dr)
dr.start(push_to_pince=True)
