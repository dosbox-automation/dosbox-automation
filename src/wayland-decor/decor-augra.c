// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// Adapted from libdecor's cairo plugin (gitlab.freedesktop.org/libdecor/libdecor)
// Copyright (c) Jonas Ådahl, Christian Rauch and libdecor contributors
// Licensed under MIT (retained below)
// Local changes: renamed *_cairo symbols to *_augra, own plugin description
// and priority, larger titlebar geometry.

/*
 * Copyright © 2018 Jonas Ådahl
 * Copyright © 2019 Christian Rauch
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <linux/input.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <wayland-cursor.h>

#include "libdecor-plugin.h"
#include "utils.h"
#include "desktop-settings.h"
#include "os-compatibility.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "common/libdecor-cairo-blur.h"

static const size_t SHADOW_MARGIN = 24;	/* graspable part of the border */
/* Taller bar and larger glyphs than upstream, title font sized
 * separately from the button symbols. Static fallbacks until sizes
 * derive from the desktop's font settings. */
static const size_t TITLE_HEIGHT = 36;
static const size_t BUTTON_WIDTH = 44;
static const size_t SYM_DIM = 17;
static const size_t TITLE_FONT_SIZE = 18;

/* Palette globals, not consts: set once at plugin init from the
 * desktop's color scheme. Values follow libadwaita's headerbar
 * colors (light #ffffff/#fafafa, dark #303030/#242424). Single
 * plugin instance per process, same pattern as upstream's globals. */
static uint32_t COL_TITLE = 0xFF303030;
static uint32_t COL_TITLE_INACT = 0xFF242424;
static uint32_t COL_BUTTON_MIN = 0xFFFFBB00;
static uint32_t COL_BUTTON_MAX = 0xFF238823;
static uint32_t COL_BUTTON_CLOSE = 0xFFFB6542;
static uint32_t COL_BUTTON_INACT = 0xFF404040;
static uint32_t COL_SYM = 0xFFFFFFFF;
static uint32_t COL_SYM_ACT = 0xFF20322A;
static uint32_t COL_SYM_INACT = 0xFF909090;

static void
apply_color_scheme(void)
{
	switch (libdecor_get_color_scheme()) {
	case LIBDECOR_COLOR_SCHEME_PREFER_DARK:
		/* dark palette is the initializer values */
		break;
	case LIBDECOR_COLOR_SCHEME_PREFER_LIGHT:
	case LIBDECOR_COLOR_SCHEME_DEFAULT:
		COL_TITLE = 0xFFFFFFFF;
		COL_TITLE_INACT = 0xFFFAFAFA;
		COL_BUTTON_INACT = 0xFFD5D5D5;
		COL_SYM = 0xFF333333;
		COL_SYM_ACT = 0xFF20322A;
		COL_SYM_INACT = 0xFF9A9A9A;
		break;
	}
}

static const uint32_t DOUBLE_CLICK_TIME_MS = 400;

static const char *cursor_names[] = {
	"top_side",
	"bottom_side",
	"left_side",
	"top_left_corner",
	"bottom_left_corner",
	"right_side",
	"top_right_corner",
	"bottom_right_corner"
};


/* color conversion function from 32bit integer to double components */

double
red(const uint32_t *const col) {
	return ((const uint8_t*)(col))[2] / (double)(255);
}

double
green(const uint32_t *const col) {
	return ((const uint8_t*)(col))[1] / (double)(255);
}

double
blue(const uint32_t *const col) {
	return ((const uint8_t*)(col))[0] / (double)(255);
}

double
alpha(const uint32_t *const col) {
	return ((const uint8_t*)(col))[3] / (double)(255);
}

void
cairo_set_rgba32(cairo_t *cr, const uint32_t *const c) {
	cairo_set_source_rgba(cr, red(c), green(c), blue(c), alpha(c));
}

static bool
streql(const char *str1, const char *str2)
{
	return (str1 && str2) && (strcmp(str1, str2) == 0);
}

enum decoration_type {
	DECORATION_TYPE_NONE,
	DECORATION_TYPE_ALL,
	DECORATION_TYPE_MAXIMIZED,
	DECORATION_TYPE_TILED
};

enum component {
	NONE = 0,
	SHADOW,
	TITLE,
	BUTTON_MIN,
	BUTTON_MAX,
	BUTTON_CLOSE,
};

enum composite_mode {
	COMPOSITE_SERVER,
	COMPOSITE_CLIENT,
};

struct seat {
	struct libdecor_plugin_augra *plugin_augra;

	char *name;

	struct wl_seat *wl_seat;
	struct wl_pointer *wl_pointer;

	struct wl_surface *cursor_surface;
	struct wl_cursor *current_cursor;
	int cursor_scale;
	struct wl_list cursor_outputs;

	struct wl_cursor_theme *cursor_theme;
	/* cursors for resize edges and corners */
	struct wl_cursor *cursors[ARRAY_LENGTH(cursor_names)];
	struct wl_cursor *cursor_left_ptr;

	struct wl_surface *pointer_focus;
	struct libdecor_frame_augra *pointer_focus_frame;

	int pointer_x, pointer_y;

	uint32_t pointer_button_time_stamp;

	uint32_t serial;

	bool grabbed;

	struct wl_list link;
};

struct output {
	struct libdecor_plugin_augra *plugin_augra;

	struct wl_output *wl_output;
	uint32_t id;
	int scale;

	struct wl_list link;
};

struct buffer {
	struct wl_buffer *wl_buffer;
	bool in_use;
	bool is_detached;

	void *data;
	size_t data_size;
	int width;
	int height;
	int scale;
	int buffer_width;
	int buffer_height;
};

struct border_component {
	enum component type;

	bool is_hidden;
	bool opaque;

	enum composite_mode composite_mode;
	struct {
		struct wl_surface *wl_surface;
		struct wl_subsurface *wl_subsurface;
		struct buffer *buffer;
		struct wl_list output_list;
		int scale;
	} server;
	struct {
		cairo_surface_t *image;
		struct border_component *parent_component;
	} client;

	struct wl_list child_components; /* border_component::link */
	struct wl_list link; /* border_component::child_components */
};

struct surface_output {
	struct output *output;
	struct wl_list link;
};

struct cursor_output {
	struct output *output;
	struct wl_list link;
};

struct libdecor_frame_augra {
	struct libdecor_frame frame;

	struct libdecor_plugin_augra *plugin_augra;

	int content_width;
	int content_height;

	enum decoration_type decoration_type;

	enum libdecor_window_state window_state;

	char *title;

	enum libdecor_capabilities capabilities;

	struct border_component *focus;
	struct border_component *active;
	struct border_component *grab;

	bool shadow_showing;
	struct border_component shadow;

	struct {
		bool is_showing;
		struct border_component title;
		struct border_component min;
		struct border_component max;
		struct border_component close;
	} title_bar;

	/* store pre-processed shadow tile */
	cairo_surface_t *shadow_blur;

	struct wl_list link;
};

struct libdecor_plugin_augra {
	struct libdecor_plugin plugin;

	struct wl_callback *globals_callback;
	struct wl_callback *globals_callback_shm;

	struct libdecor *context;

	struct wl_registry *wl_registry;
	struct wl_subcompositor *wl_subcompositor;
	struct wl_compositor *wl_compositor;

	struct wl_shm *wl_shm;
	struct wl_callback *shm_callback;
	bool has_argb;

	struct wl_list visible_frame_list;
	struct wl_list seat_list;
	struct wl_list output_list;

	char *cursor_theme_name;
	int cursor_size;

	PangoFontDescription *font;

	cairo_surface_t *app_icon;

	bool handle_cursor;
};

static const char *libdecor_cairo_proxy_tag = "libdecor-cairo";

static void
sync_active_component(struct libdecor_frame_augra *frame_augra,
		      struct seat *seat);

static void
synthesize_pointer_enter(struct seat *seat);

static void
synthesize_pointer_leave(struct seat *seat);

static bool
own_proxy(struct wl_proxy *proxy)
{
	if (!proxy)
		return false;

	return (wl_proxy_get_tag(proxy) == &libdecor_cairo_proxy_tag);
}

static bool
own_surface(struct wl_surface *surface)
{
	return own_proxy((struct wl_proxy *) surface);
}

static bool
own_output(struct wl_output *output)
{
	return own_proxy((struct wl_proxy *) output);
}

static bool
moveable(struct libdecor_frame_augra *frame_augra) {
	return libdecor_frame_has_capability(&frame_augra->frame,
					     LIBDECOR_ACTION_MOVE);
}

static bool
resizable(struct libdecor_frame_augra *frame_augra) {
	return libdecor_frame_has_capability(&frame_augra->frame,
					     LIBDECOR_ACTION_RESIZE);
}

static bool
minimizable(struct libdecor_frame_augra *frame_augra) {
	return libdecor_frame_has_capability(&frame_augra->frame,
					     LIBDECOR_ACTION_MINIMIZE);
}

static bool
closeable(struct libdecor_frame_augra *frame_augra) {
	return libdecor_frame_has_capability(&frame_augra->frame,
					     LIBDECOR_ACTION_CLOSE);
}

static void
buffer_free(struct buffer *buffer);

static void
draw_border_component(struct libdecor_frame_augra *frame_augra,
		      struct border_component *border_component);

static void
send_cursor(struct seat *seat);

static bool
update_local_cursor(struct seat *seat);

