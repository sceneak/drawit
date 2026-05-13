#define GLAD_GL_IMPLEMENTATION
#include <glad2/gl.h>

#include <sokol/sokol_app.h>
#include <sokol/sokol_time.h>

#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg/nanovg.h>
#include <nanovg/nanovg_gl.h>

#define PFH_IMPLEMENTATION
#include <perfect-freehand/pfh.h>

#include <math.h>
#include <stdint.h>

#define CMD_HIST_MAX 256
#define STROKE_BOUNDS_MARGIN 5
#define MAX_SPEED_INCH 1200 /* will mult. by dpi_scale */
#define STATUS_MAX 512

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define COLOR_INIT_HEX(hex) {             \
               .r = ((hex) >> 24) & 0xFF, \
               .g = ((hex) >> 16) & 0xFF, \
               .b = ((hex) >> 8) & 0xFF,  \
               .a =  (hex) & 0xFF,        \
        }
#define COLOR_FROM_HEX(hex) (color) COLOR_INIT_HEX(hex)

typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3;
typedef struct { unsigned char r, g, b, a; } color;
typedef struct { float x0, y0, x1, y1; } rect;

typedef struct { vec2 coord; float pressure; } point;

enum theme {
	THEME_DARK,
	THEME_LIGHT,
	THEME_MAX,
};

#define T point
#define name da_point
#include "da.t.h"

struct stroke_desc {
	size_t       input_idx,
	             input_count;
	size_t       vertex_idx,
	             vertex_count;
	rect         bounds;
	const color *colors;
	bool         deleted;
};

#define T struct stroke_desc
#define name da_stroke_desc
#include "da.t.h"

struct stroke_ctx {
	struct da_point       *input_da;
	pfh_vec2_buf           pfh_vertex_buf;
	struct da_stroke_desc *desc_da;
};

struct text_obj {
	struct gapbuf buf;
};

#define T struct text_obj
#define name da_text_obj
#include "da.t.h"

struct text_ctx {
	struct da_text_obj *text_da;
};

struct canvas {
	struct stroke_ctx stroke_ctx;
	struct text_ctx text_ctx;
};

enum cmd_type {
	CMD_NONE,
	CMD_STROKE_CREATE,
	CMD_STROKE_DELETE,
};

struct cmd {
	enum cmd_type type;
	union {
		struct cmd_stroke_data {
			int idx;
			struct stroke_ctx *stroke_ctx;
			struct da_point *point_da; 
			const color *colors;
		} stroke;
	} v;
};

struct cmd_hist {
	long before_first;
	long last;
	long cursor;
	struct cmd cmds[CMD_HIST_MAX];
};

enum input_state {
	INPUT_STATE_DRAWING,
	INPUT_STATE_COMMAND,
	INPUT_STATE_TEXT,
};


#define T struct cmd
#define name da_cmd
#include "da.t.h"

#define BOUNDS_INIT_DEFAULT {    \
                .x0 = INFINITY,  \
                .y0 = INFINITY,  \
                .x1 = -INFINITY, \
                .y1 = -INFINITY, \
        }

/************ GLOBALS & STATE ************/

#include "gen/inconsolata_ttf.h"

static const uint8_t APP_ICON_32x32[] = {
#ifndef __INTELLISENSE__
#include "gen/icon32x32.inc"
#else
0
#endif
};


static float dpi_scale; /* needs to be cached due to sokol wackyness */
static int screen_width, screen_height;
static NVGcontext *vg;
static int font_handle;

static const color COLOR_DEEP_CHARCOAL = COLOR_INIT_HEX(0x121212FF);
static const color COLOR_SKIN = COLOR_INIT_HEX(0xF5E1D2FF);
static const color COLOR_SCENE = COLOR_INIT_HEX(0xCCFF00FF);
static const color COLOR_MARIGOLD = COLOR_INIT_HEX(0xD97706FF);
static const color COLOR_HOTPINK = COLOR_INIT_HEX(0xFF69B4FF);
static const color COLOR_RASPBERRY = COLOR_INIT_HEX(0xD81B60FF);
static const color COLOR_TURQUOISE = COLOR_INIT_HEX(0x40E0D0FF);
static const color COLOR_TEAL = COLOR_INIT_HEX(0x009688FF);

