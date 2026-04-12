#define GLAD_GL_IMPLEMENTATION
#include <glad2/gl.h>

#include <sokol/sokol_app.h>

#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg/nanovg.h>
#include <nanovg/nanovg_gl.h>

#define PFH_IMPLEMENTATION
#include <perfect-freehand/pfh.h>

#include "ds.h"


#define CMD_HIST_MAX 256
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3;
typedef struct { float r, g, b, a; } color;

typedef struct { vec2 coord; float pressure; } point;

DA_DEFINE(point, da_point)
DA_DEFINE(int, da_int)

struct object {
	struct da_int   *input_starts;
	struct da_point *input_points;
	struct da_int   *stroke_starts;
	pfh_vec2_buf     stroke_buf;
};

enum cmd_type {
	CMD_NONE,
	CMD_STROKE_CREATE,
	CMD_STROKE_DELETE,
};

struct cmd {
	enum cmd_type type;
	union {
		struct da_point *points; 
	} v;
};

struct cmd_hist {
	size_t start;
	size_t end;
	long cursor;
	struct cmd cmds[CMD_HIST_MAX];
};

DA_DEFINE(struct cmd, da_cmd)

DA_DEFINE(struct object, da_object)

static float zoom_frac = 0.1f;

static const color CLEAR_COLOR_DEFAULT = { .1f, .1f, .1f, .0f };
static color clear_color;

static int screen_width, screen_height;
static NVGcontext *vg;
static vec2 mouse_screen;
static vec2 mouse_world;
static vec2 camera = {0, 0};
static float zoom = 1.0f;

static bool is_panning = false;
static vec2 pan_pivot_mouse;
static vec2 pan_pivot_camera;

static bool is_drawing_obj = false;
static bool is_drawing_stroke = false;
static struct da_object *objects;

static struct cmd cmd_curr;
static struct cmd_hist cmd_hist;

