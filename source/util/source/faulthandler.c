/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "faulthandler.h"
#include "common_utils.h"

#include <onion/fault_frame.h>

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void (*g_cleanup_handler)(void) = NULL;

void crash_log(const char *fmt, ...) {
	char msg[0x1000];
	va_list args;
	va_start(args, fmt);
	__builtin_vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	// Append newline at the end
	size_t msg_len = strlen(msg);
	if (msg_len < sizeof(msg) - 1) {
		msg[msg_len] = '\n';
		msg[msg_len + 1] = '\0';
	} else {
		msg[sizeof(msg) - 2] = '\n';
		msg[sizeof(msg) - 1] = '\0';
	}

	int fd = open("/data/OnionHEN/OnionHEN_util_crash.log", O_WRONLY | O_CREAT | O_APPEND, 0777);
	if (fd < 0) {
		return;
	}
	write(fd, msg, strlen(msg));
	close(fd);
	printf("[Crash Log]: %s", msg);  // msg already includes a newline
}

// NOLINTBEGIN(bugprone-signal-handler)

extern void shutdown_ipc(void);
extern void kill_loading_app(void);

static void __attribute__((used)) cleanup_and_throw(void) {
	//onion_notify(true, "Fatal error occured. Cleaning up, catching and exiting...");
	if (g_cleanup_handler != NULL) {
		g_cleanup_handler();
		g_cleanup_handler = NULL;
	}
	longjmp(g_catch_buf, 1);
	onion_notify(true, "OnionHEN utilities daemon has crashed ...\n\nSome OnionHEN features will be unavailable until you reboot");
	// TODO longjump here
}

static uintptr_t __attribute__((naked, noinline)) get_cleanup_function(void) {
	__asm__ volatile(
		"lea cleanup_and_throw(%rip), %rax\n"
		"ret\n"
	);
}
bool is_handler_enabled = true;
static void fault_handler(int sig) {
	if(!is_handler_enabled) {
		crash_log("Signal handler is disabled, ignoring signal %d", sig);
		return;
	}
	crash_log("signal %d received\n", sig);
	onion_print_backtrace(crash_log);
	/* Must read this frame directly — see onion/fault_frame.h. */
	onion_frame_t *frame = onion_current_frame();
	frame->addr = get_cleanup_function();
}

// NOLINTEND(bugprone-signal-handler)

void fault_handler_init(void (*cleanup_handler)(void)) {
	g_cleanup_handler = cleanup_handler;
	signal(SIGSEGV, fault_handler);
	signal(SIGILL, fault_handler);
	signal(10, fault_handler);
	signal(9, fault_handler);

}