static void
libdecor_plugin_cairo_destroy(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_augra *plugin_augra =
		(struct libdecor_plugin_augra *) plugin;
	struct seat *seat, *seat_tmp;
	struct output *output, *output_tmp;
	struct libdecor_frame_augra *frame, *frame_tmp;

	if (plugin_augra->globals_callback)
		wl_callback_destroy(plugin_augra->globals_callback);
	if (plugin_augra->globals_callback_shm)
		wl_callback_destroy(plugin_augra->globals_callback_shm);
	if (plugin_augra->shm_callback)
		wl_callback_destroy(plugin_augra->shm_callback);
	wl_registry_destroy(plugin_augra->wl_registry);

	wl_list_for_each_safe(seat, seat_tmp, &plugin_augra->seat_list, link) {
		struct cursor_output *cursor_output, *tmp;

		if (seat->wl_pointer)
			wl_pointer_destroy(seat->wl_pointer);
		if (seat->cursor_surface)
			wl_surface_destroy(seat->cursor_surface);
		wl_seat_destroy(seat->wl_seat);
		if (seat->cursor_theme)
			wl_cursor_theme_destroy(seat->cursor_theme);

		wl_list_for_each_safe(cursor_output, tmp, &seat->cursor_outputs, link) {
			wl_list_remove(&cursor_output->link);
			free(cursor_output);
		}
		free(seat->name);

		free(seat);
	}

	wl_list_for_each_safe(output, output_tmp,
			      &plugin_augra->output_list, link) {
		if (wl_output_get_version (output->wl_output) >=
		    WL_OUTPUT_RELEASE_SINCE_VERSION)
			wl_output_release(output->wl_output);
		else
			wl_output_destroy(output->wl_output);
		free(output);
	}

	wl_list_for_each_safe(frame, frame_tmp,
			      &plugin_augra->visible_frame_list, link) {
		wl_list_remove(&frame->link);
	}

	free(plugin_augra->cursor_theme_name);

	if (plugin_augra->wl_shm)
		wl_shm_destroy(plugin_augra->wl_shm);

	pango_font_description_free(plugin_augra->font);

	if (plugin_augra->app_icon)
		cairo_surface_destroy(plugin_augra->app_icon);

	if (plugin_augra->wl_compositor)
		wl_compositor_destroy(plugin_augra->wl_compositor);
	if (plugin_augra->wl_subcompositor)
		wl_subcompositor_destroy(plugin_augra->wl_subcompositor);

	libdecor_plugin_release(&plugin_augra->plugin);
	free(plugin_augra);
}

static void
init_server_component(struct border_component *border_component,
		      enum component type)
{
	border_component->composite_mode = COMPOSITE_SERVER;
	wl_list_init(&border_component->child_components);
	border_component->type = type;
}

static void
init_client_component(struct border_component *border_component,
		      struct border_component *parent,
		      enum component type)
{
	border_component->composite_mode = COMPOSITE_CLIENT;
	wl_list_init(&border_component->child_components);
	wl_list_insert(parent->child_components.prev, &border_component->link);
	border_component->client.parent_component = parent;
	border_component->type = type;
}

static void
init_components(struct libdecor_frame_augra *frame_augra)
{
	init_server_component(&frame_augra->title_bar.title,
			      TITLE);
	init_client_component(&frame_augra->title_bar.min,
			      &frame_augra->title_bar.title,
			      BUTTON_MIN);
	init_client_component(&frame_augra->title_bar.max,
			      &frame_augra->title_bar.title,
			      BUTTON_MAX);
	init_client_component(&frame_augra->title_bar.close,
			      &frame_augra->title_bar.title,
			      BUTTON_CLOSE);
	init_server_component(&frame_augra->shadow,
			      SHADOW);
}

static struct libdecor_frame_augra *
libdecor_frame_cairo_new(struct libdecor_plugin_augra *plugin_augra)
{
	struct libdecor_frame_augra *frame_augra = zalloc(sizeof *frame_augra);
	cairo_t *cr;

	static const int size = 128;
	static const int boundary = 32;

	frame_augra->plugin_augra = plugin_augra;
	frame_augra->shadow_blur = cairo_image_surface_create(
					CAIRO_FORMAT_ARGB32, size, size);
	wl_list_insert(&plugin_augra->visible_frame_list, &frame_augra->link);

	init_components(frame_augra);

	cr = cairo_create(frame_augra->shadow_blur);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_rectangle(cr, boundary, boundary, size-2*boundary, size-2*boundary);
	cairo_fill(cr);
	cairo_destroy(cr);
	blur_surface(frame_augra->shadow_blur, 64);

	return frame_augra;
}

static int
libdecor_plugin_cairo_get_fd(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_augra *plugin_augra =
		(struct libdecor_plugin_augra *) plugin;
	struct wl_display *wl_display =
		libdecor_get_wl_display(plugin_augra->context);

	return wl_display_get_fd(wl_display);
}

static int
libdecor_plugin_cairo_dispatch(struct libdecor_plugin *plugin,
			       int timeout)
{
	struct libdecor_plugin_augra *plugin_augra =
		(struct libdecor_plugin_augra *) plugin;
	struct wl_display *wl_display =
		libdecor_get_wl_display(plugin_augra->context);
	struct pollfd fds[1];
	int ret;
	int dispatch_count = 0;

	while (wl_display_prepare_read(wl_display) != 0)
		dispatch_count += wl_display_dispatch_pending(wl_display);

	if (wl_display_flush(wl_display) < 0 &&
	    errno != EAGAIN) {
		wl_display_cancel_read(wl_display);
		return -errno;
	}

	fds[0] = (struct pollfd) { wl_display_get_fd(wl_display), POLLIN };

	ret = poll(fds, ARRAY_SIZE (fds), timeout);
	if (ret > 0) {
		if (fds[0].revents & POLLIN) {
			wl_display_read_events(wl_display);
			dispatch_count += wl_display_dispatch_pending(wl_display);
			return dispatch_count;
		} else {
			wl_display_cancel_read(wl_display);
			return dispatch_count;
		}
	} else if (ret == 0) {
		wl_display_cancel_read(wl_display);
		return dispatch_count;
	} else {
		wl_display_cancel_read(wl_display);
		return -errno;
	}
}

static void
libdecor_plugin_cairo_set_handle_application_cursor(struct libdecor_plugin *plugin,
						    bool handle_cursor)
{
	struct libdecor_plugin_augra *plugin_augra =
		(struct libdecor_plugin_augra *) plugin;

	plugin_augra->handle_cursor = handle_cursor;
}

static struct libdecor_frame *
libdecor_plugin_cairo_frame_new(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_augra *plugin_augra =
		(struct libdecor_plugin_augra *) plugin;
	struct libdecor_frame_augra *frame_augra;

	frame_augra = libdecor_frame_cairo_new(plugin_augra);

	return &frame_augra->frame;
}

static void
toggle_maximized(struct libdecor_frame *const frame)
{
	if (!resizable((struct libdecor_frame_augra *)frame))
		return;

	if (!(libdecor_frame_get_window_state(frame) &
	      LIBDECOR_WINDOW_STATE_MAXIMIZED))
		libdecor_frame_set_maximized(frame);
	else
		libdecor_frame_unset_maximized(frame);
}

static void
buffer_release(void *user_data,
	       struct wl_buffer *wl_buffer)
{
	struct buffer *buffer = user_data;

	if (buffer->is_detached)
		buffer_free(buffer);
	else
		buffer->in_use = false;
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static struct buffer *
create_shm_buffer(struct libdecor_plugin_augra *plugin_augra,
		  int width,
		  int height,
		  bool opaque,
		  int scale)
{
	struct wl_shm_pool *pool;
	int fd, size, buffer_width, buffer_height, stride;
	void *data;
	struct buffer *buffer;
	enum wl_shm_format buf_fmt;

	buffer_width = width * scale;
	buffer_height = height * scale;
	stride = buffer_width * 4;
	size = stride * buffer_height;

	fd = libdecor_os_create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n",
			size, strerror(errno));
		return NULL;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}

	buf_fmt = opaque ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888;

	pool = wl_shm_create_pool(plugin_augra->wl_shm, fd, size);
	buffer = zalloc(sizeof *buffer);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
						      buffer_width, buffer_height,
						      stride,
						      buf_fmt);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->data = data;
	buffer->data_size = size;
	buffer->width = width;
	buffer->height = height;
	buffer->scale = scale;
	buffer->buffer_width = buffer_width;
	buffer->buffer_height = buffer_height;

	return buffer;
}

static void
buffer_free(struct buffer *buffer)
{
	if (buffer->wl_buffer) {
		wl_buffer_destroy(buffer->wl_buffer);
		munmap(buffer->data, buffer->data_size);
		buffer->wl_buffer = NULL;
		buffer->in_use = false;
	}
	free(buffer);
}

static void
free_border_component(struct border_component *border_component)
{
	struct surface_output *surface_output, *surface_output_tmp;

	if (border_component->server.wl_surface) {
		wl_subsurface_destroy(border_component->server.wl_subsurface);
		border_component->server.wl_subsurface = NULL;
		wl_surface_destroy(border_component->server.wl_surface);
		border_component->server.wl_surface = NULL;
	}
	if (border_component->server.buffer) {
		buffer_free(border_component->server.buffer);
		border_component->server.buffer = NULL;
	}
	if (border_component->client.image) {
		cairo_surface_destroy(border_component->client.image);
		border_component->client.image = NULL;
	}
	if (border_component->server.output_list.next != NULL) {
		wl_list_for_each_safe(surface_output, surface_output_tmp,
				      &border_component->server.output_list, link) {
			wl_list_remove(&surface_output->link);
			free(surface_output);
		}
	}
}

static void
libdecor_plugin_cairo_frame_free(struct libdecor_plugin *plugin,
				 struct libdecor_frame *frame)
{
	struct libdecor_plugin_augra *plugin_augra =
		(struct libdecor_plugin_augra *) plugin;
	struct libdecor_frame_augra *frame_augra =
		(struct libdecor_frame_augra *) frame;
	struct seat *seat;

	wl_list_for_each(seat, &plugin_augra->seat_list, link) {
		if (seat->pointer_focus) {
		    if (wl_surface_get_user_data(seat->pointer_focus) == frame_augra)
			    seat->pointer_focus = NULL;
		    if (seat->pointer_focus_frame == frame_augra)
			    seat->pointer_focus_frame = NULL;
		}
	}

	free_border_component(&frame_augra->title_bar.title);
	free_border_component(&frame_augra->title_bar.min);
	free_border_component(&frame_augra->title_bar.max);
	free_border_component(&frame_augra->title_bar.close);
	frame_augra->title_bar.is_showing = false;
	free_border_component(&frame_augra->shadow);
	frame_augra->shadow_showing = false;
	if (frame_augra->shadow_blur != NULL) {
		cairo_surface_destroy(frame_augra->shadow_blur);
		frame_augra->shadow_blur = NULL;
	}

	free(frame_augra->title);
	frame_augra->title = NULL;

	frame_augra->decoration_type = DECORATION_TYPE_NONE;

	if (frame_augra->link.next != NULL)
		wl_list_remove(&frame_augra->link);
}

static bool
is_border_surfaces_showing(struct libdecor_frame_augra *frame_augra)
{
	return frame_augra->shadow_showing;
}

static bool
is_title_bar_surfaces_showing(struct libdecor_frame_augra *frame_augra)
{
	return frame_augra->title_bar.is_showing;
}

