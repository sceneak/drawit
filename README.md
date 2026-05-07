## Drawit
A very minimal and lightweight vector-based whiteboarding program for illustrating concepts live.

## Build
Run `make BUILD=RELEASE`

Dependencies include POSIX and OpenGL. On Linux, X11 and libcursor are also required, check the Makefile.

## How to Use
- **Zoom & Pan:** Scroll to zoom, middle mouse button to pan (canvas is infinite)
- **Draw:** Left click to stroke with primary color, right click for secondary color (strokes are somewhat smoothed)
- **Colors:** Press `1` for fluorescent lemon green, `2` for hotpink, `3` for turquoise as primary color. Hold Alt to set as secondary instead
- **Delete:** Hold `X` to select a stroke, release to delete
- **Undo/Redo:** `Ctrl+Z` to undo, `Ctrl+R` to redo