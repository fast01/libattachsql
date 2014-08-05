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

#include "config.h"
#include "common.h"
#include "connect.h"
#include "sha1.h"
#include "net.h"
#include <errno.h>
#include <string.h>

ascon_st *ascore_con_create(const char *host, in_port_t port, const char *user, const char *pass, const char *schema)
{
  ascon_st *con;

  con = new (std::nothrow) ascon_st;
  if (con == NULL)
  {
    return NULL;
  }

  con->host= host;
  con->port= port;
  if ((user == NULL) or (strlen(user) > ASCORE_MAX_USER_SIZE))
  {
    con->local_errcode= ASRET_USER_TOO_LONG;
    con->status= ASCORE_CON_STATUS_PARAMETER_ERROR;
    return con;
  }
  con->user= user;
  // We don't really care how long pass is since we itterate though it during
  // SHA1 passes.  Needs to be nul terminated.  NULL is also acceptable.
  con->pass= pass;
  if ((schema == NULL) or (strlen(schema) > ASCORE_MAX_SCHEMA_SIZE))
  {
    con->local_errcode= ASRET_SCHEMA_TOO_LONG;
    con->status= ASCORE_CON_STATUS_PARAMETER_ERROR;
    return con;
  }
  con->schema= schema;

  return con;
}

void ascore_con_set_option(ascon_st *con, ascore_con_options_t option, bool value)
{
  switch (option)
  {
    case ASCORE_CON_OPTION_POLLING:
      con->options.polling= value;
      break;
    case ASCORE_CON_OPTION_RAW_SCRAMBLE:
      con->options.raw_scramble= value;
      break;
    case ASCORE_CON_OPTION_FOUND_ROWS:
      con->options.found_rows= value;
      break;
    case ASCORE_CON_OPTION_INTERACTIVE:
      con->options.interactive= value;
      break;
    case ASCORE_CON_OPTION_MULTI_STATEMENTS:
      con->options.multi_statements= value;
      break;
    case ASCORE_CON_OPTION_AUTH_PLUGIN:
      con->options.auth_plugin = value;
      break;
    case ASCORE_CON_OPTION_PROTOCOL_TCP:
      con->options.protocol= ASCORE_CON_PROTOCOL_TCP;
      break;
    case ASCORE_CON_OPTION_PROTOCOL_UDS:
      con->options.protocol= ASCORE_CON_PROTOCOL_UDS;
      break;
    default:
      /* This will only happen if an invalid options is provided */
      return;
  }
}

void ascore_con_destroy(ascon_st *con)
{
  if (con == NULL)
  {
    return;
  }

  if (con->read_buffer != NULL)
  {
    ascore_buffer_free(con->read_buffer);
  }

  if (con->uv_objects.stream != NULL)
  {
    uv_close((uv_handle_t*)con->uv_objects.stream, NULL);
    uv_run(con->uv_objects.loop, UV_RUN_DEFAULT);
  }
  uv_loop_delete(con->uv_objects.loop);
  delete con;
}

bool ascore_con_get_option(ascon_st *con, ascore_con_options_t option)
{
  switch(option)
  {
    case ASCORE_CON_OPTION_POLLING:
      return con->options.polling;
      break;
    // TODO: this is to do with auth plugins
    case ASCORE_CON_OPTION_RAW_SCRAMBLE:
      return con->options.raw_scramble;
      break;
    case ASCORE_CON_OPTION_FOUND_ROWS:
      return con->options.found_rows;
      break;
    case ASCORE_CON_OPTION_INTERACTIVE:
      return con->options.interactive;
      break;
    case ASCORE_CON_OPTION_MULTI_STATEMENTS:
      return con->options.multi_statements;
      break;
    case ASCORE_CON_OPTION_AUTH_PLUGIN:
      return con->options.auth_plugin;
      break;
    case ASCORE_CON_OPTION_PROTOCOL_TCP:
      if (con->options.protocol == ASCORE_CON_PROTOCOL_TCP)
      {
        return true;
      }
      return false;
      break;
    case ASCORE_CON_OPTION_PROTOCOL_UDS:
      if (con->options.protocol == ASCORE_CON_PROTOCOL_UDS)
      {
        return true;
      }
      return false;
      break;
    default:
      break;
  }
  return false;
}

