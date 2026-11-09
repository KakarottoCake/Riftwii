/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtrestart.h"

#define RTRESTART_SDIO_SENDCMD 7u
#define RTRESTART_SDHC_WRITE 3u
#define RTRESTART_CMD_WRITEMULTIBLOCK 0x19u
#define RTRESTART_CMDTYPE_AC 3u
#define RTRESTART_RESPONSE_R1 1u

void rtrestart_prepare_write(struct rtrestart_block* blk, uint32_t sd_mode, uint32_t lba, const uint8_t* sector,
                             struct rtrestart_call* call) {
    struct rtrestart_request* rq = &blk->request;
    if (sd_mode == RTRESTART_SD_D2X) {
        /* d2x's sector write: the sector, a 1-sector count, then the data. */
        rq->cmd = lba;
        rq->cmd_type = 1u;
        blk->vec[0].data = (uint32_t)(uintptr_t)&rq->cmd;
        blk->vec[0].len = 4;
        blk->vec[1].data = (uint32_t)(uintptr_t)&rq->cmd_type;
        blk->vec[1].len = 4;
        blk->vec[2].data = (uint32_t)(uintptr_t)sector;
        blk->vec[2].len = RTRESTART_SECTOR;
        call->ioctl = RTRESTART_SDHC_WRITE;
        call->in_count = 3u;
        call->out_count = 0u;
        return;
    }
    {
        uint32_t i;
        /* A CMD25 of one block, as the savegame engine writes one. */
        rq->cmd = RTRESTART_CMD_WRITEMULTIBLOCK;
        rq->cmd_type = RTRESTART_CMDTYPE_AC;
        rq->rsp_type = RTRESTART_RESPONSE_R1;
        rq->arg = sd_mode == RTRESTART_SD_SDHC ? lba : lba * RTRESTART_SECTOR;
        rq->blk_cnt = 1u;
        rq->blk_size = RTRESTART_SECTOR;
        rq->dma_addr = (uint32_t)(uintptr_t)sector;
        rq->isdma = 1u;
        rq->pad0 = 0;
        for (i = 0; i < 4u; ++i) blk->response[i] = 0;
        blk->vec[0].data = (uint32_t)(uintptr_t)rq;
        blk->vec[0].len = sizeof(*rq);
        blk->vec[1].data = (uint32_t)(uintptr_t)sector;
        blk->vec[1].len = RTRESTART_SECTOR;
        blk->vec[2].data = (uint32_t)(uintptr_t)blk->response;
        blk->vec[2].len = 16;
        call->ioctl = RTRESTART_SDIO_SENDCMD;
        call->in_count = 2u;
        call->out_count = 1u;
    }
}