static struct border_component *
get_server_component(struct border_component *border_component)
{
	switch (border_component->composite_mode) {
	case COMPOSITE_SERVER:
		return border_component;
	case COMPOSITE_CLIENT:
		return get_server_component(border_component->client.parent_component);
	}
	return NULL;
}

static void
redraw_border_component(struct libdecor_frame_augra *frame_augra,
			struct border_component *border_component)
{
	struct border_component *server_component;

	server_component = get_server_component(border_component);
	draw_border_component(frame_augra, server_component);
}

static void
hide_border_component(struct libdecor_frame_augra *frame_augra,
		      struct border_component *border_component)
{
	border_component->is_hidden = true;

	switch (border_component->composite_mode) {
	case COMPOSITE_SERVER:
		if (!border_component->server.wl_surface)
			return;

		wl_surface_attach(border_component->server.wl_surface,
				  NULL, 0, 0);
		wl_surface_commit(border_component->server.wl_surface);
		break;
	case COMPOSITE_CLIENT:
		redraw_border_component(frame_augra, border_component);
		break;
	}
}

static void
hide_border_surfaces(struct libdecor_frame_augra *frame_augra)
{
	hide_border_component(frame_augra, &frame_augra->shadow);
	frame_augra->shadow_showing = false;
}

static void
hide_title_bar_surfaces(struct libdecor_frame_augra *frame_augra)
{
	hide_border_component(frame_augra, &frame_augra->title_bar.title);
	hide_border_component(frame_augra, &frame_augra->title_bar.min);
	hide_border_component(frame_augra, &frame_augra->title_bar.max);
	hide_border_component(frame_augra, &frame_augra->title_bar.close);
	frame_augra->title_bar.is_showing = false;
}

static struct border_component *
get_component_for_surface(struct libdecor_frame_augra *frame_augra,
			  struct wl_surface *surface)
{
	if (frame_augra->shadow.server.wl_surface == surface)
		return &frame_augra->shadow;
	if (frame_augra->title_bar.title.server.wl_surface == surface)
		return &frame_augra->title_bar.title;
	return NULL;
}

static void
calculate_component_size(struct libdecor_frame_augra *frame_augra,
			 enum component component,
			 int *component_x,
			 int *component_y,
			 int *component_width,
			 int *component_height);

static void
update_component_focus(struct libdecor_frame_augra *frame_augra,
		       struct wl_surface *surface,
		       struct seat *seat)
{
	static struct border_component *border_component;
	static struct border_component *child_component;
	static struct border_component *focus_component;

	border_component = get_component_for_surface(frame_augra, surface);
	if (!border_component) {
		focus_component = NULL;
		goto out;
	}

	focus_component = border_component;
	wl_list_for_each(child_component, &border_component->child_components, link) {
		int component_x = 0, component_y = 0;
		int component_width = 0, component_height = 0;

		calculate_component_size(frame_augra, child_component->type,
					 &component_x, &component_y,
					 &component_width, &component_height);
		if (seat->pointer_x >= component_x &&
		    seat->pointer_x < component_x + component_width &&
		    seat->pointer_y >= component_y &&
		    seat->pointer_y < component_y + component_height) {
			focus_component = child_component;
			break;
		}
	}

out:
	if (frame_augra->grab)
		frame_augra->active = frame_augra->grab;
	else
		frame_augra->active = focus_component;
	frame_augra->focus = focus_component;

}

static void
ensure_component(struct libdecor_frame_augra *frame_augra,
		 struct border_component *cmpnt);

static bool
redraw_scale(struct libdecor_frame_augra *frame_augra,
	     struct border_component *cmpnt)
{
	struct surface_output *surface_output;
	int scale = 1;

	if (cmpnt->is_hidden)
		return false;

	ensure_component(frame_augra, cmpnt);

	wl_list_for_each(surface_output, &cmpnt->server.output_list, link) {
		scale = MAX(scale, surface_output->output->scale);
	}
	if (scale != cmpnt->server.scale) {
		cmpnt->server.scale = scale;
		if ((cmpnt->type != SHADOW) || is_border_surfaces_showing(frame_augra)) {
			draw_border_component(frame_augra, cmpnt);
			return true;
		}
	}
	return false;
}

static bool
add_surface_output(struct libdecor_plugin_augra *plugin_augra,
		   struct wl_output *wl_output,
		   struct wl_list *list)
{
	struct output *output;
	struct surface_output *surface_output;

	if (!own_output(wl_output))
		return false;

	output = wl_output_get_user_data(wl_output);

	if (output == NULL)
		return false;

	surface_output = zalloc(sizeof *surface_output);
	surface_output->output = output;
	wl_list_insert(list, &surface_output->link);
	return true;
}

static void
surface_enter(void *data,
	      struct wl_surface *wl_surface,
	      struct wl_output *wl_output)
{
	struct libdecor_frame_augra *frame_augra = data;
	struct border_component *cmpnt;

	if (!(own_surface(wl_surface) && own_output(wl_output)))
	    return;

	cmpnt = get_component_for_surface(frame_augra, wl_surface);
	if (cmpnt == NULL)
		return;

	if (!add_surface_output(frame_augra->plugin_augra, wl_output,
				&cmpnt->server.output_list))
		return;

	if (redraw_scale(frame_augra, cmpnt))
		libdecor_frame_toplevel_commit(&frame_augra->frame);
}

static bool
remove_surface_output(struct wl_list *list, struct wl_output *wl_output)
{
	struct surface_output *surface_output;
	wl_list_for_each(surface_output, list, link) {
		if (surface_output->output->wl_output == wl_output) {
			wl_list_remove(&surface_output->link);
			free(surface_output);
			return true;
		}
	}
	return false;
}

static void
surface_leave(void *data,
	      struct wl_surface *wl_surface,
	      struct wl_output *wl_output)
{
	struct libdecor_frame_augra *frame_augra = data;
	struct border_component *cmpnt;

	if (!(own_surface(wl_surface) && own_output(wl_output)))
	    return;

	cmpnt = get_component_for_surface(frame_augra, wl_surface);
	if (cmpnt == NULL)
		return;

	if (!remove_surface_output(&cmpnt->server.output_list, wl_output))
		return;

	if (redraw_scale(frame_augra, cmpnt))
		libdecor_frame_toplevel_commit(&frame_augra->frame);
}

static struct wl_surface_listener surface_listener = {
	surface_enter,
	surface_leave,
};

static void
create_surface_subsurface_pair(struct libdecor_frame_augra *frame_augra,
			       struct wl_surface **out_wl_surface,
			       struct wl_subsurface **out_wl_subsurface)
{
	struct libdecor_plugin_augra *plugin_augra = frame_augra->plugin_augra;
	struct libdecor_frame *frame = &frame_augra->frame;
	struct wl_compositor *wl_compositor = plugin_augra->wl_compositor;
	struct wl_subcompositor *wl_subcompositor = plugin_augra->wl_subcompositor;
	struct wl_surface *wl_surface;
	struct wl_surface *parent;
	struct wl_subsurface *wl_subsurface;

	wl_surface = wl_compositor_create_surface(wl_compositor);
	wl_proxy_set_tag((struct wl_proxy *) wl_surface,
			 &libdecor_cairo_proxy_tag);

	parent = libdecor_frame_get_wl_surface(frame);
	wl_subsurface = wl_subcompositor_get_subsurface(wl_subcompositor,
							wl_surface,
							parent);

	*out_wl_surface = wl_surface;
	*out_wl_subsurface = wl_subsurface;
}

static void
ensure_component(struct libdecor_frame_augra *frame_augra,
		 struct border_component *cmpnt)
{
	switch (cmpnt->composite_mode) {
	case COMPOSITE_SERVER:
		if (!cmpnt->server.wl_surface) {
			wl_list_init(&cmpnt->server.output_list);
			cmpnt->server.scale = 1;
			create_surface_subsurface_pair(frame_augra,
						       &cmpnt->server.wl_surface,
						       &cmpnt->server.wl_subsurface);
			wl_surface_add_listener(cmpnt->server.wl_surface,
						&surface_listener,
						frame_augra);
		}
		break;
	case COMPOSITE_CLIENT:
			wl_list_init(&cmpnt->server.output_list);
		break;
	}

	cmpnt->is_hidden = false;
}

static void
ensure_border_surfaces(struct libdecor_frame_augra *frame_augra)
{
	int min_width, min_height, current_max_w, current_max_h;

	frame_augra->shadow.opaque = false;
	ensure_component(frame_augra, &frame_augra->shadow);

	libdecor_frame_get_min_content_size(&frame_augra->frame,
					    &min_width, &min_height);
	min_width = MAX(min_width, (int)MAX(56, 4 * BUTTON_WIDTH));
	min_height = MAX(min_height, (int)MAX(56, TITLE_HEIGHT + 1));
	libdecor_frame_set_min_content_size(&frame_augra->frame, min_width, min_height);
	libdecor_frame_get_max_content_size(&frame_augra->frame, &current_max_w, 
		&current_max_h);
	if (current_max_w && current_max_w < min_width) current_max_w = min_width;
	if (current_max_h && current_max_h < min_height) current_max_h = min_height;
	libdecor_frame_set_max_content_size(&frame_augra->frame, current_max_w, 
		current_max_h);
}


static void
ensure_title_bar_surfaces(struct libdecor_frame_augra *frame_augra)
{
	frame_augra->title_bar.title.opaque = true;
	ensure_component(frame_augra, &frame_augra->title_bar.title);

	frame_augra->title_bar.min.opaque = true;
	ensure_component(frame_augra, &frame_augra->title_bar.min);

	frame_augra->title_bar.max.opaque = true;
	ensure_component(frame_augra, &frame_augra->title_bar.max);

	frame_augra->title_bar.close.opaque = true;
	ensure_component(frame_augra, &frame_augra->title_bar.close);
}

