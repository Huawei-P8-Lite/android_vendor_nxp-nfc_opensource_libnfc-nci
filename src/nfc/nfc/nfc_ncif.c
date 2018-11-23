/******************************************************************************
 *
 *  Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright (C) 2015 NXP Semiconductors
 *  The original Work has been changed by NXP Semiconductors.
 *
 *  Copyright (C) 1999-2014 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/


/******************************************************************************
 *
 *  This file contains functions that interface with the NFC NCI transport.
 *  On the receive side, it routes events to the appropriate handler
 *  (callback). On the transmit side, it manages the command transmission.
 *
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include "nfc_target.h"

#if NFC_INCLUDED == TRUE
#include "nfc_hal_api.h"
#include "nfc_api.h"
#include "nci_defs.h"
#include "nci_hmsgs.h"
#include "nfc_int.h"
#include "rw_api.h"
#include "rw_int.h"
#include "hcidefs.h"
#include "nfc_hal_api.h"

#if(NXP_EXTNS == TRUE)
#include "nfa_ce_int.h"
#include "nfa_sys.h"
#include "nfa_dm_int.h"
#include "nfa_hci_int.h"
#include <config.h>
#endif
tNFC_CONN_CB *p_cb_stored = NULL;
#if (NFC_RW_ONLY == FALSE)
static const UINT8 nfc_mpl_code_to_size[] =
{64, 128, 192, 254};

#endif /* NFC_RW_ONLY */

#if(NXP_EXTNS == TRUE)
#define NFC_NCI_WAIT_DATA_NTF_TOUT      2
#define NFC_NCI_RFFIELD_EVT_TIMEOUT     2
UINT8 temp_buff[660];
#endif

#define NFC_PB_ATTRIB_REQ_FIXED_BYTES   1
#define NFC_LB_ATTRIB_REQ_FIXED_BYTES   8

#if(NXP_EXTNS == TRUE)
// Global Structure varibale for FW Version
static tNFC_FW_VERSION nfc_fw_version;
static UINT8 gScreenState = 0x0; // SCREEN ON UNLOCKED
#if((NFC_NXP_CHIP_TYPE != PN547C2) && (NFC_NXP_AID_MAX_SIZE_DYN == TRUE))
static UINT16 maxRoutingTableSize;
#endif
uint8_t sListenActivated;
extern tNFA_CE_CB nfa_ce_cb;
BOOLEAN core_reset_init_num_buff = FALSE;
UINT8 nfcc_dh_conn_id = 0xFF;
extern void nfa_hci_rsp_timeout (tNFA_HCI_EVENT_DATA *p_evt_data);
#endif

#if((NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
void disc_deact_ntf_timeout_handler(tNFC_RESPONSE_EVT event);
extern BOOLEAN etsi_reader_in_progress;
#endif
#if((NXP_EXTNS == TRUE) && (NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE))
void uicc_eeprom_get_config(UINT8* config_resp);
void uicc_eeprom_set_config(UINT8* config_resp);
#endif

/*******************************************************************************
**
** Function         nfc_ncif_update_window
**
** Description      Update tx cmd window to indicate that NFCC can received
**
** Returns          void
**
*********************************************************************************/
void nfc_ncif_update_window (void)
{
    /* Sanity check - see if we were expecting a update_window */
    if (nfc_cb.nci_cmd_window == NCI_MAX_CMD_WINDOW)
    {
        if (nfc_cb.nfc_state != NFC_STATE_W4_HAL_CLOSE)
        {
            NFC_TRACE_ERROR0("nfc_ncif_update_window: Unexpected call");
        }
        return;
    }

    /* Stop command-pending timer */
    nfc_stop_timer (&nfc_cb.nci_wait_rsp_timer);

    nfc_cb.p_vsc_cback = NULL;
    nfc_cb.nci_cmd_window++;

    /* Check if there were any commands waiting to be sent */
    nfc_ncif_check_cmd_queue (NULL);
}
/*******************************************************************************
**
** Function         nfc_ncif_update_data_queue
**
** Description      Update tx cmd window  to indicate that NFCC can received and core credit ntf received
**
** Returns          void
**
*********************************************************************************/
void nfc_ncif_update_data_queue (void)
{
    nfc_cb.nci_cmd_window++;
    NFC_TRACE_ERROR0 ("nfc_ncif_update_data_queue- incrementing window");
    /* Check if there were any commands waiting to be sent */
    nfc_ncif_check_cmd_queue(NULL);
}
/*******************************************************************************
**
** Function         nfc_ncif_cmd_timeout
**
** Description      Handle a command timeout
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_cmd_timeout (void)
{
    NFC_TRACE_DEBUG0 ("nfc_ncif_cmd_timeout(): enter");

#if(NXP_EXTNS == TRUE)
    NFC_TRACE_DEBUG0 ("Start Middleware Recovery Procedure");
    {
        // TO DO:
        // Sending command which are core_reset,init,discovery as raw packet (Hard coding)
        // and we have to match the RF state between before entering recovery and after recovery.
        // finally, executing the last command which is before entering recovery, and invoking the callback.

        //step 1. Sending core reset/init command
        //step 2. deciding to send command whether discovery or not.
        //step 3. invoking the callback function which is registered at last time before entering recovery.

        /*
        consist of buffer(p_core_init_rsp_params)
        buffer[0] = Indicators 0:Normal mode 1:Recovery mode 2:core reset was last command 3:core init was last command
        buffer[1] = Last NFCC NCI RF State, 0:IDLE 1:DISCOVERY
        buffer[2] = Length of discovery command to be sent as a part of recovery.
        buffer[3~34] = Last Discovery command(including phase data).(32 byte)
        buffer[35] = Length of last command.
        buffer[36~254] = Last Command /or Last Command including Data.
        buffer[255] = Last screen state
        */
        UINT8 *buffer           = NULL;
        tNFC_CONN_CB *p_cb      = NULL;
        UINT16 bufflen          = 0x100;
        UINT16 fw_dwnld_status  = NFC_STATUS_FAILED;
        tNFC_STATUS status      = NFC_STATUS_FAILED;
        tNFC_FWUpdate_Info_t fw_update_inf;

        /*Stop the command timeout timer*/
        nfc_stop_timer (&nfc_cb.nci_wait_rsp_timer);

        buffer = (UINT8 *)malloc(bufflen*sizeof(UINT8));

        if(NULL == buffer)
        {
            NFC_TRACE_ERROR0 ("Recovery MEM Allocation is failed!!");
            return;
        }
        memset(buffer, 0x00, (bufflen*sizeof(UINT8)));

        /*We need to send Core Reset and Core Init as a part of recovery*/
        buffer[0] = 0x01;

        /*Set the last screen state*/
        buffer[255] = gScreenState;

        NFC_TRACE_DEBUG1 ("MW last RF discovery flags 0x%x", nfa_dm_cb.disc_cb.disc_flags);
        NFC_TRACE_DEBUG1 ("MW last RF discovery state 0x%x", nfa_dm_cb.disc_cb.disc_state);

        if (nfc_cb.nfc_state == NFC_STATE_CORE_INIT)
        {
#if((NXP_EXTNS == TRUE) && (NXP_NFCC_MW_RCVRY_BLK_FW_DNLD == TRUE))
            NFC_TRACE_DEBUG0 ("MW recovery should abort FW download checking at time of cmd_timeout");
#else
            NFC_TRACE_DEBUG0 ("Force FW Download !");
            nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_CHECK_FLASH_REQ, &fw_update_inf);
            nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_FW_DWNLD, &fw_dwnld_status);
            NFC_TRACE_DEBUG1 ("FW Download 0x%x", fw_dwnld_status);
            if (fw_dwnld_status !=  NFC_STATUS_OK)
                nfc_enabled (NFC_STATUS_FAILED, NULL);
#endif
        }
        /*Prepare the DISCOVERY command*/
        if(!(nfa_dm_cb.disc_cb.disc_state > NFA_DM_RFST_IDLE))
        {
            NFC_TRACE_DEBUG0 ("NFCC last NCI RF state is IDLE");
            /*NCI stack RF state set to IDLE*/
            buffer[1] = 0x00;
        }
        else
        {
            UINT8 *ps = (UINT8 *)nfc_cb.p_last_disc;
            NFC_TRACE_DEBUG0 ("NFCC last NCI RF state is DISCOVERY");
            /*Last NFCC state set to DISCOVERY*/
            buffer[1] = 0x01;
            /*NCI stack RF state set to DISCOVERY*/
            nfa_dm_cb.disc_cb.disc_state = 0x01;
            /*Listen Mode flags initialized to 0x00*/
            nfa_ce_cb.flags = 0x00;

            if(ps[0])
            {
                /*Set the length of last DISCOVERY packet including header*/
                buffer[2] = 2 + 1 + 1 + (ps[0] * 2); /*HDR(2) + Length(1) + Configs(1) + Data(TechnMode(2) * 2)*/
                /*Copy the DISCOVERY command header*/
                memcpy((UINT8 *)&buffer[3], "\x21\x03", 2);
                /*Set the length of content of the last DISCOVERY command*/
                buffer[5] = 1 + (ps[0] * 2); /*Configs(1) + Data(TechnMode(2) * 2)*/
                /*Copy the content of DISCOVERY command*/
                memcpy((UINT8 *)&buffer[6], ps, buffer[5]);
            }
            else
            {
                buffer[2] = 0x1A; /*Length of hard coded recovery discovery command*/
                memcpy((UINT8 *)&buffer[3],
                    "\x21\x03\x17\x0B\x00\x01\x01\x01\x02\x01\x03\x01\x80\x01\x81\x01\x82\x01\x83\x01\x85\x01\x06\x01\x77\x01", 24);
            }
        }
        /*Prepare the last command sent just before CORE_RESET_NTF*/
        if(( (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x06) &&
             (nfc_cb.last_cmd_buf[0] == 0x01 && nfc_cb.last_cmd_buf[1] == 0x01) )   || /*DEACTIVATE TO SLEEP*/
           (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x04)               || /**/
           (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x08)               || /**/
           ( (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x06) &&             /*DEACTIVATE TO DISCOVERY*/
             (nfc_cb.last_cmd_buf[0] == 0x01 && nfc_cb.last_cmd_buf[1] == 0x03) )
          )
        {
            buffer[35] = 0x00;  /*No need to send last command*/
        }
        else if( (nfc_cb.last_hdr[0] == 0x20 && (nfc_cb.last_hdr[1] == 0x02 || nfc_cb.last_hdr[1] == 0x03)) || /*SET CONFIG COMMAND*/
                 (nfc_cb.last_hdr[0] == 0x2F && nfc_cb.last_hdr[1] == 0x15)                                 || /*PROPRIETARY COMMAND*/
                 (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x01)                                 || /*LISTEN ROUTING COMMAND*/
                 ( (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x06) &&
                   (nfc_cb.last_cmd_buf[0] == 0x01 && nfc_cb.last_cmd_buf[1] == 0x00) )                        /*DEACTIVATE TO IDLE*/
               )
        {
            /*Set the length of last command*/
            buffer[35] = nfc_cb.cmd_size + 3;  /*HDR(2) + Length(1) + Cmd data(n)*/
            /*Copy the last HEADER*/
            memcpy(&buffer[36], nfc_cb.last_hdr, NFC_SAVED_HDR_SIZE);
            /*Copy the last command*/
            memcpy(&buffer[38], nfc_cb.last_cmd_buf, nfc_cb.cmd_size + 1);
        }
        else /*either CORE_RESET or CORE_INIT was the last command*/
        {
            /*full length of command*/
            buffer[35] = 2 + 1 + nfc_cb.cmd_size;  /*HDR(2) + Length(1) + Command data size(n)*/

            memcpy(&buffer[36], nfc_cb.last_hdr, NFC_SAVED_HDR_SIZE);

            buffer[38] = nfc_cb.cmd_size;   /*Length of last command*/

            if(nfc_cb.cmd_size > 0)         /*last command was CORE_RESET*/
                memcpy(&buffer[39], nfc_cb.last_cmd, NFC_SAVED_CMD_SIZE);
            else
                buffer[35] = 0x00;          /*last command was CORE_INIT*/

            if(nfc_cb.last_hdr[0] == 0x20 && nfc_cb.last_hdr[1] == 0x00)
                buffer[0] = 2;              /*indicate last command was CORE_RESET*/
            else if (nfc_cb.last_hdr[0] == 0x20 && nfc_cb.last_hdr[1] == 0x01)
                buffer[0] = 3;              /*indicate last command was CORE_INIT*/
        }
        /* if HCI state is in wait for response then stop the timer and send the response timeout event to the upper layer*/
        if(nfa_hci_cb.hci_state == NFA_HCI_STATE_WAIT_RSP)
        {
            BT_HDR *p_msg;
            nfa_sys_stop_timer(&nfa_hci_cb.timer);
            if ((p_msg = (BT_HDR *) GKI_getbuf (sizeof (BT_HDR))) != NULL)
            {
                NFC_TRACE_DEBUG0("nfc_ncif_cmd_timeout(): Sending HCI response timeout to upper layer");
                p_msg->event = NFA_HCI_RSP_TIMEOUT_EVT;
                p_msg->layer_specific = 0;
                nfa_sys_sendmsg (p_msg);
            }
        }
        if(0xFF != nfcc_dh_conn_id)
        {
            /* Flush the data if any before proceeding further with the recovery */
            p_cb = nfc_find_conn_cb_by_conn_id(nfcc_dh_conn_id);
            if(NULL != p_cb)
            {
                NFC_TRACE_DEBUG1("nfc_ncif_cmd_timeout(): connection id %d", nfcc_dh_conn_id);
                status = NFC_FlushData(p_cb->conn_id);
                if(status != NFC_STATUS_OK)
                {
                    NFC_TRACE_ERROR0 ("nfc_ncif_cmd_timeout(): NFC data flush failed");
                }
            }
        }
        NFC_TRACE_DEBUG3 ("nfc_ncif_cmd_timeout(): Indicator:0x%X disc cmd len:0x%X last cmd len:0x%X", buffer[0], buffer[2], buffer[35]);
        nfc_cb.p_hal->core_initialized (buffer);
        if(buffer != NULL) free(buffer);
        NFC_TRACE_DEBUG0 ("nfc_ncif_cmd_timeout(): exit");
    }
#else
    /* report an error */
    nfc_ncif_event_status(NFC_GEN_ERROR_REVT, NFC_STATUS_HW_TIMEOUT);
    nfc_ncif_event_status(NFC_NFCC_TIMEOUT_REVT, NFC_STATUS_HW_TIMEOUT);

    /* if enabling NFC, notify upper layer of failure */
    if (nfc_cb.nfc_state == NFC_STATE_CORE_INIT)
    {
        nfc_enabled (NFC_STATUS_FAILED, NULL);
    }
    /* XXX maco since this failure is unrecoverable, abort the process */
    abort();
#endif
}

