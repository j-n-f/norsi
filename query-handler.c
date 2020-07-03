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

/**
 * Handling of requests from other clients for the current idle/active state of
 * the current wayland seat.
 *
 * - determines path for server socket
 * - creates folder for socket under XDG runtime path
 * - initializes the server, and listens for new connections
 * - manages connections, and responds to requests made on those connections
 **/

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "query-handler.h"
#include "safety-tracker.h"

/**
 * The maximum number of backlogged connection requests
 **/
#define QUERY_HANDLER_MAX_CONNECTION_BACKLOG 1

/**
 * The maximum number of simultaneous active client connections
 **/
#define QUERY_HANDLER_MAX_CLIENTS 16

/**
 * Maximum buffer length for incoming/outgoing data
 **/
#define QUERY_HANDLER_MAX_CLIENT_BUFFER 1024

/**
 * The state kept for each connected client
 **/
struct client_state {
    /* Input buffer */
    unsigned char in[QUERY_HANDLER_MAX_CLIENT_BUFFER];
    /* Amount of data in input buffer */
    int in_len;
    /* non-zero => a full message is ready for handling in input buffer */
    int in_ready;
    /* Output buffer */
    unsigned char out[QUERY_HANDLER_MAX_CLIENT_BUFFER];
    /* Amount of data in output buffer */
    int out_len;
};

/**
 * e.g. /run/user/1000/norsi
 **/
static char socket_folder[PATH_MAX] = {0};

/**
 * e.g. /run/user/1000/norsi/socket.sock
 **/
static char socket_path_full[PATH_MAX] = {0};

/**
 * The main listening socket for incoming connections
 **/
static int socket_listener_fd = -1;

/**
 * An entry for the state of each connected client
 **/
static struct client_state client_state[QUERY_HANDLER_MAX_CLIENTS] = {0};

/**
 * Polling configuration for active client connections
 **/
static struct pollfd conn_poll_fds[QUERY_HANDLER_MAX_CLIENTS] = {0};

/**
 * Get the folder that the socket file is created in
 *
 * e.g. /run/user/1000/norsi
 *
 * note: in the future we may support systems that don't use XDG
 **/
static const char *query_handler_get_socket_folder(void)
{
    char *folder = NULL;
    int curr_path_len = 0;

    if (socket_folder[0] == '\0') {
        /* socket folder hasn't been calculated yet */
        char *xdg_path = getenv("XDG_RUNTIME_DIR");

        if (xdg_path != NULL) {
            strncpy(socket_folder, xdg_path, PATH_MAX);
            curr_path_len += strlen(socket_folder);

            strncat(socket_folder, "/norsi", PATH_MAX - 1 - curr_path_len);

            folder = socket_folder;
        } else {
            /* TODO: handle XDG not available (e.g. use /tmp instead) */
        }
    } else {
        /* cached value can be returned */
        folder = socket_folder;
    }

    return folder;
}

/**
 * Get the full path to the filename of the socket file
 *
 * e.g. /run/user/1000/norsi/socket.sock
 *
 * note: in the future we may support systems that don't use XDG
 **/
static const char *query_handler_get_full_socket_path(void)
{
    char *path = NULL;
    int curr_path_len = 0;

    if (socket_path_full[0] == '\0') {
        /* full socket path hasn't been calculated yet */
        const char *folder = query_handler_get_socket_folder();

        if (folder != NULL) {
            strncpy(socket_path_full, folder, PATH_MAX);
            curr_path_len += strlen(socket_path_full);

            strncat(
                socket_path_full,
                "/socket.sock",
                PATH_MAX - 1 - curr_path_len
            );

            path = socket_path_full;
        } else {
            /* TODO: handle not being able to get socket folder */
        }
    } else {
        /* cached value can be returned */
        path = socket_path_full;
    }

    return path;
}

/**
 * utility function to make a socket non-blocking
 *
 * Returns 0 on success, -1 otherwise (check errno)
 **/
static int query_handler_make_socket_nonblocking(int fd)
{
    int socket_flags = fcntl(fd, F_GETFL, 0);
    socket_flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, socket_flags);
}

/**
 * Call this once at start-up to initialize the query handler
 **/
