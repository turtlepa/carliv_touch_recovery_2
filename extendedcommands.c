#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "make_ext4fs.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"
#include "cutils/android_reboot.h"
#include "mmcutils/mmcutils.h"

#include "adb_install.h"

#define ABS_MT_POSITION_X 0x35  /* Center X ellipse position */

int signature_check_enabled = 1;
int script_assert_enabled = 1;
int md5_check_enabled = 1;
static const char *SDCARD_UPDATE_FILE = "/sdcard/update.zip";

int
get_filtered_menu_selection(char** headers, char** items, int menu_only, int initial_selection, int items_count) {
    int index;
    int offset = 0;
    int* translate_table = (int*)malloc(sizeof(int) * items_count);
    for (index = 0; index < items_count; index++) {
        if (items[index] == NULL)
            continue;
        char *item = items[index];
        items[index] = NULL;
        items[offset] = item;
        translate_table[offset] = index;
        offset++;
    }
    items[offset] = NULL;

    initial_selection = translate_table[initial_selection];
    int ret = get_menu_selection(headers, items, menu_only, initial_selection);
    if (ret < 0 || ret >= offset) {
        free(translate_table);
        return ret;
    }

    ret = translate_table[ret];
    free(translate_table);
    return ret;
}

void write_string_to_file(const char* filename, const char* string) {
    ensure_path_mounted(filename);
    char tmp[PATH_MAX];
    sprintf(tmp, "mkdir -p $(dirname %s)", filename);
    __system(tmp);
    FILE *file = fopen(filename, "w");
    if (file != NULL) {
        fprintf(file, "%s", string);
        fclose(file);
    }
}

void write_recovery_version() {
    if ( is_data_media() ) {
        write_string_to_file("/sdcard/0/clockworkmod/.recovery_version",EXPAND(RECOVERY_VERSION) "\n" EXPAND(TARGET_DEVICE));
    }
    write_string_to_file("/sdcard/clockworkmod/.recovery_version",EXPAND(RECOVERY_VERSION) "\n" EXPAND(TARGET_DEVICE));
}

void
toggle_signature_check()
{
    signature_check_enabled = !signature_check_enabled;
    ui_print("Signature Check: %s\n", signature_check_enabled ? "Enabled" : "Disabled");
}

void
toggle_script_assert_check()
{
    script_assert_enabled = !script_assert_enabled;
    ui_print("Script assert Check: %s\n", script_assert_enabled ? "Enabled" : "Disabled");
}

void
toggle_md5_check()
{
    md5_check_enabled = !md5_check_enabled;
    ui_print("md5 Check: %s\n", md5_check_enabled ? "Enabled" : "Disabled");
}

#define POWER_ITEM_RECOVERY	    0
#define POWER_ITEM_BOOTLOADER   1
#define POWER_ITEM_POWEROFF	    2

void show_power_menu() {
	static char* headers[] = { "Power Options",
                                "",
                                NULL
    };
 
    static char* list[] = { "Reboot Recovery",
                            "Reboot to Bootloader",
                            "Power Off",
                            NULL
    };
	
	char bootloader_mode[PROPERTY_VALUE_MAX];
    property_get("ro.bootloader.mode", bootloader_mode, "");
    if (!strcmp(bootloader_mode, "download")) {
        list[1] = "Reboot to Download";
    }
    
	for (;;) 
	{
		int chosen_item = get_menu_selection(headers, list, 0, 0);
		if (chosen_item == GO_BACK)
            break;
		switch (chosen_item) {	
			case POWER_ITEM_RECOVERY:
			{
                ui_print("Rebooting recovery...\n");
                reboot_main_system(ANDROID_RB_RESTART2, 0, "recovery");
                break;
            }
			case POWER_ITEM_BOOTLOADER:
			{
                if (!strcmp(bootloader_mode, "download")) {
                    ui_print("Rebooting to download mode...\n");
                    reboot_main_system(ANDROID_RB_RESTART2, 0, "download");
                } else {
                    ui_print("Rebooting to bootloader...\n");
                    reboot_main_system(ANDROID_RB_RESTART2, 0, "bootloader");
                }
                break;
            }	
			case POWER_ITEM_POWEROFF:
			{
                ui_print("Shutting down...\n");
                reboot_main_system(ANDROID_RB_POWEROFF, 0, 0);
                break;
            }
		}
	}
}

void toggle_touch_control_menu() 
{
    if (ensure_path_mounted("/sdcard/") != 0) {
        LOGE ("Can't mount sdcard\n");
        return;
    }

    static const char* headers[] = {  "Toggle touch control type",
		                                "",
		                                NULL
    };

    static char* list[] = { "",			//Toggle full touch
                            "Disable all touch",
                            NULL
    };
    
    struct stat info;
    if (0 != stat("/sdcard/clockworkmod/.full_nav", &info))
    {
    	list[0] = "Touch control - ON";
    }
    else if (0 == stat("/sdcard/clockworkmod/.full_nav", &info))
    {
    	list[0] = "Touch control - OFF";
    }
    
    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item)
    {
        case 0:
            {
            	if (0 != stat("/sdcard/clockworkmod/.full_nav", &info))
            	{
		    	__system("touch /sdcard/clockworkmod/.full_nav");
		    	ui_print("Full touch control disabled\n");
				}
				else if (0 == stat("/sdcard/clockworkmod/.full_nav", &info))
				{
				    	__system("rm /sdcard/clockworkmod/.full_nav");
				    	ui_print("Full touch control enabled\n");
				    	ui_print("Click on buttons on screen for selection.\n");
				    	ui_print("Swipe left for Go Back.\n");
				    	ui_print("Swipe right for Scroll Down.\n");
				    	ui_print("Swipe left for Scroll Up.\n");
				}
				
				toggle_touch_control_menu();
            }
            break;
        case 1:
            {
		    	__system("touch /sdcard/clockworkmod/.full_nav");
		    	ui_print("Full touch control disabled\n");
		    	ui_print("Use Volume/Power keys to navigate.\n");
			toggle_touch_control_menu();
	            }
	            break;
    }
    //ui_set_show_text(1);
}

int install_zip(const char* packagefilepath)
{
    ui_print("\n-- Installing: %s\n", packagefilepath);
    if (device_flash_type() == MTD) {
        set_sdcard_update_bootloader_message();
    }
    int status = install_package(packagefilepath);
    ui_reset_progress();
    if (status != INSTALL_SUCCESS) {
        ui_set_background(BACKGROUND_ICON_ERROR);
        ui_print("Installation aborted.\n");
        return 1;
    }
    ui_set_background(BACKGROUND_ICON_CLOCKWORK);
    ui_print("\nInstall from sdcard complete.\n");
    ui_reset_icons();
    return 0;
}

#define ITEM_CHOOSE_ZIP       0
#define ITEM_APPLY_SIDELOAD   1
#define ITEM_APPLY_UPDATE     2 // /sdcard/update.zip
#define ITEM_SIG_CHECK        3
#define ITEM_ASSERT_CHECK     4
#define ITEM_CHOOSE_ZIP_INT   5

void show_install_update_menu()
{
    static char* headers[] = {  "Install update from zip file",
                                "",
                                NULL
    };
    
    char* install_menu_items[] = {  "Choose zip Sdcard",
                                    "Install zip from sideload",
                                    "Apply /sdcard/update.zip",
                                    "Toggle Signature Verification",
                                    "Toggle Assert Verification",
                                    NULL,
                                    NULL };

    char *other_sd = NULL;
    if(EXTRA_SDCARD == EMMC) {
		install_menu_items[5] = "choose zip from internal sdcard";
        other_sd = "/emmc/";
    } 
    else if (EXTRA_SDCARD == EXTERNALSD) {
		install_menu_items[5] = "choose zip from external sdcard";
        other_sd = "/external_sd/";
    }
    
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, install_menu_items, 0, 0);
        switch (chosen_item)
        {
            case ITEM_SIG_CHECK:
                toggle_signature_check();
                break;
            case ITEM_ASSERT_CHECK:
                toggle_script_assert_check();
                break;    
            case ITEM_APPLY_UPDATE:
            {
                if (confirm_selection("Confirm install?", "Yes - Install /sdcard/update.zip"))
                    install_zip(SDCARD_UPDATE_FILE);
                break;
            }
            case ITEM_CHOOSE_ZIP:
                show_choose_zip_menu("/sdcard/");
                break;
            case ITEM_APPLY_SIDELOAD:
                apply_from_adb();
                break;
            case ITEM_CHOOSE_ZIP_INT:
                if (other_sd != NULL)
                    show_choose_zip_menu(other_sd);
                break;
            default:
                return;
        }

    }
}

