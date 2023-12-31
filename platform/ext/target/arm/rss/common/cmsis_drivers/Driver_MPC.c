/*
 * Copyright (c) 2016-2023 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Driver_MPC_Sie.h"
#include "cmsis_driver_config.h"
#include "RTE_Device.h"

#if (defined (RTE_VM0_MPC) && (RTE_VM0_MPC == 1))
ARM_DRIVER_MPC(MPC_VM0_DEV, Driver_VM0_MPC);
#endif /* RTE_VM0_MPC */

#if (defined (RTE_VM1_MPC) && (RTE_VM1_MPC == 1))
ARM_DRIVER_MPC(MPC_VM1_DEV, Driver_VM1_MPC);
#endif /* RTE_VM1_MPC */

#if (defined (RTE_SIC_MPC) && (RTE_SIC_MPC == 1))
ARM_DRIVER_MPC(MPC_SIC_DEV, Driver_SIC_MPC);
#endif /* RTE_SIC_MPC */