static void
calculate_component_size(struct libdecor_frame_augra *frame_augra,
			 enum component component,
			 int *component_x,
			 int *component_y,
			 int *component_width,
			 int *component_height)
{
	struct libdecor_frame *frame = &frame_augra->frame;
	int content_width, content_height;

	content_width = libdecor_frame_get_content_width(frame);
	content_height = libdecor_frame_get_content_height(frame);

	switch (component) {
	case NONE:
		*component_width = 0;
		*component_height = 0;
		return;
	case SHADOW:
		*component_x = -(int)SHADOW_MARGIN;
		*component_y = -(int)(SHADOW_MARGIN+TITLE_HEIGHT);
		*component_width = content_width + 2 * SHADOW_MARGIN;
		*component_height = content_height
				    + 2 * SHADOW_MARGIN
				    + TITLE_HEIGHT;
		return;
	case TITLE:
		*component_x = 0;
		*component_y = -(int)TITLE_HEIGHT;
		*component_width = content_width;
		*component_height = TITLE_HEIGHT;
		return;
	case BUTTON_MIN:
		*component_x = content_width - 3 * BUTTON_WIDTH;
		*component_y = 0;
		*component_width = BUTTON_WIDTH;
		*component_height = TITLE_HEIGHT;
		return;
	case BUTTON_MAX:
		*component_x = content_width - 2 * BUTTON_WIDTH;
		*component_y = 0;
		*component_width = BUTTON_WIDTH;
		*component_height = TITLE_HEIGHT;
		return;
	case BUTTON_CLOSE:
		*component_x = content_width - BUTTON_WIDTH;
		*component_y = 0;
		*component_width = BUTTON_WIDTH;
		*component_height = TITLE_HEIGHT;
		return;
	}

	abort();
}

static int
border_component_get_scale(struct border_component *border_component)
{
	switch (border_component->composite_mode) {
	case COMPOSITE_SERVER:
		return border_component->server.scale;
	case COMPOSITE_CLIENT:
		return border_component_get_scale(
				border_component->client.parent_component);
	}
	return 0;
}

static void
draw_title_text(struct libdecor_frame_augra *frame_augra,
		cairo_t *cr,
		const int *title_width,
		bool active)
{
	const uint32_t col_title = active ? COL_TITLE : COL_TITLE_INACT;
	const uint32_t col_title_text = active ? COL_SYM : COL_SYM_INACT;

	PangoLayout *layout;

	/* title fade out at buttons */
	const int fade_width = 5 * BUTTON_WIDTH;
	int fade_start;
	cairo_pattern_t *fade;

	/* text position and dimensions */
	int text_extents_width, text_extents_height;
	double text_x, text_y;
	double text_width, text_height;

	const char *title;

	title = libdecor_frame_get_title((struct libdecor_frame*) frame_augra);
	if (!title)
		return;

	layout = pango_cairo_create_layout(cr);

	pango_layout_set_text(layout,
			      title,
			      -1);
	pango_layout_set_font_description(layout, frame_augra->plugin_augra->font);
	pango_layout_get_size(layout, &text_extents_width, &text_extents_height);

	/* set text position and dimensions */
	text_width = text_extents_width / PANGO_SCALE;
	text_height = text_extents_height / PANGO_SCALE;
	text_x = *title_width / 2.0 - text_width / 2.0;
	text_x += MIN(0.0, ((*title_width - fade_width) - (text_x + text_width)));
	text_x = MAX(text_x, BUTTON_WIDTH);
	/* one pixel above geometric center; titles with few descenders
	 * read as sitting low when centered on the layout box */
	text_y = TITLE_HEIGHT / 2.0 - text_height / 2.0 - 1.0;

	/* draw title text */
	cairo_move_to(cr, text_x, text_y);
	cairo_set_rgba32(cr, &col_title_text);
	pango_cairo_show_layout(cr, layout);

	/* app icon at the left edge, vertically centered */
	if (frame_augra->plugin_augra->app_icon) {
		cairo_surface_t *icon = frame_augra->plugin_augra->app_icon;
		const double icon_dim = TITLE_HEIGHT - 6.0;
		const double icon_x = 10.0;
		/* below geometric center; tuned by eye against the title
		 * text on GNOME (2026-08-23) */
		const double icon_y = (TITLE_HEIGHT - icon_dim) / 2.0 + 2.0;
		double src_w = cairo_image_surface_get_width(icon);
		double src_h = cairo_image_surface_get_height(icon);

		if (src_w > 0 && src_h > 0) {
			cairo_save(cr);
			cairo_translate(cr, icon_x, icon_y);
			cairo_scale(cr, icon_dim / src_w, icon_dim / src_h);
			cairo_set_source_surface(cr, icon, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
		}
	}

	/* draw fade-out from title text to buttons */
	fade_start = *title_width - fade_width;
	fade = cairo_pattern_create_linear(fade_start, 0,
					   fade_start + 2 * BUTTON_WIDTH, 0);
	cairo_pattern_add_color_stop_rgba(fade, 0,
					  red(&col_title),
					  green(&col_title),
					  blue(&col_title),
					  0);
	cairo_pattern_add_color_stop_rgb(fade, 1,
					 red(&col_title),
					 green(&col_title),
					 blue(&col_title));
	cairo_rectangle(cr, fade_start, 0, fade_width, TITLE_HEIGHT);
	cairo_set_source(cr, fade);
	cairo_fill(cr);

	cairo_pattern_destroy(fade);
	g_object_unref(layout);
}

static void
draw_component_content(struct libdecor_frame_augra *frame_augra,
		       struct border_component *border_component,
		       int component_width,
		       int component_height,
		       enum component component)
{
	struct buffer *buffer;
	cairo_surface_t *surface = NULL;
	int width = 0, height = 0;
	int scale;
	cairo_t *cr;

	/* button symbol origin */
	const double x = BUTTON_WIDTH / 2 - SYM_DIM / 2 + 0.5;
	const double y = TITLE_HEIGHT / 2 - SYM_DIM / 2 + 0.5;

	enum libdecor_window_state state;

	bool active;

	uint32_t col_title;

	bool cap_min, cap_max, cap_close;

	/* capabilities of decorations */
	cap_min = minimizable(frame_augra);
	cap_max = resizable(frame_augra);
	cap_close = closeable(frame_augra);

	scale = border_component_get_scale(border_component);

	state = libdecor_frame_get_window_state((struct libdecor_frame *) frame_augra);

	active = state & LIBDECOR_WINDOW_STATE_ACTIVE;

	col_title = active ? COL_TITLE : COL_TITLE_INACT;

	/* clear buffer */
	switch (border_component->composite_mode) {
	case COMPOSITE_SERVER:
		buffer = border_component->server.buffer;

		surface = cairo_image_surface_create_for_data(
				  buffer->data, CAIRO_FORMAT_ARGB32,
				  buffer->buffer_width, buffer->buffer_height,
				  cairo_format_stride_for_width(
					  CAIRO_FORMAT_ARGB32,
					  buffer->buffer_width)
				  );
		cairo_surface_set_device_scale(surface, scale, scale);
		width = buffer->width;
		height = buffer->height;
		break;
	case COMPOSITE_CLIENT:
		surface = cairo_surface_reference(border_component->client.image);
		width = cairo_image_surface_get_width(surface);
		height = cairo_image_surface_get_height(surface);
		break;
	}

	cr = cairo_create(surface);
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);

	/* background */
	switch (component) {
	case NONE:
		break;
	case SHADOW:
		if (frame_augra->decoration_type != DECORATION_TYPE_TILED)
			render_shadow(cr,
				      frame_augra->shadow_blur,
				      -(int)SHADOW_MARGIN/2,
				      -(int)SHADOW_MARGIN/2,
				      width + SHADOW_MARGIN,
				      height + SHADOW_MARGIN,
				      64,
				      64);
		break;
	case TITLE:
		cairo_set_rgba32(cr, &col_title);
		cairo_paint(cr);
		break;
	case BUTTON_MIN:
		if (cap_min && frame_augra->active == &frame_augra->title_bar.min)
			cairo_set_rgba32(cr, active ? &COL_BUTTON_MIN : &COL_BUTTON_INACT);
		else
			cairo_set_rgba32(cr, &col_title);
		cairo_paint(cr);
		break;
	case BUTTON_MAX:
		if (cap_max && frame_augra->active == &frame_augra->title_bar.max)
			cairo_set_rgba32(cr, active ? &COL_BUTTON_MAX : &COL_BUTTON_INACT);
		else
			cairo_set_rgba32(cr, &col_title);
		cairo_paint(cr);
		break;
	case BUTTON_CLOSE:
		if (cap_close && frame_augra->active == &frame_augra->title_bar.close)
			cairo_set_rgba32(cr, active ? &COL_BUTTON_CLOSE : &COL_BUTTON_INACT);
		else
			cairo_set_rgba32(cr, &col_title);
		cairo_paint(cr);
		break;
	}

	/* button symbols */
	/* https://www.cairographics.org/FAQ/#sharp_lines */
	cairo_set_line_width(cr, 1);

	switch (component) {
	case TITLE:
		draw_title_text(frame_augra,cr, &component_width, active);
		break;
	case BUTTON_MIN:
		if (!active) {
			/* inactive: use single desaturated color */
			cairo_set_rgba32(cr, &COL_SYM_INACT);
		} else {
			if (!cap_min ||
			    frame_augra->active == &frame_augra->title_bar.min) {
				/* active (a.k.a. prelight) */
				cairo_set_rgba32(cr, &COL_SYM_ACT);
			} else {
				/* normal */
				cairo_set_rgba32(cr, &COL_SYM);
			}
		}
		cairo_move_to(cr, x, y + SYM_DIM - 1);
		cairo_rel_line_to(cr, SYM_DIM - 1, 0);
		cairo_stroke(cr);
		break;
	case BUTTON_MAX:
		if (!active) {
			/* inactive: use single desaturated color */
			cairo_set_rgba32(cr, &COL_SYM_INACT);
		} else {
			if (!cap_max ||
			    frame_augra->active == &frame_augra->title_bar.max) {
				/* active (a.k.a. prelight) */
				cairo_set_rgba32(cr, &COL_SYM_ACT);
			} else {
				/* normal */
				cairo_set_rgba32(cr, &COL_SYM);
			}
		}

		if (state & LIBDECOR_WINDOW_STATE_MAXIMIZED) {
			const size_t small = 12;
			cairo_rectangle(cr,
					x,
					y + SYM_DIM - small,
					small - 1,
					small - 1);
			cairo_move_to(cr,
				      x + SYM_DIM - small,
				      y + SYM_DIM - small);
			cairo_line_to(cr, x + SYM_DIM - small, y);
			cairo_rel_line_to(cr, small - 1, 0);
			cairo_rel_line_to(cr, 0, small - 1);
			cairo_line_to(cr, x + small - 1, y + small - 1);
		} else {
			cairo_rectangle(cr, x, y, SYM_DIM - 1, SYM_DIM - 1);
		}
		cairo_stroke(cr);
		break;
	case BUTTON_CLOSE:
		if (!active) {
			/* inactive: use single desaturated color */
			cairo_set_rgba32(cr, &COL_SYM_INACT);
		} else {
			if (!cap_close ||
			    frame_augra->active == &frame_augra->title_bar.close) {
				/* active (a.k.a. prelight) */
				cairo_set_rgba32(cr, &COL_SYM_ACT);
			} else {
				/* normal */
				cairo_set_rgba32(cr, &COL_SYM);
			}
		}
		cairo_move_to(cr, x, y);
		cairo_rel_line_to(cr, SYM_DIM - 1, SYM_DIM - 1);
		cairo_move_to(cr, x + SYM_DIM - 1, y);
		cairo_line_to(cr, x, y + SYM_DIM - 1);
		cairo_stroke(cr);
		break;
	default:
		break;
	}

	/* mask the toplevel surface */
	if (component == SHADOW) {
		int component_x, component_y, component_width, component_height;
		calculate_component_size(frame_augra, component,
					 &component_x, &component_y,
					 &component_width, &component_height);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_rectangle(cr, -component_x, -component_y,
				libdecor_frame_get_content_width(
					&frame_augra->frame),
				libdecor_frame_get_content_height(
					&frame_augra->frame));
		cairo_fill(cr);
	}

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static void
set_component_input_region(struct libdecor_frame_augra *frame_augra,
			   struct border_component *border_component)
{
	if (border_component->type == SHADOW && frame_augra->shadow_showing) {
		struct wl_region *input_region;
		int component_x;
		int component_y;
		int component_width;
		int component_height;

		calculate_component_size(frame_augra, border_component->type,
					 &component_x, &component_y,
					 &component_width, &component_height);

		/*
		 * the input region is the outer surface size minus the inner
		 * content size
		 */
		input_region = wl_compositor_create_region(
				       frame_augra->plugin_augra->wl_compositor);
		wl_region_add(input_region, 0, 0,
			      component_width, component_height);
		wl_region_subtract(input_region, -component_x, -component_y,
			libdecor_frame_get_content_width(&frame_augra->frame),
			libdecor_frame_get_content_height(&frame_augra->frame));
		wl_surface_set_input_region(border_component->server.wl_surface,
					    input_region);
		wl_region_destroy(input_region);
	}
}

static void
ensure_component_realized_server(struct libdecor_frame_augra *frame_augra,
				 struct border_component *border_component,
				 int component_width,
				 int component_height,
				 int scale)
{
	struct buffer *old_buffer;
	struct buffer *buffer = NULL;

	old_buffer = border_component->server.buffer;
	if (old_buffer) {
		if (!old_buffer->in_use &&
		    old_buffer->buffer_width == component_width * scale &&
		    old_buffer->buffer_height == component_height * scale) {
			buffer = old_buffer;
		} else {
			buffer_free(old_buffer);
			border_component->server.buffer = NULL;
		}
	}

	if (!buffer)
		buffer = create_shm_buffer(frame_augra->plugin_augra,
					   component_width,
					   component_height,
					   border_component->opaque,
					   border_component->server.scale);

	border_component->server.buffer = buffer;
}

static void
ensure_component_realized_client(struct libdecor_frame_augra *frame_augra,
				 struct border_component *border_component,
				 int component_width,
				 int component_height,
				 int scale)
{
	cairo_surface_t *old_image;

	old_image = border_component->client.image;
	if (old_image) {
		int cairo_buffer_width;
		int cairo_buffer_height;
		double x_scale;
		double y_scale;

		cairo_surface_get_device_scale(old_image, &x_scale, &y_scale);
		cairo_buffer_width =
			(int) round(cairo_image_surface_get_width(old_image) *
				    x_scale);
		cairo_buffer_height =
			(int) round(cairo_image_surface_get_height(old_image) *
				    y_scale);

		if (cairo_buffer_width != component_width * scale ||
		    cairo_buffer_height != component_height * scale) {
			cairo_surface_destroy(old_image);
			border_component->client.image = NULL;
		}
	}

	if (!border_component->client.image) {
		cairo_surface_t *new_image;

		new_image =
			cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						   component_width * scale,
						   component_height * scale);
		cairo_surface_set_device_scale(new_image, scale, scale);
		border_component->client.image = new_image;
	}

}

