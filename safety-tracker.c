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
 * This code keeps track of idle/active periods for the user, and maintains a
 * status based on how hard the user is working.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "safety-tracker.h"

/**
 * This structure represents an interval for tracking cumulative activity.
 **/
struct tracking_period_config {
    /* The name of this tracking period (e.g. "micro", "normal", "workday") */
    const char *name;
    /* How many seconds a user can work before needing a break. */
    int limit_seconds;
    /**
     * How many seconds of inactivity will reset the active time accumulator,
     * must be less than `break_seconds`, or 0 to disable.
     **/
    int reset_seconds;
    /**
     * How many seconds of inactivity are needed to reset the active time
     * accumulator. Must be greater than `reset_seconds`.
     **/
    int break_seconds;
};

/**
 * This structure represents the current state of a tracked interval.
 **/
struct tracking_period {
    /* The configuration for this tracking period */
    struct tracking_period_config config;
    /**
     * An accumulator for the total active time measured for this period. Note
     * that it will be reset according to the config.
     **/
    int active_seconds;
    /**
     * TODO: in the future, the cumulative active time won't be enough. We'll
     * also want to track/log when the periods occur for reporting/charting.
     **/
};

/* TODO: these should be user-configurable */
/**
 * Global configuration for work/break durations
 **/
static struct tracking_period periods[] = {
    {
        .config = {
            .name = "micro",
            .limit_seconds = 3 * 60,
            .reset_seconds = 15,
            .break_seconds = 30,
        },
        .active_seconds = 0,
    },
    {
        .config = {
            .name = "normal",
            .limit_seconds = 45 * 60,
            .reset_seconds = 0,
            .break_seconds = 10 * 60,
        },
        .active_seconds = 0,
    },
    {
        .config = {
            .name = "workday",
            .limit_seconds = 4 * 60 * 60,
            .reset_seconds = 0,
            .break_seconds = 8 * 60 * 60,
        },
        .active_seconds = 0,
    },
};

/**
 * Get the number of different periods being tracked
 **/
static int tracker_count_periods(void)
{
    return sizeof(periods)/sizeof(periods[0]);
}

/**
 * This function receives the total number of idle seconds in a period of user
 * inactivity.
 **/
void tracker_provide_idle_seconds(int idle_seconds)
{
    for (int i = 0; i < tracker_count_periods(); i++) {
        struct tracking_period_config *config = &(periods[i].config);
        struct tracking_period *period = &(periods[i]);

        if (period->active_seconds > 0) {
            /* We only do a reset if there is some activity to clear */

            if (period->active_seconds < config->limit_seconds && \
                    config->reset_seconds > 0 && \
                    idle_seconds > config->reset_seconds
            ) {
                /**
                 * A reset is warranted (makes more sense for small intervals)
                 **/
                period->active_seconds = 0; 
            } else if (idle_seconds > config->break_seconds) {
                /**
                 * Whether beyond the period's limit or not, the number of
                 * elapsed idle seconds is greater than any break needed, so the
                 * accumulator for active time can be reset
                 **/
                printf("BREAK RESET\n");
                period->active_seconds = 0;
            }
        }
    } 
}

/**
 * This function receives individual periods of active seconds during user
 * activity. i.e. you can't give it a total number of active seconds, because
 * it needs to accumulate active time surrounding small-enough idle periods.
 * (i.e. small enough to not reset the accumulator) If you call this function
 * every 1s, then you would pass it the value 1 for each call, etc.
 **/
void tracker_provide_active_seconds(int active_seconds)
{
    for (int i = 0; i < tracker_count_periods(); i++) {
        struct tracking_period *period = &(periods[i]);

        period->active_seconds += active_seconds;
    } 
}

/**
 * This is a debugging function which prints out the status for all tracking
 * periods
 */
void tracker_display_nag_status(void)
{
    for (int i = 0; i < tracker_count_periods(); i++) {
        struct tracking_period_config *config = &(periods[i].config);
        struct tracking_period *period = &(periods[i]);

        const char *nag_status = NULL;

        if (period->active_seconds > config->limit_seconds) {
            nag_status = "BREAK REQUIRED";
        } else {
            nag_status = "SAFE";
        }

        fprintf(stderr, "%5i/%5i ('%s' period) [%s]\n",
            period->active_seconds,
            config->limit_seconds,
            config->name,
            nag_status
        );
    } 
}

/**
 * Get a JSON dump of all status for all tracking periods
 **/
char *tracker_get_status_json(void)
{
    char *buff = malloc(512);
    memset(buff, 0, 512);

    strcat(buff, "{\"periods\":[");
    for (int i = 0; i < tracker_count_periods(); i++) {
        struct tracking_period_config *config = &(periods[i].config);
        struct tracking_period *period = &(periods[i]);
        
        strcat(buff, "{\"name\":\"");
        strcat(buff, config->name);
        strcat(buff, "\",\"safe\":");
        
        if (period->active_seconds > config->limit_seconds) {
            strcat(buff, "false");
        } else {
            strcat(buff, "true");
        }
        
        strcat(buff, ",\"accumulated_seconds\":");
        char readout[16];
        sprintf(readout, "%i", period->active_seconds);
        strcat(buff, readout);

        strcat(buff, ",\"break_at\":");
        sprintf(readout, "%i", period->config.limit_seconds);
        strcat(buff, readout);

        strcat(buff, "},");
    } 
    int len = strlen(buff);
    buff[len-1] = 0;
    strcat(buff, "]}\n");

    return buff;
}
