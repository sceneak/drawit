#define GLAD_GL_IMPLEMENTATION
#include <glad2/gl.h>

#include <sokol/sokol_app.h> 
#include <sokol/sokol_time.h>

#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg/nanovg.h>
#include <nanovg/nanovg_gl.h>

#define PFH_IMPLEMENTATION
#include <perfect-freehand/pfh.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "types.h"

#define CANVAS_HIST_MAX 256
#define STROKE_BOUNDS_MARGIN 5
#define VELOCITY_MAX_IN 1200 /* will mult. by screen_dpi_scale (inch) */
#define STATUS_LINE_MAX 512
#define PEN_PT_INIT { .coord = { FLT_MAX, FLT_MAX }, .pressure = .5 }
#define FONT_SIZE_DEFAULT 64.0


/* ==============================
 * CORE DATA STRUCTURES 
 * ==============================
 */
struct pen_pt {
	vec2 coord;
	float pressure;
};

#define T struct pen_pt
#define NAME da_pen_pt
#include "da.t.h"

enum object_kind {
	OBJECT_KIND_NONE,
	OBJECT_KIND_GROUP,
	OBJECT_KIND_STROKE,
	OBJECT_KIND_TEXT,
};

struct object_data {
	vec2   transl;
	rect   bounds;
	bool   deleted;
};

struct object_handle {
	enum object_kind kind;
	int              id;
	/* int           gen; */
};

#define T struct object_handle
#define NAME da_object_handle
#include "da.t.h"

struct group {
	struct object_data       obj;
	struct object_handle    *members;
	size_t                   member_count;
};

#define T struct group
#define NAME da_group
#include "da.t.h"

struct group_store {
	struct da_group *group_da;
	size_t           group_max_count;
};

struct stroke_desc {
	struct object_data obj;

	size_t        input_idx,
	              input_count;
	size_t        vertex_idx,
	              vertex_count;
	const color  *colors;
};

#define T struct stroke_desc
#define NAME da_stroke_desc
#include "da.t.h"

struct stroke_store {
	struct da_pen_pt      *input_da;
	pfh_vec2_buf           pfh_vertex_buf;
	struct da_stroke_desc *desc_da;
	size_t                 desc_max_count;
};

struct text_obj {
	struct object_data obj;

	int            font_handle;
	float          font_size;
	float          line_height;
	const color   *colors;
	struct gapbuf *buf;
	char           bounds_dirty;
};

#define T struct text_obj
#define NAME da_text_obj
#include "da.t.h"

struct text_store {
	struct da_text_obj *text_da;
	size_t text_max_count;
};

struct canvas {
	struct stroke_store stroke_store;
	struct text_store   text_store;
	struct group_store  group_store;
};

struct canvas_checkpoint {
	size_t text_count,
	       stroke_count,
	       group_count;
};

enum cmd_kind {
	CMD_NONE,
	CMD_OBJECT_DELETE,
	CMD_OBJECT_DRAG,
	CMD_OBJECT_GROUP,
	CMD_GROUP_DELETE,

	CMD_STROKE_CREATE,
	CMD_TEXT_CREATE,
};

struct cmd {
	enum cmd_kind kind;
	union ctx {
		struct object_handle obj;
		struct {
			struct object_handle *hndls;
			size_t count;
		} objs;
		struct {
			struct object_handle *hndls;
			size_t count;
			/* flt driftnya tb, daripada track start-end for each obj */
			vec2 delta;
		} drag;
	} v;
};

struct canvas_hist {
	struct canvas *canvas;

	long before_first;
	long last;
	long cursor;
	struct cmd cmds[CANVAS_HIST_MAX];
};

#define T struct cmd
#define NAME da_cmd
#include "da.t.h"

/* APP CTX STRUCTS **/
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
	const color *clear_colors;
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
	MODE_EDIT,
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
	char buf[STATUS_LINE_MAX];
	size_t len;
};

enum select_kind {
	SELECT_NONE,
	SELECT_NEAREST,
	/* SELECT_BOX, */
};

struct drawit_ctx {
	struct graphic_ctx       gfx;

	enum theme               theme;
	enum mode                mode;

	struct status_line       status_line;

	struct pen               pen;

	enum select_kind         select_kind;
	struct da_object_handle *selection_da;

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

static const color COLORS_BACKGROUND[THEME_MAX] = { COLOR_DEEP_CHARCOAL, COLOR_SKIN };
static const color COLORS_CONTRAST[THEME_MAX] = { COLOR_SKIN, COLOR_DEEP_CHARCOAL };
static const color COLORS_YELLOW[THEME_MAX] = { COLOR_SCENE, COLOR_MARIGOLD };
static const color COLORS_BLUE[THEME_MAX] = { COLOR_TURQUOISE, COLOR_TEAL };
static const color COLORS_RED[THEME_MAX] = { COLOR_HOTPINK, COLOR_RASPBERRY };

static const pfh_stroke_opts STROKE_OPTS = {
	.size = 16,
	.thinning = .55,
	.streamline = .45,
	.smoothing = .55,
	.easing = NULL,
	.simulate_pressure = false,
	.is_complete = false,
	.start = { .cap = true, .taper = PFH_TAPER_NONE, .easing = NULL, },
	.end = { .cap = true, .taper = PFH_TAPER_NONE, .easing = NULL, },
	.last = false,
};

static struct drawit_ctx g_drawit = {
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

static inline void status_line_set(struct status_line *s, const char *str)
{
	s->len = snprintf(s->buf, ARRAY_SIZE(s->buf), "%s", str);
	s->len = min(s->len, ARRAY_SIZE(s->buf)-1);
}

/* ==============================
 * CANVAS
 * ==============================
 */
struct canvas canvas_create_empty()
{
	struct stroke_store stroke_store = { 0 };
	struct text_store text_store = { 0 };
	struct group_store group_store = { 0 };

