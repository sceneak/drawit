#define GLAD_GL_IMPLEMENTATION
#include <glad2/gl.h>
#undef GLAD_GL_IMPLEMENTATION

#include <sokol/sokol_app.h> 
#include <sokol/sokol_time.h>

#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg/nanovg.h>
#include <nanovg/nanovg_gl.h>
#undef NANOVG_GL3_IMPLEMENTATION

#define PFH_IMPLEMENTATION
#include <perfect-freehand/pfh.h>
#undef PFH_IMPLEMENTATION

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>

#include "types.h"
#include "canvas.c"

#define VELOCITY_MAX_IN 1200 /* will mult. by screen_dpi_scale (inch) */
#define STATUS_LINE_MAX 512
#define PEN_PT_INIT { .coord = { FLT_MAX, FLT_MAX }, .pressure = .5 }
#define FONT_SIZE_DEFAULT 64.0

/* ==============================
 * CORE DATA STRUCTURES 
 * ==============================
 */
struct camera {
	vec2 pos;
	float zoom;
	float zoom_frac;

	bool is_panning;
	vec2 pan_pivot_mouse;
	vec2 pan_pivot_self;
};

struct mouse {
	vec2 screen;
	vec2 world;
	bool is_outside_frame;
	bool hide;
};

struct graphic_ctx {
	float screen_dpi_scale; /* needs to be cached due to sokol wackyness */
	int screen_width, screen_height;

	NVGcontext *vg;
	int font_handle;

	struct camera cam;
	struct mouse mouse;
};


enum theme {
	THEME_DARK,
	THEME_LIGHT,
	THEME_MAX,
};

enum mode {
	MODE_NORMAL,
	MODE_SELECT,
	MODE_DRAW,
	MODE_COMMAND,
	MODE_TEXT,
	MODE_DRAG,
};

struct pen {
	struct pen_pt curr_pt, last_pt;
	const color *colors_primary;
	const color *colors_secondary;
	const color **active_colors;
};

struct status_line {
	int verbosity;
	char buf[STATUS_LINE_MAX];
	size_t len;
};

enum select_state {
	SELECT_STATE_SINGLE,
	SELECT_STATE_BOX,
};

struct drawit_ctx {
	struct graphic_ctx       gfx;

	enum theme               theme;
	enum mode                mode;
	enum mode                exit_mode;

	struct status_line       status_line;

	struct pen               pen;

	enum select_state        select_state;
	struct da_object_handle *selection_da;
	vec2                     select_anchor;
	rect                     select_rect;
	bool                     is_selecting_rect;

	bool                     hide_selection_outline;

	bool                     is_grouping;
	struct canvas_checkpoint group_checkpoint;

	vec2                     drag_start, drag_end;

	int                      active_text_id;

	struct canvas            canvas;
	struct canvas_hist       hist;
};

/* GLOBALS */
static const u8 APP_ICON_32x32[] = {
	#ifndef __INTELLISENSE__
		#include "gen/icon32x32.inc"
	#else
		0
	#endif
};
#include "gen/inconsolata_ttf.h"

static const color COLOR_DEEP_CHARCOAL = COLOR_INIT_HEX(0x121212FF);
static const color COLOR_SKIN = COLOR_INIT_HEX(0xF5E1D2FF);
static const color COLOR_SCENE = COLOR_INIT_HEX(0xCCFF00FF);
static const color COLOR_MARIGOLD = COLOR_INIT_HEX(0xD97706FF);
static const color COLOR_HOTPINK = COLOR_INIT_HEX(0xFF69B4FF);
static const color COLOR_RASPBERRY = COLOR_INIT_HEX(0xD81B60FF);
static const color COLOR_TURQUOISE = COLOR_INIT_HEX(0x40E0D0FF);
static const color COLOR_TEAL = COLOR_INIT_HEX(0x009688FF);

static const color COLOR_SOFT_LAVENDER  = COLOR_INIT_HEX(0xC084FCFF);
static const color COLOR_DEEP_VIOLET = COLOR_INIT_HEX(0x7C3AEDFF);
static const color COLOR_SKY_BLUE = COLOR_INIT_HEX(0x38BDF8FF);
static const color COLOR_DEEP_BLUE = COLOR_INIT_HEX(0x2563EBFF);

static const color COLORS_BACKGROUND[THEME_MAX] = { COLOR_DEEP_CHARCOAL, COLOR_SKIN };
static const color COLORS_CONTRAST[THEME_MAX] = { COLOR_SKIN, COLOR_DEEP_CHARCOAL };
static const color COLORS_OUTLINE[THEME_MAX] = { COLOR_SOFT_LAVENDER, COLOR_DEEP_VIOLET };
static const color COLORS_SELECT[THEME_MAX] = { COLOR_SKY_BLUE, COLOR_DEEP_BLUE };
static const color COLORS_YELLOW[THEME_MAX] = { COLOR_SCENE, COLOR_MARIGOLD };
static const color COLORS_BLUE[THEME_MAX] = { COLOR_TURQUOISE, COLOR_TEAL };
static const color COLORS_RED[THEME_MAX] = { COLOR_HOTPINK, COLOR_RASPBERRY };