void on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
  ascon_st *con= (ascon_st *)resolver->loop->data;

  asdebug("Resolver callback");
  if (status != 0)
  {
    asdebug("DNS lookup failure: %s", uv_err_name(uv_last_error(resolver->loop)));
    con->status= ASCORE_CON_STATUS_CONNECT_FAILED;
    con->local_errcode= ASRET_DNS_ERROR;
    snprintf(con->errmsg, ASCORE_ERROR_BUFFER_SIZE, "DNS lookup failure: %s", uv_err_name(uv_last_error(resolver->loop)));
    return;
  }
  char addr[17] = {'\0'};

  uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
  asdebug("DNS lookup success: %s", addr);
  uv_tcp_init(resolver->loop, &con->uv_objects.socket.tcp);

  con->uv_objects.connect_req.data= (void*) &con->uv_objects.socket.tcp;
  uv_tcp_connect(&con->uv_objects.connect_req, &con->uv_objects.socket.tcp, *(struct sockaddr_in*) res->ai_addr, on_connect);

  uv_freeaddrinfo(res);
}

ascore_con_status_t ascore_con_poll(ascon_st *con)
{
  //asdebug("Connection poll");
  if (con == NULL)
  {
    return ASCORE_CON_STATUS_PARAMETER_ERROR;
  }

  if ((con->status == ASCORE_CON_STATUS_NOT_CONNECTED) or (con->status == ASCORE_CON_STATUS_CONNECT_FAILED) or (con->status == ASCORE_CON_STATUS_IDLE))
  {
    return con->status;
  }
  if (con->options.polling)
  {
    uv_run(con->uv_objects.loop, UV_RUN_NOWAIT);
  }
  else
  {
    uv_run(con->uv_objects.loop, UV_RUN_DEFAULT);
  }

  return con->status;
}

ascore_con_status_t ascore_connect(ascon_st *con)
{
  int ret;

  con->uv_objects.hints.ai_family = PF_INET;
  con->uv_objects.hints.ai_socktype = SOCK_STREAM;
  con->uv_objects.hints.ai_protocol = IPPROTO_TCP;
  con->uv_objects.hints.ai_flags = 0;

  if (con == NULL)
  {
    return ASCORE_CON_STATUS_PARAMETER_ERROR;
  }
  if (con->status != ASCORE_CON_STATUS_NOT_CONNECTED)
  {
    return con->status;
  }
  con->uv_objects.loop= uv_default_loop();

  con->uv_objects.loop->data= con;

  snprintf(con->str_port, 6, "%d", con->port);
  // If port is 0 and no explicit option set then assume we mean UDS
  // instead of TCP
  if (con->options.protocol == ASCORE_CON_PROTOCOL_UNKNOWN)
  {
    if (con->port == 0)
    {
      con->options.protocol= ASCORE_CON_PROTOCOL_UDS;
    }
    else
    {
      con->options.protocol= ASCORE_CON_PROTOCOL_TCP;
    }
  }
  switch(con->options.protocol)
  {
    case ASCORE_CON_PROTOCOL_TCP:
      asdebug("TCP connection");
      asdebug("Async DNS lookup: %s", con->host);
      ret= uv_getaddrinfo(con->uv_objects.loop, &con->uv_objects.resolver, on_resolved, con->host, con->str_port, &con->uv_objects.hints);
      if (ret)
      {
        asdebug("DNS lookup fail: %s", uv_err_name(uv_last_error(con->uv_objects.loop)));
        con->local_errcode= ASRET_DNS_ERROR;
        snprintf(con->errmsg, ASCORE_ERROR_BUFFER_SIZE, "DNS lookup failure: %s", uv_err_name(uv_last_error(con->uv_objects.loop)));
        con->status= ASCORE_CON_STATUS_CONNECT_FAILED;
        return con->status;
      }
      con->status= ASCORE_CON_STATUS_CONNECTING;
      if (con->options.polling)
      {
        uv_run(con->uv_objects.loop, UV_RUN_NOWAIT);
      }
      else
      {
        uv_run(con->uv_objects.loop, UV_RUN_DEFAULT);
      }
      break;
    case ASCORE_CON_PROTOCOL_UDS:
      asdebug("UDS connection");
      uv_pipe_init(con->uv_objects.loop, &con->uv_objects.socket.uds, 1);
      con->uv_objects.connect_req.data= (void*) &con->uv_objects.socket.uds;
      con->status= ASCORE_CON_STATUS_CONNECTING;
      uv_pipe_connect(&con->uv_objects.connect_req, &con->uv_objects.socket.uds, con->host, on_connect);
      if (con->options.polling)
      {
        uv_run(con->uv_objects.loop, UV_RUN_NOWAIT);
      }
      else
      {
        uv_run(con->uv_objects.loop, UV_RUN_DEFAULT);
      }
      break;
    case ASCORE_CON_PROTOCOL_UNKNOWN:
      asdebug("Unknown protocol, this shouldn't happen");
      con->status= ASCORE_CON_STATUS_CONNECT_FAILED;
  }

  return con->status;
}