static enum theme theme = THEME_DARK;
static const color *clear_colors;

static vec2 mouse_screen;
static vec2 mouse_world;
static bool mouse_in_frame = true;
static vec2 camera = {0, 0};
static float zoom_frac = 0.1f;
static float zoom = 1.0f;

static enum input_state input_state = INPUT_STATE_DRAWING;


static char status_line[STATUS_MAX];
static size_t status_line_len = 0;


static const color COLORS_BACKGROUND[THEME_MAX] = { COLOR_DEEP_CHARCOAL, COLOR_SKIN };
static const color COLORS_CONTRAST[THEME_MAX] = { COLOR_SKIN, COLOR_DEEP_CHARCOAL };
static const color COLORS_YELLOW[THEME_MAX] = { COLOR_SCENE, COLOR_MARIGOLD };
static const color COLORS_BLUE[THEME_MAX] = { COLOR_TURQUOISE, COLOR_TEAL };
static const color COLORS_RED[THEME_MAX] = { COLOR_HOTPINK, COLOR_RASPBERRY };

static const color **active_stroke_colors;
static const color *stroke_colors_primary;
static const color *stroke_colors_secondary;

static bool is_panning = false;
static vec2 pan_pivot_mouse;
static vec2 pan_pivot_camera;

static bool draw_closest_stroke_bounds = false;
static bool is_drawing_stroke = false;
static bool is_deleting_stroke = false;
static struct canvas curr_canvas;

static struct cmd cmd_curr;
static struct cmd_hist cmd_hist;
static size_t cmd_save_idx = 0;

static const pfh_stroke_opts STROKE_OPTS = {
	.size = 16,
	.thinning = .55,
	.streamline = .45,
	.smoothing = .55,
	.easing = NULL,
	.simulate_pressure = false,
	.is_complete = false,
	.start = {
		.cap = true,
		.taper = PFH_TAPER_NONE,
		.easing = NULL,
	},
	.end = {
		.cap = true,
		.taper = PFH_TAPER_NONE,
		.easing = NULL,
	},
	.last = false,
};

/************ HELPERS ************/

static inline vec2 vec2_all(float s)
{
	return (vec2){s, s};
}

static inline float vec2_dist2(vec2 from, vec2 to)
{
	vec2 dist = { to.x - from.x, to.y - from.y };
	return dist.x * dist.x + dist.y * dist.y; 
}

static inline vec2 screen_to_world(vec2 screen)
{
	return (vec2){
		.x = ( (screen.x - screen_width/2) / zoom) + camera.x,
		.y = ( (screen_height/2 - screen.y) / zoom) + camera.y,
	};
}

static inline rect rect_create(vec2 center, vec2 extents)
{
	return (rect){
		.x0 = center.x - extents.x,
		.y0 = center.y - extents.y,
		.x1 = center.x + extents.x,
		.y1 = center.y + extents.y,
	};
}

static inline rect rect_fit(rect r, vec2 v)
{
	return (rect){
		.x0 = fminf(r.x0, v.x),
		.y0 = fminf(r.y0, v.y),
		.x1 = fmaxf(r.x1, v.x),
		.y1 = fmaxf(r.y1, v.y),
	};
}

static inline rect rect_fit_rect(rect r1, rect r2)
{
	return (rect){
		.x0 = fminf(r1.x0, r2.x0),
		.y0 = fminf(r1.y0, r2.y0),
		.x1 = fmaxf(r1.x1, r2.x1),
		.y1 = fmaxf(r1.y1, r2.y1),
	};
}

static inline bool rect_contains(rect r, vec2 v)
{
	return r.x0 < v.x && v.x < r.x1
	    && r.y0 < v.y && v.y < r.y1;
}

