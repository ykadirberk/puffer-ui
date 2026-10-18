# Manual checks

What the automated suites cannot prove, because the operating system is in
the loop. Run these on a real desktop before a release (and after changing
the code named in each item). Every item says what to do and what must happen.

## Windows

### Window chrome and multiple windows (r98, r99)
- `pui_ex_windows`: the "Close second" / "Open second" button on the primary's
  titlebar closes and reopens the second window on the first click, even while
  another app is in front. Dragging the empty part of either titlebar moves the
  window; double-clicking it maximizes; the edges resize.
- `pui_demo`: window 2 opens to the right of window 1; its close glyph closes
  it (its docked panels appear in window 1); window 1's titlebar button brings
  it back. With vsync on, the HUD shows the display's refresh rate with both
  windows open.

### Pictures, drops, dialogs, window state (r100)
- `pui_ex_files`: drag a PNG and a JPEG from Explorer onto the window - the
  overlay "Drop to open" shows while dragging, both pictures load on release.
- Press "Open...": the system dialog opens over the window (modal to it);
  Cancel leaves the list unchanged; choosing two files adds both.
- Move and resize the window, close it, start it again: it reopens there.
  Unplug or disable the monitor it was on: it reopens on the primary display.

### Popups beyond the window (r101)
- `pui_ex_combo --size 660x260`: open the combo near the bottom edge - the
  dropdown extends below the window (an OS popup window, no taskbar entry, the
  main window keeps focus). Hover and click an item: it is picked.
- With the dropdown open, click another application: the dropdown closes.
- Drag the window to a second monitor with a different scale and repeat.

### Mixed DPI (r97, r101)
- Two monitors at different scales (e.g. 100% and 150%): drag `pui_tour`
  between them; text stays sharp, layout stays logical, the titlebar buttons
  and resize edges keep working, popups open at the right size on each.

### Screen readers (r106)
- Start Narrator (Win+Ctrl+Enter), run `pui_ex_a11y`, press Tab: Narrator
  announces each widget with its role and name ("Press me, button", "Wrap
  lines, check box, checked", "Volume, slider, 0.6", the text field's value).
- Narrator's scan mode (Caps Lock+Space) reaches every widget; activating the
  button increments "presses"; activating the checkbox toggles it.
- Inspect.exe (Windows SDK) shows the window's children with the framework id
  "PufferUI" and bounding rectangles over the widgets.

## Linux / macOS
- The CI builds and runs the headless and offscreen suites. By hand: on X11,
  Wayland and macOS the custom titlebar drags the window where the platform has
  a native hit-test; where it does not, the window shows its OS frame and the
  custom bar stands down (`window::os_frame`).
