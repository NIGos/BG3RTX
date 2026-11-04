#pragma once
#include "launcher.h"
namespace launcher::web {
// -1 asks the caller to use the native UI (runtime/payload unavailable).
int run(HINSTANCE, const std::wstring& game, bool report, bool no_game,
        const std::wstring& screenshot = {}, int width = 1180,
        const std::wstring& view = {});
int native_report(HWND owner);
int action_dialog(HWND owner, const std::wstring& title, const std::wstring& text,
                  const std::vector<std::pair<int, std::wstring>>& actions);
}
