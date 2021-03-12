#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlmio.h>

//#include <js_native_api.h>
#include <node_api.h>
#include <uv.h>


static uv_poll_t handle;


static void uv_callback(uv_poll_t* handle, int status, int events)
{
  wlmio_tick();
}


static napi_value cleanup(napi_env env, napi_callback_info info)
{
  uv_poll_stop(&handle);
  return NULL;
}


static void handle_exception(napi_env env)
{
  bool r;
  napi_status s = napi_is_exception_pending(env, &r);
  if(s != napi_ok || !r)
  { return; }

  napi_value ex;
  s = napi_get_and_clear_last_exception(env, &ex);
  if(s != napi_ok)
  { return; }

  s = napi_fatal_exception(env, ex);
}


static napi_env status_callback_env = NULL;
static napi_ref status_callback_ref = NULL;


static napi_value set_status_callback(napi_env env, napi_callback_info info)
{
  size_t argc = 1;
  napi_value argv;
  napi_status status = napi_get_cb_info(env, info, &argc, &argv, NULL, NULL);
  if(status != napi_ok)
  { return NULL; }

  napi_valuetype type;
  status = napi_typeof(env, argv, &type);
  if(status != napi_ok || type != napi_function)
  { return NULL; }

  if(status_callback_ref != NULL)
  {
    napi_delete_reference(env, status_callback_ref);
    status_callback_ref = NULL;
  }

  status_callback_env = env;
  napi_create_reference(env, argv, 1, &status_callback_ref);

  return NULL;
}


static napi_status status_to_object(napi_env env, const struct wlmio_status* const status, napi_value* const object)
{
  napi_status s = napi_create_object(env, object);
  if(s != napi_ok)
  { goto exit; }

  napi_value uptime;
  s = napi_create_uint32(env, status->uptime, &uptime);
  if(s != napi_ok)
  { goto exit; }

  s = napi_set_named_property(env, *object, "uptime", uptime);
  if(s != napi_ok)
  { goto exit; }

  napi_value health;
  s = napi_create_uint32(env, status->health, &health);
  if(s != napi_ok)
  { goto exit; }

  s = napi_set_named_property(env, *object, "health", health);
  if(s != napi_ok)
  { goto exit; }

  napi_value mode;
  s = napi_create_uint32(env, status->mode, &mode);
  if(s != napi_ok)
  { goto exit; }

  s = napi_set_named_property(env, *object, "mode", mode);
  if(s != napi_ok)
  { goto exit; }

  napi_value vendor_status;
  s = napi_create_uint32(env, status->vendor_status, &vendor_status);
  if(s != napi_ok)
  { goto exit; }

  s = napi_set_named_property(env, *object, "vendorStatus", vendor_status);
  if(s != napi_ok)
  { goto exit; }

exit:
  return s;
}


static void status_callback(const uint8_t node_id, const struct wlmio_status* const old_status, const struct wlmio_status* const new_status)
{
  if(status_callback_ref == NULL)
  { return; }

  napi_env env = status_callback_env;

  napi_handle_scope scope;
  napi_status status = napi_open_handle_scope(env, &scope);

  napi_value callback;
  status = napi_get_reference_value(env, status_callback_ref, &callback);
  if(status != napi_ok)
  { goto exit; }

  napi_value global;
  status = napi_get_global(env, &global);
  if(status != napi_ok)
  { goto exit; }

  napi_value argv[3];

  status = napi_create_uint32(env, node_id, &argv[0]);
  if(status != napi_ok)
  { goto exit; }

  status = status_to_object(env, old_status, &argv[1]);
  if(status != napi_ok)
  { goto exit; }

  status = status_to_object(env, new_status, &argv[2]);
  if(status != napi_ok)
  { goto exit; }

  status = napi_call_function(env, global, callback, 3, argv, NULL);
  if(status != napi_ok)
  { goto exit; }

  handle_exception(env);

exit:
  napi_close_handle_scope(env, scope);
  return;
}


static napi_value get_node_id(napi_env env, napi_callback_info info)
{
  napi_value node_id;
  napi_status s = napi_create_uint32(env, wlmio_get_node_id(), &node_id);
  if(s != napi_ok)
  { return NULL; }

  return node_id;
}


struct register_list_param
{
  napi_env env;
  napi_ref callback_ref;
  char name[51];
};