	da_stroke_desc_create(&stroke_store.desc_da, DA_INITIAL_CAPACITY);
	da_pen_pt_create(&stroke_store.input_da, DA_INITIAL_CAPACITY);

	da_text_obj_create(&text_store.text_da, DA_INITIAL_CAPACITY);

	da_group_create(&group_store.group_da, DA_INITIAL_CAPACITY);

	pfh_vec2_buf_init(&stroke_store.pfh_vertex_buf, DA_INITIAL_CAPACITY);

	return (struct canvas) { stroke_store, text_store, group_store };
}

/* purely for the pending groups feature */
struct canvas_checkpoint canvas_checkpoint_make(const struct canvas *canvas)
{
	struct canvas_checkpoint chk;
	chk.stroke_count = canvas->stroke_store.desc_da->count;
	chk.text_count = canvas->text_store.text_da->count;
	chk.group_count = canvas->group_store.group_da->count;
	return chk;
}

int canvas_objects_created_after(const struct canvas *canvas,
                                 struct canvas_checkpoint chk,
                                 struct object_handle **out_handles,
                                 size_t *out_count)
{
	const struct da_stroke_desc *stroke_desc_da = canvas->stroke_store.desc_da;
	const struct da_text_obj *text_da = canvas->text_store.text_da;
	const struct da_group *group_da = canvas->group_store.group_da;

	int dstroke = stroke_desc_da->count - chk.stroke_count;
	int dtext = text_da->count - chk.text_count;
	int dgroup = group_da->count - chk.group_count;

	int id;

	if (!dstroke && !dtext && !dgroup) return -1;
	if (dstroke < 0 || dtext < 0 || dgroup < 0) return -2;

	*out_handles = (struct object_handle *)malloc((dstroke + dtext + dgroup) * sizeof(struct object_handle *));
	assert(*out_handles);

	*out_count = 0;
	for (id = (int)chk.stroke_count; (size_t)id < stroke_desc_da->count; id++) {
		if (stroke_desc_da->elems[id].obj.deleted) continue;
		(*out_handles)[(*out_count)++] = (struct object_handle){ .kind = OBJECT_KIND_STROKE, .id = id };
	}
	for (id = (int)chk.text_count; (size_t)id < text_da->count; id++) {
		if (text_da->elems[id].obj.deleted) continue;
		(*out_handles)[(*out_count)++] = (struct object_handle){ .kind = OBJECT_KIND_TEXT, .id = id };
	}
	for (id = (int)chk.group_count; (size_t)id < group_da->count; id++) {
		if (group_da->elems[id].obj.deleted) continue;
		(*out_handles)[(*out_count)++] = (struct object_handle){ .kind = OBJECT_KIND_GROUP, .id = id };
	}
	
	return 0;
}

/* CANVAS: STROKES */
void stroke_store_print(const struct stroke_store *store) 
{
	size_t i;
	struct stroke_desc *s;

	printf("inputs (%zu)\n", store->input_da->count);
	printf("vertices (%zu)\n", store->pfh_vertex_buf.count);
	puts("desc_da:");
	for (i = 0; i < store->desc_da->count; i++) {
		s = store->desc_da->elems + i;
		printf("stroke_desc[%zu]\n", i);
		printf("  input: id %zu, count %zu\n", s->input_idx, s->input_count);
		printf("  vertex: id %zu, count %zu\n", s->vertex_idx, s->vertex_count);
	}
	puts("");
}

void stroke_store_begin(struct stroke_store *store, const color *colors)
{
	da_stroke_desc_append(&store->desc_da, (struct stroke_desc) { 
		.obj.bounds   = BOUNDS_INIT,

		.input_idx    = store->input_da->count, 
		.input_count  = 0,
		.vertex_idx   = store->pfh_vertex_buf.count, 
		.vertex_count = 0,
		.colors       = colors,
		.obj.deleted      = false,
	});
	store->desc_max_count = store->desc_da->count;
}

void stroke_store_render_last(struct stroke_store *store)
{
	struct stroke_desc *s = DA_LAST(store->desc_da);

	store->pfh_vertex_buf.count = s->vertex_idx;
	pfh_get_stroke(
		&store->pfh_vertex_buf,
		(pfh_point*)store->input_da->elems + s->input_idx,
		s->input_count,
		&STROKE_OPTS
	);
	s->vertex_count = store->pfh_vertex_buf.count - s->vertex_idx;
}

void stroke_store_append_point(struct stroke_store *store, struct pen_pt pt)
{
	struct stroke_desc *s = DA_LAST(store->desc_da);
	const vec2 PT_EXTENTS = vec2_all(STROKE_OPTS.size/2 + STROKE_BOUNDS_MARGIN);

	da_pen_pt_append(&store->input_da, pt);
	s->input_count++;
	s->obj.bounds = rect_fit_rect(s->obj.bounds, rect_create(pt.coord, PT_EXTENTS));

	stroke_store_render_last(store);
}

void stroke_store_append_points(struct stroke_store *store, const struct pen_pt pts[], int count)
{
	const vec2 PT_EXTENTS = vec2_all(STROKE_OPTS.size/2 + STROKE_BOUNDS_MARGIN);

	struct stroke_desc *s = DA_LAST(store->desc_da);
	int i;

	da_pen_pt_append_n(&store->input_da, pts, count);
	s->input_count += count;
	for (i = 0; i < count; i++) {
		s->obj.bounds = rect_fit_rect(s->obj.bounds, rect_create(pts[i].coord, PT_EXTENTS));
	}

	stroke_store_render_last(store);
}

void stroke_store_delete_last(struct stroke_store *store)
{
	struct stroke_desc *s = DA_LAST(store->desc_da);

	store->input_da->count = s->input_idx;
	store->pfh_vertex_buf.count = s->vertex_idx;
	store->desc_da->count--;
}

void stroke_store_restore_last(struct stroke_store *store)
{
	struct stroke_desc *s;

	assert(store->desc_max_count > store->desc_da->count);

	store->desc_da->count++;
	s = DA_LAST(store->desc_da);

	store->input_da->count = s->input_idx + s->input_count;
	store->pfh_vertex_buf.count = s->vertex_idx + s->vertex_count;
}

float stroke_store_dist(const struct stroke_store *store, int id, vec2 v)
{
	const struct stroke_desc *s = store->desc_da->elems + id;
	const struct pen_pt *s_inputs = store->input_da->elems + s->input_idx;
	float nearest_dist2 = FLT_MAX;
	float dist2;

	vec2 global_coords;
	size_t i;

	for (i = 0; i < s->input_count; i++) {
		global_coords = vec2_add(s_inputs[i].coord, s->obj.transl);
		dist2 = vec2_dist2(v, global_coords);
		if (dist2 < nearest_dist2)
			nearest_dist2 = dist2;
	}
	return nearest_dist2;
}

int stroke_store_hit_nearest(const struct stroke_store *store, vec2 v, float *dist2_out)
{
	struct stroke_desc *desc;
	rect global_bounds;

	size_t nearest_stroke_idx = -1;
	float nearest_dist2 = FLT_MAX;
	float dist2;
	size_t i;

	for (i = 0; i < store->desc_da->count; i++) {
		desc = store->desc_da->elems + i;

		if (store->desc_da->elems[i].obj.deleted)
			continue;

		global_bounds = rect_transl(desc->obj.bounds, desc->obj.transl);
		if (!rect_contains(global_bounds, v))
			continue;

		dist2 = stroke_store_dist(store, i, v);
		if (dist2 >= nearest_dist2)
			continue;

		nearest_dist2 = dist2;
		nearest_stroke_idx = i;
	}
	if (dist2_out) *dist2_out = nearest_dist2;
	return nearest_stroke_idx;
}

/* CANVAS: TEXT */
struct text_obj text_obj_create(const color *colors, int font_handle, float font_size, vec2 transl)
{
	const float LEADING_RATIO = 1.2f;

