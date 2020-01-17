/*
 * expand.c - decompressor implementation
 *
 * Copyright (C) 2019 Emmanuel Marty
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/*
 * Uses the libdivsufsort library Copyright (c) 2003-2008 Yuta Mori
 *
 * Inspired by cap by Sven-�ke Dahl. https://github.com/svendahl/cap
 * Also inspired by Charles Bloom's compression blog. http://cbloomrants.blogspot.com/
 * With ideas from LZ4 by Yann Collet. https://github.com/lz4/lz4
 * With help and support from spke <zxintrospec@gmail.com>
 *
 */

#include <stdlib.h>
#include <string.h>
#include "format.h"
#include "expand.h"
#include "libapultra.h"

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else /* _MSC_VER */
#define FORCE_INLINE __attribute__((always_inline))
#endif /* _MSC_VER */

static inline FORCE_INLINE int apultra_read_bit(const unsigned char **ppInBlock, const unsigned char *pDataEnd, int *nCurBitMask, unsigned char *bits, const int nBitBufferIdx) {
   const unsigned char *pInBlock = *ppInBlock;
   int nBit;

   if (nCurBitMask[nBitBufferIdx] == 0) {
      if (pInBlock >= pDataEnd) return -1;
      bits[nBitBufferIdx] = *pInBlock++;
      nCurBitMask[nBitBufferIdx] = 128;
   }

   nBit = (bits[nBitBufferIdx] & 128) ? 1 : 0;

   bits[nBitBufferIdx] <<= 1;
   nCurBitMask[nBitBufferIdx] >>= 1;

   *ppInBlock = pInBlock;
   return nBit;
}

static inline FORCE_INLINE int apultra_read_gamma2(const unsigned char **ppInBlock, const unsigned char *pDataEnd, int *nCurBitMask, unsigned char *bits, const int nBitBufferIdx) {
   int bit;
   unsigned int v = 1;

   if (nBitBufferIdx == 0) {
      /* Standard aPLib encoding. */
      do {
         v = (v << 1) + apultra_read_bit(ppInBlock, pDataEnd, nCurBitMask, bits, nBitBufferIdx);
         bit = apultra_read_bit(ppInBlock, pDataEnd, nCurBitMask, bits, nBitBufferIdx);
         if (bit < 0) return bit;
      } while (bit);
   }
   else {
      /* Enhanced encoding for 8-bit microprocessors ...
       * 1) Write out values of 256 and higher lo-byte first (so the gamma2 decoder only needs to rotate a byte).
       * 2) Swap meaning of continue/stop bits (saves a byte on the 6502, with no effect on other platforms).
       */
      int l = 0;

      do {
         if ((l == 0) && (v >= 256)) {
            l = v;
            v = 1;
         }
         v = (v << 1) + apultra_read_bit(ppInBlock, pDataEnd, nCurBitMask, bits, nBitBufferIdx);
         bit = apultra_read_bit(ppInBlock, pDataEnd, nCurBitMask, bits, nBitBufferIdx);
         if (bit < 0) return bit;
      } while (bit == 0);

      if (l != 0) {
         v = (v << 8) + (l & 255);
      }
   }

   return v;
}

/**
 * Get maximum decompressed size of compressed data
 *
 * @param pInputData compressed data
 * @param nInputSize compressed size in bytes
 * @param nFlags compression flags (a bitmask of APULTRA_FLAG_xxx, or 0)
 *
 * @return maximum decompressed size
 */