static void register_list_callback(const int32_t r, void* const p)
{
  struct register_list_param* const param = p;

  napi_env env = param->env;

  napi_handle_scope scope;
  napi_status s = napi_open_handle_scope(env, &scope);

  napi_value callback;
  s = napi_get_reference_value(env, param->callback_ref, &callback);
  if(s != napi_ok)
  { goto exit; }

  napi_value global;
  s = napi_get_global(env, &global);
  if(s != napi_ok)
  { goto exit; }

  napi_value argv[2];

  s = napi_create_int32(env, r, &argv[0]);
  if(s != napi_ok)
  { goto exit; }

  s = napi_create_string_utf8(env, param->name, NAPI_AUTO_LENGTH, &argv[1]);
  if(s != napi_ok)
  { goto exit; }

  s = napi_call_function(env, global, callback, sizeof(argv) / sizeof(napi_value), argv, NULL);
  if(s != napi_ok)
  { goto exit; }

  handle_exception(env);

exit:
  napi_delete_reference(env, param->callback_ref);
  napi_close_handle_scope(env, scope);
  free(p);
}


static napi_value register_list(napi_env env, napi_callback_info info)
{
  size_t argc = 3;
  napi_value argv[3];
  napi_status s = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if(s != napi_ok || argc != 3)
  { goto exit; }

  napi_valuetype type;

  s = napi_typeof(env, argv[0], &type);
  if(s != napi_ok || type != napi_number)
  { goto exit; }

  s = napi_typeof(env, argv[1], &type);
  if(s != napi_ok || type != napi_number)
  { goto exit; }

  s = napi_typeof(env, argv[2], &type);
  if(s != napi_ok || type != napi_function)
  { goto exit; }

  uint32_t node_id;
  s = napi_get_value_uint32(env, argv[0], &node_id);
  if(s != napi_ok || node_id > 127)
  { goto exit; }

  uint32_t index;
  s = napi_get_value_uint32(env, argv[1], &index);
  if(s != napi_ok)
  { goto exit; }

  struct register_list_param* const param = malloc(sizeof(struct register_list_param));
  param->env = env;
  napi_create_reference(env, argv[2], 1, &param->callback_ref);
  int32_t r = wlmio_register_list(node_id, index, param->name, register_list_callback, param);
  if(r < 0)
  {
    napi_delete_reference(env, param->callback_ref);
    free(param);
    goto exit;
  }

exit:
  return NULL;
}


struct register_access_param
{
  napi_env env;
  napi_ref callback_ref;
  struct wlmio_register_access regr;
};


static void register_access_callback(const int32_t r, void* const p)
{
  struct register_access_param* const param = p;

  napi_env env = param->env;

  napi_handle_scope scope;
  napi_status s = napi_open_handle_scope(env, &scope);

  napi_value callback;
  s = napi_get_reference_value(env, param->callback_ref, &callback);
  if(s != napi_ok)
  { goto exit; }

  napi_value global;
  s = napi_get_global(env, &global);
  if(s != napi_ok)
  { goto exit; }

  napi_value argv[2];

  s = napi_create_int32(env, r, &argv[0]);
  if(s != napi_ok)
  { goto exit; }

  void* data;
  s = napi_create_arraybuffer(env, sizeof(struct wlmio_register_access), &data, &argv[1]);
  if(s != napi_ok)
  { goto exit; }
  memcpy(data, &param->regr, sizeof(struct wlmio_register_access));

  s = napi_call_function(env, global, callback, sizeof(argv) / sizeof(napi_value), argv, NULL);
  if(s != napi_ok)
  {
    handle_exception(env);
    goto exit;
  }

exit:
  napi_delete_reference(env, param->callback_ref);
  napi_close_handle_scope(env, scope);
  free(p);
}


static napi_value register_access(napi_env env, napi_callback_info info)
{
  int32_t r = 0;

  size_t argc = 4;
  napi_value argv[4];
  napi_status s = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if(s != napi_ok || argc != 4)
  { 
    r = -EINVAL;
    goto exit;
  }

  napi_valuetype type;

  s = napi_typeof(env, argv[0], &type);
  if(s != napi_ok || type != napi_number)
  {
    r = -EINVAL;
    goto exit;
  }

  s = napi_typeof(env, argv[1], &type);
  if(s != napi_ok || type != napi_string)
  { 
    r = -EINVAL;
    goto exit;
  }

  bool is_arraybuffer;
  s = napi_is_arraybuffer(env, argv[2], &is_arraybuffer);
  if(s != napi_ok || !is_arraybuffer)
  {
    r = -EINVAL;
    goto exit; 
  }

  s = napi_typeof(env, argv[3], &type);
  if(s != napi_ok || type != napi_function)
  { 
    r = -EINVAL;
    goto exit;
  }

  uint32_t node_id;
  s = napi_get_value_uint32(env, argv[0], &node_id);
  if(s != napi_ok || node_id > 127)
  { 
    r = -EINVAL;
    goto exit;
  }

  char name[51];
  s = napi_get_value_string_utf8(env, argv[1], name, sizeof(name), NULL);
  if(s != napi_ok)
  {
    r = -EINVAL;
    goto exit;
  }

  size_t length;
  void* data;
  s = napi_get_arraybuffer_info(env, argv[2], &data, &length);
  if(s != napi_ok || length < sizeof(struct wlmio_register_access))
  {
    r = -EINVAL;
    goto exit;
  }

  struct register_access_param* const param = malloc(sizeof(struct register_access_param));
  param->env = env;
  napi_create_reference(env, argv[3], 1, &param->callback_ref);
  r = wlmio_register_access(node_id, name, data, &param->regr, register_access_callback, param);
  if(r < 0)
  {
    napi_delete_reference(env, param->callback_ref);
    free(param);
    goto exit;
  }

  napi_value result;
exit:
  napi_create_int32(env, r, &result);
  return result;
}


