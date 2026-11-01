# Offline check of the replay ambient batch (camp + inn packs): no game, no real queue (see ..\..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\replay_capture\ambient\dryrun.ps1 [-Out <scratch dir>]
# Budget: two launches of 45 s plans; each pack at most replay_pack_cap_mb 3500 MB (+ 10 MB log).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\..\lean_dryrun.ps1') -Batch 'replay_capture/ambient' -Independent -Out $Out -MaxMinutes 7 -MaxMB 7100 -TraceMB 0 -LogMB 3510
exit $LASTEXITCODE
