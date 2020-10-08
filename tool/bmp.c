#include <bmp.h>


void bmph_dump(bitmapfileheader_t *bmph, const char* name)
{
	printf("\n---------------\n");
	printf("%s [%X]\n{\n", name, bmph);
	BMP_PRINT_DUMP_X(bmph->bfType);
	BMP_PRINT_DUMP_D(bmph->bfSize);
	BMP_PRINT_DUMP_HU(bmph->bfReserved1);
	BMP_PRINT_DUMP_HU(bmph->bfReserved2);
	BMP_PRINT_DUMP_D(bmph->bfOffBits);
	
	printf("}\n---------------\n");
}

void bmpinfo_dump(bitmapinfo_t *bmpinfo, const char* name)
{
	printf("\n---------------\n");
	printf("%s [%X]\n{\n", name, bmpinfo);
	
	BMP_PRINT_DUMP_D(bmpinfo->biSize);
	BMP_PRINT_DUMP_D(bmpinfo->biWidth);
	BMP_PRINT_DUMP_D(bmpinfo->biHeight);
	BMP_PRINT_DUMP_HU(bmpinfo->biPlanes);
	BMP_PRINT_DUMP_HU(bmpinfo->biBitCount);
	BMP_PRINT_DUMP_D(bmpinfo->biCompression);
	BMP_PRINT_DUMP_D(bmpinfo->biSizeImage);
	
	printf("}\n---------------\n");
}

int get_filesize(FILE* file)
{
	assert(file);
	
	int pos = ftell(file);
	fseek(file, 0, SEEK_END);
	int filesize = ftell(file);
	fseek(file, pos, SEEK_SET);
#ifdef BMP_DEBUG_SESSION
	printf("%d::%s:: size of file = %d\n", __LINE__, __PRETTY_FUNCTION__, filesize);
#endif
	return filesize;
}

int get_filesize_debug(FILE* file, const char* filename)
{
	assert(file);
	
	int pos = ftell(file);
	fseek(file, 0, SEEK_END);
	int filesize = ftell(file);
	fseek(file, pos, SEEK_SET);
	printf("%d::%s:: size of file \"%s\" = %d\n", __LINE__, __PRETTY_FUNCTION__, filename, filesize);
	
	return filesize;
}

uint8_t bitextract(const DWORD byte, const DWORD mask)
{
	if (mask == 0)
	{
		return 0;
	}
	
	DWORD maskBuf = mask;
	DWORD maskPadding = 0;
	
	while (!(maskBuf & 1))
	{
		maskBuf >>= 1;
		maskPadding++;
	}
	
	return (byte & mask) >> maskPadding;
}

int get_padding(DWORD width, WORD bitCount)
{
	return ((width * (bitCount / 8)) % 4) & 3;
}

