/* 7zDec.c -- Decoding from 7z folder
: Igor Pavlov : Public domain */

#include "Precomp.h"

#include <string.h>

/* #define Z7_PPMD_SUPPORT */

#include "7z.h"
#include "7zCrc.h"

#include "Bcj2.h"
#include "Bra.h"
#include "CpuArch.h"
#include "Delta.h"
#include "LzmaDec.h"
#include "Lzma2Dec.h"
#ifdef Z7_PPMD_SUPPORT
#include "Ppmd7.h"
#endif

/* 7zAES support (decode-only). Supplied by 7zAesDec.c. */
#include "7zAesDec.h"

#define k_Copy 0
#ifndef Z7_NO_METHOD_LZMA2
#define k_LZMA2 0x21
#endif
#define k_LZMA  0x30101
#define k_BCJ2  0x303011B

/* ---- in-memory ILookInStream (used to feed decrypted bytes to inner decoders) ----
 * 7zAES is an input filter: the packed stream is AES-CBC decrypted before the
 * main method decompresses it. When a folder carries the 7zAES coder we read the
 * whole packed stream, decrypt it into a memory buffer, and hand that buffer to
 * the main-method decoder through this read-only look-stream. */

typedef struct {
  ILookInStream vt;
  const Byte *data;
  size_t size;
  size_t pos;
} CZ7MemLook;

static SRes Z7MemLook_Look(ILookInStreamPtr pp, const void **buf, size_t *size) {
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CZ7MemLook)
  {
    size_t avail = p->size - p->pos;
    if (*size > avail) *size = avail;
    *buf = p->data + p->pos;
  }
  return SZ_OK;
}

static SRes Z7MemLook_Skip(ILookInStreamPtr pp, size_t offset) {
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CZ7MemLook)
  if (offset > p->size - p->pos) return SZ_ERROR_INPUT_EOF;
  p->pos += offset;
  return SZ_OK;
}

static SRes Z7MemLook_Read(ILookInStreamPtr pp, void *buf, size_t *size) {
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CZ7MemLook)
  {
    size_t avail = p->size - p->pos;
    if (*size > avail) *size = avail;
    memcpy(buf, p->data + p->pos, *size);
    p->pos += *size;
  }
  return SZ_OK;
}

static SRes Z7MemLook_Seek(ILookInStreamPtr pp, Int64 *pos, ESzSeek origin) {
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CZ7MemLook)
  {
    Int64 base = (origin == SZ_SEEK_SET) ? 0
               : (origin == SZ_SEEK_CUR) ? (Int64)p->pos
               : (Int64)p->size;
    Int64 np = base + *pos;
    if (np < 0 || (UInt64)np > p->size) return SZ_ERROR_INPUT_EOF;
    p->pos = (size_t)np;
    *pos = np;
  }
  return SZ_OK;
}

static void Z7MemLook_CreateVTable(CZ7MemLook *p, const Byte *data, size_t size) {
  p->vt.Look = Z7MemLook_Look;
  p->vt.Skip = Z7MemLook_Skip;
  p->vt.Read = Z7MemLook_Read;
  p->vt.Seek = Z7MemLook_Seek;
  p->data = data;
  p->size = size;
  p->pos = 0;
}

#if !defined(Z7_NO_METHODS_FILTERS)
#define Z7_USE_BRANCH_FILTER
#endif

#if !defined(Z7_NO_METHODS_FILTERS) || \
     defined(Z7_USE_NATIVE_BRANCH_FILTER) && defined(MY_CPU_ARM64)
#define Z7_USE_FILTER_ARM64
#ifndef Z7_USE_BRANCH_FILTER
#define Z7_USE_BRANCH_FILTER
#endif
#define k_ARM64 0xa
#endif

#if !defined(Z7_NO_METHODS_FILTERS) || \
     defined(Z7_USE_NATIVE_BRANCH_FILTER) && defined(MY_CPU_ARMT)
#define Z7_USE_FILTER_ARMT
#ifndef Z7_USE_BRANCH_FILTER
#define Z7_USE_BRANCH_FILTER
#endif
#define k_ARMT  0x3030701
#endif