	return (struct text_obj) {
		.obj.transl = transl,
		.obj.bounds = BOUNDS_INIT,

		.font_handle = font_handle,
		.font_size = font_size,
		.line_height = font_size * LEADING_RATIO,
		.colors = colors,
		.buf = gapbuf_create(GAPBUF_INITIAL_ALLOC),
	};
}

void text_store_print(const struct text_store *store)
{
	size_t i;
	struct text_obj *t;

	puts("text_da:");
	for (i = 0; i < store->text_da->count; i++) {
		t = store->text_da->elems + i;
		printf("text[%zu]\n", i);
		printf("transl: (%f, %f)\n"
		       "  font: %d\n"
		       "  size: %f\n"
		       "gapbuf: s(%zu) e(%zu) c(%zu)\n",
			t->obj.transl.x, t->obj.transl.y,
			t->font_handle,
			t->font_size,
			t->buf->gap_start, t->buf->gap_end, t->buf->capacity
		);
	}
	puts("");
	
}

static void nvg_fontsize_ctx(NVGcontext *ctx, const struct text_obj *txt);
static void text_obj_update_bounds(struct text_obj *txt, struct graphic_ctx *gfx)
{
	const struct gapbuf *buf = txt->buf;
	size_t start = 0, i = 0;
	float x = 0.0f, max_w = 0.0f;
	float h = txt->line_height;

	nvg_fontsize_ctx(gfx->vg, txt);

	for (;;) {
		if (i >= buf->capacity) {
			x += nvgTextBounds(gfx->vg, x, 0, buf->data + start, buf->data + i, NULL);
			if (x > max_w) max_w = x;
			break;
		}
		if (i == buf->gap_start && buf->gap_start != buf->gap_end) {
			x += nvgTextBounds(gfx->vg, x, 0, buf->data + start, buf->data + i, NULL);
			start = i = buf->gap_end;
			continue;
		}
		if (buf->data[i] == '\n') {
			x += nvgTextBounds(gfx->vg, x, 0, buf->data + start, buf->data + i, NULL);
			if (x > max_w) max_w = x;

			start = i + 1;
			h += txt->line_height;
			x = 0.0f;
		}
		i++;
	}

	txt->obj.bounds = (rect) {
		.x0 = 0,     .y0 = 0,
		.x1 = max_w, .y1 = h,
	};
	txt->bounds_dirty = 0;
}

void text_obj_insert(struct text_obj *txt, unsigned char c)
{
	gapbuf_insert(&txt->buf, c);
	txt->bounds_dirty = 1;
}

void text_obj_backspace(struct text_obj *txt)
{
	gapbuf_backspace(txt->buf);
	txt->bounds_dirty = 1;
}

void text_obj_cursor(struct text_obj *txt, int dir) { gapbuf_open(txt->buf, txt->buf->gap_start+dir); }

static inline void text_store_append(struct text_store *store,
                                     const color *colors,
                                     int font_handle,
                                     float font_size,
                                     vec2 pos)
{
	size_t i;

	for (i = store->text_da->count; i < store->text_max_count; i++) {
		gapbuf_destroy(&store->text_da->elems[i].buf);
	}

	da_text_obj_append(&store->text_da, text_obj_create(colors, font_handle, font_size, pos));
	store->text_max_count = store->text_da->count;
}

int text_store_hit(const struct text_store *store, vec2 v)
{
	struct text_obj *txt;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->text_da->count; i++) {
		txt = store->text_da->elems + i;
		if (txt->obj.deleted) continue;