void show_wipe_menu()
{

    static char* headers[] = {  "Wipe Menu",
								"",
								NULL
    };

    char* list[] = { "Wipe Data - Factory Reset",
                            "Wipe Cache",
                            "Wipe Dalvik Cache",
                            NULL,	 	 
                            NULL
    };

    for (;;)
    {
		int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
		switch (chosen_item)
        	{
             case 0:
				wipe_data(ui_text_visible());
                if (!ui_text_visible()) return;
                break;   
             case 1:
                wipe_cache(ui_text_visible());
                if (!ui_text_visible()) return;
                break;  
             case 2:
                wipe_dalvik_cache(ui_text_visible());
                if (!ui_text_visible()) return;
                break;   
        }
    }
    
}

void free_string_array(char** array)
{
    if (array == NULL)
        return;
    char* cursor = array[0];
    int i = 0;
    while (cursor != NULL)
    {
        free(cursor);
        cursor = array[++i];
    }
    free(array);
}

char** gather_files(const char* directory, const char* fileExtensionOrDirectory, int* numFiles)
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int total = 0;
    int i;
    char** files = NULL;
    int pass;
    *numFiles = 0;
    int dirLen = strlen(directory);

    dir = opendir(directory);
    if (dir == NULL) {
        ui_print("Couldn't open directory.\n");
        return NULL;
    }

    int extension_length = 0;
    if (fileExtensionOrDirectory != NULL)
        extension_length = strlen(fileExtensionOrDirectory);

    int isCounting = 1;
    i = 0;
    for (pass = 0; pass < 2; pass++) {
        while ((de=readdir(dir)) != NULL) {
            // skip hidden files
            if (de->d_name[0] == '.')
                continue;

            // NULL means that we are gathering directories, so skip this
            if (fileExtensionOrDirectory != NULL)
            {
                // make sure that we can have the desired extension (prevent seg fault)
                if (strlen(de->d_name) < extension_length)
                    continue;
                // compare the extension
                if (strcmp(de->d_name + strlen(de->d_name) - extension_length, fileExtensionOrDirectory) != 0)
                    continue;
            }
            else
            {
                struct stat info;
                char fullFileName[PATH_MAX];
                strcpy(fullFileName, directory);
                strcat(fullFileName, de->d_name);
                lstat(fullFileName, &info);
                // make sure it is a directory
                if (!(S_ISDIR(info.st_mode)))
                    continue;
            }

            if (pass == 0)
            {
                total++;
                continue;
            }

            files[i] = (char*) malloc(dirLen + strlen(de->d_name) + 2);
            strcpy(files[i], directory);
            strcat(files[i], de->d_name);
            if (fileExtensionOrDirectory == NULL)
                strcat(files[i], "/");
            i++;
        }
        if (pass == 1)
            break;
        if (total == 0)
            break;
        rewinddir(dir);
        *numFiles = total;
        files = (char**) malloc((total+1)*sizeof(char*));
        files[total]=NULL;
    }

    if(closedir(dir) < 0) {
        LOGE("Failed to close directory.");
    }

    if (total==0) {
        return NULL;
    }

    // sort the result
    if (files != NULL) {
        for (i = 0; i < total; i++) {
            int curMax = -1;
            int j;
            for (j = 0; j < total - i; j++) {
                if (curMax == -1 || strcmp(files[curMax], files[j]) < 0)
                    curMax = j;
            }
            char* temp = files[curMax];
            files[curMax] = files[total - i - 1];
            files[total - i - 1] = temp;
        }
    }

    return files;
}

// pass in NULL for fileExtensionOrDirectory and you will get a directory chooser
char* choose_file_menu(const char* directory, const char* fileExtensionOrDirectory, const char* headers[])
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int numFiles = 0;
    int numDirs = 0;
    int i;
    char* return_value = NULL;
    int dir_len = strlen(directory);

    i = 0;
    while (headers[i]) {
        i++;
    }
    const char** fixed_headers = (const char*)malloc((i + 3) * sizeof(char*));
    i = 0;
    while (headers[i]) {
        fixed_headers[i] = headers[i];
        i++;
    }
    fixed_headers[i] = directory;
    fixed_headers[i + 1] = "";
    fixed_headers[i + 2 ] = NULL;

    char** files = gather_files(directory, fileExtensionOrDirectory, &numFiles);
    char** dirs = NULL;
    if (fileExtensionOrDirectory != NULL)
        dirs = gather_files(directory, NULL, &numDirs);
    int total = numDirs + numFiles;
    if (total == 0)
    {
        ui_print("No files found.\n");
    }
    else
    {
        char** list = (char**) malloc((total + 1) * sizeof(char*));
        list[total] = NULL;


        for (i = 0 ; i < numDirs; i++)
        {
            list[i] = strdup(dirs[i] + dir_len);
        }

        for (i = 0 ; i < numFiles; i++)
        {
            list[numDirs + i] = strdup(files[i] + dir_len);
        }

        for (;;)
        {
            int chosen_item = get_menu_selection(fixed_headers, list, 0, 0);
            if (chosen_item == GO_BACK)
                break;
            static char ret[PATH_MAX];
            if (chosen_item < numDirs)
            {
                char* subret = choose_file_menu(dirs[chosen_item], fileExtensionOrDirectory, headers);
                if (subret != NULL)
                {
                    strcpy(ret, subret);
                    return_value = ret;
                    break;
                }
                continue;
            }
            strcpy(ret, files[chosen_item - numDirs]);
            return_value = ret;
            break;
        }
        free_string_array(list);
    }

    free_string_array(files);
    free_string_array(dirs);
    free(fixed_headers);
    return return_value;
}

void show_choose_zip_menu(const char *mount_point)
{
    if (ensure_path_mounted(mount_point) != 0) {
        LOGE ("Can't mount %s\n", mount_point);
        return;
    }

    static char* headers[] = {  "Choose a zip to apply",
                                "",
                                NULL
    };

    char* file = choose_file_menu(mount_point, ".zip", headers);
    if (file == NULL)
        return;
    static char* confirm_install  = "Confirm install?";
    static char confirm[PATH_MAX];
    sprintf(confirm, "Yes - Install %s", basename(file));
    if (confirm_selection(confirm_install, confirm))
        install_zip(file);
}

void show_nandroid_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[] = {  "Choose an image to restore",
                                "",
                                NULL
    };

    char* file = choose_file_menu(path, NULL, headers);
    if (file == NULL)
        return;

    if (confirm_selection("Confirm restore?", "Yes - Restore"))
        nandroid_restore(file, 1, 1, 1, 1, 1, 0);
}

void show_nandroid_delete_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[] = {  "Choose an image to delete",
                                "",
                                NULL
    };

    char* file = choose_file_menu(path, NULL, headers);
    if (file == NULL)
        return;

    if (confirm_selection("Confirm delete?", "Yes - Delete")) {
        // nandroid_restore(file, 1, 1, 1, 1, 1, 0);
        char tmp[PATH_MAX];
        ui_print("Deleting %s\n", basename(file));
        sprintf(tmp, "rm -rf %s", file);
        __system(tmp);
        ui_print("Backup deleted!\n");
        uint64_t sdcard_free_mb = recalc_sdcard_space(path);
		ui_print("SD Card space free: %lluMB\n", sdcard_free_mb);
    }
}

