#include <perfect-freehand/pfh.h>
#include "types.h"

#define CANVAS_HIST_MAX 256
#define STROKE_BOUNDS_MARGIN 5

struct object_data {
	int    group_id;
	vec2   transl;
	rect   bounds;
	bool   deleted;
};

enum object_kind {
	OBJECT_KIND_NONE,
	OBJECT_KIND_GROUP,
	OBJECT_KIND_STROKE,
	OBJECT_KIND_TEXT,
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

struct pen_pt {
	vec2 coord;
	float pressure;
};

#define T struct pen_pt
#define NAME da_pen_pt
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

enum canvas_cmd_kind {
	CANVAS_CMD_NONE,
	CANVAS_CMD_OBJECT_DELETE,
	CANVAS_CMD_OBJECT_DRAG,
	CANVAS_CMD_OBJECT_GROUP,
	CANVAS_CMD_GROUP_DELETE,

	CANVAS_CMD_STROKE_CREATE,
	CANVAS_CMD_TEXT_CREATE,
};

struct canvas_cmd {
	enum canvas_cmd_kind kind;
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
	struct canvas_cmd cmds[CANVAS_HIST_MAX];
};

/* GLOBALS */
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
static const struct object_data OBJ_DEFAULT = {
	.group_id = -1,
	.bounds = {
		.x0 = INFINITY,
		.y0 = INFINITY,
		.x1 = -INFINITY,
		.y1 = -INFINITY
	}
};
static const struct object_handle HANDLE_DEFAULT = { .id = -1, .kind = OBJECT_KIND_NONE };


static struct canvas canvas_create_empty()
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
static struct canvas_checkpoint canvas_checkpoint_make(const struct canvas *canvas)
{
	struct canvas_checkpoint chk;
	chk.stroke_count = canvas->stroke_store.desc_da->count;
	chk.text_count = canvas->text_store.text_da->count;
	chk.group_count = canvas->group_store.group_da->count;
	return chk;
}

static int canvas_objects_created_after(const struct canvas *canvas,
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
	long total = dstroke + dtext + dgroup;

	int id;

	if (!dstroke && !dtext && !dgroup) return -1;
	if (dstroke < 0 || dtext < 0 || dgroup < 0) return -2;

	*out_handles = (struct object_handle *)malloc(total * sizeof(struct object_handle *));
	assert(*out_handles);

	*out_count = 0;
	for (id = (int)chk.stroke_count; (size_t)id < stroke_desc_da->count; id++) {
		if (stroke_desc_da->elems[id].obj.deleted) continue;
		(*out_handles)[(*out_count)++] = (struct object_handle){ .kind = OBJECT_KIND_STROKE,
		                                                         .id = id };
	}
	for (id = (int)chk.text_count; (size_t)id < text_da->count; id++) {
		if (text_da->elems[id].obj.deleted) continue;
		(*out_handles)[(*out_count)++] = (struct object_handle){ .kind = OBJECT_KIND_TEXT,
		                                                         .id = id };
	}
	for (id = (int)chk.group_count; (size_t)id < group_da->count; id++) {
		if (group_da->elems[id].obj.deleted) continue;
		(*out_handles)[(*out_count)++] = (struct object_handle){ .kind = OBJECT_KIND_GROUP,
		                                                         .id = id };
	}
	
	return 0;
}

/* CANVAS: STROKES */
static void stroke_store_begin(struct stroke_store *store, const color *colors)
{
	da_stroke_desc_append(&store->desc_da, (struct stroke_desc) { 
		.obj          = OBJ_DEFAULT,
		.input_idx    = store->input_da->count, 
		.input_count  = 0,
		.vertex_idx   = store->pfh_vertex_buf.count, 
		.vertex_count = 0,
		.colors       = colors,
	});
	store->desc_max_count = store->desc_da->count;
}

static void stroke_store_render_last(struct stroke_store *store)
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

static void stroke_store_append_point(struct stroke_store *store, struct pen_pt pt)
{
	struct stroke_desc *s = DA_LAST(store->desc_da);
	const vec2 PT_EXTENTS = vec2_all(STROKE_OPTS.size/2 + STROKE_BOUNDS_MARGIN);

	da_pen_pt_append(&store->input_da, pt);
	s->input_count++;
	s->obj.bounds = rect_fit_rect(s->obj.bounds, rect_create(pt.coord, PT_EXTENTS));

	stroke_store_render_last(store);
}
/*
static void stroke_store_append_points(struct stroke_store *store,
                                       const struct pen_pt pts[], int count)
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
*/
static void stroke_store_delete_last(struct stroke_store *store)
{
	struct stroke_desc *s = DA_LAST(store->desc_da);

	store->input_da->count = s->input_idx;
	store->pfh_vertex_buf.count = s->vertex_idx;
	store->desc_da->count--;
}

static void stroke_store_restore_last(struct stroke_store *store)
{
	struct stroke_desc *s;

	assert(store->desc_max_count > store->desc_da->count);

	store->desc_da->count++;
	s = DA_LAST(store->desc_da);

	store->input_da->count = s->input_idx + s->input_count;
	store->pfh_vertex_buf.count = s->vertex_idx + s->vertex_count;
}

static float stroke_store_dist(const struct stroke_store *store, int id, vec2 v)
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

static struct object_handle stroke_store_hit_nearest(const struct stroke_store *store,
                                                     vec2 v, float *dist2_out,
                                                     bool root_only)
{
	struct stroke_desc *desc;
	rect global_bounds;

	long nearest_stroke_idx = -1;
	float nearest_dist2 = FLT_MAX;
	float dist2;
	size_t i;

	for (i = 0; i < store->desc_da->count; i++) {
		desc = store->desc_da->elems + i;
		if (desc->obj.deleted) continue;
		if (desc->obj.group_id != -1 && root_only) continue;

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

	if (nearest_stroke_idx == -1) return HANDLE_DEFAULT;
	return (struct object_handle) { .id = nearest_stroke_idx, .kind = OBJECT_KIND_STROKE };
}

static void stroke_store_hit_rect(const struct stroke_store *store,
                                  struct da_object_handle **hndls,
                                  rect r, bool root_only)
{
	struct stroke_desc *desc;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->desc_da->count; i++) {
		desc = store->desc_da->elems + i;
		if (desc->obj.deleted) continue;
		if (desc->obj.group_id != -1 && root_only) continue;

		global_bounds = rect_transl(desc->obj.bounds, desc->obj.transl);
		if (!rect_overlaps(global_bounds, r)) continue;

		da_object_handle_append(hndls, (struct object_handle) { .id = i,
		                                                        .kind = OBJECT_KIND_STROKE });
	}
}

/* CANVAS: TEXT */
static struct text_obj text_obj_create(const color *colors, int font_handle,
                                       float font_size, vec2 transl)
{
	const float LEADING_RATIO = 1.2f;
	struct text_obj txt = {
		.obj         = OBJ_DEFAULT,
		.font_handle = font_handle,
		.font_size   = font_size,
		.line_height = font_size * LEADING_RATIO,
		.colors      = colors,
		.buf         = gapbuf_create(GAPBUF_INITIAL_ALLOC),
	};

	txt.obj.transl = transl;
	return txt;
}

static inline void text_obj_nvg_context(const struct text_obj *txt, NVGcontext *ctx)
{
	nvgFontSize(ctx, txt->font_size);
	nvgFontFaceId(ctx, txt->font_handle);
	nvgTextAlign(ctx, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
}

static void text_obj_update_bounds(struct text_obj *txt, NVGcontext *ctx)
{
	const struct gapbuf *buf = txt->buf;
	size_t start = 0, i = 0;
	float x = 0.0f, max_w = 0.0f;
	float h = txt->line_height;

	text_obj_nvg_context(txt, ctx);

	for (;;) {
		if (i >= buf->capacity) {
			x += nvgTextBounds(ctx, x, 0, buf->data + start, buf->data + i, NULL);
			if (x > max_w) max_w = x;
			break;
		}
		if (i == buf->gap_start && buf->gap_start != buf->gap_end) {
			x += nvgTextBounds(ctx, x, 0, buf->data + start, buf->data + i, NULL);
			start = i = buf->gap_end;
			continue;
		}
		if (buf->data[i] == '\n') {
			x += nvgTextBounds(ctx, x, 0, buf->data + start, buf->data + i, NULL);
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

static void text_obj_insert(struct text_obj *txt, unsigned char c)
{
	gapbuf_insert(&txt->buf, c);
	txt->bounds_dirty = 1;
}

static void text_obj_backspace(struct text_obj *txt)
{
	gapbuf_backspace(txt->buf);
	txt->bounds_dirty = 1;
}

static void text_obj_cursor(struct text_obj *txt, int dir)
{
	gapbuf_open(txt->buf, txt->buf->gap_start+dir);
}

static inline void text_store_append(struct text_store *store, const color *colors,
                                     int font_handle, float font_size, vec2 pos)
{
	size_t i;

	for (i = store->text_da->count; i < store->text_max_count; i++) {
		gapbuf_destroy(&store->text_da->elems[i].buf);
	}

	da_text_obj_append(&store->text_da, text_obj_create(colors, font_handle, font_size, pos));
	store->text_max_count = store->text_da->count;
}

static struct object_handle text_store_hit(const struct text_store *store,
                                           vec2 v, bool root_only)
{
	struct text_obj *txt;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->text_da->count; i++) {
		txt = store->text_da->elems + i;
		if (txt->obj.deleted) continue;
		if (txt->obj.group_id != -1 && root_only) continue;

		global_bounds = rect_transl(txt->obj.bounds, txt->obj.transl);
		if (!rect_contains(global_bounds, v)) continue;

		return (struct object_handle) { .id = i, .kind = OBJECT_KIND_TEXT };
	}
	return HANDLE_DEFAULT;
}

static void text_store_hit_rect(const struct text_store *store,
                                struct da_object_handle **hndls_da,
                                rect r, bool root_only)
{
	struct text_obj *txt;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->text_da->count; i++) {
		txt = store->text_da->elems + i;
		if (txt->obj.deleted) continue;
		if (txt->obj.group_id != -1 && root_only) continue;

		global_bounds = rect_transl(txt->obj.bounds, txt->obj.transl);
		if (!rect_overlaps(global_bounds, r)) continue;

		da_object_handle_append(hndls_da, (struct object_handle){ .id = i,
		                                                          .kind = OBJECT_KIND_TEXT });
	}
}

static void text_store_delete_last(const struct text_store *store)
{
	store->text_da->count--;
}

static void text_store_restore_last(const struct text_store *store)
{
	assert(store->text_da->count < store->text_max_count);
	store->text_da->count++;
}

/* CANVAS: GROUPS */
static int group_store_next_id(struct group_store *store)
{
	return store->group_da->count;
}

static void group_store_append_owned(struct group_store *store,
                                     struct object_handle *hndls,
                                     size_t count, rect bounds)
{
	struct group grp = {
		.obj          = OBJ_DEFAULT,
		.members      = hndls,
		.member_count = count
	};
	size_t i;

	grp.obj.bounds = bounds;

	for (i = store->group_da->count; i < store->group_max_count; i++) {
		free(store->group_da->elems[i].members);
	}
	da_group_append(&store->group_da, grp);
	store->group_max_count = store->group_da->count;
}

static struct object_handle group_store_hit(const struct group_store *store,
                                            vec2 v, bool root_only)
{
	struct group *grp;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->group_da->count; i++) {
		grp = store->group_da->elems + i;
		if (grp->obj.deleted) continue;
		if (grp->obj.group_id != -1 && root_only) continue;

		global_bounds = rect_transl(grp->obj.bounds, grp->obj.transl);
		if (!rect_contains(global_bounds, v)) continue;

		return (struct object_handle) { .id = i, .kind = OBJECT_KIND_GROUP };
	}
	return HANDLE_DEFAULT;
}

static void group_store_hit_rect(const struct group_store *store,
                                 struct da_object_handle **hndls_da,
                                 rect r, bool root_only)
{
	struct group *grp;
	rect global_bounds;
	size_t i;

	for (i = 0; i < store->group_da->count; i++) {
		grp = store->group_da->elems + i;
		if (grp->obj.deleted) continue;
		if (grp->obj.group_id != -1 && root_only) continue;

		global_bounds = rect_transl(grp->obj.bounds, grp->obj.transl);
		if (!rect_overlaps(global_bounds, r)) continue;

		da_object_handle_append(hndls_da, (struct object_handle) { .id = i,
		                                                           .kind = OBJECT_KIND_GROUP });
	}
}

/* CANVAS: GENERAL OBJ FUNC */

/* Most of these directly access/modify store internals
 * instead of making store specific methods for them. */

static struct object_data *canvas_object_get(const struct canvas *canvas,
                                             struct object_handle hndl)
{
	switch (hndl.kind) {
	case OBJECT_KIND_STROKE: return &canvas->stroke_store.desc_da->elems[hndl.id].obj;
	case OBJECT_KIND_TEXT: return &canvas->text_store.text_da->elems[hndl.id].obj;
	case OBJECT_KIND_GROUP: return &canvas->group_store.group_da->elems[hndl.id].obj;
	default: return NULL;
	}
}

static void canvas_object_set_deleted(struct canvas *canvas,
                                      struct object_handle hndl, bool deleted)
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

static void canvas_object_transl(struct canvas *canvas, struct object_handle hndl, vec2 d)
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

static struct object_handle canvas_object_hit(struct canvas *canvas, vec2 v)
{
	struct object_handle hndl;

	hndl = group_store_hit(&canvas->group_store, v, true);
	if (hndl.kind == OBJECT_KIND_NONE) {
		hndl = text_store_hit(&canvas->text_store, v, true);
	}
	if (hndl.kind == OBJECT_KIND_NONE) {
		hndl = stroke_store_hit_nearest(&canvas->stroke_store, v, NULL, true);
	}
	return hndl;
}

static void canvas_object_hit_rect(struct canvas *canvas,
                                   struct da_object_handle **hndls_da, rect r)
{
	group_store_hit_rect(&canvas->group_store, hndls_da, r, true);
	text_store_hit_rect(&canvas->text_store, hndls_da, r, true);
	stroke_store_hit_rect(&canvas->stroke_store, hndls_da, r, true);
}

/* CANVAS OBJ GROUPING */
static int canvas_object_group_owned(struct canvas *canvas,
                                     struct object_handle *hndls, size_t count)
{
	struct object_data *obj;
	rect global_bounds;
	size_t i;
	int future_id;

	if (count < 2) return -1;

	future_id = group_store_next_id(&canvas->group_store);

	obj = canvas_object_get(canvas, hndls[0]);
	obj->group_id = future_id;
	global_bounds = rect_transl(obj->bounds, obj->transl);

	for (i = 1; i < count; i++) {
		obj = canvas_object_get(canvas, hndls[i]);

		obj->group_id = future_id;
		rect obj_global = rect_transl(obj->bounds, obj->transl);
		global_bounds = rect_fit_rect(global_bounds, obj_global);
	}

	group_store_append_owned(&canvas->group_store, hndls, count, global_bounds);
	return 0;
}

static void canvas_group__update_members_group(struct canvas *canvas, int group_id, bool deleted)
{
	struct da_group *group_da = canvas->group_store.group_da;
	struct object_data *obj;
	struct group *grp;
	size_t i;

	grp = group_da->elems + group_id;

	for (i = 0; i < grp->member_count; i++) {
		obj = canvas_object_get(canvas, grp->members[i]);
		obj->group_id = deleted ? -1 : group_id;
	}
}

static void canvas_group_delete_last(struct canvas *canvas)
{
	struct da_group *group_da = canvas->group_store.group_da;
	group_da->count--;
	canvas_group__update_members_group(canvas, group_da->count, -1);
}

static void canvas_group_restore_last(struct canvas *canvas)
{
	struct da_group *group_da = canvas->group_store.group_da;
	assert(group_da->count < canvas->group_store.group_max_count);
	group_da->count++;

	canvas_group__update_members_group(canvas, group_da->count-1, group_da->count-1);
}

static void canvas_group_set_deleted(struct canvas *canvas, int id, bool deleted)
{
	struct da_group *group_da = canvas->group_store.group_da;
	group_da->elems[id].obj.deleted = deleted;
	canvas_group__update_members_group(canvas, id, -1);
}

/* ==============================
 * CANVAS CMD
 * ==============================
 */
static void canvas_cmd_cleanup(struct canvas_cmd *cmd)
{
	switch (cmd->kind) {
	case CANVAS_CMD_STROKE_CREATE:
	case CANVAS_CMD_TEXT_CREATE:
	case CANVAS_CMD_OBJECT_GROUP:
		break;
	case CANVAS_CMD_GROUP_DELETE: break;
	case CANVAS_CMD_OBJECT_DELETE:
		free(cmd->v.objs.hndls);
		break;
	case CANVAS_CMD_OBJECT_DRAG:
		free(cmd->v.drag.hndls);
		break;
	default: break;
	}
	cmd->kind = CANVAS_CMD_NONE;
}

static void canvas_cmd_undo(struct canvas_cmd c, struct canvas *canvas)
{
	size_t i;

	switch (c.kind) {
	case CANVAS_CMD_OBJECT_DELETE:
		for (i = 0; i < c.v.objs.count; i++) {
			canvas_object_set_deleted(canvas, c.v.objs.hndls[i], false);
		}
		break;
	case CANVAS_CMD_OBJECT_DRAG:
		for (i = 0; i < c.v.drag.count; i++) {
			canvas_object_transl(canvas, c.v.drag.hndls[i], vec2_neg(c.v.drag.delta));
		}
		break;
	case CANVAS_CMD_GROUP_DELETE:
		canvas_group_set_deleted(canvas, c.v.obj.id, false);
		break;
	case CANVAS_CMD_OBJECT_GROUP:
		canvas_group_delete_last(canvas);
		break;
	case CANVAS_CMD_STROKE_CREATE:
		stroke_store_delete_last(&canvas->stroke_store);
		break;
	case CANVAS_CMD_TEXT_CREATE:
		text_store_delete_last(&canvas->text_store);
		break;
	default: break;
	}
}

static void canvas_cmd_redo(struct canvas_cmd c, struct canvas *canvas)
{
	size_t i;

	switch (c.kind) {
	case CANVAS_CMD_OBJECT_DELETE:
		for (i = 0; i < c.v.objs.count; i++) {
			canvas_object_set_deleted(canvas, c.v.objs.hndls[i], true);
		}
		break;
	case CANVAS_CMD_OBJECT_DRAG:
		for (i = 0; i < c.v.drag.count; i++) {
			canvas_object_transl(canvas, c.v.drag.hndls[i], c.v.drag.delta);
		}
		break;
	case CANVAS_CMD_GROUP_DELETE:
		canvas_group_set_deleted(canvas, c.v.obj.id, true);
		break;
	case CANVAS_CMD_OBJECT_GROUP:
		canvas_group_restore_last(canvas);
		break;
	case CANVAS_CMD_STROKE_CREATE:
		stroke_store_restore_last(&canvas->stroke_store);
		break;
	case CANVAS_CMD_TEXT_CREATE:
		text_store_restore_last(&canvas->text_store);
		break;
	default: break;
	}
}

/* CANVAS CMD MAKE */
static struct canvas_cmd canvas_cmd_make(enum canvas_cmd_kind kind)
{
	struct canvas_cmd cmd;
	cmd.kind = kind;
	return cmd;
}

static struct canvas_cmd canvas_cmd_obj_make(enum canvas_cmd_kind kind, struct object_handle hndl)
{
	struct canvas_cmd cmd;
	cmd.kind = kind;
	cmd.v.obj = hndl;
	return cmd;
}

static struct canvas_cmd canvas_cmd_objs_make(enum canvas_cmd_kind kind, struct object_handle *hndls, size_t count)
{
	struct canvas_cmd cmd;
	cmd.kind = kind;
	cmd.v.objs.hndls = hndls;
	cmd.v.objs.count = count;
	return cmd;
}

static struct canvas_cmd canvas_cmd_drag_make(struct object_handle *hndls, size_t count, vec2 d)
{
	struct canvas_cmd cmd;
	cmd.kind = CANVAS_CMD_OBJECT_DRAG;
	cmd.v.drag.hndls = hndls;
	cmd.v.drag.count = count;
	cmd.v.drag.delta = d;
	return cmd;
}

/* CANVAS_HIST */
static void canvas_hist_record(struct canvas_hist *hist, struct canvas_cmd cmd)
{
	hist->cursor = RINGBUF_INCR(hist->cursor, ARRAY_SIZE(hist->cmds), 1);

	canvas_cmd_cleanup(hist->cmds + hist->cursor);
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

	canvas_cmd_undo(hist->cmds[hist->cursor], hist->canvas);
	hist->cursor = RINGBUF_DECR(hist->cursor, ARRAY_SIZE(hist->cmds), 1);
	return 0;
}

static int canvas_hist_redo(struct canvas_hist *hist)
{
	if (hist->cursor == hist->last) {
		return -1;
	}

	hist->cursor = RINGBUF_INCR(hist->cursor, ARRAY_SIZE(hist->cmds), 1);
	canvas_cmd_redo(hist->cmds[hist->cursor], hist->canvas);
	return 0;
}
