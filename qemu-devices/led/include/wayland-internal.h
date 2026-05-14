#pragma once

#include <bits/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>

#include "wayland.h"
#include "xdg-shell-client-protocol.h"
#include <wayland-client-protocol.h>
#include <wayland-client.h>

static inline bool have_required_globals(const struct wayland_client *state) {
	return state->compositor != NULL && state->shm != NULL &&
		   state->xdg_wm_base != NULL && state->wl_seat != NULL;
}

void xdg_wm_base_ping_handler([[maybe_unused]] void *data,
							  struct xdg_wm_base *xdg_wm_base, uint32_t serial);

// REGISTRY
void registry_global_handler(void *data, struct wl_registry *registry,
							 uint32_t name, const char *interface,
							 uint32_t version);

void registry_global_remove_handler(
	[[maybe_unused]] void *data, [[maybe_unused]] struct wl_registry *registry,
	uint32_t name);

// TOPLEVEL
void xdg_toplevel_configure_handler([[maybe_unused]] void *data,
									struct xdg_toplevel *xdg_toplevel,
									int32_t width, int32_t height,
									struct wl_array *states);
void xdg_toplevel_configure_bounds_handler([[maybe_unused]] void *data,
										   struct xdg_toplevel *xdg_toplevel,
										   int32_t width, int32_t height);
void xdg_toplevel_close_handler([[maybe_unused]] void *data,
								struct xdg_toplevel *xdg_toplevel);
void xdg_toplevel_wm_capabilities_handler(void *data,
										  struct xdg_toplevel *xdg_toplevel,
										  struct wl_array *);

// XDG_SURFACE
void xdg_surface_configure_handler([[maybe_unused]] void *data,
								   struct xdg_surface *surface,
								   uint32_t serial);

// WL_CALLBACK
void wl_callback_done_handler([[maybe_unused]] void *data,
							  struct wl_callback *wl_callback, uint32_t time);

// WL_SEAT
void wl_seat_name_handler([[maybe_unused]] void *data, struct wl_seat *wl_seat,
						  const char *name);
void wl_seat_capabilities_hander([[maybe_unused]] void *data,
								 struct wl_seat *wl_seat,
								 uint32_t capabilities);

// POINTER
void wl_pointer_enter_handler([[maybe_unused]] void *data,
							  struct wl_pointer *pointer, uint32_t serial,
							  struct wl_surface *surface, wl_fixed_t x,
							  wl_fixed_t y);
void wl_pointer_leave_handler([[maybe_unused]] void *data,
							  struct wl_pointer *pointer, uint32_t serial,
							  struct wl_surface *surface);
void wl_pointer_motion_handler([[maybe_unused]] void *data,
							   struct wl_pointer *pointer, uint32_t time,
							   wl_fixed_t x, wl_fixed_t y);
void wl_pointer_button_handler(void *data, struct wl_pointer *pointer,
							   uint32_t serial, uint32_t time, uint32_t button,
							   uint32_t state);
void wl_pointer_axis_handler(void *data, struct wl_pointer *pointer,
							 uint32_t time, uint32_t axis, wl_fixed_t value);
void wl_pointer_axis_source_handler(void *data, struct wl_pointer *wl_pointer,
									uint32_t axis_source);
void wl_pointer_axis_relative_direction_handler(void *data, struct wl_pointer *wl_pointer,
												uint32_t axis, uint32_t direction);
void wl_pointer_frame_handler(void *data, struct wl_pointer *wl_pointer);
void wl_pointer_axis_stop_handler(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis);
void wl_pointer_axis_discrete_handler(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete);
void wl_pointer_axis_value120_handler(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120);
// KEYBOARD
void wl_keyboard_keymap_handler([[maybe_unused]] void *data,
								struct wl_keyboard *wl_keyboard,
								uint32_t format, int32_t fd, uint32_t size);
void wl_keyboard_enter_handler([[maybe_unused]] void *data,
							   struct wl_keyboard *wl_keyboard, uint32_t serial,
							   struct wl_surface *surface,
							   struct wl_array *keys);
void wl_keyboard_leave_handler([[maybe_unused]] void *data,
							   struct wl_keyboard *wl_keyboard, uint32_t serial,
							   struct wl_surface *surface);
void wl_keyboard_key_handler([[maybe_unused]] void *data,
							 struct wl_keyboard *wl_keyboard, uint32_t serial,
							 uint32_t time, uint32_t key, uint32_t state);
void wl_keyboard_modifiers_handler([[maybe_unused]] void *data,
								   struct wl_keyboard *wl_keyboard,
								   uint32_t serial, uint32_t mods_depressed,
								   uint32_t mods_latched, uint32_t mods_locked,
								   uint32_t group);
void wl_keyboard_repeat_info_handler([[maybe_unused]] void *data,
									 struct wl_keyboard *wl_keyboard,
									 int32_t rate, int32_t delay);

// KEYBOARD
void wl_keyboard_enter_handler(void *data, struct wl_keyboard *keyboard,
							   uint32_t serial, struct wl_surface *surface,
							   struct wl_array *keys);
void wl_keyboard_leave_handler(void *data, struct wl_keyboard *keyboard,
							   uint32_t serial, struct wl_surface *surface);

void wl_keyboard_key_handler(void *data, struct wl_keyboard *keyboard,
							 uint32_t serial, uint32_t time, uint32_t key,
							 uint32_t keystate);

void wl_keyboard_keymap_handler(void *data, struct wl_keyboard *keyboard,
								uint32_t format, int fd, uint32_t size);

void wl_keyboard_modifiers_handler(void *data, struct wl_keyboard *keyboard,
								   uint32_t serial, uint32_t mods_depressed,
								   uint32_t mods_latched, uint32_t mods_locked,
								   uint32_t group);

void wl_keyboard_repeat_info_handler(void *data, struct wl_keyboard *keyboard,
									 int32_t rate, int32_t delay);
