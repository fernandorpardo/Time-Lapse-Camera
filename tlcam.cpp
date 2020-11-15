/** ************************************************************************************************
* Time Lapse Camera
* Fernando R
*
* TLCAM is part of the Wilson project (wwww.iambobot.com)
*
* DESCRIPTION
* TLCAM does:
*  - capture images from a V4L device (USB camera or Pi camera), either directly as JPEG or as YUYV, and in the latter case 
*    compress to JPEG
*  - store the JPEG images into files in /var/www/ramdisk (IMAGE_STORAGE_PATH), where ramdisk is 
*    an adhoc created RAM storage
*  - display the images as they are captured, when 'display' option is selected, by copying 
*    the capture memory buffer into the frame buffer 
*
* When running the TLCAM in a device with an APACHE server, a companion WEB app can be used to play the images 
* as 'real-time live video', so to speak. 
* TLCAM allows multiple players to fetch the images and display the video simultaneously.
* SW is not limited to one single camera although needs a few changes to capture more than one at a time. 
*
* REQUISITES
* You need to create a directory to store the capture files. It is reconmended you create a RAM disk for performance.
* In case you need guidance about how to create the RAM disk check instructions at www.iambobot.com.
* Change the #define IMAGE_STORAGE_PATH in the .h file to point to your storage.
*
* LIMITATIONS
* - YUYV display needs some debugging
*
* VERSION 
* (See version.cpp)
* 01.00.00 - release candidate	
* 01.01.00 - PI camera support 
*			 cloud hosting (image upload)
*            selectable /dev/video device (multiple cameras)
*
* REFERENCES
*	This SW use code from 
*		- Kenneth Finnegan - A bare-bones example of how to use jpeglib to decompress a jpg in memory. (https://gist.github.com/PhirePhly/3080633)
*		- Jay Rambhia (https://gist.github.com/jayrambhia/5866483)
*		- https://stackoverflow.com/questions/4491649/how-to-convert-yuy2-to-a-bitmap-in-c
*		- http://stackoverflow.com/questions/17029136/weird-image-while-trying-to-compress-yuv-image-to-jpeg-using-libjpeg
*
* **************************************************************************************************
**/

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <jpeglib.h>    
#include <jerror.h>
#include <iostream>
#include <fstream>
#include <vector>
using namespace std;
#include <linux/fb.h> // frame buffer
#include <cmath>

#include "HTTPpost.h"
#include "glib.h"
#include "tlcam.h"

char *version(char *str, size_t max_sz);
const char fulldatafilename[] =IMAGE_STORAGE_PATH DATA_FILE;	
CaptureResolution *CapResolution;

// Memory buffer to store the JPEG decompressed image
// Keep a permanet buffer and resize as needed and 
// avoid calling malloc and free for each image
// called from: JPEG_decompress
unsigned char *gmemptr;
size_t gmemsize;
unsigned char *gmemalloc(size_t sz)
{
	if(sz>gmemsize)
	{
		fprintf(stdout, "*** gmemalloc malloc %d\n", sz);
		if(gmemptr) free(gmemptr);
		gmemptr= (unsigned char *) malloc( sizeof(char) * sz + 1024 );
		if(!gmemptr) 
		{
			fprintf(stderr, "\nERROR malloc %d", sz); 
			gmemsize= 0;
		}
		else gmemsize= sz;
	}
	return gmemptr;
}


// 	 _________
// 	|         |   SECTION 1
//	|    *    |   V4L DEVICE 
// 	|  * *    |   CAMERA IMAGE CAPTURE
// 	|    *    |
// 	|    *    |
// 	|  *****  |
// 	|_________|

// V4L_device class is based and takes code from
// 		- Jay Rambhia (https://gist.github.com/jayrambhia/5866483)
#define MAX_CAP_FOURCC	32
#define MAX_V4L_FORMATS 32
struct V4LDriverCameraInformation
{
	char driver[32];
	char card[64];
	char bus_info[64];
	char version[8];
	unsigned int capabilities;
	char bounds[16];
	char defrect[16];
	char pixelaspect[16];
	char cap_fourcc_description[MAX_CAP_FOURCC][16];
	char cap_fourcc[MAX_CAP_FOURCC][5];
	int ncap;
	struct 
	{
		bool yuyv;
		bool mjpg;
		bool jpeg;
	} format;
	int V4L_formats[MAX_V4L_FORMATS];
};

class V4L_device
{
	public:
		V4L_device(const char*);
		~V4L_device(void);
		int SetWorkingMode(CaptureResolution , char* );
		void* AllocateBuffer(void);
		int CaptureImage(void);	
		void printinfo(void);
		int GetDriverInfo(void);
		struct V4LDriverCameraInformation drvinfo;			
		void *ptr_capture_buffer;
		size_t capture_length;
		int dev; // copy of private camera
		// Working mode
		struct 
		{
			int width;
			int height;
			int field;
			int pixel_size;
			unsigned int pixelformat;
		} wkm;
	private:
		
		int GetSupportedFormats(void);
		int xioctl(int , void *);
		int camera;	// file descriptor (open)
		struct v4l2_buffer v4l_buf;		
};

V4L_device::V4L_device(const char *path)
{
//	fprintf(stdout, "\nV4L_device create %s", path);
	ptr_capture_buffer= 0;
	memset(&v4l_buf, 0, sizeof(struct v4l2_buffer));
	memset(&drvinfo, 0, sizeof(struct V4LDriverCameraInformation)); 
	dev= camera = open(path, O_RDWR);
	if (camera == -1)
	{
		perror("Opening video device");
	}
}

V4L_device::~V4L_device()
{
	if(ptr_capture_buffer) munmap(ptr_capture_buffer, v4l_buf.length);  
	if(camera) close(camera); 
	fprintf(stdout, "\nV4L_device destroy\n");
	fflush(stdout);
}

