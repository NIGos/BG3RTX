$root = Split-Path $PSScriptRoot
Set-Location $root
function Keep-Log($tag) { Copy-Item bg3rtx.log "captures\log_$tag.txt" -ErrorAction SilentlyContinue }
.\captures\run_phases.ps1 -Phases captures\ls_scene.json -Tag grym -Save "Grymforge - 32h 59m"; Keep-Log grym
.\captures\run_phases.ps1 -Phases captures\ls_scene.json -Tag wqh -Save "Water Queens House"; Keep-Log wqh
.\captures\run_phases.ps1 -Phases captures\ls_val.json -Tag val -Validation; Keep-Log val
Copy-Item captures\validation.log captures\validation_ls.log -ErrorAction SilentlyContinue
& .\scripts\auto-run.ps1 -Background -NoBuild -Move -Traces 2 *> captures\run_move.txt; Keep-Log move
"all done"