static void
ensure_component_realized(struct libdecor_frame_augra *frame_augra,
			  struct border_component *border_component,
			  int component_width,
			  int component_height,
			  int scale)
{
	switch (border_component->composite_mode) {
	case COMPOSITE_SERVER:
		ensure_component_realized_server(frame_augra, border_component,
						 component_width,
						 component_height,
						 scale);
		break;
	case COMPOSITE_CLIENT:
		ensure_component_realized_client(frame_augra, border_component,
						 component_width,
						 component_height,
						 scale);
		break;
	}
}

static cairo_t *
create_cairo_for_parent(struct border_component *border_component)
{
	struct border_component *parent =
		border_component->client.parent_component;
	struct buffer *buffer;
	struct border_component *server_component;
	cairo_surface_t *parent_surface;
	cairo_t *cr;

	switch (parent->composite_mode) {
	case COMPOSITE_SERVER:
		buffer = parent->server.buffer;
		parent_surface = cairo_image_surface_create_for_data(
				  buffer->data, CAIRO_FORMAT_ARGB32,
				  buffer->buffer_width, buffer->buffer_height,
				  cairo_format_stride_for_width(
					  CAIRO_FORMAT_ARGB32,
					  buffer->buffer_width)
				  );
		cr = cairo_create(parent_surface);
		cairo_surface_destroy(parent_surface);
		cairo_scale(cr, buffer->scale, buffer->scale);
		return cr;
	case COMPOSITE_CLIENT:
		cr = cairo_create(parent->client.image);
		server_component = get_server_component(border_component);
		cairo_scale(cr,
			    server_component->server.scale,
			    server_component->server.scale);
		return cr;
	}
	return NULL;
}

static void
draw_border_component(struct libdecor_frame_augra *frame_augra,
		      struct border_component *border_component)
{
	enum component component = border_component->type;
	struct buffer *buffer;
	cairo_t *cr;
	int component_x;
	int component_y;
	int component_width;
	int component_height;
	int scale;
	struct border_component *child_component;

	if (border_component->is_hidden)
		return;

	calculate_component_size(frame_augra, component,
				 &component_x, &component_y,
				 &component_width, &component_height);

	set_component_input_region(frame_augra, border_component);

	scale = border_component_get_scale(border_component);
	ensure_component_realized(frame_augra, border_component,
				  component_width,
				  component_height,
				  scale);

	draw_component_content(frame_augra,
			       border_component,
			       component_width, component_height,
			       component);

	switch(border_component->composite_mode) {
	case COMPOSITE_SERVER:
		buffer = border_component->server.buffer;
		wl_surface_attach(border_component->server.wl_surface,
				  buffer->wl_buffer,
				  0, 0);
		wl_surface_set_buffer_scale(border_component->server.wl_surface,
					    buffer->scale);
		buffer->in_use = true;
		wl_surface_commit(border_component->server.wl_surface);
		wl_surface_damage_buffer(border_component->server.wl_surface, 0, 0,
					 component_width * scale,
					 component_height * scale);
		wl_subsurface_set_position(border_component->server.wl_subsurface,
					   component_x, component_y);
		break;
	case COMPOSITE_CLIENT:
		cr = create_cairo_for_parent(border_component);
		cairo_set_source_surface(cr,
					 border_component->client.image,
					 component_x, component_y);
		cairo_paint(cr);
		cairo_destroy(cr);
		break;
	}

	wl_list_for_each(child_component, &border_component->child_components, link)
		draw_border_component(frame_augra, child_component);
}

static void
draw_border(struct libdecor_frame_augra *frame_augra)
{
	draw_border_component(frame_augra, &frame_augra->shadow);
	frame_augra->shadow_showing = true;
}

static void
draw_title_bar(struct libdecor_frame_augra *frame_augra)
{
	draw_border_component(frame_augra, &frame_augra->title_bar.title);
	frame_augra->title_bar.is_showing = true;
}

static void
draw_decoration(struct libdecor_frame_augra *frame_augra)
{
	switch (frame_augra->decoration_type) {
	case DECORATION_TYPE_NONE:
		if (frame_augra->link.next != NULL)
			wl_list_remove(&frame_augra->link);
		if (is_border_surfaces_showing(frame_augra))
			hide_border_surfaces(frame_augra);
		if (is_title_bar_surfaces_showing(frame_augra))
			hide_title_bar_surfaces(frame_augra);
		break;
	case DECORATION_TYPE_TILED:
	case DECORATION_TYPE_ALL:
		/* show borders */
		ensure_border_surfaces(frame_augra);
		draw_border(frame_augra);
		/* show title bar */
		ensure_title_bar_surfaces(frame_augra);
		draw_title_bar(frame_augra);
		/* link frame */
		if (frame_augra->link.next == NULL)
			wl_list_insert(
				&frame_augra->plugin_augra->visible_frame_list,
				&frame_augra->link);
		break;
	case DECORATION_TYPE_MAXIMIZED:
		/* hide borders */
		if (is_border_surfaces_showing(frame_augra))
			hide_border_surfaces(frame_augra);
		/* show title bar */
		ensure_title_bar_surfaces(frame_augra);
		draw_title_bar(frame_augra);
		/* link frame */
		if (frame_augra->link.next == NULL)
			wl_list_insert(
				&frame_augra->plugin_augra->visible_frame_list,
				&frame_augra->link);
		break;
	}
}

static enum decoration_type
window_state_to_decoration_type(enum libdecor_window_state window_state)
{
	if (window_state & LIBDECOR_WINDOW_STATE_FULLSCREEN)
		return DECORATION_TYPE_NONE;
	else if (window_state & LIBDECOR_WINDOW_STATE_MAXIMIZED)
		/* title bar, no shadows */
		return DECORATION_TYPE_MAXIMIZED;
	else if (window_state & LIBDECOR_WINDOW_STATE_TILED_LEFT ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_RIGHT ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_TOP ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_BOTTOM)
		/* title bar, invisible shadows */
		return DECORATION_TYPE_TILED;
	else
		/* title bar, shadows */
		return DECORATION_TYPE_ALL;
}

