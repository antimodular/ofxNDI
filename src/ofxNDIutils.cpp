/*
	NDI utility functions

	using the NDI SDK to receive frames from the network

	http://NDI.NewTek.com

	Copyright (C) 2016-2021 Lynn Jarvis.

	http://www.spout.zeal.co

	=========================================================================
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
	=========================================================================

	16.10.16 - Create file
	22.02.17 - Changed lower size limit for SSE copy to 640x480
	30.03.18 - const source for memcpy_sse2 and rgba_bgra_sse2

	Changes with update to 3.5
	11.06.18 - __movsd for OSX (https://github.com/ThomasLengeling/ofxNDI)
			 - _rotl replacement for OSX

	03.12-19 - changes for Ubuntu/ARM (https://github.com/IDArnhem/ofxNDI)
			 - TODO bring up to date with Spout SDK
	04.12.19 - Cleanup
			 - TODO - use TARGET_LINUX_ARM for SSE functions?
	05.12.19 - Clean all functions
			 - justify targets with compiler definitions
	07.12.19 - rgba_bgra use more portable known-size types
			   unsigned __int32 > uint32_t (https://github.com/IDArnhem/ofxNDI)
	22.08.21 - CopyImage overloads 
			     Previous version compatibility with rgba<>bgra and invert options
				 Same size as dest with invert option
				 Line-by-line with source and dest pitch
	15.11.21 - Add timing functions
	18.11.21 - unsigned __int32 > uint32_t (https://github.com/leadedge/ofxNDI/issues/23)
			   Replace CopyMemory with memcpy for MacOS compatibility

*/
#include "ofxNDIutils.h"

// _rotl replacement
// Other solutions possible
// https://stackoverflow.com/questions/776508/best-practices-for-circular-shift-rotate-operations-in-c
#define BitsCount( val ) ( sizeof( val ) * CHAR_BIT )
#define Shift( val, steps ) ( steps % BitsCount( val ) )
#define ROL( val, steps ) ( ( val << Shift( val, steps ) ) | ( val >> ( BitsCount( val ) - Shift( val, steps ) ) ) )
#define ROR( val, steps ) ( ( val >> Shift( val, steps ) ) | ( val << ( BitsCount( val )

namespace ofxNDIutils {

#ifdef USE_CHRONO
	// Timing counters
	std::chrono::steady_clock::time_point start;
	std::chrono::steady_clock::time_point end;
#endif


#if defined (__APPLE__)

	static inline void *__movsd(void *d, const void *s, size_t n) {
#if defined(__aarch64__)
        return memcpy(d, s, n);
#else
		asm volatile ("rep movsb"
			: "=D" (d),
			"=S" (s),
			"=c" (n)
			: "0" (d),
			"1" (s),
			"2" (n)
			: "memory");
		return d;
#endif
    }
#endif


#if defined(TARGET_WIN32) || defined (TARGET_OSX)

	// movsd requires 4 byte aligned data
	void memcpy_movsd(void* dst, const void* src, size_t Size)
	{
		// one DWORD per rep move
		const unsigned long *pSrc = static_cast<const unsigned long *>(src); // Source buffer
		unsigned long *pDst = static_cast<unsigned long *>(dst); // Dest buffer
		__movsd(pDst, pSrc, Size >> 2); //Size divided by 4 (4 bytes per rep move)
	}