int V4L_device::xioctl(int request, void *arg)
{
	int r;
	do r = ioctl (camera, request, arg); 
	while (-1 == r && EINTR == errno);
	return r; 
}

// Get Camera information from DRIVER
int V4L_device::GetDriverInfo(void)
{
//	memset(&drvinfo, 0, sizeof(struct V4LDriverCameraInformation)); 
	// (1) VIDIOC_QUERYCAP
	struct v4l2_capability caps = {};
	if ( xioctl(VIDIOC_QUERYCAP, &caps) == -1 )
	{
		perror("Querying Capabilities");
		return -1;
	}
	snprintf(drvinfo.driver, sizeof(V4LDriverCameraInformation::driver), "%s", caps.driver);
	snprintf(drvinfo.card, sizeof(V4LDriverCameraInformation::card), "%s", caps.card);
	snprintf(drvinfo.bus_info, sizeof(V4LDriverCameraInformation::bus_info), "%s", caps.bus_info);
	snprintf(drvinfo.version, sizeof(V4LDriverCameraInformation::version), "%d.%d", (caps.version>>16)&&0xff,(caps.version>>24)&&0xff);
	drvinfo.capabilities= caps.capabilities;

	// (2) VIDIOC_CROPCAP
	struct v4l2_cropcap cropcap = {0};
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl (VIDIOC_CROPCAP, &cropcap))
	{
	//	perror("\nWARNING - Querying Cropping Capabilities");
	//	return -1;
	}
	else {
		snprintf(drvinfo.bounds, sizeof(V4LDriverCameraInformation::bounds), "%dx%d+%d+%d", cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left, cropcap.bounds.top);
		snprintf(drvinfo.defrect, sizeof(V4LDriverCameraInformation::defrect), "%dx%d+%d+%d", cropcap.defrect.width, cropcap.defrect.height, cropcap.defrect.left, cropcap.defrect.top);
		snprintf(drvinfo.pixelaspect, sizeof(V4LDriverCameraInformation::pixelaspect), "%d/%d", cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator);
	}
	// (3) Camera capabilities - fourcc
	// v4l2-ctl --list-formats
	memset(&drvinfo.cap_fourcc, 0, sizeof(V4LDriverCameraInformation::cap_fourcc));
	drvinfo.ncap=0;
	struct v4l2_fmtdesc fmtdesc = {0};
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while (0 == xioctl(VIDIOC_ENUM_FMT, &fmtdesc))
	{
		//fprintf(stdout, "\n%i  %s", drvinfo.ncap, (char*)fmtdesc.description);
		fmtdesc.index++;
		strncpy(drvinfo.cap_fourcc_description[drvinfo.ncap], (char*)fmtdesc.description, sizeof(V4LDriverCameraInformation::cap_fourcc_description[0]));
		snprintf(drvinfo.cap_fourcc[drvinfo.ncap],5, "%s", (char *)&fmtdesc.pixelformat);
		drvinfo.ncap++;
		if(drvinfo.ncap == MAX_CAP_FOURCC)
		{
			fprintf(stdout, "\nWARNING - truncated cap_fourcc");
			break;
		}
	}
	return 0;
}


// Get Camera information from DRIVER
int V4L_device::GetSupportedFormats(void)
{
	fprintf(stdout, "\nTrying formats ...");
	fflush (stdout);
	// Camera capabilities
	//	try one by one all formats
	memset(&drvinfo.V4L_formats, 0, sizeof(V4LDriverCameraInformation::V4L_formats));
	struct v4l2_format format = {0};
	int j=0;
	for(unsigned int i = 0; i < (sizeof(V4L_formats)/sizeof(int)); i++)
	{
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		format.fmt.pix.width = 320;
		format.fmt.pix.height = 240;
		format.fmt.pix.pixelformat = V4L_formats[i];
		format.fmt.pix.field = V4L2_FIELD_NONE;
		if(	xioctl(VIDIOC_S_FMT, &format) != -1 
			&& format.fmt.pix.pixelformat == V4L_formats[i])
		{
			//fprintf(stdout, "\nFormat %d %s", i, V4L_formats_str[i]);
			drvinfo.V4L_formats[j++]= i+1;
			switch(V4L_formats[i])
			{
				case V4L2_PIX_FMT_YUYV:		drvinfo.format.yuyv= true; break;
				case V4L2_PIX_FMT_MJPEG:	drvinfo.format.mjpg= true; break;
				case V4L2_PIX_FMT_JPEG:		drvinfo.format.jpeg= true; break;
			}
		}
	}
	fprintf(stdout, " done");
	return 0;
}