static struct drawit_ctx g_drawit = {
	.status_line = {
		.verbosity = 1
	},
	.gfx = {
		.cam = { .zoom = 1.0f, .zoom_frac = 0.1f }
	}
};

/* HELPERS */
static inline vec2 screen_to_world(const struct graphic_ctx *gfx, vec2 screen)
{
	return (vec2) {
		.x = ( (screen.x - gfx->screen_width/2) / gfx->cam.zoom) + gfx->cam.pos.x,
		.y = ( (screen.y - gfx->screen_height/2) / gfx->cam.zoom) + gfx->cam.pos.y,
	};
}

static inline void set_mouse_world_from_screen(const struct graphic_ctx *gfx, struct mouse *mouse)
{
	mouse->world = screen_to_world(gfx, mouse->screen);
}

static inline NVGcolor color_to_NVGcolor(color c)
{
	return nvgRGBA(c.r, c.g, c.b, c.a);
}

static inline void status_line_set(struct status_line *s, int level, const char *fmt, ...)
{
	va_list args;

	if (level > s->verbosity) return;

	va_start(args, fmt);
	s->len = vsnprintf(s->buf, ARRAY_SIZE(s->buf), fmt, args);
	va_end(args);

	s->len = min(s->len, ARRAY_SIZE(s->buf)-1);
}

/* ==============================
 * DRAWIT HELPERS
 * ==============================
 */
static void drawit_undo(struct drawit_ctx *drawit)
{
	if (canvas_hist_undo(&drawit->hist) == -1) {
		status_line_set(&drawit->status_line, 0, "Already at oldest history");
	}
}
static void drawit_redo(struct drawit_ctx *drawit)
{
	if (canvas_hist_redo(&drawit->hist) == -1) {
		status_line_set(&drawit->status_line, 0, "Already at newest change");
	}
}

static int drawit_select_mouse(struct drawit_ctx *drawit)
{
	struct da_object_handle **selection = &drawit->selection_da;
	struct canvas *canvas = &drawit->canvas;
	struct mouse *mouse = &drawit->gfx.mouse;

	if (!(*selection)->count) {
		da_object_handle_append_empty(selection, 1);
	} else if ((*selection)->count > 1) {
		(*selection)->count = 1;
	}

	(*selection)->elems[0] = canvas_object_hit(canvas, mouse->world);

	if ((*selection)->elems[0].kind == OBJECT_KIND_NONE) {
		(*selection)->count = 0;
		return -1;
	}
	return 0;
}

static void drawit_select_rect(struct drawit_ctx *drawit)
{
	struct da_object_handle **selection = &drawit->selection_da;
	struct canvas *canvas = &drawit->canvas;

	(*selection)->count = 0;
	canvas_object_hit_rect(canvas, selection, g_drawit.select_rect);
}

static void drawit_delete_selection(struct drawit_ctx *drawit)
{
	struct da_object_handle *selection_da = drawit->selection_da;
	struct canvas *canvas = &drawit->canvas;
	struct canvas_hist *hist = &drawit->hist;

	struct object_handle *hndls;
	size_t i;

	for (i = 0; i < selection_da->count; i++) {
		canvas_object_set_deleted(canvas, selection_da->elems[i], true);
	}
	hndls = da_object_handle_elemdup(selection_da);
	canvas_hist_record(hist, canvas_cmd_objs_make(CANVAS_CMD_OBJECT_DELETE, hndls, selection_da->count));
	selection_da->count = 0;
}

static void drawit_group_owned(struct drawit_ctx *drawit, struct object_handle *hndls, size_t count)
{
	struct canvas *canvas = &drawit->canvas;
	struct canvas_hist *hist = &drawit->hist;

	if (canvas_object_group_owned(canvas, hndls, count) != 0) {
		free(hndls);
		return;
	}
	canvas_hist_record(hist, canvas_cmd_make(CANVAS_CMD_OBJECT_GROUP));
}

static void drawit_delete_group(struct drawit_ctx *drawit)
{
	struct da_object_handle *selection_da = drawit->selection_da;
	struct canvas *canvas = &drawit->canvas;
	struct canvas_hist *hist = &drawit->hist;

	if (selection_da->elems[0].kind != OBJECT_KIND_GROUP) return;

	canvas_group_set_deleted(canvas, selection_da->elems[0].id, true);
	canvas_hist_record(hist, canvas_cmd_obj_make(CANVAS_CMD_GROUP_DELETE, selection_da->elems[0]));
	selection_da->count = 0;
}


/* ==============================
 * MODES
 * ==============================
 */
static void mode_enter_normal(void);
static void mode_enter_select(void);
static void mode_enter_draw(void);
static void mode_enter_command(void);
static void mode_enter_text(int edit_id);
static void mode_enter_drag(void);

static void mode_exit_set(void) { g_drawit.exit_mode = g_drawit.mode; }
static void mode_exit(void)
{
	switch(g_drawit.exit_mode) {
	case MODE_SELECT: mode_enter_select(); break;
	default: mode_enter_normal();
	}
	g_drawit.exit_mode = MODE_NORMAL;
}