int show_choose_delete_menu() 
{
    static char *CHOOSE_DELETE_MENU_ITEMS[] = { "View and delete old backups",
                                                NULL,
                                                NULL };

    #define ITEM_VIEW_BACKUPS_ON_SDCARD 0
    #define ITEM_VIEW_BACKUPS_ON_EXTRASD 1

    static char* headers[] = { "Choose a device to delete",
                               "backups from.",
                               "",
                               NULL
    };

    char *other_sd = NULL;
    if(EXTRA_SDCARD == EMMC) {
        CHOOSE_DELETE_MENU_ITEMS[1]="View and delete backups on /emmc";
        other_sd = "/emmc";
    } else if (EXTRA_SDCARD == EXTERNALSD) {
        CHOOSE_DELETE_MENU_ITEMS[1]="View and delete backups on /external_sd";
        other_sd = "/external_sd";
    }

    for (;;) {
        char base_path[PATH_MAX];
        int chosen_item = get_menu_selection(headers, CHOOSE_DELETE_MENU_ITEMS, 0, 0);
        switch(chosen_item) {
            case ITEM_VIEW_BACKUPS_ON_SDCARD:
                nandroid_get_backup_path(base_path, 0);
                break;
            case ITEM_VIEW_BACKUPS_ON_EXTRASD:
                nandroid_get_backup_path(base_path, 1);
                break;
            default:
                ui_print("Cancelling backup.\n");
                return 1;
        }
        show_nandroid_delete_menu(base_path);
    }
}

static void run_dedupe_gc(const char* other_sd) {
    ensure_path_mounted("/sdcard");
	char path[PATH_MAX], tmp[PATH_MAX];
	nandroid_get_assigned_backup_path(path, 0);
    if (other_sd) {
        ensure_path_mounted(other_sd);
		nandroid_get_assigned_backup_path(path, 1);
    }
	sprintf(tmp, "%s/blobs", path);
	nandroid_dedupe_gc(tmp);
    uint64_t sdcard_free_mb = recalc_sdcard_space(path);
    ui_print("SD Card space free: %lluMB\n", sdcard_free_mb);
}

int show_lowspace_menu(int i, const char* backup_path)
{
	static char *LOWSPACE_MENU_ITEMS[4] = { "Continue with backup",
											"View and delete old backups",
                                            NULL,
                                            NULL,
											NULL };

	#define ITEM_CONTINUE_BACKUP     0
	#define ITEM_VIEW_DELETE_BACKUPS 1
    #define ITEM_FREE_DATA_OR_CANCEL 2

    if(nandroid_get_default_backup_format() == NANDROID_BACKUP_FORMAT_DUP) {
        LOWSPACE_MENU_ITEMS[2] = "Free unused backup data";
        LOWSPACE_MENU_ITEMS[3] = "Cancel backup";
    } else {
        LOWSPACE_MENU_ITEMS[2] = "Cancel backup";
        LOWSPACE_MENU_ITEMS[3] = NULL;
    }

	static char* headers[] = { "Limited space available!",
								"",
								"There may not be enough space",
								"to continue backup.",
								"",
								"What would you like to do?",
								"",
								NULL
	};

	for (;;) {
		int chosen_item = get_menu_selection(headers, LOWSPACE_MENU_ITEMS, 0, 0);
		switch(chosen_item) {
			case ITEM_CONTINUE_BACKUP: {
				ui_print("Proceeding with backup.\n");
				return 0;
			}
			case ITEM_VIEW_DELETE_BACKUPS: {
                if(EXTRA_SDCARD) {
                    show_choose_delete_menu();
                } else {
                    char base_path[PATH_MAX];
                    nandroid_get_backup_path(base_path, 0);
				    show_nandroid_delete_menu(base_path);
                }
				break;
            }
            case ITEM_FREE_DATA_OR_CANCEL: {
                if(nandroid_get_default_backup_format() != NANDROID_BACKUP_FORMAT_DUP) {
                    ui_print("Cancelling backup.\n");
                    return 1;
                }
                char *other_sd = NULL;
                if(EXTRA_SDCARD) {
                    switch(EXTRA_SDCARD) {
                        case EMMC:
                            other_sd = "/emmc";
                            break;
                        case EXTERNALSD:
                            other_sd = "/external_sd";
                            break;
                    }
                }
                run_dedupe_gc(other_sd);
                break;
            }
			default:
				ui_print("Cancelling backup.\n");
				return 1;
		}
	}
}

#define MAX_NUM_USB_VOLUMES 3
#define LUN_FILE_EXPANDS    2

struct lun_node {
    const char *lun_file;
    struct lun_node *next;
};

static struct lun_node *lun_head = NULL;
static struct lun_node *lun_tail = NULL;

int control_usb_storage_set_lun(Volume* vol, bool enable, const char *lun_file) {
    const char *vol_device = enable ? vol->device : "";
    int fd;
    struct lun_node *node;

    // Verify that we have not already used this LUN file
    for(node = lun_head; node; node = node->next) {
        if (!strcmp(node->lun_file, lun_file)) {
            // Skip any LUN files that are already in use
            return -1;
        }
    }

    // Open a handle to the LUN file
    LOGI("Trying %s on LUN file %s\n", vol->device, lun_file);
    if ((fd = open(lun_file, O_WRONLY)) < 0) {
        LOGW("Unable to open ums lunfile %s (%s)\n", lun_file, strerror(errno));
        return -1;
    }

    // Write the volume path to the LUN file
    if ((write(fd, vol_device, strlen(vol_device) + 1) < 0) &&
       (!enable || !vol->device2 || (write(fd, vol->device2, strlen(vol->device2)) < 0))) {
        LOGW("Unable to write to ums lunfile %s (%s)\n", lun_file, strerror(errno));
        close(fd);
        return -1;
    } else {
        // Volume path to LUN association succeeded
        close(fd);

        // Save off a record of this lun_file being in use now
        node = (struct lun_node *)malloc(sizeof(struct lun_node));
        node->lun_file = strdup(lun_file);
        node->next = NULL;
        if (lun_head == NULL)
           lun_head = lun_tail = node;
        else {
           lun_tail->next = node;
           lun_tail = node;
        }

        LOGI("Successfully %sshared %s on LUN file %s\n", enable ? "" : "un", vol->device, lun_file);
        return 0;
    }
}

int control_usb_storage_for_lun(Volume* vol, bool enable) {
    static const char* lun_files[] = {
#ifdef BOARD_UMS_LUNFILE
        BOARD_UMS_LUNFILE,
#endif
#ifdef TARGET_USE_CUSTOM_LUN_FILE_PATH
        TARGET_USE_CUSTOM_LUN_FILE_PATH,
#endif
        "/sys/devices/platform/usb_mass_storage/lun%d/file",
        "/sys/class/android_usb/android0/f_mass_storage/lun/file",
        "/sys/class/android_usb/android0/f_mass_storage/lun_ex/file",
        NULL
    };

    // If recovery.fstab specifies a LUN file, use it
    if (vol->lun) {
        return control_usb_storage_set_lun(vol, enable, vol->lun);
    }

    // Try to find a LUN for this volume
    //   - iterate through the lun file paths
    //   - expand any %d by LUN_FILE_EXPANDS
    int lun_num = 0;
    int i;
    for(i = 0; lun_files[i]; i++) {
        const char *lun_file = lun_files[i];
        for(lun_num = 0; lun_num < LUN_FILE_EXPANDS; lun_num++) {
            char formatted_lun_file[255];
    
            // Replace %d with the LUN number
            bzero(formatted_lun_file, 255);
            snprintf(formatted_lun_file, 254, lun_file, lun_num);
    
            // Attempt to use the LUN file
            if (control_usb_storage_set_lun(vol, enable, formatted_lun_file) == 0) {
                return 0;
            }
        }
    }

    // All LUNs were exhausted and none worked
    LOGW("Could not %sable %s on LUN %d\n", enable ? "en" : "dis", vol->device, lun_num);

    return -1;  // -1 failure, 0 success
}