/*******************************************************************************
**
** Function         nfc_wait_2_deactivate_timeout
**
** Description      Handle a command timeout
**
** Returns          void
**
*******************************************************************************/
void nfc_wait_2_deactivate_timeout (void)
{
    NFC_TRACE_ERROR0 ("nfc_wait_2_deactivate_timeout");
    nfc_cb.flags  &= ~NFC_FL_DEACTIVATING;
    nci_snd_deactivate_cmd ((UINT8) ((TIMER_PARAM_TYPE) nfc_cb.deactivate_timer.param));
}
#if (NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function         nfc_ncif_retransmit_data
**
** Description      Retransmission of lost packet
**
** Returns          void
**
*******************************************************************************/
UINT8 nfc_ncif_retransmit_data (tNFC_CONN_CB *p_cb, BT_HDR *p_data)
{
    NFC_TRACE_ERROR0 ("nfc_ncif_retransmit_data");
    if(p_data == NULL)
    {
        NFC_TRACE_ERROR0 ("nfc_ncif_retransmit_data: p_data is NULL");
        return NCI_STATUS_FAILED;
    }
    if (p_cb->num_buff != NFC_CONN_NO_FC)
        p_cb->num_buff--;

    HAL_RE_WRITE(p_data);
    if(p_cb->conn_id == NFC_NFCEE_CONN_ID)
    {
        //Start waiting for credit ntf
        nfc_cb.bIsCreditNtfRcvd = FALSE;
        nfc_start_timer (&nfc_cb.nci_wait_data_ntf_timer, (UINT16)(NFC_TTYPE_NCI_WAIT_DATA_NTF), NFC_NCI_WAIT_DATA_NTF_TOUT);
    }
    return NCI_STATUS_OK;
}
#endif
/*******************************************************************************
**
** Function         nfc_ncif_send_data
**
** Description      This function is called to add the NCI data header
**                  and send it to NCIT task for sending it to transport
**                  as credits are available.
**
** Returns          void
**
*******************************************************************************/
UINT8 nfc_ncif_send_data (tNFC_CONN_CB *p_cb, BT_HDR *p_data)
{
    UINT8   *pp;
    UINT8   *ps;
    UINT8   ulen = NCI_MAX_PAYLOAD_SIZE;
    BT_HDR  *p;
    UINT8   pbf = 1;
    UINT8   buffer_size = p_cb->buff_size;
    UINT8   hdr0 = p_cb->conn_id;
    BOOLEAN fragmented = FALSE;
#if(NXP_EXTNS == TRUE)
    UINT8* pTemp;
    if(core_reset_init_num_buff == TRUE)
    {
        NFC_TRACE_ERROR0("Reinitializing the num_buff");

        if(!p_cb->num_buff)
            p_cb->num_buff++;
        core_reset_init_num_buff = FALSE;
    }
#endif
    if(get_i2c_fragmentation_enabled() == I2C_FRAGMENATATION_ENABLED)
    {
         if(nfc_cb.i2c_data_t.nci_cmd_channel_busy == 1 && p_data)
         {
             NFC_TRACE_DEBUG0 ("NxpNci : avoiding data packet sending data packet");
             nfc_cb.i2c_data_t.conn_id = p_cb->conn_id;
             GKI_enqueue (&p_cb->tx_q, p_data);
             nfc_cb.i2c_data_t.data_stored = 1;
             return NCI_STATUS_OK;
         }
    }
    NFC_TRACE_DEBUG3 ("nfc_ncif_send_data :%d, num_buff:%d qc:%d", p_cb->conn_id, p_cb->num_buff, p_cb->tx_q.count);
    if (p_cb->id == NFC_RF_CONN_ID)
    {
        if (nfc_cb.nfc_state != NFC_STATE_OPEN)
        {
            if (nfc_cb.nfc_state == NFC_STATE_CLOSING)
            {
                if ((p_data == NULL) && /* called because credit from NFCC */
                    (nfc_cb.flags  & NFC_FL_DEACTIVATING))
                {
                    if (p_cb->init_credits == p_cb->num_buff)
                    {
                        /* all the credits are back */
                        nfc_cb.flags  &= ~NFC_FL_DEACTIVATING;
                        NFC_TRACE_DEBUG2 ("deactivating NFC-DEP init_credits:%d, num_buff:%d", p_cb->init_credits, p_cb->num_buff);
                        nfc_stop_timer(&nfc_cb.deactivate_timer);
                        nci_snd_deactivate_cmd ((UINT8)((TIMER_PARAM_TYPE)nfc_cb.deactivate_timer.param));
                    }
                }
            }
            return NCI_STATUS_FAILED;
        }
    }

    if (p_data)
    {
        /* always enqueue the data to the tx queue */
        GKI_enqueue (&p_cb->tx_q, p_data);
    }

#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
    if((nfc_cb.bBlockWiredMode) && (p_cb->conn_id == NFC_NFCEE_CONN_ID))
    {
        /* CE is activated on UICC hence return simply without sedning data */
        return (NCI_STATUS_OK);
    }
#endif

    /* try to send the first data packet in the tx queue  */
    p_data = (BT_HDR *)GKI_getfirst (&p_cb->tx_q);

    /* post data fragment to NCIT task as credits are available */
    while (p_data && (p_data->len >= 0) && (p_cb->num_buff > 0))
    {
        if (p_data->len <= buffer_size)
        {
            pbf         = 0;   /* last fragment */
            ulen        = (UINT8)(p_data->len);
            fragmented  = FALSE;
        }
        else
        {
            fragmented  = TRUE;
            ulen        = buffer_size;
        }

        if (!fragmented)
        {
            /* if data packet is not fragmented, use the original buffer */
            p         = p_data;
            p_data    = (BT_HDR *)GKI_dequeue (&p_cb->tx_q);
        }
        else
        {
            /* the data packet is too big and need to be fragmented
             * prepare a new GKI buffer
             * (even the last fragment to avoid issues) */
            if ((p = NCI_GET_CMD_BUF(ulen)) == NULL)
                return (NCI_STATUS_BUFFER_FULL);
            p->len    = ulen;
            p->offset = NCI_MSG_OFFSET_SIZE + NCI_DATA_HDR_SIZE + 1;
            if (p->len)
            {
                pp        = (UINT8 *)(p + 1) + p->offset;
                ps        = (UINT8 *)(p_data + 1) + p_data->offset;
                memcpy (pp, ps, ulen);
            }
            /* adjust the BT_HDR on the old fragment */
            p_data->len     -= ulen;
            p_data->offset  += ulen;
        }

        p->event             = BT_EVT_TO_NFC_NCI;
        p->layer_specific    = pbf;
        p->len              += NCI_DATA_HDR_SIZE;
        p->offset           -= NCI_DATA_HDR_SIZE;
        pp = (UINT8 *)(p + 1) + p->offset;
        /* build NCI Data packet header */
        NCI_DATA_PBLD_HDR(pp, pbf, hdr0, ulen);
        if (p_cb->num_buff != NFC_CONN_NO_FC)
            p_cb->num_buff--;

#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
        if(p_cb->conn_id == NFC_NFCEE_CONN_ID)
        {
            nfc_cb.temp_data = (BT_HDR*)temp_buff;
            nfc_cb.temp_data->offset = p->offset;
            pTemp = (UINT8*)(nfc_cb.temp_data+1) + nfc_cb.temp_data->offset;
            nfc_cb.temp_data->len = p->len;
            memcpy(pTemp, (UINT8*)(p+1)+(p->offset), p->len);
        }
#endif

        /* send to HAL */
        HAL_WRITE(p);
#if(NXP_EXTNS == TRUE)
        /* start NFC data ntf timeout timer */
        if( get_i2c_fragmentation_enabled () == I2C_FRAGMENATATION_ENABLED)
        {
            nfc_cb.nci_cmd_window--;
        }
        if(p_cb->conn_id != NFC_RF_CONN_ID)
        {
            //Start waiting for credit ntf
            if(p_cb->conn_id == NFC_NFCEE_CONN_ID)
            {
                nfc_cb.bIsCreditNtfRcvd = FALSE;
            }
            nfc_start_timer (&nfc_cb.nci_wait_data_ntf_timer, (UINT16)(NFC_TTYPE_NCI_WAIT_DATA_NTF), NFC_NCI_WAIT_DATA_NTF_TOUT);
        }
        if (!fragmented)
        {
            /* check if there are more data to send */
            p_data = (BT_HDR *)GKI_getfirst (&p_cb->tx_q);
        }
#endif
    }

    return (NCI_STATUS_OK);
}

#if(NXP_EXTNS == TRUE)
/*Function to empty cmd queue.*/
void nfc_ncif_empty_cmd_queue ()
{
    BT_HDR *p_buf = (BT_HDR *)GKI_dequeue (&nfc_cb.nci_cmd_xmit_q);

    while(p_buf)
    {
        GKI_freebuf (p_buf);
        p_buf = (BT_HDR *)GKI_dequeue (&nfc_cb.nci_cmd_xmit_q);
    }
}

/*Function to empty data queue.*/
void nfc_ncif_empty_data_queue ()
{
    BT_HDR * p_data    = (BT_HDR *)GKI_dequeue (&p_cb_stored->tx_q);

    while(p_data)
    {
        GKI_freebuf (p_data);
        p_data    = (BT_HDR *)GKI_dequeue (&p_cb_stored->tx_q);
    }
}
#endif
/*******************************************************************************
**
** Function         nfc_ncif_check_cmd_queue
**
** Description      Send NCI command to the transport
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_check_cmd_queue (BT_HDR *p_buf)
{
    UINT8   *ps;
    /* If there are commands waiting in the xmit queue, or if the controller cannot accept any more commands, */
    /* then enqueue this command */
    if (p_buf)
    {
        if ((nfc_cb.nci_cmd_xmit_q.count) || (nfc_cb.nci_cmd_window == 0))
        {
            GKI_enqueue (&nfc_cb.nci_cmd_xmit_q, p_buf);
#if (NXP_EXTNS == TRUE)
            if(p_buf != NULL)
            {
                NFC_TRACE_DEBUG0 ("nfc_ncif_check_cmd_queue : making p_buf NULL.");
                p_buf = NULL;
            }
#else
            p_buf = NULL;
#endif
        }
    }
    /* If controller can accept another command, then send the next command */
    if (nfc_cb.nci_cmd_window > 0)
    {
        /* If no command was provided, or if older commands were in the queue, then get cmd from the queue */
        if (!p_buf)
            p_buf = (BT_HDR *)GKI_dequeue (&nfc_cb.nci_cmd_xmit_q);

        if (p_buf)
        {
#if (NXP_EXTNS == TRUE)
            NFC_TRACE_DEBUG0 ("nfc_ncif_check_cmd_queue : Writing to HAL...");
            /*save the message header to double check the response */
            ps   = (UINT8 *)(p_buf + 1) + p_buf->offset;
            /*save command HEADER(GID+OID) only*/
            memcpy(nfc_cb.last_hdr, ps, NFC_SAVED_HDR_SIZE);
            /*save command length only*/
            nfc_cb.cmd_size = *(ps + NFC_SAVED_HDR_SIZE);
            NFC_TRACE_DEBUG2 ("%s : cmd_size:%d", __FUNCTION__, nfc_cb.cmd_size);
            if((nfc_cb.last_hdr[0] == 0x20 && (nfc_cb.last_hdr[1] == 0x02 || nfc_cb.last_hdr[1] == 0x03))
                || (nfc_cb.last_hdr[0] == 0x2F && nfc_cb.last_hdr[1] == 0x15)
                || (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x01)
                || (nfc_cb.last_hdr[0] == 0x21 && nfc_cb.last_hdr[1] == 0x06))
            {
                if (nfc_cb.last_cmd_buf != NULL)
                {
                    GKI_freebuf(nfc_cb.last_cmd_buf); // ======> Free before allocation
                }
                nfc_cb.last_cmd_buf = (UINT8 *) GKI_getbuf(nfc_cb.cmd_size + 1);
                if(nfc_cb.last_cmd_buf != NULL)
                {
                    /*save command data including length and excluding header*/
                    memcpy(nfc_cb.last_cmd_buf, ps + NFC_SAVED_HDR_SIZE, (nfc_cb.cmd_size + 1));
                }
                else
                {
                    NFC_TRACE_DEBUG0 ("NULL buffer nfc_cb.last_cmd_buf");
                }
                memcpy(nfc_cb.last_cmd, ps + NCI_MSG_HDR_SIZE, NFC_SAVED_CMD_SIZE);
            }
            else
            {
                memcpy(nfc_cb.last_cmd, ps + NCI_MSG_HDR_SIZE, NFC_SAVED_CMD_SIZE);
            }
#else
            /* save the message header to double check the response */
            ps   = (UINT8 *)(p_buf + 1) + p_buf->offset;
            memcpy(nfc_cb.last_hdr, ps, NFC_SAVED_HDR_SIZE);
            memcpy(nfc_cb.last_cmd, ps + NCI_MSG_HDR_SIZE, NFC_SAVED_CMD_SIZE);
#endif
            if (p_buf->layer_specific == NFC_WAIT_RSP_VSC)
            {
                /* save the callback for NCI VSCs)  */
                nfc_cb.p_vsc_cback = (void *)((tNFC_NCI_VS_MSG *)p_buf)->p_cback;
            }
#if(NXP_EXTNS == TRUE)
            else if (p_buf->layer_specific == NFC_WAIT_RSP_NXP)
            {
                /* save the callback for NCI NXPs)  */
                nfc_cb.p_vsc_cback = (void *)((tNFC_NCI_VS_MSG *)p_buf)->p_cback;
                nfc_cb.nxpCbflag = TRUE;
            }
            /* Indicate command is pending */
            nfc_cb.nci_cmd_window--;
#endif
            /* send to HAL */
            HAL_WRITE(p_buf);
#if(NXP_EXTNS != TRUE)
             /* Indicate command is pending */
             nfc_cb.nci_cmd_window--;
#endif
            if (get_i2c_fragmentation_enabled () == I2C_FRAGMENATATION_ENABLED)
            {
                nfc_cb.i2c_data_t.nci_cmd_channel_busy= 1;
                NFC_TRACE_DEBUG0 ("setting channel busy flag");
            }

            /* start NFC command-timeout timer */
            nfc_start_timer (&nfc_cb.nci_wait_rsp_timer, (UINT16)(NFC_TTYPE_NCI_WAIT_RSP), nfc_cb.nci_wait_rsp_tout);
        }
    }

    if (nfc_cb.nci_cmd_window == NCI_MAX_CMD_WINDOW)
    {
        /* the command queue must be empty now */
        if (nfc_cb.flags & NFC_FL_CONTROL_REQUESTED)
        {
            /* HAL requested control or stack needs to handle pre-discover */
            nfc_cb.flags &= ~NFC_FL_CONTROL_REQUESTED;
            if (nfc_cb.flags & NFC_FL_DISCOVER_PENDING)
            {
                if (nfc_cb.p_hal->prediscover ())
                {
                    /* HAL has the command window now */
                    nfc_cb.flags         |= NFC_FL_CONTROL_GRANTED;
                    nfc_cb.nci_cmd_window = 0;
                }
                else
                {
                    /* HAL does not need to send command,
                     * - restore the command window and issue the discovery command now */
                    nfc_cb.flags         &= ~NFC_FL_DISCOVER_PENDING;
                    ps                    = (UINT8 *)nfc_cb.p_disc_pending;
                    nci_snd_discover_cmd (*ps, (tNFC_DISCOVER_PARAMS *)(ps + 1));
#if(NXP_EXTNS == TRUE)
                    if(nfc_cb.p_last_disc)
                    {
                        GKI_freebuf( nfc_cb.p_last_disc);
                        nfc_cb.p_last_disc = NULL;
                    }
                    nfc_cb.p_last_disc = nfc_cb.p_disc_pending;
#else
                    GKI_freebuf (nfc_cb.p_disc_pending);
#endif
                    nfc_cb.p_disc_pending = NULL;
                }
            }
            else if (nfc_cb.flags & NFC_FL_HAL_REQUESTED)
            {
                /* grant the control to HAL */
                nfc_cb.flags         &= ~NFC_FL_HAL_REQUESTED;
                nfc_cb.flags         |= NFC_FL_CONTROL_GRANTED;
                nfc_cb.nci_cmd_window = 0;
                nfc_cb.p_hal->control_granted ();
            }
        }
    }
}


