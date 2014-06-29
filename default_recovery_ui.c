/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <linux/input.h>

#include "recovery_ui.h"
#include "common.h"
#include "extendedcommands.h"


#define resX gr_fb_width()		//Value obtained from function 'gr_fb_width()'
#define resY gr_fb_height()		//Value obtained from function 'gr_fb_height()'	


char* MENU_HEADERS[] = { NULL };

char* MENU_ITEMS[] = { "Reboot Phone",
                       "Install zip",
                       "Wipe Menu",
                       "Backup/Restore",
                       "Mounts/Storage",
                       "Advanced Menu",
                       "Carliv",
                       "Power Menu",
                       NULL };

void device_ui_init(UIParameters* ui_parameters) {
}

int device_recovery_start() {
    return 0;
}

int device_wipe_data() {
    return 0;
}

int device_wipe_cache() {
    return 0;
}

int device_perform_action(int which) {
    return which;
}

int device_reboot_now(volatile char* key_pressed, int key_code) {
    return 0;
}

//For those devices which has skewed X axis and Y axis detection limit (Not similar to XY resolution of device), So need normalization
int MT_X(int fd, int x)
{
	int abs_store[6] = {0};

  	ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), abs_store);
   	int maxX = abs_store[2];

	int out;
	out = maxX ? (x*resX/maxX) : x;
	return out;
}

int MT_Y(int fd, int y)
{	
	int abs_store[6] = {0};
   	
   	ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), abs_store);
   	int maxY = abs_store[2];

	int out;
	out = maxY ? (y*resY/maxY) : y;
	return out;
}