int control_usb_storage(Volume **volumes, bool enable) {
    int res = -1;
    int i;
    for(i = 0; i < MAX_NUM_USB_VOLUMES; i++) {
        Volume *volume = volumes[i];
        if (volume) {
            int vol_res = control_usb_storage_for_lun(volume, enable);
            if (vol_res == 0) res = 0; // if any one path succeeds, we return success
        }
    }

    // Release memory used by the LUN file linked list
    struct lun_node *node = lun_head;
    while(node) {
       struct lun_node *next = node->next;
       free((void *)node->lun_file);
       free(node);
       node = next;
    }
    lun_head = lun_tail = NULL;

    return res;  // -1 failure, 0 success
}

void show_mount_usb_storage_menu()
{
    // Build a list of Volume objects; some or all may not be valid
    Volume* volumes[MAX_NUM_USB_VOLUMES] = {
        volume_for_path("/sdcard"),
        volume_for_path("/emmc"),
        volume_for_path("/external_sd")
    };

    // Enable USB storage
    if (control_usb_storage(volumes, 1))
        return;

    static char* headers[] = {  "USB Mass Storage device",
                                "Leaving this menu unmounts",
                                "your SD card from your PC.",
                                "",
                                NULL
    };

    static char* list[] = { "Unmount", NULL };

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == 0)
            break;
    }

    // Disable USB storage
    control_usb_storage(volumes, 0);
}

int confirm_selection(const char* title, const char* confirm)
{
    struct stat info;
    if (0 == stat("/sdcard/clockworkmod/.no_confirm", &info))
        return 1;

    char* confirm_headers[]  = {  title, "  THIS CAN NOT BE UNDONE.", "", NULL };
    int many_confirm = 0 == stat("/sdcard/clockworkmod/.many_confirm", &info);
    if (many_confirm) {
        char* items[] = { "No",
                        "No",
                        "No",
                        "No",
                        "No",
                        "No",
                        "No",
                        confirm, //" Yes -- wipe partition",   // [7]
                        "No",
                        "No",
                        "No",
                        NULL };
        int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
        return chosen_item == 7;
    }
    else {
        char* items[] = { "No",
                        confirm, //" Yes -- wipe partition",   // [1]
                        NULL };
        int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
        return chosen_item == 1;
    }
}

#define MKE2FS_BIN      "/sbin/mke2fs"
#define TUNE2FS_BIN     "/sbin/tune2fs"
#define E2FSCK_BIN      "/sbin/e2fsck"

extern struct selabel_handle *sehandle;
int format_device(const char *device, const char *path, const char *fs_type) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        // silent failure for sd-ext
        if (strcmp(path, "/sd-ext") == 0)
            return -1;
        LOGE("unknown volume \"%s\"\n", path);
        return -1;
    }
    if (is_data_media_volume_path(path)) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strstr(path, "/data") == path && is_data_media()) {
        return format_unknown_device(NULL, path, NULL);
    }
    if (strcmp(fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", path);
        return -1;
    }

    if (strcmp(fs_type, "rfs") == 0) {
        if (ensure_path_unmounted(path) != 0) {
            LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
            return -1;
        }
        if (0 != format_rfs_device(device, path)) {
            LOGE("format_volume: format_rfs_device failed on %s\n", device);
            return -1;
        }
        return 0;
    }
 
    if (strcmp(v->mount_point, path) != 0) {
        return format_unknown_device(v->device, path, NULL);
    }

    if (ensure_path_unmounted(path) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(fs_type, "yaffs2") == 0 || strcmp(fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n",device);
            return -1;
        }
        return 0;
    }

    if (strcmp(fs_type, "ext4") == 0) {
        int length = 0;
        if (strcmp(v->fs_type, "ext4") == 0) {
            // Our desired filesystem matches the one in fstab, respect v->length
            length = v->length;
        }
        reset_ext4fs_info();
        int result = make_ext4fs(device, length, v->mount_point, sehandle);
        if (result != 0) {
            LOGE("format_volume: make_ext4fs failed on %s\n", device);
            return -1;
        }
        return 0;
    }

    return format_unknown_device(device, path, fs_type);
}