/*******************************************************************************
**
** Function         nfc_ncif_send_cmd
**
** Description      Send NCI command to the NCIT task
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_send_cmd (BT_HDR *p_buf)
{
#if(NXP_EXTNS == TRUE)
    NFC_TRACE_DEBUG0 ("nfc_ncif_send_cmd()");
    if(p_buf == NULL)
    {
        NFC_TRACE_DEBUG0 ("p_buf is NULL.");
        return;
    }
    UINT8 *cmd = NULL;
    cmd = (UINT8 *)(p_buf+1) + p_buf->offset;

    if(sListenActivated == TRUE)
    {
        nfc_stop_timer (&nfc_cb.listen_activation_timer_list);
        sListenActivated = FALSE;
    }
#endif
    /* post the p_buf to NCIT task */
    p_buf->event            = BT_EVT_TO_NFC_NCI;
    p_buf->layer_specific   = 0;
    nfc_ncif_check_cmd_queue (p_buf);
}


/*******************************************************************************
**
** Function         nfc_ncif_process_event
**
** Description      This function is called to process the data/response/notification
**                  from NFCC
**
** Returns          TRUE if need to free buffer
**
*******************************************************************************/
BOOLEAN nfc_ncif_process_event (BT_HDR *p_msg)
{
    UINT8   mt, pbf, gid, *p, *pp;
    BOOLEAN free = TRUE;
    UINT8   oid;
    UINT8   *p_old, old_gid, old_oid, old_mt;
    p = (UINT8 *) (p_msg + 1) + p_msg->offset;

    pp = p;
    NCI_MSG_PRS_HDR0 (pp, mt, pbf, gid);
#if(NXP_EXTNS == TRUE)
    oid = ((*pp) & NCI_OID_MASK);


    if(sListenActivated == TRUE)
    {
        nfc_stop_timer (&nfc_cb.listen_activation_timer_list);
        sListenActivated = FALSE;
    }

    if ((nfc_cb.nxpCbflag == TRUE)&&(nfc_ncif_proc_proprietary_rsp(mt,gid,oid) == TRUE))
    {
        nci_proc_prop_nxp_rsp(p_msg);
        nfc_cb.nxpCbflag = FALSE;
        return (free);
    }
#endif

    switch (mt)
    {
    case NCI_MT_DATA:
        NFC_TRACE_DEBUG0 ("NFC received data");
        nfc_ncif_proc_data (p_msg);
        free = FALSE;
        break;

    case NCI_MT_RSP:
        NFC_TRACE_DEBUG1 ("NFC received rsp gid:%d", gid);
        oid = ((*pp) & NCI_OID_MASK);
        p_old   = nfc_cb.last_hdr;
        NCI_MSG_PRS_HDR0(p_old, old_mt, pbf, old_gid);
        old_oid = ((*p_old) & NCI_OID_MASK);
        /* make sure this is the RSP we are waiting for before updating the command window */
        if ((old_gid != gid) || (old_oid != oid))
        {
#if(NFC_NXP_ESE == TRUE && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
            if(((gid == NCI_GID_RF_MANAGE)&&(oid == NCI_MSG_RF_DISCOVER)) && (etsi_reader_in_progress == TRUE))
            {
                NFC_TRACE_DEBUG0 ("Changing disc_state and disc_flags");
                nfa_dm_cb.disc_cb.disc_state = NFA_DM_RFST_IDLE;
                nfa_dm_cb.disc_cb.disc_flags &= ~(NFA_DM_DISC_FLAGS_W4_NTF | NFA_DM_DISC_FLAGS_STOPPING);
                nfa_dm_cb.disc_cb.disc_flags |= (NFA_DM_DISC_FLAGS_W4_RSP|NFA_DM_DISC_FLAGS_NOTIFY|NFA_DM_DISC_FLAGS_ENABLED);
                disc_deact_ntf_timeout_handler(NFC_NFCC_TIMEOUT_REVT);
            }
            else
            {
#endif
            /*no response after the deactivate command, handling the error scenario after the recovery*/
            if((gid == NCI_GID_RF_MANAGE)&&(oid == NCI_MSG_RF_DISCOVER)&&(nfc_cb.nci_cmd_window == 0))
            {
                NFC_TRACE_DEBUG0 ("resetting the nci_cmd_window");
                nfc_cb.nci_cmd_window ++;
            }
            else
            {
                NFC_TRACE_ERROR2 ("nfc_ncif_process_event unexpected rsp: gid:0x%x, oid:0x%x", gid, oid);
                return TRUE;
            }
#if((NFC_NXP_ESE == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
            }
#endif
        }

        switch (gid)
        {
        case NCI_GID_CORE:      /* 0000b NCI Core group */
            free = nci_proc_core_rsp (p_msg);
            break;
        case NCI_GID_RF_MANAGE:   /* 0001b NCI Discovery group */
            nci_proc_rf_management_rsp (p_msg);
            break;
#if (NFC_NFCEE_INCLUDED == TRUE)
#if (NFC_RW_ONLY == FALSE)
        case NCI_GID_EE_MANAGE:  /* 0x02 0010b NFCEE Discovery group */
            nci_proc_ee_management_rsp (p_msg);
            break;
#endif
#endif
        case NCI_GID_PROP:      /* 1111b Proprietary */
                nci_proc_prop_rsp (p_msg);
            break;
        default:
            NFC_TRACE_ERROR1 ("NFC: Unknown gid:%d", gid);
            break;
        }
#if(NXP_EXTNS == TRUE)
        //received command response
        //set the saved header and command  buffer to zero
        nfc_cb.cmd_size = 0x00;
        memset(nfc_cb.last_hdr,0,NFC_SAVED_HDR_SIZE);
        memset(nfc_cb.last_cmd,0,NFC_SAVED_CMD_SIZE);
#endif
        if(get_i2c_fragmentation_enabled() == I2C_FRAGMENATATION_ENABLED)
        {
            nfc_cb.i2c_data_t.nci_cmd_channel_busy = 0;
            NFC_TRACE_DEBUG1("%s,updating window" , __FUNCTION__);
            p_cb_stored = nfc_find_conn_cb_by_conn_id(nfc_cb.i2c_data_t.conn_id);
            nfc_ncif_update_window ();
            if(p_cb_stored &&
                    (nfc_cb.i2c_data_t.data_stored == 1)  &&
                    (nfc_cb.i2c_data_t.nci_cmd_channel_busy == 0x00) )
            {
                NFC_TRACE_ERROR0 ("resending the stored data  packet");
                nfc_ncif_send_data (p_cb_stored, NULL);
                nfc_cb.i2c_data_t.data_stored = 0;
            }
        }
        else
        {
            nfc_ncif_update_window ();
        }
        break;
    case NCI_MT_NTF:
        NFC_TRACE_DEBUG1 ("NFC received ntf gid:%d", gid);
        switch (gid)
        {
        case NCI_GID_CORE:      /* 0000b NCI Core group */
            nci_proc_core_ntf (p_msg);
            break;
        case NCI_GID_RF_MANAGE:   /* 0001b NCI Discovery group */
            nci_proc_rf_management_ntf (p_msg);
            break;
#if (NFC_NFCEE_INCLUDED == TRUE)
#if (NFC_RW_ONLY == FALSE)
        case NCI_GID_EE_MANAGE:  /* 0x02 0010b NFCEE Discovery group */
            nci_proc_ee_management_ntf (p_msg);
            break;
#endif
#endif
        case NCI_GID_PROP:      /* 1111b Proprietary */
                nci_proc_prop_ntf (p_msg);
            break;
        default:
            NFC_TRACE_ERROR1 ("NFC: Unknown gid:%d", gid);
            break;
        }
        break;

    default:
        NFC_TRACE_DEBUG2 ("NFC received unknown mt:0x%x, gid:%d", mt, gid);
    }

    return (free);
}

/*******************************************************************************
**
** Function         nfc_ncif_rf_management_status
**
** Description      This function is called to report an event
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_rf_management_status (tNFC_DISCOVER_EVT event, UINT8 status)
{
    tNFC_DISCOVER   evt_data;
    if (nfc_cb.p_discv_cback)
    {
        evt_data.status = (tNFC_STATUS) status;
        (*nfc_cb.p_discv_cback) (event, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_set_config_status
**
** Description      This function is called to report NFC_SET_CONFIG_REVT
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_set_config_status (UINT8 *p, UINT8 len)
{
    tNFC_RESPONSE   evt_data;
    (void)len;

    if (nfc_cb.p_resp_cback)
    {
        evt_data.set_config.status          = (tNFC_STATUS) *p++;
        evt_data.set_config.num_param_id    = NFC_STATUS_OK;
        if (evt_data.set_config.status != NFC_STATUS_OK)
        {
            evt_data.set_config.num_param_id    = *p++;
            STREAM_TO_ARRAY (evt_data.set_config.param_ids, p, evt_data.set_config.num_param_id);
        }

        (*nfc_cb.p_resp_cback) (NFC_SET_CONFIG_REVT, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_event_status
**
** Description      This function is called to report an event
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_event_status (tNFC_RESPONSE_EVT event, UINT8 status)
{
    tNFC_RESPONSE   evt_data;

#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
    if((status == NFC_STATUS_WIRED_SESSION_ABORTED) || (status == NFC_STATUS_DWP_APDU_DROPPPED))
    {
        tNFC_CONN_CB *p_cb;
        nfc_cb.bBlockWiredMode = TRUE;
        nfc_cb.bBlkPwrlinkAndModeSetCmd  = TRUE;
        p_cb = nfc_find_conn_cb_by_conn_id(nfa_hci_cb.conn_id);
        nfc_stop_timer(&nfc_cb.rf_filed_event_timeout_timer);
        if(!nfc_cb.bIsCreditNtfRcvd)
        {
            nfc_stop_timer(&nfc_cb.nci_wait_data_ntf_timer);
            nfc_cb.bIsCreditNtfRcvd = TRUE;

            if((p_cb) && !p_cb->num_buff)
                p_cb->num_buff++;
        }
        nfc_cb.bSetmodeOnReq = TRUE;  /* Sending mode set ON is required on RF deactivation to resume DWP */
        nfa_hci_cb.bIsHciResponseTimedout = FALSE;
        if(status == NFC_STATUS_DWP_APDU_DROPPPED)
        {
            nfc_cb.bRetransmitDwpPacket = TRUE;
        }
        if(status == NFC_STATUS_WIRED_SESSION_ABORTED)
        {
            nfc_cb.bIsDwpResPending = TRUE;
            nfa_hci_cb.IsWiredSessionAborted = TRUE;
        }
        nfa_sys_stop_timer(&nfa_hci_cb.timer);
        if(p_cb && p_cb->p_cback)
            (*p_cb->p_cback)(nfa_hci_cb.conn_id, NFC_HCI_RESTART_TIMER, (tNFC_CONN *)&evt_data);
    }
