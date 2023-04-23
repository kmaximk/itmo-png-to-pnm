
#include "return_codes.h"
#include <isa-l/igzip_lib.h>

#include <libdeflate.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
struct image
{
	unsigned char *data;
	size_t size;
	unsigned char *plteData;
	size_t plteSize;
	int type;
};

int inf(unsigned char *inputData, unsigned char *outputData, size_t inSize, size_t outSize)
{
#if defined(ZLIB) + defined(LIBDEFLATE) + defined(ISAL) > 1
#	error "Only one library can be defined"
#endif
#if !defined(ZLIB) && !defined(LIBDEFLATE) && !defined(ISAL)
#	error "Wrong library, use ZLIB or LIBDEFLATE or ISAL"
#endif
#ifdef ZLIB
	z_stream infl;
	infl.zalloc = Z_NULL;
	infl.zfree = Z_NULL;
	infl.opaque = Z_NULL;
	infl.avail_in = inSize;
	infl.next_in = inputData;
	infl.avail_out = outSize;
	infl.next_out = outputData;
	int ret = inflateInit(&infl);
	if (ret == Z_MEM_ERROR)
	{
		return ERROR_OUT_OF_MEMORY;
	}
	ret = inflate(&infl, Z_NO_FLUSH);
	if (ret == Z_DATA_ERROR || ret == Z_STREAM_ERROR || ret == Z_BUF_ERROR)
	{
		return ERROR_DATA_INVALID;
	}
	else if (ret == Z_MEM_ERROR)
	{
		return ERROR_OUT_OF_MEMORY;
	}
	ret = inflateEnd(&infl);
	if (ret == Z_STREAM_ERROR)
	{
		return ERROR_DATA_INVALID;
	}
	return SUCCESS;
#endif
#ifdef LIBDEFLATE
	struct libdeflate_decompressor *de = libdeflate_alloc_decompressor();
	if (de == NULL)
	{
		return ERROR_OUT_OF_MEMORY;
	}
	int res = libdeflate_deflate_decompress(de, inputData + 2, inSize - 6, outputData, outSize, NULL);
	if (res == LIBDEFLATE_BAD_DATA || res == LIBDEFLATE_SHORT_OUTPUT || res == LIBDEFLATE_INSUFFICIENT_SPACE)
	{
		return ERROR_DATA_INVALID;
	}
	return SUCCESS;
#endif
#ifdef ISAL
	struct inflate_state infl;
	isal_inflate_init(&infl);
	infl.avail_in = inSize - 6;
	infl.next_in = inputData + 2;
	infl.avail_out = outSize;
	infl.next_out = outputData;
	int x = isal_inflate(&infl);
	if (x != ISAL_DECOMP_OK)
	{
		return ERROR_DATA_INVALID;
	}
	return SUCCESS;
#endif
}
struct ihdrRet
{
	char *text;
	int returnCode;
	int type;
};
struct ihdrRet ihdrChunk(FILE *f, struct image *bu, int *pars)
{
	unsigned char *buf = (*bu).data;
	struct ihdrRet ans = { 0, 0, 0 };
	int ret = fread(buf, 1, 8, f);
	if (ret != 8)
	{
		ans.text = "Read less bytes expected 8\n";
		ans.returnCode = ERROR_DATA_INVALID;
		return ans;
	}
	int size = buf[3] + buf[2] * 16 * 16 + buf[1] * 16 * 16 * 16 * 16 + buf[0] * 16 * 16 * 16 * 16 * 16 * 16;
	if (size != 13)
	{
		ans.text = "Wrong IHDR chunk size\n";
		ans.returnCode = ERROR_DATA_INVALID;
		return ans;
	}
	char name[8] = { buf[4] & 0xFF, buf[5] & 0xFF, buf[6] & 0xFF, buf[7] & 0xFF };
	if (strcmp("IHDR", name) != 0)
	{
		ans.text = "Error wrong name of the first chunk\n";
		ans.returnCode = ERROR_DATA_INVALID;
		return ans;
	}
	ret = fread(buf, 1, 8, f);
	if (ret != 8)
	{
		ans.text = "Wrong width and height\n";
		ans.returnCode = ERROR_DATA_INVALID;
		return ans;
	}
	int width = 0;
	int height = 0;
	int umn = 1;
	for (int i = 0; i < 4; i++)
	{
		width += (buf[3 - i] & 0xFF) * umn;
		height += (buf[7 - i] & 0xFF) * umn;
		umn *= (16 * 16);
	}
	pars[0] = width;
	pars[1] = height;
	fread(buf, 1, 9, f);
	if (buf[0] != 8)
	{
		ans.text = "Only support 8 bit depth images\n";
		ans.returnCode = ERROR_UNSUPPORTED;
		return ans;
	}
	if (buf[2] != 0)
	{
		ans.text = "Only deflate algorithm with value 0\n";
		ans.returnCode = ERROR_DATA_INVALID;
		return ans;
	}
	if (buf[3] != 0)
	{
		ans.text = "There is only 1 filtration method with value 0\n";
		ans.returnCode = ERROR_DATA_INVALID;
		return ans;
	}
	if (buf[4] != 0)
	{
		ans.text = "Only support images without interlace\n";
		ans.returnCode = ERROR_UNSUPPORTED;
		return ans;
	}
	(*bu).type = buf[1];
	if (buf[1] == 0 || buf[1] == 3)
	{
		ans.type = 1;
		return ans;
	}
	else if (buf[1] == 2)
	{
		ans.type = 3;
		return ans;
	}
	else
	{
		ans.text = "Only color types 0, 2, 3 are supported\n";
		ans.returnCode = ERROR_UNSUPPORTED;
		return ans;
	}
}
struct pair
{
	char *text;
	int returnCode;
};
void makeError(struct pair *ans, char *text, int ret)
{
	(*ans).text = text;
	(*ans).returnCode = ret;
}
struct pair parsePNG(FILE *f, struct image *buf)
{
	int ret;
	struct pair ans;
	int plte = 1;
	unsigned char tmp[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	while (1)
	{
		ans.returnCode = SUCCESS;
		ret = fread(tmp, 1, 8, f);
		if (ret != 8)
		{
			makeError(&ans, "Cannot read next chunk information\n", ERROR_DATA_INVALID);
			return ans;
		}
		size_t size = 0;
		int umn = 1;
		for (int i = 0; i < 4; i++)
		{
			size += (tmp[3 - i] & 0xFF) * umn;
			umn *= (16 * 16);
		}
		unsigned char *temp = malloc(sizeof(unsigned char) * size);
		if (!temp)
		{
			makeError(&ans, "Not enough memory for chunk data\n", ERROR_OUT_OF_MEMORY);
			return ans;
		}
		char name[8] = { tmp[4] & 0xFF, tmp[5] & 0xFF, tmp[6] & 0xFF, tmp[7] & 0xFF };
		if (strcmp(name, "IDAT") == 0)
		{
			unsigned char *t = realloc((*buf).data, ((*buf).size + size) * sizeof(char));
			if (t == NULL)
			{
				free(temp);
				makeError(&ans, "Not enough memory for new chunk\n", ERROR_OUT_OF_MEMORY);
				return ans;
			}
			(*buf).data = t;
			ret = fread(temp, 1, size, f);
			if (ret != size)
			{
				free(temp);
				makeError(&ans, "Wrong size of data in idat chunk\n", ERROR_DATA_INVALID);
				return ans;
			}
			for (int i = 0; i < size; i++)
			{
				(*buf).data[(*buf).size + i] = temp[i];
			}
			plte = 0;
			(*buf).size += size;
		}
		else if (strcmp(name, "IEND") == 0)
		{
			break;
		}
		else if (strcmp(name, "PLTE") == 0)
		{
			if ((*buf).type == 0)
			{
				free(temp);
				makeError(&ans, "Color type 0 don't expect plte chunk\n", ERROR_DATA_INVALID);
				return ans;
			}
			if (plte == 0)
			{
				free(temp);
				makeError(&ans, "Pallet chunk in wrong place\n", ERROR_DATA_INVALID);
				return ans;
			}
			(*buf).plteData = malloc(size);
			(*buf).plteSize = size / 3;
			if (!(*buf).plteData)
			{
				free(temp);
				makeError(&ans, "Cannot alloc memory for pallet\n", ERROR_OUT_OF_MEMORY);
				return ans;
			}
			plte = 0;
			ret = fread((*buf).plteData, 1, size, f);
			if (ret != size)
			{
				free(temp);
				makeError(&ans, "Wrong plte chunk size\n", ERROR_DATA_INVALID);
				return ans;
			}
		}
		else if (size == 0)
		{
			free(temp);
			makeError(&ans, "Expected IEND chunk, found unsupported\n", ERROR_DATA_INVALID);
			return ans;
		}
		else
		{
			ret = fread(temp, 1, size, f);
			if (ret != size)
			{
				free(temp);
				makeError(&ans, "Wrong chunk size\n", ERROR_DATA_INVALID);
				return ans;
			}
		}
		ret = fread(tmp, 1, 4, f);
		if (ret != 4)
		{
			free(temp);
			makeError(&ans, "Wrong chunk hashcode size\n", ERROR_DATA_INVALID);
			return ans;
		}
		free(temp);
	}
	return ans;
}
int isGrayScale(unsigned char r, unsigned char g, unsigned char b)
{
	return r == g && g == b;
}

void writeToFile(FILE *f, unsigned char *out2, struct image buf, int asP5, int size, int type, int par[])
{
	if (buf.type == 2 || asP5 == 0)
	{
		fprintf(f, "P6\n");
		fprintf(f, "%i %i\n", par[0], par[1]);
		fprintf(f, "255\n");
		fwrite(out2, 1, (size * type * ((buf.type == 3) * 3 + 1)), f);
	}
	else if (buf.type == 0 || asP5 == 1)
	{
		fprintf(f, "P5\n");
		fprintf(f, "%i %i\n", par[0], par[1]);
		fprintf(f, "255\n");
		fwrite(out2, 1, size * type, f);
	}
}
int convertRaw(int size, int type, int par[], unsigned char *out2, unsigned char *out1, struct image buf, int *asP5)
{
	int x = 0;
	int cnt = 0;
	int i = 1;
	while (i < size * type + par[1])
	{
		if (buf.type == 3)
		{
			if (out1[i] > buf.plteSize)
			{
				return -2;
			}
			for (int j = 0; j < 3; j++)
			{
				out2[x + j] = buf.plteData[3 * out1[i] + j];
			}
			if (!isGrayScale(out2[x], out2[x + 1], out2[x + 2]))
			{
				*asP5 = 0;
			}
			x += 3;
			i += 1;
		}
		else
		{
			for (int j = 0; j < type; j++)
			{
				out2[x + j] = out1[i + j];
			}
			x += type;
			i += type;
		}
		cnt++;
		if (cnt % par[0] == 0)
		{
			if (i < size * type + par[1] && (out1[i] != 0))
			{
				return -1;
			}
			i++;
		}
	}
	if (*asP5 == 1 && buf.type == 3)
	{
		cnt = 0;
		x = 0;
		i = 1;
		while (i < size * type + par[1])
		{
			out2[x] = buf.plteData[3 * out1[i]];
			x++;
			cnt++;
			i++;
			if (cnt % par[0] == 0)
			{
				i++;
			}
		}
	}
	return 0;
}
void checkFree(unsigned char *f)
{
	if (f)
	{
		free(f);
	}
}
int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		fprintf(stderr, "Wrong number of arguments expected 2\n");
		return ERROR_PARAMETER_INVALID;
	}
	int size = 8;
	struct image buf = { .data = malloc(size * 8), 100 };
	if (!buf.data)
	{
		fprintf(stderr, "Not enough memory\n");
		return ERROR_OUT_OF_MEMORY;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f)
	{
		free(buf.data);
		fprintf(stderr, "Cannot open input file\n");
		return ERROR_CANNOT_OPEN_FILE;
	}
	double n = 0;
	int ret = fread(buf.data, 1, size, f);
	if (ret != size)
	{
		fclose(f);
		free(buf.data);
		fprintf(stderr, "Wrong data in the file\n");
		return ERROR_DATA_INVALID;
	}
	int par[2] = { 0, 0 };
	struct ihdrRet ans = ihdrChunk(f, &buf, par);
	if (ans.returnCode != SUCCESS)
	{
		fclose(f);
		free(buf.data);
		fprintf(stderr, "%s", ans.text);
		return ans.returnCode;
	}
	int type = ans.type;
	size = par[0] * par[1];
	free(buf.data);
	buf.data = NULL;
	buf.size = 0;
	struct pair r = parsePNG(f, &buf);
	if (r.returnCode != SUCCESS)
	{
		fclose(f);
		checkFree(buf.data);
		checkFree(buf.plteData);
		fprintf(stderr, "%s", r.text);
		return r.returnCode;
	}
	fclose(f);
	unsigned char *out1 = malloc(sizeof(unsigned char) * size * type + par[1]);
	if (!out1)
	{
		checkFree(buf.data);
		checkFree(buf.plteData);
		fprintf(stderr, "Not enough memory for decoded data\n");
		return ERROR_OUT_OF_MEMORY;
	}
	ret = inf(buf.data, out1, buf.size, size * type + par[1]);
	if (ret == ERROR_OUT_OF_MEMORY)
	{
		checkFree(buf.data);
		checkFree(buf.plteData);
		free(out1);
		fprintf(stderr, "Not enough memory to decompress\n");
		return ret;
	}
	else if (ret == ERROR_DATA_INVALID)
	{
		checkFree(buf.data);
		checkFree(buf.plteData);
		free(out1);
		fprintf(stderr, "Wrong IDAT chunk data\n");
		return ret;
	}
	checkFree(buf.data);
	unsigned char *out2;
	out2 = malloc(sizeof(unsigned char) * size * type * (3 * (buf.type == 3) + 1));
	if (!out2)
	{
		free(out1);
		checkFree(buf.plteData);
		fprintf(stderr, "Not enough memory for decoded data\n");
		return ERROR_OUT_OF_MEMORY;
	}
	int asP5 = 1;
	ret = convertRaw(size, type, par, out2, out1, buf, &asP5);
	if (ret != 0)
	{
		free(out1);
		free(out2);
		checkFree(buf.plteData);
		if (ret == -1)
		{
			fprintf(stderr, "Unsupported filter, only support filter None\n");
			return ERROR_UNSUPPORTED;
		}
		else if (ret == -2)
		{
			fprintf(stderr, "Pallet index greater than its size\n");
			return ERROR_DATA_INVALID;
		}
	}
	f = fopen(argv[2], "wb");
	if (!f)
	{
		free(out1);
		free(out2);
		checkFree(buf.plteData);
		fprintf(stderr, "Cannot open input file\n");
		return ERROR_CANNOT_OPEN_FILE;
	}
	writeToFile(f, out2, buf, asP5, size, type, par);
	fclose(f);
	free(out1);
	free(out2);
	checkFree(buf.plteData);
}