size_t apultra_get_max_decompressed_size(const unsigned char *pInputData, size_t nInputSize, const unsigned int nFlags) {
   const unsigned char *pInputDataEnd = pInputData + nInputSize;
   int nCurBitMask[3] = { 0, 0, 0 };
   unsigned char bits[3] = { 0, 0, 0 };
   int nMatchOffset = 1;
   int nFollowsLiteral = 1;
   size_t nDecompressedSize = 0;
   int nSingleBitBufferIdx = 0;
   int nGammaBitBufferIdx = (nFlags & APULTRA_FLAG_ENHANCED) ? 1 : 0;
   int nNibblesBitBufferIdx = (nFlags & APULTRA_FLAG_ENHANCED) ? 2 : 0;

   if (pInputData >= pInputDataEnd)
      return -1;
   pInputData++;
   nDecompressedSize++;

   while (1) {
      unsigned int nResult;

      nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nSingleBitBufferIdx);
      if (nResult < 0) return -1;

      if (!nResult) {
         /* '0': literal */
         if (pInputData < pInputDataEnd) {
            pInputData++;
            nDecompressedSize++;
            nFollowsLiteral = 1;
         }
         else {
            return -1;
         }
      }
      else {
         nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nSingleBitBufferIdx);
         if (nResult < 0) return -1;

         if (nResult == 0) {
            unsigned int nMatchLen;
            unsigned int nMatchLenBias = 0;
            unsigned int nIsRepMatch = 0;

            /* '10': 8+n bits offset */
            unsigned int nMatchOffsetHi = apultra_read_gamma2(&pInputData, pInputDataEnd, nCurBitMask, bits, nGammaBitBufferIdx);
            if (nFollowsLiteral == 0 || nMatchOffsetHi != 2) {
               if (nFollowsLiteral)
                  nMatchOffset = (nMatchOffsetHi - 3) << 8;
               else
                  nMatchOffset = (nMatchOffsetHi - 2) << 8;
               nMatchOffset |= (unsigned int)(*pInputData++);

               if (nMatchOffset < 128)
                  nMatchLenBias = 2;
            }
            else {
               /* else rep-match */
               nIsRepMatch = 1;
            }

            nFollowsLiteral = 0;
            nMatchLen = apultra_read_gamma2(&pInputData, pInputDataEnd, nCurBitMask, bits, nGammaBitBufferIdx);

            if (!nIsRepMatch) {
               if (nMatchOffset >= MINMATCH3_OFFSET)
                  nMatchLen++;
               if (nMatchOffset >= MINMATCH4_OFFSET)
                  nMatchLen++;
            }

            nMatchLen += nMatchLenBias;
            nDecompressedSize += nMatchLen;
         }
         else {
            nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nSingleBitBufferIdx);
            if (nResult < 0) return -1;

            if (nResult == 0) {
               unsigned int nCommand;
               unsigned int nMatchLen;

               /* '110': 7 bits offset + 1 bit length */
               nCommand = (unsigned int)(*pInputData++);
               if (nCommand == 0x00) {
                  /* EOD. No match len follows. */
                  break;
               }

               /* Bits 7-1: offset; bit 0: length */
               nMatchOffset = (nCommand >> 1);
               nMatchLen = (nCommand & 1) + 2;

               nFollowsLiteral = 0;
               nDecompressedSize += nMatchLen;
            }
            else {
               unsigned int nShortMatchOffset;

               /* '111': 4 bit offset */
               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset = nResult << 3;

               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset |= nResult << 2;

               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset |= nResult << 1;

               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset |= nResult << 0;

               nFollowsLiteral = 1;
               nDecompressedSize++;
            }
         }
      }
   }

   return nDecompressedSize;
}

/**
 * Decompress data in memory
 *
 * @param pInputData compressed data
 * @param pOutBuffer buffer for decompressed data
 * @param nInputSize compressed size in bytes
 * @param nMaxOutBufferSize maximum capacity of decompression buffer
 * @param nFlags compression flags (a bitmask of APULTRA_FLAG_xxx, or 0)
 *
 * @return actual decompressed size, or -1 for error
 */