int V4L_device::SetWorkingMode(CaptureResolution res, char *preferred)
{
	GetSupportedFormats();
	
	struct v4l2_format format = {0};
	// DEFAULT mode is MJPEG
	wkm.pixelformat= 0;
	if(drvinfo.format.mjpg)	
		wkm.pixelformat= V4L2_PIX_FMT_MJPEG;
	else if(drvinfo.format.jpeg)	
		wkm.pixelformat= V4L2_PIX_FMT_JPEG;
	else if(drvinfo.format.yuyv)
		wkm.pixelformat= V4L2_PIX_FMT_YUYV;
	else {
		fprintf(stdout, "\nERROR: No valid format");
		fflush (stdout);
		return -1;
	}		
	// overrule if CLI option
	if(drvinfo.format.yuyv && strcmp(preferred, "YUYV")==0) wkm.pixelformat= V4L2_PIX_FMT_YUYV;
	if(drvinfo.format.mjpg && strcmp(preferred, "MJPG")==0) wkm.pixelformat= V4L2_PIX_FMT_MJPEG;
	
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.width = res.width;
	format.fmt.pix.height = res.height;
	format.fmt.pix.field = V4L2_FIELD_NONE;
	format.fmt.pix.pixelformat = wkm.pixelformat;   
	// Chek if format is supported
	if (!( (xioctl(VIDIOC_S_FMT, &format) != -1) && (format.fmt.pix.pixelformat == wkm.pixelformat) ) )
	{
		perror("\nERROR: Selected format is not supported.");
		return -1;
	}
	wkm.width= format.fmt.pix.width;
	wkm.height=  format.fmt.pix.height;
	wkm.field=  format.fmt.pix.field;
	return wkm.pixelformat; 
}
void* V4L_device::AllocateBuffer()
{
	ptr_capture_buffer= 0;
	memset(&v4l_buf, 0, sizeof(struct v4l2_buffer));

	struct v4l2_requestbuffers req = {0};
	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (-1 == xioctl(VIDIOC_REQBUFS, &req))
	{
		perror("Requesting Buffer");
		return (void *) -1;
	}
	v4l_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l_buf.memory = V4L2_MEMORY_MMAP;
	v4l_buf.index = 0;
	// query the status of a buffer at any time after buffers have been 
	// allocated with the ioctl VIDIOC_REQBUFS ioctl.
	if(-1 == xioctl(VIDIOC_QUERYBUF, &v4l_buf))
	{
		perror("Querying Buffer");
		return (void *) -1;
	}
	// pointer to the buffer of the image captured
	// map or unmap files or devices into memory
	ptr_capture_buffer= mmap (NULL, v4l_buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera, v4l_buf.m.offset);	
	return ptr_capture_buffer;
}
int V4L_device::CaptureImage()
{
	// call the VIDIOC_QBUF ioctl to enqueue an empty (capturing) or 
	// filled (output) buffer in the driver’s incoming queue
	if(-1 == xioctl(VIDIOC_QBUF, &v4l_buf))
	{
		perror("Query Buffer");
		return -1;
	}
	// Start streaming I/O
	if(-1 == xioctl(VIDIOC_STREAMON, &v4l_buf.type)) 
	{
		perror("Start Capture");
		return -1;
	}
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(camera, &fds);
	struct timeval tv = {0};
	tv.tv_sec = 2;
	if(-1 == select(camera+1, &fds, NULL, NULL, &tv))
	{
		perror("Waiting for Frame");
		return -1;
	}
	if(-1 == xioctl(VIDIOC_DQBUF, &v4l_buf))
	{
		perror("Retrieving Frame");
		return -1;
	}
	capture_length= v4l_buf.bytesused;
	return 0; 
}	

void V4L_device::printinfo()
{
	GetDriverInfo();
	GetSupportedFormats();
	fprintf(stdout, "\nVIDIOC_QUERYCAP");
	fprintf(stdout, "\n\tDriver:        \"%s\"", drvinfo.driver);
	fprintf(stdout, "\n\tCard:          \"%s\"", drvinfo.card);
	fprintf(stdout, "\n\tBus:           \"%s\"", drvinfo.bus_info);
	fprintf(stdout, "\n\tVersion:       %s", drvinfo.version);
	fprintf(stdout, "\n\tCapabilities:  %08x", drvinfo.capabilities); 

	fprintf(stdout, "\nVIDIOC_CROPCAP");
	fprintf(stdout, "\n\tBounds:        \"%s\"", drvinfo.bounds);
	fprintf(stdout, "\n\tDefrect:       \"%s\"", drvinfo.defrect);
	fprintf(stdout, "\n\tPixel aspect:  \"%s\"", drvinfo.pixelaspect);

	fprintf(stdout, "\nVIDIOC_ENUM_FMT");
	for(int i=0; i<drvinfo.ncap; i++)
		fprintf(stdout, "\n\t%s: \t\t%s", drvinfo.cap_fourcc[i], drvinfo.cap_fourcc_description[i]);

	fprintf(stdout, "\nFormat supported");
	fprintf(stdout, "\n\tJPEG: \t\t%s", drvinfo.format.jpeg?"Yes":"No");
	fprintf(stdout, "\n\tMJPG: \t\t%s", drvinfo.format.mjpg?"Yes":"No");
	fprintf(stdout, "\n\tYUYV: \t\t%s", drvinfo.format.yuyv?"Yes":"No");
	for(size_t i=0; drvinfo.V4L_formats[i]!=0 && i<sizeof(V4LDriverCameraInformation::V4L_formats)/sizeof(int); i++)			
		fprintf(stdout,"\n\t%s",  V4L_formats_str[drvinfo.V4L_formats[i]-1]);

	fprintf(stdout, "\n");
}



//  _________
// |         |   SECTION 2
// |   * *   |   OUTPUT IMAGE VIA THE HDMI DISPLAY
// |  *   *  |   DECODE AND DUMP THE IMAGE BUFFER TO THE FRAMEBUFFER
// |     *   |   THIS PART ONLY APPLIES WHEN 'DISPLAY' OPTION IS CHOSEN
// |   *     |
// |  *****  |
// |_________|

// The following section are used when calling with option 'display'
// The set of functions decode and dump the outcome into the framebuffer 
struct ImageInfo
{
	int width;
	int height;
	int pixel_size;
};