int query_handler_init_server(void)
{
    int socket_fd;
    struct sockaddr_un addr;
    const char *sock_folder = query_handler_get_socket_folder();
    const char *sock_path = query_handler_get_full_socket_path();

    /* Initialize connection polling configs */
    memset(conn_poll_fds, 0, sizeof(struct pollfd) * QUERY_HANDLER_MAX_CLIENTS);
    for (int i = 0; i < QUERY_HANDLER_MAX_CLIENTS; i++) {
        conn_poll_fds[i].fd = -1;
    }

    /* validate socket configuration */
    if (sock_path == NULL) {
        /**
         * TODO: handle being unable to service requests
         *
         * This shouldn't interfere with logging/history, but it will make any
         * sort of UI the user has rigged up non-functional
         **/
        fprintf(stderr, "no socket path during init\n");
    }

    /**
     * TODO: currently assuming that program was properly shut down, so we don't
     * have to deal with folders/sockets not being cleaned up. In the future,
     * this should be more robust, and clean up existing resources before
     * creating new ones. (if another process isn't already running, but the
     * main startup code shouldn't even be calling this function in that case)
     **/
    /* Create a folder for the socket */
    if (mkdir(sock_folder, 0700) == -1) {
        /* TODO: handle failure to create socket folder */
        fprintf(
            stderr, "failed to create %s (%s)\n", sock_folder, strerror(errno)
        );
    }

    /* Create a socket at the configured path */
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        /* TODO: handle */
        fprintf(stderr, "unable to create socket during init\n");
    }
   
    if (query_handler_make_socket_nonblocking(socket_fd) == -1) {
        fprintf(
            stderr,
            "couldn't make socket nonblocking during init (%s)\n",
            strerror(errno)
        );
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        fprintf(
            stderr, "couldn't bind socket during init (%s)\n", strerror(errno)
        );
    }

    if (listen(socket_fd, QUERY_HANDLER_MAX_CONNECTION_BACKLOG) == -1) {
        fprintf(
            stderr,
            "couldn't listen on socket during init (%s)\n",
            strerror(errno)
        );
    }

    /* Hang on to this FD globally */
    socket_listener_fd = socket_fd;

    return 0;
}

/**
 * Get the number of actively connected clients
 */
static int query_handler_current_client_count(void)
{
    int count = 0;

    for (int i = 0; i < QUERY_HANDLER_MAX_CLIENTS; i++) {
        if (conn_poll_fds[i].fd >= 0) {
            count++;
        }
    }

    return count;
}

/**
 * Given a newly connected client's FD, store it in the global list used to
 * manage connections.
 **/
static void query_handler_store_connection(int fd)
{
    for (int i = 0; i < QUERY_HANDLER_MAX_CLIENTS; i++) {
        if (conn_poll_fds[i].fd == -1) {
            conn_poll_fds[i].fd = fd;
            conn_poll_fds[i].events = POLLIN;
            conn_poll_fds[i].revents = 0;

            memset(&(client_state[i]), 0, sizeof(struct client_state));

            query_handler_make_socket_nonblocking(fd);

            break;
        }
    }
}

/**
 * Checks if there are any complete messages that need handling
 *
 * Returns 1 if any complete messages are ready for handling
 **/
static int query_handler_message_ready(int client_id)
{
    struct client_state *cs = &(client_state[client_id]);
    unsigned char *buff = cs->in;

    for (int i = 0; i < cs->in_len; i++) {
        if (buff[i] == '\n') {
            return 1;
        }
    }

    return 0;
}

/**
 * Handle a single message from some client's input buffer, causing responses
 * to be written to their output buffer.
 *
 * returns non-zero on success
 **/
static int query_handler_handle_message(int client_id)
{
    /* TODO: bounds checks */
    struct client_state *cs = &(client_state[client_id]);
    unsigned char *buff = cs->in;
    char parse_buff[QUERY_HANDLER_MAX_CLIENT_BUFFER];
    int msg_len = 0;
    
    memset(parse_buff, 0, QUERY_HANDLER_MAX_CLIENT_BUFFER);

    for (int i = 0; i < cs->in_len; i++) {
        if (buff[i] == '\n') {
            msg_len = i;
            break;
        }
    }

    memcpy(parse_buff, cs->in, msg_len);
    parse_buff[msg_len] = '\0';

    if (strcmp(parse_buff, "status") == 0) {
        printf("client %i requested status\n", client_id);

        char *status = tracker_get_status_json();
        /**
         * TODO: can't do this if we don't have enough in out buffer (i.e. N
         * consecutive status requests)
         **/
        memcpy(cs->out, status, strlen(status));
        cs->out_len = strlen(status);
        free(status);
    } else if (strcmp(parse_buff, "info") == 0) {
        /* TODO: this is just a dummy handler for testing */
        printf("client %i requested info\n", client_id);
    } else {
        printf("client %i made unknown request\n", client_id);
    }

    /* Shift the input buffer to handle the next message */
    /* +1 is for trailing newline */
    memmove(cs->in, &(cs->in[msg_len + 1]), cs->in_len - (msg_len + 1));
    cs->in_len -= msg_len + 1;

    return 1;
}

/**
 * This function is non-blocking and must be called continuously by the main
 * loop so that new connections are accepted, existing connections can have
 * their requests responded to, and so on.
 *
 * Returns 0 if everything is fine, -1 otherwise
 **/
