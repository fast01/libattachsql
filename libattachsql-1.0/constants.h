/* vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * Copyright 2014 Hewlett-Packard Development Company, L.P.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain 
 * a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct attachsql_connect_t;
typedef struct attachsql_connect_t attachsql_connect_t;

enum attachsql_events_t
{
  ATTACHSQL_EVENT_NONE,
  ATTACHSQL_EVENT_CONNECTED,
  ATTACHSQL_EVENT_ERROR,
  ATTACHSQL_EVENT_EOF,
  ATTACHSQL_EVENT_ROW_READY
};

typedef void (attachsql_callback_fn)(attachsql_connect_t *con, attachsql_events_t events, void *context);

enum attachsql_return_t
{
  ATTACHSQL_RETURN_OK,
  ATTACHSQL_RETURN_NOT_CONNECTED,
  ATTACHSQL_RETURN_CONNECTING,
  ATTACHSQL_RETURN_IDLE,
  ATTACHSQL_RETURN_PROCESSING,
  ATTACHSQL_RETURN_ROW_READY,
  ATTACHSQL_RETURN_ERROR,
  ATTACHSQL_RETURN_EOF
};

#ifdef __cplusplus
}
#endif