		global_bounds = rect_transl(txt->obj.bounds, txt->obj.transl);
		if (!rect_contains(global_bounds, v))
			continue;
		return i;
	}
	return -1;
}

void text_store_delete_last(const struct text_store *store)
{
	store->text_da->count--;
}

void text_store_restore_last(const struct text_store *store)
{
	assert(store->text_da->count < store->text_max_count);
	store->text_da->count++;
}

/* CANVAS: GROUPS */
void group_store_append_owned(struct group_store *store, struct object_handle *hndls, size_t count, rect bounds)
{
	struct group g = {0};
	size_t i;

	g.members = hndls;
	g.member_count = count;
	g.obj.bounds = bounds;

	for (i = store->group_da->count; i < store->group_max_count; i++) {
		free(store->group_da->elems[i].members);
	}
	da_group_append(&store->group_da, g);
	store->group_max_count = store->group_da->count;
}

void group_store_delete_last(struct group_store *store)
{
	store->group_da->count--;
}

void group_store_restore_last(struct group_store *store)
{
	assert(store->group_da->count < store->group_max_count);
	store->group_da->count++;
}

void group_store_set_deleted(struct group_store *store, int id, bool deleted)
{
	store->group_da->elems[id].obj.deleted = deleted;
}

int group_store_hit(const struct group_store *store, vec2 v)
{
	struct group *grp;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->group_da->count; i++) {
		grp = store->group_da->elems + i;
		if (grp->obj.deleted) continue;

		global_bounds = rect_transl(grp->obj.bounds, grp->obj.transl);
		if (!rect_contains(global_bounds, v))
			continue;
		return i;
	}
	return -1;
}

/* CANVAS: GENERAL OBJ FUNC */

/* Most of these directly access/modify store internals
 * instead of making store specific methods for them. */

struct object_data *canvas_object_get(const struct canvas *canvas, struct object_handle hndl)
{
	switch (hndl.kind) {
	case OBJECT_KIND_STROKE: return &canvas->stroke_store.desc_da->elems[hndl.id].obj;
	case OBJECT_KIND_TEXT: return &canvas->text_store.text_da->elems[hndl.id].obj;
	case OBJECT_KIND_GROUP: return &canvas->group_store.group_da->elems[hndl.id].obj;
	default: return NULL;
	}
}

int canvas_object_group_owned(struct canvas *canvas, struct object_handle *hndls, size_t count)
{
	struct object_data *obj;
	rect global_bounds;
	size_t i;

	if (count < 2) return -1;

	obj = canvas_object_get(canvas, hndls[0]);
	global_bounds = rect_transl(obj->bounds, obj->transl);

	for (i = 1; i < count; i++) {
		obj = canvas_object_get(canvas, hndls[i]);
		rect obj_global = rect_transl(obj->bounds, obj->transl);
		global_bounds = rect_fit_rect(global_bounds, obj_global);
	}

	group_store_append_owned(&canvas->group_store, hndls, count, global_bounds);
	return 0;
}

void canvas_object_set_deleted(struct canvas *canvas, struct object_handle hndl, bool deleted)
{
	struct object_data *obj = canvas_object_get(canvas, hndl);
	struct group *group;
	size_t i;

	obj->deleted = deleted;

	if (hndl.kind != OBJECT_KIND_GROUP) return;

	group = canvas->group_store.group_da->elems + hndl.id;
	for (i = 0; i < group->member_count; i++) {
		canvas_object_set_deleted(canvas, group->members[i], deleted);
	}
}

