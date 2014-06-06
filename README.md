My modded version of CWM Recovery for phones with MTK CPU. Use it with CM-10.1 building environment.

The recovery supports now compressed backups (tar.gzip), and if you want to compile it you need to download android_external_pigz repo (forked from Cyanogenmod 10.2 because 10.1 doesn't have it); extract it in your cm-10.1 directory in external folder, then rename the extracted folder from "android_external_pigz" to "pigz". I tried to use the method with pigz folder inside recovery, like in twrp, but that didn't worked flawless. I had to revert even the Bsydz version ported from Philz recovery because of the same reason. Those methods work most of the part (backup, restore...), but in special tasks, like advanced backup or restore they fail because the symlinks aren't properly implemented. 
Usind my method with external pigz, works like it should in every case, and the symlinks are correct setted during compilation.

Also, you need to define a font size and device resolution in your device BoardConfig. Try to use a font size good for your resolution. The options are: font_7x16.h, font_10x18.h, roboto_10x18.h, roboto_15x24.h, roboto_23x41.h

```
BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_15x24.h\"
DEVICE_RESOLUTION := 540x960
TARGET_SCREEN_WIDTH := 540
TARGET_SCREEN_HEIGHT := 960

```

Enjoy!
