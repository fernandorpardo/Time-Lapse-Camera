# Time-Lapse Camera

Time-Lapse Camera (TLCAM) is part of the Wilson project (www.iambobot.com)

## DESCRIPTION

TLCAM converts a Raspberry Pi with an USB camera or a Pi Camera into a Time-Lapse Camera.

TLCAM does:
- capture images from a V4L device, either directly as JPEG or as YUYV, and in the latter case, compress the capture to JPEG
- store the JPEG image files at /var/www/ramdisk (#define IMAGE_STORAGE_PATH), where `ramdisk` is the mounting point of an adhoc created RAM disk.
- display the images as they are captured, when 'display' option is selected, by copying the capture memory buffer into the frame buffer. When the capture is JPEG it is first decompressed and then copied.

This SW has been developed and tested in a Raspberry Pi Zero. Nothing prevents it from running on other Linux platforms. 

[TLCAM WEB app](https://github.com/fernandorpardo/Time-Lapse-Camera-WEB-app) is the companion app to play the images captured by TLCAM as motion pictures. Apache WEB server needs to be installed on the Raspberry to host the application. 
Alternatively, images can be uploaded to an external server using option `cloud`.

TLCAM allows multiple players to fetch the images and display the video simultaneously.

SW is not limited to one single camera although it has not been tested and would probably need a few changes to capture more than one at a time.


## REQUISITES
You need to create a directory to store the capture files. It is recommended you create a RAM disk for better performance.

In case you need guidance about how to create the RAM disk check instructions at www.iambobot.com

Change the #define IMAGE_STORAGE_PATH in the .h file to point to your storage.


## USAGE
Call TLCAM with the capture time period in milliseconds

e.g.: “tlcam 100” captures 10 images per second. 

```console
tlcam 100
```
Capture rate limit depends on HW performance, of both, camera and HW platform (Raspberry).

Default working mode is VGA (640 x 480) and MPEJ, when supported.

Type “tlcam” to see usage information. 

```console
$ tlcam
Time Lapse Camera version 01.01.00-2020.11.15-194946
Usage:
tlcam <time> [--command] [options]
   time      - capture period in miliseconds (<=100 recommended)
commands are:
   --info    - shows camera information
Options are:
   videoX    - select camera driver /dev/videoX. Default is video0
   qvga      - set QVGA capture(320x240)
   vga       - set VGA capture (640x480) (default)
   svga      - set Super-VGA capture (800x600)
   hd        - set high definition capture (1080x720)
   jpeg      - requests the camera to capture JPEG encoded images
   mjpg      - requests the camera to capture MJPG encoded images (default)
   yuyv      - requests the camera to capture YUYV encoded images
   display   - output captured image into the framebuffer (HDMI output)
   noverbose - stop console output
   agent     - runs silently: set noverbose and disable kbhit
   cloud     - upload image to cloud host instead of local camera storage (default is local)

example:
   tlcam 100
   tlcam 100 yuyv vga
   tlcam 100 agent
```

## LIMITATIONS
Resolutions currently supported are HD (1080 x 720), SVGA (800 x 600), VGA (640 x 480) and QVGA (340 x 240).

YUV option needs some debugging.

## REFERENCES
- V4L capture code is based on the SW published by [Jay Rambhia](https://gist.github.com/jayrambhia/5866483)
- JPEG decompression is based on the example by Kenneth Finnegan - [A bare-bones example of how to use jpeglib to decompress a jpg in memory](https://gist.github.com/PhirePhly/3080633)
