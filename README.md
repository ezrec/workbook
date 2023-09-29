= Workbook

Copy to SYS:System/Workbook

- Unreleases
  - Add icon move via drag/drop.
  - Fix issue introduced in v1.11 where windows could only be resized to be smaller.
- v1.11
  - Add Leave-Out & Put-Away in Icon menu.
  - Add Backdrop menu item
  - Add Information.. window.
- v1.10
  - Add multi-selection marquee
  - Add list view (Name, Size, Date, Type) modes.
  - Set Volume window title to include disk usage information.
  - Add 'New Drawer' window menu feature.
  - Add Drag/Drop manager.
- v1.9
  - Several Copy and Delete item fixes.
  - Support StartNotify() for filesystems that have it.
  - Faster window opening.
- v1.8
  - Add Copy menu item functionality.
  - Add Delete menu item functionality.
- v1.7
  - Reduce rendering done when selecting/deselecting icons.
  - Fix issue where only one drawer icon opened when multiple are selected.
  - Ensure all selected icons are affected by Icon menu commands, not just ones in the current window.
  - Ensure only icon images, not labels, are in the icon mouse selection area.
- v1.6
  - Add 'Snapshot' functionality
  - Disable broken 'Information...' menu item for now.
  - Add hard-coded custom items to the menu (Updater and Amistore)
- v1.5
  - Rename process/task to 'Workbench' for AmigaOS tool (ie WBRun) compatibility.
  - Only refresh title bar when AvailMem actually changes
  - Add 'Clean Up' functionality
  - Render icons with 3D borders (workaround for AROS icon.library bugs)
- v1.4
  - Add select all/none menu items.
  - Add shift-select and mutual exclusion selection modes.
  - Enable 'Format...' functionality for disk devices.
  - Fix issue with overlog Execute... label.

- v1.3
  - Add 'Rename' functionality.
  - Fix issues with 'Show All Files' functionality.

- v1.2
  - Added 'branding' support via `WB_xxx` macros in Makefile
  - VBCC compilation support

- v1.1
  - Added 'About' pop-up
  - Added 'Execute...' pop-up and action.
-
