## Drawit
A very minimal and lightweight vector-based whiteboarding program for illustrating concepts live.

### Build
Run `make BUILD=RELEASE`. Optionally, run `make install`

Dependencies include POSIX and OpenGL. On Linux, X11 and libcursor are also required, check the Makefile.

### How to Use
Note that the editor is somewhat vim-inspired, with the mouse on the dominant right hand. Future plans do include mouse-centric controls.
- **Zoom & Pan:** Scroll to zoom, middle mouse button to pan (canvas is infinite)
- **Draw:** Left click to stroke with primary color, right click for secondary color (strokes are somewhat smoothed)
- **Colors:** Keys `1-4` switches the primary colors. Hold `alt` to set as secondary instead
- **Select:** Press `S` to enter SELECT mode. Hover to select the object under the cursor, click and drag for multi-select, `ESC` to exit mode
- **Move:** Press `W` to grab the object under the cursor, press `W` again to drop it. `ESC` cancels. In SELECT, `W` moves the selection
- **Group:** Press `A` to begin grouping, press `A` again to group objects created since. In SELECT, `A` groups the selection and `Shift+A` ungroups.
- **Delete:** Press `X` to delete the object under the cursor or selection.
- **Copy & Paste:** `Ctrl+C` copies objects under the cursor or selection, `Ctrl+V` pastes at the cursor
- **Text:** Press `i` to start typing at the cursor, `ESC` exits
- **Undo/Redo:** `Ctrl+Z` to undo, `Ctrl+R` to redo
- **Commands:** `:` to enter COMMAND, `ESC` to cancel, `ENTER` to execute. So far, commands include:
    - `:quit` to exit.
    - `:light` and `:dark` to switch themes.
    - `:meta` to show metadata.
    - `:selout` to toggle selection outlines.
    - `:verbosity N` to set the status line verbosity (0 or 1)