// Decompress JPEG into memory
// takes the jpeg image from 'jpg_buffer' memory
// output decompressed image into permanet buffer pointed by 'gmemptr' (=== local function pointer 'bmp_buffer)
// returns:
// 		decompressed RGB image in memory (gmemptr) 
// 		Imgage info: width, height, and pixel size (bytes per pixel) -> image size in memory is= width x height x pixel_size
// Original code for JPEG_decompress comes from (original comments are kept):
//		- Kenneth Finnegan - A bare-bones example of how to use jpeglib to decompress a jpg in memory. (https://gist.github.com/PhirePhly/3080633)
int JPEG_decompress (ImageInfo *info, unsigned char *jpg_buffer, unsigned long jpg_size) 
{
	int rc;
	// Variables for the decompressor itself
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;

	// Variables for the output buffer, and how long each row is
	int row_stride, width, height, pixel_size;

	// Allocate a new decompress struct, with the default error handler.
	// The default error handler will exit() on pretty much any issue,
	// so it's likely you'll want to replace it or supplement it with
	// your own.
	cinfo.err = jpeg_std_error(&jerr);	
	jpeg_create_decompress(&cinfo);

	// Configure this decompressor to read its data from a memory 
	// buffer starting at unsigned char *jpg_buffer, which is jpg_size
	// long, and which must contain a complete jpg already.
	//
	// If you need something fancier than this, you must write your 
	// own data source manager, which shouldn't be too hard if you know
	// what it is you need it to do. See jpeg-8d/jdatasrc.c for the 
	// implementation of the standard jpeg_mem_src and jpeg_stdio_src 
	// managers as examples to work from.
	jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);

	// Have the decompressor scan the jpeg header. This won't populate
	// the cinfo struct output fields, but will indicate if the
	// jpeg is valid.
	rc = jpeg_read_header(&cinfo, TRUE);

	if (rc != 1) {
		fprintf(stdout, "File does not seem to be a normal JPEG");
		return -1;
	}

	// By calling jpeg_start_decompress, you populate cinfo
	// and can then allocate your output bitmap buffers for
	// each scanline.
	jpeg_start_decompress(&cinfo);
	
	width = cinfo.output_width;
	height = cinfo.output_height;
	pixel_size = cinfo.output_components;

	// Calculate memory and resize buffer as needed
	gmemalloc( (size_t) (width * height * pixel_size) );
	if(!gmemptr) return -1;

	// The row_stride is the total number of bytes it takes to store an
	// entire scanline (row). 
	row_stride = width * pixel_size;

	//
	// Now that you have the decompressor entirely configured, it's time
	// to read out all of the scanlines of the jpeg.
	//
	// By default, scanlines will come out in RGBRGBRGB...  order, 
	// but this can be changed by setting cinfo.out_color_space
	//
	// jpeg_read_scanlines takes an array of buffers, one for each scanline.
	// Even if you give it a complete set of buffers for the whole image,
	// it will only ever decompress a few lines at a time. For best 
	// performance, you should pass it an array with cinfo.rec_outbuf_height
	// scanline buffers. rec_outbuf_height is typically 1, 2, or 4, and 
	// at the default high quality decompression setting is always 1.
	while (cinfo.output_scanline < cinfo.output_height) {
		unsigned char *buffer_array[1];
		buffer_array[0] = gmemptr + \
						   (cinfo.output_scanline) * row_stride;

		jpeg_read_scanlines(&cinfo, buffer_array, 1);

	}

	// Once done reading *all* scanlines, release all internal buffers,
	// etc by calling jpeg_finish_decompress. This lets you go back and
	// reuse the same cinfo object with the same settings, if you
	// want to decompress several jpegs in a row.
	//
	// If you didn't read all the scanlines, but want to stop early,
	// you instead need to call jpeg_abort_decompress(&cinfo)
	jpeg_finish_decompress(&cinfo);

	// At this point, optionally go back and either load a new jpg into
	// the jpg_buffer, or define a new jpeg_mem_src, and then start 
	// another decompress operation.
	
	// Once you're really really done, destroy the object to free everything
	jpeg_destroy_decompress(&cinfo);

	info->width= width;
	info->height= height;
	info->pixel_size= pixel_size;

	return 0;
}

 
//	FRAMEBUFFER	
// 	POSITION 
//		frame buffer position (0,0) = TOP-LEFT corner
// 	Frame buffer is BGR - 32 bits word per pixel
// 		byte 0 blue
// 		byte 1 green
// 		byte 2 red
// 		byte 4 transparency	

//	JPEG IMGAGE
//		first-byte is top-left corner
int display_imageRGB_2_fb(ImageInfo *info, unsigned char *bmp_buffer, char *fbp, struct fb_var_screeninfo *vinfo, uint32_t fb_x0, uint32_t fb_y0)  
{
	int height= info->height;
	int width= info->width;
	unsigned char pixel[4];
	pixel[3]=0; // No transparency	
	unsigned int FB_WIDTH= vinfo->xres;
	//unsigned int FB_HEIGHT= vinfo->yres;
	//unsigned int FB_BPP= vinfo->bits_per_pixel;
	char *img_ptr= (char*) bmp_buffer;
	int img_BYTES_per_pixel= 3; 
	unsigned int* fb_ptr_row = (unsigned int*) fbp + fb_y0 * FB_WIDTH;
	unsigned int *ptr;
	for (int y = 0; y < height; y++) // 1080
	{
		ptr= fb_ptr_row;
		fb_ptr_row +=  FB_WIDTH;
		ptr += fb_x0;
		for (int x = 0; x < width ; x++) // 1920
		{
			pixel[0]= *(char *) (img_ptr + 2);	// Blue
			pixel[1]= *(char *) (img_ptr + 1);  // Green
			pixel[2]= *(char *) (img_ptr + 0);	// Red	
			//if( !(do_transparency && *((unsigned int*) &pixel)==transparent_color) ) 
			*ptr= *(unsigned int *) &pixel;
			ptr++;
			img_ptr += img_BYTES_per_pixel;
		}		
	}	
	return 0;
}

