/** @file
 * Copyright (c) 2016-2018, 2021-2022 Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "include/bsa_acs_val.h"
#include "include/bsa_acs_common.h"

#include "include/bsa_acs_smmu.h"
#include "include/bsa_acs_iovirt.h"

/**
  @brief  This API reads 32-bit data from a register of an SMMU controller
          specifed by index
  @param offset   32-bit register offset
  @param index    when multiple SMMU controllers are present in the system.
                  '0' based index to uniquely identify them

  @return  32-bit data value
**/
uint32_t
val_smmu_read_cfg(uint32_t offset, uint32_t index)
{

  uint64_t ctrl_base = val_smmu_get_info(SMMU_CTRL_BASE, index);

  if (ctrl_base == 0)
      return 0;

  return val_mmio_read(ctrl_base + offset);
}

#ifndef TARGET_LINUX

/**
  @brief   This API executes all the SMMU tests sequentially
           1. Caller       -  Application layer.
           2. Prerequisite -  val_smmu_create_info_table()
  @param   num_pe - the number of PE to run these tests on.
  @param   g_sw_view - Keeps the information about which view tests to be run
  @return  Consolidated status of all the tests run.
**/
uint32_t
val_smmu_execute_tests(uint32_t num_pe, uint32_t *g_sw_view)
{
  uint32_t status, i;
  uint32_t num_smmu;

  status = ACS_STATUS_PASS;

  for (i=0 ; i<MAX_TEST_SKIP_NUM ; i++){
      if (g_skip_test_num[i] == ACS_SMMU_TEST_NUM_BASE) {
          val_print(ACS_PRINT_TEST, "\n       USER Override - Skipping all SMMU tests \n", 0);
          return ACS_STATUS_SKIP;
      }
  }

  if (g_single_module != SINGLE_MODULE_SENTINEL && g_single_module != ACS_SMMU_TEST_NUM_BASE &&
       (g_single_test == SINGLE_MODULE_SENTINEL ||
         (g_single_test - ACS_SMMU_TEST_NUM_BASE > 100 ||
          g_single_test - ACS_SMMU_TEST_NUM_BASE < 0))) {
    val_print(ACS_PRINT_TEST, "\n      USER Override - Skipping all SMMU tests ", 0);
    val_print(ACS_PRINT_TEST, "\n      (Running only a single module)\n", 0);
    return ACS_STATUS_SKIP;
  }

  num_smmu = val_iovirt_get_smmu_info(SMMU_NUM_CTRL, 0);
  if (num_smmu == 0) {
    val_print(ACS_PRINT_WARN, "\n       No SMMU Controller Found, Skipping SMMU tests...\n", 0);
    return ACS_STATUS_SKIP;
  }

  g_curr_module = 1 << SMMU_MODULE;

  if (g_sw_view[G_SW_OS]) {
       val_print(ACS_PRINT_ERR, "\nOperating System View:\n", 0);
       status |= os_i001_entry(num_pe);
       status |= os_i002_entry(num_pe);
       status |= os_i005_entry(num_pe);
       status |= os_i006_entry(num_pe);
  }

  if (g_sw_view[G_SW_HYP]) {
       val_print(ACS_PRINT_ERR, "\nHypervisor View:\n", 0);
       status |= hyp_i001_entry(num_pe);
       status |= hyp_i002_entry(num_pe);
       status |= hyp_i003_entry(num_pe);
       status |= hyp_i004_entry(num_pe);
  }


  if (status != ACS_STATUS_PASS)
    val_print(ACS_PRINT_TEST, "\n      *** One or more tests have Failed/Skipped.*** \n", 0);
  else
    val_print(ACS_PRINT_TEST, "\n       All SMMU tests Passed!! \n", 0);

  return status;
}
#endif

/**
  @brief  This API is used to start the process of saving
          DMA addresses being used by the input devic. It is
          used by the test to indicate the upcoming DMA
          transfers to be recorded

  @param  ctrl_index  dma controller index

  @return 0 for success
**/
uint32_t
val_smmu_start_monitor_dev(uint32_t ctrl_index)
{
  void *ap = NULL;

  ap = (void *)val_dma_get_info(DMA_PORT_INFO, ctrl_index);
  if (ap == NULL) {
      val_print(ACS_PRINT_ERR, "Invalid Controller index %d \n", ctrl_index);
      return ACS_STATUS_ERR;
  }

  pal_smmu_device_start_monitor_iova(ap);

  return 0;
}