/* MODE: NORMAL */
static void mode_normal_event_camera(struct graphic_ctx *gfx, const sapp_event *e)
{
	struct camera *cam = &gfx->cam;
	struct mouse *mouse = &gfx->mouse;
	float ratio;

	switch(e->type) {
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		if (cam->is_panning) {
			cam->pos.x = cam->pan_pivot_self.x +
			             (cam->pan_pivot_mouse.x - mouse->screen.x)/cam->zoom;
			cam->pos.y = cam->pan_pivot_self.y +
			             (cam->pan_pivot_mouse.y - mouse->screen.y)/cam->zoom;
		}
		break;
	case SAPP_EVENTTYPE_MOUSE_UP:
		cam->is_panning = cam->is_panning && e->mouse_button != SAPP_MOUSEBUTTON_MIDDLE;
		break;
	case SAPP_EVENTTYPE_MOUSE_DOWN:
		if (e->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
			cam->is_panning = true;
			cam->pan_pivot_mouse = mouse->screen;
			cam->pan_pivot_self = cam->pos;
		}
		break;
	case SAPP_EVENTTYPE_MOUSE_SCROLL:
		ratio = (1 + cam->zoom_frac * e->scroll_y);
		cam->zoom *= ratio;
		/* (World - OldCamera) * OldZoom = (World - NewCamera) * NewZoom */
		cam->pos.x = mouse->world.x - (mouse->world.x - cam->pos.x) / ratio;
		cam->pos.y = mouse->world.y - (mouse->world.y - cam->pos.y) / ratio;
		break;
	default: break;
	}
}

static int mode_normal_event_transitions(const sapp_event *e)
{
	switch (e->type) {
	case SAPP_EVENTTYPE_CHAR: 
		if (e->char_code == ':') {
			mode_enter_command();
			return 1;
		}
		if (e->char_code == 'i') {
			mode_enter_text(-1);
			return 1;
		}
		break;
	default: break;
	}
	return 0;
}

static void mode_normal_event(const sapp_event *e)
{
	struct canvas_checkpoint *checkpt = &g_drawit.group_checkpoint;
	struct canvas *canvas = &g_drawit.canvas;
	struct pen *pen = &g_drawit.pen;
	struct status_line *status = &g_drawit.status_line;
	struct graphic_ctx *gfx = &g_drawit.gfx;

	struct object_handle *hndls;
	size_t count;

	if (mode_normal_event_transitions(e) != 0)
		return;

	mode_normal_event_camera(gfx, e);

	switch (e->type) {
	case SAPP_EVENTTYPE_MOUSE_DOWN:
		if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT)
			pen->active_colors = &pen->colors_primary;
		else if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT)
			pen->active_colors = &pen->colors_secondary;
		else break;
		mode_enter_draw();
		break;
	/* Keyboard */
	case SAPP_EVENTTYPE_KEY_DOWN:
		pen->active_colors = &pen->colors_primary;
		if (e->modifiers & SAPP_MODIFIER_ALT)
			pen->active_colors = &pen->colors_secondary;

		switch (e->key_code) {
		case SAPP_KEYCODE_1:
			*pen->active_colors = COLORS_YELLOW;
			break;
		case SAPP_KEYCODE_2:
			*pen->active_colors = COLORS_RED;
			break;
		case SAPP_KEYCODE_3:
			*pen->active_colors = COLORS_BLUE;
			break;
		case SAPP_KEYCODE_4:
			*pen->active_colors = COLORS_CONTRAST;
			break;
		case SAPP_KEYCODE_Z:
			if (e->modifiers & SAPP_MODIFIER_CTRL) {
				drawit_undo(&g_drawit);
			}
			break;
		case SAPP_KEYCODE_R:
			if (e->modifiers & SAPP_MODIFIER_CTRL) {
				drawit_redo(&g_drawit);
			}
			break;
		case SAPP_KEYCODE_W:
			if (e->key_repeat) break;

			if (drawit_select_mouse(&g_drawit) != 0) break;
			mode_exit_set();
			mode_enter_drag();
			break;
		case SAPP_KEYCODE_S:
			mode_enter_select();
			break;
		case SAPP_KEYCODE_X:
			if (e->key_repeat) break;

			if (drawit_select_mouse(&g_drawit) != 0) break;
			drawit_delete_selection(&g_drawit);
			break;
		case SAPP_KEYCODE_A:
			if (e->key_repeat) break;

			if (g_drawit.is_grouping) {
				g_drawit.is_grouping = false;

				if (canvas_objects_created_after(canvas, *checkpt,
				                                 &hndls, &count) != 0) {
					status_line_set(status, 1, "Invalid Grouping Checkpoint");
					break;
				}
				drawit_group_owned(&g_drawit, hndls, count);
				status_line_set(status, 1, "Commit Group");
			} else {
				g_drawit.is_grouping = true;

				*checkpt = canvas_checkpoint_make(canvas);
				status_line_set(status, 1, "Begin Group");
			}
			break;
		case SAPP_KEYCODE_M:
			/* g_drawit.save_idx = g_drawit.hist.cursor; */
			break;
		default: break;
		}

		pen->active_colors = &pen->colors_primary;
		break;
	case SAPP_EVENTTYPE_KEY_UP: break;
	default: break;
	}
}

