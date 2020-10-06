#ifndef ARG_NAME
	#define ARG_NAME(arg) #arg
#endif

//#define BMP_DEBUG_SESSION
#define BMPHDUMP(bmph) bmph_dump(bmph, #bmph)
#define BMPINFODUMP(bmpinfo) bmpinfo_dump(bmpinfo, #bmpinfo)

#define BMP_PRINT_DUMP_D(num)	printf("\t %20s = %8d (size = %d) [%X],\n",		#num,		num, sizeof(num), &num);
#define BMP_PRINT_DUMP_HU(num)	printf("\t %20s = %8hu (size = %d) [%X],\n",	#num,		num, sizeof(num), &num);
#define BMP_PRINT_DUMP_X(num)	printf("\t %20s = %8X (size = %d) [%X],\n",		#num,		num, sizeof(num), &num);

#define Y_TRANSCRIPTION(R, G, B) 0.257 * R + 0.504 * G + 0.098 * B +  16
#define U_TRANSCRIPTION(R, G, B) -0.148 * R - 0.291 * G + 0.439 * B + 128
#define V_TRANSCRIPTION(R, G, B) 0.439 * R - 0.368 * G - 0.071 * B + 128


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
void YUVfromRGB(double* Y, double* U, double* V, const BYTE R, const BYTE G, const BYTE B);
