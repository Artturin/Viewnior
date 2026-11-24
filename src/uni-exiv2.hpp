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

#ifdef __cplusplus
extern "C" {
#endif

void    uni_read_exiv2_map          (const char *uri,
                                     void (*callback)(const char*, const char*, void*),
                                     void *user_data);

/* Copy the metadata of an image to a new file of it, for saving an edited
 * image. uni_read_exiv2_to_cache() reads it from the original, and
 * uni_write_exiv2_from_cache() writes it to the new file. If the pixels
 * that were saved already have the EXIF orientation applied, pass TRUE
 * for @reset_orientation_tag so viewers don't apply it a second time. */
int     uni_read_exiv2_to_cache     (const char *uri);
int     uni_write_exiv2_from_cache  (const char *uri,
                                     int reset_orientation_tag);

#ifdef __cplusplus
}
#endif

#endif /* __UNI_EXIV2__H_ */