static inline void status_line_set(const char *str)
{
	size_t len = snprintf(status_line, ARRAY_SIZE(status_line), "%s", str);
	status_line_len = min(len, ARRAY_SIZE(status_line)-1);
}

struct canvas canvas_create_empty()
{
	struct stroke_ctx stroke_ctx = {
		.desc_da = da_stroke_desc_create(DA_INITIAL_CAPACITY),
		.input_da = da_point_create(DA_INITIAL_CAPACITY),
	};
	pfh_vec2_buf_init(&stroke_ctx.pfh_vertex_buf, DA_INITIAL_CAPACITY);

	return (struct canvas) {
		stroke_ctx
	};
}


/************ CANVAS: STROKE ************/

void stroke_ctx_print(const struct stroke_ctx *ctx) 
{
	size_t i;
	struct stroke_desc *s;

	printf("inputs (%zu)\n", ctx->input_da->count);
	printf("vertices (%zu)\n", ctx->pfh_vertex_buf.count);
	puts("desc_da:");
	for (i = 0; i < ctx->desc_da->count; i++) {
		s = ctx->desc_da->elems + i;
		printf("stroke_desc[%zu]\n", i);
		printf("  input: idx %zu, count %zu\n", s->input_idx, s->input_count);
		printf("  vertex: idx %zu, count %zu\n", s->vertex_idx, s->vertex_count);
		printf("  colors[%d]: (%d, %d, %d, %d)\n", theme, s->colors[theme].r, s->colors[theme].g, s->colors[theme].b, s->colors[theme].a);
	}
	puts("");
}

void stroke_ctx_begin(struct stroke_ctx *ctx, const color *colors)
{
	ctx->desc_da = da_stroke_desc_append(ctx->desc_da, (struct stroke_desc) { 
		.input_idx    = ctx->input_da->count, 
		.input_count  = 0,
		.vertex_idx   = ctx->pfh_vertex_buf.count, 
		.vertex_count = 0,
		.colors       = colors,
		.bounds       = BOUNDS_INIT_DEFAULT,
		.deleted      = false,
	});
}

void stroke_ctx_render_last(struct stroke_ctx *ctx)
{
	struct stroke_desc *s = DA_LAST(ctx->desc_da);

	ctx->pfh_vertex_buf.count = s->vertex_idx;
	pfh_get_stroke(
		&ctx->pfh_vertex_buf,
		(pfh_point*)ctx->input_da->elems + s->input_idx,
		s->input_count,
		&STROKE_OPTS
	);
	s->vertex_count = ctx->pfh_vertex_buf.count - s->vertex_idx;
}

void stroke_ctx_append_point(struct stroke_ctx *ctx, point pt)
{
	struct stroke_desc *s = DA_LAST(ctx->desc_da);
	const vec2 PT_EXTENTS = vec2_all(STROKE_OPTS.size/2 + STROKE_BOUNDS_MARGIN);

	ctx->input_da = da_point_append(ctx->input_da, pt);
	s->input_count++;
	s->bounds = rect_fit_rect(s->bounds, rect_create(pt.coord, PT_EXTENTS));

	stroke_ctx_render_last(ctx);
}

void stroke_ctx_append_points(struct stroke_ctx *ctx, point pts[], int count)
{
	int i;
	struct stroke_desc *s = DA_LAST(ctx->desc_da);
	const vec2 PT_EXTENTS = vec2_all(STROKE_OPTS.size/2 + STROKE_BOUNDS_MARGIN);

	ctx->input_da = da_point_append_n(ctx->input_da, pts, count);
	s->input_count += count;
	for (i = 0; i < count; i++)
		s->bounds = rect_fit_rect(s->bounds, rect_create(pts[i].coord, PT_EXTENTS));

	stroke_ctx_render_last(ctx);
}

void stroke_ctx_mark_delete(struct stroke_ctx *ctx, int stroke_idx, bool deleted)
{
	ctx->desc_da->elems[stroke_idx].deleted = deleted;
}

void stroke_ctx_delete_last(struct stroke_ctx *ctx)
{
	struct stroke_desc *s = DA_LAST(ctx->desc_da);

	ctx->input_da->count = s->input_idx;
	ctx->pfh_vertex_buf.count = s->vertex_idx;
	ctx->desc_da->count--;
}

