/* jsonfile.h -- reading and writing small JSON files on SD (jansson)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __JSONFILE_H
#define __JSONFILE_H

#include <stdbool.h>
#include <jansson.h>

/* NULL if the file is missing or not valid JSON. Release with json_decref. */
json_t *json_load(const char *path);

/* Writes indented JSON, creating folders as needed. Writes to a temporary
 * file first so a pulled card never leaves half a file. */
bool json_save(const char *path, const json_t *root);

#endif