static void
libdecor_plugin_cairo_frame_commit(struct libdecor_plugin *plugin,
				   struct libdecor_frame *frame,
				   struct libdecor_state *state,
				   struct libdecor_configuration *configuration)
{
	struct libdecor_frame_augra *frame_augra =
		(struct libdecor_frame_augra *) frame;
	enum libdecor_window_state old_window_state;
	enum libdecor_window_state new_window_state;
	int old_content_width, old_content_height;
	int new_content_width, new_content_height;
	enum decoration_type old_decoration_type;
	enum decoration_type new_decoration_type;

	old_window_state = frame_augra->window_state;
	new_window_state = libdecor_frame_get_window_state(frame);

	old_content_width = frame_augra->content_width;
	old_content_height = frame_augra->content_height;
	new_content_width = libdecor_frame_get_content_width(frame);
	new_content_height = libdecor_frame_get_content_height(frame);

	old_decoration_type = frame_augra->decoration_type;
	new_decoration_type = window_state_to_decoration_type(new_window_state);

	if (old_decoration_type == new_decoration_type &&
	    old_content_width == new_content_width &&
	    old_content_height == new_content_height &&
	    old_window_state == new_window_state)
		return;

	frame_augra->content_width = new_content_width;
	frame_augra->content_height = new_content_height;
	frame_augra->decoration_type = new_decoration_type;
	frame_augra->window_state = new_window_state;

	draw_decoration(frame_augra);
}

static void
libdecor_plugin_cairo_frame_property_changed(struct libdecor_plugin *plugin,
					     struct libdecor_frame *frame)
{
	struct libdecor_frame_augra *frame_augra =
		(struct libdecor_frame_augra *) frame;
	bool redraw_needed = false;
	const char *new_title;

	new_title = libdecor_frame_get_title(frame);
	if (frame_augra->title_bar.is_showing) {
		if (!streql(frame_augra->title, new_title))
			redraw_needed = true;
	}

	if (frame_augra->title) {
		free(frame_augra->title);
		frame_augra->title = NULL;
	}

	if (new_title) {
		frame_augra->title = strdup(new_title);
	}

	if (frame_augra->capabilities != libdecor_frame_get_capabilities(frame)) {
		frame_augra->capabilities = libdecor_frame_get_capabilities(frame);
		redraw_needed = true;
	}

	if (redraw_needed) {
		draw_decoration(frame_augra);
		libdecor_frame_toplevel_commit(frame);
	}
}

static bool
streq(const char *str1,
      const char *str2)
{
	if (!str1 && !str2)
		return true;

	if (str1 && str2)
		return strcmp(str1, str2) == 0;

	return false;
}

static void
libdecor_plugin_cairo_frame_popup_grab(struct libdecor_plugin *plugin,
				       struct libdecor_frame *frame,
				       const char *seat_name)
{
	struct libdecor_frame_augra *frame_augra =
		(struct libdecor_frame_augra *) frame;
	struct libdecor_plugin_augra *plugin_augra = frame_augra->plugin_augra;
	struct seat *seat;

	wl_list_for_each(seat, &plugin_augra->seat_list, link) {
		if (streq(seat->name, seat_name)) {
			if (seat->grabbed) {
				fprintf(stderr, "libdecor-WARNING: Application "
					"tried to grab seat twice\n");
			}
			synthesize_pointer_leave(seat);
			seat->grabbed = true;
			return;
		}
	}

	fprintf(stderr,
		"libdecor-WARNING: Application tried to grab unknown seat\n");
}

static void
libdecor_plugin_cairo_frame_popup_ungrab(struct libdecor_plugin *plugin,
					 struct libdecor_frame *frame,
					 const char *seat_name)
{
	struct libdecor_frame_augra *frame_augra =
		(struct libdecor_frame_augra *) frame;
	struct libdecor_plugin_augra *plugin_augra = frame_augra->plugin_augra;
	struct seat *seat;

	wl_list_for_each(seat, &plugin_augra->seat_list, link) {
		if (streq(seat->name, seat_name)) {
			if (!seat->grabbed) {
				fprintf(stderr, "libdecor-WARNING: Application "
					"tried to ungrab seat twice\n");
			}
			seat->grabbed = false;
			synthesize_pointer_enter(seat);
			sync_active_component(frame_augra, seat);
			return;
		}
	}

	fprintf(stderr,
		"libdecor-WARNING: Application tried to ungrab unknown seat\n");
}

static bool
libdecor_plugin_cairo_frame_get_border_size(struct libdecor_plugin *plugin,
					    struct libdecor_frame *frame,
					    struct libdecor_configuration *configuration,
					    int *left,
					    int *right,
					    int *top,
					    int *bottom)
{
	enum libdecor_window_state window_state;

	if (configuration) {
		if (!libdecor_configuration_get_window_state(
			    configuration, &window_state))
			return false;
	} else {
		window_state = libdecor_frame_get_window_state(frame);
	}

	if (left)
		*left = 0;
	if (right)
		*right = 0;
	if (bottom)
		*bottom = 0;
	if (top) {
		enum decoration_type type = window_state_to_decoration_type(window_state);

		if (((struct libdecor_frame_augra *)frame)->title_bar.is_showing &&
		    (type != DECORATION_TYPE_NONE))
			*top = TITLE_HEIGHT;
		else
			*top = 0;
	}

	return true;
}

static struct libdecor_plugin_interface cairo_plugin_iface = {
	.destroy = libdecor_plugin_cairo_destroy,
	.get_fd = libdecor_plugin_cairo_get_fd,
	.dispatch = libdecor_plugin_cairo_dispatch,

	.set_handle_application_cursor = libdecor_plugin_cairo_set_handle_application_cursor,

	.frame_new = libdecor_plugin_cairo_frame_new,
	.frame_free = libdecor_plugin_cairo_frame_free,
	.frame_commit = libdecor_plugin_cairo_frame_commit,
	.frame_property_changed = libdecor_plugin_cairo_frame_property_changed,
	.frame_popup_grab = libdecor_plugin_cairo_frame_popup_grab,
	.frame_popup_ungrab = libdecor_plugin_cairo_frame_popup_ungrab,
	.frame_get_border_size = libdecor_plugin_cairo_frame_get_border_size,
};

static void
init_wl_compositor(struct libdecor_plugin_augra *plugin_augra,
		   uint32_t id,
		   uint32_t version)
{
	plugin_augra->wl_compositor =
		wl_registry_bind(plugin_augra->wl_registry,
				 id, &wl_compositor_interface,
				 MIN(version, 4));
}

static void
init_wl_subcompositor(struct libdecor_plugin_augra *plugin_augra,
		      uint32_t id,
		      uint32_t version)
{
	plugin_augra->wl_subcompositor =
		wl_registry_bind(plugin_augra->wl_registry,
				 id, &wl_subcompositor_interface, 1);
}

static void
shm_format(void *user_data,
	   struct wl_shm *wl_shm,
	   uint32_t format)
{
	struct libdecor_plugin_augra *plugin_augra = user_data;

	if (format == WL_SHM_FORMAT_ARGB8888)
		plugin_augra->has_argb = true;
}

struct wl_shm_listener shm_listener = {
	shm_format
};

static void
shm_callback(void *user_data,
		 struct wl_callback *callback,
		 uint32_t time)
{
	struct libdecor_plugin_augra *plugin_augra = user_data;
	struct libdecor *context = plugin_augra->context;

	wl_callback_destroy(callback);
	plugin_augra->globals_callback_shm = NULL;

	if (!plugin_augra->has_argb) {
		libdecor_notify_plugin_error(
				context,
				LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
				"Compositor is missing required shm format");
		return;
	}

	libdecor_notify_plugin_ready(context);
}

static const struct wl_callback_listener shm_callback_listener = {
	shm_callback
};

static void
init_wl_shm(struct libdecor_plugin_augra *plugin_augra,
	    uint32_t id,
	    uint32_t version)
{
	struct libdecor *context = plugin_augra->context;
	struct wl_display *wl_display = libdecor_get_wl_display(context);

	plugin_augra->wl_shm =
		wl_registry_bind(plugin_augra->wl_registry,
				 id, &wl_shm_interface, 1);
	wl_shm_add_listener(plugin_augra->wl_shm, &shm_listener, plugin_augra);

	plugin_augra->globals_callback_shm = wl_display_sync(wl_display);
	wl_callback_add_listener(plugin_augra->globals_callback_shm,
				 &shm_callback_listener,
				 plugin_augra);
}

static void
cursor_surface_enter(void *data,
		     struct wl_surface *wl_surface,
		     struct wl_output *wl_output)
{
	struct seat *seat = data;

	if(own_output(wl_output)) {
		struct cursor_output *cursor_output;
		cursor_output = zalloc(sizeof *cursor_output);
		cursor_output->output = wl_output_get_user_data(wl_output);
		wl_list_insert(&seat->cursor_outputs, &cursor_output->link);
		if (update_local_cursor(seat))
			send_cursor(seat);
	}
}

static void
cursor_surface_leave(void *data,
		     struct wl_surface *wl_surface,
		     struct wl_output *wl_output)
{
	struct seat *seat = data;

	if(own_output(wl_output)) {
		struct cursor_output *cursor_output, *tmp;
		wl_list_for_each_safe(cursor_output, tmp, &seat->cursor_outputs, link) {
			if (cursor_output->output->wl_output == wl_output) {
				wl_list_remove(&cursor_output->link);
				free(cursor_output);
			}
		}

		if (update_local_cursor(seat))
			send_cursor(seat);
	}
}

static struct wl_surface_listener cursor_surface_listener = {
	cursor_surface_enter,
	cursor_surface_leave,
};

static void
ensure_cursor_surface(struct seat *seat)
{
	struct wl_compositor *wl_compositor = seat->plugin_augra->wl_compositor;

	if (seat->cursor_surface)
		return;

	seat->cursor_surface = wl_compositor_create_surface(wl_compositor);
	wl_surface_add_listener(seat->cursor_surface,
				&cursor_surface_listener, seat);
}