int format_unknown_device(const char *device, const char* path, const char *fs_type)
{
    LOGI("Formatting unknown device.\n");

    if (fs_type != NULL && get_flash_type(fs_type) != UNSUPPORTED)
        return erase_raw_partition(fs_type, device);

    // if this is SDEXT:, don't worry about it if it does not exist.
    if (0 == strcmp(path, "/sd-ext"))
    {
        struct stat st;
        Volume *vol = volume_for_path("/sd-ext");
        if (vol == NULL || 0 != stat(vol->device, &st))
        {
            LOGI("No app2sd partition found. Skipping format of /sd-ext.\n");
            return 0;
        }
    }

    if (NULL != fs_type) {
        if (strcmp("ext3", fs_type) == 0) {
            LOGI("Formatting ext3 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext3_device(device);
        }

        if (strcmp("ext2", fs_type) == 0) {
            LOGI("Formatting ext2 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext2_device(device);
        }
    }

    if (0 != ensure_path_mounted(path))
    {
        ui_print("Error mounting %s!\n", path);
        ui_print("Skipping format...\n");
        return 0;
    }

    char tmp[PATH_MAX];
    if (strcmp(path, "/data") == 0) {
        sprintf(tmp, "cd /data ; for f in $(ls -a | grep -v ^media$); do rm -rf $f; done");
        __system(tmp);
        // if the /data/media sdcard has already been migrated for android 4.2,
        // prevent the migration from happening again by writing the .layout_version
        struct stat st;
        if (0 == lstat("/data/media/0", &st)) {
            char* layout_version = "2";
            FILE* f = fopen("/data/.layout_version", "wb");
            if (NULL != f) {
                fwrite(layout_version, 1, 2, f);
                fclose(f);
            }
            else {
                LOGI("error opening /data/.layout_version for write.\n");
            }
        }
        else {
            LOGI("/data/media/0 not found. migration may occur.\n");
        }
    }
    else {
        sprintf(tmp, "rm -rf %s/*", path);
        __system(tmp);
        sprintf(tmp, "rm -rf %s/.*", path);
        __system(tmp);
    }

    ensure_path_unmounted(path);
    return 0;
}

//#define MOUNTABLE_COUNT 5
//#define DEVICE_COUNT 4
//#define MMC_COUNT 2

typedef struct {
    char mount[255];
    char unmount[255];
    Volume* v;
} MountMenuEntry;

typedef struct {
    char txt[255];
    Volume* v;
} FormatMenuEntry;

int is_safe_to_format(char* name)
{
    char str[255];
    char* partition;
    property_get("ro.cwm.forbid_format", str, "/misc,/nvram,/radio,/bootloader,/recovery,/efs,/wimax");

    partition = strtok(str, ", ");
    while (partition != NULL) {
        if (strcmp(name, partition) == 0) {
            return 0;
        }
        partition = strtok(NULL, ", ");
    }

    return 1;
}

void show_partition_menu()
{
    static char* headers[] = {  "Mounts and Storage Menu",
                                "",
                                NULL
    };

    static MountMenuEntry* mount_menu = NULL;
    static FormatMenuEntry* format_menu = NULL;

    typedef char* string;

    int i, mountable_volumes, formatable_volumes;
    int num_volumes;
    Volume* device_volumes;

    num_volumes = get_num_volumes();
    device_volumes = get_device_volumes();

    string options[255];

    if(!device_volumes)
        return;

    mountable_volumes = 0;
    formatable_volumes = 0;

    mount_menu = malloc(num_volumes * sizeof(MountMenuEntry));
    format_menu = malloc(num_volumes * sizeof(FormatMenuEntry));

    for (i = 0; i < num_volumes; ++i) {
        Volume* v = &device_volumes[i];
        if(strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) != 0 && strcmp("emmc", v->fs_type) != 0 && strcmp("bml", v->fs_type) != 0) {
            if (strcmp("datamedia", v->fs_type) != 0) {
                sprintf(&mount_menu[mountable_volumes].mount, "mount %s", v->mount_point);
                sprintf(&mount_menu[mountable_volumes].unmount, "unmount %s", v->mount_point);
                mount_menu[mountable_volumes].v = &device_volumes[i];
                ++mountable_volumes;
            }
            if (is_safe_to_format(v->mount_point)) {
                sprintf(&format_menu[formatable_volumes].txt, "format %s", v->mount_point);
                format_menu[formatable_volumes].v = &device_volumes[i];
                ++formatable_volumes;
            }
        }
        else if (strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) == 0 && is_safe_to_format(v->mount_point))
        {
            sprintf(&format_menu[formatable_volumes].txt, "format %s", v->mount_point);
            format_menu[formatable_volumes].v = &device_volumes[i];
            ++formatable_volumes;
        }
    }

    static char* confirm_format  = "Confirm format?";
    static char* confirm = "Yes - Format";
    char confirm_string[255];

    for (;;)
    {
        for (i = 0; i < mountable_volumes; i++)
        {
            MountMenuEntry* e = &mount_menu[i];
            Volume* v = e->v;
            if(is_path_mounted(v->mount_point))
                options[i] = e->unmount;
            else
                options[i] = e->mount;
        }

        for (i = 0; i < formatable_volumes; i++)
        {
            FormatMenuEntry* e = &format_menu[i];

            options[mountable_volumes+i] = e->txt;
        }

        if (!is_data_media()) {
          options[mountable_volumes + formatable_volumes] = "mount USB storage";
          options[mountable_volumes + formatable_volumes + 1] = NULL;
        }
        else {
          options[mountable_volumes + formatable_volumes] = "format /data and /data/media (/sdcard)";
          options[mountable_volumes + formatable_volumes + 1] = NULL;
        }

        int chosen_item = get_menu_selection(headers, &options, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        if (chosen_item == (mountable_volumes+formatable_volumes)) {
            if (!is_data_media()) {
                show_mount_usb_storage_menu();
            }
            else {
                if (!confirm_selection("format /data and /data/media (/sdcard)", confirm))
                    continue;
                ignore_data_media_workaround(1);
                ui_print("Formatting /data...\n");
                if (0 != format_volume("/data"))
                    ui_print("Error formatting /data!\n");
                else
                    ui_print("Done.\n");
                ignore_data_media_workaround(0);
            }
        }
        else if (chosen_item < mountable_volumes) {
            MountMenuEntry* e = &mount_menu[chosen_item];
            Volume* v = e->v;

            if (is_path_mounted(v->mount_point))
            {
                if (0 != ensure_path_unmounted(v->mount_point))
                    ui_print("Error unmounting %s!\n", v->mount_point);
            }
            else
            {
                if (0 != ensure_path_mounted(v->mount_point))
                    ui_print("Error mounting %s!\n",  v->mount_point);
            }
        }
        else if (chosen_item < (mountable_volumes + formatable_volumes))
        {
            chosen_item = chosen_item - mountable_volumes;
            FormatMenuEntry* e = &format_menu[chosen_item];
            Volume* v = e->v;

            sprintf(confirm_string, "%s - %s", v->mount_point, confirm_format);

            if (!confirm_selection(confirm_string, confirm))
                continue;
            ui_print("Formatting %s...\n", v->mount_point);
            if (0 != format_volume(v->mount_point))
                ui_print("Error formatting %s!\n", v->mount_point);
            else
                ui_print("Done.\n");
        }
    }

    free(mount_menu);
    free(format_menu);
}

void show_nandroid_advanced_backup_menu(const char *path, int other_sd)
{
	if (ensure_path_mounted(path) != 0) {
		LOGE ("Can't mount %s\n", path);
		return;
	}

	static char* advancedheaders[] = { "Choose the partitions to backup.",
					NULL
    };
    
    int backup_list [7];
    char* list[7];
    
    backup_list[0] = 1;
    backup_list[1] = 1;
    backup_list[2] = 1;
    backup_list[3] = 1;
    backup_list[4] = 1;
    backup_list[5] = NULL;
    
    list[5] = "Perform Backup";
    list[6] = NULL;
    
    int cont = 1;
    for (;cont;) {
		if (backup_list[0] == 1)
			list[0] = "Backup boot: Yes";
		else
			list[0] = "Backup boot: No";
			
		if (backup_list[1] == 1)
	    	list[1] = "Backup recovery: Yes";
	    else
	    	list[1] = "Backup recovery: No";
	    	
	    if (backup_list[2] == 1)
    		list[2] = "Backup system: Yes";
	    else
	    	list[2] = "Backup system: No";

	    if (backup_list[3] == 1)
	    	list[3] = "Backup data: Yes";
	    else
	    	list[3] = "Backup data: No";

	    if (backup_list[4] == 1)
	    	list[4] = "Backup cache: Yes";
	    else
	    	list[4] = "Backup cache: No";
	    	
	    int chosen_item = get_menu_selection (advancedheaders, list, 0, 0);
	    switch (chosen_item) {
			case GO_BACK: return;
			case 0: backup_list[0] = !backup_list[0];
				break;
			case 1: backup_list[1] = !backup_list[1];
				break;
			case 2: backup_list[2] = !backup_list[2];
				break;
			case 3: backup_list[3] = !backup_list[3];
				break;
			case 4: backup_list[4] = !backup_list[4];
				break;	
		   
			case 5: cont = 0;
				break;
		}
	}
	
	nandroid_generate_timestamp_path(path, other_sd);
	return nandroid_advanced_backup(path, backup_list[0], backup_list[1], backup_list[2], backup_list[3], backup_list[4], backup_list[5]);
}

void show_nandroid_advanced_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE ("Can't mount %s\n", path);
        return;
    }

    static char* advancedheaders[] = {  "Choose an image to restore",
                                "",
                                "Choose an image to restore",
                                "first. The next menu will",
                                "show you more options.",
                                "",
                                NULL
    };

    char* file = choose_file_menu(path, NULL, advancedheaders);
    if (file == NULL)
        return;

    static char* headers[] = {  "Advanced Restore",
                                "",
                                NULL
    };

    static char* list[] = { "Restore boot",
                            "Restore system",
                            "Restore data",
                            "Restore cache",
                            "Restore sd-ext",
                            "Restore wimax",
                            NULL
    };
    
    if (0 != get_partition_device("wimax", path)) {
        // disable wimax restore option
        list[5] = NULL;
    }

    static char* confirm_restore  = "Confirm restore?";

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item)
    {
        case 0:
            if (confirm_selection(confirm_restore, "Yes - Restore boot"))
                nandroid_restore(file, 1, 0, 0, 0, 0, 0);
            break;
        case 1:
            if (confirm_selection(confirm_restore, "Yes - Restore system"))
                nandroid_restore(file, 0, 1, 0, 0, 0, 0);
            break;
        case 2:
            if (confirm_selection(confirm_restore, "Yes - Restore data"))
                nandroid_restore(file, 0, 0, 1, 0, 0, 0);
            break;
        case 3:
            if (confirm_selection(confirm_restore, "Yes - Restore cache"))
                nandroid_restore(file, 0, 0, 0, 1, 0, 0);
            break;
        case 4:
            if (confirm_selection(confirm_restore, "Yes - Restore sd-ext"))
                nandroid_restore(file, 0, 0, 0, 0, 1, 0);
            break;
        case 5:
            if (confirm_selection(confirm_restore, "Yes - Restore wimax"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 1);
            break;
    }
}

