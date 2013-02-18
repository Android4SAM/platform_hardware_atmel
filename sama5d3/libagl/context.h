/* libs/opengles/context.h
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#ifndef ANDROID_CONTEXT_INTERFACE_H
#define ANDROID_CONTEXT_INTERFACE_H

#include "gl_context.h"
#include <ui/Region.h>

namespace android {

class Region;
// ---------------------------------------------------------------------------

struct region_iterator : public copybit_region_t {
    region_iterator(const Region& region)
        : b(region.begin()), e(region.end()) {
        this->next = iterate;
    }
private:
    static int iterate(copybit_region_t const * self, copybit_rect_t* rect) {
        region_iterator const* me = static_cast<region_iterator const*>(self);
        if (me->b != me->e) {
            *reinterpret_cast<Rect*>(rect) = *me->b++;
            return 1;
        }
        return 0;
    }
    mutable Region::const_iterator b;
    Region::const_iterator const e;
};

}

using namespace android::gl;

#endif //ANDROID_CONTEXT_INTERFACE_H
