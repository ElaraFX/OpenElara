/**************************************************************************
 * Copyright (C) 2013 Elvic Liang<len3dev@gmail.com>
 * All rights reserved.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
