#ifndef _IOTAPI_DEV_HANDLER_H__
#define _IOTAPI_DEV_HANDLER_H__

#include <tinyara/iotdev.h>
#include <tinyara/iotbus_sig.h>

typedef struct _iotapi_dev_ctx_s *iotapi_hnd;

typedef void (*iotapi_cbk)(iotbus_int_type_e evt, void *arg);

int iotapi_dev_init(iotapi_hnd *hnd);
int iotapi_dev_deinit(iotapi_hnd hnd);
int iotapi_dev_register(iotapi_hnd hnd, iotbus_int_type_e evt, iotapi_cbk cbk, void *arg);
int iotapi_dev_unregister(iotapi_hnd hnd);
iotbus_int_type_e iotapi_dev_get_int_type(iotapi_hnd hnd);

#endif // _IOTAPI_DEV_HANDLER_H__