static bool
ensure_cursor_theme(struct seat *seat)
{
	struct libdecor_plugin_augra *plugin_augra = seat->plugin_augra;
	int scale = 1;
	struct wl_cursor_theme *theme;
	struct cursor_output *cursor_output;

	wl_list_for_each(cursor_output, &seat->cursor_outputs, link) {
		scale = MAX(scale, cursor_output->output->scale);
	}

	if (seat->cursor_theme && seat->cursor_scale == scale)
		return false;

	seat->cursor_scale = scale;
	theme = wl_cursor_theme_load(plugin_augra->cursor_theme_name,
				     plugin_augra->cursor_size * scale,
				     plugin_augra->wl_shm);
	if (theme == NULL)
		return false;

	if (seat->cursor_theme)
		wl_cursor_theme_destroy(seat->cursor_theme);

	seat->cursor_theme = theme;

	for (unsigned int i = 0; i < ARRAY_LENGTH(cursor_names); i++) {
		seat->cursors[i] = wl_cursor_theme_get_cursor(
						   seat->cursor_theme,
						   cursor_names[i]);
	}

	seat->cursor_left_ptr = wl_cursor_theme_get_cursor(seat->cursor_theme,
							   "left_ptr");
	seat->current_cursor = seat->cursor_left_ptr;

	return true;
}

enum libdecor_resize_edge
component_edge(const struct border_component *cmpnt,
	       const int pointer_x,
	       const int pointer_y,
	       const int margin)
{
	const bool top = pointer_y < margin * 2;
	const bool bottom = pointer_y > (cmpnt->server.buffer->height - margin * 2);
	const bool left = pointer_x < margin * 2;
	const bool right = pointer_x > (cmpnt->server.buffer->width - margin * 2);

	if (top)
		if (left)
			return LIBDECOR_RESIZE_EDGE_TOP_LEFT;
		else if (right)
			return LIBDECOR_RESIZE_EDGE_TOP_RIGHT;
		else
			return LIBDECOR_RESIZE_EDGE_TOP;
	else if (bottom)
		if (left)
			return LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT;
		else if (right)
			return LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT;
		else
			return LIBDECOR_RESIZE_EDGE_BOTTOM;
	else if (left)
		return LIBDECOR_RESIZE_EDGE_LEFT;
	else if (right)
		return LIBDECOR_RESIZE_EDGE_RIGHT;
	else
		return LIBDECOR_RESIZE_EDGE_NONE;
}

static bool
update_local_cursor(struct seat *seat)
{
	if (!seat->pointer_focus) {
		seat->current_cursor = seat->cursor_left_ptr;
		return false;
	}

	if (!own_surface(seat->pointer_focus)) {
		if (seat->plugin_augra->handle_cursor) {
			seat->current_cursor = seat->cursor_left_ptr;
			return true;
		} else {
			return false;
		}
	}

	struct libdecor_frame_augra *frame_augra =
			wl_surface_get_user_data(seat->pointer_focus);
	struct wl_cursor *wl_cursor = NULL;

	if (!frame_augra || !frame_augra->active) {
		seat->current_cursor = seat->cursor_left_ptr;
		return false;
	}

	bool theme_updated = ensure_cursor_theme(seat);

	if (frame_augra->active->type == SHADOW &&
	    is_border_surfaces_showing(frame_augra) &&
	    resizable(frame_augra)) {
		enum libdecor_resize_edge edge;
		edge = component_edge(frame_augra->active,
				      seat->pointer_x,
				      seat->pointer_y, SHADOW_MARGIN);

		if (edge != LIBDECOR_RESIZE_EDGE_NONE)
			wl_cursor = seat->cursors[edge - 1];
	} else {
		wl_cursor = seat->cursor_left_ptr;
	}

	if (seat->current_cursor != wl_cursor) {
		seat->current_cursor = wl_cursor;
		return true;
	}

	return theme_updated;
}

static void
send_cursor(struct seat *seat)
{
	struct wl_cursor_image *image;
	struct wl_buffer *buffer;

	if (seat->pointer_focus == NULL || seat->current_cursor == NULL)
		return;

	image = seat->current_cursor->images[0];
	buffer = wl_cursor_image_get_buffer(image);
	wl_surface_attach(seat->cursor_surface, buffer, 0, 0);
	wl_surface_set_buffer_scale(seat->cursor_surface, seat->cursor_scale);
	wl_surface_damage_buffer(seat->cursor_surface, 0, 0,
				 image->width * seat->cursor_scale,
				 image->height * seat->cursor_scale);
	wl_surface_commit(seat->cursor_surface);
	wl_pointer_set_cursor(seat->wl_pointer, seat->serial,
			      seat->cursor_surface,
			      image->hotspot_x / seat->cursor_scale,
			      image->hotspot_y / seat->cursor_scale);
}

static void
sync_active_component(struct libdecor_frame_augra *frame_augra,
		      struct seat *seat)
{
	struct border_component *old_active;

	if (!seat->pointer_focus)
		return;

	old_active = frame_augra->active;
	update_component_focus(frame_augra, seat->pointer_focus, seat);
	if (old_active != frame_augra->active) {
		draw_decoration(frame_augra);
		libdecor_frame_toplevel_commit(&frame_augra->frame);
	}

	if (update_local_cursor(seat))
		send_cursor(seat);
}

static void
synthesize_pointer_enter(struct seat *seat)
{
	struct wl_surface *surface;
	struct libdecor_frame_augra *frame_augra;

	surface = seat->pointer_focus;
	if (!surface || !own_surface(surface))
		return;

	frame_augra = wl_surface_get_user_data(surface);
	if (!frame_augra)
		return;

	update_component_focus(frame_augra, seat->pointer_focus, seat);
	frame_augra->grab = NULL;

	/* update decorations */
	if (frame_augra->active) {
		draw_decoration(frame_augra);
		libdecor_frame_toplevel_commit(&frame_augra->frame);
	}

	update_local_cursor(seat);
	send_cursor(seat);
}

static void
synthesize_pointer_leave(struct seat *seat)
{
	struct wl_surface *surface;
	struct libdecor_frame_augra *frame_augra;

	surface = seat->pointer_focus;
	if (!surface || !own_surface(surface))
		return;

	frame_augra = wl_surface_get_user_data(surface);
	if (!frame_augra)
		return;

	if (!frame_augra->active)
		return;

	frame_augra->active = NULL;
	draw_decoration(frame_augra);
	libdecor_frame_toplevel_commit(&frame_augra->frame);
	update_local_cursor(seat);
}

static void
pointer_enter(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface,
	      wl_fixed_t surface_x,
	      wl_fixed_t surface_y)
{
	struct seat *seat = data;
	struct libdecor_frame_augra *frame_augra = NULL;

	if (!surface)
		return;

	if (!own_surface(surface)) {
		struct seat *seat = wl_pointer_get_user_data(wl_pointer);
		struct libdecor_plugin_augra *plugin_augra = seat->plugin_augra;

		if (!plugin_augra->handle_cursor)
			return;
	} else {
		frame_augra = wl_surface_get_user_data(surface);
	}

	ensure_cursor_surface(seat);

	seat->pointer_x = wl_fixed_to_int(surface_x);
	seat->pointer_y = wl_fixed_to_int(surface_y);
	seat->serial = serial;
	seat->pointer_focus = surface;
	seat->pointer_focus_frame = frame_augra;

	if (seat->grabbed)
		return;

	synthesize_pointer_enter(seat);
}

static void
pointer_leave(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface)
{
	struct seat *seat = data;

	if (!surface)
		return;

	if (!own_surface(surface))
		return;

	synthesize_pointer_leave(seat);
	seat->pointer_focus = NULL;
	seat->pointer_focus_frame = NULL;
}

static void
pointer_motion(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t time,
	       wl_fixed_t surface_x,
	       wl_fixed_t surface_y)
{
	struct seat *seat = data;

	seat->pointer_x = wl_fixed_to_int(surface_x);
	seat->pointer_y = wl_fixed_to_int(surface_y);

	if (seat->grabbed)
		return;

	if (!seat->pointer_focus_frame)
		return;

	sync_active_component(seat->pointer_focus_frame, seat);
}

static void
pointer_button(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t serial,
	       uint32_t time,
	       uint32_t button,
	       uint32_t state)
{
	struct seat *seat = data;
	struct libdecor_frame_augra *frame_augra;

	if (!seat->pointer_focus || !own_surface(seat->pointer_focus))
		return;

	frame_augra = wl_surface_get_user_data(seat->pointer_focus);
	if (!frame_augra)
		return;

	if (seat->grabbed) {
		libdecor_frame_dismiss_popup(&frame_augra->frame, seat->name);
		return;
	}

	if (!frame_augra->active)
		return;

	if (button == BTN_LEFT) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
			enum libdecor_resize_edge edge =
					LIBDECOR_RESIZE_EDGE_NONE;

			frame_augra->grab = NULL;

			switch (frame_augra->active->type) {
			case SHADOW:
				edge = component_edge(frame_augra->active,
						      seat->pointer_x,
						      seat->pointer_y,
						      SHADOW_MARGIN);
				break;
			case TITLE:
				if (time-seat->pointer_button_time_stamp <
				    DOUBLE_CLICK_TIME_MS) {
					toggle_maximized(&frame_augra->frame);
				}
				else if (moveable(frame_augra)) {
					seat->pointer_button_time_stamp = time;
					libdecor_frame_move(&frame_augra->frame,
							    seat->wl_seat,
							    serial);
				}
				break;
			case BUTTON_MIN:
			case BUTTON_MAX:
			case BUTTON_CLOSE:
				frame_augra->grab = frame_augra->active;
				break;
			default:
				break;
			}

			if (edge != LIBDECOR_RESIZE_EDGE_NONE &&
			    resizable(frame_augra)) {
				libdecor_frame_resize(
					&frame_augra->frame,
					seat->wl_seat,
					serial,
					edge);
			}
		}
		else if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
			 frame_augra->grab) {
			libdecor_frame_ref(&frame_augra->frame);
			if (frame_augra->grab == frame_augra->focus) {
				switch (frame_augra->active->type) {
				case BUTTON_MIN:
					if (minimizable(frame_augra))
						libdecor_frame_set_minimized(
							&frame_augra->frame);
					break;
				case BUTTON_MAX:
					toggle_maximized(&frame_augra->frame);
					break;
				case BUTTON_CLOSE:
					if (closeable(frame_augra))
						libdecor_frame_close(&frame_augra->frame);
					break;
				default:
					break;
				}
			}
			frame_augra->grab = NULL;
			sync_active_component(frame_augra, seat);
			libdecor_frame_unref(&frame_augra->frame);
		}
	}
	else if (button == BTN_RIGHT &&
		 state == WL_POINTER_BUTTON_STATE_PRESSED &&
		 seat->pointer_focus == frame_augra->title_bar.title.server.wl_surface) {
			libdecor_frame_show_window_menu(&frame_augra->frame,
							seat->wl_seat,
							serial,
							seat->pointer_x,
							seat->pointer_y - TITLE_HEIGHT);
	}
}

