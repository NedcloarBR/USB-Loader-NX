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
 * @file usbl_es_shim.h
 * @brief es (IETicketService) commands not wrapped by libnx.
 * @copyright libnx Authors
 */
#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IETicketService::ImportTicket (1). */
Result esImportTicket(Service *s, const void *ticket, size_t ticket_size, const void *cert, size_t cert_size);

/* IETicketService::ListCommonTicketRightsIds (11) and ListPersonalizedTicketRightsIds (12); rights ids are 0x10 bytes. */
Result esListCommonTicketRightsIds(Service *s, s32 *out_count, void *out_rights_ids, size_t size);
Result esListPersonalizedTicketRightsIds(Service *s, s32 *out_count, void *out_rights_ids, size_t size);

#ifdef __cplusplus
}
#endif
