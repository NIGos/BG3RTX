# IP grass instanced rows at camp (live_batch m4-ip-grass), NOT authorized

One lean job, `hig1-ip-grass-camp-fg2` (Campsite - 27h, 144 s, FG 2, foreground): Ultra (+ Bounce, Hidden hits) with
startup `rt_hit_material_ip_grass=1` (the 105e47e4 frame patch and the RT_HIT_IP_GRASS variants), then its rows OFF / ON /
OFF live, exposure held in every A/B window. Why: m4p1 031746 has camp's row reason at 60% of hit lookups; the IP
grass pair (105e47e4/35a18400, depth 05d39f75/73849471) is most of it (docs/TRACK_D_REFLECTIONS.md 14 "Fourth family").
Gates (`expect_every`): IP grass rows captured and mapped > 0 in diag-on, none in diag-off; its clip check checked > 0
and rejected < 0.04%; the other families keep < 0.04%; capacity, cascade full and scene wait 0 in the cost windows.
Deciding: the `hit material` qualified share (predicted +30 points, range 10-40) and shot-on vs shot-off-a/b (null
predicted, band about 3.5%); cost from the cost windows. Undo: `rt_hit_material_ip_grass=0` (live; startup to drop
the patch). Offline check: `powershell -File tools\live_batches\m4_ip_grass\dryrun.ps1`.