static void
pointer_axis(void *data,
	     struct wl_pointer *wl_pointer,
	     uint32_t time,
	     uint32_t axis,
	     wl_fixed_t value)
{
}

static struct wl_pointer_listener pointer_listener = {
	pointer_enter,
	pointer_leave,
	pointer_motion,
	pointer_button,
	pointer_axis
};

static void
seat_capabilities(void *data,
		  struct wl_seat *wl_seat,
		  uint32_t capabilities)
{
	struct seat *seat = data;

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) &&
	    !seat->wl_pointer) {
		seat->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->wl_pointer,
					&pointer_listener, seat);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) &&
		   seat->wl_pointer) {
		wl_pointer_release(seat->wl_pointer);
		seat->wl_pointer = NULL;
	}
}

static void
seat_name(void *data,
	  struct wl_seat *wl_seat,
	  const char *name)
{
	struct seat *seat = data;

	seat->name = strdup(name);
}

static struct wl_seat_listener seat_listener = {
	seat_capabilities,
	seat_name
};

static void
init_wl_seat(struct libdecor_plugin_augra *plugin_augra,
	     uint32_t id,
	     uint32_t version)
{
	struct seat *seat;

	if (version < 3) {
		libdecor_notify_plugin_error(
				plugin_augra->context,
				LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
				"%s version 3 required but only version %i is available\n",
				wl_seat_interface.name, version);
	}

	seat = zalloc(sizeof *seat);
	seat->cursor_scale = 1;
	seat->plugin_augra = plugin_augra;
	wl_list_init(&seat->cursor_outputs);
	wl_list_insert(&plugin_augra->seat_list, &seat->link);
	seat->wl_seat =
		wl_registry_bind(plugin_augra->wl_registry,
				 id, &wl_seat_interface, 3);
	wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
}

static void
output_geometry(void *data,
		struct wl_output *wl_output,
		int32_t x,
		int32_t y,
		int32_t physical_width,
		int32_t physical_height,
		int32_t subpixel,
		const char *make,
		const char *model,
		int32_t transform)
{
}

static void
output_mode(void *data,
	    struct wl_output *wl_output,
	    uint32_t flags,
	    int32_t width,
	    int32_t height,
	    int32_t refresh)
{
}

static void
output_done(void *data,
	    struct wl_output *wl_output)
{
	struct output *output = data;
	struct libdecor_frame_augra *frame_augra;
	struct seat *seat;

	wl_list_for_each(frame_augra,
			&output->plugin_augra->visible_frame_list, link) {
		bool updated = false;
		updated |= redraw_scale(frame_augra, &frame_augra->shadow);
		updated |= redraw_scale(frame_augra, &frame_augra->title_bar.title);
		if (updated)
			libdecor_frame_toplevel_commit(&frame_augra->frame);
	}
	wl_list_for_each(seat, &output->plugin_augra->seat_list, link) {
		if (update_local_cursor(seat))
			send_cursor(seat);
	}
}

static void
output_scale(void *data,
	     struct wl_output *wl_output,
	     int32_t factor)
{
	struct output *output = data;

	output->scale = factor;
}

static struct wl_output_listener output_listener = {
	output_geometry,
	output_mode,
	output_done,
	output_scale
};

static void
init_wl_output(struct libdecor_plugin_augra *plugin_augra,
	       uint32_t id,
	       uint32_t version)
{
	struct output *output;

	if (version < 2) {
		libdecor_notify_plugin_error(
				plugin_augra->context,
				LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
				"%s version 2 required but only version %i is available\n",
				wl_output_interface.name, version);
	}

	output = zalloc(sizeof *output);
	output->plugin_augra = plugin_augra;
	wl_list_insert(&plugin_augra->output_list, &output->link);
	output->id = id;
	output->wl_output =
		wl_registry_bind(plugin_augra->wl_registry,
				 id, &wl_output_interface,
				 MIN (version, 3));
	wl_proxy_set_tag((struct wl_proxy *) output->wl_output,
			 &libdecor_cairo_proxy_tag);
	wl_output_add_listener(output->wl_output, &output_listener, output);
}

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct libdecor_plugin_augra *plugin_augra = user_data;

	if (strcmp(interface, "wl_compositor") == 0)
		init_wl_compositor(plugin_augra, id, version);
	else if (strcmp(interface, "wl_subcompositor") == 0)
		init_wl_subcompositor(plugin_augra, id, version);
	else if (strcmp(interface, "wl_shm") == 0)
		init_wl_shm(plugin_augra, id, version);
	else if (strcmp(interface, "wl_seat") == 0)
		init_wl_seat(plugin_augra, id, version);
	else if (strcmp(interface, "wl_output") == 0)
		init_wl_output(plugin_augra, id, version);
}

static void
remove_surface_outputs(struct border_component *cmpnt, struct output *output)
{
	struct surface_output *surface_output;
	wl_list_for_each(surface_output, &cmpnt->server.output_list, link) {
		if (surface_output->output == output) {
			wl_list_remove(&surface_output->link);
			free(surface_output);
			break;
		}
	}
}

static void
output_removed(struct libdecor_plugin_augra *plugin_augra,
	       struct output *output)
{
	struct libdecor_frame_augra *frame_augra;
	struct seat *seat;

	wl_list_for_each(frame_augra, &plugin_augra->visible_frame_list, link) {
		remove_surface_outputs(&frame_augra->shadow, output);
		remove_surface_outputs(&frame_augra->title_bar.title, output);
		remove_surface_outputs(&frame_augra->title_bar.min, output);
		remove_surface_outputs(&frame_augra->title_bar.max, output);
		remove_surface_outputs(&frame_augra->title_bar.close, output);
	}
	wl_list_for_each(seat, &plugin_augra->seat_list, link) {
		struct cursor_output *cursor_output, *tmp;
		wl_list_for_each_safe(cursor_output, tmp, &seat->cursor_outputs, link) {
			if (cursor_output->output == output) {
				wl_list_remove(&cursor_output->link);
				free(cursor_output);
			}
		}
	}

	wl_list_remove(&output->link);
	wl_output_destroy(output->wl_output);
	free(output);
}

static void
registry_handle_global_remove(void *user_data,
			      struct wl_registry *wl_registry,
			      uint32_t name)
{
	struct libdecor_plugin_augra *plugin_augra = user_data;
	struct output *output;

	wl_list_for_each(output, &plugin_augra->output_list, link) {
		if (output->id == name) {
			output_removed(plugin_augra, output);
			break;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static bool
has_required_globals(struct libdecor_plugin_augra *plugin_augra)
{
	if (!plugin_augra->wl_compositor)
		return false;
	if (!plugin_augra->wl_subcompositor)
		return false;
	if (!plugin_augra->wl_shm)
		return false;

	return true;
}

static void
globals_callback(void *user_data,
		 struct wl_callback *callback,
		 uint32_t time)
{
	struct libdecor_plugin_augra *plugin_augra = user_data;

	wl_callback_destroy(callback);
	plugin_augra->globals_callback = NULL;
}

static const struct wl_callback_listener globals_callback_listener = {
	globals_callback
};

static struct libdecor_plugin *
libdecor_plugin_new(struct libdecor *context)
{
	struct libdecor_plugin_augra *plugin_augra;
	struct wl_display *wl_display;
	const char *icon_path;

	plugin_augra = zalloc(sizeof *plugin_augra);
	libdecor_plugin_init(&plugin_augra->plugin,
			     context,
			     &cairo_plugin_iface);
	plugin_augra->context = context;

	wl_list_init(&plugin_augra->visible_frame_list);
	wl_list_init(&plugin_augra->seat_list);
	wl_list_init(&plugin_augra->output_list);

	apply_color_scheme();

	/* fetch cursor theme and size*/
	if (!libdecor_get_cursor_settings(&plugin_augra->cursor_theme_name,
					  &plugin_augra->cursor_size)) {
		plugin_augra->cursor_theme_name = NULL;
		plugin_augra->cursor_size = 24;
	}

	/* define a sens-serif bold font at symbol size */
	plugin_augra->font = pango_font_description_new();
	pango_font_description_set_family(plugin_augra->font, "sans");
	pango_font_description_set_weight(plugin_augra->font, PANGO_WEIGHT_BOLD);
	pango_font_description_set_absolute_size(plugin_augra->font, TITLE_FONT_SIZE * PANGO_SCALE);

	/* titlebar icon, path supplied by the launcher; absent or broken
	 * file means no icon */
	plugin_augra->app_icon = NULL;
	icon_path = getenv("DOSBOX_DECOR_ICON");
	if (icon_path && icon_path[0] != '\0') {
		cairo_surface_t *icon =
			cairo_image_surface_create_from_png(icon_path);
		if (cairo_surface_status(icon) == CAIRO_STATUS_SUCCESS)
			plugin_augra->app_icon = icon;
		else
			cairo_surface_destroy(icon);
	}

	wl_display = libdecor_get_wl_display(context);
	plugin_augra->wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(plugin_augra->wl_registry,
				 &registry_listener,
				 plugin_augra);

	plugin_augra->globals_callback = wl_display_sync(wl_display);
	wl_callback_add_listener(plugin_augra->globals_callback,
				 &globals_callback_listener,
				 plugin_augra);
	wl_display_roundtrip(wl_display);

	if (!has_required_globals(plugin_augra)) {
		fprintf(stderr, "libdecor-cairo-WARNING: Could not get required globals\n");
		libdecor_plugin_cairo_destroy(&plugin_augra->plugin);
		return NULL;
	}

	return &plugin_augra->plugin;
}

static struct libdecor_plugin_priority priorities[] = {
	{ NULL, LIBDECOR_PLUGIN_PRIORITY_HIGH }
};

LIBDECOR_EXPORT const struct libdecor_plugin_description
libdecor_plugin_description = {
	.api_version = LIBDECOR_PLUGIN_API_VERSION,
	.capabilities = LIBDECOR_PLUGIN_CAPABILITY_BASE,
	.description = "dosbox-automation decoration plugin (decor-augra)",
	.priorities = priorities,
	.constructor = libdecor_plugin_new,
};
