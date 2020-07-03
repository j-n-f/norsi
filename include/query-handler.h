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

#ifndef QUERY_HANDLER_H
#define QUERY_HANLDER_H

int query_handler_init_server(void);
int query_handler_run(void);
int query_handler_cleanup(void);

#endif
