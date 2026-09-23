// The "Settings…" dialog: every mdview.ini option in one window. Changes are
// previewed live in all open viewers; OK saves them, Cancel restores the old
// ones.
#pragma once

#include <windows.h>

namespace mdv {

// Shows the modal dialog for the viewer window `viewer`, owned by its
// top-level window. If the dialog is already open (another Lister), it is
// brought to the front instead. The viewer may be destroyed while the dialog
// runs: callers must not touch it afterwards.
void ShowSettingsDialog(HWND viewer);

// An open dialog re-reads GlobalSettings() (changed from a context menu).
void RefreshSettingsDialog();

}  // namespace mdv