	//
	// Fast memcpy
	//
	// Original source - William Chan
	// (dead link) http://williamchan.ca/portfolio/assembly/ssememcpy/
	// See also :
	//	http://stackoverflow.com/questions/1715224/very-fast-memcpy-for-image-processing
	//	http://www.gamedev.net/topic/502313-special-case---faster-than-memcpy/
	//	and others.
	//
	// Approx 1.7 times speed of memcpy (0.84 msec per frame 1920x1080)
	//
	void memcpy_sse2(void* dst, const void* src, size_t Size)
	{
		char * pSrc = (char *)src;				  // Source buffer
		char * pDst = (char *)dst;				  // Destination buffer
		unsigned int n = (unsigned int)Size >> 7; // Counter = size divided by 128 (8 * 128bit registers)

		__m128i Reg0, Reg1, Reg2, Reg3, Reg4, Reg5, Reg6, Reg7;
		for (unsigned int Index = n; Index > 0; --Index) {

			// SSE2 prefetch
			_mm_prefetch(pSrc + 256, _MM_HINT_NTA);
			_mm_prefetch(pSrc + 256 + 64, _MM_HINT_NTA);

			// move data from src to registers
			// 8 x 128 bit (16 bytes each)
			// Increment source pointer by 16 bytes each
			// for a total of 128 bytes per cycle
			Reg0 = _mm_load_si128((__m128i *)(pSrc));
			Reg1 = _mm_load_si128((__m128i *)(pSrc + 16));
			Reg2 = _mm_load_si128((__m128i *)(pSrc + 32));
			Reg3 = _mm_load_si128((__m128i *)(pSrc + 48));
			Reg4 = _mm_load_si128((__m128i *)(pSrc + 64));
			Reg5 = _mm_load_si128((__m128i *)(pSrc + 80));
			Reg6 = _mm_load_si128((__m128i *)(pSrc + 96));
			Reg7 = _mm_load_si128((__m128i *)(pSrc + 112));

			// move data from registers to dest
			_mm_stream_si128((__m128i *)(pDst), Reg0);
			_mm_stream_si128((__m128i *)(pDst + 16), Reg1);
			_mm_stream_si128((__m128i *)(pDst + 32), Reg2);
			_mm_stream_si128((__m128i *)(pDst + 48), Reg3);
			_mm_stream_si128((__m128i *)(pDst + 64), Reg4);
			_mm_stream_si128((__m128i *)(pDst + 80), Reg5);
			_mm_stream_si128((__m128i *)(pDst + 96), Reg6);
			_mm_stream_si128((__m128i *)(pDst + 112), Reg7);

			pSrc += 128;
			pDst += 128;
		}
	} // end memcpy_sse2