#endif

    if (nfc_cb.p_resp_cback)
    {
        evt_data.status = (tNFC_STATUS) status;
        (*nfc_cb.p_resp_cback) (event, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_error_status
**
** Description      This function is called to report an error event to data cback
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_error_status (UINT8 conn_id, UINT8 status)
{
    tNFC_CONN_CB * p_cb;
    p_cb = nfc_find_conn_cb_by_conn_id (conn_id);
    if (p_cb && p_cb->p_cback)
    {
        (*p_cb->p_cback) (conn_id, NFC_ERROR_CEVT, (void *) &status);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_rf_field_ntf
**
** Description      This function is called to process RF field notification
**
** Returns          void
**
*******************************************************************************/
#if (NFC_RW_ONLY == FALSE)
void nfc_ncif_proc_rf_field_ntf (UINT8 rf_status)
{
    NFC_TRACE_DEBUG0( "nfc_ncif_proc_rf_field_ntf");
    tNFC_RESPONSE   evt_data;
#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
    if(rf_status == 0x01)
    {
        NFC_TRACE_DEBUG0("nfc_ncif_proc_rf_field_ntf ON_EVT");
        nfc_cb.bBlockWiredMode = TRUE;
        nfc_cb.bBlkPwrlinkAndModeSetCmd  = TRUE;
        nfc_stop_timer(&nfc_cb.nci_wait_setMode_Ntf_timer); /* stop mode set Ntf timer if it is allready started */
        nfc_start_timer(&nfc_cb.rf_filed_event_timeout_timer, (UINT16)(NFC_TTYPE_NCI_WAIT_RF_FIELD_NTF), NFC_NCI_RFFIELD_EVT_TIMEOUT);
    }
    else
    {
        nfc_stop_timer(&nfc_cb.rf_filed_event_timeout_timer);
        if(nfc_cb.bBlockWiredMode)
        {
            nfc_start_timer(&nfc_cb.rf_filed_event_timeout_timer, (UINT16)(NFC_TTYPE_NCI_WAIT_RF_FIELD_NTF), NFC_NCI_RFFIELD_EVT_TIMEOUT);
        }
    }
#endif

    if (nfc_cb.p_resp_cback)
    {
        evt_data.status            = (tNFC_STATUS) NFC_STATUS_OK;
        evt_data.rf_field.rf_field = rf_status;
        (*nfc_cb.p_resp_cback) (NFC_RF_FIELD_REVT, &evt_data);
    }
}
#endif

#if ((NXP_EXTNS == TRUE ) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
/*******************************************************************************
**
** Function         nfc_ncif_allow_dwp_transmission
**
** Description      This function is called when CE deactivation/RF event timedout/RF OFF event
**                  eSE action notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_allow_dwp_transmission()
{
    tNFC_RESPONSE   evt_data;
    tNFC_CONN_CB *p_cb;
    NFC_TRACE_DEBUG2("nfc_ncif_allow_dwp_transmission %d, %d ", nfc_cb.bIsDwpResPending, nfc_cb.bRetransmitDwpPacket);
    p_cb = nfc_find_conn_cb_by_conn_id(nfa_hci_cb.conn_id);

    if((p_cb)&&(p_cb->conn_id == NFC_NFCEE_CONN_ID))
    {
        if(nfa_hci_cb.bIsHciResponseTimedout)
        {
            nfa_hci_cb.IsHciTimerChanged =  FALSE;
            nfc_cb.bRetransmitDwpPacket  = FALSE;
            nfa_hci_cb.bIsHciResponseTimedout = FALSE;
            nfc_cb.bIsDwpResPending = FALSE;
        }
        if(nfc_cb.bIsDwpResPending)
        {
            nfa_sys_stop_timer(&nfa_hci_cb.timer);
            nfc_cb.bIsDwpResPending = FALSE;
            nfa_hci_cb.IsHciTimerChanged = TRUE;
            if(p_cb->p_cback)
                (*p_cb->p_cback)(nfa_hci_cb.conn_id, NFC_HCI_RESTART_TIMER, (tNFC_CONN *)&evt_data);
        }
        else if(nfc_cb.bRetransmitDwpPacket)
        {
            nfa_sys_stop_timer(&nfa_hci_cb.timer);
            nfc_ncif_retransmit_data(p_cb, nfc_cb.temp_data);
            nfc_cb.bRetransmitDwpPacket = FALSE;
            if(p_cb->p_cback)
                (*p_cb->p_cback)(nfa_hci_cb.conn_id, NFC_HCI_RESTART_TIMER, (tNFC_CONN *)&evt_data);
        }
        else
        {
            nfc_ncif_send_data (p_cb, NULL);
        }
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_onWiredModeHold_timeout
**
** Description      This function is called when RF field event timedout
**                  To aviod infinite dwp block on RF_ON event
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_onWiredModeHold_timeout()
{
    NFC_TRACE_DEBUG0("nfc_ncif_onWiredModeHold_timeout");
    if (nfc_cb.bBlockWiredMode)
    {
        nfc_ncif_resume_dwp_wired_mode();
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_resume_dwp_wired_mode
**
** Description      This function is called to resume DWP session when RF is session is over
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_resume_dwp_wired_mode()
{
    NFC_TRACE_DEBUG0("nfc_ncif_resume_dwp_wired_mode");
    tNFC_CONN_CB *p_cb;
    p_cb = nfc_find_conn_cb_by_conn_id(nfa_hci_cb.conn_id);
    nfc_cb.bBlkPwrlinkAndModeSetCmd  = FALSE;
    nfc_cb.bIssueModeSetCmd = FALSE;
    if(nfc_cb.pwr_link_cmd.bPwrLinkCmdRequested)
    {
        NFC_TRACE_DEBUG0("pwr link cmd to send");
        nci_snd_pwr_nd_lnk_ctrl_cmd(NFCEE_ID_ESE,nfc_cb.pwr_link_cmd.param);
        nfc_cb.pwr_link_cmd.bPwrLinkCmdRequested = FALSE;
        if(!nfc_cb.bCeActivatedeSE)
            nfc_cb.bIssueModeSetCmd = TRUE;
    }
    else if(((nfc_cb.bSetmodeOnReq) || (!GKI_queue_is_empty(&p_cb->tx_q))) && (!nfc_cb.bCeActivatedeSE))
    {
        NFC_TRACE_DEBUG0("mode set cmd to send");
        nfc_cb.bSetmodeOnReq = TRUE;
        nci_snd_nfcee_mode_set(NFCEE_ID_ESE, NFC_MODE_ACTIVATE);
        nfc_start_timer(&nfc_cb.nci_wait_setMode_Ntf_timer, (UINT16)NFC_TYPE_NCI_WAIT_SETMODE_NTF, NFC_NCI_SETMODE_NTF_TIMEOUT);
    }
    else
    {
        nfc_cb.bBlockWiredMode = FALSE;
        nfc_cb.bCeActivatedeSE = FALSE;
        nfc_ncif_allow_dwp_transmission();
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_modeSet_Ntf_timeout
**
** Description      This function is called when mode set ntf timedout
**                  To aviod infinite wait for mode set ntf
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_modeSet_Ntf_timeout()
{
    NFC_TRACE_DEBUG0("nfc_ncif_modeSet_Ntf_timeout");
    if (nfc_cb.bBlockWiredMode)
    {
        nfc_cb.bBlockWiredMode = FALSE;
        nfc_cb.bCeActivatedeSE = FALSE;
        nfc_ncif_allow_dwp_transmission();
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_modeSet_rsp_timeout
**
** Description      This function is called when mode set rsp timedout
**                  To aviod infinite wait for mode set rsp
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_modeSet_rsp_timeout()
{
    NFC_TRACE_DEBUG0("nfc_ncif_modeSet_rsp_timeout");
    tNFC_NFCEE_MODE_SET_REVT    mode_set_info;
    tNFC_RESPONSE_CBACK   *p_cback = nfc_cb.p_resp_cback;
    tNFC_NFCEE_INFO_REVT  nfcee_info;
    tNFC_RESPONSE         *p_evt   = (tNFC_RESPONSE *) &nfcee_info;
    tNFC_RESPONSE_EVT     event    = NFC_NFCEE_INFO_REVT;
    p_evt = (tNFC_RESPONSE *) &mode_set_info;
    event = NFC_NFCEE_MODE_SET_REVT;
    mode_set_info.nfcee_id    = NFCEE_ID_ESE;
    mode_set_info.mode        = NFC_MODE_ACTIVATE;
    mode_set_info.status        = NCI_STATUS_OK;
    if (p_cback)
    {
        (*p_cback) (event, p_evt);
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_pwr_link_rsp_timeout
**
** Description      This function is called when pwr link cmd is ignored due to rf session ongoing and sending fake resp
**                  to aviod infinite wait for power link Rsp
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_pwr_link_rsp_timeout()
{
    NFC_TRACE_DEBUG0("nfc_ncif_pwr_link_rsp_timeout");
    tNFC_RESPONSE_CBACK *p_cback = nfc_cb.p_resp_cback;
    tNFC_NFCEE_EE_PWR_LNK_REVT  pwr_lnk_ctrl;
    tNFC_NFCEE_INFO_REVT  nfcee_info;
    tNFC_RESPONSE_EVT event = NFC_NFCEE_INFO_REVT;
    tNFC_RESPONSE         *p_evt   = (tNFC_RESPONSE *) &nfcee_info;
    p_evt                   = (tNFC_RESPONSE *) &pwr_lnk_ctrl;
    pwr_lnk_ctrl.status         = NCI_STATUS_OK;
    pwr_lnk_ctrl.nfcee_id       = NFCEE_ID_ESE;
    event                       = NFC_NFCEE_PWR_LNK_CTRL_REVT;
    if (p_cback)
        (*p_cback) (event, p_evt);
}
#endif

/*******************************************************************************
**
** Function         nfc_ncif_proc_credits
**
** Description      This function is called to process data credits
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_credits(UINT8 *p, UINT16 plen)
{
    UINT8   num, xx;
    tNFC_CONN_CB * p_cb;
    (void)plen;

    num = *p++;
    for (xx = 0; xx < num; xx++)
    {
        p_cb = nfc_find_conn_cb_by_conn_id(*p++);
        if (p_cb && p_cb->num_buff != NFC_CONN_NO_FC)
        {
            p_cb->num_buff += (*p);
#if (BT_USE_TRACES == TRUE)
            if (p_cb->num_buff > p_cb->init_credits)
            {
                if (nfc_cb.nfc_state == NFC_STATE_OPEN)
                {
                    /* if this happens in activated state, it's very likely that our NFCC has issues */
                    /* However, credit may be returned after deactivation */
                    NFC_TRACE_ERROR2( "num_buff:0x%x, init_credits:0x%x", p_cb->num_buff, p_cb->init_credits);
                }
                p_cb->num_buff = p_cb->init_credits;
            }
#endif
#if (NXP_EXTNS == TRUE)
            if(p_cb->conn_id != NFC_RF_CONN_ID)
            {
                nfc_stop_timer (&nfc_cb.nci_wait_data_ntf_timer);
            }
            if(p_cb->conn_id == NFC_NFCEE_CONN_ID)
            {
                nfc_cb.bIsCreditNtfRcvd = TRUE;
            }
#endif
            if(get_i2c_fragmentation_enabled() == I2C_FRAGMENATATION_ENABLED){
                 nfc_ncif_update_data_queue();
            }
            /* check if there's nay data in tx q to be sent */
            nfc_ncif_send_data (p_cb, NULL);
        }
        p++;
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_decode_rf_params
**
** Description      This function is called to process the detected technology
**                  and mode and the associated parameters for DISCOVER_NTF and
**                  ACTIVATE_NTF
**
** Returns          void
**
*******************************************************************************/
UINT8 * nfc_ncif_decode_rf_params (tNFC_RF_TECH_PARAMS *p_param, UINT8 *p)
{
    tNFC_RF_PA_PARAMS   *p_pa;
    UINT8               len, *p_start, u8;
    tNFC_RF_PB_PARAMS   *p_pb;
    tNFC_RF_LF_PARAMS   *p_lf;
    tNFC_RF_PF_PARAMS   *p_pf;
    tNFC_RF_PISO15693_PARAMS *p_i93;

    len             = *p++;
    p_start         = p;
    memset ( &p_param->param, 0, sizeof (tNFC_RF_TECH_PARAMU));
    if (  NCI_DISCOVERY_TYPE_POLL_A == p_param->mode
        || NCI_DISCOVERY_TYPE_POLL_A_ACTIVE == p_param->mode  )
    {
        p_pa        = &p_param->param.pa;
        /*
SENS_RES Response   2 bytes Defined in [DIGPROT] Available after Technology Detection
NFCID1 length   1 byte  Length of NFCID1 Available after Collision Resolution
NFCID1  4, 7, or 10 bytes   Defined in [DIGPROT]Available after Collision Resolution
SEL_RES Response    1 byte  Defined in [DIGPROT]Available after Collision Resolution
HRx Length  1 Octets    Length of HRx Parameters collected from the response to the T1T RID command.
HRx 0 or 2 Octets   If present, the first byte SHALL contain HR0 and the second byte SHALL contain HR1 as defined in [DIGITAL].
        */
        STREAM_TO_ARRAY (p_pa->sens_res, p, 2);
        p_pa->nfcid1_len     = *p++;
        if (p_pa->nfcid1_len > NCI_NFCID1_MAX_LEN)
            p_pa->nfcid1_len = NCI_NFCID1_MAX_LEN;
        STREAM_TO_ARRAY (p_pa->nfcid1, p, p_pa->nfcid1_len);
        u8                   = *p++;
        if (u8)
            p_pa->sel_rsp    = *p++;
        if (len == (7 + p_pa->nfcid1_len + u8)) /* 2(sens_res) + 1(len) + p_pa->nfcid1_len + 1(len) + u8 + hr (1:len + 2) */
        {
            p_pa->hr_len     = *p++;
            if (p_pa->hr_len == NCI_T1T_HR_LEN)
            {
                p_pa->hr[0]  = *p++;
                p_pa->hr[1]  = *p;
            }
        }
    }
    else if (NCI_DISCOVERY_TYPE_POLL_B == p_param->mode)
    {
        /*
SENSB_RES Response length (n)   1 byte  Length of SENSB_RES Response (Byte 2 - Byte 12 or 13)Available after Technology Detection
SENSB_RES Response Byte 2 - Byte 12 or 13   11 or 12 bytes  Defined in [DIGPROT] Available after Technology Detection
        */
        p_pb                = &p_param->param.pb;
        p_pb->sensb_res_len = *p++;
        if (p_pb->sensb_res_len > NCI_MAX_SENSB_RES_LEN)
            p_pb->sensb_res_len = NCI_MAX_SENSB_RES_LEN;
        STREAM_TO_ARRAY (p_pb->sensb_res, p, p_pb->sensb_res_len);
        memcpy (p_pb->nfcid0, p_pb->sensb_res, NFC_NFCID0_MAX_LEN);
    }
    else if (  NCI_DISCOVERY_TYPE_POLL_F == p_param->mode
                   || NCI_DISCOVERY_TYPE_POLL_F_ACTIVE == p_param->mode  )
    {
        /*
Bit Rate    1 byte  1   212 kbps/2   424 kbps/0 and 3 to 255  RFU
SENSF_RES Response length.(n) 1 byte  Length of SENSF_RES (Byte 2 - Byte 17 or 19).Available after Technology Detection
SENSF_RES Response Byte 2 - Byte 17 or 19  n bytes Defined in [DIGPROT] Available after Technology Detection
        */
        p_pf                = &p_param->param.pf;
        p_pf->bit_rate      = *p++;
        p_pf->sensf_res_len = *p++;
        if (p_pf->sensf_res_len > NCI_MAX_SENSF_RES_LEN)
            p_pf->sensf_res_len = NCI_MAX_SENSF_RES_LEN;
        STREAM_TO_ARRAY (p_pf->sensf_res, p, p_pf->sensf_res_len);
        memcpy (p_pf->nfcid2, p_pf->sensf_res, NCI_NFCID2_LEN);
        p_pf->mrti_check    = p_pf->sensf_res[NCI_MRTI_CHECK_INDEX];
        p_pf->mrti_update   = p_pf->sensf_res[NCI_MRTI_UPDATE_INDEX];
    }
    else if (  NCI_DISCOVERY_TYPE_LISTEN_F == p_param->mode
            || NCI_DISCOVERY_TYPE_LISTEN_F_ACTIVE == p_param->mode  )
    {
        p_lf                = &p_param->param.lf;
        u8                  = *p++;
        if (u8)
        {
            STREAM_TO_ARRAY (p_lf->nfcid2, p, NCI_NFCID2_LEN);
        }
    }
    else if (NCI_DISCOVERY_TYPE_POLL_ISO15693 == p_param->mode)
    {
        p_i93               = &p_param->param.pi93;
        p_i93->flag         = *p++;
        p_i93->dsfid        = *p++;
        STREAM_TO_ARRAY (p_i93->uid, p, NFC_ISO15693_UID_LEN);
    }
    else if (NCI_DISCOVERY_TYPE_POLL_KOVIO == p_param->mode)
    {
        p_param->param.pk.uid_len = len;
        if (p_param->param.pk.uid_len > NFC_KOVIO_MAX_LEN)
        {
            NFC_TRACE_ERROR2( "Kovio UID len:0x%x exceeds max(0x%x)", p_param->param.pk.uid_len, NFC_KOVIO_MAX_LEN);
            p_param->param.pk.uid_len = NFC_KOVIO_MAX_LEN;
        }
        STREAM_TO_ARRAY (p_param->param.pk.uid, p, p_param->param.pk.uid_len);
    }

    return (p_start + len);
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_discover_ntf
**
** Description      This function is called to process discover notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_discover_ntf (UINT8 *p, UINT16 plen)
{
    tNFC_DISCOVER   evt_data;
    (void)plen;

    if (nfc_cb.p_discv_cback)
    {
        p                              += NCI_MSG_HDR_SIZE;
        evt_data.status                 = NCI_STATUS_OK;
        evt_data.result.rf_disc_id      = *p++;
        evt_data.result.protocol        = *p++;

        /* fill in tNFC_RESULT_DEVT */
        evt_data.result.rf_tech_param.mode  = *p++;
        p = nfc_ncif_decode_rf_params (&evt_data.result.rf_tech_param, p);

        evt_data.result.more = *p++;
        (*nfc_cb.p_discv_cback) (NFC_RESULT_DEVT, &evt_data);
    }
}

#if (NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function         nfc_ncif_proc_rf_wtx_ntf
**
** Description      This function is called to process rf wtx notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_rf_wtx_ntf (UINT8 *p, UINT16 plen)
{
    tNFC_CONN_CB *p_cb = NULL;
    (void)p;
    (void)plen;

    p_cb = nfc_find_conn_cb_by_conn_id (NFC_RF_CONN_ID);

    if(NULL != p_cb)
    {
        if ((p_cb->conn_id == NFC_RF_CONN_ID)
        &&(p_cb->p_cback)  )
        {
            /* Indicate upper layer that local device started receiving rf wtx */
            (*p_cb->p_cback) (p_cb->conn_id, NFC_RF_WTX_CEVT, NULL);
        }
    }
}
#endif

/*******************************************************************************
**
** Function         nfc_ncif_proc_activate
**
** Description      This function is called to process de-activate
**                  response and notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_activate (UINT8 *p, UINT8 len)
{
    tNFC_DISCOVER   evt_data;
    tNFC_INTF_PARAMS        *p_intf = &evt_data.activate.intf_param;
    tNFC_INTF_PA_ISO_DEP    *p_pa_iso;
    tNFC_INTF_LB_ISO_DEP    *p_lb_iso;
    tNFC_INTF_PB_ISO_DEP    *p_pb_iso;
#if (NFC_RW_ONLY == FALSE)
    tNFC_INTF_PA_NFC_DEP    *p_pa_nfc;
    int                     mpl_idx = 0;
    UINT8                   gb_idx = 0, mpl;
#endif
    UINT8                   t0;
    tNCI_DISCOVERY_TYPE     mode;
    tNFC_CONN_CB * p_cb = &nfc_cb.conn_cb[NFC_RF_CONN_ID];
    UINT8                   *pp, len_act;
    UINT8                   buff_size, num_buff;
    tNFC_RF_PA_PARAMS       *p_pa;

    nfc_set_state (NFC_STATE_OPEN);

    memset (p_intf, 0, sizeof (tNFC_INTF_PARAMS));
    evt_data.activate.rf_disc_id    = *p++;
    p_intf->type                    = *p++;
    evt_data.activate.protocol      = *p++;

    if (evt_data.activate.protocol == NCI_PROTOCOL_18092_ACTIVE)
        evt_data.activate.protocol = NCI_PROTOCOL_NFC_DEP;

#if (NXP_EXTNS == TRUE)
    if ((evt_data.activate.protocol == NCI_PROTOCOL_UNKNOWN) &&
        (p_intf->type == NCI_INTERFACE_FRAME))
            evt_data.activate.protocol = NCI_PROTOCOL_T3BT;
#endif

    evt_data.activate.rf_tech_param.mode    = *p++;
    buff_size                               = *p++;
    num_buff                                = *p++;
    /* fill in tNFC_activate_DEVT */
    p = nfc_ncif_decode_rf_params (&evt_data.activate.rf_tech_param, p);

    evt_data.activate.data_mode             = *p++;
    evt_data.activate.tx_bitrate            = *p++;
    evt_data.activate.rx_bitrate            = *p++;
    mode         = evt_data.activate.rf_tech_param.mode;
    len_act      = *p++;
    NFC_TRACE_DEBUG3 ("nfc_ncif_proc_activate:%d %d, mode:0x%02x", len, len_act, mode);

#if (NXP_EXTNS == TRUE)
    if( (evt_data.activate.protocol == NCI_PROTOCOL_NFC_DEP )&&(mode == NCI_DISCOVERY_TYPE_LISTEN_F) )
    {
        sListenActivated = TRUE;
        nfc_start_timer (&nfc_cb.listen_activation_timer_list, (UINT16)(NFC_TTYPE_LISTEN_ACTIVATION),  2);
    }

#if(NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME)
        if(nfc_cb.bBlockWiredMode)
        {
            nfc_stop_timer(&nfc_cb.rf_filed_event_timeout_timer);
        }
#endif
#endif

    /* just in case the interface reports activation parameters not defined in the NCI spec */
    p_intf->intf_param.frame.param_len      = len_act;
    if (p_intf->intf_param.frame.param_len > NFC_MAX_RAW_PARAMS)
        p_intf->intf_param.frame.param_len = NFC_MAX_RAW_PARAMS;
    pp = p;
    STREAM_TO_ARRAY (p_intf->intf_param.frame.param, pp, p_intf->intf_param.frame.param_len);
    if (evt_data.activate.intf_param.type == NCI_INTERFACE_ISO_DEP)
    {
        /* Make max payload of NCI aligned to max payload of ISO-DEP for better performance */
        if (buff_size > NCI_ISO_DEP_MAX_INFO)
            buff_size = NCI_ISO_DEP_MAX_INFO;

        switch (mode)
        {
        case NCI_DISCOVERY_TYPE_POLL_A:
            p_pa_iso                  = &p_intf->intf_param.pa_iso;
            p_pa_iso->ats_res_len     = *p++;

            if (p_pa_iso->ats_res_len == 0)
                break;

            if (p_pa_iso->ats_res_len > NFC_MAX_ATS_LEN)
                p_pa_iso->ats_res_len = NFC_MAX_ATS_LEN;
            STREAM_TO_ARRAY (p_pa_iso->ats_res, p, p_pa_iso->ats_res_len);
            pp = &p_pa_iso->ats_res[NCI_ATS_T0_INDEX];
            t0 = p_pa_iso->ats_res[NCI_ATS_T0_INDEX];
            pp++;       /* T0 */
            if (t0 & NCI_ATS_TA_MASK)
                pp++;   /* TA */
            if (t0 & NCI_ATS_TB_MASK)
            {
                /* FWI (Frame Waiting time Integer) & SPGI (Start-up Frame Guard time Integer) */
                p_pa_iso->fwi       = (((*pp) >> 4) & 0x0F);
                p_pa_iso->sfgi      = ((*pp) & 0x0F);
                pp++;   /* TB */
            }
            if (t0 & NCI_ATS_TC_MASK)
            {
                p_pa_iso->nad_used  = ((*pp) & 0x01);
                pp++;   /* TC */
            }
            p_pa_iso->his_byte_len  = (UINT8) (p_pa_iso->ats_res_len - (pp - p_pa_iso->ats_res));
            if (p_pa_iso->his_byte_len > NFC_MAX_HIS_BYTES_LEN)
                p_pa_iso->his_byte_len = NFC_MAX_HIS_BYTES_LEN;
            memcpy (p_pa_iso->his_byte,  pp, p_pa_iso->his_byte_len);
            break;

        case NCI_DISCOVERY_TYPE_LISTEN_A:
            p_intf->intf_param.la_iso.rats = *p++;
            break;

        case NCI_DISCOVERY_TYPE_POLL_B:
            /* ATTRIB RSP
            Byte 1   Byte 2 ~ 2+n-1
            MBLI/DID Higher layer - Response
            */
            p_pb_iso                     = &p_intf->intf_param.pb_iso;
            p_pb_iso->attrib_res_len     = *p++;

            if (p_pb_iso->attrib_res_len == 0)
                break;

            if (p_pb_iso->attrib_res_len > NFC_MAX_ATTRIB_LEN)
                p_pb_iso->attrib_res_len = NFC_MAX_ATTRIB_LEN;
            STREAM_TO_ARRAY (p_pb_iso->attrib_res, p, p_pb_iso->attrib_res_len);
            p_pb_iso->mbli = (p_pb_iso->attrib_res[0]) >> 4;
            if (p_pb_iso->attrib_res_len > NFC_PB_ATTRIB_REQ_FIXED_BYTES)
            {
                p_pb_iso->hi_info_len    = p_pb_iso->attrib_res_len - NFC_PB_ATTRIB_REQ_FIXED_BYTES;
                if (p_pb_iso->hi_info_len > NFC_MAX_GEN_BYTES_LEN)
                    p_pb_iso->hi_info_len = NFC_MAX_GEN_BYTES_LEN;
                memcpy (p_pb_iso->hi_info, &p_pb_iso->attrib_res[NFC_PB_ATTRIB_REQ_FIXED_BYTES], p_pb_iso->hi_info_len);
            }
            break;

        case NCI_DISCOVERY_TYPE_LISTEN_B:
            /* ATTRIB CMD
            Byte 2~5 Byte 6  Byte 7  Byte 8  Byte 9  Byte 10 ~ 10+k-1
            NFCID0   Param 1 Param 2 Param 3 Param 4 Higher layer - INF
            */
            p_lb_iso                     = &p_intf->intf_param.lb_iso;
            p_lb_iso->attrib_req_len     = *p++;

            if (p_lb_iso->attrib_req_len == 0)
                break;

            if (p_lb_iso->attrib_req_len > NFC_MAX_ATTRIB_LEN)
                p_lb_iso->attrib_req_len = NFC_MAX_ATTRIB_LEN;
            STREAM_TO_ARRAY (p_lb_iso->attrib_req, p, p_lb_iso->attrib_req_len);
            memcpy (p_lb_iso->nfcid0, p_lb_iso->attrib_req, NFC_NFCID0_MAX_LEN);
            if (p_lb_iso->attrib_req_len > NFC_LB_ATTRIB_REQ_FIXED_BYTES)
            {
                p_lb_iso->hi_info_len    = p_lb_iso->attrib_req_len - NFC_LB_ATTRIB_REQ_FIXED_BYTES;
                if (p_lb_iso->hi_info_len > NFC_MAX_GEN_BYTES_LEN)
                    p_lb_iso->hi_info_len = NFC_MAX_GEN_BYTES_LEN;
                memcpy (p_lb_iso->hi_info, &p_lb_iso->attrib_req[NFC_LB_ATTRIB_REQ_FIXED_BYTES], p_lb_iso->hi_info_len);
            }
            break;
        }

    }
#if (NFC_RW_ONLY == FALSE)
    else if(evt_data.activate.intf_param.type == NCI_INTERFACE_NFC_DEP)
    {
#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
        if(nfc_cb.bBlockWiredMode)
        {
            nfc_ncif_resume_dwp_wired_mode();
        }
#endif
        /* Make max payload of NCI aligned to max payload of NFC-DEP for better performance */
        if (buff_size > NCI_NFC_DEP_MAX_DATA)
            buff_size = NCI_NFC_DEP_MAX_DATA;

        p_pa_nfc                  = &p_intf->intf_param.pa_nfc;
        p_pa_nfc->atr_res_len     = *p++;

        if (p_pa_nfc->atr_res_len > 0)
        {
            if (p_pa_nfc->atr_res_len > NFC_MAX_ATS_LEN)
                p_pa_nfc->atr_res_len = NFC_MAX_ATS_LEN;
            STREAM_TO_ARRAY (p_pa_nfc->atr_res, p, p_pa_nfc->atr_res_len);
            if (  (mode == NCI_DISCOVERY_TYPE_POLL_A)
                ||(mode == NCI_DISCOVERY_TYPE_POLL_F)
                ||(mode == NCI_DISCOVERY_TYPE_POLL_A_ACTIVE)
                ||(mode == NCI_DISCOVERY_TYPE_POLL_F_ACTIVE)  )
            {
                /* ATR_RES
                Byte 3~12 Byte 13 Byte 14 Byte 15 Byte 16 Byte 17 Byte 18~18+n
                NFCID3T   DIDT    BST     BRT     TO      PPT     [GT0 ... GTn] */
                mpl_idx                 = 14;
                gb_idx                  = NCI_P_GEN_BYTE_INDEX;
                p_pa_nfc->waiting_time  = p_pa_nfc->atr_res[NCI_L_NFC_DEP_TO_INDEX] & 0x0F;
            }
            else if (  (mode == NCI_DISCOVERY_TYPE_LISTEN_A)
                     ||(mode == NCI_DISCOVERY_TYPE_LISTEN_F)
                     ||(mode == NCI_DISCOVERY_TYPE_LISTEN_A_ACTIVE)
                     ||(mode == NCI_DISCOVERY_TYPE_LISTEN_F_ACTIVE)  )
            {
                /* ATR_REQ
                Byte 3~12 Byte 13 Byte 14 Byte 15 Byte 16 Byte 17~17+n
                NFCID3I   DIDI    BSI     BRI     PPI     [GI0 ... GIn] */
                mpl_idx = 13;
                gb_idx  = NCI_L_GEN_BYTE_INDEX;
            }

            mpl                         = ((p_pa_nfc->atr_res[mpl_idx]) >> 4) & 0x03;
            p_pa_nfc->max_payload_size  = nfc_mpl_code_to_size[mpl];
            if (p_pa_nfc->atr_res_len > gb_idx)
            {
                p_pa_nfc->gen_bytes_len = p_pa_nfc->atr_res_len - gb_idx;
                if (p_pa_nfc->gen_bytes_len > NFC_MAX_GEN_BYTES_LEN)
                    p_pa_nfc->gen_bytes_len = NFC_MAX_GEN_BYTES_LEN;
                memcpy (p_pa_nfc->gen_bytes, &p_pa_nfc->atr_res[gb_idx], p_pa_nfc->gen_bytes_len);
            }
        }
    }
#endif
    else if ((evt_data.activate.intf_param.type == NCI_INTERFACE_FRAME) && (evt_data.activate.protocol == NCI_PROTOCOL_T1T) )
    {
        p_pa = &evt_data.activate.rf_tech_param.param.pa;
        if ((len_act == NCI_T1T_HR_LEN) && (p_pa->hr_len == 0))
        {
            p_pa->hr_len    = NCI_T1T_HR_LEN;
            p_pa->hr[0]     = *p++;
            p_pa->hr[1]     = *p++;
        }
    }
#if(NXP_EXTNS == TRUE)
    /*
     * Code to handle the Reader over SWP.
     * 1. Do not activate tag for this NTF.
     * 2. Pass this info to JNI as START_READER_EVT.
     */
    else if (evt_data.activate.intf_param.type == NCI_INTERFACE_UICC_DIRECT || evt_data.activate.intf_param.type == NCI_INTERFACE_ESE_DIRECT)
    {
        NFC_TRACE_DEBUG1("nfc_ncif_proc_activate:interface type  %x", evt_data.activate.intf_param.type);
    }
#endif

    p_cb->act_protocol  = evt_data.activate.protocol;
#if(NXP_EXTNS == TRUE)
    p_cb->act_interface = evt_data.activate.intf_param.type;
    p_cb->sel_res = evt_data.activate.rf_tech_param.param.pa.sel_rsp;
#endif
    p_cb->buff_size     = buff_size;
    p_cb->num_buff      = num_buff;
    p_cb->init_credits  = num_buff;

    if (nfc_cb.p_discv_cback)
    {
        (*nfc_cb.p_discv_cback) (NFC_ACTIVATE_DEVT, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_deactivate
**
** Description      This function is called to process de-activate
**                  response and notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_deactivate (UINT8 status, UINT8 deact_type, BOOLEAN is_ntf)
{
    tNFC_DISCOVER   evt_data;
    tNFC_DEACTIVATE_DEVT    *p_deact;
    tNFC_CONN_CB * p_cb = &nfc_cb.conn_cb[NFC_RF_CONN_ID];
    void    *p_data;

    nfc_set_state (NFC_STATE_IDLE);
    p_deact             = &evt_data.deactivate;
    p_deact->status     = status;
    p_deact->type       = deact_type;
    p_deact->is_ntf     = is_ntf;

    while ((p_data = GKI_dequeue (&p_cb->rx_q)) != NULL)
    {
        GKI_freebuf (p_data);
    }

    while ((p_data = GKI_dequeue (&p_cb->tx_q)) != NULL)
    {
        GKI_freebuf (p_data);
    }

    if (p_cb->p_cback)
        (*p_cb->p_cback) (NFC_RF_CONN_ID, NFC_DEACTIVATE_CEVT, (tNFC_CONN *) p_deact);

#if (NXP_EXTNS == TRUE)
    if((nfc_cb.flags & (NFC_FL_DISCOVER_PENDING | NFC_FL_CONTROL_REQUESTED))
       && (deact_type == NFC_DEACTIVATE_TYPE_DISCOVERY) && (is_ntf == TRUE))
    {
        NFC_TRACE_DEBUG0 ("Abnormal State, Deactivate NTF is ignored, MW is already going to Discovery state");
        return;
    }
#endif

    if (nfc_cb.p_discv_cback)
    {
        (*nfc_cb.p_discv_cback) (NFC_DEACTIVATE_DEVT, &evt_data);
    }
}
/*******************************************************************************
**
** Function         nfc_ncif_proc_ee_action
**
** Description      This function is called to process NFCEE ACTION NTF
**
** Returns          void
**
*******************************************************************************/
#if ((NFC_NFCEE_INCLUDED == TRUE) && (NFC_RW_ONLY == FALSE))
void nfc_ncif_proc_ee_action (UINT8 *p, UINT16 plen)
{
    tNFC_EE_ACTION_REVT evt_data;
    tNFC_RESPONSE_CBACK *p_cback = nfc_cb.p_resp_cback;
    UINT8   data_len, ulen, tag, *p_data;
    UINT8   max_len;

    if (p_cback)
    {
        memset (&evt_data.act_data, 0, sizeof (tNFC_ACTION_DATA));
        evt_data.status             = NFC_STATUS_OK;
        evt_data.nfcee_id           = *p++;
        evt_data.act_data.trigger   = *p++;
        data_len                    = *p++;
        if (plen >= 3)
            plen -= 3;
        if (data_len > plen)
            data_len = (UINT8) plen;

        switch (evt_data.act_data.trigger)
        {
        case NCI_EE_TRIG_7816_SELECT:
            if (data_len > NFC_MAX_AID_LEN)
                data_len = NFC_MAX_AID_LEN;
            NFC_TRACE_DEBUG1 ("AID len = %d", data_len);
            evt_data.act_data.param.aid.len_aid = data_len;
            STREAM_TO_ARRAY (evt_data.act_data.param.aid.aid, p, data_len);
            break;
#if ((NXP_EXTNS == TRUE) && (NXP_NFCC_PROP_ACTN_NTF == TRUE))
        case NCI_EE_TRIG_RF_PROT_PROP_RSP_NTF:
        case NCI_EE_TRIG_RF_TECH_PROP_RSP_NTF:
            if (data_len > NFC_MAX_AID_LEN)
                data_len = NFC_MAX_AID_LEN;
            NFC_TRACE_DEBUG1 ("AID len = %d", data_len);
            evt_data.act_data.param.aid.len_aid = data_len;
            STREAM_TO_ARRAY (evt_data.act_data.param.aid.aid, p, data_len);
            plen = plen - data_len;
            if((plen != 0) && (*p++ == NCI_EE_TRIG_PROP_RSP_NTF))
            {
                evt_data.act_data.nfcee_act_ntf.len_data = *p++;
                STREAM_TO_ARRAY (&evt_data.act_data.nfcee_act_ntf.data, p, evt_data.act_data.nfcee_act_ntf.len_data);
            }
            break;
#endif
        case NCI_EE_TRIG_RF_PROTOCOL:
            evt_data.act_data.param.protocol    = *p++;
            break;
        case NCI_EE_TRIG_RF_TECHNOLOGY:
            evt_data.act_data.param.technology  = *p++;
            break;
        case NCI_EE_TRIG_APP_INIT:
            while (data_len > NFC_TL_SIZE)
            {
                data_len    -= NFC_TL_SIZE;
                tag         = *p++;
                ulen        = *p++;
                if (ulen > data_len)
                    ulen = data_len;
                p_data      = NULL;
                max_len     = ulen;
                switch (tag)
                {
                case NCI_EE_ACT_TAG_AID:    /* AID                 */
                    if (max_len > NFC_MAX_AID_LEN)
                        max_len = NFC_MAX_AID_LEN;
                    evt_data.act_data.param.app_init.len_aid = max_len;
                    p_data = evt_data.act_data.param.app_init.aid;
                    break;
                case NCI_EE_ACT_TAG_DATA:   /* hex data for app    */
                    if (max_len > NFC_MAX_APP_DATA_LEN)
                        max_len = NFC_MAX_APP_DATA_LEN;
                    evt_data.act_data.param.app_init.len_data   = max_len;
                    p_data                                      = evt_data.act_data.param.app_init.data;
                    break;
                }
                if (p_data)
                {
                    STREAM_TO_ARRAY (p_data, p, max_len);
                }
                data_len -= ulen;
            }
            break;
#if ((NXP_EXTNS == TRUE) && (NXP_NFCC_PROP_ACTN_NTF == TRUE))
         case NCI_EE_TRIG_PROP_RSP_NTF:
            NFC_TRACE_DEBUG1("NCI_EE_TRIG_PROP_APP Data len = %d", data_len);
            evt_data.act_data.nfcee_act_ntf.len_data = data_len;
            STREAM_TO_ARRAY (&evt_data.act_data.nfcee_act_ntf.data, p, data_len);
            break;
#endif
        }

#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
        nfc_stop_timer(&nfc_cb.rf_filed_event_timeout_timer);
        tNFC_CONN_CB *p_cb;
        p_cb = nfc_find_conn_cb_by_conn_id(nfa_hci_cb.conn_id);
        if(evt_data.nfcee_id != 0xC0)
        {
            nfc_cb.bBlockWiredMode = TRUE;
            nfc_cb.bBlkPwrlinkAndModeSetCmd  = TRUE;
            if(!nfc_cb.bIsDwpResPending && nfa_hci_cb.assembling)
            {
                /*To faster xceive timeout when UICC-CE is activated during chained rsp rx*/
                NFC_TRACE_DEBUG0("restarted hci timer during chained response");
                nfa_hci_cb.IsWiredSessionAborted = TRUE;
                nfa_sys_stop_timer(&nfa_hci_cb.timer);
                if(p_cb && p_cb->p_cback)
                    (*p_cb->p_cback)(nfa_hci_cb.conn_id, NFC_HCI_RESTART_TIMER, (tNFC_CONN*)&evt_data);
            }
        }
        else
        {
            if(evt_data.act_data.trigger == NCI_EE_TRIG_RF_TECHNOLOGY)
            {
                nfc_cb.bBlockWiredMode = TRUE;
            }
            else if(nfc_cb.bBlockWiredMode)
            {
                nfc_cb.bCeActivatedeSE = TRUE;
                nfc_ncif_resume_dwp_wired_mode();
            }
        }
#endif
        (*p_cback) (NFC_EE_ACTION_REVT, (tNFC_RESPONSE *) &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_ee_discover_req
**
** Description      This function is called to process NFCEE DISCOVER REQ NTF
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_ee_discover_req (UINT8 *p, UINT16 plen)
{
    tNFC_RESPONSE_CBACK *p_cback = nfc_cb.p_resp_cback;
    tNFC_EE_DISCOVER_REQ_REVT   ee_disc_req;
    tNFC_EE_DISCOVER_INFO       *p_info;
    UINT8                       u8;

    NFC_TRACE_DEBUG2 ("nfc_ncif_proc_ee_discover_req %d len:%d", *p, plen);
    if (p_cback)
    {
        u8  = *p;
        ee_disc_req.status      = NFC_STATUS_OK;
        ee_disc_req.num_info    = *p++;
        p_info                  = ee_disc_req.info;
        if (plen)
            plen--;
        while ((u8 > 0) && (plen >= NFC_EE_DISCOVER_ENTRY_LEN))
        {
            p_info->op  = *p++;                  /* T */
            if (*p != NFC_EE_DISCOVER_INFO_LEN)/* L */
            {
                NFC_TRACE_DEBUG1 ("bad entry len:%d", *p );
                return;
            }
            p++;
            /* V */
            p_info->nfcee_id    = *p++;
            p_info->tech_n_mode = *p++;
            p_info->protocol    = *p++;
            u8--;
            plen    -=NFC_EE_DISCOVER_ENTRY_LEN;
            p_info++;
        }
        (*p_cback) (NFC_EE_DISCOVER_REQ_REVT, (void *) &ee_disc_req);
    }

}

/*******************************************************************************
**
** Function         nfc_ncif_proc_get_routing
**
** Description      This function is called to process get routing notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_get_routing (UINT8 *p, UINT8 len)
{
    tNFC_GET_ROUTING_REVT evt_data;
    UINT8       more, num_entries, xx, yy, *pn, tl;
    tNFC_STATUS status = NFC_STATUS_CONTINUE;
#if(NXP_EXTNS == TRUE)
    (void)(p);
    (void)(len);
#endif

#if(NXP_EXTNS == FALSE)
    if (nfc_cb.p_resp_cback)
    {
        more        = *p++;
        num_entries = *p++;
        for (xx = 0; xx < num_entries; xx++)
        {
            if ((more == FALSE) && (xx == (num_entries - 1)))
                status = NFC_STATUS_OK;
            evt_data.status         = (tNFC_STATUS) status;
            evt_data.nfcee_id       = *p++;
            evt_data.num_tlvs       = *p++;
            evt_data.tlv_size       = 0;
            pn                      = evt_data.param_tlvs;
            for (yy = 0; yy < evt_data.num_tlvs; yy++)
            {
                tl                  = *(p+1);
                tl                 += NFC_TL_SIZE;
                STREAM_TO_ARRAY (pn, p, tl);
                evt_data.tlv_size  += tl;
                pn                 += tl;
            }
            (*nfc_cb.p_resp_cback) (NFC_GET_ROUTING_REVT, (tNFC_RESPONSE *) &evt_data);
        }
    }
    if (nfc_cb.p_resp_cback)
    {
        more        = *p;
        if(more == FALSE)
        {
            status = NFC_STATUS_OK;
        }
        evt_data.status         = (tNFC_STATUS) status;
        evt_data.num_tlvs       = *(p+1);
        evt_data.tlv_size       = len;
        memcpy(evt_data.param_tlvs,p,len);
        (*nfc_cb.p_resp_cback) (NFC_GET_ROUTING_REVT, (tNFC_RESPONSE *) &evt_data);
    }
#endif
}
#endif

/*******************************************************************************
**
** Function         nfc_ncif_proc_conn_create_rsp
**
** Description      This function is called to process connection create
**                  response
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_conn_create_rsp (UINT8 *p, UINT16 plen, UINT8 dest_type)
{
    tNFC_CONN_CB * p_cb;
    tNFC_STATUS    status;
    tNFC_CONN_CBACK *p_cback;
    tNFC_CONN   evt_data;
    UINT8           conn_id;
    (void)plen;

    /* find the pending connection control block */
    p_cb                = nfc_find_conn_cb_by_conn_id (NFC_PEND_CONN_ID);
    if (p_cb)
    {
        p                                  += NCI_MSG_HDR_SIZE;
        status                              = *p++;
        p_cb->buff_size                     = *p++;
        p_cb->num_buff = p_cb->init_credits = *p++;
        conn_id                             = *p++;
        evt_data.conn_create.status         = status;
        evt_data.conn_create.dest_type      = dest_type;
        evt_data.conn_create.id             = p_cb->id;
        evt_data.conn_create.buff_size      = p_cb->buff_size;
        evt_data.conn_create.num_buffs      = p_cb->num_buff;
        p_cback = p_cb->p_cback;
        if (status == NCI_STATUS_OK)
        {
            nfc_set_conn_id (p_cb, conn_id);
#if(NXP_EXTNS == TRUE)
                if(p_cb->id == 1)
                {
                    nfcc_dh_conn_id = conn_id;
                }
#endif
        }
        else
        {
            nfc_free_conn_cb (p_cb);
        }


        if (p_cback)
            (*p_cback) (conn_id, NFC_CONN_CREATE_CEVT, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_report_conn_close_evt
**
** Description      This function is called to report connection close event
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_report_conn_close_evt (UINT8 conn_id, tNFC_STATUS status)
{
    tNFC_CONN       evt_data;
    tNFC_CONN_CBACK *p_cback;
    tNFC_CONN_CB    *p_cb;

    p_cb = nfc_find_conn_cb_by_conn_id (conn_id);
    if (p_cb)
    {
        p_cback         = p_cb->p_cback;
        nfc_free_conn_cb (p_cb);
        evt_data.status = status;
        if (p_cback)
            (*p_cback) (conn_id, NFC_CONN_CLOSE_CEVT, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_reset_rsp
**
** Description      This function is called to process reset response/notification
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_reset_rsp (UINT8 *p, BOOLEAN is_ntf)
{
    UINT8 *temp = p, len;
    UINT8 status = *p++;
    BOOLEAN wait_for_ntf = FALSE;

    if (is_ntf)
    {
#if(NXP_EXTNS == TRUE)
#if(NXP_NFCC_FORCE_NCI1_0_INIT == TRUE)
        if(status == 0x02)
        {
            NFC_TRACE_DEBUG2 ("CORE_RESET_NTF 2 reason Command Received status nfc_state : 0x%x : 0x%x", status ,nfc_cb.nfc_state);
            wait_for_ntf = FALSE;
            status = NCI_STATUS_OK;
        }
        else if(status == 0x00)
        {
            NFC_TRACE_DEBUG2 ("CORE_RESET_NTF 2 reason Unrecoverable Error status nfc_state : 0x%x : 0x%x", status ,nfc_cb.nfc_state);
            core_reset_init_num_buff = TRUE;
            nfc_ncif_cmd_timeout();
        }
        else
        {
            NFC_TRACE_DEBUG1 ("CORE_RESET_NTF 1 nfc_state :0x%x ", nfc_cb.nfc_state);
            NFC_TRACE_DEBUG1 ("CORE_RESET_NTF 1 status :0x%x ", status);
            core_reset_init_num_buff = TRUE;
            nfc_ncif_cmd_timeout();
        }
    #else
        NFC_TRACE_DEBUG1 ("reset notification nfc_state :0x%x ", nfc_cb.nfc_state);
        NFC_TRACE_DEBUG1 ("reset notification!!:0x%x ", status);
        core_reset_init_num_buff = TRUE;
        nfc_ncif_cmd_timeout();
#endif
    }
    else
    {
        #if(NXP_NFCC_FORCE_NCI1_0_INIT == TRUE)
            NFC_TRACE_DEBUG1 ("CORE_RESET_RSP 2 nfc_state :0x%x ", nfc_cb.nfc_state);
            len = *(--temp);     //extract the no of params in reset response
            if (nfc_cb.flags & (NFC_FL_RESTARTING|NFC_FL_POWER_CYCLE_NFCC))
            {
                nfc_reset_all_conn_cbs ();
            }
            if(len == 0x01) //If response length is 01 means NCI2.0
            {
                wait_for_ntf = TRUE;
            }
        #else
            NFC_TRACE_DEBUG1 ("reset response nfc_state :0x%x ", nfc_cb.nfc_state);
            if (nfc_cb.flags & (NFC_FL_RESTARTING|NFC_FL_POWER_CYCLE_NFCC))
            {
                nfc_reset_all_conn_cbs ();
            }
#if 0
            /*Check NCI version only in case of reset rsp*/
            if (!is_ntf && status == NCI_STATUS_OK)
            {
                if ((*p) != NCI_VERSION)
                {
                    NFC_TRACE_ERROR2 ("NCI version mismatch!!:0x%02x != 0x%02x ", NCI_VERSION, *p);
                    if ((*p) < NCI_VERSION_0_F)
                    {
                        NFC_TRACE_ERROR0 ("NFCC version is too old");
                        status = NCI_STATUS_FAILED;
                    }
                }
            }
#endif
        #endif
    }
#else
        NFC_TRACE_ERROR1 ("reset notification!!:0x%x ", status);
        /* clean up, if the state is OPEN
         * FW does not report reset ntf right now */
        if (nfc_cb.nfc_state == NFC_STATE_OPEN)
        {
            /*if any conn_cb is connected, close it.
              if any pending outgoing packets are dropped.*/
            nfc_reset_all_conn_cbs ();
        }
        status = NCI_STATUS_OK;
    }

    if (nfc_cb.flags & (NFC_FL_RESTARTING|NFC_FL_POWER_CYCLE_NFCC))
    {
        nfc_reset_all_conn_cbs ();
    }

    if (status == NCI_STATUS_OK) //Status of CORE_RESET_RESPONSE if not notification
    {
        if ((*p) != NCI_VERSION)
        {
            NFC_TRACE_ERROR2 ("NCI version mismatch!!:0x%02x != 0x%02x ", NCI_VERSION, *p);
            if ((*p) < NCI_VERSION_0_F)
            {
                NFC_TRACE_ERROR0 ("NFCC version is too old");
                status = NCI_STATUS_FAILED;
            }
        }
    }
#endif

    if (status == NCI_STATUS_OK)
    {
#if(NXP_EXTNS == TRUE)
#if(NXP_NFCC_FORCE_NCI1_0_INIT == TRUE)
        if(!wait_for_ntf)
        {
            NFC_TRACE_DEBUG0 ("Got CORE_RESET_NTF 2 sending CORE_INIT_CMD 1");
            nci_snd_core_init ();
         }
         else
         {
            NFC_TRACE_DEBUG0 ("Waiting for CORE_RESET_NTF 2 reason CMD received");
            /* start NFC command-timeout timer */
            nfc_start_timer (&nfc_cb.nci_wait_rsp_timer, (UINT16)(NFC_TTYPE_NCI_WAIT_RSP), nfc_cb.nci_wait_rsp_tout);
         }
#endif
        NFC_TRACE_ERROR0 ("reset notification sending core init");
#endif
        nci_snd_core_init ();
    }
    else
    {
        NFC_TRACE_ERROR0 ("Failed to reset NFCC");
        nfc_enabled (status, NULL);
    }
 }

#if(NXP_EXTNS == TRUE)
UINT8 nfc_hal_nfcc_reset(void)
{
    nfc_nci_ExtnCmd_t extn_cmd;
    /*NCI_RESET_CMD*/
    UINT8 cmd_reset_nci[] = {0x20,0x00,0x01,0x00};
    UINT8 core_status = NCI_STATUS_FAILED;
    UINT8 retry_count = 0;
    NFC_TRACE_DEBUG0("Inside nfc_hal_nfcc_reset");

    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_reset_nci);
    extn_cmd.p_cmd = cmd_reset_nci;
    do {
        core_status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while(NCI_STATUS_OK != core_status && retry_count < (NFC_NFCC_INIT_MAX_RETRY+1));
    return core_status;
}
UINT8 nfc_hal_nfcc_init(UINT8 **pinit_rsp)
{
    nfc_nci_ExtnCmd_t extn_cmd;
    /*NCI_INIT_CMD*/
    UINT8 cmd_init_nci[] = {0x20,0x01,0x00};
    UINT8 init_status = NCI_STATUS_FAILED;
    UINT8 retry_count = 0;
    NFC_TRACE_DEBUG0("Inside nfc_hal_nfcc_init");
    if (pinit_rsp == NULL)
        return init_status;
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_init_nci);
    extn_cmd.p_cmd = cmd_init_nci;
    do {
        init_status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while(NCI_STATUS_OK != init_status && retry_count < (NFC_NFCC_INIT_MAX_RETRY+1));
    if(init_status == NCI_STATUS_OK && extn_cmd.rsp_len > 0)
    {
        *pinit_rsp = GKI_getbuf(extn_cmd.rsp_len);
        if(NULL != *pinit_rsp)
            memcpy(*pinit_rsp, extn_cmd.p_cmd_rsp, extn_cmd.rsp_len);
        else
        {
            init_status = NCI_STATUS_FAILED;
            NFC_TRACE_DEBUG0("nfc_hal_nfcc_init: Memory alocation failed");
        }
    }
    return init_status;
}
#endif
/*******************************************************************************
**
** Function         nfc_ncif_proc_init_rsp
**
** Description      This function is called to process init response
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_init_rsp (BT_HDR *p_msg)
{
    NFC_TRACE_DEBUG0("nfc_ncif_proc_init_rsp");
    UINT8 *p, status;
    tNFC_CONN_CB * p_cb = &nfc_cb.conn_cb[NFC_RF_CONN_ID];
#if (NXP_EXTNS == TRUE)
    static UINT8 retry_cnt = 0;
    UINT16 fw_status,
          fw_dwnld_status = NCI_STATUS_FAILED,
          fw_mw_ver_status = NCI_STATUS_FAILED;
    tNFC_FWUpdate_Info_t fw_update_inf;
    UINT8 *init_rsp = NULL ;
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
    UINT8 config_resp[16];
#endif
    int i;
    memset(&fw_update_inf, 0x00, sizeof(tNFC_FWUpdate_Info_t));
#endif
    p = (UINT8 *) (p_msg + 1) + p_msg->offset;
    /* handle init params in nfc_enabled */
    status   = *(p + NCI_MSG_HDR_SIZE);
#if(NXP_EXTNS == FALSE)
    if(NCI_STATUS_OK == status)
    {
#if(NXP_NFCC_MW_RCVRY_BLK_FW_DNLD == TRUE)
        //MW_FW recovery disabling at time of sig/crash/die
        NFC_TRACE_DEBUG1 ("MW_RCVRY_FW_DNLD_ALLOWED  -> %d", MW_RCVRY_FW_DNLD_ALLOWED);
        if(NFC_STATE_CORE_INIT == nfc_cb.nfc_state && MW_RCVRY_FW_DNLD_ALLOWED == FALSE)
#else
        if(NFC_STATE_CORE_INIT == nfc_cb.nfc_state)
#endif
        {
            nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_CHECK_FLASH_REQ, &fw_update_inf);
            NFC_TRACE_DEBUG1 ("fw_update required  -> %d", fw_update_inf.fw_update_reqd);

            if(fw_update_inf.fw_update_reqd == TRUE)
            {
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
                uicc_eeprom_get_config(config_resp);
#endif
                nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_FW_DWNLD, &fw_dwnld_status);
                status = nfc_hal_nfcc_reset();
                if(NCI_STATUS_OK == status)
                {
                    status = nfc_hal_nfcc_init(&init_rsp);
                    if(NCI_STATUS_OK == status)
                    {
                        /* Initialize BT_HDR */
                        p_msg->len    =  init_rsp[2]+3;
                        /* copying init rsp to p_msg  */
                        memcpy ((UINT8 *)(p_msg + 1) + p_msg->offset, init_rsp, p_msg->len);
                    }
                    if(!init_rsp)
                        GKI_freebuf(init_rsp);
                }
            }
        }
    }

    fw_status = nfc_ncif_store_FWVersion(p);
#if((NFC_NXP_CHIP_TYPE != PN547C2) && (NFC_NXP_AID_MAX_SIZE_DYN == TRUE))
    nfc_ncif_set_MaxRoutingTableSize(p);
#endif
    nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_FW_MW_VER_CHECK, &fw_mw_ver_status);
#endif

    /* TODO To be removed after 553 bringup */
    fw_mw_ver_status = NCI_STATUS_OK;
    if (status == NCI_STATUS_OK
#if(NXP_EXTNS == FALSE)
            &&
            fw_status == NCI_STATUS_OK
            &&
            NCI_STATUS_OK == fw_mw_ver_status
#endif
       )
    {
#if (NXP_EXTNS == TRUE)
        if(fw_dwnld_status == NCI_STATUS_OK)
        {
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
                uicc_eeprom_set_config(config_resp);
#endif
        }
#endif
        p_cb->id            = NFC_RF_CONN_ID;
        p_cb->act_protocol  = NCI_PROTOCOL_UNKNOWN;
#if(NXP_EXTNS == FALSE)
        retry_cnt = 0;
#endif
        nfc_set_state (NFC_STATE_W4_POST_INIT_CPLT);

        nfc_cb.p_nci_init_rsp = p_msg;
        nfc_cb.p_hal->core_initialized (p);
    }

#if(NXP_EXTNS == FALSE)
    /*Recovery is added in case of corrupted init rsp is received
     * eg: 400119 - only 3 bytes are received. In this case since the
     * rsp buffer[3] is status and the value is 0x00 because of memset for the rsp buffer
     * was done before copying the init response.
     * Hence FW version check is added to do check the proper status and go for a recovery*/
    else if(retry_cnt++ < NFC_NFCC_INIT_MAX_RETRY &&
            fw_status != NCI_STATUS_OK &&
            NCI_STATUS_OK == fw_mw_ver_status)
    {
        NFC_TRACE_DEBUG0("GKI_FREEBUF1");
        GKI_send_event (NFC_TASK, NFC_TASK_EVT_TRANSPORT_READY);
        GKI_freebuf (p_msg);
    }
#endif
    else
    {
#if(NXP_EXTNS == FALSE)
        status = NCI_STATUS_FAILED;
        retry_cnt = 0;
#endif
        NFC_TRACE_DEBUG0("GKI_FREEBUF2");
        nfc_enabled (status, NULL);
        GKI_freebuf (p_msg);
    }
}

#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function         nfc_ncif_store_FWVersion
**
** Description      This function is called to fill the structure with FW Version
**
** Returns          tNFC_STATUS
**
*******************************************************************************/
tNFC_STATUS nfc_ncif_store_FWVersion(UINT8 * p_buf)
{
    UINT8 status = NFC_STATUS_OK;
    int len = p_buf[2] + 2; /*include 2 byte header*/
    memset (&nfc_fw_version, 0, sizeof(nfc_fw_version));
    nfc_fw_version.rom_code_version = p_buf[len-2];
    nfc_fw_version.major_version = p_buf[len-1];
    nfc_fw_version.minor_version = p_buf[len];
    if(nfc_fw_version.rom_code_version == 0 ||
       nfc_fw_version.major_version == 0)
    {
        status = NFC_STATUS_FAILED;
    }
    NFC_TRACE_DEBUG3("FW Version: %x.%x.%x", nfc_fw_version.rom_code_version,
                      nfc_fw_version.major_version,nfc_fw_version.minor_version);
    return status;
}
#if((NFC_NXP_CHIP_TYPE != PN547C2) && (NFC_NXP_AID_MAX_SIZE_DYN == TRUE))
/*******************************************************************************
**
** Function         nfc_ncif_set_MaxRoutingTableSize
**
** Description      This function is called to get the Max Routing Table Size
**
** Returns          tNFC_STATUS
**
*******************************************************************************/
tNFC_STATUS nfc_ncif_set_MaxRoutingTableSize(UINT8 * p_buf)
{
    UINT8 status = NFC_STATUS_OK;
    int len = p_buf[2] + 2; /*include 2 byte header*/
    maxRoutingTableSize = p_buf[len-8];
    maxRoutingTableSize = maxRoutingTableSize << 8 | p_buf[len-9];
    NFC_TRACE_DEBUG1("MAX Routing size : %d",maxRoutingTableSize);
    if(maxRoutingTableSize == 0)
    {
        status = NFC_STATUS_FAILED;
    }
    return status;
}

/*******************************************************************************
**
** Function         NFC_getMaxRoutingTableSize
**
** Description      This function is called to get the Max routing Table size
**
** Returns          UINT16
**
*******************************************************************************/
UINT16 nfc_ncif_getMaxRoutingTableSize()
{
    return maxRoutingTableSize;
}
#endif
/*******************************************************************************
**
** Function         nfc_ncif_getFWVersion
**
** Description      This function is called to fet the FW Version
**
** Returns          tNFC_FW_VERSION
**
*******************************************************************************/
tNFC_FW_VERSION nfc_ncif_getFWVersion()
{
  return nfc_fw_version;
}

/*******************************************************************************
**
** Function         nfc_ncif_storeScreenState
**
** Description      This function is called to store the last screen state
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_storeScreenState(UINT8 state)
{
    /* store the screen state */
    gScreenState = state;
    NFC_TRACE_DEBUG1 ("nfc_ncif_storeScreenState gScreenState : %d", gScreenState);
}
#endif
/*******************************************************************************
**
** Function         nfc_ncif_proc_get_config_rsp
**
** Description      This function is called to process get config response
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_get_config_rsp (BT_HDR *p_evt)
{
    UINT8   *p;
    tNFC_RESPONSE_CBACK *p_cback = nfc_cb.p_resp_cback;
    tNFC_RESPONSE  evt_data;

    p_evt->offset += NCI_MSG_HDR_SIZE;
    p_evt->len    -= NCI_MSG_HDR_SIZE;
    if (p_cback)
    {
        p                                = (UINT8 *) (p_evt + 1) + p_evt->offset;
        evt_data.get_config.status       = *p++;
        evt_data.get_config.tlv_size     = p_evt->len;
        evt_data.get_config.p_param_tlvs = p;
        (*p_cback) (NFC_GET_CONFIG_REVT, &evt_data);
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_t3t_polling_ntf
**
** Description      Handle NCI_MSG_RF_T3T_POLLING NTF
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_t3t_polling_ntf (UINT8 *p, UINT16 plen)
{
    UINT8 status;
    UINT8 num_responses;

    /* Pass result to RW_T3T for processing */
    STREAM_TO_UINT8 (status, p);
    STREAM_TO_UINT8 (num_responses, p);
    plen-=NFC_TL_SIZE;
    rw_t3t_handle_nci_poll_ntf (status, num_responses, (UINT8) plen, p);
}

/*******************************************************************************
**
** Function         nfc_data_event
**
** Description      Report Data event on the given connection control block
**
** Returns          void
**
*******************************************************************************/
void nfc_data_event (tNFC_CONN_CB * p_cb)
{
    BT_HDR      *p_evt;
    tNFC_DATA_CEVT data_cevt;
    UINT8       *p;

    if (p_cb->p_cback)
    {
        while ((p_evt = (BT_HDR *)GKI_getfirst (&p_cb->rx_q)) != NULL)
        {
            if (p_evt->layer_specific & NFC_RAS_FRAGMENTED)
            {
                /* Not the last fragment */
                if (!(p_evt->layer_specific & NFC_RAS_TOO_BIG))
                {
                    /* buffer can hold more */
                    if (  (p_cb->conn_id != NFC_RF_CONN_ID)
                        ||(nfc_cb.reassembly)  )
                    {
                        /* If not rf connection or If rf connection and reassembly requested,
                         * try to Reassemble next packet */
                        break;
                    }
                }
            }

            p_evt = (BT_HDR *) GKI_dequeue (&p_cb->rx_q);

            if(p_evt == NULL)
            {
                break;
            }
            /* report data event */
            p_evt->offset   += NCI_MSG_HDR_SIZE;
            p_evt->len      -= NCI_MSG_HDR_SIZE;

            if (p_evt->layer_specific)
                data_cevt.status = NFC_STATUS_CONTINUE;
            else
            {
                nfc_cb.reassembly = TRUE;
                data_cevt.status = NFC_STATUS_OK;
            }

            data_cevt.p_data = p_evt;
            /* adjust payload, if needed */
            if (p_cb->conn_id == NFC_RF_CONN_ID)
            {
                /* if NCI_PROTOCOL_T1T/NCI_PROTOCOL_T2T/NCI_PROTOCOL_T3T, the status byte needs to be removed
                 */
                if ((p_cb->act_protocol >= NCI_PROTOCOL_T1T) && (p_cb->act_protocol <= NCI_PROTOCOL_T3T))
                {
#if(NXP_EXTNS == TRUE)
                    if( (p_cb->act_protocol == NCI_PROTOCOL_T2T) && ( p_cb->act_interface == NCI_INTERFACE_FIRST_VS) && ( p_cb->sel_res == 0x01) )
                    {
                        NFC_TRACE_ERROR0 ("Skylander tag detected dont handle this");
                    }else
#endif
                    {
                        p_evt->len--;
                        p                = (UINT8 *) (p_evt + 1);
                        data_cevt.status = *(p + p_evt->offset + p_evt->len);
                    }
                }
            }
            (*p_cb->p_cback) (p_cb->conn_id, NFC_DATA_CEVT, (void *) &data_cevt);
            p_evt = NULL;
        }
    }
}

/*******************************************************************************
**
** Function         nfc_ncif_proc_data
**
** Description      Find the connection control block associated with the data
**                  packet. Assemble the data packet, if needed.
**                  Report the Data event.
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_proc_data (BT_HDR *p_msg)
{
    UINT8   *pp, cid;
    tNFC_CONN_CB * p_cb;
    UINT8   pbf;
    BT_HDR  *p_last;
    UINT8   *ps, *pd;
    UINT16  size;
    BT_HDR  *p_max = NULL;
    UINT16  len;

    pp   = (UINT8 *) (p_msg+1) + p_msg->offset;
    NFC_TRACE_DEBUG3 ("nfc_ncif_proc_data 0x%02x%02x%02x", pp[0], pp[1], pp[2]);
    NCI_DATA_PRS_HDR (pp, pbf, cid, len);
    p_cb = nfc_find_conn_cb_by_conn_id (cid);
#if((NXP_EXTNS == TRUE) && (NXP_ESE_DUAL_MODE_PRIO_SCHEME == NXP_ESE_WIRED_MODE_RESUME))
    if((p_cb) && (cid == NFC_RF_CONN_ID) && (nfc_cb.bBlockWiredMode))
    {
        nfc_ncif_resume_dwp_wired_mode();
    }
#endif
    if (p_cb && (p_msg->len >= NCI_DATA_HDR_SIZE))
    {
        NFC_TRACE_DEBUG1 ("nfc_ncif_proc_data len:%d", len);

        p_msg->layer_specific       = 0;
        if (pbf)
        {
#if(NXP_EXTNS == TRUE)
            NFC_TRACE_DEBUG0 ("nfc_ncif_proc_data requesting reassembly for chained data");
            nfc_cb.reassembly = TRUE;
#endif
            p_msg->layer_specific   = NFC_RAS_FRAGMENTED;
        }
        p_last = (BT_HDR *)GKI_getlast (&p_cb->rx_q);
        if (p_last && (p_last->layer_specific & NFC_RAS_FRAGMENTED))
        {
            /* last data buffer is not last fragment, append this new packet to the last */
            size = GKI_get_buf_size(p_last);
            if (size < (BT_HDR_SIZE + p_last->len + p_last->offset + len))
            {
                /* the current size of p_last is not big enough to hold the new fragment, p_msg */
                if (size != GKI_MAX_BUF_SIZE)
                {
                    /* try the biggest GKI pool */
                    p_max = (BT_HDR *)GKI_getpoolbuf (GKI_MAX_BUF_SIZE_POOL_ID);
                    if (p_max)
                    {
                        /* copy the content of last buffer to the new buffer */
                        memcpy(p_max, p_last, BT_HDR_SIZE);
                        pd  = (UINT8 *)(p_max + 1) + p_max->offset;
                        ps  = (UINT8 *)(p_last + 1) + p_last->offset;
                        memcpy(pd, ps, p_last->len);

                        /* place the new buffer in the queue instead */
                        GKI_remove_from_queue (&p_cb->rx_q, p_last);
                        GKI_freebuf (p_last);
                        GKI_enqueue (&p_cb->rx_q, p_max);
                        p_last  = p_max;
                    }
                }
                if (p_max == NULL)
                {
                    /* Biggest GKI Pool not available (or)
                     * Biggest available GKI Pool is not big enough to hold the new fragment, p_msg */
                    p_last->layer_specific  |= NFC_RAS_TOO_BIG;
                }
            }

            ps   = (UINT8 *)(p_msg + 1) + p_msg->offset + NCI_MSG_HDR_SIZE;
            len  = p_msg->len - NCI_MSG_HDR_SIZE;

            if (!(p_last->layer_specific & NFC_RAS_TOO_BIG))
            {
                pd   = (UINT8 *)(p_last + 1) + p_last->offset + p_last->len;
                memcpy(pd, ps, len);
                p_last->len  += len;
                /* do not need to update pbf and len in NCI header.
                 * They are stripped off at NFC_DATA_CEVT and len may exceed 255 */
                NFC_TRACE_DEBUG1 ("nfc_ncif_proc_data len:%d", p_last->len);
                p_last->layer_specific  = p_msg->layer_specific;
                GKI_freebuf (p_msg);
#ifdef DISP_NCI
                if (!(p_last->layer_specific & NFC_RAS_FRAGMENTED))
                {
                    /* this packet was reassembled. display the complete packet */
                    DISP_NCI ((UINT8 *)(p_last + 1) + p_last->offset, p_last->len, TRUE);
                }
#endif
                nfc_data_event (p_cb);
            }
            else
            {
                /* Not enough memory to add new buffer
                 * Send data already in queue first with status Continue */
                nfc_data_event (p_cb);
                /* now enqueue the new buffer to the rx queue */
                GKI_enqueue (&p_cb->rx_q, p_msg);
            }
        }
        else
        {
            /* if this is the first fragment on RF link */
            if (  (p_msg->layer_specific & NFC_RAS_FRAGMENTED)
                &&(p_cb->conn_id == NFC_RF_CONN_ID)
                &&(p_cb->p_cback)  )
            {
                /* Indicate upper layer that local device started receiving data */
                (*p_cb->p_cback) (p_cb->conn_id, NFC_DATA_START_CEVT, NULL);
            }
            /* enqueue the new buffer to the rx queue */
            GKI_enqueue (&p_cb->rx_q, p_msg);
            nfc_data_event (p_cb);
        }
        return;
    }
    GKI_freebuf (p_msg);
}
#if(NXP_EXTNS == TRUE)
/*******************************************************************************
**
** Function         nfc_ncif_credit_ntf_timeout
**
** Description      Handle a credit ntf  timeout
**
** Returns          void
**
*******************************************************************************/
void nfc_ncif_credit_ntf_timeout()
{
    NFC_TRACE_DEBUG0 ("nfc_ncif_credit_ntf_timeout : Enter");

#if(NXP_EXTNS == TRUE)
    tNFC_CONN_CB *p_cb;
#endif
    if( get_i2c_fragmentation_enabled () == I2C_FRAGMENATATION_ENABLED)
    {
        nfc_cb.i2c_data_t.data_ntf_timeout = 1;
    }
    else
    {
        //No need to wait for response since credit ntf timeout calls recovery
        //Stop the response timer
        nfa_sys_stop_timer (&nfa_hci_cb.timer);

        //send timeout event to upper layer
        nfa_hci_rsp_timeout (NULL);

        //will be used to reset credit buffer after recovery
        core_reset_init_num_buff = TRUE;
#if(NXP_EXTNS == TRUE)
        p_cb = nfc_find_conn_cb_by_conn_id(NFC_NFCEE_CONN_ID);
        if(p_cb && (p_cb->num_buff != NFC_CONN_NO_FC) && (p_cb->num_buff == 0))
            p_cb->num_buff++;

        p_cb = nfc_find_conn_cb_by_conn_id(NFC_RF_CONN_ID);
        if(p_cb && (p_cb->num_buff != NFC_CONN_NO_FC) && (p_cb->num_buff == 0))
            p_cb->num_buff++;
#endif
    }

    if( get_i2c_fragmentation_enabled () == I2C_FRAGMENATATION_ENABLED)
    {
        //nfc_cb.nfc_state = NFC_STATE_RECOVERY;
        NFC_TRACE_DEBUG0 ("nfc_ncif_credit_ntf_timeout :decrementing window");
        //TODO: Write logic for VEN_RESET.
        nfc_cb.p_hal->power_cycle();
        //Remove the pending cmds from the cmd queue. send any pending rsp/cback to jni
        nfc_ncif_empty_cmd_queue();
        //Cancel any ongoing data transfer.
        /**
         * send core reset - keep config
         * send core init
         * send discovery
         * */

        //Update the cmd window, since rsp has not came.
        //nfc_ncif_update_window ();
        nfc_ncif_empty_data_queue ();
        if(nfc_cb.i2c_data_t.data_ntf_timeout)
        {
            nfc_cb.i2c_data_t.data_ntf_timeout = 0;
        }
    }
    NFC_TRACE_ERROR0 ("cmd timeout sending core reset!!!");
     nfc_ncif_cmd_timeout();
    //nci_snd_core_reset(0x00);
}

/*******************************************************************************
**
** Function         nfc_ncif_process_proprietary_rsp
**
** Description      Process the response to avoid collision
**                  while nxpCbflag is set
**
** Returns          TRUE if proprietary response else FALSE
**
*******************************************************************************/

BOOLEAN nfc_ncif_proc_proprietary_rsp (UINT8 mt, UINT8 gid, UINT8 oid)
{
    BOOLEAN stat = FALSE;
    NFC_TRACE_DEBUG3 ("nfc_ncif_proc_proprietary_rsp: mt=%u, gid=%u, oid=%u", mt, gid, oid);
    switch(mt)
    {
    case NCI_MT_DATA:
        switch (gid)
        {
        case 0x03:
            switch (oid)
            {
            case 0x00:      /*Data Response*/
                stat = FALSE;
                break;

            default:
                stat = TRUE;
                break;
            }
            break;

        default:
            stat = TRUE;
            break;
        }
        break;

    case NCI_MT_NTF:
        switch (gid)
        {
        case NCI_GID_CORE:
            switch (oid)
            {
            case 0x00:      /*CORE_RESET_NTF*/
            case 0x06:      /*CORE_CONN_CREDITS_NTF*/
                stat = FALSE;
                break;

            default:
                stat = TRUE;
                break;
            }
            break;
        case NCI_GID_RF_MANAGE:
            switch (oid)
            {
            case 0x06:      /*CORE_CONN_CREDITS_NTF*/
                stat = FALSE;
                break;
            case 0x09:
                stat = FALSE;/*NFA_EE_ACTION_NTF*/
                break;
            default:
                stat = TRUE;
                break;
            }
            break;
        case NCI_GID_EE_MANAGE :
             switch (oid)
             {
             case 0x00:
                  stat = FALSE;
                  break;
             default :
                  stat = TRUE;
                  break;
             }
                break;
        default:
            stat = TRUE;
            break;
        }
        break;

    default:
        stat = TRUE;
        break;

    }
    NFC_TRACE_DEBUG1 ("nfc_ncif_proc_proprietary_rsp: exit status=%u", stat);
    return stat;
}

#endif

#if((NFC_NXP_ESE == TRUE) && (NXP_EXTNS == TRUE) && (NXP_ESE_ETSI_READER_ENABLE == TRUE))
void disc_deact_ntf_timeout_handler(tNFC_RESPONSE_EVT event)
{
    tNFC_RESPONSE   evt_data;
    etsi_reader_in_progress = FALSE;
    if (nfc_cb.p_resp_cback)
    {
        evt_data.status = (tNFC_STATUS) NFC_STATUS_HW_TIMEOUT;
        (*nfc_cb.p_resp_cback) (event, &evt_data);
    }
}
#endif

#if(NXP_EXTNS == TRUE)
#if(NFC_NXP_STAT_DUAL_UICC_EXT_SWITCH == TRUE)
/*******************************************************************************
**
** Function:        nfc_ncif_reset_nfcc
**
** Description:     Reset the NFCC
**
** Returns:         NFA_STATUS_OK if successfully initiated
**                  NFA_STATUS_FAILED otherwise
**
*******************************************************************************/
tNFC_STATUS nfc_ncif_reset_nfcc()
{
    NFC_TRACE_DEBUG1("%s : Enter",__FUNCTION__);
    nfc_nci_ExtnCmd_t extn_cmd;
    UINT8 status = NCI_STATUS_FAILED;
    int isfound,retry_count = 0;
    long retlen = 0;
    uint8_t *buffer = NULL;
    long bufflen = 256;

    /*NCI_INIT_CMD*/
    static uint8_t cmd_init_nci[] = {0x20, 0x01, 0x00};
    /*NCI_RESET_CMD*/
    static uint8_t cmd_reset_nci[] = {0x20, 0x00, 0x01, 0x00}; //keep configuration
    /*NXP_ACT_PROP_EXTN*/
    static uint8_t cmd_act_prop_extn[] = {0x2F, 0x02, 0x00};
    /*NXP_CORE_STANDBY*/
    static uint8_t cmd_core_standby[] = {0x2F, 0x00, 0x01, 0x01};

    /*Nfcc power cycle*/
    nfc_cb.p_hal->power_cycle();

    /*Transceive NCI_INIT_CMD*/
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_reset_nci);
    extn_cmd.p_cmd = cmd_reset_nci;

    do {
        status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while((status != NCI_STATUS_OK) && (retry_count <= 3));
    if(status != NCI_STATUS_OK)
        goto TheEndReset;

    /*Transceive NCI_RESET_CMD*/
    retry_count = 0;
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_init_nci);
    extn_cmd.p_cmd = cmd_init_nci;
    do {
        status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while((status != NCI_STATUS_OK) && (retry_count <= 3));
    if(status != NCI_STATUS_OK)
        goto TheEndReset;

        /*Transceive NXP_ACT_PROP_EXTN*/
        retry_count = 0;
    retlen = 0;
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_act_prop_extn);
    extn_cmd.p_cmd = cmd_act_prop_extn;

    do {
        status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while((status != NCI_STATUS_OK) && (retry_count <= 3));
    if(status != NCI_STATUS_OK)
        goto TheEndReset;

        /*Transceive NXP_CORE_STANDBY*/
        retry_count = 0;
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_core_standby);
    extn_cmd.p_cmd = cmd_core_standby;

    do {
        status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while((status != NCI_STATUS_OK) && (retry_count <= 3));
    TheEndReset:
    NFC_TRACE_DEBUG1("%s : Exit",__FUNCTION__);
    return status;
}
/*******************************************************************************
**
** Function         uicc_eeprom_get_config
**
** Description      get UICC configuration from EEPROM
**
** Returns          None
**
*******************************************************************************/
void uicc_eeprom_get_config(UINT8* config_resp )
{
    NFC_TRACE_DEBUG1("%s : Enter",__FUNCTION__);
    UINT8 cmd_get_dualUicc_config[] = {0x20, 0x03, 0x03, 0x01, 0xA0, 0xEC};
    nfc_nci_ExtnCmd_t extn_cmd;
    int uicc_mode = 0;
    UINT8* config_rsp = NULL;
    UINT8 config_status = NCI_STATUS_FAILED;
    UINT8 retry_count = 0;

    if(GetNumValue(NAME_NXP_DUAL_UICC_ENABLE, &uicc_mode, sizeof(int)))
    {
        NFC_TRACE_DEBUG1("NXP_DUAL_UICC_ENABLE : 0x%02x",uicc_mode);

    }else
    {
        uicc_mode = 0x00;
        NFC_TRACE_DEBUG1("NXP_DUAL_UICC_ENABLE not found; taking default value : 0x%02x",uicc_mode);
    }
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));
    extn_cmd.cmd_len = sizeof(cmd_get_dualUicc_config);
    extn_cmd.p_cmd = cmd_get_dualUicc_config;
    do {
        config_status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while((config_status != NCI_STATUS_OK) && (retry_count <= 3));
    if(config_status == NCI_STATUS_OK && extn_cmd.rsp_len > 0)
    {
            memcpy(config_resp, extn_cmd.p_cmd_rsp, extn_cmd.rsp_len);
    }
    else
    {
        NFC_TRACE_DEBUG1("%s :HAL_NFC_IOCTL_NCI_TRANSCEIVE Failed",__FUNCTION__);
    }
}

/*******************************************************************************
**
** Function         uicc_eeprom_set_config
**
** Description      set UICC configuration in EEPROM
**
** Returns         No Data
**
*******************************************************************************/
void uicc_eeprom_set_config(UINT8* config_rsp)
{
    NFC_TRACE_DEBUG1("%s : Enter",__FUNCTION__);
    UINT8 cmd_set_dualUicc_config[] = {0x20, 0x02, 0x05, 0x01, 0xA0, 0xEC, 0x01, 0x11};
    nfc_nci_ExtnCmd_t extn_cmd;
    int uicc_mode = 0;
    UINT8 config_status = NCI_STATUS_FAILED;
    UINT8 retry_count = 0;

    if(GetNumValue(NAME_NXP_DUAL_UICC_ENABLE, &uicc_mode, sizeof(int)))
    {
        NFC_TRACE_DEBUG1("NXP_DUAL_UICC_ENABLE : 0x%02x",uicc_mode);

    }else
    {
        uicc_mode = 0x00;
        NFC_TRACE_DEBUG1("NXP_DUAL_UICC_ENABLE not found; taking default value : 0x%02x",uicc_mode);
    }
    memset(&extn_cmd, 0x00, sizeof(nfc_nci_ExtnCmd_t));

    if(uicc_mode == 0x00)
    {
        cmd_set_dualUicc_config[7] = 0x01;
    }
    else if(config_rsp != NULL)
    {
        cmd_set_dualUicc_config[7] = config_rsp[8];
    }

    extn_cmd.cmd_len = sizeof(cmd_set_dualUicc_config);
    extn_cmd.p_cmd = cmd_set_dualUicc_config;
    do {
        config_status = nfc_cb.p_hal->ioctl(HAL_NFC_IOCTL_NCI_TRANSCEIVE, &extn_cmd);
        retry_count++;
    }
    while((config_status != NCI_STATUS_OK) && (retry_count <= 3));
    if((config_status != NCI_STATUS_OK) && (retry_count = 4))
    {
        NFC_TRACE_DEBUG1("%s :HAL_NFC_IOCTL_NCI_TRANSCEIVE Failed",__FUNCTION__);
    }
}
#endif
#endif

#endif /* NFC_INCLUDED == TRUE*/