static void mode_enter_normal(void)
{
	struct da_object_handle *selection = g_drawit.selection_da;

	g_drawit.mode = MODE_NORMAL;
	g_drawit.gfx.mouse.hide = false;
	sapp_show_mouse(false);

	selection->count = 0;
}

/* MODE: SELECT */
static void mode_select_event(const sapp_event *e)
{
	struct mouse *mouse = &g_drawit.gfx.mouse;
	struct object_handle *hndls;

	switch(g_drawit.select_state) {
	case SELECT_STATE_SINGLE:
		drawit_select_mouse(&g_drawit);
		break;
	case SELECT_STATE_BOX:
		drawit_select_rect(&g_drawit);
		break;
	default: break;
	}

	switch (e->type) {
	case SAPP_EVENTTYPE_MOUSE_DOWN:
		g_drawit.select_state = SELECT_STATE_BOX;
		g_drawit.select_anchor = mouse->world;
		g_drawit.select_rect = rect_fit2(mouse->world, mouse->world);
		g_drawit.is_selecting_rect = true;
		break;
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		if (!g_drawit.is_selecting_rect) break;
		g_drawit.select_rect = rect_fit2(g_drawit.select_anchor, mouse->world);
		break;
	case SAPP_EVENTTYPE_MOUSE_UP:
		if (!g_drawit.selection_da->count) {
			g_drawit.select_state = SELECT_STATE_SINGLE;
		}
		g_drawit.is_selecting_rect = false;
		break;
	default: break;
	}

	if (g_drawit.is_selecting_rect) return;

	switch (e->type) {
	case SAPP_EVENTTYPE_KEY_DOWN:
		switch(e->key_code) {
		case SAPP_KEYCODE_Z:
			if (e->modifiers & SAPP_MODIFIER_CTRL) {
				drawit_undo(&g_drawit);
			}
			break;
		case SAPP_KEYCODE_R:
			if (e->modifiers & SAPP_MODIFIER_CTRL) {
				drawit_redo(&g_drawit);
			}
			break;
		case SAPP_KEYCODE_W:
			if (e->key_repeat) break;

			if (!g_drawit.selection_da->count) break;
			mode_exit_set();
			mode_enter_drag();
			break;
		case SAPP_KEYCODE_X:
			if (e->key_repeat) break;

			if (!g_drawit.selection_da->count) break;
			drawit_delete_selection(&g_drawit);
			break;
		case SAPP_KEYCODE_A:
			if (e->key_repeat) break;

			if (e->modifiers & SAPP_MODIFIER_SHIFT) {
				if (!g_drawit.selection_da->count) break;
				drawit_delete_group(&g_drawit);
			} else if (g_drawit.selection_da->count) {
				hndls = da_object_handle_elemdup(g_drawit.selection_da);
				drawit_group_owned(&g_drawit, hndls, g_drawit.selection_da->count);
			}
			break;
		case SAPP_KEYCODE_ESCAPE:
			g_drawit.select_state = SELECT_STATE_SINGLE;
			mode_exit();
			break;
		default: break;
		}
		break;
	default:
		break;
	}
}

static void mode_enter_select(void)
{
	g_drawit.mode = MODE_SELECT;
	g_drawit.gfx.mouse.hide = true;
	sapp_show_mouse(true);
}

/* MODE: DRAG */
static void mode_drag_event(const sapp_event *e)
{
	struct da_object_handle *selection_da = g_drawit.selection_da;
	struct canvas *canvas = &g_drawit.canvas;
	struct mouse *mouse = &g_drawit.gfx.mouse;
	struct canvas_hist *hist = &g_drawit.hist;
	struct object_handle *hndls;
	size_t i;
	vec2 d;

	switch (e->type) {
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		d = vec2_sub(mouse->world, g_drawit.drag_end);

		for (i = 0; i < selection_da->count; i++) { 
			canvas_object_transl(canvas, selection_da->elems[i], d);
		}
		g_drawit.drag_end = mouse->world;
		break;

	case SAPP_EVENTTYPE_KEY_DOWN:
		if (e->key_repeat) break;

		switch (e->key_code) {
		case SAPP_KEYCODE_W:
			d = vec2_sub(g_drawit.drag_end, g_drawit.drag_start);
			hndls = da_object_handle_elemdup(selection_da);
			canvas_hist_record(hist, canvas_cmd_drag_make(hndls, selection_da->count, d));
			mode_exit();
			break;
		case SAPP_KEYCODE_ESCAPE:
			d = vec2_sub(g_drawit.drag_start, g_drawit.drag_end);
			for (i = 0; i < selection_da->count; i++) { 
				canvas_object_transl(canvas, selection_da->elems[i], d);
			}
			mode_exit();
		default: break;
		}
		break;
	default:
		break;
	}
}

static void mode_enter_drag(void)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;

	g_drawit.mode = MODE_DRAG;
	gfx->mouse.hide = true;
	sapp_show_mouse(false);

	g_drawit.drag_start = gfx->mouse.world;
	g_drawit.drag_end = gfx->mouse.world;
}

/* MODE: DRAW */

