/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2015 NXP Semiconductors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _PHNXPNCIHAL_ADAPTATION_H_
#define _PHNXPNCIHAL_ADAPTATION_H_

#include <hardware/hardware.h>
#include <hardware/nfc.h>

#if(NFC_NXP_CHIP_TYPE == PN547C2)
#define NFC_NCI_NXP_PN54X_HARDWARE_MODULE_ID "nfc_nci.pn54x"
#else
#define NFC_NCI_NXP_PN54X_HARDWARE_MODULE_ID "nfc_nci.nqx"
#endif

typedef struct
{
    struct nfc_nci_device nci_device;

    /* Local definitions */
    int (*ioctl)(const struct nfc_nci_device *p_dev, void *p_data, long arg);
    int(*check_fw_dwnld_flag)(const struct nfc_nci_device *p_dev, uint8_t* param1);
} pn547_dev_t;

/* NXP HAL functions */

int phNxpNciHal_open(nfc_stack_callback_t *p_cback,
        nfc_stack_data_callback_t *p_data_cback);
int phNxpNciHal_write(uint16_t data_len, const uint8_t *p_data);
int phNxpNciHal_ioctl(long arg, void *p_data);
int phNxpNciHal_core_initialized(uint8_t* p_core_init_rsp_params);
int phNxpNciHal_pre_discover(void);
int phNxpNciHal_close(void);
int phNxpNciHal_control_granted(void);
int phNxpNciHal_power_cycle(void);
int phNxpNciHal_MinOpen(nfc_stack_callback_t *p_cback,
        nfc_stack_data_callback_t *p_data_cback);
int phNxpNciHal_Minclose(void);
int phNxpNciHal_getFWDownloadFlag(uint8_t* fwDnldRequest);
#endif /* _PHNXPNCIHAL_ADAPTATION_H_ */
