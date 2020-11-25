#ifndef ARG_NAME
	#define ARG_NAME(arg) #arg
#endif

//#define BMP_DEBUG_SESSION
#define BMPHDUMP(bmph) bmph_dump(bmph, #bmph)
#define BMPINFODUMP(bmpinfo) bmpinfo_dump(bmpinfo, #bmpinfo)

#define BMP_PRINT_DUMP_D(num)	printf("\t %20s = %8d (size = %zu) [%lX],\n",	#num,	num, sizeof(num), (size_t)&num);
#define BMP_PRINT_DUMP_HU(num)	printf("\t %20s = %8hu (size = %zu) [%lX],\n",	#num,	num, sizeof(num), (size_t)&num);
#define BMP_PRINT_DUMP_X(num)	printf("\t %20s = %8X (size = %zu) [%lX],\n",	#num,	num, sizeof(num), (size_t)&num);

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <string.h>

#include <assert.h>
//#define NDEBUG

#include <errno.h>


#ifndef __FILENAME__
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

//#define BMP_DEBUG_SESSION
#ifndef ERRPRINTF_BMP
#ifdef BMP_DEBUG_SESSION
	#define ERRPRINTF_BMP(format, ...)	fprintf(stderr, "%d::%s::%s__::__ " format "\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__, ## __VA_ARGS__)
#else
	#define ERRPRINTF_BMP(format, ...)
#endif
#endif


typedef int		 errno_t;
typedef int*	pict_t;

/* names by WinAPI */
typedef uint8_t		BYTE;	//1
typedef uint16_t	WORD;	//2
typedef uint32_t	DWORD;	//4
typedef int32_t		LONG;	//4

enum TYPE_PIXEL_FORMAT_DATA
{
	RGB,
	YUV
};


#pragma pack(push, 1)
typedef struct
{
	WORD	bfType;
	DWORD	bfSize;
	WORD	bfReserved1;
	WORD	bfReserved2;
	DWORD	bfOffBits;
} bitmapfileheader_t;

typedef struct
{
	DWORD	biSize;
	DWORD	biWidth;
	DWORD	biHeight;
	WORD	biPlanes;
	WORD	biBitCount;

	DWORD	biCompression;
	DWORD	biSizeImage;
	LONG	biXPelsPerMeter;
	LONG	biYPelsPerMeter;
	DWORD	biClrUsed;
	DWORD	biClrImportant;
	
	DWORD	biRedMask;
	DWORD	biGreenMask;
	DWORD	biBlueMask;
	
	DWORD	biAlphaMask;
} bitmapinfo_t;

typedef BYTE* framedata_t;

#pragma pack(pop)

//int get_filesize(FILE* file);
//unsigned char bitextract(const DWORD byte, const DWORD mask);
//int get_padding(DWORD width, WORD bitCount);
framedata_t load_bmp(const char* filename, int *width, int *height);
/**
 * @brief allocate buffer to data and set rgb24 from bmp file to data
 * @param data pointer to data array. Is changing, be careful
 * @param filename format name of bmp file
 * @param frame_ind num of frame
 * @return zero on success, an errno code on failure.
 */
int load_frame(framedata_t *data, const char *filename, size_t frame_ind);