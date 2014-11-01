/*
 * Copyright (C) 2014 The Atmel Android Open Source Project
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

#ifndef ANDROID_INCLUDE_HARDWARE_LOADER_H
#define ANDROID_INCLUDE_HARDWARE_LOADER_H

#include <hardware/hardware.h>

__BEGIN_DECLS

/**
 * Get the module info associated with a module instance by class 'class_id'
 * and instance 'inst'.
 *
 * In some cases, we need to load the module directly.
 * such as camera.goldfish.so for fake camera if we don't have a real hardware module
 *
 * @return: 0 == success, <0 == error and *module == NULL
 */
int hw_get_module_by_class_directly(const char *class_id, const char *inst,
                           const struct hw_module_t **module);

__END_DECLS

#endif  /* ANDROID_INCLUDE_HARDWARE_LOADER_H */