static void choose_default_backup_format() {
    static char* headers[] = {  "Default Backup Format",
                                "",
                                NULL
    };

    int fmt = nandroid_get_default_backup_format();

    char **list;
    char* list_tar_default[] = { "tar (default)",
                                 "dup",
                                 "tar + gzip",
                                 NULL };
    char* list_dup_default[] = { "tar",
                                 "dup (default)",
                                 "tar + gzip",
                                 NULL };
    char* list_tgz_default[] = { "tar",
                                 "dup",
                                 "tar + gzip (default)",
                                 NULL };

    if (fmt == NANDROID_BACKUP_FORMAT_DUP) {
        list = list_dup_default;
    } else if (fmt == NANDROID_BACKUP_FORMAT_TGZ) {
        list = list_tgz_default;
    } else {
        list = list_tar_default;
    }

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item) {
        case 0:
            write_string_to_file(NANDROID_BACKUP_FORMAT_FILE, "tar");
            ui_print("Default backup format set to tar.\n");
            break;
        case 1:
            write_string_to_file(NANDROID_BACKUP_FORMAT_FILE, "dup");
            ui_print("Default backup format set to dedupe.\n");
            break;
        case 2:
            write_string_to_file(NANDROID_BACKUP_FORMAT_FILE, "tgz");
            ui_print("Default backup format set to tar + gzip.\n");
            break;
    }
}

void show_nandroid_advanced_menu()
{
    static char* headers[] = {  "Advanced Backup and Restore",
                                "",
                                NULL
    };

    char* list[] = {"Advanced Backup",
                            "Advanced Restore",
                            NULL,
                            NULL,
                            NULL
    };
    
    char *other_sd = NULL;
    if(EXTRA_SDCARD == EMMC) {
        list[2] = "Advanced backup to internal_sd";
        list[3] = "Advanced restore from internal_sd";
        other_sd = "/emmc";
    }
    else if (EXTRA_SDCARD == EXTERNALSD) {        
        list[2] = "Advanced backup to external_sd";
        list[3] = "Advanced restore from external_sd";
        other_sd = "/external_sd";
    }	
#ifdef RECOVERY_EXTEND_NANDROID_MENU
    extend_nandroid_menu(list, 4, sizeof(list) / sizeof(char*));
#endif

    for (;;) {
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
		switch (chosen_item)
        	{
			char backup_path[PATH_MAX];	
			case 0:
                nandroid_get_backup_path(backup_path, 0);
				show_nandroid_advanced_backup_menu(backup_path, 0);
                break;
            case 1:
				nandroid_get_backup_path(backup_path, 0);
                show_nandroid_advanced_restore_menu(backup_path);
                break;
            case 2:
                if (other_sd != NULL) {
                    nandroid_get_backup_path(backup_path, 1);
					show_nandroid_advanced_backup_menu(backup_path, 1);
                }
                break;
            case 3:
                if (other_sd != NULL) {
                    nandroid_get_backup_path(backup_path, 1);
                    show_nandroid_advanced_restore_menu(backup_path);
                }
                break;
            default:
#ifdef RECOVERY_EXTEND_NANDROID_MENU
                handle_nandroid_menu(4, chosen_item);
#endif
                break;    
          }
    }
    
}

void show_nandroid_menu()
{
    static char* headers[] = {  "Backup and Restore",
                                "",
                                NULL
    };

    char* list[] = { "Backup",
                            "Restore",
                            "Delete Old Backups",
                            "Advanced Backup Restore",
                            "Default backup format",
                            "Toggle MD5 Verification",
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL
    };
    
    char *other_sd = NULL;
    if(EXTRA_SDCARD == EMMC) {
		list[6] = "Backup to internal sdcard";
		list[7] = "Restore from internal sdcard";
		list[8] = "Delete from internal sdcard";
		if(nandroid_get_default_backup_format() == NANDROID_BACKUP_FORMAT_DUP) 
              list[9] = "Free Unused Old Data";
		other_sd = "/emmc";
	} else if (EXTRA_SDCARD == EXTERNALSD) {
		list[6] = "Backup to external sdcard";
		list[7] = "Restore from external sdcard";
		list[8] = "Delete from external sdcard";
		if(nandroid_get_default_backup_format() == NANDROID_BACKUP_FORMAT_DUP) 
              list[9] = "Free Unused Old Data";
		other_sd = "/external_sd";
    }
#ifdef RECOVERY_EXTEND_NANDROID_MENU
    extend_nandroid_menu(list, 10, sizeof(list) / sizeof(char*));
#endif

    for (;;) {
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
			char backup_path[PATH_MAX];
            case 0:
                {
					nandroid_generate_timestamp_path(backup_path, 0);
                    nandroid_backup(backup_path);
					break;
                }
            case 1:
                {
					nandroid_get_backup_path(backup_path, 0);
                	show_nandroid_restore_menu(backup_path);
                	break;
				}
            case 2:
                {
					nandroid_get_backup_path(backup_path, 0);
                	show_nandroid_delete_menu(backup_path);
                	break;
				}
            case 3:
                show_nandroid_advanced_menu();
                break;
            case 4:
                choose_default_backup_format();
                break;
            case 5:
                toggle_md5_check();
                break;
            case 6:
                {
                    nandroid_generate_timestamp_path(backup_path, 1);
                    nandroid_backup(backup_path);
					break;
                }
            case 7:
                if (other_sd != NULL) {
                    nandroid_get_backup_path(backup_path, 1);
                    show_nandroid_restore_menu(backup_path);
                }
                break;
            case 8:
                if (other_sd != NULL) {
					nandroid_get_backup_path(backup_path, 1);
                    show_nandroid_delete_menu(backup_path);
                }
                break;
            case 9:
                run_dedupe_gc(other_sd);
                break;    
                    
            default:
#ifdef RECOVERY_EXTEND_NANDROID_MENU
                handle_nandroid_menu(10, chosen_item);
#endif
                break;
        }
    }
}

void partition_sdcard(const char* volume) {
    if (!can_partition(volume)) {
        ui_print("Can't partition device: %s\n", volume);
        return;
    }

    static char* ext_sizes[] = { "128M",
                                 "256M",
                                 "512M",
                                 "1024M",
                                 "2048M",
                                 "4096M",
                                 NULL };

    static char* swap_sizes[] = { "0M",
                                  "32M",
                                  "64M",
                                  "128M",
                                  "256M",
                                  NULL };

    static char* partition_types[] = { "ext3",
                                       "ext4",
                                       NULL
    };

    static char* ext_headers[] = { "Ext Size", "", NULL };
    static char* swap_headers[] = { "Swap Size", "", NULL };
    static char* fstype_headers[] = {"Partition Type", "", NULL };

    int ext_size = get_menu_selection(ext_headers, ext_sizes, 0, 0);
    if (ext_size == GO_BACK)
        return;

    int swap_size = get_menu_selection(swap_headers, swap_sizes, 0, 0);
    if (swap_size == GO_BACK)
        return;

    int partition_type = get_menu_selection(fstype_headers, partition_types, 0, 0);
    if (partition_type == GO_BACK)
        return;

    char sddevice[256];
    Volume *vol = volume_for_path(volume);
    strcpy(sddevice, vol->device);
    // we only want the mmcblk, not the partition
    sddevice[strlen("/dev/block/mmcblkX")] = NULL;
    char cmd[PATH_MAX];
    setenv("SDPATH", sddevice, 1);
    sprintf(cmd, "sdparted -es %s -ss %s -efs %s -s", ext_sizes[ext_size], swap_sizes[swap_size], partition_types[partition_type]);
    ui_print("Partitioning SD Card... please wait...\n");
    if (0 == __system(cmd))
        ui_print("Done!\n");
    else
        ui_print("An error occured while partitioning your SD Card. Please see /tmp/recovery.log for more details.\n");
}

int can_partition(const char* volume) {
    Volume *vol = volume_for_path(volume);
    if (vol == NULL) {
        LOGI("Can't format unknown volume: %s\n", volume);
        return 0;
    }

    int vol_len = strlen(vol->device);
    // do not allow partitioning of a device that isn't mmcblkX or mmcblkXp1
    if (vol->device[vol_len - 2] == 'p' && vol->device[vol_len - 1] != '1') {
        LOGI("Can't partition unsafe device: %s\n", vol->device);
        return 0;
    }
    
    if (strcmp(vol->fs_type, "vfat") != 0) {
        LOGI("Can't partition non-vfat: %s\n", vol->fs_type);
        return 0;
    }

    return 1;
}

