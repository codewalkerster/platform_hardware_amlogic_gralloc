/*
 * Copyright (C) 2020, 2023 Arm Limited.
 * SPDX-License-Identifier: Apache-2.0
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

package arm.graphics;

/**
 * Used by IAllocator/IMapper (gralloc) to describe formats data types.
 *
 * This is an enum for the formats data types.
 */
@VintfStability
@Backing(type="long")
enum DataType {
    UNORM  = 0,
    SNORM  = 1,
    UINT   = 2,
    SINT   = 3,
    SFLOAT = 4,
    UNKNOWN = 0xFF,
}
