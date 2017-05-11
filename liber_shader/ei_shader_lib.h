/**************************************************************************
 * Copyright (C) 2013 Rendease Co., Ltd.
 * All rights reserved.
 *
 * This program is commercial software: you must not redistribute it 
 * and/or modify it without written permission from Rendease Co., Ltd.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * End User License Agreement for more details.
 *
 * You should have received a copy of the End User License Agreement along 
 * with this program.  If not, see <http://www.rendease.com/licensing/>
 *************************************************************************/

#ifndef EI_SHADER_LIB_H
#define EI_SHADER_LIB_H

/** The header of the module.
 * \file ei_shader_lib.h
 * \author Elvic Liang
 */

#define EI_OSL_INTEROP
#include <ei.h>

/** The function to initialize a module on loading. */
EI_SHADER_API void module_init();
/** The function to cleanup a module on unloading. */
EI_SHADER_API void module_exit();

#endif