void on_connect(uv_connect_t *req, int status)
{
  ascon_st *con= (ascon_st*)req->handle->loop->data;
  asdebug("Connect event callback");
  if (status != 0)
  {
    asdebug("Connect fail: %s", uv_err_name(uv_last_error(req->handle->loop)));
    con->local_errcode= ASRET_CONNECT_ERROR;
    con->status= ASCORE_CON_STATUS_CONNECT_FAILED;
    snprintf(con->errmsg, ASCORE_ERROR_BUFFER_SIZE, "Connection failed: %s", uv_err_name(uv_last_error(req->handle->loop)));
    return;
  }
  asdebug("Connection succeeded!");
  con->next_packet_type= ASCORE_PACKET_TYPE_HANDSHAKE;
  // maybe move the set con->stream to connect function
  con->uv_objects.stream= (uv_stream_t*)req->data;
  uv_read_start((uv_stream_t*)req->data, on_alloc, ascore_read_data_cb);
}

uv_buf_t on_alloc(uv_handle_t *client, size_t suggested_size)
{
  size_t buffer_free;
  uv_buf_t buf;
  ascon_st *con= (ascon_st*) client->loop->data;

  asdebug("%zd bytes requested for read buffer", suggested_size);

  if (con->read_buffer == NULL)
  {
    asdebug("Creating read buffer");
    con->read_buffer= ascore_buffer_create();
  }
  buffer_free= ascore_buffer_get_available(con->read_buffer);
  if (buffer_free < suggested_size)
  {
    asdebug("Enlarging buffer, free: %zd, requested: %zd", buffer_free, suggested_size);
    ascore_buffer_increase(con->read_buffer);
    buffer_free= ascore_buffer_get_available(con->read_buffer);
  }
  buf.base= con->read_buffer->buffer_write_ptr;
  buf.len= buffer_free;

  return buf;
}

void ascore_packet_read_handshake(ascon_st *con)
{
  asdebug("Connect handshake packet");
  buffer_st *buffer= con->read_buffer;

  // Protocol version
  if (buffer->buffer_read_ptr[0] != 10)
  {
    // Note that 255 is a special immediate auth fail case
    asdebug("Bad protocol version");
    con->local_errcode= ASRET_BAD_PROTOCOL;
    snprintf(con->errmsg, ASCORE_ERROR_BUFFER_SIZE, "Incompatible protocol version");
    return;
  }

  // Server version (null-terminated string)
  buffer->buffer_read_ptr++;
  strncpy(con->server_version, buffer->buffer_read_ptr, ASCORE_MAX_SERVER_VERSION_LEN);
  buffer->buffer_read_ptr+= strlen(con->server_version) + 1;

  // Thread ID
  con->thread_id= ascore_unpack_int4(buffer->buffer_read_ptr);
  buffer->buffer_read_ptr+= 4;

  // Scramble buffer and 1 byte filler
  memcpy(con->scramble_buffer, buffer->buffer_read_ptr, 8);
  buffer->buffer_read_ptr+= 9;

  // Server capabilities
  con->server_capabilities= (ascore_capabilities_t)ascore_unpack_int2(buffer->buffer_read_ptr);
  buffer->buffer_read_ptr+= 2;
  // Check MySQL 4.1 protocol capability is on, we won't support old auth
  if (not (con->server_capabilities & ASCORE_CAPABILITY_PROTOCOL_41))
  {
    asdebug("MySQL <4.1 Auth not supported");
    con->local_errcode= ASRET_NO_OLD_AUTH;
    snprintf(con->errmsg, ASCORE_ERROR_BUFFER_SIZE, "MySQL 4.1 protocol and higher required");
  }

  con->charset= buffer->buffer_read_ptr[0];
  buffer->buffer_read_ptr++;

  con->server_status= ascore_unpack_int2(buffer->buffer_read_ptr);
  // 13 byte filler and unrequired scramble length (until auth plugins)
  buffer->buffer_read_ptr+= 15;

  memcpy(con->scramble_buffer + 8, buffer->buffer_read_ptr, 12);
  // '\0' scramble terminator
  buffer->buffer_read_ptr+= 13;

  // MySQL 5.5 onwards has more password plugin stuff here, ignore for now
  ascore_packet_read_end(con);

  // Create response packet
  ascore_handshake_response(con);
}