void canvas_object_transl(struct canvas *canvas, struct object_handle hndl, vec2 d)
{
	struct object_data *obj = canvas_object_get(canvas, hndl);
	struct group *group;
	size_t i;

	obj->transl = vec2_add(obj->transl, d);

	if (hndl.kind != OBJECT_KIND_GROUP) return;

	group = canvas->group_store.group_da->elems + hndl.id;
	for (i = 0; i < group->member_count; i++) {
		canvas_object_transl(canvas, group->members[i], d);
	}
}

struct object_handle canvas_object_hit(struct canvas *canvas, vec2 v)
{
	struct object_handle hndl;

	hndl.kind = OBJECT_KIND_GROUP;
	hndl.id = group_store_hit(&canvas->group_store, v);

	if (hndl.id == -1) {
		hndl.kind = OBJECT_KIND_TEXT;
		hndl.id = text_store_hit(&canvas->text_store, v);
	}

	if (hndl.id == -1) {
		hndl.kind = OBJECT_KIND_STROKE;
		hndl.id = stroke_store_hit_nearest(&canvas->stroke_store, v, NULL);
	}

	if (hndl.id == -1) {
		hndl.kind = OBJECT_KIND_NONE;
	}
	return hndl;
}

struct object_handle canvas_object_hit_region(struct canvas *canvas, rect region);

/* ==============================
 * CMD
 * ==============================
 */
void cmd_cleanup(struct cmd *cmd)
{
	switch (cmd->kind) {
	case CMD_STROKE_CREATE:
	case CMD_TEXT_CREATE:
	case CMD_OBJECT_GROUP:
		break;
	case CMD_GROUP_DELETE: break;
	case CMD_OBJECT_DELETE:
		free(cmd->v.objs.hndls);
		break;
	case CMD_OBJECT_DRAG:
		free(cmd->v.drag.hndls);
		break;
	default: break;
	}
	cmd->kind = CMD_NONE;
}

void cmd_undo(struct cmd c, struct canvas *canvas)
{
	size_t i;

	switch (c.kind) {
	case CMD_OBJECT_DELETE:
		for (i = 0; i < c.v.objs.count; i++) {
			canvas_object_set_deleted(canvas, c.v.objs.hndls[i], false);
		}
		break;
	case CMD_OBJECT_DRAG:
		for (i = 0; i < c.v.drag.count; i++) {
			canvas_object_transl(canvas, c.v.drag.hndls[i], vec2_neg(c.v.drag.delta));
		}
		break;
	case CMD_GROUP_DELETE:
		group_store_set_deleted(&canvas->group_store, c.v.obj.id, false);
		break;
	case CMD_OBJECT_GROUP:
		group_store_delete_last(&canvas->group_store);
		break;
	case CMD_STROKE_CREATE:
		stroke_store_delete_last(&canvas->stroke_store);
		break;
	case CMD_TEXT_CREATE:
		text_store_delete_last(&canvas->text_store);
		break;
	default: break;
	}
}

void cmd_redo(struct cmd c, struct canvas *canvas)
{
	size_t i;

	switch (c.kind) {
	case CMD_OBJECT_DELETE:
		for (i = 0; i < c.v.objs.count; i++) {
			canvas_object_set_deleted(canvas, c.v.objs.hndls[i], true);
		}
		break;
	case CMD_OBJECT_DRAG:
		for (i = 0; i < c.v.drag.count; i++) {
			canvas_object_transl(canvas, c.v.drag.hndls[i], c.v.drag.delta);
		}
		break;
	case CMD_GROUP_DELETE:
		group_store_set_deleted(&canvas->group_store, c.v.obj.id, true);
		break;
	case CMD_OBJECT_GROUP:
		group_store_restore_last(&canvas->group_store);
		break;
	case CMD_STROKE_CREATE:
		stroke_store_restore_last(&canvas->stroke_store);
		break;
	case CMD_TEXT_CREATE:
		text_store_restore_last(&canvas->text_store);
		break;
	default: break;
	}
}

/* CMD MAKE */
struct cmd cmd_make(enum cmd_kind kind)
{
	struct cmd cmd;
	cmd.kind = kind;
	return cmd;
}

struct cmd cmd_obj_make(enum cmd_kind kind, struct object_handle hndl)
{
	struct cmd cmd;
	cmd.kind = kind;
	cmd.v.obj = hndl;
	return cmd;
}

struct cmd cmd_objs_make(enum cmd_kind kind, struct object_handle *hndls, size_t count)
{
	struct cmd cmd;
	cmd.kind = kind;
	cmd.v.objs.hndls = hndls;
	cmd.v.objs.count = count;
	return cmd;
}

struct cmd cmd_objects_drag_make(struct object_handle *hndls, size_t count, vec2 d)
{
	struct cmd cmd;
	cmd.kind = CMD_OBJECT_DRAG;
	cmd.v.drag.hndls = hndls;
	cmd.v.drag.count = count;
	cmd.v.drag.delta = d;
	return cmd;
}