static void mode_draw_stroke_try_append(struct stroke_store *store, struct pen_pt pt)
{
	const int MIN_PX = 2;
	static struct pen_pt last_valid_pt = PEN_PT_INIT;

	if (vec2_dist2(pt.coord, last_valid_pt.coord) < MIN_PX*MIN_PX)
		return;
	last_valid_pt = pt;

	stroke_store_append_point(store, pt);
}

static void mode_draw_event(const sapp_event *e)
{
	struct stroke_store *store = &g_drawit.canvas.stroke_store;
	struct pen *pen = &g_drawit.pen;

	switch (e->type) {
	case SAPP_EVENTTYPE_MOUSE_UP:
		mode_exit();
		break;
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		mode_draw_stroke_try_append(store, pen->curr_pt);
		break;
	default: break;
	}
}

static void mode_enter_draw(void)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct status_line *status = &g_drawit.status_line;

	struct stroke_store *store = &g_drawit.canvas.stroke_store;
	struct canvas_hist *hist = &g_drawit.hist;
	struct pen *pen = &g_drawit.pen;
	const color *colors = *pen->active_colors;

	if (status->len) status->len = 0;

	g_drawit.mode = MODE_DRAW;
	gfx->mouse.hide = false;
	sapp_show_mouse(false);

	stroke_store_begin(store, colors);
	stroke_store_append_point(store, pen->curr_pt);
	/* What's recorded is only the 'act' of creating a stroke */
	canvas_hist_record(hist, canvas_cmd_make(CANVAS_CMD_STROKE_CREATE));
}


/* MODE: COMMAND */

static void command_exec(const char *str)
{
	struct status_line *status = &g_drawit.status_line;
	int tmp;

	if (str[0] == ':') {
		str++;
		if (str[0] == 'l') {
			g_drawit.theme = THEME_LIGHT;
			status_line_set(status, 0, "theme=light");
		} else if (drawit_strcasecmp(str, "dark") == 0) {
			g_drawit.theme = THEME_DARK;
			status_line_set(status, 0, "theme=dark");
		} else if (drawit_strcasecmp(str, "quit") == 0) {
			sapp_request_quit();
		} else if (drawit_strcasecmp(str, "meta") == 0) {
			status_line_set(status, 0, "Compiled on " __DATE__ " " __TIME__);
		} else if (drawit_strcasecmp(str, "selout") == 0) {
			g_drawit.hide_selection_outline = !g_drawit.hide_selection_outline;
			status_line_set(status, 0, "toggle selection outline");
		} else if (sscanf(str, "verbosity %d", &tmp) == 1) {
			g_drawit.status_line.verbosity = tmp;
			status_line_set(status, 0, "verbosity=%d", tmp);
		} else {
			status_line_set(status, 0, "unknown command");
		}
	}
}

static void mode_command_event(const sapp_event *e)
{
	struct status_line *status = &g_drawit.status_line;

	if (e->type == SAPP_EVENTTYPE_CHAR && status->len < STATUS_LINE_MAX-1) {
		status->buf[status->len++] = e->char_code;
		return;
	}
	if (e->type != SAPP_EVENTTYPE_KEY_DOWN)
		return;

	switch (e->key_code) {
	case SAPP_KEYCODE_BACKSPACE:
		if (status->len <= 1)
			break;

		status->len--;
		break;
	case SAPP_KEYCODE_ENTER:
		status->buf[status->len] = '\0';
		command_exec(status->buf); /* status line is reused to write results. don't zero */

		mode_exit();
		break;
	case SAPP_KEYCODE_ESCAPE:
		mode_exit();
		status->len = 0;
		break;
	default: break;
	}
}

static void mode_enter_command(void)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct status_line *status = &g_drawit.status_line;

	g_drawit.mode = MODE_COMMAND;
	gfx->mouse.hide = true;
	status_line_set(status, 0, ":");
	sapp_show_mouse(true);
}


/* MODE: TEXT */

static void mode_text_event(const sapp_event *e)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct text_store *store = &g_drawit.canvas.text_store;
	struct text_obj *t = store->text_da->elems + g_drawit.active_text_id;

	assert(t);

	if (e->type == SAPP_EVENTTYPE_CHAR) {
		text_obj_insert(t, (unsigned char)e->char_code);
	}

	if (e->type != SAPP_EVENTTYPE_KEY_DOWN)
		return;

	switch (e->key_code) {
	case SAPP_KEYCODE_LEFT:
		text_obj_cursor(t, -1);
		break;
	case SAPP_KEYCODE_RIGHT:
		text_obj_cursor(t, 1);
		break;
	case SAPP_KEYCODE_UP:
		break;
	case SAPP_KEYCODE_DOWN:
		break;
	case SAPP_KEYCODE_BACKSPACE:
		text_obj_backspace(t);
		break;
	case SAPP_KEYCODE_ENTER:
		text_obj_insert(t, '\n');
		break;
	case SAPP_KEYCODE_ESCAPE:
		g_drawit.active_text_id = -1;
		mode_exit();
		break;
	default: break;
	}

	if (t->bounds_dirty) {
		text_obj_update_bounds(t, gfx->vg);
	}
}

