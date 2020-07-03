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

#ifndef SAFETY_TRACKER_H
#define SAFETY_TRACKER_H

void tracker_provide_idle_seconds(int idle_seconds);
void tracker_provide_active_seconds(int active_seconds);
void tracker_display_nag_status(void);
char *tracker_get_status_json(void);

#endif