float stroke_ctx_dist(const struct stroke_ctx *ctx, int stroke_idx, vec2 v)
{
	const struct stroke_desc *s = ctx->desc_da->elems + stroke_idx;
	const point *s_inputs = ctx->input_da->elems + s->input_idx;
	float closest_dist2 = FLT_MAX;
	float dist2;
	size_t i;

	for (i = 0; i < s->input_count; i++) {
		dist2 = vec2_dist2(v, s_inputs[i].coord);
		if (dist2 < closest_dist2)
			closest_dist2 = dist2;
	}
	return closest_dist2;
}

int stroke_ctx_closest(const struct stroke_ctx *ctx, vec2 v)
{
	size_t closest_stroke_idx = -1;
	float closest_dist2 = FLT_MAX;
	float dist2;
	size_t i;

	for (i = 0; i < ctx->desc_da->count; i++) {
		if (ctx->desc_da->elems[i].deleted)
			continue;
		if (!rect_contains(ctx->desc_da->elems[i].bounds, v))
			continue;
		dist2 = stroke_ctx_dist(ctx, i, v);
		if (dist2 >= closest_dist2)
			continue;
		closest_dist2 = dist2;
		closest_stroke_idx = i;
	}
	return closest_stroke_idx;
}

/************ CANVAS: TEXT ************/

void text_ctx_print(const struct text_ctx *ctx);
void text_ctx_insert(struct text_ctx *ctx, const color *colors);
void text_ctx_render(struct text_ctx *ctx);
void text_ctx_edit(struct text_ctx *ctx, point pt);
void text_ctx_delete(struct text_ctx *ctx, int text_idx, bool deleted);
float text_ctx_dist(const struct text_ctx *ctx, int text_idx, vec2 v);
int text_ctx_closest_idx(const struct text_ctx *ctx, vec2 v);

/************ COMMAND ************/

static void cmd_hist_forget(struct cmd *cmd)
{
	switch(cmd->type) {
	case CMD_STROKE_DELETE:
		break;
	case CMD_STROKE_CREATE:
		free(cmd->v.stroke.point_da);
		break;
	default: break;
	}
	cmd->type = CMD_NONE;
}

static void cmd_hist_record(struct cmd cmd)
{
	cmd_hist.cursor = RINGBUF_INCR(cmd_hist.cursor, ARRAY_SIZE(cmd_hist.cmds), 1);
	cmd_hist_forget(cmd_hist.cmds + cmd_hist.cursor);
	cmd_hist.cmds[cmd_hist.cursor] = cmd;

	cmd_hist.last = cmd_hist.cursor;

	if (cmd_hist.before_first == cmd_hist.last)
		cmd_hist.before_first = RINGBUF_INCR(cmd_hist.before_first, ARRAY_SIZE(cmd_hist.cmds), 1);
}

static void cmd_stroke_create(struct cmd cmd)
{
	stroke_ctx_begin(cmd.v.stroke.stroke_ctx, cmd.v.stroke.colors);
	stroke_ctx_append_points(
		cmd.v.stroke.stroke_ctx,
		cmd.v.stroke.point_da->elems,
		cmd.v.stroke.point_da->count
	);
}

static void cmd_hist_undo(void)
{
	struct cmd cmd;

	if (cmd_hist.cursor == cmd_hist.before_first) {
		status_line_set("Already at oldest history");
		return;
	}

	cmd = cmd_hist.cmds[cmd_hist.cursor];
	cmd_hist.cursor = RINGBUF_DECR(cmd_hist.cursor, ARRAY_SIZE(cmd_hist.cmds), 1);

	switch(cmd.type) {
	case CMD_STROKE_DELETE:
		stroke_ctx_mark_delete(
			cmd.v.stroke.stroke_ctx,
			cmd.v.stroke.idx,
			false
		);
		break;
	case CMD_STROKE_CREATE:
		stroke_ctx_delete_last(&curr_canvas.stroke_ctx);
		break;
	default: break;
	}
}

