/* jsonfile.c -- reading and writing small JSON files on SD (jansson)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include "jsonfile.h"
#include "backup.h"

json_t *json_load(const char *path) {
	json_error_t err;
	json_t *root = json_load_file(path, 0, &err);
	if (!root && err.line >= 0)   /* line -1: the file couldn't be opened (normal: no file) */
		printf("json: %s:%d: %s\r\n", path, err.line, err.text);
	return root;
}

bool json_save(const char *path, const json_t *root) {
	char tmp[300];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if (!mkdir_parents(path))
		return false;
	if (json_dump_file(root, tmp, JSON_INDENT(2) | JSON_PRESERVE_ORDER) != 0) {
		remove(tmp);
		return false;
	}
	remove(path);
	return rename(tmp, path) == 0;
}