struct get_info_param 
{
  napi_env env;
  napi_ref callback_ref;
  struct wlmio_node_info info;
};


static void get_info_callback(const int32_t r, void* const p)
{
  struct get_info_param* const param = p;

  napi_env env = param->env;

  napi_handle_scope scope;
  napi_status s = napi_open_handle_scope(env, &scope);

  napi_value callback;
  s = napi_get_reference_value(env, param->callback_ref, &callback);
  if(s != napi_ok)
  { goto exit; }

  napi_value global;
  s = napi_get_global(env, &global);
  if(s != napi_ok)
  { goto exit; }

  napi_value argv[2];

  s = napi_create_int32(env, r, &argv[0]);
  if(s != napi_ok)
  { goto exit; }

  void* data;
  s = napi_create_arraybuffer(env, sizeof(struct wlmio_node_info), &data, &argv[1]);
  if(s != napi_ok)
  { goto exit; }
  memcpy(data, &param->info, sizeof(struct wlmio_node_info));

  s = napi_call_function(env, global, callback, sizeof(argv) / sizeof(napi_value), argv, NULL);
  if(s != napi_ok)
  {
    handle_exception(env);
    goto exit;
  }

exit:
  napi_delete_reference(env, param->callback_ref);
  napi_close_handle_scope(env, scope);
  free(p);
}


static napi_value get_info(napi_env env, napi_callback_info info)
{
  size_t argc = 2;
  napi_value argv[2];
  napi_status s = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if(s != napi_ok || argc != 2)
  { goto exit; }

  napi_valuetype type;

  s = napi_typeof(env, argv[0], &type);
  if(s != napi_ok || type != napi_number)
  { goto exit; }

  s = napi_typeof(env, argv[1], &type);
  if(s != napi_ok || type != napi_function)
  { goto exit; }

  uint32_t node_id;
  s = napi_get_value_uint32(env, argv[0], &node_id);
  if(s != napi_ok || node_id > 127)
  { goto exit; }

  struct get_info_param* const param = malloc(sizeof(struct get_info_param));
  param->env = env;
  napi_create_reference(env, argv[1], 1, &param->callback_ref);
  int32_t r = wlmio_get_node_info(node_id, &param->info, get_info_callback, param);
  if(r < 0)
  {
    napi_delete_reference(env, param->callback_ref);
    free(param);
    goto exit;
  }

exit:
  return NULL;
}


napi_property_descriptor desc[] =
{
  (napi_property_descriptor){"shutdown", NULL, cleanup, NULL, NULL, NULL, napi_enumerable, NULL},
  (napi_property_descriptor){"setStatusCallback", NULL, set_status_callback, NULL, NULL, NULL, napi_enumerable, NULL},
  (napi_property_descriptor){"nodeId", NULL, NULL, get_node_id, NULL, NULL, napi_enumerable, NULL},
  (napi_property_descriptor){"registerList", NULL, register_list, NULL, NULL, NULL, napi_enumerable, NULL},
  (napi_property_descriptor){"registerAccess", NULL, register_access, NULL, NULL, NULL, napi_enumerable, NULL},
  (napi_property_descriptor){"getInfo", NULL, get_info, NULL, NULL, NULL, napi_enumerable, NULL}
};


napi_value init(napi_env env, napi_value exports)
{
  napi_status status;
  status = napi_define_properties(env, exports, sizeof(desc) / sizeof(napi_property_descriptor), desc);
  if(status != napi_ok)
  { return NULL; }

  uv_loop_t* loop;
  status = napi_get_uv_event_loop(env, &loop);
  if(status != napi_ok)
  { return NULL; }

  int32_t r = wlmio_init();
  wlmio_set_status_callback(status_callback);
  uv_poll_init(loop, &handle, wlmio_get_epoll_fd());
  uv_poll_start(&handle, UV_READABLE, uv_callback);

  return exports;
}


NAPI_MODULE(NODE_GYP_MODULE_NAME, init)