static void cmd_hist_redo(void)
{
	struct cmd cmd;

	if (cmd_hist.cursor == cmd_hist.last) {
		status_line_set("Already at newest change");
		return;
	}

	cmd_hist.cursor = RINGBUF_INCR(cmd_hist.cursor, ARRAY_SIZE(cmd_hist.cmds), 1);
	cmd = cmd_hist.cmds[cmd_hist.cursor];

	switch(cmd.type) {
	case CMD_STROKE_DELETE:
		stroke_ctx_mark_delete(
			cmd.v.stroke.stroke_ctx,
			cmd.v.stroke.idx,
			true
		);
		break;
	case CMD_STROKE_CREATE:
		cmd_stroke_create(cmd);
		break;
	default: break;
	}
}

/************ DRAWING ************/

void drawing_mouse_down(const sapp_event *e, point pt)
{
	if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT || e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
		stroke_ctx_begin(&curr_canvas.stroke_ctx, *active_stroke_colors);
		is_drawing_stroke = true;

		if (cmd_curr.type != CMD_NONE)
			puts("Warning: something ain't right. cmd_curr is not NONE.");
		cmd_curr.type = CMD_STROKE_CREATE;
		cmd_curr.v.stroke.stroke_ctx = &curr_canvas.stroke_ctx;

		cmd_curr.v.stroke.idx = curr_canvas.stroke_ctx.desc_da->count-1;
		cmd_curr.v.stroke.colors = *active_stroke_colors;

		cmd_curr.v.stroke.point_da = da_point_create(DA_INITIAL_CAPACITY);

		stroke_ctx_append_point(&curr_canvas.stroke_ctx, pt);
		cmd_curr.v.stroke.point_da = da_point_append(cmd_curr.v.stroke.point_da, pt);
	}
}

void drawing_mouse_move(point pt)
{
	const int MIN_PX = 2;
	static point last_pt = { .coord = { FLT_MAX, FLT_MAX } };

	if (is_drawing_stroke) {
		if (vec2_dist2(pt.coord, last_pt.coord) < MIN_PX*MIN_PX)
			return;
		last_pt = pt;

		if (cmd_curr.type != CMD_STROKE_CREATE)
			puts("Warning: something ain't right. cmd_curr is not STROKE_CREATE.");
		stroke_ctx_append_point(&curr_canvas.stroke_ctx, pt);
		cmd_curr.v.stroke.point_da = da_point_append(cmd_curr.v.stroke.point_da, pt);
	}
}

void drawing_mouse_up()
{
	if (is_drawing_stroke) {
		is_drawing_stroke = false;
		cmd_hist_record(cmd_curr);
		cmd_curr.type = CMD_NONE;
	}
}

void command_exec(const char *str)
{
	if (str[0] == ':') {
		str++;
		if (drawit_strcasecmp(str, "light") == 0 || (str[0] == 'l')) {
			theme = THEME_LIGHT;
			status_line_set("theme=light");
		} else if (drawit_strcasecmp(str, "dark") == 0 || (str[0] == 'd')) {
			theme = THEME_DARK;
			status_line_set("theme=dark");
		} else if (drawit_strcasecmp(str, "quit") == 0 || (str[0] == 'q')) {
			sapp_request_quit();
		} else if (drawit_strcasecmp(str, "print") == 0 || (str[0] == 'p')) {
			stroke_ctx_print(&curr_canvas.stroke_ctx);
			status_line_set("debug print stroke ctx to stdout");
		} else {
			status_line_set("unknown command");
		}
	}
}

/************ SOKOL APP ************/

void input_state_set_drawing(void)
{
	input_state = INPUT_STATE_DRAWING;
	sapp_show_mouse(false);
}
void input_state_set_command(void)
{
	input_state = INPUT_STATE_COMMAND;
	sapp_show_mouse(true);
}

