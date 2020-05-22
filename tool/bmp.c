#include <bmp.h>


void bmph_dump(bitmapfileheader_t *bmph, const char* name)
{
	printf("\n---------------\n");
	printf("%s [%X]\n{\n", name, bmph);
	printf("\t %20s = %8X (size = %d) " "[%X],\n",	"bfType",		bmph->bfType, sizeof(bmph->bfType), &bmph->bfType);
	printf("\t %20s = %8ld (size = %d) ""[%X],\n",	"bfSize",		bmph->bfSize, sizeof(bmph->bfSize), &bmph->bfSize);
	printf("\t %20s = %8d (size = %d) " "[%X],\n",	"bfReserved1",	bmph->bfReserved1, sizeof(bmph->bfReserved1), &bmph->bfReserved1);
	printf("\t %20s = %8d (size = %d) " "[%X],\n",	"bfReserved2",	bmph->bfReserved2, sizeof(bmph->bfReserved2), &bmph->bfReserved2);
	printf("\t %20s = %8ld (size = %d) ""[%X]\n",	"bfOffBits",	bmph->bfOffBits, sizeof(bmph->bfOffBits), &bmph->bfOffBits);
	
	printf("}\n---------------\n");
}

int get_filesize(FILE* file)
{
	assert(file);
	
	int pos = ftell(file);
	fseek(file, 0, SEEK_END);
	int filesize = ftell(file);
	fseek(file, pos, SEEK_SET);
	
	return filesize;
}

unsigned char bitextract(const DWORD byte, const DWORD mask)
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

pict_t load_bmp(const char* filename,
				int *width, int *height)
{
	assert(filename);
	assert(width);
	assert(height);
	
	width  = -1;
	height = -1;
	
	unsigned char	*tmp_buf	= NULL;
	char			*buffer		= NULL;
	pict_t			frame		= NULL;
	
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
	
	BMPHDUMP(&bmph);
	
	if (bmph.bfType != 0x4D42)
	{
		fprintf(stderr, "%d:: Bad signature [[ %s.bfType ]] {%X}\n", __LINE__, ARG_NAME(bmph), bmph.bfType);
		goto err;
	}
	
	int filesize = get_filesize(file);
	if (filesize == -1)
	{
		fprintf(stderr, "%d:: Bad read size of %s\n", __LINE__, filename);
		goto err;
	}
	
	buffer = (char*)calloc(filesize, sizeof(*buffer));
	if (!buffer)
	{
		perror("calloc() failed");
		goto err;
	}
	
	
	printf("%d::%s::%s BAD HERE\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__);
	
	
	
	if (fread(buffer, 1, filesize, file) < filesize - sizeof(bitmapfileheader_t))
	{
		fprintf(stderr, "%d:: Bad here :: %s\n", __LINE__, __PRETTY_FUNCTION__);
		goto err;
	}
	
	
	BMPHDUMP(&bmph);
	
	printf("%d::%s::%s BAD HERE\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__);
	
	char *cur_pos = buffer;
	
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
//	fread(&bmpinfo.biSize, sizeof(bmpinfo.biSize), 1, file);
	
	if (bmpinfo.biSize >= 12)
	{
//		fread(&bmpinfo.biWidth,		sizeof(bmpinfo.biWidth),	1, file);
//		fread(&bmpinfo.biHeight,	sizeof(bmpinfo.biSize),		1, file);
//		fread(&bmpinfo.biPlanes,	sizeof(bmpinfo.biSize),		1, file);
//		fread(&bmpinfo.biBitCount,	sizeof(bmpinfo.biSize),		1, file);
//		
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
//		fread(&bmpinfo.biCompression,	sizeof(bmpinfo.biCompression),		1, file);
//		fread(&bmpinfo.biSizeImage,		sizeof(bmpinfo.biSizeImage),		1, file);
//		fread(&bmpinfo.biXPelsPerMeter,	sizeof(bmpinfo.biXPelsPerMeter),	1, file);
//		fread(&bmpinfo.biYPelsPerMeter,	sizeof(bmpinfo.biYPelsPerMeter),	1, file);
//		fread(&bmpinfo.biClrUsed,		sizeof(bmpinfo.biClrUsed),			1, file);
//		fread(&bmpinfo.biClrImportant,	sizeof(bmpinfo.biClrImportant),		1, file);

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
	
	if (bmpinfo.biSizeImage == 0) // necessary ?
	{
		bmpinfo.biSizeImage = (bmpinfo.biWidth * 3 + bmpinfo.biWidth % 4) * bmpinfo.biHeight;
	}
	
	bmpinfo.biRedMask   = 0;
	bmpinfo.biGreenMask = 0;
	bmpinfo.biBlueMask  = 0;
	
	if (bmpinfo.biSize >= 52)
	{
//		fread(&bmpinfo.biRedMask,		sizeof(bmpinfo.biRedMask),		1, file);
//		fread(&bmpinfo.biGreenMask,		sizeof(bmpinfo.biGreenMask),	1, file);
//		fread(&bmpinfo.biBlueMask,		sizeof(bmpinfo.biBlueMask),		1, file);

		memcpy(&bmpinfo.biRedMask, cur_pos, sizeof(bmpinfo.biRedMask));
		cur_pos += sizeof(bmpinfo.biRedMask);

		memcpy(&bmpinfo.biGreenMask, cur_pos, sizeof(bmpinfo.biGreenMask));
		cur_pos += sizeof(bmpinfo.biGreenMask);

		memcpy(&bmpinfo.biBlueMask, cur_pos, sizeof(bmpinfo.biBlueMask));
		cur_pos += sizeof(bmpinfo.biBlueMask);
	}
	
	
	printf("%d::%s::%s BAD HERE\n", __LINE__, __FILENAME__, __PRETTY_FUNCTION__);
	
	
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
	
	*width  = bmpinfo.biWidth;
	*height = bmpinfo.biHeight;
	
	tmp_buf = (unsigned char*)calloc(bmpinfo.biSizeImage, sizeof(*tmp_buf));
	if (!tmp_buf)
	{
		perror("calloc() failed");
		goto err;
	}
#ifdef BMP_DEBUG_SESSION
	printf("%d:: %d bytes was alloced\n", __LINE__, bmpinfo.biSizeImage); // log (to del // debug session)
#endif
	
	res = fread(tmp_buf, 1, bmpinfo.biSizeImage, file);
	if (res != bmpinfo.biSizeImage)
	{
		fread(stderr, "%d:: Bad reading %s\n", __LINE__, ARG_NAME(tmp_buf));
		goto err;
	}
	
	frame = (pict_t)calloc((*width) * (*height), sizeof(*frame));
	if (!frame)
	{
		perror("calloc() failed");
		goto err;
	}
	
	int mwidth = (3 * (*width) + 3) & (-4);
	// BGR -> RGB
	unsigned char* ptr = (unsigned char*)frame;
	for (int y = height - 1; y >= 0; y--)
	{
		unsigned char *pRow = tmp_buf + mwidth * y;
		for (int x = 0; x < width; x++)
		{
			*ptr++ = *(pRow + 2);
			*ptr++ = *(pRow + 1);
			*ptr++ = *pRow;
			pRow  += 3;
			ptr++;
		}
	}
	
	
	if (tmp_buf)
		free(tmp_buf);
	if (buffer)
		free(buffer);
	if (frame)
		free(frame);
	
	fclose(file);
	
	return frame;
	
err:
	if (tmp_buf)
		free(tmp_buf);
	if (buffer)
		free(buffer);
	if (frame)
		free(frame);
	
	fclose(file);
	
	return NULL;
}
