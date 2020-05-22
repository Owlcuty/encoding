#pragma one

#define ARG_NAME(arg) #arg
#define BMP_DEBUG_SESSION

#include <stdio.h>
#include <stdlib.h>

#include <assert.h>
//#define NDEBUG

#include <errno.h>


typedef int  errno_t;
typedef (int*) pict_t;

/* names by WinAPI */
typedef uint8_t		BYTE;	//1
typedef uint16_t	WORD;	//2
typedef uint32_t	DWORD;	//4
typedef int32_t		LONG;	//4

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

//int get_filesize(FILE* file);
//unsigned char bitextract(const DWORD byte, const DWORD mask);
//int get_padding(DWORD width, WORD bitCount);
pict_t load_bmp(const char* filename, int *width, int *height);