framedata_t load_bmp(const char* filename,
                     int *width, int *height)
{
	assert(filename);
	assert(width);
	assert(height);
	
	*width  = -1;
	*height = -1;
	
	uint8_t			*tmp_buf	= NULL;
	uint8_t			*buffer		= NULL;
	
	FILE *file = fopen(filename, "rb");
	if (!file)
	{
		fprintf(stderr, "%d:: Could not open file %s\n", __LINE__, filename);
		return NULL;
	}
	
	bitmapfileheader_t bmph = {};
	
	size_t res = fread(&bmph, 1, sizeof(bitmapfileheader_t), file);
	if (res != sizeof(bitmapfileheader_t))
	{
		fprintf(stderr, "%d:: Bad reading file %s\n", filename);
		goto err;
	}
	
#ifdef BMP_DEBUG_SESSION
	BMPHDUMP(&bmph);
#endif
	
	if (bmph.bfType != 0x4D42)
	{
		fprintf(stderr, "%d:: Bad signature [[ %s.bfType ]] {%X}\n", __LINE__, ARG_NAME(bmph), bmph.bfType);
		goto err;
	}
	
#ifdef BMP_DEBUG_SESSION
	int filesize = get_filesize_debug(file, filename);
#else
	int filesize = get_filesize(file);
#endif
	if (filesize == -1)
	{
		fprintf(stderr, "%d:: Bad read size of %s\n", __LINE__, filename);
		goto err;
	}
	
	buffer = (uint8_t*)calloc(filesize, sizeof(*buffer));
	if (!buffer)
	{
		perror("calloc() failed");
		goto err;
	}
	
	if (fread(buffer, 1, filesize, file) < filesize - sizeof(bitmapfileheader_t))
	{
		fprintf(stderr, "%d:: Bad here :: %s\n", __LINE__, __PRETTY_FUNCTION__);
		goto err;
	}
	
#ifdef BMP_DEBUG_SESSION
	BMPHDUMP(&bmph);
#endif
	
	uint8_t *cur_pos = buffer;
	
	if (bmph.bfSize			!= filesize	||
		bmph.bfReserved1	!= 0		||
		bmph.bfReserved2	!= 0)
	{
		printf("%d::%s::%s BAD HERE\n -- bfSize {%d} \\\\ filesize {%d} -- bfReserved1 {%d} -- bfReserved2 {%d}\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__,
			bmph.bfSize, filesize, bmph.bfReserved1, bmph.bfReserved2);
	
		goto err;
	}
	
	bitmapinfo_t bmpinfo;
	memcpy(&bmpinfo.biSize, cur_pos, sizeof(bmpinfo.biSize));
	cur_pos += sizeof(bmpinfo.biSize);
	
	if (bmpinfo.biSize >= 12)
	{
		memcpy(&bmpinfo.biWidth, cur_pos, sizeof(bmpinfo.biWidth)); // Объединить в функцию "чтения"
		cur_pos += sizeof(bmpinfo.biWidth);
	
		memcpy(&bmpinfo.biHeight, cur_pos, sizeof(bmpinfo.biHeight)); // Объединить в функцию "чтения"
		cur_pos += sizeof(bmpinfo.biHeight);
	
		memcpy(&bmpinfo.biPlanes, cur_pos, sizeof(bmpinfo.biPlanes)); // ...
		cur_pos += sizeof(bmpinfo.biPlanes);
	
		memcpy(&bmpinfo.biBitCount, cur_pos, sizeof(bmpinfo.biBitCount));
		cur_pos += sizeof(bmpinfo.biBitCount);
	}
	else
	{
		fprintf(stderr, "%d:: Bad bitmap core\n", __LINE__);
		goto err;
	}
	
	int colorsCount = bmpinfo.biBitCount >> 3;
	if (colorsCount < 3)
	{
		colorsCount = 3;
	}
	
	int bitsOnColor = bmpinfo.biBitCount / colorsCount;
	int maskVal 	= (1 << bitsOnColor) - 1;
	
	if (bmpinfo.biSize >= 40)
	{
		memcpy(&bmpinfo.biCompression, cur_pos, sizeof(bmpinfo.biCompression));
		cur_pos += sizeof(bmpinfo.biCompression);

		memcpy(&bmpinfo.biSizeImage, cur_pos, sizeof(bmpinfo.biSizeImage));
		cur_pos += sizeof(bmpinfo.biSizeImage);

		memcpy(&bmpinfo.biXPelsPerMeter, cur_pos, sizeof(bmpinfo.biXPelsPerMeter));
		cur_pos += sizeof(bmpinfo.biXPelsPerMeter);

		memcpy(&bmpinfo.biYPelsPerMeter, cur_pos, sizeof(bmpinfo.biYPelsPerMeter));
		cur_pos += sizeof(bmpinfo.biYPelsPerMeter);

		memcpy(&bmpinfo.biClrUsed, cur_pos, sizeof(bmpinfo.biClrUsed));
		cur_pos += sizeof(bmpinfo.biClrUsed);

		memcpy(&bmpinfo.biClrImportant, cur_pos, sizeof(bmpinfo.biClrImportant)); // Объединить в функцию "чтения"
		cur_pos += sizeof(bmpinfo.biClrImportant);
	}
	
	bmpinfo.biRedMask   = 0;
	bmpinfo.biGreenMask = 0;
	bmpinfo.biBlueMask  = 0;
	
	if (bmpinfo.biSize >= 52)
	{
		memcpy(&bmpinfo.biRedMask, cur_pos, sizeof(bmpinfo.biRedMask));
		cur_pos += sizeof(bmpinfo.biRedMask);

		memcpy(&bmpinfo.biGreenMask, cur_pos, sizeof(bmpinfo.biGreenMask));
		cur_pos += sizeof(bmpinfo.biGreenMask);

		memcpy(&bmpinfo.biBlueMask, cur_pos, sizeof(bmpinfo.biBlueMask));
		cur_pos += sizeof(bmpinfo.biBlueMask);
	}
	
	if (!bmpinfo.biRedMask		|| 
		!bmpinfo.biGreenMask	||
		!bmpinfo.biBlueMask)
	{
		bmpinfo.biRedMask	= maskVal << (bitsOnColor * 2);
		bmpinfo.biGreenMask	= maskVal << bitsOnColor;
		bmpinfo.biBlueMask	= maskVal;
	}
	
	if (bmpinfo.biSize >= 56)
	{
		memcpy(&bmpinfo.biAlphaMask, cur_pos, sizeof(bmpinfo.biAlphaMask));
		cur_pos += sizeof(bmpinfo.biAlphaMask);
	}
	else
	{
		bmpinfo.biAlphaMask = maskVal << (bitsOnColor * 3);
	}
	
	if (bmpinfo.biSize != 12 && bmpinfo.biSize != 40 && bmpinfo.biSize != 52 &&
		bmpinfo.biSize != 56 && bmpinfo.biSize != 108) {
		fprintf(stderr, "%d:: Error: Unsupported BMP format.", __LINE__);
		goto err;
	}
	
	if (bmpinfo.biBitCount != 16 && bmpinfo.biBitCount != 24 && bmpinfo.biBitCount != 32) {
		fprintf(stderr, "%d:: Error: Unsupported BMP bit count.", __LINE__);
		goto err;
	}
	
	if (bmpinfo.biCompression != 0 && bmpinfo.biCompression != 3) {
		fprintf(stderr, "%d:: Error: Unsupported BMP compression.", __LINE__);
		goto err;
	}
	
	if (bmph.bfOffBits != 14 + bmpinfo.biSize ||
		bmpinfo.biWidth  < 1 || bmpinfo.biWidth  > 10000 ||
		bmpinfo.biHeight < 1 || bmpinfo.biHeight > 10000 ||
		bmpinfo.biBitCount		!= 24 ||
		bmpinfo.biCompression	!= 0
		)
	{
		fprintf(stderr, "%d:: Bad data", __LINE__);
		goto err;
	}
	
#ifdef BMP_DEBUG_SESSION
	BMPHDUMP(&bmph);
#endif
	
	*width  = bmpinfo.biWidth;
	*height = bmpinfo.biHeight;
	
	int mwidth = (3 * (*width) + 3) & (-4);
	
	tmp_buf = (uint8_t*)calloc(bmpinfo.biSizeImage, sizeof(*tmp_buf));
	if (!tmp_buf)
	{
		perror("calloc() failed");
		goto err;
	}
	
#ifdef BMP_DEBUG_SESSION
	printf("%d:: %d bytes was alloced\n", __LINE__, bmpinfo.biSizeImage); // log (to del // debug session)
#endif
	
	if (bmpinfo.biSizeImage == 0) // necessary ?
	{
#ifdef BMP_DEBUG_SESSION
		printf("%d:: biSizeImage = 0\n", __LINE__);
#endif
		bmpinfo.biSizeImage = (bmpinfo.biWidth * 3 + bmpinfo.biWidth % 4) * bmpinfo.biHeight;
#ifdef BMP_DEBUG_SESSION
		printf("-->biSizeImage = {%d}\n", bmpinfo.biSizeImage);
#endif
	}
	memcpy(tmp_buf, cur_pos, bmpinfo.biSizeImage);
	
	framedata_t frame = calloc(4 * (*width) * (*height), sizeof(*frame));
	uint8_t *ptr = frame;
	// BGR -> RGB
	for (size_t y = *height; y > 0; y--)
	{
		unsigned char *pRow = tmp_buf + mwidth * (y - 1);
		for (size_t x = 0; x < *width; x++)
		{
			*ptr++ = *(pRow + 2);
			*ptr++ = *(pRow + 1);
			*ptr++ = *pRow;
			
			pRow  += 3;
		}
	}
	
	free(tmp_buf);
	free(buffer);
	
	fclose(file);
	
	return frame;
	
err:
	free(tmp_buf);
	free(buffer);
	free(frame);
	
	fclose(file);
	
	return NULL;
}