void show_rainbow_menu()
{
    static char* headers[] = {  "Rainbow Mode",
                                "",
                                NULL
    };

    static char* list[] = { "Rainbow Enabled",
                            "Rainbow Disabled",
                            NULL
    };

    for (;;)
    {
		int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
		switch (chosen_item)
        	{
			case 0:
                ui_set_rainbow_mode(1);
                ui_print("Rainbow mode enabled!\n");
                break;  
             case 1:
                ui_set_rainbow_mode(0);
                ui_print("Rainbow mode disabled\n");
                break;
        }
    }
}

void show_nvram_menu()
{
    static char* headers[] = {  "Nvram Backup/Restore",
                                "",
                                NULL
    };

    char* list[] = {"Backup Nvram",
                            "Restore Nvram",
                            NULL,
                            NULL,
                            NULL
    };
    
    char *other_sd = NULL;
    if(EXTRA_SDCARD == EMMC) {
        list[2] = "Backup Nvram to internal_sd";
        list[3] = "Restore Nvram from internal_sd";
        other_sd = "/emmc";
    }
    else if (EXTRA_SDCARD == EXTERNALSD) {        
        list[2] = "Backup Nvram to external_sd";
        list[3] = "Restore Nvram from external_sd";
        other_sd = "/external_sd";
    }	

    for (;;) {
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
		switch (chosen_item)
        	{
			char backup_path[PATH_MAX];	
			case 0:
                nandroid_get_backup_path(backup_path, 0);
				nvram_backup(backup_path);
                break;
            case 1:
				nandroid_get_backup_path(backup_path, 0);
                nvram_restore(backup_path);
                break;
            case 2:
                if (other_sd != NULL) {
                    nandroid_get_backup_path(backup_path, 1);
					nvram_backup(backup_path);
                }
                break;
            case 3:
                if (other_sd != NULL) {
                    nandroid_get_backup_path(backup_path, 1);
                    nvram_restore(backup_path);
                }
                break;
            default:
                break;    
          }
    }
    
}

static void choose_aromafm_menu(const char* aromafm_path)
{
    if (ensure_path_mounted(aromafm_path) != 0) {
        LOGE("Can't mount %s\n", aromafm_path);
        return;
    }

    static char* headers[] = {  "Browse for aromafm.zip",
                                NULL
    };

    char* aroma_file = choose_file_menu(aromafm_path, "aromafm.zip", headers);
    if (aroma_file == NULL)
        return;
    static char* confirm_install  = "Confirm Run Aroma?";
    static char confirm[PATH_MAX];
    sprintf(confirm, "Yes - Run %s", basename(aroma_file));
    if (confirm_selection(confirm_install, confirm)) {
        install_zip(aroma_file);
    }
}

//Show custom aroma menu: manually browse sdcards for Aroma file manager
static void custom_aroma_menu() {
    static char* headers[] = {  "Browse for aromafm.zip",
                                "",
                                NULL
    };

    static char* list[] = { "Search sdcard",
                            NULL,
                            NULL
    };

    char *other_sd = NULL;
    if (volume_for_path("/emmc") != NULL) {
        other_sd = "/emmc/";
        list[1] = "Search Internal sdcard";
    } else if (volume_for_path("/external_sd") != NULL) {
        other_sd = "/external_sd/";
        list[1] = "Search External sdcard";
    }

    for (;;) {
        //header function so that "Toggle menu" doesn't reset to main menu on action selected
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            case 0:
                choose_aromafm_menu("/sdcard/");
                break;
            case 1:
                choose_aromafm_menu(other_sd);
                break;
        }
    }
}

//launch aromafm.zip from default locations
static int default_aromafm (const char* aromafm_path) {
        if (ensure_path_mounted(aromafm_path) != 0) {
            //no sdcard at moint point
            return 0;
        }
        char aroma_file[PATH_MAX];
        sprintf(aroma_file, "%s/clockworkmod/.aromafm/aromafm.zip", aromafm_path);
        if (access(aroma_file, F_OK) != -1) {
            install_zip(aroma_file);
            return 1;
        }
        return 0;
}

void show_carliv_menu()
{

    static char* headers[] = {  "Carliv Menu",
								"",
								NULL
    };

    char* list[] = { "Aroma File Manager",
                            "Rainbow Mode",
                            "About",	 	 
                             NULL,
                             NULL
    };
    
    if (volume_for_path("/nvram") != NULL) {
        list[3] = "Nvram Backup/Restore";
    }

    for (;;)
    {
		int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
		switch (chosen_item)
        	{
			case 0:
                //we mount sdcards so that they can be accessed when in aroma file manager gui
                if (volume_for_path("/sdcard") != NULL) {
                    ensure_path_mounted("/sdcard");
                }
                if (volume_for_path("/external_sd") != NULL) {
                    ensure_path_mounted("/external_sd");
                } else if (volume_for_path("/emmc") != NULL) {
                    ensure_path_mounted("/emmc");
                }
                //look for clockworkmod/.aromafm/aromafm.zip in /external_sd, then /sdcard and finally /emmc
                if (volume_for_path("/external_sd") != NULL) {
                    if (default_aromafm("/external_sd")) {
                        break;
                    }
                }
                if (volume_for_path("/sdcard") != NULL) {
                    if (default_aromafm("/sdcard")) {
                        break;
                    }
                }
                if (volume_for_path("/emmc") != NULL) {
                    if (default_aromafm("/emmc")) {
                        break;
                    }
                }
                ui_print("No clockworkmod/.aromafm/aromafm.zip on sdcards\n");
                ui_print("Browsing custom locations\n");
                custom_aroma_menu();
                break;  
             case 1:
                show_rainbow_menu();
                break;                
             case 2:
                ui_print("This is a CWM Recovery modified by carliv from xda with Clockworkmod version 6 base.\n");
                ui_print("With full touch support module developed by Napstar-xda from UtterChaos Team, adapted and modified by carliv.\n");
                ui_print("For Aroma File Manager is recommended version 1.80 - Calung, from amarullz xda thread, because it has a full touch support in most of devices.\n");
                ui_print("Thank you all!\n");
                ui_print("\n");
                break;
             case 3:
                show_nvram_menu();
                break;    
        }
    }
    
}

void show_advanced_menu()
{
    static char* headers[] = {  "Advanced Menu",
                                "",
                                NULL
    };

    static char* list[] = { "report error",
                            "key test",
                            "show log",
                            "Toggle touch control",
                            "Instructions for touch control",                            
                            "partition sdcard",
                            "partition external sdcard",
                            "partition internal sdcard",
                            NULL
    };
    
    if (!can_partition("/sdcard")) {
        list[5] = NULL;
    }
    if (!can_partition("/external_sd")) {
        list[6] = NULL;
    }
    if (!can_partition("/emmc")) {
        list[7] = NULL;
    }

    for (;;)
    {
        int chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            case 0:
                handle_failure(1);
                break;
            case 1:
            {
				ui_print("Outputting key codes.\n");
				ui_print("Go back to end debugging.\n");
				struct keyStruct{
					int code;
					int x;
					int y;
					int length;
					int Xlength;
				}*key;
                int action;
                do
                {
                    key = ui_wait_key();
					if(key->code == ABS_MT_POSITION_X)
					{
				        action = device_handle_mouse(key, 1);
						ui_print("Touch: X: %d\tY: %d\n", key->x, key->y);
					}
					else if(key->code == KEY_SCROLLDOWN)			//Gesture inputs: Helpful to come out of key test for devices with less hardware keys
					{
						action = GO_BACK;
						ui_print("Gesture Touch: (KEY_SCROLLDOWN) Code: %d\n", key->code);
					}
					else if(key->code == KEY_SCROLLUP)
					{
						ui_print("Gesture Touch: (KEY_SCROLLUP) Code: %d\n", key->code);
					}
					else
					{
				        action = device_handle_key(key->code, 1);
						ui_print("Key: %x\n", key->code);
					}
                }
                while (action != GO_BACK);
                break;
			}
            case 2:
                ui_printlogtail(12);
                break;
            case 3:
				toggle_touch_control_menu();
                break;   
            case 4:
                ui_print("=================================\n");
                ui_print("For full touch control, tap on desired menu button. For Go back swipe to the left. For Scroll UP and Scroll Down in long menu pages, Swipe to Right for Page Down, and Swipe to Left for Page UP, and on top of the menu Swipe Left for Go Back.\n");
                ui_print("\n");
                break;  
            case 5:
                partition_sdcard("/sdcard");
                break;
            case 6:
                partition_sdcard("/external_sd");
                break;
            case 7:
                partition_sdcard("/emmc");
                break;
        }
    }
}

