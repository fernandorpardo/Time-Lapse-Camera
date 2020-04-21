## Time-Lapse Camera

Time-Lapse Camera for Raspberry Pi
TLCAM is part of the Wilson project (wwww.iambobot.com)

### DESCRIPTION

TLCAM converts a Raspberry into a Time-Lapse Camera by plugging an USB camera or connecting other kind of V4L device.

TLCAM does:
- capture images from a V4L device (USB camera), either directly as JPEG or as YUYV, and in the latter case, compress the capture to JPEG
- store the JPEG images into files at /var/www/ramdisk (#define IMAGE_STORAGE_PATH), where ramdisk is the mounting point an adhoc created RAM disk.
- display the images as they are captured, when 'display' option is selected, by copying the capture memory buffer into the frame buffer. When the capture is JPEG it is first decompressed and then copied.

This SW has been developed and tested in a Raspberry Pi Zero. Nothing prevents it from running on other Linux platforms. 

TLCAM WEB app is the companion app to play the images captured by TLCAM as motion pictures. Apache WEB server needs to be installed on the Raspberry to host the application. Alternatively, images can be uploaded to an external server. This feature needs an extension not published yet.

TLCAM allows multiple players to fetch the images and display the video simultaneously.

SW is not limited to one single camera although it has not been tested and would probably need a few changes to capture more than one at a time.


### REQUISITES
You need to create a directory to store the capture files. It is recommended you create a RAM disk for better performance.
In case you need guidance about how to create the RAM disk check instructions at www.iambobot.com
Change the #define IMAGE_STORAGE_PATH in the .h file to point to your storage.


### USAGE
Call TLCAM with the capture time period in milliseconds
e.g.: “tlcam 100” captures 10 images per second. Limit is up to HW performance: camera and HW platform (Raspberry).
Default working mode is QVGA (340 x 240) and MPEJ, when supported. Use parameter ‘vga’ to change resolution to (640 x 480).
Type “tlcam” to see usage information. 

### LIMITATIONS
Resolutions currently supported are VGA (640 x 480) and QVGA (340 x 240).
YUV option needs some debugging.

### REFERENCES
V4L capture code is based on the SW published by Jay Rambhia (https://gist.github.com/jayrambhia/5866483)
JPEG decompression is based on the example by 
Kenneth Finnegan - A bare-bones example of how to use jpeglib to decompress a jpg in memory. (https://gist.github.com/PhirePhly/3080633)

