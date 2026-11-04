## Windows release installer (branch inno-setup, blocked qualification, 2026-11-04)

- Added `build_setup.ps1`: VERSION.txt version, external payload.sha256.json with 229
  hashes, combined GPL/exception page, pinned Inno 6.7.3. Outputs only under .scratch/.
- Setup keeps the fixed AppId and transactional lifecycle; per-user, original icon,
  Start menu, unchecked desktop option, finish launch, Vulkan + DLSS welcome text.
- Upgrade keeps settings. Uninstall defaults to keeping settings, supports explicit
  /REMOVESETTINGS=1, removes shader cache, and delegates shortcuts/Apps cleanup to Inno.
- Added actual Setup RC stage: explicit throwaway /DIR, shortcuts disabled, isolated
  LOCALAPPDATA/TEMP, 229 install/update hashes, cache/settings keep/remove and Apps checks.
  Refuses existing Apps registration; retains recovery artifacts if setup cleanup fails.
- Fixed fresh-worktree packaging: use the lean build's manifest and production Streamline
  copies. Payload remains 229 files, 0 missing inputs and 0 manifest differences.
- Full verify_rc rerun 18:42:58-18:45:12: 11/12 rows PASS, Setup FAIL before compilation:
  ISCC.exe execution denied by Windows (Access is denied). No setup EXE was produced or
  executed; no Apps entry was created. ZIP 72.61 MiB; 226 source hashes, 12 DLLs,
  9 production Streamline DLLs and 15 notices PASS; install/update 229 hashes each;
  uninstall restored 5/5 originals; fake game unchanged; throwaway TEMP tree removed.
- Tests: lifecycle 15/15 (75.1 s), release_install 7/7 (15.0 s), setup metadata 4/4
  (1.9 s; also CTest PASS), package licences 8/8 (20.1 s): 34/34 PASS.
- No shader requests added (delta 0); no rendering/ABI changes or game launch.
- Commit blocked: git add cannot create .git/worktrees/inno-setup/index.lock (Permission
  denied); required git merge main similarly denied at ORIG_HEAD.lock. Changes are unstaged.
- Remaining: permit compiler execution and Git metadata writes, rerun verify_rc, commit
  with the requested Codex trailer. Signing/fresh-PC/interactive wizard acceptance remain open.
- User check after qualification: run Setup in a disposable user account, inspect icons,
  licence page and shortcut options; update retains settings; Apps uninstall defaults to keep.