/**
  @brief  Stops the recording of the DMA addresses being
          used by the input port.

  @param  ctrl_index  dma controller index

  @return 0 for success
**/
uint32_t
val_smmu_stop_monitor_dev(uint32_t ctrl_index)
{
  void *ap = NULL;

  ap = (void *)val_dma_get_info(DMA_PORT_INFO, ctrl_index);
  if (ap == NULL) {
      val_print(ACS_PRINT_ERR, "Invalid Controller index %d \n", ctrl_index);
      return ACS_STATUS_ERR;
  }

  pal_smmu_device_stop_monitor_iova(ap);

  return 0;
}


/**
  @brief   Check if input address is within the IOVA translation range for the device
           1. Caller       -  Test suite
           2. Prerequisite -  val_smmu_create_info_table()
  @param   ctrl_index - The device whose IO Translation range needs to be checked
  @param   dma_addr   - The input address to be checked
  @return  Success if the input address is found in the range
**/
uint32_t
val_smmu_check_device_iova(uint32_t ctrl_index, addr_t dma_addr)
{
  void *ap = NULL;
  uint32_t status;

  ap = (void *)val_dma_get_info(DMA_PORT_INFO, ctrl_index);
  if (ap == NULL) {
      val_print(ACS_PRINT_ERR, "Invalid Controller index %d \n", ctrl_index);
      return ACS_STATUS_ERR;
  }
  val_print(ACS_PRINT_DEBUG, "Input dma addr = %lx \n", dma_addr);

  status = pal_smmu_check_device_iova(ap, dma_addr);

  return status;
}

/**
  @brief  To implement the requested operation for SMMU.

  @param  ops  Desired Operation
  @param  smmu_index  SMMU index
  @param  *param1  Parameter 1
  @param  *param2  Parameter 2

  @return 0 for success
**/
uint64_t
val_smmu_ops(SMMU_OPS_e ops, uint32_t smmu_index, void *param1, void *param2)
{

  switch(ops)
  {
      case SMMU_START_MONITOR_DEV:
          return val_smmu_start_monitor_dev(*(uint32_t *)param1);

      case SMMU_STOP_MONITOR_DEV:
          return val_smmu_stop_monitor_dev(*(uint32_t *)param1);

      case SMMU_CHECK_DEVICE_IOVA:
          return val_smmu_check_device_iova(*(uint32_t *)param1, *(addr_t *)param2);
          break;

      default:
          break;
  }
  return 0;

}

/**
  @brief  Returns the maximum PASID value supported by the SMMU controller

  @param  smmu_index  SMMU index

  @return 0 is returned when PASID support isnot detected.
          Nonzero is returned ifmaximum PASID value supported
**/
uint32_t
val_smmu_max_pasids(uint32_t smmu_index)
{
  uint64_t smmu_base;

  smmu_base = val_iovirt_get_smmu_info(SMMU_CTRL_BASE, smmu_index);
  return pal_smmu_max_pasids(smmu_base);
}

/**
  @brief  Prepares the SMMU page tables to support input PASID.

  @param  smmu_index  SMMU index for which PASID support is needed.
  @param  pasid       Process Address Space IDentifier.

  @return Returns 0 for success and 1 for failure.
**/
uint32_t
val_smmu_create_pasid_entry(uint32_t smmu_index, uint32_t pasid)
{
  uint64_t smmu_base;

  smmu_base = val_smmu_get_info(SMMU_CTRL_BASE, smmu_index);
  return pal_smmu_create_pasid_entry(smmu_base, pasid);
}

/**
  @brief  Converts physical address to I/Ovirtual address

  @param  smmu_index  SMMU index
  @param  pa          Physical address to use in conversion

  @return Returns 0 for success and 1 for failure.
**/
uint64_t
val_smmu_pa2iova(uint32_t smmu_index, uint64_t pa)
{
  uint64_t smmu_base;

  smmu_base = val_smmu_get_info(SMMU_CTRL_BASE, smmu_index);
  return pal_smmu_pa2iova(smmu_base, pa);
}