static const pfh_stroke_opts STROKE_OPTS = {
	.size = 12,
	.thinning = .5,
	.streamline = .5,
	.smoothing = .5,
	.easing = NULL,
	.simulate_pressure = true,
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

static inline vec2 screen_to_world(vec2 screen)
{
	return (vec2){
		.x = ( (screen.x - screen_width/2) / zoom) + camera.x,
		.y = ( (screen_height/2 - screen.y) / zoom) + camera.y,
	};
}

/************ OBJ & STROKE ************/

void object_print(const struct object *obj) 
{
	int i;
	printf("	input_starts (%zu): ", obj->input_starts->len);
	for (i = 0; i < obj->input_starts->len; i++)
		printf("%d ", obj->input_starts->elems[i]);
	puts("");
	printf("	input_points (%zu)\n", obj->input_points->len);

	printf("	stroke_starts (%zu): ", obj->stroke_starts->len);
	for (i = 0; i < obj->stroke_starts->len; i++)
		printf("%d ", obj->stroke_starts->elems[i]);
	puts("");

	printf("	stroke_elems (%zu): ", obj->stroke_buf.len);
	for (i = 0; i < obj->stroke_buf.len; i++)
		printf("(%.1f %.1f) ", obj->stroke_buf.elems[i].x, obj->stroke_buf.elems[i].y);
	puts("");
}

void object_begin(void)
{
	is_drawing_obj = true;
	struct object obj = {
		.input_points  = da_point_create(DA_INITIAL_CAPACITY),
		.input_starts  = da_int_create(DA_INITIAL_CAPACITY),
		.stroke_starts = da_int_create(DA_INITIAL_CAPACITY),
	};
	pfh_vec2_buf_init(&obj.stroke_buf, DA_INITIAL_CAPACITY);
	objects = da_object_append(objects, obj);
}

void bake_last_stroke(struct object *obj)
{
	size_t last_stroke_start_idx = obj->stroke_starts->elems[obj->stroke_starts->len-1];
	size_t last_input_start_idx = obj->input_starts->elems[obj->input_starts->len-1];

	obj->stroke_buf.len = last_stroke_start_idx;
	pfh_get_stroke(
		&obj->stroke_buf,
		(pfh_point*)obj->input_points->elems + last_input_start_idx,
		obj->input_points->len - last_input_start_idx,
		&STROKE_OPTS
	);
}

void stroke_start(struct object *obj)
{
	obj->input_starts = da_int_append(obj->input_starts, obj->input_points->len);
	obj->stroke_starts = da_int_append(obj->stroke_starts, obj->stroke_buf.len);
}

void stroke_append_point(struct object *obj, point pt)
{
	obj->input_points = da_point_append(obj->input_points, pt);
	bake_last_stroke(obj);
}

void stroke_append_points(struct object *obj, point points[], int len)
{
	obj->input_points = da_point_append_n(obj->input_points, points, len);
	bake_last_stroke(obj);
}

void stroke_delete_last(struct object *obj)
{
	obj->input_starts->len--;
	obj->input_points->len = obj->input_starts->elems[obj->input_starts->len];

	obj->stroke_starts->len--;
	obj->stroke_buf.len = obj->stroke_starts->elems[obj->stroke_starts->len];
	pfh_get_stroke(
		&obj->stroke_buf,
		(pfh_point*)obj->input_points->elems + obj->input_starts->elems[obj->input_starts->len-1],
		obj->input_points->len - obj->input_starts->elems[obj->input_starts->len-1],
		&STROKE_OPTS
	);
}

/************ COMMAND ************/

static void cmd_hist_forget(struct cmd *cmd)
{
	switch(cmd->type) {
	case CMD_STROKE_DELETE:
	case CMD_STROKE_CREATE:
		free(cmd->v.points);
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

	cmd_hist.end = cmd_hist.cursor;

	if (cmd_hist.start == cmd_hist.end)
		cmd_hist.start = RINGBUF_INCR(cmd_hist.start, ARRAY_SIZE(cmd_hist.cmds), 1);
}

static void cmd_stroke_create(struct cmd cmd)
{
	stroke_start(objects->elems + objects->len-1);
	stroke_append_points(
		objects->elems + objects->len-1,
		cmd.v.points->elems,
		cmd.v.points->len
	);
}

static void cmd_hist_undo(void)
{
	struct cmd cmd;

	if (cmd_hist.cursor == cmd_hist.start)
		return;

	cmd_hist.cursor = RINGBUF_DECR(cmd_hist.cursor, ARRAY_SIZE(cmd_hist.cmds), 1);
	cmd = cmd_hist.cmds[cmd_hist.cursor];

	switch(cmd.type) {
	case CMD_STROKE_DELETE:
		cmd_stroke_create(cmd);
		break;
	case CMD_STROKE_CREATE:
		stroke_delete_last(objects->elems + objects->len-1);
		break;
	default: break;
	}
}

static void cmd_hist_redo(void)
{
	struct cmd cmd;

	if (cmd_hist.cursor == cmd_hist.end)
		return;

	cmd = cmd_hist.cmds[cmd_hist.cursor];
	cmd_hist.cursor = RINGBUF_INCR(cmd_hist.cursor, ARRAY_SIZE(cmd_hist.cmds), 1);

	switch(cmd.type) {
	case CMD_STROKE_DELETE:
		stroke_delete_last(objects->elems + objects->len-1);
		break;
	case CMD_STROKE_CREATE:
		cmd_stroke_create(cmd);
		break;
	default: break;
	}
}

/************ SOKOL APP ************/

void init(void) 
{
	objects = da_object_create(DA_INITIAL_CAPACITY);

	clear_color = CLEAR_COLOR_DEFAULT;
	screen_width = sapp_width();
	screen_height = sapp_height();
	gladLoaderLoadGL();
	vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
}

void cleanup(void)
{
	gladLoaderUnloadGL();
	nvgDeleteGL3(vg);
}

void event(const sapp_event *e)
{
	static bool ctrl_held = false;
	point pt;

	switch(e->type) {
	case SAPP_EVENTTYPE_MOUSE_MOVE:
	case SAPP_EVENTTYPE_MOUSE_DOWN:
	case SAPP_EVENTTYPE_MOUSE_UP:
		pt = (point) { screen_to_world((vec2){ e->mouse_x, e->mouse_y }), -1 };
		break;
	default: break;
	}

	switch(e->type) {
	case SAPP_EVENTTYPE_RESIZED:
		screen_width = sapp_width();
		screen_height = sapp_height();
		break;
	case SAPP_EVENTTYPE_KEY_DOWN:
		if (!ctrl_held)
			ctrl_held = (e->key_code == SAPP_KEYCODE_LEFT_CONTROL || e-> key_code == SAPP_KEYCODE_RIGHT_CONTROL);

		if (e->key_code == SAPP_KEYCODE_A && is_drawing_obj) {
			/* TODO: rethink this later */
			/* object_end(); */
			/* cmd_hist_record((struct cmd){CMD_OBJECT_END}); */
		}
		if (ctrl_held && e->key_code == SAPP_KEYCODE_Z)
			cmd_hist_undo();
		if (ctrl_held && e->key_code == SAPP_KEYCODE_R)
			cmd_hist_redo();
		break;
	case SAPP_EVENTTYPE_KEY_UP:
		if (ctrl_held)
			ctrl_held = !(e->key_code == SAPP_KEYCODE_LEFT_CONTROL || e-> key_code == SAPP_KEYCODE_RIGHT_CONTROL);
		break;
	case SAPP_EVENTTYPE_MOUSE_MOVE:
		mouse_screen.x = e->mouse_x;
		mouse_screen.y = e->mouse_y;
		mouse_world = screen_to_world(mouse_screen);

		if (is_panning) {
			camera.x = pan_pivot_camera.x + (pan_pivot_mouse.x - mouse_screen.x)/zoom;
			camera.y = pan_pivot_camera.y + (mouse_screen.y - pan_pivot_mouse.y)/zoom;
		}
		if (is_drawing_stroke) {
			stroke_append_point(objects->elems + objects->len-1, pt);
			cmd_curr.v.points = da_point_append(cmd_curr.v.points, pt);
		}
		break;
	case SAPP_EVENTTYPE_MOUSE_DOWN:
		if (e->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
			is_panning = true;
			pan_pivot_mouse = mouse_screen;
			pan_pivot_camera = camera;
		}
		if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
			if (!is_drawing_obj) {
				/* cmd_hist_record((struct cmd){CMD_OBJECT_BEGIN}); */
				object_begin();
			}

			stroke_start(objects->elems + objects->len-1);
			is_drawing_stroke = true;

			cmd_curr.type = CMD_STROKE_CREATE;
			cmd_curr.v.points = da_point_create(DA_INITIAL_CAPACITY);

			stroke_append_point( objects->elems + objects->len-1, pt);
			cmd_curr.v.points = da_point_append(cmd_curr.v.points, pt);
		}
		break;
	case SAPP_EVENTTYPE_MOUSE_UP:
		is_panning = is_panning && e->mouse_button != SAPP_MOUSEBUTTON_MIDDLE;
		if (is_drawing_stroke) {
			is_drawing_stroke = false;
			cmd_hist_record(cmd_curr);
		}
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

void draw_objects(void)
{
	int i, j, k;
	struct object *obj;

	for (i = 0; i < objects->len; i++) {
		obj = objects->elems + i;

		nvgBeginPath(vg);
		nvgFillColor(vg, nvgRGBA(204, 255, 0, 255));

		int curr_start = 0, next_start = 0;
		for (j = k = 0; j < obj->stroke_buf.len; j++) {
			const bool is_start = k < obj->stroke_starts->len && j == next_start;
			const pfh_vec2 p0 = obj->stroke_buf.elems[j];

			if (is_start) {
				nvgMoveTo(vg, p0.x, -p0.y);
				k++;
				curr_start = next_start;
				next_start = k < obj->stroke_starts->len 
					? obj->stroke_starts->elems[k] 
					: obj->stroke_buf.len;
			}

			const pfh_vec2 p1 = (j+1) == next_start
				? obj->stroke_buf.elems[curr_start] // apparently the last one needs to wrap back to start.
				: obj->stroke_buf.elems[j + 1];

			nvgQuadTo(vg, p0.x, -p0.y, (p0.x + p1.x) / 2, -(p0.y + p1.y) / 2);
		}
		nvgFill(vg);
	}
}

void frame(void) 
{
	float dpi = sapp_dpi_scale();

	glViewport(0, 0, screen_width, screen_height);
	glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	nvgBeginFrame(vg, screen_width/dpi, screen_height/dpi, dpi);
	nvgTranslate(vg, screen_width/2, screen_height/2);
	nvgScale(vg, zoom, zoom);
	nvgTranslate(vg, -camera.x, camera.y);

		nvgBeginPath(vg);
			nvgRect(vg, -25, -25, 50, 50);
		nvgFillColor(vg, nvgRGBA(255, 192, 0, 255));
		nvgFill(vg);

		draw_objects();
	nvgEndFrame(vg);
}

sapp_desc sokol_main(int argc, char* argv[]) {
	return (sapp_desc){
		.init_cb      = init,
		.frame_cb     = frame,
		.event_cb     = event,
		.cleanup_cb   = cleanup,
		.window_title = "Drawit",
		
	};
}

