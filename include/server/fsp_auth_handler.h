#ifndef FSP_AUTH_HANDLER_H
#define FSP_AUTH_HANDLER_H

/*
 * ============================================================================
 * FSP Auth & Permissions Event Handlers (C11 - SDD §5.4)
 * ============================================================================
 * Đăng ký các handler sự kiện Auth và Perm vào fsp_dispatcher.
 */

#include "server/fsp_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Đăng ký toàn bộ các handler AUTH và PERM vào dispatcher */
void fsp_auth_register_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* FSP_AUTH_HANDLER_H */