// Ref - How to convert yuy2 to a BITMAP in C++ 
// 		https://stackoverflow.com/questions/4491649/how-to-convert-yuy2-to-a-bitmap-in-c
// Changes done to the original code from the link above:
// original values saturate so need ro decrease
// divide by 8 and then LUMA factor changed from 37 to 31
// input (yuvy) strides by 4 bytes, output (fb) strides by 6 (2 pixels)
// 4 bytes YUYV -> 2 x pixels RGB (3 bytes). 
// FB is 4 bytes per pixel RGB. The fourth one is the transparency
int display_imgageYUVY_2_fb(ImageInfo *info, char *raw_img_buffer, char *fbp, struct fb_var_screeninfo *vinfo, uint32_t fb_x0, uint32_t fb_y0)  
{
	int height= info->height;
	int width= info->width;
	unsigned char pixel[4];
	pixel[3]=0;
	unsigned int FB_WIDTH= vinfo->xres;
	char *img_ptr= (char*) raw_img_buffer;
//	int img_BYTES_per_pixel= 3; 

	unsigned int* fb_ptr_row = (unsigned int*) fbp + fb_y0 * FB_WIDTH;
	unsigned int *ptr;
	for (int y = 0; y < height; y++) 
	{
		ptr= fb_ptr_row;
		fb_ptr_row +=  FB_WIDTH;
		ptr += fb_x0;
		for (int x = 0; x < width ; x+=2) 
		{
			long  y0 = img_ptr[0];	// Luma
			long  u0 = img_ptr[1];	// Cr
			long  y1 = img_ptr[2];
			long  v0 = img_ptr[3];	// Cb
			img_ptr += 4;
			//long  c = y0 -  16; 
			long  d = u0 - 128;	
			long  e = v0 - 128;	
			long p= 31 * (y0 -  16);
			pixel[0] = (unsigned char) (( p +  65 * d           + 16) >> 5); // blue
			pixel[1] = (unsigned char) (( p -  12 * d -  26 * e + 16) >> 5); // green
			pixel[2] = (unsigned char) (( p           +  51 * e + 16) >> 5); // red
			*ptr= *(unsigned int *) &pixel;
			ptr++;
			//c = y1-16;
			p= 31 * (y1 -  16);
			pixel[0] = (unsigned char) (( p +  65 * d           + 16) >> 5); // blue
			pixel[1] = (unsigned char) (( p -  12 * d -  26 * e + 16) >> 5); // green
			pixel[2] = (unsigned char) (( p           +  51 * e + 16) >> 5); // red
			*ptr= *(unsigned int *) &pixel;
			ptr++;
		}
	}	
	return 0;
}



//  _________
// |         |   SECTION 3
// |   ***   |   JPEG COMPRESS AND OUTPUT TO FILE
// |  *   *  |   YUV -> JPEG -> FILE.JPEG
// |     *   |   THIS PART ONLY APPLIES FOR RAW CAPTURES (YUV)
// |  *   *  |
// |   ***   |
// |_________|


//	converts a YUYV raw buffer to a JPEG buffer.
//	input is in YUYV (YUV 422). output is JPEG binary.
//		Each four bytes is two pixels.
//		Each four bytes is two Y's, a Cb and a Cr.
//		Each Y goes to one of the pixels, and the Cb and Cr belong to both pixels.
//	code based on: 
//		http://stackoverflow.com/questions/17029136/weird-image-while-trying-to-compress-yuv-image-to-jpeg-using-libjpeg
int compressYUYVtoJPEG(char *input, const int width, const int height) 
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    uint8_t* outbuffer = NULL;
    uint64_t outlen = 0;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &outbuffer, (long unsigned int *)&outlen);

    // jrow is a libjpeg row of samples array of 1 row pointer
    cinfo.image_width = width & -1;
    cinfo.image_height = height & -1;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr; //libJPEG expects YUV 3bytes, 24bit

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 92, TRUE);
	
	//-------------------------------------
	// START COMPRESS
    jpeg_start_compress(&cinfo, TRUE);
    vector<uint8_t> tmprowbuf(width * 3);
    JSAMPROW row_pointer[1];
    row_pointer[0] = &tmprowbuf[0];
    while (cinfo.next_scanline < cinfo.image_height) 
	{
        unsigned i, j;
        unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
		//input strides by 4 bytes, output strides by 6 (2 pixels)
        for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) 
		{ 
            tmprowbuf[j + 0] = input[offset + i + 0]; // Y (unique to this pixel)
            tmprowbuf[j + 1] = input[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 2] = input[offset + i + 3]; // V (shared between pixels)
            tmprowbuf[j + 3] = input[offset + i + 2]; // Y (unique to this pixel)
            tmprowbuf[j + 4] = input[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 5] = input[offset + i + 3]; // V (shared between pixels)
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
	// FINISH COMPRESS
	//-------------------------------------
   
	//fwrite(outbuffer,  sizeof(char), outlen, outfile);
	gmemalloc( outlen );
	if(gmemptr) memcpy(gmemptr, outbuffer, outlen);
	
	jpeg_destroy_compress(&cinfo);
	free(outbuffer);
	return (int) outlen;
}