void init(void) 
{
	stm_setup();

	curr_canvas = canvas_create_empty();

	clear_colors = COLORS_BACKGROUND;
	stroke_colors_primary = COLORS_YELLOW;
	stroke_colors_secondary = COLORS_RED;
	active_stroke_colors = &stroke_colors_primary;

	screen_width = sapp_width();
	screen_height = sapp_height();
	gladLoaderLoadGL();
	vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

	font_handle = nvgCreateFontMem(vg, "Inconsolata-Regular-Sub", (uint8_t *)inconsolata_ttf, inconsolata_ttf_len, 0);

	input_state_set_drawing();

#ifdef _WIN32
	#include <windows.h>
	HWND hwnd = (HWND)sapp_win32_get_hwnd();
	ShowWindow(hwnd, SW_MAXIMIZE);
#endif
}

void cleanup(void)
{
	gladLoaderUnloadGL();
	nvgDeleteGL3(vg);
}

void event_drawing(const sapp_event *e)
{
	static point last_pt = { .coord = { FLT_MAX, FLT_MAX }, .pressure = .5 };
	static uint64_t last_move = 0;

	point pt;
	double delta, vel;

	if (input_state != INPUT_STATE_DRAWING)
		return;

	if (status_line_len && (e->type == SAPP_EVENTTYPE_MOUSE_DOWN || e->type == SAPP_EVENTTYPE_KEY_DOWN))
		status_line_len = 0;

	switch(e->type) {
	case SAPP_EVENTTYPE_MOUSE_ENTER:
		mouse_in_frame = true;
		break;
	case SAPP_EVENTTYPE_MOUSE_LEAVE:
		mouse_in_frame = false;
		break;
	case SAPP_EVENTTYPE_MOUSE_MOVE:
	case SAPP_EVENTTYPE_MOUSE_DOWN:
	case SAPP_EVENTTYPE_MOUSE_UP:
		delta = stm_sec(stm_laptime(&last_move));
		pt.coord = screen_to_world( (vec2){ e->mouse_x, e->mouse_y } );

		vel = ( sqrtf(vec2_dist2(pt.coord, last_pt.coord)) / delta );
		pt.pressure = 1 - min(vel / (MAX_SPEED_INCH*dpi_scale), 1);
		pt.pressure = last_pt.pressure * .75 + pt.pressure * .25; /* blend */

		/* printf("raw (%f, %f), world (%f, %f), pressure (%f) \n", e->mouse_x, e->mouse_y, pt.coord.x, pt.coord.y, pt.pressure); */
		last_pt = pt;
		break;
	default: break;
	}

	switch(e->type) {
	case SAPP_EVENTTYPE_CHAR: 
		if (e->char_code == ':') {
			input_state_set_command();
			break;
		}
		break;
	case SAPP_EVENTTYPE_KEY_DOWN:
		if (e->key_code == SAPP_KEYCODE_X) {
			is_deleting_stroke = true;
			draw_closest_stroke_bounds = true;
		}

		if (e->key_code == SAPP_KEYCODE_A) {
		}

		if (e->key_code == SAPP_KEYCODE_M) {
			cmd_save_idx = cmd_hist.cursor;
		}
		if (e->key_code == SAPP_KEYCODE_APOSTROPHE) {
			while (cmd_hist.cursor != cmd_save_idx) {
				/* DANGER */
				/* TODO: Need to figure out forward or backwards, else infinte loop */
				cmd_hist_undo(); 
			}
		}
		if (e->key_code == SAPP_KEYCODE_EQUAL) {
			while (cmd_hist.cursor != cmd_hist.last) {
				cmd_hist_redo();
			}
		}

		/* TODO: These can screw things up if cmd_curr is ongoing */
		if ((e->modifiers & SAPP_MODIFIER_CTRL) && e->key_code == SAPP_KEYCODE_Z)
			cmd_hist_undo();
		if ((e->modifiers & SAPP_MODIFIER_CTRL) && e->key_code == SAPP_KEYCODE_R)
			cmd_hist_redo();

		active_stroke_colors = &stroke_colors_primary;
		if (e->modifiers & SAPP_MODIFIER_ALT)
			active_stroke_colors = &stroke_colors_secondary;

		switch (e->key_code) {
		case SAPP_KEYCODE_1:
			*active_stroke_colors = COLORS_YELLOW;
			break;
		case SAPP_KEYCODE_2:
			*active_stroke_colors = COLORS_RED;
			break;
		case SAPP_KEYCODE_3:
			*active_stroke_colors = COLORS_BLUE;
			break;
		case SAPP_KEYCODE_4:
			*active_stroke_colors = COLORS_CONTRAST;
			break;
		default: break;
		}

		active_stroke_colors = &stroke_colors_primary;
		break;
	case SAPP_EVENTTYPE_KEY_UP:
		if (e->key_code == SAPP_KEYCODE_X) {
			is_deleting_stroke = false;
			draw_closest_stroke_bounds = false;
			drawing_mouse_up(); /* no weird shenanigans mid draw */

			if (cmd_curr.type != CMD_NONE)
				puts("Warning: Something ain't right. cmd_curr is not NONE.");

			cmd_curr.type = CMD_STROKE_DELETE;
			cmd_curr.v.stroke.stroke_ctx = &curr_canvas.stroke_ctx;
			cmd_curr.v.stroke.idx = stroke_ctx_closest(&curr_canvas.stroke_ctx, mouse_world);
			if (cmd_curr.v.stroke.idx < 0) {
				cmd_curr.type = CMD_NONE;
				break;
			}
			stroke_ctx_mark_delete(
				&curr_canvas.stroke_ctx, 
				cmd_curr.v.stroke.idx,
				true
			);
			cmd_hist_record(cmd_curr);
			cmd_curr.type = CMD_NONE;
		}

		break;
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		mouse_screen.x = e->mouse_x;
		mouse_screen.y = e->mouse_y;
		mouse_world = screen_to_world(mouse_screen);

		if (is_panning) {
			camera.x = pan_pivot_camera.x + (pan_pivot_mouse.x - mouse_screen.x)/zoom;
			camera.y = pan_pivot_camera.y + (mouse_screen.y - pan_pivot_mouse.y)/zoom;
		}
		drawing_mouse_move(pt);
		break;
	case SAPP_EVENTTYPE_MOUSE_DOWN:
		if (e->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
			is_panning = true;
			pan_pivot_mouse = mouse_screen;
			pan_pivot_camera = camera;
		}

		if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT)
			active_stroke_colors = &stroke_colors_primary;
		else if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT)
			active_stroke_colors = &stroke_colors_secondary;

		drawing_mouse_down(e, pt);
		break;
	case SAPP_EVENTTYPE_MOUSE_UP:
		is_panning = is_panning && e->mouse_button != SAPP_MOUSEBUTTON_MIDDLE;
		drawing_mouse_up();
		break;
	case SAPP_EVENTTYPE_MOUSE_SCROLL:
		float ratio = (1 + zoom_frac * e->scroll_y);
		zoom *= ratio;
		
		// (World - OldCamera) * OldZoom = (World - NewCamera) * NewZoom
		camera.x = mouse_world.x - (mouse_world.x - camera.x) / ratio;
		camera.y = mouse_world.y - (mouse_world.y - camera.y) / ratio;
		break;
	default: break;
	}
}