#ifndef Z7_NO_METHODS_FILTERS
#define k_Delta 3
#define k_RISCV 0xb
#define k_BCJ   0x3030103
#define k_PPC   0x3030205
#define k_IA64  0x3030401
#define k_ARM   0x3030501
#define k_SPARC 0x3030805
#endif

#ifdef Z7_PPMD_SUPPORT

#define k_PPMD 0x30401

typedef struct
{
  IByteIn vt;
  const Byte *cur;
  const Byte *end;
  const Byte *begin;
  UInt64 processed;
  BoolInt extra;
  SRes res;
  ILookInStreamPtr inStream;
} CByteInToLook;

static Byte ReadByte(IByteInPtr pp)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CByteInToLook)
  if (p->cur != p->end)
    return *p->cur++;
  if (p->res == SZ_OK)
  {
    size_t size = (size_t)(p->cur - p->begin);
    p->processed += size;
    p->res = ILookInStream_Skip(p->inStream, size);
    size = (1 << 25);
    p->res = ILookInStream_Look(p->inStream, (const void **)&p->begin, &size);
    p->cur = p->begin;
    p->end = p->begin + size;
    if (size != 0)
      return *p->cur++;
  }
  p->extra = True;
  return 0;
}

static SRes SzDecodePpmd(const Byte *props, unsigned propsSize, UInt64 inSize, ILookInStreamPtr inStream,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain)
{
  CPpmd7 *ppmd;
  SRes res;
  unsigned order;
  UInt32 memSize;

  if (propsSize != 5)
    return SZ_ERROR_UNSUPPORTED;
  order = props[0];
  memSize = GetUi32(props + 1);
  if (order < PPMD7_MIN_ORDER ||
      order > PPMD7_MAX_ORDER ||
      memSize < PPMD7_MIN_MEM_SIZE ||
      memSize > PPMD7_MAX_MEM_SIZE)
    return SZ_ERROR_UNSUPPORTED;
  if ((ppmd = (CPpmd7 *)ISzAlloc_Alloc(allocMain, sizeof(CPpmd7))) == NULL)
    return SZ_ERROR_MEM;
  Ppmd7_Construct(ppmd);
  res = SZ_ERROR_MEM;
  if (Ppmd7_Alloc(ppmd, memSize, allocMain))
  {
    CByteInToLook s;
    s.vt.Read = ReadByte;
    s.inStream = inStream;
    s.begin = s.end = s.cur = NULL;
    s.extra = False;
    s.res = SZ_OK;
    s.processed = 0;

    Ppmd7_Init(ppmd, order);
    ppmd->rc.dec.Stream = &s.vt;
    res = SZ_ERROR_DATA;
    if (Ppmd7z_RangeDec_Init(&ppmd->rc.dec) && !s.extra)
    {
      Byte *buf = outBuffer;
      const Byte *lim = buf + outSize;
      for (; buf != lim; buf++)
      {
        int sym = Ppmd7z_DecodeSymbol(ppmd);
        if (s.extra || sym < 0)
          break;
        *buf = (Byte)sym;
      }
      if (buf == lim)
        if (Ppmd7z_RangeDec_IsFinishedOK(&ppmd->rc.dec)
            // || (Ppmd7z_DecodeSymbol(&ppmd) == PPMD7_SYM_END && Ppmd7z_RangeDec_IsFinishedOK(&ppmd.rc.dec))
            )
          res = SZ_OK;
    }
    if (s.extra)
      res = (s.res != SZ_OK ? s.res : SZ_ERROR_DATA);
    else if (s.processed + (size_t)(s.cur - s.begin) != inSize)
      res = SZ_ERROR_DATA;
    Ppmd7_Free(ppmd, allocMain);
  }
  ISzAlloc_Free(allocMain, ppmd);
  return res;
}

#endif


