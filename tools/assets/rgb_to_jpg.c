/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Host helper for generate_icon.py and generate_example_theme.py: raw RGB888 on stdin, JPEG file out.
 * Usage: rgb_to_jpg <width> <height> <quality> <out.jpg> */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../src/ams_mitm/usbl/third_party/stb/stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 5) {
        return 2;
    }
    const int width = atoi(argv[1]), height = atoi(argv[2]), quality = atoi(argv[3]);
    const size_t size = (size_t)width * height * 3;
    unsigned char *rgb = malloc(size);
    if (rgb == NULL || fread(rgb, 1, size, stdin) != size) {
        return 1;
    }
    return stbi_write_jpg(argv[4], width, height, 3, rgb, quality) ? 0 : 1;
}
