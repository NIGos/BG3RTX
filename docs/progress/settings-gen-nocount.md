## Generated settings block without the key count (branch settings-gen-nocount, kept, 2026-11-02)
- scripts/gen_settings_defaults.py no longer writes "N keys of bg3rtx.ini": that one line changed with every added
  key and conflicted on every branch adding one (spec-cascade-s1, omm-option-b). Tables unchanged; settings_test and
  100 CTests pass. Branches that add keys now conflict only when they touch the same ini lines.
