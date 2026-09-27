// The "Settings…" dialog: every blitzmd.ini option in one window. Changes are
// previewed live in all open viewers; OK saves them, Cancel restores the old
// ones.
#pragma once

#include <windows.h>

#include <functional>

#include "core/settings.h"

namespace mdv {

// Shows the modal dialog for the viewer window `viewer`, owned by its
// top-level window. If the dialog is already open (another Lister), it is
// brought to the front instead. The viewer may be destroyed while the dialog
// runs: callers must not touch it afterwards.
void ShowSettingsDialog(HWND viewer);

// A setting changed outside the dialog (a viewer's menu, Ctrl+wheel):
// `edit` has changed GlobalSettings() already; this saves the change. With
// the dialog open, GlobalSettings() holds its unconfirmed changes too: then
// `edit` is applied to the settings the dialog started from (what its Cancel
// returns to) and those are saved, and the dialog shows the new value.
void SaveSettingChange(const std::function<void(Settings&)>& edit);

// When the DLL is unloaded: the dialog's activation context (made from the
// DLL's manifest) is released.
void ReleaseDialogContext();

}  // namespace mdv