static void mode_enter_text(int edit_id)
{
	struct text_store *store = &g_drawit.canvas.text_store;
	struct canvas_hist *hist = &g_drawit.hist;
	const color *colors = *g_drawit.pen.active_colors;
	struct graphic_ctx *gfx = &g_drawit.gfx;

	if (edit_id >= 0) {
		g_drawit.active_text_id = edit_id;
	} else {
		text_store_append(store, colors, gfx->font_handle, FONT_SIZE_DEFAULT, gfx->mouse.world);
		canvas_hist_record(hist, canvas_cmd_make(CANVAS_CMD_TEXT_CREATE));

		g_drawit.active_text_id = store->text_da->count-1;
	}

	g_drawit.gfx.mouse.hide = true;
	g_drawit.mode = MODE_TEXT;
	sapp_show_mouse(true);
}

/* ==============================
 * NVG DRAW FUNCS
 * ==============================
 */
static inline vec2 get_cursor_offset(struct graphic_ctx *gfx, const struct text_obj *txt)
{
	size_t start, i;
	float x = 0, 
	      y = txt->line_height/2;


	start = 0;
	for (i = 0; i < txt->buf->gap_start; i++) {
		if (txt->buf->data[i] == '\n') {
			y += txt->line_height;
			start = i+1;
		}
	}
	text_obj_nvg_context(txt, gfx->vg);
	x = nvgTextBounds(gfx->vg, 0, 0, txt->buf->data + start, txt->buf->data + i, NULL);
	return (vec2) { x, y };
}

static inline vec2 get_cursor_size(struct graphic_ctx *gfx, const struct text_obj *txt)
{
	float ascender, descender;
	float advance;

	text_obj_nvg_context(txt, gfx->vg);
	nvgTextMetrics(gfx->vg, &ascender, &descender, NULL);

	if (txt->buf->gap_end == txt->buf->capacity)
		advance = nvgTextBounds(gfx->vg, 0, 0, " ", NULL, NULL);
	else
		advance = nvgTextBounds(gfx->vg, 0, 0, 
			txt->buf->data + txt->buf->gap_end,
			txt->buf->data + txt->buf->gap_end + 1, /* Assumes ASCII */
			NULL
		);

	return (vec2) { advance, ascender - descender };
}

void draw_text_cursor_overlay(struct graphic_ctx *gfx,
                              const struct text_obj *txt,
                              enum theme theme, bool local)
{
	vec2 offset = get_cursor_offset(gfx, txt);
	vec2 size = get_cursor_size(gfx, txt);
	vec2 transl = {
		(local ? 0 : txt->obj.transl.x) + offset.x,
		(local ? 0 : txt->obj.transl.y) + offset.y,
	};
	const char *c = txt->buf->gap_end < txt->buf->capacity ? txt->buf->data + txt->buf->gap_end : " ";

	nvgFillColor(gfx->vg, color_to_NVGcolor(COLORS_CONTRAST[theme]));
	nvgBeginPath(gfx->vg);
	nvgRect(gfx->vg, transl.x, transl.y - size.y/2, size.x, size.y); /* offset y to middle */
	nvgFill(gfx->vg);

	text_obj_nvg_context(txt, gfx->vg);
	nvgFillColor(gfx->vg, color_to_NVGcolor(COLORS_BACKGROUND[theme]));
	nvgText(gfx->vg, transl.x, transl.y, c, c+1); /* I give up messing with GlobalCompositeOperations */
}

void draw_bounds(struct graphic_ctx *gfx, rect bounds, const color c)
{
	nvgBeginPath(gfx->vg);
	nvgRect(gfx->vg,
		bounds.x0,
		bounds.y0,
		bounds.x1 - bounds.x0,
		bounds.y1 - bounds.y0
	);
	nvgStrokeColor(gfx->vg, nvgRGBA(c.r, c.g, c.b, c.a));
	nvgStrokeWidth(gfx->vg, 2.0f);
	nvgStroke(gfx->vg);
}

void draw_obj_bounds(struct graphic_ctx *gfx, const struct object_data *obj, enum theme theme)
{
	rect global_bounds = rect_transl(obj->bounds, obj->transl);
	draw_bounds(gfx, global_bounds, COLORS_OUTLINE[theme]);
}

void draw_text(struct graphic_ctx *gfx, const struct text_obj *txt, enum theme theme, bool draw_cursor)
{
	const struct gapbuf *buf = txt->buf;

	size_t start, i;
	float x = 0,
	      y = 0;

	text_obj_nvg_context(txt, gfx->vg);
	nvgFillColor(gfx->vg, color_to_NVGcolor(txt->colors[theme]));

	nvgSave(gfx->vg);
	nvgTranslate(gfx->vg, txt->obj.transl.x, txt->obj.transl.y);
		x = 0;
		y = txt->line_height/2;

		start = i = 0;
		for (;;) {
			if (i >= buf->capacity) { /* NOTE: nvg already checks start != end */
				nvgText(gfx->vg, x, y, buf->data + start, buf->data + i);
				break;
			}

			if (i == buf->gap_start && (buf->gap_start != buf->gap_end)) {
				x = nvgText(gfx->vg, x, y, buf->data + start, buf->data + i);
				start = i = buf->gap_end;
				continue;
			}

			if (buf->data[i] == '\n') {
				nvgText(gfx->vg, x, y, buf->data + start, buf->data + i);
				start = i+1;

				y += txt->line_height;
				x = 0;
			}
			i++;
		}
		if (draw_cursor)
			draw_text_cursor_overlay(gfx, txt, theme, true);
	nvgRestore(gfx->vg);
}

