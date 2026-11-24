/*
 * Copyright © 2009-2018 Siyan Panayotov <contact@siyanpanayotov.com>
 *
 * Based on code by (see README for details):
 * - Björn Lindqvist <bjourne@gmail.com>
 *
 * This file is part of Viewnior.
 *
 * Viewnior is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Viewnior is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Viewnior.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __UNI_EXIV2__H_
#define __UNI_EXIV2__H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void    uni_read_exiv2_map          (const char *uri,
                                     void (*callback)(const char*, const char*, void*),
                                     void *user_data);

/* Copies the metadata of the image file at @source_path onto the image
 * encoded in @data, for saving an edited image. Returns the result as a
 * new buffer to free with g_free(), and its length in @result_length, or
 * NULL if exiv2 can't read either image or write the result. If the
 * edited pixels already have the EXIF orientation applied, pass TRUE for
 * @reset_orientation_tag so viewers don't apply it a second time. */
unsigned char *uni_exiv2_copy_metadata (const char *source_path,
                                        const unsigned char *data, size_t length,
                                        int reset_orientation_tag,
                                        size_t *result_length);

#ifdef __cplusplus
}
#endif

#endif /* __UNI_EXIV2__H_ */