	//
	// Adapted from : https://searchcode.com/codesearch/view/5070982/
	// 
	// Copyright (c) 2002-2010 The ANGLE Project Authors. All rights reserved.
	// Use of this source code is governed by a BSD-style license that can be
	// found in the LICENSE file.
	//
	// https://chromium.googlesource.com/angle/angle/+/master/LICENSE
	//
	// All instructions SSE2.
	//
	void rgba_bgra_sse2(const void *source, void *dest, unsigned int width, unsigned int height, bool bInvert)
	{
		uint32_t *src = nullptr;
		uint32_t *dst = nullptr;
		uint32_t rgbapix; // 32bit rgba pixel
		unsigned int x = 0;
		unsigned int y = 0;
		__m128i brMask = _mm_set1_epi32(0x00ff00ff); // argb

		for (y = 0; y < height; y++) {

			// Start of buffer
			src = (uint32_t*)source; // unsigned int = 4 bytes
			dst = (uint32_t*)dest;

			// Increment to current line
			if (bInvert) {
				src += (uint32_t)(width*height); // end of rgba buffer
				src -= (uint32_t)width;          // beginning of the last rgba line
				src -= (uint32_t)y*width;        // current line
			}
			else {
				src += (uint32_t)y*width;
			}
			dst += (uint32_t)y*width; // dest is not inverted

			// Make output writes aligned
			for (x = 0; ((reinterpret_cast<intptr_t>(&dst[x]) & 15) != 0) && x < width; x++) {
				rgbapix = src[x];
				// rgbapix << 16		: a r g b > g b a r
				//        & 0x00ff00ff  : r g b . > . b . r
				// rgbapix & 0xff00ff00 : a r g b > a . g .
				// result of or			:           a b g r
#if defined(TARGET_WIN32)
// _rotl is available
				dst[x] = (_rotl(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#else
// _rotl replacement
				dst[x] = (ROL(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#endif
			}

			for (; x + 3 < width; x += 4) {
				__m128i sourceData = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[x]));
				// Mask out g and a, which don't change
				__m128i gaComponents = _mm_andnot_si128(brMask, sourceData);
				// Mask out b and r
				__m128i brComponents = _mm_and_si128(sourceData, brMask);
				// Swap b and r
				__m128i brSwapped = _mm_shufflehi_epi16(_mm_shufflelo_epi16(brComponents, _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1));
				__m128i result = _mm_or_si128(gaComponents, brSwapped);
				_mm_store_si128(reinterpret_cast<__m128i*>(&dst[x]), result);
			}

			// Perform leftover writes
			for (; x < width; x++) {
				rgbapix = src[x];

#if defined(TARGET_WIN32)
				// _rotl is available
				dst[x] = (_rotl(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#else
				// _rotl replacement
				dst[x] = (ROL(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#endif
			}
		}
	} // end rgba_bgra_sse2
#endif

	// Without SSE
	void rgba_bgra(const void *rgba_source, void *bgra_dest, unsigned int width, unsigned int height, bool bInvert)
	{
		for (unsigned int y = 0; y < height; y++) {

			// Start of buffer
			auto source = static_cast<const uint32_t *>(rgba_source); // unsigned int = 4 bytes
			auto dest = static_cast<uint32_t *>(bgra_dest);

			// Increment to current line
			if (bInvert) {
				// https://docs.microsoft.com/en-us/visualstudio/code-quality/c26451?view=vs-2017
				source += (unsigned long)((height - 1 - y)*width);
				dest += (unsigned long)(y * width); // dest is not inverted
			}
			else {
				source += (unsigned long)(y * width);
				dest += (unsigned long)(y * width);
			}

			for (unsigned int x = 0; x < width; x++) {
				auto rgbapix = source[x];
#if defined(TARGET_WIN32)
				// _rotl is available
				dest[x] = (_rotl(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#else
				// _rotl replacement
				dest[x] = (ROL(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#endif
			}
		}
	} // end rgba_bgra


	// Flip a buffer in place
	void FlipBuffer(const unsigned char *src,
		unsigned char *dst,
		unsigned int width,
		unsigned int height)
	{
		const unsigned char * From = src;
		unsigned char * To = dst;
		unsigned int pitch = width * 4; // RGBA default
		unsigned int line_s = 0;
		unsigned int line_t = (height - 1)*pitch;

		for (unsigned int y = 0; y < height; y++) {
			// @zilog no SSE stuff for aarch64 build
#if defined(TARGET_WIN32) || defined (TARGET_OSX)
			if (width <= 512 || height <= 512) // too small for assembler
				memcpy((void *)(To + line_t), (void *)(From + line_s), pitch);
			else if ((pitch % 16) == 0) // use sse assembler function
				memcpy_sse2((void *)(To + line_t), (void *)(From + line_s), pitch);
			else if ((pitch % 4) == 0) // use 4 byte move assembler function
				memcpy_movsd((unsigned long *)(To + line_t), (unsigned long *)(From + line_s), pitch);
			else
#endif
				memcpy((void *)(To + line_t), (void *)(From + line_s), pitch);

			line_s += pitch;
			line_t -= pitch;
		}
	} // end FlipBuffer

	//
	// Flip an image vertically
	//
	// http://www.codeproject.com/Questions/369873/How-can-i-flip-the-image-Vertically-using-cplusplu
	//
	bool FlipVertical(unsigned char *inbuf, long widthBytes, long height)
	{
		unsigned char *tb1;
		unsigned char *tb2;
		long bufsize;
		long row_cnt;
		long off1 = 0;
		long off2 = 0;

		if (inbuf == NULL)
			return false;

		bufsize = widthBytes * 4;

		tb1 = (unsigned char *)malloc(bufsize);
		if (tb1 == NULL) {
			return false;
		}

		tb2 = (unsigned char *)malloc(bufsize);
		if (tb2 == NULL) {
			free((void *)tb1);
			return false;
		}

		for (row_cnt = 0; row_cnt < (height + 1) / 2; row_cnt++)
		{
			off1 = row_cnt * bufsize;
			off2 = ((height - 1) - row_cnt)*bufsize;
			memcpy((void *)tb1, (void *)(inbuf + off1), bufsize * sizeof(unsigned char));
			memcpy((void *)tb2, (void *)(inbuf + off2), bufsize * sizeof(unsigned char));
			memcpy((void *)(inbuf + off1), (void *)tb2, bufsize * sizeof(unsigned char));
			memcpy((void *)(inbuf + off2), (void *)tb1, bufsize * sizeof(unsigned char));
		}

		free((void*)tb1);
		free((void*)tb2);

		return true;
	}

	// Copy rgba source image to dest.
	// Images must be the same size with no line padding.
	// Option flip image vertically (invert).
	void CopyImage(const unsigned char *source, unsigned char *dest,
		unsigned int width, unsigned int height, bool bInvert)
	{
		CopyImage(source, dest,	width, height, width*4, false, bInvert);

	} // end CopyImage

	// Copy rgba source image to dest.
	// Source line pitch (unused).
	// Option convert bgra<>rgba.
	// Option flip image vertically (invert).
	void CopyImage(const unsigned char *source, unsigned char *dest,
		unsigned int width, unsigned int height, unsigned int stride,
		bool bSwapRB, bool bInvert)
	{
		if (source == nullptr || dest == nullptr)
			return;

		// user requires bgra->rgba or rgba->bgra conversion from source to dest
		if (bSwapRB) {
#if defined(TARGET_WIN32) || defined (TARGET_OSX)
			rgba_bgra_sse2((const void *)source, (void *)dest, width, height, bInvert);
#else
			rgba_bgra((const void *)source, (void *)dest, width, height, bInvert);
#endif
			return;
		}

		if (bInvert) { // Flip the image in place
			FlipBuffer(source, dest, width, height);
		}
		else {
#if defined(TARGET_WIN32) || defined (TARGET_OSX)
			// Small image just use memcpy
			if (width < 512 || height < 256) {
				memcpy((void *)dest, (const void *)source, height*stride);
			}
			else if ((stride % 16) == 0) { // 16 byte aligned
				memcpy_sse2((void *)dest, (const void *)source, height*stride);
			}
			else if ((stride % 4) == 0) { // 4 byte aligned
				memcpy_movsd((void*)dest, (const void *)source, height*stride);
			}
#else
			// @zilog no SSE stuff for aarch64 build
			memcpy((void *)dest, (const void *)source, height*stride);
#endif
		}
	} // end CopyImage


	// Copy rgba image buffers line by line.
	// Allow for both source and destination line pitch.
	// Option flip image vertically (invert).
	void CopyImage(const void* rgba_source, void* rgba_dest,
		unsigned int width, unsigned int height,
		unsigned int sourcePitch, unsigned int destPitch,
		bool bInvert)
	{
		// For all rows
		for (unsigned int y = 0; y < height; y++) {
			// Start of buffers
			auto source = static_cast<const uint32_t *>(rgba_source); // unsigned int = 4 bytes
			auto dest = static_cast<uint32_t *>(rgba_dest);
			// Increment to current line
			// Pitch is line length in bytes. Divide by 4 to get the width in rgba pixels.
			if (bInvert) {
				source += (unsigned long)((height - 1 - y)*sourcePitch / 4);
				dest   += (unsigned long)(y * destPitch / 4); // dest is not inverted
			}
			else {
				source += (unsigned long)(y * sourcePitch / 4);
				dest   += (unsigned long)(y * destPitch / 4);
			}

			// Copy the line
			memcpy((void *)dest, (const void *)source, width * 4);
		}
	}


	//
	//        YUV422_to_RGBA
	//
	// Y sampled at every pixel
	// U and V sampled at every second pixel 
	// 2 pixels in 1 DWORD
	//
	//	R = Y + 1.403V
	//	G = Y - 0.344U - 0.714V
	//	B = Y + 1.770U
	//
	//	R = 298*y + 409*v + 128 >> 8
	//  G = 298*y - 100*u-208*v +128 >> 8
	//  B = 298*y + 516*u +128 >> 8
	//
	void YUV422_to_RGBA(const unsigned char * source, unsigned char * dest, unsigned int width, unsigned int height, unsigned int stride)
	{
		// Clamp out of range values
		#define CLAMPRGB(t) (((t)>255)?255:(((t)<0)?0:(t)))

		const unsigned char *yuv = source;
		unsigned char *rgba = dest;
		int r1 = 0 , g1 = 0 , b1 = 0; // , a1 = 0;
		int r2 = 0 , g2 = 0 , b2 = 0; // a2 = 0;
		int u0 = 0 , y0 = 0 , v0 = 0, y1 = 0;

		unsigned int padding = stride - width*4;

	    // Loop through 4 bytes at a time
		for (unsigned int y = 0; y <height; y ++ ) {
			for (unsigned int x = 0; x <width*2; x +=4 ) {
				u0  = (int)*yuv++;
				y0  = (int)*yuv++;
				v0  = (int)*yuv++;
				y1  = (int)*yuv++;
				// u and v are +-0.5
				u0 -= 128;
				v0 -= 128;

				// Color space conversion for RGB
				r1 = CLAMPRGB((298*y0+409*v0+128)>>8);
				g1 = CLAMPRGB((298*y0-100*u0-208*v0+128)>>8);
				b1 = CLAMPRGB((298*y0+516*u0+128)>>8);
				r2 = CLAMPRGB((298*y1+409*v0+128)>>8);
				g2 = CLAMPRGB((298*y1-100*u0-208*v0+128)>>8);
				b2 = CLAMPRGB((298*y1+516*u0+128)>>8);

				*rgba++ = (unsigned char)r1;
				*rgba++ = (unsigned char)g1;
				*rgba++ = (unsigned char)b1;
				*rgba++ = 255;
				*rgba++ = (unsigned char)r2;
				*rgba++ = (unsigned char)g2;
				*rgba++ = (unsigned char)b2;
				*rgba++ = 255;
			}
			yuv += width*2; // half width source data
			yuv += padding; // if any
		}
	}  // end YUV422_to_RGBA

#ifdef USE_CHRONO
	// Timing functions
	void StartTiming() {
		start = std::chrono::steady_clock::now();
	}

	double EndTiming() {
		end = std::chrono::steady_clock::now();
		double elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
		printf("    elapsed [%06.2f] msec\n", elapsed / 1000.0);
		// printf("elapsed [%.3f] u/sec\n", elapsed);
		return elapsed / 1000.0; // msec
	}
#endif

} // end namespace