void draw_text_store(struct graphic_ctx *gfx,
                     const struct text_store *store,
                     enum theme theme,
                     int active_text_id)
{
	size_t i;

	for (i = 0; i < store->text_da->count; i++) {
		if (store->text_da->elems[i].obj.deleted) continue;
		draw_text(gfx,
			  store->text_da->elems + i, 
			  theme,
			  i == (size_t)active_text_id);
	}
}

void draw_selected_bounds(struct graphic_ctx *gfx,
                          const struct canvas *canvas,
                          const struct da_object_handle *selects,
                          enum theme theme)
{
	struct object_data *obj;
	size_t i;

	for (i = 0; i < selects->count; i++) { 
		obj = canvas_object_get(canvas, selects->elems[i]);
		draw_obj_bounds(gfx, obj, theme);
	}
}

void draw_rect(struct graphic_ctx *gfx, rect r)
{
	nvgBeginPath(gfx->vg);
		nvgRect(gfx->vg, r.x0, r.y0, r.x1-r.x0, r.y1-r.y0);
	nvgStrokeColor(gfx->vg, nvgRGBA(0, 255, 0, 255));
	nvgStrokeWidth(gfx->vg, 2.0f);
	nvgStroke(gfx->vg);
}

void draw_stroke_store(struct graphic_ctx *gfx, struct stroke_store *store)
{
	size_t i, j;
	struct stroke_desc *s;
	pfh_vec2 *s_vertices;
	pfh_vec2 p0, p1;

	for (i = 0; i < store->desc_da->count; i++) {
		s = store->desc_da->elems + i;
		if (s->obj.deleted)
			continue;
		if (s->vertex_count < 3) /* shouldn't happen, a dot is 13 segs */
			continue;

		s_vertices = store->pfh_vertex_buf.elems + s->vertex_idx;

		nvgSave(gfx->vg);
		nvgTranslate(gfx->vg, s->obj.transl.x, s->obj.transl.y);
			nvgBeginPath(gfx->vg);
			nvgFillColor(gfx->vg, color_to_NVGcolor(s->colors[g_drawit.theme]));

			p0 = s_vertices[s->vertex_count-1];
			p1 = s_vertices[0];
			nvgMoveTo(gfx->vg, (p0.x + p1.x) / 2, (p0.y + p1.y) / 2);

			for (j = 0; j < s->vertex_count; j++) {
				p0 = s_vertices[j];
				p1 = (j+1) == s->vertex_count
					? s_vertices[0]
					: s_vertices[j + 1];
				nvgQuadTo(gfx->vg, p0.x, p0.y, (p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
			}
			nvgFill(gfx->vg);
		nvgRestore(gfx->vg);
	}
}
/*
void draw_selection(struct da_object_handle *objs)
{
	tmp = stroke_store_nearest(store, gfx->mouse.world);
	if (tmp == -1)
		return;
	draw_rect(gfx, store->desc_da->elems[tmp].obj.bounds);
}
*/

void draw_status_line(struct graphic_ctx *gfx, const struct status_line *status)
{
	const float FONT_SIZE = 26.0;
	const vec2 coord = { 0 + FONT_SIZE, gfx->screen_height - FONT_SIZE };

	if (!status->len)
		return;

	nvgFontSize(gfx->vg, FONT_SIZE);
	nvgFontFaceId(gfx->vg, gfx->font_handle);
	nvgFillColor(gfx->vg, color_to_NVGcolor(COLORS_CONTRAST[g_drawit.theme]));
	nvgTextAlign(gfx->vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgText(gfx->vg, coord.x, coord.y, status->buf, status->buf + status->len);
}


/* ==============================
 * SOKOL APP
 * ==============================
 */
void init(void) 
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct pen *pen = &g_drawit.pen;
	stm_setup();

	g_drawit.canvas = canvas_create_empty();
	g_drawit.hist.canvas = &g_drawit.canvas;
	da_object_handle_create(&g_drawit.selection_da, DA_INITIAL_CAPACITY);

	pen->colors_primary = COLORS_YELLOW;
	pen->colors_secondary = COLORS_RED;
	pen->active_colors = &pen->colors_primary;

	gfx->screen_width = sapp_width();
	gfx->screen_height = sapp_height();
	gladLoaderLoadGL();
	gfx->vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

	gfx->font_handle = nvgCreateFontMem(gfx->vg,
	                                    "Inconsolata-Regular-Sub",
	                                    (u8 *)inconsolata_ttf,
	                                    inconsolata_ttf_len, 0);

	mode_exit();

#ifdef _WIN32
	#include <windows.h>
	HWND hwnd = (HWND)sapp_win32_get_hwnd();
	ShowWindow(hwnd, SW_MAXIMIZE);
#endif
}

void cleanup(void)
{
	gladLoaderUnloadGL();
	nvgDeleteGL3(g_drawit.gfx.vg);
}

void event_pen(const sapp_event *e)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct pen *pen = &g_drawit.pen;
	static u64 last_move = 0;
	double delta, vel;

	switch (e->type) {
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		gfx->mouse.screen.x = e->mouse_x;
		gfx->mouse.screen.y = e->mouse_y;
		gfx->mouse.world = screen_to_world(gfx, gfx->mouse.screen);
		/* fall through */
	case SAPP_EVENTTYPE_MOUSE_DOWN:
	case SAPP_EVENTTYPE_MOUSE_UP:
		delta = stm_sec(stm_laptime(&last_move));

		pen->curr_pt.coord = gfx->mouse.world;
		vel = ( sqrtf(vec2_dist2(pen->curr_pt.coord, pen->last_pt.coord)) / delta );

		pen->curr_pt.pressure = 1 - min(vel / (VELOCITY_MAX_IN * gfx->screen_dpi_scale), 1);
		pen->curr_pt.pressure = pen->last_pt.pressure * .75 + pen->curr_pt.pressure * .25;
		/* ^ blend */

		pen->last_pt = pen->curr_pt;
		break;
	default: break;
	}
}

void event(const sapp_event *e)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;

	switch (e->type) {
	case SAPP_EVENTTYPE_MOUSE_ENTER:
		gfx->mouse.is_outside_frame = false;
		break;
	case SAPP_EVENTTYPE_MOUSE_LEAVE:
		gfx->mouse.is_outside_frame = true;
		break;
	case SAPP_EVENTTYPE_RESIZED:
		gfx->screen_width = sapp_width();
		gfx->screen_height = sapp_height();
		break;
	default: break;
	}

	event_pen(e);

	switch (g_drawit.mode) {
	case MODE_NORMAL:
		mode_normal_event(e);
		break;
	case MODE_SELECT:
		mode_select_event(e);
		break;
	case MODE_DRAW:
		mode_draw_event(e);
		break;
	case MODE_COMMAND:
		mode_command_event(e);
		break;
	case MODE_TEXT:
		mode_text_event(e);
		break;
	case MODE_DRAG:
		mode_drag_event(e);
		break;
	default: break;
	}
}