static SRes SzDecodeLzma(const Byte *props, unsigned propsSize, UInt64 inSize, ILookInStreamPtr inStream,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain)
{
  CLzmaDec state;
  SRes res = SZ_OK;

  LzmaDec_CONSTRUCT(&state)
  RINOK(LzmaDec_AllocateProbs(&state, props, propsSize, allocMain))
  state.dic = outBuffer;
  state.dicBufSize = outSize;
  LzmaDec_Init(&state);

  for (;;)
  {
    const void *inBuf = NULL;
    size_t lookahead = (1 << 18);
    if (lookahead > inSize)
      lookahead = (size_t)inSize;
    res = ILookInStream_Look(inStream, &inBuf, &lookahead);
    if (res != SZ_OK)
      break;

    {
      SizeT inProcessed = (SizeT)lookahead, dicPos = state.dicPos;
      ELzmaStatus status;
      res = LzmaDec_DecodeToDic(&state, outSize, (const Byte *)inBuf, &inProcessed, LZMA_FINISH_END, &status);
      lookahead -= inProcessed;
      inSize -= inProcessed;
      if (res != SZ_OK)
        break;

      if (status == LZMA_STATUS_FINISHED_WITH_MARK)
      {
        if (outSize != state.dicPos || inSize != 0)
          res = SZ_ERROR_DATA;
        break;
      }

      if (outSize == state.dicPos && inSize == 0 && status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
        break;

      if (inProcessed == 0 && dicPos == state.dicPos)
      {
        res = SZ_ERROR_DATA;
        break;
      }

      res = ILookInStream_Skip(inStream, inProcessed);
      if (res != SZ_OK)
        break;
    }
  }

  LzmaDec_FreeProbs(&state, allocMain);
  return res;
}


#ifndef Z7_NO_METHOD_LZMA2

static SRes SzDecodeLzma2(const Byte *props, unsigned propsSize, UInt64 inSize, ILookInStreamPtr inStream,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain)
{
  CLzma2Dec state;
  SRes res = SZ_OK;

  Lzma2Dec_CONSTRUCT(&state)
  if (propsSize != 1)
    return SZ_ERROR_DATA;
  RINOK(Lzma2Dec_AllocateProbs(&state, props[0], allocMain))
  state.decoder.dic = outBuffer;
  state.decoder.dicBufSize = outSize;
  Lzma2Dec_Init(&state);

  for (;;)
  {
    const void *inBuf = NULL;
    size_t lookahead = (1 << 18);
    if (lookahead > inSize)
      lookahead = (size_t)inSize;
    res = ILookInStream_Look(inStream, &inBuf, &lookahead);
    if (res != SZ_OK)
      break;

    {
      SizeT inProcessed = (SizeT)lookahead, dicPos = state.decoder.dicPos;
      ELzmaStatus status;
      res = Lzma2Dec_DecodeToDic(&state, outSize, (const Byte *)inBuf, &inProcessed, LZMA_FINISH_END, &status);
      lookahead -= inProcessed;
      inSize -= inProcessed;
      if (res != SZ_OK)
        break;

      if (status == LZMA_STATUS_FINISHED_WITH_MARK)
      {
        if (outSize != state.decoder.dicPos || inSize != 0)
          res = SZ_ERROR_DATA;
        break;
      }

      if (inProcessed == 0 && dicPos == state.decoder.dicPos)
      {
        res = SZ_ERROR_DATA;
        break;
      }

      res = ILookInStream_Skip(inStream, inProcessed);
      if (res != SZ_OK)
        break;
    }
  }

  Lzma2Dec_FreeProbs(&state, allocMain);
  return res;
}

#endif


static SRes SzDecodeCopy(UInt64 inSize, ILookInStreamPtr inStream, Byte *outBuffer)
{
  while (inSize > 0)
  {
    const void *inBuf;
    size_t curSize = (1 << 18);
    if (curSize > inSize)
      curSize = (size_t)inSize;
    RINOK(ILookInStream_Look(inStream, &inBuf, &curSize))
    if (curSize == 0)
      return SZ_ERROR_INPUT_EOF;
    memcpy(outBuffer, inBuf, curSize);
    outBuffer += curSize;
    inSize -= curSize;
    RINOK(ILookInStream_Skip(inStream, curSize))
  }
  return SZ_OK;
}

static BoolInt IS_MAIN_METHOD(UInt32 m)
{
  switch (m)
  {
    case k_Copy:
    case k_LZMA:
  #ifndef Z7_NO_METHOD_LZMA2
    case k_LZMA2:
  #endif
  #ifdef Z7_PPMD_SUPPORT
    case k_PPMD:
  #endif
      return True;
    default:
      return False;
  }
}

static BoolInt IS_SUPPORTED_CODER(const CSzCoderInfo *c)
{
  return
      c->NumStreams == 1
      /* && c->MethodID <= (UInt32)0xFFFFFFFF */
      && IS_MAIN_METHOD((UInt32)c->MethodID);
}

#define IS_BCJ2(c) ((c)->MethodID == k_BCJ2 && (c)->NumStreams == 4)

static SRes CheckSupportedFolder(const CSzFolder *f)
{
  if (f->NumCoders < 1 || f->NumCoders > 4)
    return SZ_ERROR_UNSUPPORTED;
  if (!IS_SUPPORTED_CODER(&f->Coders[0]))
    return SZ_ERROR_UNSUPPORTED;
  if (f->NumCoders == 1)
  {
    if (f->NumPackStreams != 1 || f->PackStreams[0] != 0 || f->NumBonds != 0)
      return SZ_ERROR_UNSUPPORTED;
    return SZ_OK;
  }
  
  
  #if defined(Z7_USE_BRANCH_FILTER)

  if (f->NumCoders == 2)
  {
    const CSzCoderInfo *c = &f->Coders[1];
    if (
        /* c->MethodID > (UInt32)0xFFFFFFFF || */
        c->NumStreams != 1
        || f->NumPackStreams != 1
        || f->PackStreams[0] != 0
        || f->NumBonds != 1
        || f->Bonds[0].InIndex != 1
        || f->Bonds[0].OutIndex != 0)
      return SZ_ERROR_UNSUPPORTED;
    switch ((UInt32)c->MethodID)
    {
    #if !defined(Z7_NO_METHODS_FILTERS)
      case k_Delta:
      case k_BCJ:
      case k_PPC:
      case k_IA64:
      case k_SPARC:
      case k_ARM:
      case k_RISCV:
    #endif
    #ifdef Z7_USE_FILTER_ARM64
      case k_ARM64:
    #endif
    #ifdef Z7_USE_FILTER_ARMT
      case k_ARMT:
    #endif
        break;
      default:
        return SZ_ERROR_UNSUPPORTED;
    }
    return SZ_OK;
  }

  #endif

  
  if (f->NumCoders == 4)
  {
    if (!IS_SUPPORTED_CODER(&f->Coders[1])
        || !IS_SUPPORTED_CODER(&f->Coders[2])
        || !IS_BCJ2(&f->Coders[3]))
      return SZ_ERROR_UNSUPPORTED;
    if (f->NumPackStreams != 4
        || f->PackStreams[0] != 2
        || f->PackStreams[1] != 6
        || f->PackStreams[2] != 1
        || f->PackStreams[3] != 0
        || f->NumBonds != 3
        || f->Bonds[0].InIndex != 5 || f->Bonds[0].OutIndex != 0
        || f->Bonds[1].InIndex != 4 || f->Bonds[1].OutIndex != 1
        || f->Bonds[2].InIndex != 3 || f->Bonds[2].OutIndex != 2)
      return SZ_ERROR_UNSUPPORTED;
    return SZ_OK;
  }
  
  return SZ_ERROR_UNSUPPORTED;
}






static SRes SzFolder_Decode2_Inner(const CSzFolder *folder,
    const Byte *propsData,
    const UInt64 *unpackSizes,
    const UInt64 *packPositions,
    ILookInStreamPtr inStream, UInt64 startPos,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain,
    Byte *tempBuf[])
{
  UInt32 ci;
  SizeT tempSizes[3] = { 0, 0, 0};
  SizeT tempSize3 = 0;
  Byte *tempBuf3 = 0;

  RINOK(CheckSupportedFolder(folder))

  for (ci = 0; ci < folder->NumCoders; ci++)
  {
    const CSzCoderInfo *coder = &folder->Coders[ci];

    if (IS_MAIN_METHOD((UInt32)coder->MethodID))
    {
      UInt32 si = 0;
      UInt64 offset;
      UInt64 inSize;
      Byte *outBufCur = outBuffer;
      SizeT outSizeCur = outSize;
      if (folder->NumCoders == 4)
      {
        const UInt32 indices[] = { 3, 2, 0 };
        const UInt64 unpackSize = unpackSizes[ci];
        si = indices[ci];
        if (ci < 2)
        {
          Byte *temp;
          outSizeCur = (SizeT)unpackSize;
          if (outSizeCur != unpackSize)
            return SZ_ERROR_MEM;
          temp = (Byte *)ISzAlloc_Alloc(allocMain, outSizeCur);
          if (!temp && outSizeCur != 0)
            return SZ_ERROR_MEM;
          outBufCur = tempBuf[1 - ci] = temp;
          tempSizes[1 - ci] = outSizeCur;
        }
        else if (ci == 2)
        {
          if (unpackSize > outSize) /* check it */
            return SZ_ERROR_PARAM;
          tempBuf3 = outBufCur = outBuffer + (outSize - (size_t)unpackSize);
          tempSize3 = outSizeCur = (SizeT)unpackSize;
        }
        else
          return SZ_ERROR_UNSUPPORTED;
      }
      offset = packPositions[si];
      inSize = packPositions[(size_t)si + 1] - offset;
      RINOK(LookInStream_SeekTo(inStream, startPos + offset))

      if (coder->MethodID == k_Copy)
      {
        if (inSize != outSizeCur) /* check it */
          return SZ_ERROR_DATA;
        RINOK(SzDecodeCopy(inSize, inStream, outBufCur))
      }
      else if (coder->MethodID == k_LZMA)
      {
        RINOK(SzDecodeLzma(propsData + coder->PropsOffset, coder->PropsSize, inSize, inStream, outBufCur, outSizeCur, allocMain))
      }
    #ifndef Z7_NO_METHOD_LZMA2
      else if (coder->MethodID == k_LZMA2)
      {
        RINOK(SzDecodeLzma2(propsData + coder->PropsOffset, coder->PropsSize, inSize, inStream, outBufCur, outSizeCur, allocMain))
      }
    #endif
    #ifdef Z7_PPMD_SUPPORT
      else if (coder->MethodID == k_PPMD)
      {
        RINOK(SzDecodePpmd(propsData + coder->PropsOffset, coder->PropsSize, inSize, inStream, outBufCur, outSizeCur, allocMain))
      }
    #endif
      else
        return SZ_ERROR_UNSUPPORTED;
    }
    else if (coder->MethodID == k_BCJ2)
    {
      const UInt64 offset = packPositions[1];
      const UInt64 s3Size = packPositions[2] - offset;
      
      if (ci != 3)
        return SZ_ERROR_UNSUPPORTED;
      
      tempSizes[2] = (SizeT)s3Size;
      if (tempSizes[2] != s3Size)
        return SZ_ERROR_MEM;
      tempBuf[2] = (Byte *)ISzAlloc_Alloc(allocMain, tempSizes[2]);
      if (!tempBuf[2] && tempSizes[2] != 0)
        return SZ_ERROR_MEM;
      
      RINOK(LookInStream_SeekTo(inStream, startPos + offset))
      RINOK(SzDecodeCopy(s3Size, inStream, tempBuf[2]))

      if ((tempSizes[0] & 3) != 0 ||
          (tempSizes[1] & 3) != 0 ||
          tempSize3 + tempSizes[0] + tempSizes[1] != outSize)
        return SZ_ERROR_DATA;

      {
        CBcj2Dec p;
        
        p.bufs[0] = tempBuf3;   p.lims[0] = tempBuf3 + tempSize3;
        p.bufs[1] = tempBuf[0]; p.lims[1] = tempBuf[0] + tempSizes[0];
        p.bufs[2] = tempBuf[1]; p.lims[2] = tempBuf[1] + tempSizes[1];
        p.bufs[3] = tempBuf[2]; p.lims[3] = tempBuf[2] + tempSizes[2];
        
        p.dest = outBuffer;
        p.destLim = outBuffer + outSize;
        
        Bcj2Dec_Init(&p);
        RINOK(Bcj2Dec_Decode(&p))

        {
          unsigned i;
          for (i = 0; i < 4; i++)
            if (p.bufs[i] != p.lims[i])
              return SZ_ERROR_DATA;
          if (p.dest != p.destLim || !Bcj2Dec_IsMaybeFinished(&p))
            return SZ_ERROR_DATA;
        }
      }
    }
#if defined(Z7_USE_BRANCH_FILTER)
    else if (ci == 1)
    {
#if !defined(Z7_NO_METHODS_FILTERS)
      if (coder->MethodID == k_Delta)
      {
        if (coder->PropsSize != 1)
          return SZ_ERROR_UNSUPPORTED;
        {
          Byte state[DELTA_STATE_SIZE];
          Delta_Init(state);
          Delta_Decode(state, (unsigned)(propsData[coder->PropsOffset]) + 1, outBuffer, outSize);
        }
        continue;
      }
#endif
     
#ifdef Z7_USE_FILTER_ARM64
      if (coder->MethodID == k_ARM64)
      {
        UInt32 pc = 0;
        if (coder->PropsSize == 4)
        {
          pc = GetUi32(propsData + coder->PropsOffset);
          if (pc & 3)
            return SZ_ERROR_UNSUPPORTED;
        }
        else if (coder->PropsSize != 0)
          return SZ_ERROR_UNSUPPORTED;
        z7_BranchConv_ARM64_Dec(outBuffer, outSize, pc);
        continue;
      }
#endif

#if !defined(Z7_NO_METHODS_FILTERS)
      if (coder->MethodID == k_RISCV)
      {
        UInt32 pc = 0;
        if (coder->PropsSize == 4)
        {
          pc = GetUi32(propsData + coder->PropsOffset);
          if (pc & 1)
            return SZ_ERROR_UNSUPPORTED;
        }
        else if (coder->PropsSize != 0)
          return SZ_ERROR_UNSUPPORTED;
        z7_BranchConv_RISCV_Dec(outBuffer, outSize, pc);
        continue;
      }
#endif
      
#if !defined(Z7_NO_METHODS_FILTERS) || defined(Z7_USE_FILTER_ARMT)
      {
        if (coder->PropsSize != 0)
          return SZ_ERROR_UNSUPPORTED;
       #define CASE_BRA_CONV(isa) case k_ ## isa: Z7_BRANCH_CONV_DEC(isa)(outBuffer, outSize, 0); break; // pc = 0;
        switch (coder->MethodID)
        {
         #if !defined(Z7_NO_METHODS_FILTERS)
          case k_BCJ:
          {
            UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
            z7_BranchConvSt_X86_Dec(outBuffer, outSize, 0, &state); // pc = 0
            break;
          }
          case k_PPC: Z7_BRANCH_CONV_DEC_2(BranchConv_PPC)(outBuffer, outSize, 0); break; // pc = 0;
          // CASE_BRA_CONV(PPC)
          CASE_BRA_CONV(IA64)
          CASE_BRA_CONV(SPARC)
          CASE_BRA_CONV(ARM)
         #endif
         #if !defined(Z7_NO_METHODS_FILTERS) || defined(Z7_USE_FILTER_ARMT)
          CASE_BRA_CONV(ARMT)
         #endif
          default:
            return SZ_ERROR_UNSUPPORTED;
        }
        continue;
      }
#endif
    } // (c == 1)
#endif // Z7_USE_BRANCH_FILTER
    else
      return SZ_ERROR_UNSUPPORTED;
  }

  return SZ_OK;
}


/* Build a reduced copy of `folder` with the 7zAES coder removed.
 *
 * For 7zAES the on-disk folder places AES as coder[0]: it consumes the single
 * packed stream and its output (OutIndex 0) feeds the coder below it via the
 * bond {InIndex=1, OutIndex=0}. Once the wrapper has decrypted the packed
 * stream into memory, the AES coder is fully handled and must not appear in
 * the folder handed to SzFolder_Decode2_Inner (which would otherwise reject it
 * in CheckSupportedFolder). Removing coder `rm` drops the bond whose OutIndex
 * is `rm` (it routed the removed coder's output) and shifts the remaining
 * coder numbers down; the survivors then read the decrypted stream directly
 * as pack stream 0. For the canonical [AES, main] shape this leaves a
 * single-coder folder. */
static void SzFolder_StripCoder(const CSzFolder *src, UInt32 rm, CSzFolder *dst)
{
  UInt32 i;
  UInt32 j;
  *dst = *src;
  for (i = rm; i + 1 < src->NumCoders; i++)
    dst->Coders[i] = src->Coders[i + 1];
  dst->NumCoders = src->NumCoders - 1;

  j = 0;
  for (i = 0; i < src->NumBonds; i++)
  {
    CSzBond b = src->Bonds[i];
    if (b.OutIndex == rm)
      continue;                       /* this bond fed the removed coder */
    if (b.OutIndex > rm)
      b.OutIndex--;                   /* coder numbering shifted down */
    dst->Bonds[j++] = b;
  }
  dst->NumBonds = j;
  dst->NumPackStreams = 1;
  dst->PackStreams[0] = 0;
  dst->UnpackStream = 0;
}


/* AES-aware folder decode.
 *
 * When `folder` carries a 7zAES coder (always the outermost coder, consuming
 * the packed stream), the packed bytes are read, decrypted into a temporary
 * buffer, and the inner decoder is run against a memory look-stream over the
 * decrypted bytes. Otherwise this is a straight pass-through to the inner
 * decoder. `password` may be NULL when no AES coder is present. */
static SRes SzFolder_Decode2(const CSzFolder *folder,
    const Byte *propsData,
    const UInt64 *unpackSizes,
    const UInt64 *packPositions,
    ILookInStreamPtr inStream, UInt64 startPos,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain,
    Byte *tempBuf[],
    const char *password)
{
  UInt32 ci;
  UInt32 aesCi = (UInt32)-1;
  CZ7MemLook decryptedStream;
  Byte *decryptedBuf = NULL;
  size_t decryptedSize = 0;
  SRes res;

  for (ci = 0; ci < folder->NumCoders; ci++)
    if (z7_aes_is_aes_method((UInt32)folder->Coders[ci].MethodID))
    {
      aesCi = ci;
      break;
    }

  if (aesCi == (UInt32)-1)
    return SzFolder_Decode2_Inner(folder, propsData, unpackSizes, packPositions,
        inStream, startPos, outBuffer, outSize, allocMain, tempBuf);

  {
    const CSzCoderInfo *aesCoder = &folder->Coders[aesCi];
    UInt64 offset, inSize;

    /* AES must be the pack-stream consumer (coder 0), which is how 7-Zip
     * writes an encrypted folder, and not the only coder. */
    if (aesCi != 0 || folder->NumCoders < 2)
      return SZ_ERROR_UNSUPPORTED;
    if (folder->NumPackStreams != 1)
      return SZ_ERROR_UNSUPPORTED;

    offset = packPositions[0];
    inSize = packPositions[1] - offset;
    if ((inSize & 15) != 0)
      return SZ_ERROR_DATA; /* AES-CBC requires 16-byte alignment */

    if (!password || !password[0])
      return SZ_ERROR_UNSUPPORTED; /* no password supplied for an encrypted folder */

    if (inSize != 0)
    {
      decryptedBuf = (Byte *)ISzAlloc_Alloc(allocMain, (SizeT)inSize);
      if (!decryptedBuf)
        return SZ_ERROR_MEM;
      decryptedSize = (size_t)inSize;
      res = LookInStream_SeekTo(inStream, startPos + offset);
      if (res == SZ_OK)
        res = LookInStream_Read(inStream, decryptedBuf, decryptedSize);
      if (res == SZ_OK &&
          z7_aes_decode(propsData + aesCoder->PropsOffset, aesCoder->PropsSize,
                        password, decryptedBuf, decryptedSize) != Z7_AES_OK)
        res = SZ_ERROR_CRC; /* wrong password or corrupt data */
      if (res != SZ_OK)
      {
        ISzAlloc_Free(allocMain, decryptedBuf);
        return res;
      }
    }
  }

  {
    /* Run the inner decoder over the decrypted bytes with a folder that no
     * longer contains the AES coder. The surviving coders now read the
     * decrypted memory stream as pack stream 0, and their unpack sizes are
     * the original entries shifted down past the removed coder. The AES
     * coder's declared output size is the unpadded plaintext length: the
     * decrypted buffer is longer (CBC pads to the block size) but only that
     * many leading bytes are the real payload, so that becomes the pack
     * span handed to the inner decoder. The memory look-stream has its own
     * origin, so the inner decode starts at position 0. */
    CSzFolder reduced;
    UInt64 reducedPackPositions[2];
    reducedPackPositions[0] = 0;
    reducedPackPositions[1] = unpackSizes[aesCi];
    SzFolder_StripCoder(folder, aesCi, &reduced);
    Z7MemLook_CreateVTable(&decryptedStream, decryptedBuf, decryptedSize);
    res = SzFolder_Decode2_Inner(&reduced, propsData, unpackSizes + aesCi,
        reducedPackPositions, &decryptedStream.vt, 0,
        outBuffer, outSize, allocMain, tempBuf);
  }

  if (decryptedBuf)
    ISzAlloc_Free(allocMain, decryptedBuf);
  return res;
}


SRes SzAr_DecodeFolder2(const CSzAr *p, UInt32 folderIndex,
    ILookInStreamPtr inStream, UInt64 startPos,
    Byte *outBuffer, size_t outSize,
    ISzAllocPtr allocMain, const char *password)
{
  SRes res;
  CSzFolder folder;
  CSzData sd;
  
  const Byte *data = p->CodersData + p->FoCodersOffsets[folderIndex];
  sd.Data = data;
  sd.Size = p->FoCodersOffsets[(size_t)folderIndex + 1] - p->FoCodersOffsets[folderIndex];
  
  res = SzGetNextFolderItem(&folder, &sd);

  if (res != SZ_OK)
    return res;

  if (sd.Size != 0
      || folder.UnpackStream != p->FoToMainUnpackSizeIndex[folderIndex]
      || outSize != SzAr_GetFolderUnpackSize(p, folderIndex))
    return SZ_ERROR_FAIL;
  {
    unsigned i;
    Byte *tempBuf[3] = { 0, 0, 0};

    res = SzFolder_Decode2(&folder, data,
        &p->CoderUnpackSizes[p->FoToCoderUnpackSizes[folderIndex]],
        p->PackPositions + p->FoStartPackStreamIndex[folderIndex],
        inStream, startPos,
        outBuffer, (SizeT)outSize, allocMain, tempBuf, password);
    
    for (i = 0; i < 3; i++)
      ISzAlloc_Free(allocMain, tempBuf[i]);

    if (res == SZ_OK)
      if (SzBitWithVals_Check(&p->FolderCRCs, folderIndex))
        if (CrcCalc(outBuffer, outSize) != p->FolderCRCs.Vals[folderIndex])
          res = SZ_ERROR_CRC;

    return res;
  }
}

SRes SzAr_DecodeFolder(const CSzAr *p, UInt32 folderIndex,
    ILookInStreamPtr inStream, UInt64 startPos,
    Byte *outBuffer, size_t outSize,
    ISzAllocPtr allocMain)
{
  return SzAr_DecodeFolder2(p, folderIndex, inStream, startPos,
      outBuffer, outSize, allocMain, NULL);
}