/* CANVAS_HIST */
static void canvas_hist_record(struct canvas_hist *hist, struct cmd cmd)
{
	hist->cursor = RINGBUF_INCR(hist->cursor, ARRAY_SIZE(hist->cmds), 1);

	cmd_cleanup(hist->cmds + hist->cursor);
	hist->cmds[hist->cursor] = cmd;

	hist->last = hist->cursor;

	if (hist->before_first == hist->last)
		hist->before_first = RINGBUF_INCR(hist->before_first, ARRAY_SIZE(hist->cmds), 1);
}

static int canvas_hist_undo(struct canvas_hist *hist)
{
	if (hist->cursor == hist->before_first) {
		return -1;
	}

	cmd_undo(hist->cmds[hist->cursor], hist->canvas);
	hist->cursor = RINGBUF_DECR(hist->cursor, ARRAY_SIZE(hist->cmds), 1);
	return 0;
}

static int canvas_hist_redo(struct canvas_hist *hist)
{
	if (hist->cursor == hist->last) {
		return -1;
	}

	hist->cursor = RINGBUF_INCR(hist->cursor, ARRAY_SIZE(hist->cmds), 1);
	cmd_redo(hist->cmds[hist->cursor], hist->canvas);
	return 0;
}

/* ==============================
 * MODES
 * ==============================
 */
static void mode_switch_normal(void);
static void mode_switch_edit(enum select_kind kind);
static void mode_switch_draw(void);
static void mode_switch_command(void);
static void mode_switch_text(int edit_id);
static void mode_switch_drag(void);

/* MODE: NORMAL */
static void mode_normal_event_camera(struct graphic_ctx *gfx, const sapp_event *e)
{
	struct camera *cam = &gfx->cam;
	struct mouse *mouse = &gfx->mouse;
	float ratio;

	switch(e->type) {
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		if (cam->is_panning) {
			cam->pos.x = cam->pan_pivot_self.x + (cam->pan_pivot_mouse.x - mouse->screen.x)/cam->zoom;
			cam->pos.y = cam->pan_pivot_self.y + (cam->pan_pivot_mouse.y - mouse->screen.y)/cam->zoom;
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
			mode_switch_command();
			return 1;
		}
		if (e->char_code == 'i') {
			mode_switch_text(-1);
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
	struct canvas_hist *hist = &g_drawit.hist;
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
		mode_switch_draw();
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
				if (canvas_hist_undo(hist) == -1) {
					status_line_set(status, "Already at oldest history");
				}
			}
			break;
		case SAPP_KEYCODE_R:
			if (e->modifiers & SAPP_MODIFIER_CTRL) {
				if (canvas_hist_redo(hist) == -1) {
					status_line_set(status, "Already at newest change");
				}
			}
			break;
		case SAPP_KEYCODE_X:
		case SAPP_KEYCODE_S:
			mode_switch_edit(SELECT_NEAREST);
			break;
		case SAPP_KEYCODE_Q:
			if ( !(e->modifiers & SAPP_MODIFIER_CTRL) ) break;

			if (e->modifiers & SAPP_MODIFIER_SHIFT) {
				mode_switch_edit(SELECT_NEAREST);
				break;
			}

			if (g_drawit.is_grouping) {
				g_drawit.is_grouping = false;

				if (canvas_objects_created_after(canvas, *checkpt, &hndls, &count) != 0) {
					status_line_set(status, "Invalid Grouping Checkpoint");
					break;
				}
				if (canvas_object_group_owned(canvas, hndls, count) != 0) {
					free(hndls);
					break;
				}
				canvas_hist_record(hist, cmd_make(CMD_OBJECT_GROUP));
				status_line_set(status, "Commit Group");
			} else {
				g_drawit.is_grouping = true;

				*checkpt = canvas_checkpoint_make(canvas);
				status_line_set(status, "Begin Group");
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

static void mode_switch_normal(void)
{
	struct da_object_handle *selection = g_drawit.selection_da;

	g_drawit.mode = MODE_NORMAL;
	g_drawit.gfx.mouse.hide = false;
	sapp_show_mouse(false);

	selection->count = 0;
}

/* MODE: EDIT */
static void mode_edit_event_select_nearest(const sapp_event *e)
{
	struct da_object_handle **selection = &g_drawit.selection_da;
	struct mouse *mouse = &g_drawit.gfx.mouse;
	struct canvas *canvas = &g_drawit.canvas;
	struct object_handle hndl;

	(void)e;

	hndl = canvas_object_hit(canvas, mouse->world);
	if (hndl.kind == OBJECT_KIND_NONE) {
		(*selection)->count = 0;
		return;
	}

	if (!(*selection)->count) {
		da_object_handle_append_empty(selection, 1);
	} else {
		(*selection)->count = 1;
	}
	(*selection)->elems[0] = hndl;
}

static void mode_edit_event_select(const sapp_event *e)
{
	switch(g_drawit.select_kind) {
	case SELECT_NEAREST:
		mode_edit_event_select_nearest(e);
		break;
	default: return;
	}
}

static void mode_edit_event(const sapp_event *e)
{
	struct da_object_handle *selection_da = g_drawit.selection_da;
	struct canvas *canvas = &g_drawit.canvas;
	struct canvas_hist *hist = &g_drawit.hist;
	struct object_handle *hndls;
	size_t i;

	mode_edit_event_select(e);

	switch (e->type) {
	case SAPP_EVENTTYPE_KEY_UP:
		switch (e->key_code) {
		case SAPP_KEYCODE_S:
			if (!selection_da->count) break;
			mode_switch_drag();
			break;
		case SAPP_KEYCODE_X:
			if (!selection_da->count) break;
			for (i = 0; i < selection_da->count; i++) {
				canvas_object_set_deleted(canvas, selection_da->elems[i], true);
			}
			hndls = da_object_handle_elemdup(selection_da);
			canvas_hist_record(hist, cmd_objs_make(CMD_OBJECT_DELETE, hndls, selection_da->count));
			mode_switch_normal();
			break;
		case SAPP_KEYCODE_Q:
			if ( !(e->modifiers & SAPP_MODIFIER_CTRL) ) break;
			if ( !(e->modifiers & SAPP_MODIFIER_SHIFT) ) break;

			if (!selection_da->count) break;
			if (selection_da->elems[0].kind != OBJECT_KIND_GROUP) break;

			group_store_set_deleted(&canvas->group_store, selection_da->elems[0].id, true);
			canvas_hist_record(hist, cmd_obj_make(CMD_GROUP_DELETE, selection_da->elems[0]));
			break;
		default:
			mode_switch_normal();
			break;
		}
		break;
	default:
		break;
	}
}

static void mode_switch_edit(enum select_kind kind)
{
	g_drawit.mode = MODE_EDIT;
	g_drawit.select_kind = kind;
	g_drawit.gfx.mouse.hide = false;
	sapp_show_mouse(false);
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
		switch (e->key_code) {
		case SAPP_KEYCODE_S:
			d = vec2_sub(g_drawit.drag_end, g_drawit.drag_start);
			hndls = da_object_handle_elemdup(selection_da);
			canvas_hist_record(hist, cmd_objects_drag_make(hndls, selection_da->count, d));
			mode_switch_normal();
			break;
		case SAPP_KEYCODE_ESCAPE:
			d = vec2_sub(g_drawit.drag_start, g_drawit.drag_end);
			for (i = 0; i < selection_da->count; i++) { 
				canvas_object_transl(canvas, selection_da->elems[i], d);
			}
			mode_switch_normal();
		default: break;
		}
		break;
	default:
		break;
	}
}

static void mode_switch_drag(void)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;

	g_drawit.mode = MODE_DRAG;
	gfx->mouse.hide = false;
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
		mode_switch_normal();
		break;
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		mode_draw_stroke_try_append(store, pen->curr_pt);
		break;
	default: break;
	}
}

