/**
 * Copyright © 2020 John Ferguson <src@jferg.net>
 *
 * This file is part of noRSI.
 *
 * noRSI is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * noRSI is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * noRSI.  If not, see <https://www.gnu.org/licenses/>.
 **/

#include <bits/time.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "idle-client-protocol.h"
#include "query-handler.h"
#include "safety-tracker.h"

void handle_sigterm(void);
void handle_sigint(void);
void cleanup_all(void);
void signal_handler(int signo);

/**
 * TODO: if user remains active for some period while program starts up, then
 * they are marked as "USER_UNKOWN." This is inaccurate. The absence of an idle
 * timeout should be taken as activity.
 *
 * So: state should immediately be set to USER_ACTIVE, and timer in global
 * state set so it starts counting properly... Should test if this does the
 * right thing when the program is launched by a timer or something.
 **/

enum user_activity_state {
    USER_UNKNOWN,
    USER_IDLE,
    USER_ACTIVE,
};

/* Global state singleton */
struct norsi_state {
    /* Wayland display */
    struct wl_display *display;
    /* Wayland seat */
    struct wl_seat *seat;
    /* KDE Idle Manager (hands out timeout objects) */
    struct org_kde_kwin_idle *idle_manager;
    /* KDE Idle Timeout (used to see when user is inactive) */
    struct org_kde_kwin_idle_timeout *idle_timeout;
    /* --- */
    /* 0 => no change in user state, other => check for change */
    int check_user_state;
    /* User's current state (i.e. idle or active) */
    enum user_activity_state user_state;
    /* Monotonic timestamp from when user state last changed */
    struct timespec user_state_timestamp;
};

static struct norsi_state main_state = {
    .display = NULL,
    .seat = NULL,
    .idle_manager = NULL,
    .idle_timeout = NULL,
    .check_user_state = 1,
    .user_state = USER_UNKNOWN,
    .user_state_timestamp = {0}
};

/*******************************************************************************
 * Registry Handlers
 ******************************************************************************/

/* After connecting to the display, listen for globals */
static void registry_listener_global(void *data,
    struct wl_registry *wl_registry, uint32_t name, const char *interface,
    uint32_t version
)
{
    /* Keep track of bindings in global state */
    struct norsi_state *state = data;
    
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        /* Bind to the seat interface (idle timeouts are per-seat) */
        state->seat = wl_registry_bind(
            wl_registry, name, &wl_seat_interface, 7
        );
    }
    if (strcmp(interface, org_kde_kwin_idle_interface.name) == 0) {
        /* Bind to the idle manager interface to set up timeouts */
        state->idle_manager = wl_registry_bind(
            wl_registry, name, &org_kde_kwin_idle_interface, 1
        );
    }
}

static void registry_listener_global_remove(void *data,
    struct wl_registry *wl_registry, uint32_t name
)
{
    /* Unused */
}

/* Listener to pick up globals after connecting to display */
static const struct wl_registry_listener registry_listener = {
    .global = registry_listener_global,
    .global_remove = registry_listener_global_remove,
};

/*******************************************************************************
 * Idle Timeout Handlers
 ******************************************************************************/

/* Handler for when user goes idle */
static void idle_timer_idle(void *data, 
    struct org_kde_kwin_idle_timeout *timeout
)
{
    struct norsi_state *state = data;
    state->user_state = USER_IDLE;
    clock_gettime(CLOCK_MONOTONIC, &(state->user_state_timestamp));
    state->check_user_state = 1;
}

/* handler for when user becomes active */
static void idle_timer_resumed(void *data,
    struct org_kde_kwin_idle_timeout *timeout
)
{
    struct norsi_state *state = data;
    state->user_state = USER_ACTIVE;
    clock_gettime(CLOCK_MONOTONIC, &(state->user_state_timestamp));
    state->check_user_state = 1;
}

/* Listener to pick up changes in user's activity level */
struct org_kde_kwin_idle_timeout_listener idle_timer_listener = {
    .idle = idle_timer_idle,
    .resumed = idle_timer_resumed,
};

/*******************************************************************************
 * Main Logic
 ******************************************************************************/

/**
 * Called at end of program to clean up/free resources
 **/
void cleanup_all(void)
{
    printf("cleaning up query handler\n");
    query_handler_cleanup();

    printf("cleaning up wayland objects\n");
    if (main_state.idle_timeout != NULL) {
        org_kde_kwin_idle_timeout_release(main_state.idle_timeout);
        /* TODO: figure out why call to _timeout_destroy causes segfault */
        //org_kde_kwin_idle_timeout_destroy(main_state.idle_timeout);
        /* Is _timeout_release handling this for us? */
        main_state.idle_timeout = NULL;
    }

    if (main_state.idle_manager != NULL) {
        org_kde_kwin_idle_destroy(main_state.idle_manager);
        main_state.idle_manager = NULL;
    }

    if (main_state.seat != NULL) {
        wl_seat_destroy(main_state.seat);
        main_state.seat = NULL;
    }

    if (main_state.display != NULL) {
        wl_display_disconnect(main_state.display);
        main_state.display = NULL;
    }

    printf("cleanup finished\n");
    exit(1);
}