/* Note event_command relies on event_drawing to zero out status line when entering state */
void event_command(const sapp_event *e)
{
	if (input_state != INPUT_STATE_COMMAND)
		return;

	if (e->type == SAPP_EVENTTYPE_CHAR && status_line_len < STATUS_MAX-1) {
		status_line[status_line_len++] = e->char_code;
		return;
	}
	if (e->type != SAPP_EVENTTYPE_KEY_DOWN)
		return;

	switch(e->key_code) {
	case SAPP_KEYCODE_BACKSPACE:
		if (status_line_len <= 1)
			break;

		status_line_len--;
		break;
	case SAPP_KEYCODE_ENTER:
		status_line[status_line_len] = '\0';
		command_exec(status_line); /* status line is reused to write results. don't zero */

		input_state_set_drawing();
		break;
	case SAPP_KEYCODE_ESCAPE:
		input_state_set_drawing();
		status_line_len = 0;
		break;
	default: break;
	}
}

void event(const sapp_event *e)
{
	switch(e->type) {
	case SAPP_EVENTTYPE_RESIZED:
		screen_width = sapp_width();
		screen_height = sapp_height();
		break;
	default: break;
	}

	event_drawing(e);
	event_command(e);
}

void draw_rect(rect r)
{
	nvgBeginPath(vg);
		nvgRect(vg, r.x0, r.y0, r.x1-r.x0, r.y1-r.y0);
	nvgStrokeColor(vg, nvgRGBA(0, 255, 0, 255));
	nvgStrokeWidth(vg, 2.0f);
	nvgStroke(vg);
}

