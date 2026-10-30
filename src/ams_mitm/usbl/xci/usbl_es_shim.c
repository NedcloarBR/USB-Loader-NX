/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * @file usbl_es_shim.c
 * @brief es (IETicketService) commands not wrapped by libnx.
 * @copyright libnx Authors
 */
#include <switch.h>
#include "usbl_es_shim.h"

Result esImportTicket(Service *s, const void *ticket, size_t ticket_size, const void *cert, size_t cert_size) {
    return serviceDispatch(s, 1,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { ticket, ticket_size }, { cert, cert_size } },
    );
}

static Result esListRightsIds(Service *s, u32 cmd_id, s32 *out_count, void *out_rights_ids, size_t size) {
    return serviceDispatchOut(s, cmd_id, *out_count,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { out_rights_ids, size } },
    );
}

Result esListCommonTicketRightsIds(Service *s, s32 *out_count, void *out_rights_ids, size_t size) {
    return esListRightsIds(s, 11, out_count, out_rights_ids, size);
}

Result esListPersonalizedTicketRightsIds(Service *s, s32 *out_count, void *out_rights_ids, size_t size) {
    return esListRightsIds(s, 12, out_count, out_rights_ids, size);
}