void write_fstab_root(char *path, FILE *file)
{
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGW("Unable to get recovery.fstab info for %s during fstab generation!\n", path);
        return;
    }

    char device[200];
    if (vol->device[0] != '/')
        get_partition_device(vol->device, device);
    else
        strcpy(device, vol->device);

    fprintf(file, "%s ", device);
    fprintf(file, "%s ", path);
    // special case rfs cause auto will mount it as vfat on samsung.
    fprintf(file, "%s rw\n", vol->fs_type2 != NULL && strcmp(vol->fs_type, "rfs") != 0 ? "auto" : vol->fs_type);
}

void create_fstab()
{
    struct stat info;
    __system("touch /etc/mtab");
    FILE *file = fopen("/etc/fstab", "w");
    if (file == NULL) {
        LOGW("Unable to create /etc/fstab!\n");
        return;
    }
    Volume *vol = volume_for_path("/boot");
    if (NULL != vol && strcmp(vol->fs_type, "mtd") != 0 && strcmp(vol->fs_type, "emmc") != 0 && strcmp(vol->fs_type, "bml") != 0)
         write_fstab_root("/boot", file);
    write_fstab_root("/cache", file);
    write_fstab_root("/data", file);
    write_fstab_root("/datadata", file);
    write_fstab_root("/emmc", file);
    write_fstab_root("/system", file);
    write_fstab_root("/sdcard", file);
    write_fstab_root("/sd-ext", file);
    write_fstab_root("/external_sd", file);
    fclose(file);
    LOGI("Completed outputting fstab.\n");
}

int bml_check_volume(const char *path) {
    ui_print("Checking %s...\n", path);
    ensure_path_unmounted(path);
    if (0 == ensure_path_mounted(path)) {
        ensure_path_unmounted(path);
        return 0;
    }
    
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGE("Unable process volume! Skipping...\n");
        return 0;
    }
    
    ui_print("%s may be rfs. Checking...\n", path);
    char tmp[PATH_MAX];
    sprintf(tmp, "mount -t rfs %s %s", vol->device, path);
    int ret = __system(tmp);
    printf("%d\n", ret);
    return ret == 0 ? 1 : 0;
}

void process_volumes() {
    create_fstab();

    if (is_data_media()) {
        setup_data_media();
    }

    return;

    // dead code.
    if (device_flash_type() != BML)
        return;

    ui_print("Checking for ext4 partitions...\n");
    int ret = 0;
    ret = bml_check_volume("/system");
    ret |= bml_check_volume("/data");
    if (has_datadata())
        ret |= bml_check_volume("/datadata");
    ret |= bml_check_volume("/cache");
    
    if (ret == 0) {
        ui_print("Done!\n");
        return;
    }
    
    char backup_path[PATH_MAX];
    time_t t = time(NULL);
    char backup_name[PATH_MAX];
    struct timeval tp;
    gettimeofday(&tp, NULL);
    sprintf(backup_name, "before-ext4-convert-%d", tp.tv_sec);
    struct stat st;

	char base_path[PATH_MAX];
	nandroid_get_backup_path(base_path, 0);

    ui_set_show_text(1);
    ui_print("Filesystems need to be converted to ext4.\n");
    ui_print("A backup and restore will now take place.\n");
    ui_print("If anything goes wrong, your backup will be\n");
    ui_print("named %s. Try restoring it\n", backup_name);
    ui_print("in case of error.\n");

    nandroid_backup(backup_path);
    nandroid_restore(backup_path, 1, 1, 1, 1, 1, 0);
    ui_set_show_text(0);
}

void handle_failure(int ret)
{
    if (ret == 0)
        return;
        
    char tmp[PATH_MAX];
    if(EXTRA_SDCARD == EMMC) {
        if(0 != ensure_path_mounted("/emmc")) {
            ui_print("Can't mount /emmc.\n");
            return -1;
        }
        strcpy(tmp, "/emmc/clockworkmod" );
    } else if(EXTRA_SDCARD == EXTERNALSD) {
        if(0 != ensure_path_mounted("/external_sd")) {
            ui_print("Can't mount /external_sd.\n");
            return -1;
        }
        strcpy(tmp, "/external_sd/clockworkmod" );
    } else {
        if(0 != ensure_path_mounted("/sdcard")) {
            ui_print("Can't mount /sdcard.\n");
            return -1;
        }
        strcpy(tmp, "/sdcard/clockworkmod");
    }

    mkdir(tmp, S_IRWXU | S_IRWXG | S_IRWXO);

    char cmd[PATH_MAX];
	sprintf(cmd, "cp /tmp/recovery.log %s/recovery.log", tmp);
    __system(cmd);

    ui_print("/tmp/recovery.log was copied to /your_sdcard/clockworkmod/recovery.log. Please report the issue to recovery thread where you found it.\n");
}

int is_path_mounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        return 0;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return 0;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv) {
        // volume is already mounted
        return 1;
    }
    return 0;
}

int has_datadata() {
    Volume *vol = volume_for_path("/datadata");
    return vol != NULL;
}

int volume_main(int argc, char **argv) {
    load_volume_table();
    return 0;
}

int verify_root_and_recovery() {
    if (ensure_path_mounted("/system") != 0)
        return 0;

    int ret = 0;
    struct stat st;
    // check to see if install-recovery.sh is going to clobber recovery
    // install-recovery.sh is also used to run the su daemon on stock rom for 4.3+
    // so verify that doesn't exist...
    if (0 != lstat("/system/etc/.installed_su_daemon", &st)) {
        // check install-recovery.sh exists and is executable
        if (0 == lstat("/system/etc/install-recovery.sh", &st)) {
            if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
                ui_show_text(1);
                ret = 1;
                if (confirm_selection("ROM may flash stock recovery on boot. Fix?", "Yes - Disable recovery flash")) {
                    __system("chmod -x /system/etc/install-recovery.sh");
                }
            }
        }
    }


    int exists = 0;
    if (0 == lstat("/system/bin/su", &st)) {
        exists = 1;
        if (S_ISREG(st.st_mode)) {
            if ((st.st_mode & (S_ISUID | S_ISGID)) != (S_ISUID | S_ISGID)) {
                ui_show_text(1);
                ret = 1;
                if (confirm_selection("Root access possibly lost. Fix?", "Yes - Fix root (/system/bin/su)")) {
                    __system("chmod 6755 /system/bin/su");
                }
            }
        }
    }

    if (0 == lstat("/system/xbin/su", &st)) {
        exists = 1;
        if (S_ISREG(st.st_mode)) {
            if ((st.st_mode & (S_ISUID | S_ISGID)) != (S_ISUID | S_ISGID)) {
                ui_show_text(1);
                ret = 1;
                if (confirm_selection("Root access possibly lost. Fix?", "Yes - Fix root (/system/xbin/su)")) {
                    __system("chmod 6755 /system/xbin/su");
                }
            }
        }
    }

    if (!exists) {
        ui_show_text(1);
        ret = 1;
        if (confirm_selection("Root access is missing. Root device?", "Yes - Root device (/system/xbin/su)")) {
            __system("/sbin/install-su.sh");
        }
    }

    ensure_path_unmounted("/system");
    return ret;
}