void draw_stroke_ctx(struct stroke_ctx *ctx)
{
	size_t i, j;
	struct stroke_desc *s;
	pfh_vec2 *s_vertices;
	pfh_vec2 p0, p1;
	color c;
	int tmp;

	for (i = 0; i < ctx->desc_da->count; i++) {
		s = ctx->desc_da->elems + i;
		if (s->deleted)
			continue;
		if (s->vertex_count < 3) /* shouldn't happen, a dot is 13 segs */
			continue;

		s_vertices = ctx->pfh_vertex_buf.elems + s->vertex_idx;
		c = s->colors[theme];

		nvgBeginPath(vg);
		nvgFillColor(vg, nvgRGBA(c.r, c.g, c.b, c.a));

		p0 = s_vertices[s->vertex_count-1];
		p1 = s_vertices[0];
		nvgMoveTo(vg, (p0.x + p1.x) / 2, (p0.y + p1.y) / 2);

		for (j = 0; j < s->vertex_count; j++) {
			p0 = s_vertices[j];
			p1 = (j+1) == s->vertex_count
				? s_vertices[0]
				: s_vertices[j + 1];
			nvgQuadTo(vg, p0.x, p0.y, (p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
		}
		nvgFill(vg);
	}
	if (draw_closest_stroke_bounds) {
		tmp = stroke_ctx_closest(ctx, mouse_world);
		if (tmp == -1)
			return;
		draw_rect(ctx->desc_da->elems[tmp].bounds);
	}
}

void draw_status_line(void)
{
	const float FONT_SIZE = 26.0;
	color c = COLORS_CONTRAST[theme];

	if (!status_line_len)
		return;

	nvgFontSize(vg, FONT_SIZE);
	nvgFontFaceId(vg, font_handle);
	nvgFillColor(vg, nvgRGBA(c.r, c.g, c.b, c.a));
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgText(vg, 0 + FONT_SIZE, screen_height - FONT_SIZE, status_line, status_line + status_line_len);
}

void frame(void) 
{
	color c;

	dpi_scale = sapp_dpi_scale();

	glViewport(0, 0, screen_width, screen_height);
	glClearColor(clear_colors[theme].r/255.0f, clear_colors[theme].g/255.0f, clear_colors[theme].b/255.0f, clear_colors[theme].a/255.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	nvgBeginFrame(vg, screen_width/dpi_scale, screen_height/dpi_scale, dpi_scale);

	nvgSave(vg);
	nvgTranslate(vg, screen_width/2, screen_height/2);
	nvgScale(vg, zoom, -zoom);
	nvgTranslate(vg, -camera.x, -camera.y);
		draw_stroke_ctx(&curr_canvas.stroke_ctx);
	nvgRestore(vg);
		if (input_state == INPUT_STATE_DRAWING && mouse_in_frame) {
			nvgBeginPath(vg);
				nvgCircle(vg, roundf(mouse_screen.x), round(mouse_screen.y), zoom*STROKE_OPTS.size/1.5);
			c = (*active_stroke_colors)[theme];
			nvgFillColor(vg, nvgRGBA(c.r, c.g, c.b, c.a/1.5));
			nvgFill(vg);
		}
		draw_status_line();
	nvgEndFrame(vg);
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
	};
}