size_t apultra_decompress(const unsigned char *pInputData, unsigned char *pOutData, size_t nInputSize, size_t nMaxOutBufferSize, const unsigned int nFlags) {
   const unsigned char *pInputDataEnd = pInputData + nInputSize;
   unsigned char *pCurOutData = pOutData;
   const unsigned char *pOutDataEnd = pCurOutData + nMaxOutBufferSize;
   const unsigned char *pOutDataFastEnd = pOutDataEnd - 20;
   int nCurBitMask[3] = { 0, 0, 0 };
   unsigned char bits[3] = { 0, 0, 0 };
   int nMatchOffset = 1;
   int nFollowsLiteral = 1;
   int nSingleBitBufferIdx = 0;
   int nGammaBitBufferIdx = (nFlags & APULTRA_FLAG_ENHANCED) ? 1 : 0;
   int nNibblesBitBufferIdx = (nFlags & APULTRA_FLAG_ENHANCED) ? 2 : 0;

   if (pInputData >= pInputDataEnd && pCurOutData < pOutDataEnd)
      return -1;
   *pCurOutData++ = *pInputData++;

   while (1) {
      unsigned int nResult;

      nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nSingleBitBufferIdx);
      if (nResult < 0) return -1;

      if (!nResult) {
         /* '0': literal */
         if (pInputData < pInputDataEnd && pCurOutData < pOutDataEnd) {
            *pCurOutData++ = *pInputData++;
            nFollowsLiteral = 1;
         }
         else {
            return -1;
         }
      }
      else {
         nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nSingleBitBufferIdx);
         if (nResult < 0) return -1;

         if (nResult == 0) {
            unsigned int nMatchLen;
            unsigned int nMatchLenBias = 0;
            unsigned int nIsRepMatch = 0;

            /* '10': 8+n bits offset */
            unsigned int nMatchOffsetHi = apultra_read_gamma2(&pInputData, pInputDataEnd, nCurBitMask, bits, nGammaBitBufferIdx);
            if (nFollowsLiteral == 0 || nMatchOffsetHi != 2) {
               if (nFollowsLiteral)
                  nMatchOffset = (nMatchOffsetHi - 3) << 8;
               else
                  nMatchOffset = (nMatchOffsetHi - 2) << 8;
               nMatchOffset |= (unsigned int)(*pInputData++);

               if (nMatchOffset < 128)
                  nMatchLenBias = 2;
            }
            else {
               /* else rep-match */
               nIsRepMatch = 1;
            }

            nFollowsLiteral = 0;
            const unsigned char *pSrc = pCurOutData - nMatchOffset;
            if (pSrc >= pOutData) {
               nMatchLen = apultra_read_gamma2(&pInputData, pInputDataEnd, nCurBitMask, bits, nGammaBitBufferIdx);

               if (!nIsRepMatch) {
                  if (nMatchOffset >= MINMATCH3_OFFSET)
                     nMatchLen++;
                  if (nMatchOffset >= MINMATCH4_OFFSET)
                     nMatchLen++;
               }

               nMatchLen += nMatchLenBias;

               if (nMatchLen < 11 && nMatchOffset >= 8 && pCurOutData < pOutDataFastEnd) {
                  memcpy(pCurOutData, pSrc, 8);
                  memcpy(pCurOutData + 8, pSrc + 8, 2);
                  pCurOutData += nMatchLen;
               }
               else {
                  if ((pCurOutData + nMatchLen) <= pOutDataEnd && (pSrc + nMatchLen) <= pOutDataEnd) {
                     /* Do a deterministic, left to right byte copy instead of memcpy() so as to handle overlaps */

                     if (nMatchOffset >= 16 && (pCurOutData + nMatchLen) < (pOutDataFastEnd - 15)) {
                        const unsigned char *pCopySrc = pSrc;
                        unsigned char *pCopyDst = pCurOutData;
                        const unsigned char *pCopyEndDst = pCurOutData + nMatchLen;

                        do {
                           memcpy(pCopyDst, pCopySrc, 16);
                           pCopySrc += 16;
                           pCopyDst += 16;
                        } while (pCopyDst < pCopyEndDst);

                        pCurOutData += nMatchLen;
                     }
                     else {
                        while (nMatchLen) {
                           *pCurOutData++ = *pSrc++;
                           nMatchLen--;
                        }
                     }
                  }
                  else {
                     return -1;
                  }
               }
            }
            else {
               return -1;
            }
         }
         else {
            nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nSingleBitBufferIdx);
            if (nResult < 0) return -1;

            if (nResult == 0) {
               unsigned int nCommand;
               unsigned int nMatchLen;

               /* '110': 7 bits offset + 1 bit length */
               nCommand = (unsigned int)(*pInputData++);
               if (nCommand == 0x00) {
                  /* EOD. No match len follows. */
                  break;
               }

               /* Bits 7-1: offset; bit 0: length */
               nMatchOffset = (nCommand >> 1);
               nMatchLen = (nCommand & 1) + 2;

               nFollowsLiteral = 0;
               const unsigned char *pSrc = pCurOutData - nMatchOffset;
               if (pSrc >= pOutData && (pSrc + nMatchLen) <= pOutDataEnd) {
                  if (nMatchOffset >= 8 && pCurOutData < pOutDataFastEnd) {
                     memcpy(pCurOutData, pSrc, 8);
                     memcpy(pCurOutData + 8, pSrc + 8, 2);
                     pCurOutData += nMatchLen;
                  }
                  else {
                     if ((pCurOutData + nMatchLen) <= pOutDataEnd) {
                        while (nMatchLen) {
                           *pCurOutData++ = *pSrc++;
                           nMatchLen--;
                        }
                     }
                     else {
                        return -1;
                     }
                  }
               }
               else {
                  return -1;
               }
            }
            else {
               unsigned int nShortMatchOffset;

               /* '111': 4 bit offset */
               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset = nResult << 3;

               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset |= nResult << 2;

               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset |= nResult << 1;

               nResult = apultra_read_bit(&pInputData, pInputDataEnd, nCurBitMask, bits, nNibblesBitBufferIdx);
               if (nResult < 0) return -1;
               nShortMatchOffset |= nResult << 0;

               nFollowsLiteral = 1;
               if (nShortMatchOffset) {
                  /* Short offset, 1-15 */
                  const unsigned char *pSrc = pCurOutData - nShortMatchOffset;
                  if (pSrc >= pOutData && (pCurOutData + 1) <= pOutDataEnd && (pSrc + 1) <= pOutDataEnd) {
                     *pCurOutData++ = *pSrc++;
                  }
                  else {
                     return -1;
                  }
               }
               else {
                  /* Write zero */
                  if ((pCurOutData + 1) <= pOutDataEnd) {
                     *pCurOutData++ = 0;
                  }
                  else {
                     return -1;
                  }
               }
            }
         }
      }
   }

   return (size_t)(pCurOutData - pOutData);
}