void frame(void) 
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct pen *pen = &g_drawit.pen;
	struct camera *cam = &gfx->cam;
	struct mouse *mouse = &gfx->mouse;
	struct canvas *canvas = &g_drawit.canvas;

	color c;
	gfx->screen_dpi_scale = sapp_dpi_scale();

	glViewport(0, 0, gfx->screen_width, gfx->screen_height);
	glClearColor(COLORS_BACKGROUND[g_drawit.theme].r/255.0f,
	             COLORS_BACKGROUND[g_drawit.theme].g/255.0f,
	             COLORS_BACKGROUND[g_drawit.theme].b/255.0f,
	             COLORS_BACKGROUND[g_drawit.theme].a/255.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	nvgBeginFrame(gfx->vg,
	              gfx->screen_width/gfx->screen_dpi_scale, gfx->screen_height/gfx->screen_dpi_scale,
	              gfx->screen_dpi_scale);
		nvgSave(gfx->vg);
			nvgTranslate(gfx->vg, gfx->screen_width/2, gfx->screen_height/2);
			nvgScale(gfx->vg, cam->zoom, cam->zoom);
			nvgTranslate(gfx->vg, -cam->pos.x, -cam->pos.y);

			draw_stroke_store(gfx, &canvas->stroke_store);
			draw_text_store(gfx, &canvas->text_store, g_drawit.theme, g_drawit.active_text_id);
			if (!g_drawit.hide_selection_outline)
				draw_selected_bounds(gfx, canvas, g_drawit.selection_da, g_drawit.theme);
			if (g_drawit.is_selecting_rect)
				draw_bounds(gfx, g_drawit.select_rect, COLORS_SELECT[g_drawit.theme]);

		nvgRestore(gfx->vg);

		if (!gfx->mouse.hide && !gfx->mouse.is_outside_frame) {
			nvgBeginPath(gfx->vg);
				nvgCircle(gfx->vg,
				          roundf(mouse->screen.x),
				          round(mouse->screen.y),
				          cam->zoom*STROKE_OPTS.size/1.5);
			c = (*pen->active_colors)[g_drawit.theme];
			c.a /= 1.5;
			nvgFillColor(gfx->vg, color_to_NVGcolor(c));
			nvgFill(gfx->vg);
		}
		draw_status_line(gfx, &g_drawit.status_line);
	nvgEndFrame(gfx->vg);
}

sapp_desc sokol_main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	return (sapp_desc){
		.init_cb      = init,
		.frame_cb     = frame,
		.event_cb     = event,
		.cleanup_cb   = cleanup,
		.window_title = "Drawit",
		.icon         = {
			.sokol_default = false,
			.images = {{
				.width  = 32,
				.height = 32,
				.pixels = SAPP_RANGE(APP_ICON_32x32),
			}}
		},
		.swap_interval = 1,
	};
}