int query_handler_run(void)
{
    /**
     * 1. Accept any new connections and store the FD, and set up for polling
     * 2. Read in any new data from existing connections, buffer it till a full
     *    request is present
     * 3. Create and send responses for any connections that have made valid
     *    requests
     * 4. Shutdown/cleanup any connections that have been let go
     **/

    struct pollfd listener_poll_fd = {
        .fd = socket_listener_fd,
        .events = POLLIN,
        .revents = 0,
    };

    /* Handle any new incoming connections */
    if (poll(&listener_poll_fd, 1, 0) == 1) {
        /* new connections are coming in */
        if (query_handler_current_client_count() < QUERY_HANDLER_MAX_CLIENTS) {
            /* we have room to handle a new connection */
            int new_conn_fd = accept(socket_listener_fd, NULL, NULL);

            if (new_conn_fd != -1) {
                printf("new client connection, fd=%i\n", new_conn_fd);
                query_handler_store_connection(new_conn_fd); 
            } else {
                fprintf(
                    stderr,
                    "failed to accept incoming client connection (%s)\n",
                    strerror(errno)
                );
            }
        } else {
            fprintf(stderr, "too many clients connected to accept another\n");
        }
    }

    /**
     *  1. Poll for POLLIN flags
     *  2. > 0 means some clients received requests
     *  3. read data for all with POLLIN revents
     *  4. subroutine checks if a full request came in
     *  5. subroutine cooks up a response and stores it in outgoing buffer
     *  6. If outgoing data is available, POLLOUT flag is set
     *  7. for all pollfd with revents POLLOUT, write out data from outgoing
     *     buffer, if all of it was written, clear events POLLOUT (as we don't
     *     need to know when to write)
     **/

    /* Handle any existing connections */
    if (poll(conn_poll_fds, QUERY_HANDLER_MAX_CLIENTS, 0) > 0) {
        for (int i = 0; i < QUERY_HANDLER_MAX_CLIENTS; i++) {
            struct pollfd *pfd = &(conn_poll_fds[i]);
            struct client_state *cs = &(client_state[i]);
           
            if (!(pfd->revents & (POLLIN | POLLOUT))) {
                /* skip handling if no flags are set for client */
                continue;
            }

            /* receive incoming data */
            if (pfd->revents & POLLIN) {
                unsigned char *read_to = &(cs->in[cs->in_len]);
                int max_read = QUERY_HANDLER_MAX_CLIENT_BUFFER - cs->in_len;
            
                int read_count = read(pfd->fd, read_to, max_read);
                cs->in_len = read_count;

                if (read_count == 0) {
                    /* No data waiting */
                    pfd->revents &= ~POLLIN;
                    shutdown(pfd->fd, SHUT_RDWR);
                    close(pfd->fd);
                    pfd->fd = -1;
                } else if (read_count == -1) {
                    fprintf(
                        stderr,
                        "unable to read client request (%s)\n",
                        strerror(errno)
                    );
                    continue;
                } else {
                    pfd->revents &= ~POLLIN;
               
                    if (query_handler_message_ready(i)) {
                        cs->in_ready = 1;
                    }
                }
            }

            /* send outgoing data */
            if (pfd->revents & POLLOUT) {
                int write_count = write(
                    conn_poll_fds[i].fd, cs->out, cs->out_len
                );

                if (write_count == -1) {
                    fprintf(
                        stderr,
                        "unable to write to client (%s)\n",
                        strerror(errno)
                    );
                    continue;
                }

                if (write_count == cs->out_len) {
                    /* We don't have to keep polling to write */
                    cs->out_len = 0;
                    pfd->events &= ~POLLOUT;
                } else if (write_count < cs->out_len) {
                    /**
                     * note that some data was sent, and shift what's left to
                     * the start of the buffer
                     **/
                    memcpy(
                        cs->out,
                        &(cs->out[write_count]),
                        cs->out_len - write_count
                    );
                    cs->out_len -= write_count;
                }
            }

            /* handle any complete messages in, queueing responses */
            if (query_handler_message_ready(i)) {
                /**
                 * TODO: it could be that we won't handle all messages that are
                 * queued up because we don't have enough room in the output
                 * buffer for all responses
                 **/
                while (query_handler_message_ready(i)) {
                    query_handler_handle_message(i);
                }

                /**
                 * If any responses are queued, wait till the socket becomes
                 * writeable
                 **/
                if (cs->out_len > 0) {
                    pfd->events |= POLLOUT;
                }
            }
        }
    }

    return 0;
}

/**
 * Call this function before shutting down the program so that folders, sockets,
 * and connections can be cleaned up.
 *
 * Returns 0 if everything is fine, -1 otherwise
 **/
int query_handler_cleanup(void)
{
    /* Shut down all clients */
    for (int i = 0; i < QUERY_HANDLER_MAX_CLIENTS; i++) {
        struct pollfd *pfd = &(conn_poll_fds[i]);

        if (pfd->fd != -1) {
            printf("dropping connection to client[%i]\n", i);
            shutdown(pfd->fd, SHUT_RDWR);
            close(pfd->fd);
            pfd->fd = -1;
        }
    }

    /* Shut down server listener */
    if (socket_listener_fd != -1) {
        shutdown(socket_listener_fd, SHUT_RDWR);
        close(socket_listener_fd);

        unlink(socket_path_full);
    }

    /* Cleanup socket/folder so next invocation will go cleanly */
    if (rmdir(socket_folder) == -1) {
        fprintf(
            stderr, "failed to delete folder: %s (%s)\n",
            socket_folder,
            strerror(errno
        ));
    }

    return 0;
}