void ascore_handshake_response(ascon_st *con)
{
  unsigned char *buffer_ptr;
  uint32_t capabilities;
  asret_t ret;

  buffer_ptr= (unsigned char*)con->write_buffer;

  capabilities= con->server_capabilities & ASCORE_CAPABILITY_CLIENT;
  if (con->options.found_rows)
  {
    capabilities|= ASCORE_CAPABILITY_FOUND_ROWS;
  }
  if (con->options.interactive)
  {
    capabilities|= ASCORE_CAPABILITY_INTERACTIVE;
  }
  if (con->options.multi_statements)
  {
    capabilities|= ASCORE_CAPABILITY_MULTI_STATEMENTS;
  }
  if (con->options.auth_plugin)
  {
    capabilities|= ASCORE_CAPABILITY_PLUGIN_AUTH;
  }

  ascore_pack_int4(buffer_ptr, capabilities);
  buffer_ptr+= 4;

  // Set max packet size to our buffer size for now
  ascore_pack_int4(buffer_ptr, ASCORE_DEFAULT_BUFFER_SIZE);
  buffer_ptr+= 4;

  // Change this when we support charsets
  buffer_ptr[0]= 0;
  buffer_ptr++;

  // 0x00 padding for 23 bytes
  memset(buffer_ptr, 0, 23);
  buffer_ptr+= 23;

  // User name
  memcpy(buffer_ptr, con->user, strlen(con->user));
  buffer_ptr+= strlen(con->user);
  buffer_ptr[0]= '\0';
  buffer_ptr++;

  // Password
  // TODO: add support for password plugins
  if (con->pass[0] != '\0')
  {
    buffer_ptr[0]= SHA1_DIGEST_LENGTH; // probably should use char packing?
    buffer_ptr++;
    ret= scramble_password(con, (unsigned char*)buffer_ptr);
    if (ret != ASRET_OK)
    {
      asdebug("Scramble problem!");
      con->local_errcode= ASRET_BAD_SCRAMBLE;
      return;
    }
    buffer_ptr+= SHA1_DIGEST_LENGTH;
  }
  else
  {
    buffer_ptr[0]= '\0';
    buffer_ptr++;
  }

  if (con->schema != NULL)
  {
    memcpy(buffer_ptr, con->schema, strlen(con->schema));
    buffer_ptr+= strlen(con->schema);
  }
  buffer_ptr[0]= '\0';
  buffer_ptr++;
  ascore_send_data(con, con->write_buffer, (size_t)(buffer_ptr - (unsigned char*)con->write_buffer));
  con->next_packet_type= ASCORE_PACKET_TYPE_RESPONSE;

  uv_read_start(con->uv_objects.stream, on_alloc, ascore_read_data_cb);
}

asret_t scramble_password(ascon_st *con, unsigned char *buffer)
{
  SHA1_CTX ctx;
  unsigned char stage1[SHA1_DIGEST_LENGTH];
  unsigned char stage2[SHA1_DIGEST_LENGTH];
  uint8_t it;

  if (con->scramble_buffer == NULL)
  {
    asdebug("No scramble supplied from server");
    return ASRET_NO_SCRAMBLE;
  }

  // Double hash the password
  SHA1Init(&ctx);
  SHA1Update(&ctx, (unsigned char*)con->pass, strlen(con->pass));
  SHA1Final(stage1, &ctx);
  SHA1Init(&ctx);
  SHA1Update(&ctx, stage1, SHA1_DIGEST_LENGTH);
  SHA1Final(stage2, &ctx);

  // Hash the scramble with the double hash
  SHA1Init(&ctx);
  SHA1Update(&ctx, con->scramble_buffer, SHA1_DIGEST_LENGTH);
  SHA1Update(&ctx, stage2, SHA1_DIGEST_LENGTH);
  SHA1Final(buffer, &ctx);

  // XOR the hash with the stage1 hash
  for (it= 0; it < SHA1_DIGEST_LENGTH; it++)
  {
    buffer[it]= buffer[it] ^ stage1[it];
  }

  return ASRET_OK;
}