/**
 * SIGINT handler
 */
void handle_sigint(void)
{
    cleanup_all();
}

/**
 * SIGTERM handler
 */
void handle_sigterm(void)
{
    cleanup_all();
}

/**
 * Global signal handler
 **/
void signal_handler(int signo)
{
    switch (signo) {
        case SIGINT:
            fprintf(stderr, "received SIGINT\n");
            handle_sigint();
            break;
        case SIGTERM:
            fprintf(stderr, "received SIGTERM\n");
            handle_sigterm();
            break;
        default:
            fprintf(stderr, "received unhandled signal (%i)\n", signo);
            break;
    }
}

int main(int argc, char *argv[])
{
    /* TODO: ensure that noRSI isn't already running */

    /* Set-up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /** 
     * Connect to the display, and check the registry for the support we need
     * to set up seat, idle timers, etc.
     **/
    main_state.display = wl_display_connect(NULL);

    struct wl_registry *registry = wl_display_get_registry(main_state.display);
    wl_registry_add_listener(registry, &registry_listener, &main_state);

    wl_display_roundtrip(main_state.display);
    wl_registry_destroy(registry);

    if (main_state.seat == NULL) {
        fprintf(stderr, "No seat was found\n");
        return -1;
    }
    if (main_state.idle_manager == NULL) {
        fprintf(stderr, "No support for idle management found\n");
        return -1;
    }

    /* Create a new timeout */
    main_state.idle_timeout = org_kde_kwin_idle_get_idle_timeout(
        main_state.idle_manager,
        main_state.seat,
        1000 /* ms */
    );

    /* Add listeners to the newly created timeout */
    org_kde_kwin_idle_timeout_add_listener(
        main_state.idle_timeout,
        &idle_timer_listener,
        &main_state
    );
    wl_display_roundtrip(main_state.display);

    /* we need this to manually manage the event loop */
    int display_fd = wl_display_get_fd(main_state.display);

    /* Now that idle management is sorted, start up our query handler */
    query_handler_init_server();

    /* Here we'll poll only the wayland display's FD */
    struct pollfd display_poll_fd = {
        .fd = display_fd,
        .events = POLLIN,
        .revents = 0,
    };

    /**
     * When this isn't -1, it gives the timestamp for when we last told the
     * safety tracker that the user was active (for a given period of activity,
     * i.e. it will always be reset when the user goes from IDLE -> ACTIVE).
     **/
    int last_active_update = -1;

    /* This will keep running until it receives a signal from the OS */
    while (1) {
        /* Handle Wayland business*/
        if (poll(&display_poll_fd, 1, 20) > 0) {
            /* process incoming events */
            wl_display_dispatch(main_state.display);
            /* flush outgoing requests */
            wl_display_flush(main_state.display);
        }

        /* Take note if the timeouts indicate a change in the user's state */
        if (main_state.check_user_state) {
          main_state.check_user_state = 0;

          switch (main_state.user_state) {
          case USER_UNKNOWN:
            fprintf(stderr, "user state unknown\n");
            break;
          case USER_IDLE:
            fprintf(stderr, "user is idle\n");
            break;
          case USER_ACTIVE:
            fprintf(stderr, "user is active\n");
            last_active_update = -1;
            break;
          }
        }

        if (main_state.user_state != USER_UNKNOWN) {
            struct timespec now;
            struct timespec *last_change = \
                &main_state.user_state_timestamp;
            clock_gettime(CLOCK_MONOTONIC, &now);

            /* If we're IDLE, we've already been idle for t_timeout */
            /* If we're active, it starts at 0 (immediately) */
            int offset = main_state.user_state == USER_IDLE ? 1 : 0;
            int elapsed_s = offset + now.tv_sec - last_change->tv_sec;

            switch (main_state.user_state) {
                case USER_UNKNOWN:
                    /* Nothing to report */
                    break;
                case USER_IDLE:
                    /* Tell the tracker the total time we've been IDLE */
                    tracker_provide_idle_seconds(elapsed_s);
                    break;
                case USER_ACTIVE:
                    /* Tell the tracker how much longer we've been ACTIVE */
                    if (elapsed_s > 0 && last_active_update < now.tv_sec) {
                        /* Only report if at least 1 second has elapsed */
                        if (last_active_update == -1) {
                            /* We just chanaged to the active state */
                            last_active_update = last_change->tv_sec;
                        }
                        tracker_provide_active_seconds(
                            now.tv_sec - last_active_update
                        );
                        last_active_update = now.tv_sec;
                    }
                    break;
            }
        }

        /* Handle any network activity from clients */
        query_handler_run();
    }
}