static void mode_switch_draw(void)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct status_line *status = &g_drawit.status_line;

	struct stroke_store *store = &g_drawit.canvas.stroke_store;
	struct canvas_hist *hist = &g_drawit.hist;
	struct pen *pen = &g_drawit.pen;
	const color *colors = pen->active_colors[g_drawit.theme];

	if (status->len) status->len = 0;

	g_drawit.mode = MODE_DRAW;
	gfx->mouse.hide = false;
	sapp_show_mouse(false);

	stroke_store_begin(store, colors);
	stroke_store_append_point(store, pen->curr_pt);
	/* What's recorded is only the 'act' of creating a stroke */
	canvas_hist_record(hist, cmd_make(CMD_STROKE_CREATE));
}


/* MODE: COMMAND */

static void command_exec(const char *str)
{
	struct canvas *canvas = &g_drawit.canvas;
	struct status_line *status = &g_drawit.status_line;

	if (str[0] == ':') {
		str++;
		if (drawit_strcasecmp(str, "light") == 0 || (str[0] == 'l')) {
			g_drawit.theme = THEME_LIGHT;
			status_line_set(status, "theme=light");
		} else if (drawit_strcasecmp(str, "dark") == 0 || (str[0] == 'd')) {
			g_drawit.theme = THEME_DARK;
			status_line_set(status, "theme=dark");
		} else if (drawit_strcasecmp(str, "quit") == 0 || (str[0] == 'q')) {
			sapp_request_quit();
		} else if (drawit_strcasecmp(str, "print") == 0 || (str[0] == 'p')) {
			stroke_store_print(&canvas->stroke_store);
			status_line_set(status, "debug print stroke store to stdout");
		} else if (str[0] == 'v') {
			status_line_set(status, "Compiled on " __DATE__ " " __TIME__);
		} else {
			status_line_set(status, "unknown command");
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

		mode_switch_normal();
		break;
	case SAPP_KEYCODE_ESCAPE:
		mode_switch_normal();
		status->len = 0;
		break;
	default: break;
	}
}

static void mode_switch_command(void)
{
	struct graphic_ctx *gfx = &g_drawit.gfx;
	struct status_line *status = &g_drawit.status_line;

	g_drawit.mode = MODE_COMMAND;
	gfx->mouse.hide = true;
	status_line_set(status, ":");
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
		mode_switch_normal();
		break;
	default: break;
	}

	if (t->bounds_dirty) {
		text_obj_update_bounds(t, gfx);
	}
}