//	converts a YUYV --> RGB --> JPEG buffer.
//	input is in YUYV (YUV 422). output is JPEG binary.
//	(just a test)
int compressYUYV_through_RGB_to_JPEG(FILE *outfile, const char *filename, char *input, const int width, const int height) 
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    uint8_t* outbuffer = NULL;
    uint64_t outlen = 0;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &outbuffer, (long unsigned int *)&outlen);

    // jrow is a libjpeg row of samples array of 1 row pointer
    cinfo.image_width = width & -1;
    cinfo.image_height = height & -1;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;// JCS_YCbCr; // JCS_RGB; //libJPEG expects YUV 3bytes, 24bit

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 92, TRUE); 
	
	//-------------------------------------
	// START COMPRESS
    jpeg_start_compress(&cinfo, TRUE);
    vector<uint8_t> tmprowbuf(width * 3);
    JSAMPROW row_pointer[1];
    row_pointer[0] = &tmprowbuf[0];
    while (cinfo.next_scanline < cinfo.image_height)
	{
        unsigned i, j;
        unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
		//input strides by 4 bytes, output strides by 6 (2 pixels)
        for (i = 0, j = 0; i < cinfo.image_width * 2 ; i += 4, j += 6) 
		{ 
			unsigned long y0 = (unsigned long) input[ offset + i + 0 ];
			unsigned long u =  (unsigned long) input[ offset + i + 1 ];
			unsigned long y1 = (unsigned long) input[ offset + i + 2 ];
			unsigned long v =  (unsigned long) input[ offset + i + 3 ];
			unsigned long a= ((v-128) * 37221) >> 15;
			unsigned long b= (( (u-128) * 12975) + ((v-128) * 18949)) >> 15;
			unsigned long c= ((u-128) * 66883) >> 15;
            tmprowbuf[ j + 0 ] = (unsigned char) (y0 + a);
            tmprowbuf[ j + 1 ] = (unsigned char) (y0 - b);
            tmprowbuf[ j + 2 ] = (unsigned char) (y0 + c);
			tmprowbuf[ j + 3 ] = (unsigned char) (y1 + a);
            tmprowbuf[ j + 4 ] = (unsigned char) (y1 - b);
            tmprowbuf[ j + 5 ] = (unsigned char) (y1 + c);	
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
	// FINISH COMPRESS
	//-------------------------------------
    jpeg_destroy_compress(&cinfo);
	fwrite(outbuffer,  sizeof(char), outlen, outfile);
	return (int) outlen;
}


//  _________
// |         |   SECTION 4
// |      *  |   CLOUD upload 
// |    * *  |   
// |   *  *  |   
// |  *****  |
// |      *  |
// |_________|

// memory for the POST message including HTTP header
// keep a permanent buffer to minimize mallocs
class POSTMessageMemory {
		size_t overhead;
	public:
		size_t mem_sz; 
		char *mem_ptr; 
		size_t payload;
		POSTMessageMemory ()
		{
//			printf("Constructed\n");
			overhead= sizeof(char) * (1*1024);
			mem_sz= 0; 
			mem_ptr= 0; 
		};
		~POSTMessageMemory ()
		{
			printf("Destroyed\n");
			if(mem_ptr) free(mem_ptr);
		};
		void size(size_t pl)
		{
			payload= pl;
			if(mem_sz < (payload + overhead))
			{
				mem_sz= sizeof(char) * ( 2 * payload + overhead);
				if(mem_ptr) free(mem_ptr);
				mem_ptr= (char *) malloc(mem_sz);
				printf("*** POSTMessageMemory malloc %d\n", mem_sz);
			}
		};
};
				
/*
void upload_image(POSTMessageMemory *postmem, char *filename, char* payload_ptr, double *elapsed, char *result)
{
	//char fullfilename[128];	
	char *xmlcode_ptr;
	char *memptr= postmem->mem_ptr; 
	size_t mem_sz= postmem->mem_sz;
	size_t payload_sz= postmem->payload;
	if(memptr==0) return;
	size_t nbytes;
	size_t pos;
	
	// image file
	memptr[0]='\0';
	//snprintf(fullfilename, sizeof(fullfilename),"%s%s", IMAGE_SAVED_FILES, filename);
	nbytes= hhtpPOST_header(filename, memptr, mem_sz, &pos, payload_sz);
	//file_getcontent(fullfilename, &memptr[pos], payload_sz);
	memcpy(&memptr[pos], payload_ptr, payload_sz);
	result[0]='\0';
	if(hhtpPOST_upload(memptr, nbytes, elapsed, &xmlcode_ptr) <0 )
	{
		strcpy(result, "CONNECTION ERROR");
		return;
	}
	
	pos = xmlcode_ptr? string(xmlcode_ptr).find("<result>") : string::npos;
	if(pos != string::npos) 
		sscanf(&xmlcode_ptr[pos], "%*[^>]>%[^</]</", result); // read and ignore characters other than a '>'
	
	// manifest file
	/ *
	size_t l= (size_t) strlen(filename);
	memptr[0]='\0';
	nbytes= hhtpPOST_header("data.txt", memptr, mem_sz, &pos, l);
	memcpy(&memptr[pos], filename, l);
	hhtpPOST_upload(memptr, nbytes, 0, 0);	
* /	
}
*/

//  _________
// |         |   SECTION 5
// |  ****   |   MAIN LOOP 
// |  *      |   CLI
// |  ****   |   CAPTURE, AND DISPLAY, IMAGES AT THE DEFINED INTERVAL
// |      *  |
// |   ***   |
// |_________|

enum VGAResolution {hd, qvga, vga, svga};
typedef struct
{
	VGAResolution res= vga;
	bool verbose= true; 
	bool agent= false;
	bool yuyv= false;
	bool display= false;
	bool cloud= false;
	int time;
	char V4L_format[5];
} CLI_options;

CLI_options CLIops;

static void usage(void)
{
	printf("\n");
	printf("Usage:\n"
		"tlcam <time> [--command] [options]\n"
		"   time      - capture period in miliseconds (<=100 recommended)\n"
		"commands are:\n"
		"   --info    - shows camera information\n"	
		"Options are:\n"
		"   videoX    - select camera driver /dev/videoX. Default is video0\n"
		"   qvga      - set QVGA capture(320x240)\n"
		"   vga       - set VGA capture (640x480) (default)\n"
		"   svga      - set Super-VGA capture (800x600)\n"
		"   hd        - set high definition capture (1080x720)\n"
		"   jpeg      - requests the camera to capture JPEG encoded images\n"
		"   mjpg      - requests the camera to capture MJPG encoded images (default)\n"
		"   yuyv      - requests the camera to capture YUYV encoded images\n"
		"   display   - output captured image into the framebuffer (HDMI output)\n"
		"   noverbose - stop console output\n"
		"   agent     - runs silently: set noverbose and disable kbhit\n"
		"   cloud     - upload image to cloud host instead of local camera storage (default is local)\n"
		"\nexample:\n"
		"   tlcam 100\n"
		"   tlcam 100 yuyv vga\n"
		"   tlcam 100 agent\n"
		"\n");
} 

int main(int argc, char *argv[]) 
{
	string command;
	bool is_cli= false;
	POSTMessageMemory postmem;
	string video= "video0";
	
	// memory
	gmemptr= 0;
	gmemsize= 0;	
	char str[128]; // general usage
	fprintf(stdout,"Time Lapse Camera version %s", version(str, sizeof(str)));
	if(argc<=1)
	{
		usage();
		exit(EXIT_SUCCESS);
	}
	// parameters
	size_t n_numbers= 0;
	int numbers[10];
	int i;
	for(i=1; i<argc; i++)
	{
		string param= argv[i];
		if(isNumber(argv[i]))
		{
		   if(n_numbers < (sizeof(numbers)/sizeof(size_t))) 
			   numbers[n_numbers++]= atoi(argv[i]);
		}
		else
		{	
			if(argv[i][0]=='-') 
			{
				// -commands
				if(argv[i][1]!='-')	{}
				// --commands
				else
				{
					is_cli= true;
					string param= &argv[i][2];
					command= param;
				}
			}
			// Options
			else
			{
				size_t j=0;
				for(; j<sizeof(str)-1 && j<strlen(argv[i]); j++) str[j]= tolower(argv[i][j]);
				str[j]='\0';
				if(  strncmp(str, "video",  strlen("video")) == 0)
				{
					video= str;
				}
				else if(strcmp(str, "hd") ==0) CLIops.res= hd;
				else if(strcmp(str, "qvga") ==0) CLIops.res= vga;
				else if(strcmp(str, "vga") ==0) CLIops.res= vga;
				else if(strcmp(str, "svga")==0) CLIops.res= svga;
				else if(strcmp(str, "noverbose")==0) CLIops.verbose= false;
				else if(strcmp(str, "agent")==0) CLIops.agent= true;
				else if(strcmp(str, "yuyv")==0 || strcmp(str, "yuv")==0) { CLIops.yuyv= true; strcpy(CLIops.V4L_format, "YUYV");}
				else if(strcmp(str, "jpeg")==0) { strcpy(CLIops.V4L_format, "JPEG");}
				else if(strcmp(str, "mjpg")==0 || strcmp(str, "mjpeg")==0) { strcpy(CLIops.V4L_format, "MJPG");}
				else if(strcmp(str, "display")==0) CLIops.display= true;				
				else if(strcmp(str, "cloud")==0) CLIops.cloud= true;				
			}
		}
	}

	CLIops.time= n_numbers>=1? numbers[0]: 100; // miliseconds 
	if(CLIops.agent) CLIops.verbose= false;
	
	// Resolution
	CaptureResolution res;
	const char *restxt;
	switch (CLIops.res)
	{
		case qvga: res= (CaptureResolution) { 340, 240}; restxt= "QVGA 320x240 (default)";break;
		case  vga: res= (CaptureResolution) { 640, 480}; restxt= "VGA 640x480"; break;
		case svga: res= (CaptureResolution) { 800, 600}; restxt= "SVGA 800x600"; break;
		case   hd: res= (CaptureResolution) {1080, 720}; restxt= "HD 1080x720"; break;
		default:   res= (CaptureResolution) { 640, 480}; restxt= "unknown - VGA 640x480";
	}
	
	// (1) Create V4L object for Camera 1
	string video_dev= "/dev/" + video;
	V4L_device v4lcam(video_dev.c_str()); 
	if(v4lcam.dev == -1)
	{
		fprintf(stdout, "\nERROR: Failure creating device");
		exit(EXIT_FAILURE);
	}
	
	if(is_cli)
	{
		if( command == "info")
		{
			v4lcam.printinfo(); 
		}
		else
			fprintf(stdout, "\nUnknown command %s\n", command.c_str());
	}
	else
	{
		char *fbp= 0;
		struct fb_var_screeninfo vinfo;		
		long int fb_size= 0;	// amount of bytes
		int fb= 0;
		// (2) FRAMEBUFFER INIT		
		if(CLIops.display)
		{

			struct fb_fix_screeninfo finfo;	
			fb = open(FRAMEBUFFER_DEVICE, O_RDWR);
			if (fb == -1) {
				perror("ERROR: cannot open framebuffer device");
				exit(EXIT_FAILURE);
			}
			// Get fixed screen information
			if (ioctl(fb, FBIOGET_FSCREENINFO, &finfo) == -1) {
				perror("ERROR reading fixed information");
				exit(EXIT_FAILURE);
			}
			// Get variable screen information
			if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) == -1) {
				perror("ERROR reading variable information");
				exit(EXIT_FAILURE);
			}
			fb_size = vinfo.xres * vinfo.yres * (vinfo.bits_per_pixel / 8);
			// Map to memory
			fbp = (char *)mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
			if ((int)fbp == -1) {
				perror("ERROR: failed to map framebuffer device to memory");
				exit(EXIT_FAILURE);
			}	
		}
		
		// Show camera information
		v4lcam.GetDriverInfo();
		fprintf(stdout, "\nCamera information (%s)", ("/dev/" + video).c_str());
		fprintf(stdout, "\n\tDriver:        \"%s\"", v4lcam.drvinfo.driver);
		fprintf(stdout, "\n\tCard:          \"%s\"", v4lcam.drvinfo.card);
		fprintf(stdout, "\n\tBus:           \"%s\"", v4lcam.drvinfo.bus_info);		
		
		// (3) V4L set working mode	
		int wkmf;
		if((wkmf=v4lcam.SetWorkingMode(res, CLIops.V4L_format)) < 0)
		{
			fprintf(stdout, "\nERROR: SetWorkingMode");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}		

		// (4) V4L allocate image buffer	
		if(v4lcam.AllocateBuffer() ==  (void *) -1) exit(EXIT_FAILURE);
		
		// Show working mode	
		fprintf(stdout, "\nWorking mode:");	
		fprintf(stdout, "\n\tCapture period=%d ms", CLIops.time);	
		int index= -1;
		size_t i=0;
		for(; i<sizeof(V4L_formats)/sizeof(int) && wkmf!= (int)V4L_formats[i] ; i++);
		if(i<sizeof(V4L_formats)/sizeof(int)) index= (int) i;
		fprintf(stdout, "\n\tFormat= %s", (index>=0) ? V4L_formats_str[index] : "Unknown");
		//fprintf(stdout, "\n\tFormat= %s", CLIops.yuyv?"YUYV":"MPEJ");	
		fprintf(stdout, "\n\tResolution %s", restxt); //CLIops.vga?"VGA 640x480":"QVGA 320x240 (default)");
		fprintf(stdout, "\n\n");
		
		// (5) CAPTURE LOOP
		unsigned int n=0;
		ImageInfo info;
		
		
		hhtpPOST_init(HOST_NAME, HOST_URL, HOST_PORT);
		
		if(!CLIops.agent) termios_init();
		for(;;)
		{
			// V4L capture image. Image is stored at v4lcam.ptr_capture_buffer
			if( v4lcam.CaptureImage() !=0) break;
			
			++n %= 20;
			char filename[64];
			char fullfilename[128];	
			snprintf(filename, sizeof(filename),"image_%03d.jpg", n);
			snprintf(fullfilename, sizeof(fullfilename),"%s%s", IMAGE_STORAGE_PATH, filename);	

			unsigned char *jpeg_ptr= 0;
			size_t jpeg_sz=0;
			// YUYV
			if(v4lcam.wkm.pixelformat == V4L2_PIX_FMT_YUYV)
			{
				// Compress to JPEG
		//		jpeg_sz += compressYUYV_through_RGB_to_JPEG(outfile, fullfilename, ptr_capture_buffer, CapResolution->width, CapResolution->height);
				jpeg_sz= compressYUYVtoJPEG((char*)v4lcam.ptr_capture_buffer, v4lcam.wkm.width, v4lcam.wkm.height);
				// Outcome is in gmemptr (pointer to jpeg compressed image)
				jpeg_ptr= gmemptr;
				if(CLIops.display)
				{
					info.width= v4lcam.wkm.width;
					info.height= v4lcam.wkm.height;
					display_imgageYUVY_2_fb(&info, (char *)v4lcam.ptr_capture_buffer, fbp, &vinfo, 0, 0);
				}
			}
			// JPEG
			else if(v4lcam.wkm.pixelformat == V4L2_PIX_FMT_MJPEG || v4lcam.wkm.pixelformat == V4L2_PIX_FMT_JPEG)
			{
				jpeg_ptr= (unsigned char*)v4lcam.ptr_capture_buffer;
				jpeg_sz= v4lcam.capture_length;
				if(CLIops.display)
				{
					JPEG_decompress(&info, jpeg_ptr, jpeg_sz); 
					display_imageRGB_2_fb(&info, gmemptr, fbp, &vinfo, 0, 0); 
				}
			}
			
			if(jpeg_ptr){
				// Upload JPEG file into the cloud
				if(CLIops.cloud)
				{
					double elapsed=0;
					char result[128];
					result[0]='\0';
					// allocate memory
					postmem.size((size_t) jpeg_sz);
					if(postmem.mem_ptr)
					{
					// image file upload
//					upload_image(&postmem, filename, (char*)jpeg_ptr, &elapsed, result);
									
						char *xmlcode_ptr;
						char *memptr= postmem.mem_ptr; 
						size_t mem_sz= postmem.mem_sz;
						size_t payload_sz= postmem.payload;
						size_t pos;
						
						size_t nbytes= hhtpPOST_header(filename, memptr, mem_sz, &pos, payload_sz);
						memcpy(&memptr[pos], jpeg_ptr, payload_sz);
						result[0]='\0';
						if(hhtpPOST_upload(memptr, nbytes, &elapsed, &xmlcode_ptr) <0 )
						{
							strcpy(result, "CONNECTION ERROR");
						}
						else
						{
						
						pos = xmlcode_ptr? string(xmlcode_ptr).find("<result>") : string::npos;
						if(pos != string::npos) 
							sscanf(&xmlcode_ptr[pos], "%*[^>]>%[^</]</", result); // read and ignore characters other than a '>'
						}
					}				
					
		
					
					
					if(CLIops.verbose) 
					{
						double temperature= CPUtemperature();
						if(CLIops.verbose) printf("T=%6.2fC %s %.2f ms %s\n", temperature, filename, elapsed/1000, result);
					}				
				}
				// Store JPEG image locally
				else
				{
					FILE *fp;
					// (1) JPEG file
					if ( (fp = fopen(fullfilename, "wb")) != NULL) 
					{					
						fwrite(jpeg_ptr,  sizeof(char), jpeg_sz, fp);
						fclose(fp);
					}
					// (2) Write metadata file containing the name of the JPEG just stored
					if ( (fp = fopen(fulldatafilename, "w"))!= NULL ) {
						fwrite(filename,  sizeof(char), strlen(filename), fp);
						fclose(fp);
					}	

					if(CLIops.verbose) {
						double temperature= CPUtemperature();
						if(CLIops.verbose) printf("T=%6.2fC %s\r", temperature, filename);
					}
				}
			}
			
			// Wait
			if(CLIops.agent)
				usleep(CLIops.time * 1000);
			else
			{
				if(!kbhit()) // check key pressed 
				{ 
					usleep(CLIops.time * 1000);
				} else {
					printf("\r");
					printf("Program terminated by user\n");
					break;
				}
			}
		}
		
		// (5) Terminate
		if(!CLIops.agent) termios_restore();
		if(fbp) munmap(fbp, fb_size);
		if(fb) close(fb);
	}
	
	// Terminate
	v4lcam.~V4L_device();
	if(gmemptr) free(gmemptr);
	exit(EXIT_SUCCESS);
}

/* END OF FILE */