static void mode_switch_text(int edit_id)
{
	struct text_store *store = &g_drawit.canvas.text_store;
	struct canvas_hist *hist = &g_drawit.hist;
	const color *colors = *g_drawit.pen.active_colors;
	struct graphic_ctx *gfx = &g_drawit.gfx;

	if (edit_id >= 0) {
		g_drawit.active_text_id = edit_id;
	} else {
		text_store_append(store, colors, gfx->font_handle, FONT_SIZE_DEFAULT, gfx->mouse.world);
		canvas_hist_record(hist, cmd_make(CMD_TEXT_CREATE));

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
static inline void nvg_fontsize_ctx(NVGcontext *ctx, const struct text_obj *txt)
{
	nvgFontSize(ctx, txt->font_size);
	nvgFontFaceId(ctx, txt->font_handle);
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
}

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
	nvg_fontsize_ctx(gfx->vg, txt);
	x = nvgTextBounds(gfx->vg, 0, 0, txt->buf->data + start, txt->buf->data + i, NULL);
	return (vec2) { x, y };
}

static inline vec2 get_cursor_size(struct graphic_ctx *gfx, const struct text_obj *txt)
{
	float ascender, descender;
	float advance;

	nvg_fontsize_ctx(gfx->vg, txt);
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

void draw_text_cursor_overlay(struct graphic_ctx *gfx, const struct text_obj *txt, enum theme theme, bool local)
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

	nvg_fontsize_ctx(gfx->vg, txt);
	nvgFillColor(gfx->vg, color_to_NVGcolor(COLORS_BACKGROUND[theme]));
	nvgText(gfx->vg, transl.x, transl.y, c, c+1); /* I give up messing with GlobalCompositeOperations */
}

void draw_obj_bounds(struct graphic_ctx *gfx, const struct object_data *obj)
{
	rect global_bounds = rect_transl(obj->bounds, obj->transl);

	nvgBeginPath(gfx->vg);
	nvgRect(gfx->vg,
		global_bounds.x0,
		global_bounds.y0,
		global_bounds.x1 - global_bounds.x0,
		global_bounds.y1 - global_bounds.y0
	);
	nvgStrokeColor(gfx->vg, nvgRGBA(0, 255, 0, 255));
	nvgStrokeWidth(gfx->vg, 2.0f);
	nvgStroke(gfx->vg);
}

void draw_text(struct graphic_ctx *gfx, const struct text_obj *txt, enum theme theme, bool draw_cursor)
{
	const struct gapbuf *buf = txt->buf;

	size_t start, i;
	float x = 0,
	      y = 0;

	nvg_fontsize_ctx(gfx->vg, txt);
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

void draw_selected_bounds(struct graphic_ctx *gfx, const struct canvas *canvas, const struct da_object_handle *selects)
{
	struct object_data *obj;
	size_t i;

	for (i = 0; i < selects->count; i++) { 
		obj = canvas_object_get(canvas, selects->elems[i]);
		draw_obj_bounds(gfx, obj);
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

	gfx->clear_colors = COLORS_BACKGROUND;
	pen->colors_primary = COLORS_YELLOW;
	pen->colors_secondary = COLORS_RED;
	pen->active_colors = &pen->colors_primary;

	gfx->screen_width = sapp_width();
	gfx->screen_height = sapp_height();
	gladLoaderLoadGL();
	gfx->vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

	gfx->font_handle = nvgCreateFontMem(gfx->vg, "Inconsolata-Regular-Sub", (u8 *)inconsolata_ttf, inconsolata_ttf_len, 0);

	mode_switch_normal();

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
		pen->curr_pt.pressure = pen->last_pt.pressure * .75 + pen->curr_pt.pressure * .25; /* blend */

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
	case MODE_EDIT:
		mode_edit_event(e);
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
	glClearColor(gfx->clear_colors[g_drawit.theme].r/255.0f, gfx->clear_colors[g_drawit.theme].g/255.0f, gfx->clear_colors[g_drawit.theme].b/255.0f, gfx->clear_colors[g_drawit.theme].a/255.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	nvgBeginFrame(gfx->vg, gfx->screen_width/gfx->screen_dpi_scale, gfx->screen_height/gfx->screen_dpi_scale, gfx->screen_dpi_scale);
		nvgSave(gfx->vg);
			nvgTranslate(gfx->vg, gfx->screen_width/2, gfx->screen_height/2);
			nvgScale(gfx->vg, cam->zoom, cam->zoom);
			nvgTranslate(gfx->vg, -cam->pos.x, -cam->pos.y);

			draw_stroke_store(gfx, &canvas->stroke_store);
			draw_text_store(gfx, &canvas->text_store, g_drawit.theme, g_drawit.active_text_id);
			draw_selected_bounds(gfx, canvas, g_drawit.selection_da);

		nvgRestore(gfx->vg);

		if (!gfx->mouse.hide && !gfx->mouse.is_outside_frame) {
			nvgBeginPath(gfx->vg);
				nvgCircle(gfx->vg, roundf(mouse->screen.x), round(mouse->screen.y), cam->zoom*STROKE_OPTS.size/1.5);
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

