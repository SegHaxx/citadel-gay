// Copyright (c) 1987-2023 by the citadel.org team
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#define _GNU_SOURCE
#include "sysdep.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/types.h>
#define SHOW_ME_VAPPEND_PRINTF
#include <stdarg.h>

#include "libcitadel.h"

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif

#ifdef UNDEF_MEMCPY
#undef memcpy
#endif

#ifdef HAVE_ZLIB
#include <zlib.h>
int ZEXPORT compress_gzip(Bytef * dest, size_t * destLen, const Bytef * source, uLong sourceLen, int level);
#endif
int BaseStrBufSize = 64;
int EnableSplice = 0;
int ZLibCompressionRatio = -1; /* defaults to 6 */
#ifdef HAVE_ZLIB
#define DEF_MEM_LEVEL 8 /*< memlevel??? */
#define OS_CODE 0x03	/*< unix */
const int gz_magic[2] = { 0x1f, 0x8b };	/* gzip magic header */
#endif

const char *StrBufNOTNULL = ((char*) NULL) - 1;

const char HexList[256][3] = {
	"00","01","02","03","04","05","06","07","08","09","0A","0B","0C","0D","0E","0F",
	"10","11","12","13","14","15","16","17","18","19","1A","1B","1C","1D","1E","1F",
	"20","21","22","23","24","25","26","27","28","29","2A","2B","2C","2D","2E","2F",
	"30","31","32","33","34","35","36","37","38","39","3A","3B","3C","3D","3E","3F",
	"40","41","42","43","44","45","46","47","48","49","4A","4B","4C","4D","4E","4F",
	"50","51","52","53","54","55","56","57","58","59","5A","5B","5C","5D","5E","5F",
	"60","61","62","63","64","65","66","67","68","69","6A","6B","6C","6D","6E","6F",
	"70","71","72","73","74","75","76","77","78","79","7A","7B","7C","7D","7E","7F",
	"80","81","82","83","84","85","86","87","88","89","8A","8B","8C","8D","8E","8F",
	"90","91","92","93","94","95","96","97","98","99","9A","9B","9C","9D","9E","9F",
	"A0","A1","A2","A3","A4","A5","A6","A7","A8","A9","AA","AB","AC","AD","AE","AF",
	"B0","B1","B2","B3","B4","B5","B6","B7","B8","B9","BA","BB","BC","BD","BE","BF",
	"C0","C1","C2","C3","C4","C5","C6","C7","C8","C9","CA","CB","CC","CD","CE","CF",
	"D0","D1","D2","D3","D4","D5","D6","D7","D8","D9","DA","DB","DC","DD","DE","DF",
	"E0","E1","E2","E3","E4","E5","E6","E7","E8","E9","EA","EB","EC","ED","EE","EF",
	"F0","F1","F2","F3","F4","F5","F6","F7","F8","F9","FA","FB","FC","FD","FE","FF"};


// Private Structure for the Stringbuffer
struct StrBuf {
	char *buf;		// the pointer to the dynamic buffer
	long BufSize;		// how many spcae do we optain
	long BufUsed;		// Number of Chars used excluding the trailing \\0
	int ConstBuf;		// are we just a wrapper arround a static buffer and musn't we be changed?
#ifdef SIZE_DEBUG
	long nIncreases;	// for profiling; cound how many times we needed more
	char bt [SIZ];		// Stacktrace of last increase
	char bt_lastinc[SIZ];	// How much did we increase last time?
#endif
};


static inline int Ctdl_GetUtf8SequenceLength(const char *CharS, const char *CharE);
static inline int Ctdl_IsUtf8SequenceStart(const char Char);

#ifdef SIZE_DEBUG
#ifdef HAVE_BACKTRACE
static void StrBufBacktrace(StrBuf *Buf, int which) {
	int n;
	char *pstart, *pch;
	void *stack_frames[50];
	size_t size, i;
	char **strings;

	if (which) {
		pstart = pch = Buf->bt;
	}
	else {
		pstart = pch = Buf->bt_lastinc;
	}
	size = backtrace(stack_frames, sizeof(stack_frames) / sizeof(void*));
	strings = backtrace_symbols(stack_frames, size);
	for (i = 0; i < size; i++) {
		if (strings != NULL) {
			n = snprintf(pch, SIZ - (pch - pstart), "%s\\n", strings[i]);
		}
		else {
			n = snprintf(pch, SIZ - (pch - pstart), "%p\\n", stack_frames[i]);
		}
		pch += n;
	}
	free(strings);
}
#endif


void dbg_FreeStrBuf(StrBuf *FreeMe, char *FromWhere) {
	if (hFreeDbglog == -1) {
		pid_t pid = getpid();
		char path [SIZ];
		snprintf(path, SIZ, "/tmp/libcitadel_strbuf_realloc.log.%d", pid);
		hFreeDbglog = open(path, O_APPEND|O_CREAT|O_WRONLY);
	}
	if ((*FreeMe)->nIncreases > 0) {
		char buf[SIZ * 3];
		long n;
		n = snprintf(buf, SIZ * 3, "%c+|%ld|%ld|%ld|%s|%s|\n",
			     FromWhere,
			     (*FreeMe)->nIncreases,
			     (*FreeMe)->BufUsed,
			     (*FreeMe)->BufSize,
			     (*FreeMe)->bt,
			     (*FreeMe)->bt_lastinc);
		n = write(hFreeDbglog, buf, n);
	}
	else {
		char buf[128];
		long n;
		n = snprintf(buf, 128, "%c_|0|%ld%ld|\n",
			     FromWhere,
			     (*FreeMe)->BufUsed,
			     (*FreeMe)->BufSize);
		n = write(hFreeDbglog, buf, n);
	}
}


void dbg_IncreaseBuf(StrBuf *IncMe) {
	Buf->nIncreases++;
#ifdef HAVE_BACKTRACE
	StrBufBacktrace(Buf, 1);
#endif
}


void dbg_Init(StrBuf *Buf) {
	Buf->nIncreases = 0;
	Buf->bt[0] = '\0';
	Buf->bt_lastinc[0] = '\0';
#ifdef HAVE_BACKTRACE
	StrBufBacktrace(Buf, 0);
#endif
}

#else
/* void it... */
#define dbg_FreeStrBuf(a, b)
#define dbg_IncreaseBuf(a)
#define dbg_Init(a)

#endif

/*
 *  swaps the contents of two StrBufs
 * this is to be used to have cheap switched between a work-buffer and a target buffer 
 *  A First one
 *  B second one
 */
static inline void iSwapBuffers(StrBuf *A, StrBuf *B) {
	StrBuf C;

	memcpy(&C, A, sizeof(*A));
	memcpy(A, B, sizeof(*B));
	memcpy(B, &C, sizeof(C));

}


void SwapBuffers(StrBuf *A, StrBuf *B) {
	iSwapBuffers(A, B);
}


/* 
 *  Cast operator to Plain String 
 * @note if the buffer is altered by StrBuf operations, this pointer may become 
 *  invalid. So don't lean on it after altering the buffer!
 *  Since this operation is considered cheap, rather call it often than risking
 *  your pointer to become invalid!
 *  Str the string we want to get the c-string representation for
 * @returns the Pointer to the Content. Don't mess with it!
 */
inline const char *ChrPtr(const StrBuf *Str) {
	if (Str == NULL)
		return "";
	return Str->buf;
}


/*
 *  since we know strlen()'s result, provide it here.
 *  Str the string to return the length to
 * @returns contentlength of the buffer
 */
inline int StrLength(const StrBuf *Str) {
	return (Str != NULL) ? Str->BufUsed : 0;
}


// local utility function to resize the buffer
// Buf		the buffer whichs storage we should increase
// KeepOriginal	should we copy the original buffer or just start over with a new one
// DestSize	what should fit in after?
static int IncreaseBuf(StrBuf *Buf, int KeepOriginal, int DestSize) {
	char *NewBuf;
	size_t NewSize = Buf->BufSize * 2;

	if (Buf->ConstBuf) {
		return -1;
	}
		
	if (DestSize > 0) {
		while ((NewSize <= DestSize) && (NewSize != 0)) {
			NewSize *= 2;
		}
	}

	if (NewSize == 0) {
		return -1;
	}

	NewBuf = (char*) malloc(NewSize);
	if (NewBuf == NULL) {
		return -1;
	}

	if (KeepOriginal && (Buf->BufUsed > 0)) {
		memcpy(NewBuf, Buf->buf, Buf->BufUsed);
	}
	else {
		NewBuf[0] = '\0';
		Buf->BufUsed = 0;
	}
	free (Buf->buf);
	Buf->buf = NewBuf;
	Buf->BufSize = NewSize;

	dbg_IncreaseBuf(Buf);

	return Buf->BufSize;
}


// shrink / increase an _EMPTY_ buffer to NewSize. Buffercontent is thoroughly ignored and flushed.
// Buf		Buffer to shrink (has to be empty)
// ThreshHold	if the buffer is bigger then this, its readjusted
// NewSize	if we Shrink it, how big are we going to be afterwards?
void ReAdjustEmptyBuf(StrBuf *Buf, long ThreshHold, long NewSize) {
	if ((Buf != NULL) && (Buf->BufUsed == 0) && (Buf->BufSize < ThreshHold)) {
		free(Buf->buf);
		Buf->buf = (char*) malloc(NewSize);
		Buf->BufUsed = 0;
		Buf->BufSize = NewSize;
	}
}


/*
 *  shrink long term buffers to their real size so they don't waste memory
 *  Buf buffer to shrink
 *  Force if not set, will just executed if the buffer is much to big; set for lifetime strings
 * @returns physical size of the buffer
 */
long StrBufShrinkToFit(StrBuf *Buf, int Force) {
	if (Buf == NULL)
		return -1;
	if (Force || (Buf->BufUsed + (Buf->BufUsed / 3) > Buf->BufSize)) {
		char *TmpBuf;

		TmpBuf = (char*) malloc(Buf->BufUsed + 1);
		if (TmpBuf == NULL)
			return -1;

		memcpy (TmpBuf, Buf->buf, Buf->BufUsed + 1);
		Buf->BufSize = Buf->BufUsed + 1;
		free(Buf->buf);
		Buf->buf = TmpBuf;
	}
	return Buf->BufUsed;
}


/*
 *  Allocate a new buffer with default buffer size
 * @returns the new stringbuffer
 */
StrBuf *NewStrBuf(void) {
	StrBuf *NewBuf;

	NewBuf = (StrBuf*) malloc(sizeof(StrBuf));
	if (NewBuf == NULL)
		return NULL;

	NewBuf->buf = (char*) malloc(BaseStrBufSize);
	if (NewBuf->buf == NULL) {
		free(NewBuf);
		return NULL;
	}
	NewBuf->buf[0] = '\0';
	NewBuf->BufSize = BaseStrBufSize;
	NewBuf->BufUsed = 0;
	NewBuf->ConstBuf = 0;

	dbg_Init (NewBuf);
	return NewBuf;
}


/* 
 *  Copy Constructor; returns a duplicate of CopyMe
 *  CopyMe Buffer to faxmilate
 * @returns the new stringbuffer
 */
StrBuf *NewStrBufDup(const StrBuf *CopyMe) {
	StrBuf *NewBuf;
	
	if (CopyMe == NULL)
		return NewStrBuf();

	NewBuf = (StrBuf*) malloc(sizeof(StrBuf));
	if (NewBuf == NULL)
		return NULL;

	NewBuf->buf = (char*) malloc(CopyMe->BufSize);
	if (NewBuf->buf == NULL) {
		free(NewBuf);
		return NULL;
	}

	memcpy(NewBuf->buf, CopyMe->buf, CopyMe->BufUsed + 1);
	NewBuf->BufUsed = CopyMe->BufUsed;
	NewBuf->BufSize = CopyMe->BufSize;
	NewBuf->ConstBuf = 0;

	dbg_Init(NewBuf);

	return NewBuf;
}


/* 
 *  Copy Constructor; CreateRelpaceMe will contain CopyFlushMe afterwards.
 *  NoMe if non-NULL, we will use that buffer as value; KeepOriginal will abused as len.
 *  CopyFlushMe Buffer to faxmilate if KeepOriginal, or to move into CreateRelpaceMe if !KeepOriginal.
 *  CreateRelpaceMe If NULL, will be created, else Flushed and filled CopyFlushMe 
 *  KeepOriginal should CopyFlushMe remain intact? or may we Steal its buffer?
 * @returns the new stringbuffer
 */
void NewStrBufDupAppendFlush(StrBuf **CreateRelpaceMe, StrBuf *CopyFlushMe, const char *NoMe, int KeepOriginal) {
	StrBuf *NewBuf;
	
	if (CreateRelpaceMe == NULL)
		return;

	if (NoMe != NULL) {
		if (*CreateRelpaceMe != NULL)
			StrBufPlain(*CreateRelpaceMe, NoMe, KeepOriginal);
		else 
			*CreateRelpaceMe = NewStrBufPlain(NoMe, KeepOriginal);
		return;
	}

	if (CopyFlushMe == NULL) {
		if (*CreateRelpaceMe != NULL)
			FlushStrBuf(*CreateRelpaceMe);
		else 
			*CreateRelpaceMe = NewStrBuf();
		return;
	}

	/* 
	 * Randomly Chosen: bigger than 64 chars is cheaper to swap the buffers instead of copying.
	 * else *CreateRelpaceMe may use more memory than needed in a longer term, CopyFlushMe might
	 * be a big IO-Buffer...
	 */
	if (KeepOriginal || (StrLength(CopyFlushMe) < 256)) {
		if (*CreateRelpaceMe == NULL) {
			*CreateRelpaceMe = NewBuf = NewStrBufPlain(NULL, CopyFlushMe->BufUsed);
			dbg_Init(NewBuf);
		}
		else {
			NewBuf = *CreateRelpaceMe;
			FlushStrBuf(NewBuf);
		}
		StrBufAppendBuf(NewBuf, CopyFlushMe, 0);
	}
	else {
		if (*CreateRelpaceMe == NULL) {
			*CreateRelpaceMe = NewBuf = NewStrBufPlain(NULL, CopyFlushMe->BufUsed);
			dbg_Init(NewBuf);
		}
		else  {
			NewBuf = *CreateRelpaceMe;
		}
		iSwapBuffers (NewBuf, CopyFlushMe);
	}
	if (!KeepOriginal) {
		FlushStrBuf(CopyFlushMe);
	}
	return;
}


/*
 *  create a new Buffer using an existing c-string
 * this function should also be used if you want to pre-suggest
 * the buffer size to allocate in conjunction with ptr == NULL
 *  ptr the c-string to copy; may be NULL to create a blank instance
 *  nChars How many chars should we copy; -1 if we should measure the length ourselves
 * @returns the new stringbuffer
 */
StrBuf *NewStrBufPlain(const char* ptr, int nChars) {
	StrBuf *NewBuf;
	size_t Siz = BaseStrBufSize;
	size_t CopySize;

	NewBuf = (StrBuf*) malloc(sizeof(StrBuf));
	if (NewBuf == NULL)
		return NULL;

	if (nChars < 0)
		CopySize = strlen((ptr != NULL)?ptr:"");
	else
		CopySize = nChars;

	while ((Siz <= CopySize) && (Siz != 0))
		Siz *= 2;

	if (Siz == 0) {
		free(NewBuf);
		return NULL;
	}

	NewBuf->buf = (char*) malloc(Siz);
	if (NewBuf->buf == NULL) {
		free(NewBuf);
		return NULL;
	}
	NewBuf->BufSize = Siz;
	if (ptr != NULL) {
		memcpy(NewBuf->buf, ptr, CopySize);
		NewBuf->buf[CopySize] = '\0';
		NewBuf->BufUsed = CopySize;
	}
	else {
		NewBuf->buf[0] = '\0';
		NewBuf->BufUsed = 0;
	}
	NewBuf->ConstBuf = 0;

	dbg_Init(NewBuf);

	return NewBuf;
}


/*
 *  Set an existing buffer from a c-string
 *  Buf buffer to load
 *  ptr c-string to put into 
 *  nChars set to -1 if we should work 0-terminated
 * @returns the new length of the string
 */
int StrBufPlain(StrBuf *Buf, const char* ptr, int nChars) {
	size_t Siz;
	size_t CopySize;

	if (Buf == NULL)
		return -1;
	if (ptr == NULL) {
		FlushStrBuf(Buf);
		return -1;
	}

	Siz = Buf->BufSize;

	if (nChars < 0)
		CopySize = strlen(ptr);
	else
		CopySize = nChars;

	while ((Siz <= CopySize) && (Siz != 0))
		Siz *= 2;

	if (Siz == 0) {
		FlushStrBuf(Buf);
		return -1;
	}

	if (Siz != Buf->BufSize)
		IncreaseBuf(Buf, 0, Siz);
	memcpy(Buf->buf, ptr, CopySize);
	Buf->buf[CopySize] = '\0';
	Buf->BufUsed = CopySize;
	Buf->ConstBuf = 0;
	return CopySize;
}


/*
 *  use strbuf as wrapper for a string constant for easy handling
 *  StringConstant a string to wrap
 *  SizeOfStrConstant should be sizeof(StringConstant)-1
 */
StrBuf* _NewConstStrBuf(const char* StringConstant, size_t SizeOfStrConstant)
{
	StrBuf *NewBuf;

	NewBuf = (StrBuf*) malloc(sizeof(StrBuf));
	if (NewBuf == NULL)
		return NULL;
	NewBuf->buf = (char*) StringConstant;
	NewBuf->BufSize = SizeOfStrConstant;
	NewBuf->BufUsed = SizeOfStrConstant;
	NewBuf->ConstBuf = 1;

	dbg_Init(NewBuf);

	return NewBuf;
}


/*
 *  flush the content of a Buf; keep its struct
 *  buf Buffer to flush
 */
int FlushStrBuf(StrBuf *buf)
{
	if ((buf == NULL) || (buf->buf == NULL))
		return -1;
	if (buf->ConstBuf)
		return -1;       
	buf->buf[0] ='\0';
	buf->BufUsed = 0;
	return 0;
}

/*
 *  wipe the content of a Buf thoroughly (overwrite it -> expensive); keep its struct
 *  buf Buffer to wipe
 */
int FLUSHStrBuf(StrBuf *buf)
{
	if (buf == NULL)
		return -1;
	if (buf->ConstBuf)
		return -1;
	if (buf->BufUsed > 0) {
		memset(buf->buf, 0, buf->BufUsed);
		buf->BufUsed = 0;
	}
	return 0;
}

#ifdef SIZE_DEBUG
int hFreeDbglog = -1;
#endif
/*
 *  Release a Buffer
 * Its a double pointer, so it can NULL your pointer
 * so fancy SIG11 appear instead of random results
 *  FreeMe Pointer Pointer to the buffer to free
 */
void FreeStrBuf (StrBuf **FreeMe)
{
	if (*FreeMe == NULL)
		return;

	dbg_FreeStrBuf(FreeMe, 'F');

	if (!(*FreeMe)->ConstBuf) 
		free((*FreeMe)->buf);
	free(*FreeMe);
	*FreeMe = NULL;
}

/*
 *  flatten a Buffer to the Char * we return 
 * Its a double pointer, so it can NULL your pointer
 * so fancy SIG11 appear instead of random results
 * The Callee then owns the buffer and is responsible for freeing it.
 *  SmashMe Pointer Pointer to the buffer to release Buf from and free
 * @returns the pointer of the buffer; Callee owns the memory thereafter.
 */
char *SmashStrBuf (StrBuf **SmashMe) {
	char *Ret;

	if ((SmashMe == NULL) || (*SmashMe == NULL))
		return NULL;
	
	dbg_FreeStrBuf(SmashMe, 'S');

	Ret = (*SmashMe)->buf;
	free(*SmashMe);
	*SmashMe = NULL;
	return Ret;
}


/*
 *  Release the buffer
 * If you want put your StrBuf into a Hash, use this as Destructor.
 *  VFreeMe untyped pointer to a StrBuf. be shure to do the right thing [TM]
 */
void HFreeStrBuf (void *VFreeMe) {
	StrBuf *FreeMe = (StrBuf*)VFreeMe;
	if (FreeMe == NULL)
		return;

	dbg_FreeStrBuf(SmashMe, 'H');

	if (!FreeMe->ConstBuf) 
		free(FreeMe->buf);
	free(FreeMe);
}


/*******************************************************************************
 *                      Simple string transformations                          *
 *******************************************************************************/

//  Wrapper around atol
long StrTol(const StrBuf *Buf) {
	if (Buf == NULL)
		return 0;
	if(Buf->BufUsed > 0)
		return atol(Buf->buf);
	else
		return 0;
}


//  Wrapper around atoi
int StrToi(const StrBuf *Buf) {
	if (Buf == NULL)
		return 0;
	if (Buf->BufUsed > 0)
		return atoi(Buf->buf);
	else
		return 0;
}


// Checks to see if the string is a pure number 
// Buf The buffer to inspect
// returns 1 if its a pure number, 0, if not.
int StrBufIsNumber(const StrBuf *Buf) {
	char * pEnd;
	if ((Buf == NULL) || (Buf->BufUsed == 0)) {
		return 0;
	}
	strtoll(Buf->buf, &pEnd, 10);
	if (pEnd == Buf->buf)
		return 0;
	if ((pEnd != NULL) && (pEnd == Buf->buf + Buf->BufUsed))
		return 1;
	if (Buf->buf == pEnd)
		return 0;
	return 0;
} 


// modifies a Single char of the Buf
// You can point to it via char* or a zero-based integer
// Buf The buffer to manipulate
// ptr char* to zero; use NULL if unused
// nThChar zero based pointer into the string; use -1 if unused
// PeekValue The Character to place into the position
long StrBufPeek(StrBuf *Buf, const char* ptr, long nThChar, char PeekValue) {
	if (Buf == NULL)
		return -1;
	if (ptr != NULL)
		nThChar = ptr - Buf->buf;
	if ((nThChar < 0) || (nThChar > Buf->BufUsed))
		return -1;
	Buf->buf[nThChar] = PeekValue;
	return nThChar;
}


// modifies a range of chars of the Buf
// You can point to it via char* or a zero-based integer
// Buf The buffer to manipulate
// ptr char* to zero; use NULL if unused
// nThChar zero based pointer into the string; use -1 if unused
// nChars how many chars are to be flushed?
// PookValue The Character to place into that area
long StrBufPook(StrBuf *Buf, const char* ptr, long nThChar, long nChars, char PookValue) {
	if (Buf == NULL)
		return -1;
	if (ptr != NULL)
		nThChar = ptr - Buf->buf;
	if ((nThChar < 0) || (nThChar > Buf->BufUsed))
		return -1;
	if (nThChar + nChars > Buf->BufUsed)
		nChars =  Buf->BufUsed - nThChar;

	memset(Buf->buf + nThChar, PookValue, nChars);
	// just to be sure...
	Buf->buf[Buf->BufUsed] = 0;
	return nChars;
}


// Append a StringBuffer to the buffer
// Buf Buffer to modify
// AppendBuf Buffer to copy at the end of our buffer
// Offset Should we start copying from an offset?
void StrBufAppendBuf(StrBuf *Buf, const StrBuf *AppendBuf, unsigned long Offset) {
	if ((AppendBuf == NULL) || (AppendBuf->buf == NULL) ||
	    (Buf == NULL) || (Buf->buf == NULL))
		return;

	if (Buf->BufSize - Offset < AppendBuf->BufUsed + Buf->BufUsed + 1)
		IncreaseBuf(Buf, (Buf->BufUsed > 0), AppendBuf->BufUsed + Buf->BufUsed);

	memcpy(Buf->buf + Buf->BufUsed, AppendBuf->buf + Offset, AppendBuf->BufUsed - Offset);
	Buf->BufUsed += AppendBuf->BufUsed - Offset;
	Buf->buf[Buf->BufUsed] = '\0';
}


// Append a C-String to the buffer
// Buf		Buffer to modify
// AppendBuf	Buffer to copy at the end of our buffer
// AppendSize	number of bytes to copy; set to -1 if we should count it in advance
// Offset	Should we start copying from an offset?
void StrBufAppendBufPlain(StrBuf *Buf, const char *AppendBuf, long AppendSize, unsigned long Offset) {
	long aps;
	long BufSizeRequired;

	if ((AppendBuf == NULL) || (Buf == NULL))
		return;

	if (AppendSize < 0) {
		aps = strlen(AppendBuf + Offset);
	}
	else {
		aps = AppendSize - Offset;
	}

	BufSizeRequired = Buf->BufUsed + aps + 1;
	if (Buf->BufSize <= BufSizeRequired) {
		IncreaseBuf(Buf, (Buf->BufUsed > 0), BufSizeRequired);
	}

	memcpy(Buf->buf + Buf->BufUsed, 
	       AppendBuf + Offset, 
	       aps);
	Buf->BufUsed += aps;
	Buf->buf[Buf->BufUsed] = '\0';
}


//  sprintf like function appending the formated string to the buffer
// vsnprintf version to wrap into own calls
//  Buf Buffer to extend by format and Params
//  format printf alike format to add
//  ap va_list containing the items for format
void StrBufVAppendPrintf(StrBuf *Buf, const char *format, va_list ap) {
	va_list apl;
	size_t BufSize;
	size_t nWritten;
	size_t Offset;
	size_t newused;

	if ((Buf == NULL)  || (format == NULL))
		return;

	BufSize = Buf->BufSize;
	nWritten = Buf->BufSize + 1;
	Offset = Buf->BufUsed;
	newused = Offset + nWritten;
	
	while (newused >= BufSize) {
		va_copy(apl, ap);
		nWritten = vsnprintf(Buf->buf + Offset, Buf->BufSize - Offset, format, apl);
		va_end(apl);
		newused = Offset + nWritten;
		if (newused >= Buf->BufSize) {
			if (IncreaseBuf(Buf, 1, newused) == -1)
				return; // TODO: error handling?
			newused = Buf->BufSize + 1;
		}
		else {
			Buf->BufUsed = Offset + nWritten;
			BufSize = Buf->BufSize;
		}

	}
}


// sprintf like function appending the formated string to the buffer
// Buf Buffer to extend by format and Params
// format printf alike format to add
void StrBufAppendPrintf(StrBuf *Buf, const char *format, ...) {
	size_t BufSize;
	size_t nWritten;
	size_t Offset;
	size_t newused;
	va_list arg_ptr;
	
	if ((Buf == NULL)  || (format == NULL))
		return;

	BufSize = Buf->BufSize;
	nWritten = Buf->BufSize + 1;
	Offset = Buf->BufUsed;
	newused = Offset + nWritten;

	while (newused >= BufSize) {
		va_start(arg_ptr, format);
		nWritten = vsnprintf(Buf->buf + Buf->BufUsed, Buf->BufSize - Buf->BufUsed, format, arg_ptr);
		va_end(arg_ptr);
		newused = Buf->BufUsed + nWritten;
		if (newused >= Buf->BufSize) {
			if (IncreaseBuf(Buf, 1, newused) == -1)
				return; // TODO: error handling?
			newused = Buf->BufSize + 1;
		}
		else {
			Buf->BufUsed += nWritten;
			BufSize = Buf->BufSize;
		}

	}
}


//  sprintf like function putting the formated string into the buffer
//  Buf Buffer to extend by format and Parameters
//  format printf alike format to add
void StrBufPrintf(StrBuf *Buf, const char *format, ...) {
	size_t nWritten;
	va_list arg_ptr;
	
	if ((Buf == NULL)  || (format == NULL))
		return;

	nWritten = Buf->BufSize + 1;
	while (nWritten >= Buf->BufSize) {
		va_start(arg_ptr, format);
		nWritten = vsnprintf(Buf->buf, Buf->BufSize, format, arg_ptr);
		va_end(arg_ptr);
		if (nWritten >= Buf->BufSize) {
			if (IncreaseBuf(Buf, 0, 0) == -1)
				return; // TODO: error handling?
			nWritten = Buf->BufSize + 1;
			continue;
		}
		Buf->BufUsed = nWritten ;
	}
}


//  Callback for cURL to append the webserver reply to a buffer
//  ptr, size, nmemb, and stream are pre-defined by the cURL API; see man 3 curl for more info
size_t CurlFillStrBuf_callback(void *ptr, size_t size, size_t nmemb, void *stream) {

	StrBuf *Target;

	Target = stream;
	if (ptr == NULL)
		return 0;

	StrBufAppendBufPlain(Target, ptr, size * nmemb, 0);
	return size * nmemb;
}


// extracts a substring from Source into dest
// dest buffer to place substring into
// Source string to copy substring from
// Offset chars to skip from start
// nChars number of chars to copy
// returns the number of chars copied; may be different from nChars due to the size of Source
int StrBufSub(StrBuf *dest, const StrBuf *Source, unsigned long Offset, size_t nChars) {
	size_t NCharsRemain;
	if (Offset > Source->BufUsed) {
		if (dest != NULL)
			FlushStrBuf(dest);
		return 0;
	}
	if (Offset + nChars < Source->BufUsed) {
		if ((nChars >= dest->BufSize) && 
		    (IncreaseBuf(dest, 0, nChars + 1) == -1))
			return 0;
		memcpy(dest->buf, Source->buf + Offset, nChars);
		dest->BufUsed = nChars;
		dest->buf[dest->BufUsed] = '\0';
		return nChars;
	}
	NCharsRemain = Source->BufUsed - Offset;
	if ((NCharsRemain  >= dest->BufSize) && 
	    (IncreaseBuf(dest, 0, NCharsRemain + 1) == -1))
		return 0;
	memcpy(dest->buf, Source->buf + Offset, NCharsRemain);
	dest->BufUsed = NCharsRemain;
	dest->buf[dest->BufUsed] = '\0';
	return NCharsRemain;
}

/**
 *  Cut nChars from the start of the string
 *  Buf Buffer to modify
 *  nChars how many chars should be skipped?
 */
void StrBufCutLeft(StrBuf *Buf, int nChars)
{
	if ((Buf == NULL) || (Buf->BufUsed == 0)) return;
	if (nChars >= Buf->BufUsed) {
		FlushStrBuf(Buf);
		return;
	}
	memmove(Buf->buf, Buf->buf + nChars, Buf->BufUsed - nChars);
	Buf->BufUsed -= nChars;
	Buf->buf[Buf->BufUsed] = '\0';
}

/**
 *  Cut the trailing n Chars from the string
 *  Buf Buffer to modify
 *  nChars how many chars should be trunkated?
 */
void StrBufCutRight(StrBuf *Buf, int nChars)
{
	if ((Buf == NULL) || (Buf->BufUsed == 0) || (Buf->buf == NULL))
		return;

	if (nChars >= Buf->BufUsed) {
		FlushStrBuf(Buf);
		return;
	}
	Buf->BufUsed -= nChars;
	Buf->buf[Buf->BufUsed] = '\0';
}

/**
 *  Cut the string after n Chars
 *  Buf Buffer to modify
 *  AfternChars after how many chars should we trunkate the string?
 *  At if non-null and points inside of our string, cut it there.
 */
void StrBufCutAt(StrBuf *Buf, int AfternChars, const char *At)
{
	if ((Buf == NULL) || (Buf->BufUsed == 0)) return;
	if (At != NULL){
		AfternChars = At - Buf->buf;
	}

	if ((AfternChars < 0) || (AfternChars >= Buf->BufUsed))
		return;
	Buf->BufUsed = AfternChars;
	Buf->buf[Buf->BufUsed] = '\0';
}


/**
 *  Strip leading and trailing spaces from a string; with premeasured and adjusted length.
 *  Buf the string to modify
 */
void StrBufTrim(StrBuf *Buf)
{
	int delta = 0;
	if ((Buf == NULL) || (Buf->BufUsed == 0)) return;

	while ((Buf->BufUsed > 0) &&
	       isspace(Buf->buf[Buf->BufUsed - 1]))
	{
		Buf->BufUsed --;
	}
	Buf->buf[Buf->BufUsed] = '\0';

	if (Buf->BufUsed == 0) return;

	while ((Buf->BufUsed > delta) && (isspace(Buf->buf[delta]))){
		delta ++;
	}
	if (delta > 0) StrBufCutLeft(Buf, delta);
}
/**
 *  changes all spaces in the string  (tab, linefeed...) to Blank (0x20)
 *  Buf the string to modify
 */
void StrBufSpaceToBlank(StrBuf *Buf)
{
	char *pche, *pch;

	if ((Buf == NULL) || (Buf->BufUsed == 0)) return;

	pch = Buf->buf;
	pche = pch + Buf->BufUsed;
	while (pch < pche) 
	{
		if (isspace(*pch))
			*pch = ' ';
		pch ++;
	}
}

void StrBufStripAllBut(StrBuf *Buf, char leftboundary, char rightboundary)
{
	const char *pLeft;
	const char *pRight;

	if ((Buf == NULL) || (Buf->buf == NULL)) {
		return;
	}

	pRight = strchr(Buf->buf, rightboundary);
	if (pRight != NULL) {
		StrBufCutAt(Buf, 0, pRight);
	}

	pLeft = strrchr(ChrPtr(Buf), leftboundary);
	if (pLeft != NULL) {
		StrBufCutLeft(Buf, pLeft - Buf->buf + 1);
	}
}


/**
 *  uppercase the contents of a buffer
 *  Buf the buffer to translate
 */
void StrBufUpCase(StrBuf *Buf) 
{
	char *pch, *pche;

	if ((Buf == NULL) || (Buf->BufUsed == 0)) return;

	pch = Buf->buf;
	pche = pch + Buf->BufUsed;
	while (pch < pche) {
		*pch = toupper(*pch);
		pch ++;
	}
}


/**
 *  lowercase the contents of a buffer
 *  Buf the buffer to translate
 */
void StrBufLowerCase(StrBuf *Buf) 
{
	char *pch, *pche;

	if ((Buf == NULL) || (Buf->BufUsed == 0)) return;

	pch = Buf->buf;
	pche = pch + Buf->BufUsed;
	while (pch < pche) {
		*pch = tolower(*pch);
		pch ++;
	}
}


/*******************************************************************************
 *           a tokenizer that kills, maims, and destroys                       *
 *******************************************************************************/

/**
 *  Replace a token at a given place with a given length by another token with given length
 *  Buf String where to work on
 *  where where inside of the Buf is the search-token
 *  HowLong How long is the token to be replaced
 *  Repl Token to insert at 'where'
 *  ReplLen Length of repl
 * @returns -1 if fail else length of resulting Buf
 */
int StrBufReplaceToken(StrBuf *Buf, long where, long HowLong, 
		       const char *Repl, long ReplLen)
{

	if ((Buf == NULL) || 
	    (where > Buf->BufUsed) ||
	    (where + HowLong > Buf->BufUsed))
		return -1;

	if (where + ReplLen - HowLong > Buf->BufSize)
		if (IncreaseBuf(Buf, 1, Buf->BufUsed + ReplLen) < 0)
			return -1;

	memmove(Buf->buf + where + ReplLen, 
		Buf->buf + where + HowLong,
		Buf->BufUsed - where - HowLong);
						
	memcpy(Buf->buf + where, 
	       Repl, ReplLen);

	Buf->BufUsed += ReplLen - HowLong;

	return Buf->BufUsed;
}

/**
 *  Counts the numbmer of tokens in a buffer
 *  source String to count tokens in
 *  tok    Tokenizer char to count
 * @returns numbers of tokenizer chars found
 */
int StrBufNum_tokens(const StrBuf *source, char tok)
{
	char *pch, *pche;
	long NTokens;
	if ((source == NULL) || (source->BufUsed == 0))
		return 0;
	if ((source->BufUsed == 1) && (*source->buf == tok))
		return 2;
	NTokens = 1;
	pch = source->buf;
	pche = pch + source->BufUsed;
	while (pch < pche)
	{
		if (*pch == tok)
			NTokens ++;
		pch ++;
	}
	return NTokens;
}

/**
 *  a string tokenizer
 *  Source StringBuffer to read into
 *  parmnum n'th Parameter to remove
 *  separator tokenizer character
 * @returns -1 if not found, else length of token.
 */
int StrBufRemove_token(StrBuf *Source, int parmnum, char separator)
{
	int ReducedBy;
	char *d, *s, *end;		/* dest, source */
	int count = 0;

	/* Find desired eter */
	end = Source->buf + Source->BufUsed;
	d = Source->buf;
	while ((d <= end) && 
	       (count < parmnum))
	{
		/* End of string, bail! */
		if (!*d) {
			d = NULL;
			break;
		}
		if (*d == separator) {
			count++;
		}
		d++;
	}
	if ((d == NULL) || (d >= end))
		return 0;		/* @Parameter not found */

	/* Find next eter */
	s = d;
	while ((s <= end) && 
	       (*s && *s != separator))
	{
		s++;
	}
	if (*s == separator)
		s++;
	ReducedBy = d - s;

	/* Hack and slash */
	if (s >= end) {
		return 0;
	}
	else if (*s) {
		memmove(d, s, Source->BufUsed - (s - Source->buf));
		Source->BufUsed += ReducedBy;
		Source->buf[Source->BufUsed] = '\0';
	}
	else if (d == Source->buf) {
		*d = 0;
		Source->BufUsed = 0;
	}
	else {
		*--d = '\0';
		Source->BufUsed += ReducedBy;
	}
	/*
	while (*s) {
		*d++ = *s++;
	}
	*d = 0;
	*/
	return ReducedBy;
}

int StrBufExtract_tokenFromStr(StrBuf *dest, const char *Source, long SourceLen, int parmnum, char separator)
{
	const StrBuf Temp = {
		(char*)Source,
		SourceLen,
		SourceLen,
		1
#ifdef SIZE_DEBUG
		,
		0,
		"",
		""
#endif
	};

	return StrBufExtract_token(dest, &Temp, parmnum, separator);
}

/**
 *  a string tokenizer
 *  dest Destination StringBuffer
 *  Source StringBuffer to read into
 *  parmnum n'th Parameter to extract
 *  separator tokenizer character
 * @returns -1 if not found, else length of token.
 */
int StrBufExtract_token(StrBuf *dest, const StrBuf *Source, int parmnum, char separator)
{
	const char *s, *e;		//* source * /
	int len = 0;			//* running total length of extracted string * /
	int current_token = 0;		//* token currently being processed * /
	 
	if (dest != NULL) {
		dest->buf[0] = '\0';
		dest->BufUsed = 0;
	}
	else
		return(-1);

	if ((Source == NULL) || (Source->BufUsed ==0)) {
		return(-1);
	}
	s = Source->buf;
	e = s + Source->BufUsed;

	//cit_backtrace();
	//lprintf (CTDL_DEBUG, "test >: n: %d sep: %c source: %s \n willi \n", parmnum, separator, source);

	while ((s < e) && !IsEmptyStr(s)) {
		if (*s == separator) {
			++current_token;
		}
		if (len >= dest->BufSize) {
			dest->BufUsed = len;
			if (IncreaseBuf(dest, 1, -1) < 0) {
				dest->BufUsed --;
				break;
			}
		}
		if ( (current_token == parmnum) && 
		     (*s != separator)) {
			dest->buf[len] = *s;
			++len;
		}
		else if (current_token > parmnum) {
			break;
		}
		++s;
	}
	
	dest->buf[len] = '\0';
	dest->BufUsed = len;
		
	if (current_token < parmnum) {
		//lprintf (CTDL_DEBUG,"test <!: %s\n", dest);
		return(-1);
	}
	//lprintf (CTDL_DEBUG,"test <: %d; %s\n", len, dest);
	return(len);
}





/**
 *  a string tokenizer to fetch an integer
 *  Source String containing tokens
 *  parmnum n'th Parameter to extract
 *  separator tokenizer character
 * @returns 0 if not found, else integer representation of the token
 */
int StrBufExtract_int(const StrBuf* Source, int parmnum, char separator)
{
	StrBuf tmp;
	char buf[64];
	
	tmp.buf = buf;
	buf[0] = '\0';
	tmp.BufSize = 64;
	tmp.BufUsed = 0;
	tmp.ConstBuf = 1;
	if (StrBufExtract_token(&tmp, Source, parmnum, separator) > 0)
		return(atoi(buf));
	else
		return 0;
}

/**
 *  a string tokenizer to fetch a long integer
 *  Source String containing tokens
 *  parmnum n'th Parameter to extract
 *  separator tokenizer character
 * @returns 0 if not found, else long integer representation of the token
 */
long StrBufExtract_long(const StrBuf* Source, int parmnum, char separator)
{
	StrBuf tmp;
	char buf[64];
	
	tmp.buf = buf;
	buf[0] = '\0';
	tmp.BufSize = 64;
	tmp.BufUsed = 0;
	tmp.ConstBuf = 1;
	if (StrBufExtract_token(&tmp, Source, parmnum, separator) > 0)
		return(atoi(buf));
	else
		return 0;
}


/**
 *  a string tokenizer to fetch an unsigned long
 *  Source String containing tokens
 *  parmnum n'th Parameter to extract
 *  separator tokenizer character
 * @returns 0 if not found, else unsigned long representation of the token
 */
unsigned long StrBufExtract_unsigned_long(const StrBuf* Source, int parmnum, char separator)
{
	StrBuf tmp;
	char buf[64];
	char *pnum;
	
	tmp.buf = buf;
	buf[0] = '\0';
	tmp.BufSize = 64;
	tmp.BufUsed = 0;
	tmp.ConstBuf = 1;
	if (StrBufExtract_token(&tmp, Source, parmnum, separator) > 0) {
		pnum = &buf[0];
		if (*pnum == '-')
			pnum ++;
		return (unsigned long) atol(pnum);
	}
	else 
		return 0;
}



/**
 *  a string tokenizer; Bounds checker
 *  function to make shure whether StrBufExtract_NextToken and friends have reached the end of the string.
 *  Source our tokenbuffer
 *  pStart the token iterator pointer to inspect
 * @returns whether the revolving pointer is inside of the search range
 */
int StrBufHaveNextToken(const StrBuf *Source, const char **pStart)
{
	if ((Source == NULL) || 
	    (*pStart == StrBufNOTNULL) ||
	    (Source->BufUsed == 0))
	{
		return 0;
	}
	if (*pStart == NULL)
	{
		return 1;
	}
	else if (*pStart > Source->buf + Source->BufUsed)
	{
		return 0;
	}
	else if (*pStart <= Source->buf)
	{
		return 0;
	}

	return 1;
}

/**
 *  a string tokenizer
 *  dest Destination StringBuffer
 *  Source StringBuffer to read into
 *  pStart pointer to the end of the last token. Feed with NULL on start.
 *  separator tokenizer 
 * @returns -1 if not found, else length of token.
 */
int StrBufExtract_NextToken(StrBuf *dest, const StrBuf *Source, const char **pStart, char separator)
{
	const char *s;          /* source */
	const char *EndBuffer;  /* end stop of source buffer */
	int current_token = 0;	/* token currently being processed */
	int len = 0;		/* running total length of extracted string */

	if ((Source          == NULL) || 
	    (Source->BufUsed == 0)      ) 
	{
		*pStart = StrBufNOTNULL;
		if (dest != NULL)
			FlushStrBuf(dest);
		return -1;
	}
	 
	EndBuffer = Source->buf + Source->BufUsed;

	if (dest != NULL) 
	{
		dest->buf[0] = '\0';
		dest->BufUsed = 0;
	}
	else
	{
		*pStart = EndBuffer + 1;
		return -1;
	}

	if (*pStart == NULL)
	{
		*pStart = Source->buf; /* we're starting to examine this buffer. */
	}
	else if ((*pStart < Source->buf) || 
		 (*pStart > EndBuffer  )   ) 
	{
		return -1; /* no more tokens to find. */
	}

	s = *pStart;
	/* start to find the next token */
	while ((s <= EndBuffer)      && 
	       (current_token == 0) ) 
	{
		if (*s == separator) 
		{
			/* we found the next token */
			++current_token;
		}

		if (len >= dest->BufSize) 
		{
			/* our Dest-buffer isn't big enough, increase it. */
			dest->BufUsed = len;

			if (IncreaseBuf(dest, 1, -1) < 0) {
				/* WHUT? no more mem? bail out. */
				s = EndBuffer;
				dest->BufUsed --;
				break;
			}
		}

		if ( (current_token == 0 ) &&   /* are we in our target token? */
		     (!IsEmptyStr(s)     ) &&
		     (separator     != *s)    ) /* don't copy the token itself */
		{
			dest->buf[len] = *s;    /* Copy the payload */
			++len;                  /* remember the bigger size. */
		}

		++s;
	}

	/* did we reach the end? */
	if ((s > EndBuffer)) {
		EndBuffer = StrBufNOTNULL;
		*pStart = EndBuffer;
	}
	else {
		*pStart = s;  /* remember the position for the next run */
	}

	/* sanitize our extracted token */
	dest->buf[len] = '\0';
	dest->BufUsed  = len;

	return (len);
}


/**
 *  a string tokenizer
 *  Source StringBuffer to read from
 *  pStart pointer to the end of the last token. Feed with NULL.
 *  separator tokenizer character
 *  nTokens number of tokens to fastforward over
 * @returns -1 if not found, else length of token.
 */
int StrBufSkip_NTokenS(const StrBuf *Source, const char **pStart, char separator, int nTokens)
{
	const char *s, *EndBuffer;	//* source * /
	int len = 0;			//* running total length of extracted string * /
	int current_token = 0;		//* token currently being processed * /

	if ((Source == NULL) || 
	    (Source->BufUsed ==0)) {
		return(-1);
	}
	if (nTokens == 0)
		return Source->BufUsed;

	if (*pStart == NULL)
		*pStart = Source->buf;

	EndBuffer = Source->buf + Source->BufUsed;

	if ((*pStart < Source->buf) || 
	    (*pStart >  EndBuffer)) {
		return (-1);
	}


	s = *pStart;

	//cit_backtrace();
	//lprintf (CTDL_DEBUG, "test >: n: %d sep: %c source: %s \n willi \n", parmnum, separator, source);

	while ((s < EndBuffer) && !IsEmptyStr(s)) {
		if (*s == separator) {
			++current_token;
		}
		if (current_token >= nTokens) {
			break;
		}
		++s;
	}
	*pStart = s;
	(*pStart) ++;

	return(len);
}

/**
 *  a string tokenizer to fetch an integer
 *  Source StringBuffer to read from
 *  pStart Cursor on the tokenstring
 *  separator tokenizer character
 * @returns 0 if not found, else integer representation of the token
 */
int StrBufExtractNext_int(const StrBuf* Source, const char **pStart, char separator)
{
	StrBuf tmp;
	char buf[64];
	
	tmp.buf = buf;
	buf[0] = '\0';
	tmp.BufSize = 64;
	tmp.BufUsed = 0;
	tmp.ConstBuf = 1;
	if (StrBufExtract_NextToken(&tmp, Source, pStart, separator) > 0)
		return(atoi(buf));
	else
		return 0;
}

/**
 *  a string tokenizer to fetch a long integer
 *  Source StringBuffer to read from
 *  pStart Cursor on the tokenstring
 *  separator tokenizer character
 * @returns 0 if not found, else long integer representation of the token
 */
long StrBufExtractNext_long(const StrBuf* Source, const char **pStart, char separator)
{
	StrBuf tmp;
	char buf[64];
	
	tmp.buf = buf;
	buf[0] = '\0';
	tmp.BufSize = 64;
	tmp.BufUsed = 0;
	tmp.ConstBuf = 1;
	if (StrBufExtract_NextToken(&tmp, Source, pStart, separator) > 0)
		return(atoi(buf));
	else
		return 0;
}


/**
 *  a string tokenizer to fetch an unsigned long
 *  Source StringBuffer to read from
 *  pStart Cursor on the tokenstring
 *  separator tokenizer character
 * @returns 0 if not found, else unsigned long representation of the token
 */
unsigned long StrBufExtractNext_unsigned_long(const StrBuf* Source, const char **pStart, char separator)
{
	StrBuf tmp;
	char buf[64];
	char *pnum;
	
	tmp.buf = buf;
	buf[0] = '\0';
	tmp.BufSize = 64;
	tmp.BufUsed = 0;
	tmp.ConstBuf = 1;
	if (StrBufExtract_NextToken(&tmp, Source, pStart, separator) > 0) {
		pnum = &buf[0];
		if (*pnum == '-')
			pnum ++;
		return (unsigned long) atol(pnum);
	}
	else 
		return 0;
}





/*******************************************************************************
 *                             Escape Appending                                *
 *******************************************************************************/

/** 
 *  Escape a string for feeding out as a URL while appending it to a Buffer
 *  OutBuf the output buffer
 *  In Buffer to encode
 *  PlainIn way in from plain old c strings
 */
void StrBufUrlescAppend(StrBuf *OutBuf, const StrBuf *In, const char *PlainIn)
{
	const char *pch, *pche;
	char *pt, *pte;
	int len;
	
	if (((In == NULL) && (PlainIn == NULL)) || (OutBuf == NULL) )
		return;
	if (PlainIn != NULL) {
		len = strlen(PlainIn);
		pch = PlainIn;
		pche = pch + len;
	}
	else {
		pch = In->buf;
		pche = pch + In->BufUsed;
		len = In->BufUsed;
	}

	if (len == 0) 
		return;

	pt = OutBuf->buf + OutBuf->BufUsed;
	pte = OutBuf->buf + OutBuf->BufSize - 4; /**< we max append 3 chars at once plus the \0 */

	while (pch < pche) {
		if (pt >= pte) {
			IncreaseBuf(OutBuf, 1, -1);
			pte = OutBuf->buf + OutBuf->BufSize - 4; /**< we max append 3 chars at once plus the \0 */
			pt = OutBuf->buf + OutBuf->BufUsed;
		}

		if((*pch >= 'a' && *pch <= 'z') ||
		   (*pch >= '@' && *pch <= 'Z') || /* @ A-Z */
		   (*pch >= '0' && *pch <= ':') || /* 0-9 : */
		   (*pch == '!') || (*pch == '_') || 
		   (*pch == ',') || (*pch == '.'))
		{
			*(pt++) = *(pch++);
			OutBuf->BufUsed++;
		}			
		else {
			*pt = '%';
			*(pt + 1) = HexList[(unsigned char)*pch][0];
			*(pt + 2) = HexList[(unsigned char)*pch][1];
			pt += 3;
			OutBuf->BufUsed += 3;
			pch ++;
		}
	}
	*pt = '\0';
}


/*
 *  Escape a string for feeding out as a the username/password part of an URL while appending it to a Buffer
 *  OutBuf the output buffer
 *  In Buffer to encode
 *  PlainIn way in from plain old c strings
 */
void StrBufUrlescUPAppend(StrBuf *OutBuf, const StrBuf *In, const char *PlainIn) {
	const char *pch, *pche;
	char *pt, *pte;
	int len;
	
	if (((In == NULL) && (PlainIn == NULL)) || (OutBuf == NULL) )
		return;
	if (PlainIn != NULL) {
		len = strlen(PlainIn);
		pch = PlainIn;
		pche = pch + len;
	}
	else {
		pch = In->buf;
		pche = pch + In->BufUsed;
		len = In->BufUsed;
	}

	if (len == 0) 
		return;

	pt = OutBuf->buf + OutBuf->BufUsed;
	pte = OutBuf->buf + OutBuf->BufSize - 4; /**< we max append 3 chars at once plus the \0 */

	while (pch < pche) {
		if (pt >= pte) {
			IncreaseBuf(OutBuf, 1, -1);
			pte = OutBuf->buf + OutBuf->BufSize - 4; /**< we max append 3 chars at once plus the \0 */
			pt = OutBuf->buf + OutBuf->BufUsed;
		}

		if((*pch >= 'a' && *pch <= 'z') ||
		   (*pch >= 'A' && *pch <= 'Z') || /* A-Z */
		   (*pch >= '0' && *pch <= ':') || /* 0-9 : */
		   (*pch == '!') || (*pch == '_') || 
		   (*pch == ',') || (*pch == '.'))
		{
			*(pt++) = *(pch++);
			OutBuf->BufUsed++;
		}			
		else {
			*pt = '%';
			*(pt + 1) = HexList[(unsigned char)*pch][0];
			*(pt + 2) = HexList[(unsigned char)*pch][1];
			pt += 3;
			OutBuf->BufUsed += 3;
			pch ++;
		}
	}
	*pt = '\0';
}

/* 
 *  append a string with characters having a special meaning in xml encoded to the buffer
 *  OutBuf the output buffer
 *  In Buffer to encode
 *  PlainIn way in from plain old c strings
 *  PlainInLen way in from plain old c strings; maybe you've got binary data or know the length?
 *  OverrideLowChars should chars < 0x20 be replaced by _ or escaped as xml entity?
 */
void StrBufXMLEscAppend(StrBuf *OutBuf,
			const StrBuf *In,
			const char *PlainIn,
			long PlainInLen,
			int OverrideLowChars)
{
	const char *pch, *pche;
	char *pt, *pte;
	int IsUtf8Sequence;
	int len;

	if (((In == NULL) && (PlainIn == NULL)) || (OutBuf == NULL) )
		return;
	if (PlainIn != NULL) {
		if (PlainInLen < 0)
			len = strlen((const char*)PlainIn);
		else
			len = PlainInLen;
		pch = PlainIn;
		pche = pch + len;
	}
	else {
		pch = (const char*)In->buf;
		pche = pch + In->BufUsed;
		len = In->BufUsed;
	}

	if (len == 0)
		return;

	pt = OutBuf->buf + OutBuf->BufUsed;
	/**< we max append 6 chars at once plus the \0 */
	pte = OutBuf->buf + OutBuf->BufSize - 6;

	while (pch < pche) {
		if (pt >= pte) {
			OutBuf->BufUsed = pt - OutBuf->buf;
			IncreaseBuf(OutBuf, 1, -1);
			pte = OutBuf->buf + OutBuf->BufSize - 6;
			/**< we max append 3 chars at once plus the \0 */

			pt = OutBuf->buf + OutBuf->BufUsed;
		}

		if (*pch == '<') {
			memcpy(pt, HKEY("&lt;"));
			pt += 4;
			pch ++;
		}
		else if (*pch == '>') {
			memcpy(pt, HKEY("&gt;"));
			pt += 4;
			pch ++;
		}
		else if (*pch == '&') {
			memcpy(pt, HKEY("&amp;"));
			pt += 5;
			pch++;
		}
		else if ((*pch >= 0x20) && (*pch <= 0x7F)) {
			*pt = *pch;
			pt++; pch++;
		}
		else {
			IsUtf8Sequence =  Ctdl_GetUtf8SequenceLength(pch, pche);
			if (IsUtf8Sequence)
			{
				while ((IsUtf8Sequence > 0) && 
				       (pch < pche))
				{
					*pt = *pch;
					pt ++;
					pch ++;
					--IsUtf8Sequence;
				}
			}
			else
			{
				*pt = '&';
				pt++;
				*pt = HexList[*(unsigned char*)pch][0];
				pt ++;
				*pt = HexList[*(unsigned char*)pch][1];
				pt ++; pch ++;
				*pt = '&';
				pt++;
				pch ++;
			}
		}
	}
	*pt = '\0';
	OutBuf->BufUsed = pt - OutBuf->buf;
}


/** 
 *  append a string in hex encoding to the buffer
 *  OutBuf the output buffer
 *  In Buffer to encode
 *  PlainIn way in from plain old c strings
 *  PlainInLen way in from plain old c strings; maybe you've got binary data or know the length?
 */
void StrBufHexEscAppend(StrBuf *OutBuf, const StrBuf *In, const unsigned char *PlainIn, long PlainInLen)
{
	const unsigned char *pch, *pche;
	char *pt, *pte;
	int len;
	
	if (((In == NULL) && (PlainIn == NULL)) || (OutBuf == NULL) )
		return;
	if (PlainIn != NULL) {
		if (PlainInLen < 0)
			len = strlen((const char*)PlainIn);
		else
			len = PlainInLen;
		pch = PlainIn;
		pche = pch + len;
	}
	else {
		pch = (const unsigned char*)In->buf;
		pche = pch + In->BufUsed;
		len = In->BufUsed;
	}

	if (len == 0) 
		return;

	pt = OutBuf->buf + OutBuf->BufUsed;
	pte = OutBuf->buf + OutBuf->BufSize - 3; /**< we max append 3 chars at once plus the \0 */

	while (pch < pche) {
		if (pt >= pte) {
			IncreaseBuf(OutBuf, 1, -1);
			pte = OutBuf->buf + OutBuf->BufSize - 3; /**< we max append 3 chars at once plus the \0 */
			pt = OutBuf->buf + OutBuf->BufUsed;
		}

		*pt = HexList[*pch][0];
		pt ++;
		*pt = HexList[*pch][1];
		pt ++; pch ++; OutBuf->BufUsed += 2;
	}
	*pt = '\0';
}


void StrBufBase64Append(StrBuf *OutBuf, const StrBuf *In, const char *PlainIn, long PlainInLen, int linebreaks) {
	const char *pch;
	char *pt;
	int len;
	long ExpectLen;
	
	if (((In == NULL) && (PlainIn == NULL)) || (OutBuf == NULL) )
		return;
	if (PlainIn != NULL) {
		if (PlainInLen < 0)
			len = strlen(PlainIn);
		else
			len = PlainInLen;
		pch = PlainIn;
	}
	else {
		pch = In->buf;
		len = In->BufUsed;
	}

	if (len == 0) 
		return;

	ExpectLen = ((len * 134) / 100) + OutBuf->BufUsed;

	if (ExpectLen > OutBuf->BufSize)
		if (IncreaseBuf(OutBuf, 1, ExpectLen) < ExpectLen)
			return;

	pt = OutBuf->buf + OutBuf->BufUsed;

	len = CtdlEncodeBase64(pt, pch, len, linebreaks);

	pt += len;
	OutBuf->BufUsed += len;
	*pt = '\0';
}


// append a string in hex encoding to the buffer
// OutBuf	the output buffer
// In		Buffer to encode
// PlainIn	way in from plain old c strings
void StrBufHexescAppend(StrBuf *OutBuf, const StrBuf *In, const char *PlainIn) {
	StrBufHexEscAppend(OutBuf, In, (const unsigned char*) PlainIn, -1);
}

/*
 *  Append a string, escaping characters which have meaning in HTML.  
 *
 *  Target	target buffer
 *  Source	source buffer; set to NULL if you just have a C-String
 *  PlainIn       Plain-C string to append; set to NULL if unused
 *  nbsp		If nonzero, spaces are converted to non-breaking spaces.
 *  nolinebreaks	if set to 1, linebreaks are removed from the string.
 *                      if set to 2, linebreaks are replaced by &ltbr/&gt
 */
long StrEscAppend(StrBuf *Target, const StrBuf *Source, const char *PlainIn, int nbsp, int nolinebreaks)
{
	const char *aptr, *eiptr;
	char *bptr, *eptr;
	long len;

	if (((Source == NULL) && (PlainIn == NULL)) || (Target == NULL) )
		return -1;

	if (PlainIn != NULL) {
		aptr = PlainIn;
		len = strlen(PlainIn);
		eiptr = aptr + len;
	}
	else {
		aptr = Source->buf;
		eiptr = aptr + Source->BufUsed;
		len = Source->BufUsed;
	}

	if (len == 0) 
		return -1;

	bptr = Target->buf + Target->BufUsed;
	eptr = Target->buf + Target->BufSize - 11; /* our biggest unit to put in...  */

	while (aptr < eiptr){
		if(bptr >= eptr) {
			IncreaseBuf(Target, 1, -1);
			eptr = Target->buf + Target->BufSize - 11; /* our biggest unit to put in...  */
			bptr = Target->buf + Target->BufUsed;
		}
		if (*aptr == '<') {
			memcpy(bptr, "&lt;", 4);
			bptr += 4;
			Target->BufUsed += 4;
		}
		else if (*aptr == '>') {
			memcpy(bptr, "&gt;", 4);
			bptr += 4;
			Target->BufUsed += 4;
		}
		else if (*aptr == '&') {
			memcpy(bptr, "&amp;", 5);
			bptr += 5;
			Target->BufUsed += 5;
		}
		else if (*aptr == '"') {
			memcpy(bptr, "&quot;", 6);
			bptr += 6;
			Target->BufUsed += 6;
		}
		else if (*aptr == '\'') {
			memcpy(bptr, "&#39;", 5);
			bptr += 5;
			Target->BufUsed += 5;
		}
		else if (*aptr == LB) {
			*bptr = '<';
			bptr ++;
			Target->BufUsed ++;
		}
		else if (*aptr == RB) {
			*bptr = '>';
			bptr ++;
			Target->BufUsed ++;
		}
		else if (*aptr == QU) {
			*bptr ='"';
			bptr ++;
			Target->BufUsed ++;
		}
		else if ((*aptr == 32) && (nbsp == 1)) {
			memcpy(bptr, "&nbsp;", 6);
			bptr += 6;
			Target->BufUsed += 6;
		}
		else if ((*aptr == '\n') && (nolinebreaks == 1)) {
			*bptr='\0';	/* nothing */
		}
		else if ((*aptr == '\n') && (nolinebreaks == 2)) {
			memcpy(bptr, "&lt;br/&gt;", 11);
			bptr += 11;
			Target->BufUsed += 11;
		}


		else if ((*aptr == '\r') && (nolinebreaks != 0)) {
			*bptr='\0';	/* nothing */
		}
		else{
			*bptr = *aptr;
			bptr++;
			Target->BufUsed ++;
		}
		aptr ++;
	}
	*bptr = '\0';
	if ((bptr = eptr - 1 ) && !IsEmptyStr(aptr) )
		return -1;
	return Target->BufUsed;
}

/**
 *  Append a string, escaping characters which have meaning in HTML.  
 * Converts linebreaks into blanks; escapes single quotes
 *  Target	target buffer
 *  Source	source buffer; set to NULL if you just have a C-String
 *  PlainIn       Plain-C string to append; set to NULL if unused
 */
void StrMsgEscAppend(StrBuf *Target, const StrBuf *Source, const char *PlainIn)
{
	const char *aptr, *eiptr;
	char *tptr, *eptr;
	long len;

	if (((Source == NULL) && (PlainIn == NULL)) || (Target == NULL) )
		return ;

	if (PlainIn != NULL) {
		aptr = PlainIn;
		len = strlen(PlainIn);
		eiptr = aptr + len;
	}
	else {
		aptr = Source->buf;
		eiptr = aptr + Source->BufUsed;
		len = Source->BufUsed;
	}

	if (len == 0) 
		return;

	eptr = Target->buf + Target->BufSize - 8; 
	tptr = Target->buf + Target->BufUsed;
	
	while (aptr < eiptr){
		if(tptr >= eptr) {
			IncreaseBuf(Target, 1, -1);
			eptr = Target->buf + Target->BufSize - 8; 
			tptr = Target->buf + Target->BufUsed;
		}
	       
		if (*aptr == '\n') {
			*tptr = ' ';
			Target->BufUsed++;
		}
		else if (*aptr == '\r') {
			*tptr = ' ';
			Target->BufUsed++;
		}
		else if (*aptr == '\'') {
			*(tptr++) = '&';
			*(tptr++) = '#';
			*(tptr++) = '3';
			*(tptr++) = '9';
			*tptr = ';';
			Target->BufUsed += 5;
		} else {
			*tptr = *aptr;
			Target->BufUsed++;
		}
		tptr++; aptr++;
	}
	*tptr = '\0';
}



/**
 *  Append a string, escaping characters which have meaning in ICAL.  
 * [\n,] 
 *  Target	target buffer
 *  Source	source buffer; set to NULL if you just have a C-String
 *  PlainIn       Plain-C string to append; set to NULL if unused
 */
void StrIcalEscAppend(StrBuf *Target, const StrBuf *Source, const char *PlainIn)
{
	const char *aptr, *eiptr;
	char *tptr, *eptr;
	long len;

	if (((Source == NULL) && (PlainIn == NULL)) || (Target == NULL) )
		return ;

	if (PlainIn != NULL) {
		aptr = PlainIn;
		len = strlen(PlainIn);
		eiptr = aptr + len;
	}
	else {
		aptr = Source->buf;
		eiptr = aptr + Source->BufUsed;
		len = Source->BufUsed;
	}

	if (len == 0) 
		return;

	eptr = Target->buf + Target->BufSize - 8; 
	tptr = Target->buf + Target->BufUsed;
	
	while (aptr < eiptr){
		if(tptr + 3 >= eptr) {
			IncreaseBuf(Target, 1, -1);
			eptr = Target->buf + Target->BufSize - 8; 
			tptr = Target->buf + Target->BufUsed;
		}
	       
		if (*aptr == '\n') {
			*tptr = '\\';
			Target->BufUsed++;
			tptr++;
			*tptr = 'n';
			Target->BufUsed++;
		}
		else if (*aptr == '\r') {
			*tptr = '\\';
			Target->BufUsed++;
			tptr++;
			*tptr = 'r';
			Target->BufUsed++;
		}
		else if (*aptr == ',') {
			*tptr = '\\';
			Target->BufUsed++;
			tptr++;
			*tptr = ',';
			Target->BufUsed++;
		} else {
			*tptr = *aptr;
			Target->BufUsed++;
		}
		tptr++; aptr++;
	}
	*tptr = '\0';
}

/**
 *  Append a string, escaping characters which have meaning in JavaScript strings .  
 *
 *  Target	target buffer
 *  Source	source buffer; set to NULL if you just have a C-String
 *  PlainIn       Plain-C string to append; set to NULL if unused
 * @returns size of result or -1
 */
long StrECMAEscAppend(StrBuf *Target, const StrBuf *Source, const char *PlainIn)
{
	const char *aptr, *eiptr;
	char *bptr, *eptr;
	long len;
	int IsUtf8Sequence;

	if (((Source == NULL) && (PlainIn == NULL)) || (Target == NULL) )
		return -1;

	if (PlainIn != NULL) {
		aptr = PlainIn;
		len = strlen(PlainIn);
		eiptr = aptr + len;
	}
	else {
		aptr = Source->buf;
		eiptr = aptr + Source->BufUsed;
		len = Source->BufUsed;
	}

	if (len == 0) 
		return -1;

	bptr = Target->buf + Target->BufUsed;
	eptr = Target->buf + Target->BufSize - 7; /* our biggest unit to put in...  */

	while (aptr < eiptr){
		if(bptr >= eptr) {
			IncreaseBuf(Target, 1, -1);
			eptr = Target->buf + Target->BufSize - 7; /* our biggest unit to put in...  */
			bptr = Target->buf + Target->BufUsed;
		}
		switch (*aptr) {
		case '\n':
			memcpy(bptr, HKEY("\\n"));
			bptr += 2;
			Target->BufUsed += 2;				
			break;
		case '\r':
			memcpy(bptr, HKEY("\\r"));
			bptr += 2;
			Target->BufUsed += 2;
			break;
		case '"':
			*bptr = '\\';
			bptr ++;
			*bptr = '"';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case '\\':
			if ((*(aptr + 1) == 'u') &&
			    isxdigit(*(aptr + 2)) &&
			    isxdigit(*(aptr + 3)) &&
			    isxdigit(*(aptr + 4)) &&
			    isxdigit(*(aptr + 5)))
			{ /* oh, a unicode escaper. let it pass through. */
				memcpy(bptr, aptr, 6);
				aptr += 5;
				bptr +=6;
				Target->BufUsed += 6;
			}
			else 
			{
				*bptr = '\\';
				bptr ++;
				*bptr = '\\';
				bptr ++;
				Target->BufUsed += 2;
			}
			break;
		case '\b':
			*bptr = '\\';
			bptr ++;
			*bptr = 'b';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case '\f':
			*bptr = '\\';
			bptr ++;
			*bptr = 'f';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case '\t':
			*bptr = '\\';
			bptr ++;
			*bptr = 't';
			bptr ++;
			Target->BufUsed += 2;
			break;
		default:
			IsUtf8Sequence =  Ctdl_GetUtf8SequenceLength(aptr, eiptr);
			while (IsUtf8Sequence > 0){
				*bptr = *aptr;
				Target->BufUsed ++;
				if (--IsUtf8Sequence)
					aptr++;
				bptr++;
			}
		}
		aptr ++;
	}
	*bptr = '\0';
	if ((bptr == eptr - 1 ) && !IsEmptyStr(aptr) )
		return -1;
	return Target->BufUsed;
}

/**
 *  Append a string, escaping characters which have meaning in HTML + json.  
 *
 *  Target	target buffer
 *  Source	source buffer; set to NULL if you just have a C-String
 *  PlainIn       Plain-C string to append; set to NULL if unused
 *  nbsp		If nonzero, spaces are converted to non-breaking spaces.
 *  nolinebreaks	if set to 1, linebreaks are removed from the string.
 *                      if set to 2, linebreaks are replaced by &ltbr/&gt
 */
long StrHtmlEcmaEscAppend(StrBuf *Target, const StrBuf *Source, const char *PlainIn, int nbsp, int nolinebreaks)
{
	const char *aptr, *eiptr;
	char *bptr, *eptr;
	long len;
	int IsUtf8Sequence = 0;

	if (((Source == NULL) && (PlainIn == NULL)) || (Target == NULL) )
		return -1;

	if (PlainIn != NULL) {
		aptr = PlainIn;
		len = strlen(PlainIn);
		eiptr = aptr + len;
	}
	else {
		aptr = Source->buf;
		eiptr = aptr + Source->BufUsed;
		len = Source->BufUsed;
	}

	if (len == 0) 
		return -1;

	bptr = Target->buf + Target->BufUsed;
	eptr = Target->buf + Target->BufSize - 11; /* our biggest unit to put in...  */

	while (aptr < eiptr){
		if(bptr >= eptr) {
			IncreaseBuf(Target, 1, -1);
			eptr = Target->buf + Target->BufSize - 11; /* our biggest unit to put in...  */
			bptr = Target->buf + Target->BufUsed;
		}
		switch (*aptr) {
		case '<':
			memcpy(bptr, HKEY("&lt;"));
			bptr += 4;
			Target->BufUsed += 4;
			break;
		case '>':
			memcpy(bptr, HKEY("&gt;"));
			bptr += 4;
			Target->BufUsed += 4;
			break;
		case '&':
			memcpy(bptr, HKEY("&amp;"));
			bptr += 5;
			Target->BufUsed += 5;
			break;
		case LB:
			*bptr = '<';
			bptr ++;
			Target->BufUsed ++;
			break;
		case RB:
			*bptr = '>';
			bptr ++;
			Target->BufUsed ++;
			break;
		case '\n':
			switch (nolinebreaks) {
			case 1:
				*bptr='\0';	/* nothing */
				break;
			case 2:
				memcpy(bptr, HKEY("&lt;br/&gt;"));
				bptr += 11;
				Target->BufUsed += 11;
				break;
			default:
				memcpy(bptr, HKEY("\\n"));
				bptr += 2;
				Target->BufUsed += 2;				
			}
			break;
		case '\r':
			switch (nolinebreaks) {
			case 1:
			case 2:
				*bptr='\0';	/* nothing */
				break;
			default:
				memcpy(bptr, HKEY("\\r"));
				bptr += 2;
				Target->BufUsed += 2;
				break;
			}
			break;
		case '"':
		case QU:
			*bptr = '\\';
			bptr ++;
			*bptr = '"';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case '\\':
			if ((*(aptr + 1) == 'u') &&
			    isxdigit(*(aptr + 2)) &&
			    isxdigit(*(aptr + 3)) &&
			    isxdigit(*(aptr + 4)) &&
			    isxdigit(*(aptr + 5)))
			{ /* oh, a unicode escaper. let it pass through. */
				memcpy(bptr, aptr, 6);
				aptr += 5;
				bptr +=6;
				Target->BufUsed += 6;
			}
			else 
			{
				*bptr = '\\';
				bptr ++;
				*bptr = '\\';
				bptr ++;
				Target->BufUsed += 2;
			}
			break;
		case '\b':
			*bptr = '\\';
			bptr ++;
			*bptr = 'b';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case '\f':
			*bptr = '\\';
			bptr ++;
			*bptr = 'f';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case '\t':
			*bptr = '\\';
			bptr ++;
			*bptr = 't';
			bptr ++;
			Target->BufUsed += 2;
			break;
		case  32:
			if (nbsp == 1) {
				memcpy(bptr, HKEY("&nbsp;"));
				bptr += 6;
				Target->BufUsed += 6;
				break;
			}
		default:
			IsUtf8Sequence =  Ctdl_GetUtf8SequenceLength(aptr, eiptr);
			while (IsUtf8Sequence > 0){
				*bptr = *aptr;
				Target->BufUsed ++;
				if (--IsUtf8Sequence)
					aptr++;
				bptr++;
			}
		}
		aptr ++;
	}
	*bptr = '\0';
	if ((bptr = eptr - 1 ) && !IsEmptyStr(aptr) )
		return -1;
	return Target->BufUsed;
}


/*
 *  replace all non-Ascii characters by another
 *  Buf buffer to inspect
 *  repl charater to stamp over non ascii chars
 */
void StrBufAsciify(StrBuf *Buf, const char repl) {
	long offset;

	for (offset = 0; offset < Buf->BufUsed; offset ++)
		if (!isascii(Buf->buf[offset]))
			Buf->buf[offset] = repl;
	
}

/*
 *  unhide special chars hidden to the HTML escaper
 *  target buffer to put the unescaped string in
 *  source buffer to unescape
 */
void StrBufEUid_unescapize(StrBuf *target, const StrBuf *source) {
	int a, b, len;
	char hex[3];

	if ((source == NULL) || (target == NULL) || (target->buf == NULL)) {
		return;
	}

	if (target != NULL)
		FlushStrBuf(target);

	len = source->BufUsed;
	for (a = 0; a < len; ++a) {
		if (target->BufUsed >= target->BufSize)
			IncreaseBuf(target, 1, -1);

		if (source->buf[a] == '=') {
			hex[0] = source->buf[a + 1];
			hex[1] = source->buf[a + 2];
			hex[2] = 0;
			b = 0;
			sscanf(hex, "%02x", &b);
			target->buf[target->BufUsed] = b;
			target->buf[++target->BufUsed] = 0;
			a += 2;
		}
		else {
			target->buf[target->BufUsed] = source->buf[a];
			target->buf[++target->BufUsed] = 0;
		}
	}
}


/*
 *  hide special chars from the HTML escapers and friends
 *  target buffer to put the escaped string in
 *  source buffer to escape
 */
void StrBufEUid_escapize(StrBuf *target, const StrBuf *source) {
	int i, len;

	if (target != NULL)
		FlushStrBuf(target);

	if ((source == NULL) || (target == NULL) || (target->buf == NULL))
	{
		return;
	}

	len = source->BufUsed;
	for (i=0; i<len; ++i) {
		if (target->BufUsed + 4 >= target->BufSize)
			IncreaseBuf(target, 1, -1);
		if ( (isalnum(source->buf[i])) || 
		     (source->buf[i]=='-') || 
		     (source->buf[i]=='_') ) {
			target->buf[target->BufUsed++] = source->buf[i];
		}
		else {
			sprintf(&target->buf[target->BufUsed], 
				"=%02X", 
				(0xFF &source->buf[i]));
			target->BufUsed += 3;
		}
	}
	target->buf[target->BufUsed + 1] = '\0';
}


/*******************************************************************************
 *                      Quoted Printable de/encoding                           *
 *******************************************************************************/

/*
 *  decode a buffer from base 64 encoding; destroys original
 *  Buf Buffor to transform
 */
int StrBufDecodeBase64(StrBuf *Buf) {
	char *xferbuf;
	size_t siz;

	if (Buf == NULL) {
		return -1;
	}

	xferbuf = (char*) malloc(Buf->BufSize);
	if (xferbuf == NULL)
		return -1;

	*xferbuf = '\0';
	siz = CtdlDecodeBase64(xferbuf, Buf->buf, Buf->BufUsed);
	free(Buf->buf);
	Buf->buf = xferbuf;
	Buf->BufUsed = siz;

	Buf->buf[Buf->BufUsed] = '\0';
	return siz;
}


/*
 *  decode a buffer from base 64 encoding; expects targetbuffer
 *  BufIn Buffor to transform
 *  BufOut Buffer to put result into
 */
int StrBufDecodeBase64To(const StrBuf *BufIn, StrBuf *BufOut) {
	if ((BufIn == NULL) || (BufOut == NULL))
		return -1;

	if (BufOut->BufSize < BufIn->BufUsed) {
		IncreaseBuf(BufOut, 0, BufIn->BufUsed);
	}

	BufOut->BufUsed = CtdlDecodeBase64(BufOut->buf, BufIn->buf, BufIn->BufUsed);
	return BufOut->BufUsed;
}

typedef struct __z_enc_stream {
	StrBuf OutBuf;
	z_stream zstream;
} z_enc_stream;


vStreamT *StrBufNewStreamContext(eStreamType type, const char **Err) {
	//base64_decodestate *state;;
	*Err = NULL;

	switch (type) {

		//case eBase64Decode:
		//case eBase64Encode:
			//state = (base64_decodestate*) malloc(sizeof(base64_decodestate));
			//base64_init_decodestate(state);
			//return (vStreamT*) state;
			//break;

		case eZLibDecode: {

			z_enc_stream *stream;
			int err;
	
			stream = (z_enc_stream *) malloc(sizeof(z_enc_stream));
			memset(stream, 0, sizeof(z_enc_stream));
			stream->OutBuf.BufSize = 4*SIZ; /// TODO 64
			stream->OutBuf.buf = (char*)malloc(stream->OutBuf.BufSize);
		
			err = inflateInit(&stream->zstream);

			if (err != Z_OK) {
				StrBufDestroyStreamContext(type, (vStreamT**) &stream, Err);
				*Err = zError(err);
				return NULL;
			}
			return (vStreamT*) stream;
	
		}

		case eZLibEncode: {
			z_enc_stream *stream;
			int err;
	
			stream = (z_enc_stream *) malloc(sizeof(z_enc_stream));
			memset(stream, 0, sizeof(z_enc_stream));
			stream->OutBuf.BufSize = 4*SIZ; /// todo 64
			stream->OutBuf.buf = (char*)malloc(stream->OutBuf.BufSize);
			/* write gzip header */
			stream->OutBuf.BufUsed = snprintf
				(stream->OutBuf.buf,
			 	stream->OutBuf.BufSize, 
			 	"%c%c%c%c%c%c%c%c%c%c",
			 	gz_magic[0], gz_magic[1], Z_DEFLATED,
			 	0 /*flags */ , 0, 0, 0, 0 /*time */ , 0 /* xflags */ ,
			 	OS_CODE);
	
			err = deflateInit2(&stream->zstream,
				   	ZLibCompressionRatio,
				   	Z_DEFLATED,
				   	-MAX_WBITS,
				   	DEF_MEM_LEVEL,
				   	Z_DEFAULT_STRATEGY);
			if (err != Z_OK) {
				StrBufDestroyStreamContext(type, (vStreamT**) &stream, Err);
				*Err = zError(err);
				return NULL;
			}
			return (vStreamT*) stream;
		}

		case eEmtyCodec:
			/// TODO
			break;
		}
	return NULL;
}


int StrBufDestroyStreamContext(eStreamType type, vStreamT **vStream, const char **Err) {
	int err;
	int rc = 0;
	*Err = NULL;
	
	if ((vStream == NULL) || (*vStream==NULL)) {
		*Err = strerror(EINVAL);
		return EINVAL;
	}
	switch (type)
	{
	//case eBase64Encode:
	//case eBase64Decode:
		//free(*vStream);
		//*vStream = NULL;
		//break;
	case eZLibDecode:
	{
		z_enc_stream *stream = (z_enc_stream *)*vStream;
		(void)inflateEnd(&stream->zstream);
		free(stream->OutBuf.buf);
		free(stream);
	}
	case eZLibEncode:
	{
		z_enc_stream *stream = (z_enc_stream *)*vStream;
		err = deflateEnd(&stream->zstream);
		if (err != Z_OK) {
			*Err = zError(err);
			rc = -1;
		}
		free(stream->OutBuf.buf);
		free(stream);
		*vStream = NULL;
		break;
	}
	case eEmtyCodec: 
		break; /// TODO
	}
	return rc;
}

int StrBufStreamTranscode(eStreamType type, IOBuffer *Target, IOBuffer *In, const char* pIn, long pInLen, vStreamT *vStream, int LastChunk, const char **Err) {
	int rc = 0;
	switch (type)
	{
	//case eBase64Encode:
	//{
		// ???
	//}
	//break;
	//case eBase64Decode:
	//{
		//// ???
	//}
	//break;
	case eZLibEncode:
	{
		z_enc_stream *stream = (z_enc_stream *)vStream;
		int org_outbuf_len = stream->OutBuf.BufUsed;
		int err;
		unsigned int chunkavail;

		if (In->ReadWritePointer != NULL)
		{
			stream->zstream.next_in = (Bytef *) In->ReadWritePointer;
			stream->zstream.avail_in = (uInt) In->Buf->BufUsed - 
				(In->ReadWritePointer - In->Buf->buf);
		}
		else
		{
			stream->zstream.next_in = (Bytef *) In->Buf->buf;
			stream->zstream.avail_in = (uInt) In->Buf->BufUsed;
		}
		
		stream->zstream.next_out = (unsigned char*)stream->OutBuf.buf + stream->OutBuf.BufUsed;
		stream->zstream.avail_out = chunkavail = (uInt) stream->OutBuf.BufSize - stream->OutBuf.BufUsed;

		err = deflate(&stream->zstream,  (LastChunk) ? Z_FINISH : Z_NO_FLUSH);

		stream->OutBuf.BufUsed += (chunkavail - stream->zstream.avail_out);

		if (Target && (LastChunk || (stream->OutBuf.BufUsed != org_outbuf_len))) {
			iSwapBuffers(Target->Buf, &stream->OutBuf);
		}

		if (stream->zstream.avail_in == 0) {
			FlushStrBuf(In->Buf);
			In->ReadWritePointer = NULL;
		}
		else {
			if (stream->zstream.avail_in < 64) {
				memmove(In->Buf->buf,
					In->Buf->buf + In->Buf->BufUsed - stream->zstream.avail_in,
					stream->zstream.avail_in);

				In->Buf->BufUsed = stream->zstream.avail_in;
				In->Buf->buf[In->Buf->BufUsed] = '\0';
			}
			else {
				In->ReadWritePointer = In->Buf->buf + (In->Buf->BufUsed - stream->zstream.avail_in);
			}
		}
		rc = (LastChunk && (err != Z_FINISH));
		if (!rc && (err != Z_OK)) {
			*Err = zError(err);
		}
		
	}
	break;
	case eZLibDecode: {
		z_enc_stream *stream = (z_enc_stream *)vStream;
		int org_outbuf_len = stream->zstream.total_out;
		int err;

		if ((stream->zstream.avail_out != 0) && (stream->zstream.next_in != NULL)) {
			if (In->ReadWritePointer != NULL) {
				stream->zstream.next_in = (Bytef *) In->ReadWritePointer;
				stream->zstream.avail_in = (uInt) In->Buf->BufUsed - (In->ReadWritePointer - In->Buf->buf);
			}
			else {
				stream->zstream.next_in = (Bytef *) In->Buf->buf;
				stream->zstream.avail_in = (uInt) In->Buf->BufUsed;
			}
		}

		stream->zstream.next_out = (unsigned char*)stream->OutBuf.buf + stream->OutBuf.BufUsed;
		stream->zstream.avail_out = (uInt) stream->OutBuf.BufSize - stream->OutBuf.BufUsed;

		err = inflate(&stream->zstream, Z_NO_FLUSH);

		///assert(ret != Z_STREAM_ERROR);  /* state not clobbered * /
		switch (err) {
		case Z_NEED_DICT:
			err = Z_DATA_ERROR;     /* and fall through */

		case Z_DATA_ERROR:
			*Err = zError(err);
		case Z_MEM_ERROR:
			(void)inflateEnd(&stream->zstream);
			return err;
		}

		stream->OutBuf.BufUsed += stream->zstream.total_out + org_outbuf_len;

		if (Target) iSwapBuffers(Target->Buf, &stream->OutBuf);

		if (stream->zstream.avail_in == 0) {
			FlushStrBuf(In->Buf);
			In->ReadWritePointer = NULL;
		}
		else {
			if (stream->zstream.avail_in < 64) {
				memmove(In->Buf->buf,
					In->Buf->buf + In->Buf->BufUsed - stream->zstream.avail_in,
					stream->zstream.avail_in);

				In->Buf->BufUsed = stream->zstream.avail_in;
				In->Buf->buf[In->Buf->BufUsed] = '\0';
			}
			else {
				
				In->ReadWritePointer = In->Buf->buf + (In->Buf->BufUsed - stream->zstream.avail_in);
			}
		}
	}
		break;
	case eEmtyCodec: {

	}
		break; /// TODO
	}
	return rc;
}

/**
 *  decode a buffer from base 64 encoding; destroys original
 *  Buf Buffor to transform
 */
int StrBufDecodeHex(StrBuf *Buf) {
	unsigned int ch;
	char *pch, *pche, *pchi;

	if (Buf == NULL) return -1;

	pch = pchi = Buf->buf;
	pche = pch + Buf->BufUsed;

	while (pchi < pche){
		ch = decode_hex(pchi);
		*pch = ch;
		pch ++;
		pchi += 2;
	}

	*pch = '\0';
	Buf->BufUsed = pch - Buf->buf;
	return Buf->BufUsed;
}

/**
 *  replace all chars >0x20 && < 0x7F with Mute
 *  Mute char to put over invalid chars
 *  Buf Buffor to transform
 */
int StrBufSanitizeAscii(StrBuf *Buf, const char Mute)
{
	unsigned char *pch;

	if (Buf == NULL) return -1;
	pch = (unsigned char *)Buf->buf;
	while (pch < (unsigned char *)Buf->buf + Buf->BufUsed) {
		if ((*pch < 0x20) || (*pch > 0x7F))
			*pch = Mute;
		pch ++;
	}
	return Buf->BufUsed;
}


/**
 *  remove escaped strings from i.e. the url string (like %20 for blanks)
 *  Buf Buffer to translate
 *  StripBlanks Reduce several blanks to one?
 */
long StrBufUnescape(StrBuf *Buf, int StripBlanks)
{
	int a, b;
	char hex[3];
	long len;

	if (Buf == NULL)
		return -1;

	while ((Buf->BufUsed > 0) && (isspace(Buf->buf[Buf->BufUsed - 1]))){
		Buf->buf[Buf->BufUsed - 1] = '\0';
		Buf->BufUsed --;
	}

	a = 0; 
	while (a < Buf->BufUsed) {
		if (Buf->buf[a] == '+')
			Buf->buf[a] = ' ';
		else if (Buf->buf[a] == '%') {
			/* don't let % chars through, rather truncate the input. */
			if (a + 2 > Buf->BufUsed) {
				Buf->buf[a] = '\0';
				Buf->BufUsed = a;
			}
			else {			
				hex[0] = Buf->buf[a + 1];
				hex[1] = Buf->buf[a + 2];
				hex[2] = 0;
				b = 0;
				sscanf(hex, "%02x", &b);
				Buf->buf[a] = (char) b;
				len = Buf->BufUsed - a - 2;
				if (len > 0)
					memmove(&Buf->buf[a + 1], &Buf->buf[a + 3], len);
			
				Buf->BufUsed -=2;
			}
		}
		a++;
	}
	return a;
}


/**
 * 	RFC2047-encode a header field if necessary.
 *		If no non-ASCII characters are found, the string
 *		will be copied verbatim without encoding.
 *
 * 	target		Target buffer.
 * 	source		Source string to be encoded.
 * @returns     encoded length; -1 if non success.
 */
int StrBufRFC2047encode(StrBuf **target, const StrBuf *source)
{
	const char headerStr[] = "=?UTF-8?Q?";
	int need_to_encode = 0;
	int i = 0;
	unsigned char ch;

	if ((source == NULL) || 
	    (target == NULL))
	    return -1;

	while ((i < source->BufUsed) &&
	       (!IsEmptyStr (&source->buf[i])) &&
	       (need_to_encode == 0)) {
		if (((unsigned char) source->buf[i] < 32) || 
		    ((unsigned char) source->buf[i] > 126)) {
			need_to_encode = 1;
		}
		i++;
	}

	if (!need_to_encode) {
		if (*target == NULL) {
			*target = NewStrBufPlain(source->buf, source->BufUsed);
		}
		else {
			FlushStrBuf(*target);
			StrBufAppendBuf(*target, source, 0);
		}
		if (*target != 0)
			return (*target)->BufUsed;
		else
			return 0;
	}
	if (*target == NULL)
		*target = NewStrBufPlain(NULL, sizeof(headerStr) + source->BufUsed * 2);
	else if (sizeof(headerStr) + source->BufUsed >= (*target)->BufSize)
		IncreaseBuf(*target, sizeof(headerStr) + source->BufUsed, 0);
	memcpy ((*target)->buf, headerStr, sizeof(headerStr) - 1);
	(*target)->BufUsed = sizeof(headerStr) - 1;
	for (i=0; (i < source->BufUsed); ++i) {
		if ((*target)->BufUsed + 4 >= (*target)->BufSize)
			IncreaseBuf(*target, 1, 0);
		ch = (unsigned char) source->buf[i];
		if ((ch  <  32) || 
		    (ch  > 126) || 
		    (ch == '=') ||
                    (ch == '?') ||
		    (ch == '_') ||
		    (ch == '[') ||
		    (ch == ']')   )
		{
			sprintf(&(*target)->buf[(*target)->BufUsed], "=%02X", ch);
			(*target)->BufUsed += 3;
		}
		else {
			if (ch == ' ')
				(*target)->buf[(*target)->BufUsed] = '_';
			else
				(*target)->buf[(*target)->BufUsed] = ch;
			(*target)->BufUsed++;
		}
	}
	
	if ((*target)->BufUsed + 4 >= (*target)->BufSize)
		IncreaseBuf(*target, 1, 0);

	(*target)->buf[(*target)->BufUsed++] = '?';
	(*target)->buf[(*target)->BufUsed++] = '=';
	(*target)->buf[(*target)->BufUsed] = '\0';
	return (*target)->BufUsed;;
}

// Quoted-Printable encode a message; make it < 80 columns width.
// source	Source string to be encoded.
// returns	buffer with encoded message.
StrBuf *StrBufQuotedPrintableEncode(const StrBuf *EncodeMe) {
	StrBuf *OutBuf;
	char *Optr, *OEptr;
	const char *ptr, *eptr;
	unsigned char ch;
	int LinePos;

	OutBuf = NewStrBufPlain(NULL, StrLength(EncodeMe) * 4);
	Optr = OutBuf->buf;
	OEptr = OutBuf->buf + OutBuf->BufSize;
	ptr = EncodeMe->buf;
	eptr = EncodeMe->buf + EncodeMe->BufUsed;
	LinePos = 0;

	while (ptr < eptr) {
		if (Optr + 4 >= OEptr) {
			long Offset;
			Offset = Optr - OutBuf->buf;
			OutBuf->BufUsed = Optr - OutBuf->buf;
			IncreaseBuf(OutBuf, 1, 0);
			Optr = OutBuf->buf + Offset;
			OEptr = OutBuf->buf + OutBuf->BufSize;
		}
		if (*ptr == '\r') {		// ignore carriage returns
			ptr ++;
		}
		else if (*ptr == '\n') {	// hard line break
			memcpy(Optr, HKEY("=0A"));
			Optr += 3;
			LinePos += 3;
			ptr ++;
		}
		else if (( (*ptr >= 32) && (*ptr <= 60) ) || ( (*ptr >= 62) && (*ptr <= 126) )) {
			*Optr = *ptr;
			Optr ++;
			ptr ++;
			LinePos ++;
		}
		else {
			ch = *ptr;
			*Optr = '=';
			Optr ++;
			*Optr = HexList[ch][0];
			Optr ++;
			*Optr = HexList[ch][1];
			Optr ++;
			LinePos += 3;
			ptr ++;
		}

		if (LinePos > 72) {		// soft line break
			if (isspace(*(Optr - 1))) {
				ch = *(Optr - 1);
				Optr --;
				*Optr = '=';
				Optr ++;
				*Optr = HexList[ch][0];
				Optr ++;
				*Optr = HexList[ch][1];
				Optr ++;
				LinePos += 3;
			}
			*Optr = '=';
			Optr ++;
			*Optr = '\n';
			Optr ++;
			LinePos = 0;
		}
	}
	*Optr = '\0';
	OutBuf->BufUsed = Optr - OutBuf->buf;

	return OutBuf;
}


static void AddRecipient(StrBuf *Target, StrBuf *UserName, StrBuf *EmailAddress, StrBuf *EncBuf) {
	int QuoteMe = 0;

	if (StrLength(Target) > 0) StrBufAppendBufPlain(Target, HKEY(", "), 0);
	if (strchr(ChrPtr(UserName), ',') != NULL) QuoteMe = 1;

	if (QuoteMe)  StrBufAppendBufPlain(Target, HKEY("\""), 0);
	StrBufRFC2047encode(&EncBuf, UserName);
	StrBufAppendBuf(Target, EncBuf, 0);
	if (QuoteMe)  StrBufAppendBufPlain(Target, HKEY("\" "), 0);
	else          StrBufAppendBufPlain(Target, HKEY(" "), 0);

	if (StrLength(EmailAddress) > 0){
		StrBufAppendBufPlain(Target, HKEY("<"), 0);
		StrBufAppendBuf(Target, EmailAddress, 0); /* TODO: what about IDN???? */
		StrBufAppendBufPlain(Target, HKEY(">"), 0);
	}
}


/**
 * \brief QP encode parts of an email TO/CC/BCC vector, and strip/filter invalid parts
 * \param Recp Source list of email recipients
 * \param UserName Temporary buffer for internal use; Please provide valid buffer.
 * \param EmailAddress Temporary buffer for internal use; Please provide valid buffer.
 * \param EncBuf Temporary buffer for internal use; Please provide valid buffer.
 * \returns encoded & sanitized buffer with the contents of Recp; Caller owns this memory.
 */
StrBuf *StrBufSanitizeEmailRecipientVector(const StrBuf *Recp, StrBuf *UserName, StrBuf *EmailAddress, StrBuf *EncBuf) {
	StrBuf *Target;
	const char *pch, *pche;
	const char *UserStart, *UserEnd, *EmailStart, *EmailEnd, *At;

	if ((Recp == NULL) || (StrLength(Recp) == 0))
		return NULL;

	pch = ChrPtr(Recp);
	pche = pch + StrLength(Recp);

	if (!CheckEncode(pch, -1, pche))
		return NewStrBufDup(Recp);

	Target = NewStrBufPlain(NULL, StrLength(Recp));

	while ((pch != NULL) && (pch < pche))
	{
		while (isspace(*pch)) pch++;
		UserEnd = EmailStart = EmailEnd = NULL;
		
		if ((*pch == '"') || (*pch == '\'')) {
			UserStart = pch + 1;
			
			UserEnd = strchr(UserStart, *pch);
			if (UserEnd == NULL) 
				break; ///TODO: Userfeedback??
			EmailStart = UserEnd + 1;
			while (isspace(*EmailStart))
				EmailStart++;
			if (UserEnd == UserStart) {
				UserStart = UserEnd = NULL;
			}
			
			if (*EmailStart == '<') {
				EmailStart++;
				EmailEnd = strchr(EmailStart, '>');
				if (EmailEnd == NULL)
					EmailEnd = strchr(EmailStart, ',');
				
			}
			else {
				EmailEnd = strchr(EmailStart, ',');
			}
			if (EmailEnd == NULL)
				EmailEnd = pche;
			pch = EmailEnd + 1;
		}
		else {
			int gt = 0;
			UserStart = pch;
			EmailEnd = strchr(UserStart, ',');
			if (EmailEnd == NULL) {
				EmailEnd = strchr(pch, '>');
				pch = NULL;
				if (EmailEnd != NULL) {
					gt = 1;
				}
				else {
					EmailEnd = pche;
				}
			}
			else {

				pch = EmailEnd + 1;
				while ((EmailEnd > UserStart) && !gt &&
				       ((*EmailEnd == ',') ||
					(*EmailEnd == '>') ||
					(isspace(*EmailEnd))))
				{
					if (*EmailEnd == '>')
						gt = 1;
					else 
						EmailEnd--;
				}
				if (EmailEnd == UserStart)
					break;
			}
			if (gt) {
				EmailStart = strchr(UserStart, '<');
				if ((EmailStart == NULL) || (EmailStart > EmailEnd))
					break;
				UserEnd = EmailStart;

				while ((UserEnd > UserStart) && 
				       isspace (*(UserEnd - 1)))
					UserEnd --;
				EmailStart ++;
				if (UserStart >= UserEnd)
					UserStart = UserEnd = NULL;
			}
			else { /* this is a local recipient... no domain, just a realname */
				EmailStart = UserStart;
				At = strchr(EmailStart, '@');
				if (At == NULL) {
					UserEnd = EmailEnd;
					EmailEnd = NULL;
				}
				else {
					EmailStart = UserStart;
					UserStart = NULL;
				}
			}
		}

		if ((UserStart != NULL) && (UserEnd != NULL))
			StrBufPlain(UserName, UserStart, UserEnd - UserStart);
		else if ((UserStart != NULL) && (UserEnd == NULL))
			StrBufPlain(UserName, UserStart, UserEnd - UserStart);
		else
			FlushStrBuf(UserName);

		if ((EmailStart != NULL) && (EmailEnd != NULL))
			StrBufPlain(EmailAddress, EmailStart, EmailEnd - EmailStart);
		else if ((EmailStart != NULL) && (EmailEnd == NULL))
			StrBufPlain(EmailAddress, EmailStart, EmailEnd - pche);
		else 
			FlushStrBuf(EmailAddress);

		AddRecipient(Target, UserName, EmailAddress, EncBuf);

		if (pch == NULL)
			break;
		
		if ((pch != NULL) && (*pch == ','))
			pch ++;
		if (pch != NULL) while (isspace(*pch))
			pch ++;
	}
	return Target;
}


/**
 *  replaces all occurances of 'search' by 'replace'
 *  buf Buffer to modify
 *  search character to search
 *  replace character to replace search by
 */
void StrBufReplaceChars(StrBuf *buf, char search, char replace) {
	long i;
	if (buf == NULL)
		return;
	for (i=0; i<buf->BufUsed; i++)
		if (buf->buf[i] == search)
			buf->buf[i] = replace;

}

/**
 *  removes all \\r s from the string, or replaces them with \n if its not a combination of both.
 *  buf Buffer to modify
 */
void StrBufToUnixLF(StrBuf *buf)
{
	char *pche, *pchS, *pchT;
	if (buf == NULL)
		return;

	pche = buf->buf + buf->BufUsed;
	pchS = pchT = buf->buf;
	while (pchS < pche)
	{
		if (*pchS == '\r')
		{
			pchS ++;
			if (*pchS != '\n') {
				*pchT = '\n';
				pchT++;
			}
		}
		*pchT = *pchS;
		pchT++; pchS++;
	}
	*pchT = '\0';
	buf->BufUsed = pchT - buf->buf;
}


/*******************************************************************************
 *                 Iconv Wrapper; RFC822 de/encoding                           *
 *******************************************************************************/

/**
 *  Wrapper around iconv_open()
 * Our version adds aliases for non-standard Microsoft charsets
 * such as 'MS950', aliasing them to names like 'CP950'
 *
 *  tocode	Target encoding
 *  fromcode	Source encoding
 *  pic           anonimized pointer to iconv struct
 */
void  ctdl_iconv_open(const char *tocode, const char *fromcode, void *pic)
{
#ifdef HAVE_ICONV
	iconv_t ic = (iconv_t)(-1) ;
	ic = iconv_open(tocode, fromcode);
	if (ic == (iconv_t)(-1) ) {
		char alias_fromcode[64];
		if ( (strlen(fromcode) == 5) && (!strncasecmp(fromcode, "MS", 2)) ) {
			safestrncpy(alias_fromcode, fromcode, sizeof alias_fromcode);
			alias_fromcode[0] = 'C';
			alias_fromcode[1] = 'P';
			ic = iconv_open(tocode, alias_fromcode);
		}
	}
	*(iconv_t *)pic = ic;
#endif
}


/**
 *  find one chunk of a RFC822 encoded string
 *  Buffer where to search
 *  bptr where to start searching
 * @returns found position, NULL if none.
 */
static inline const char *FindNextEnd (const StrBuf *Buf, const char *bptr)
{
	const char * end;
	/* Find the next ?Q? */
	if (Buf->BufUsed - (bptr - Buf->buf)  < 6)
		return NULL;

	end = strchr(bptr + 2, '?');

	if (end == NULL)
		return NULL;

	if ((Buf->BufUsed - (end - Buf->buf) > 3) &&
	    (((*(end + 1) == 'B') || (*(end + 1) == 'Q')) ||
	     ((*(end + 1) == 'b') || (*(end + 1) == 'q'))) && 
	    (*(end + 2) == '?')) {
		/* skip on to the end of the cluster, the next ?= */
		end = strstr(end + 3, "?=");
	}
	else
		/* sort of half valid encoding, try to find an end. */
		end = strstr(bptr, "?=");
	return end;
}



/**
 *  convert one buffer according to the preselected iconv pointer PIC
 *  ConvertBuf buffer we need to translate
 *  TmpBuf To share a workbuffer over several iterations. prepare to have it filled with useless stuff afterwards.
 *  pic Pointer to the iconv-session Object
 */
void StrBufConvert(StrBuf *ConvertBuf, StrBuf *TmpBuf, void *pic)
{
#ifdef HAVE_ICONV
	long trycount = 0;
	size_t siz;
	iconv_t ic;
	char *ibuf;			/**< Buffer of characters to be converted */
	char *obuf;			/**< Buffer for converted characters */
	size_t ibuflen;			/**< Length of input buffer */
	size_t obuflen;			/**< Length of output buffer */


	if ((ConvertBuf == NULL) || (TmpBuf == NULL))
		return;

	/* since we're converting to utf-8, one glyph may take up to 6 bytes */
	if (ConvertBuf->BufUsed * 6 >= TmpBuf->BufSize)
		IncreaseBuf(TmpBuf, 0, ConvertBuf->BufUsed * 6);
TRYAGAIN:
	ic = *(iconv_t*)pic;
	ibuf = ConvertBuf->buf;
	ibuflen = ConvertBuf->BufUsed;
	obuf = TmpBuf->buf;
	obuflen = TmpBuf->BufSize;
	
	siz = iconv(ic, &ibuf, &ibuflen, &obuf, &obuflen);

	if (siz < 0) {
		if (errno == E2BIG) {
			trycount ++;			
			IncreaseBuf(TmpBuf, 0, 0);
			if (trycount < 5) 
				goto TRYAGAIN;

		}
		else if (errno == EILSEQ){ 
			/* hm, invalid utf8 sequence... what to do now? */
			/* An invalid multibyte sequence has been encountered in the input */
		}
		else if (errno == EINVAL) {
			/* An incomplete multibyte sequence has been encountered in the input. */
		}

		FlushStrBuf(TmpBuf);
	}
	else {
		TmpBuf->BufUsed = TmpBuf->BufSize - obuflen;
		TmpBuf->buf[TmpBuf->BufUsed] = '\0';
		
		/* little card game: wheres the red lady? */
		iSwapBuffers(ConvertBuf, TmpBuf);
		FlushStrBuf(TmpBuf);
	}
#endif
}


/**
 *  catches one RFC822 encoded segment, and decodes it.
 *  Target buffer to fill with result
 *  DecodeMe buffer with stuff to process
 *  SegmentStart points to our current segment in DecodeMe
 *  SegmentEnd Points to the end of our current segment in DecodeMe
 *  ConvertBuf Workbuffer shared between several iterations. Random content; needs to be valid
 *  ConvertBuf2 Workbuffer shared between several iterations. Random content; needs to be valid
 *  FoundCharset Characterset to default decoding to; if we find another we will overwrite it.
 */
inline static void DecodeSegment(StrBuf *Target, 
				 const StrBuf *DecodeMe, 
				 const char *SegmentStart, 
				 const char *SegmentEnd, 
				 StrBuf *ConvertBuf,
				 StrBuf *ConvertBuf2, 
				 StrBuf *FoundCharset)
{
	StrBuf StaticBuf;
	char charset[128];
	char encoding[16];
#ifdef HAVE_ICONV
	iconv_t ic = (iconv_t)(-1);
#else
	void *ic = NULL;
#endif
	/* Now we handle foreign character sets properly encoded
	 * in RFC2047 format.
	 */
	StaticBuf.buf = (char*) SegmentStart; /*< it will just be read there... */
	StaticBuf.BufUsed = SegmentEnd - SegmentStart;
	StaticBuf.BufSize = DecodeMe->BufSize - (SegmentStart - DecodeMe->buf);
	extract_token(charset, SegmentStart, 1, '?', sizeof charset);
	if (FoundCharset != NULL) {
		FlushStrBuf(FoundCharset);
		StrBufAppendBufPlain(FoundCharset, charset, -1, 0);
	}
	extract_token(encoding, SegmentStart, 2, '?', sizeof encoding);
	StrBufExtract_token(ConvertBuf, &StaticBuf, 3, '?');
	
	*encoding = toupper(*encoding);
	if (*encoding == 'B') {	/**< base64 */
		if (ConvertBuf2->BufSize < ConvertBuf->BufUsed)
			IncreaseBuf(ConvertBuf2, 0, ConvertBuf->BufUsed);
		ConvertBuf2->BufUsed = CtdlDecodeBase64(ConvertBuf2->buf, 
							ConvertBuf->buf, 
							ConvertBuf->BufUsed);
	}
	else if (*encoding == 'Q') {	/**< quoted-printable */
		long pos;
		
		pos = 0;
		while (pos < ConvertBuf->BufUsed)
		{
			if (ConvertBuf->buf[pos] == '_') 
				ConvertBuf->buf[pos] = ' ';
			pos++;
		}
		
		if (ConvertBuf2->BufSize < ConvertBuf->BufUsed)
			IncreaseBuf(ConvertBuf2, 0, ConvertBuf->BufUsed);

		ConvertBuf2->BufUsed = CtdlDecodeQuotedPrintable(
			ConvertBuf2->buf, 
			ConvertBuf->buf,
			ConvertBuf->BufUsed);
	}
	else {
		StrBufAppendBuf(ConvertBuf2, ConvertBuf, 0);
	}
#ifdef HAVE_ICONV
	ctdl_iconv_open("UTF-8", charset, &ic);
	if (ic != (iconv_t)(-1) ) {		
#endif
		StrBufConvert(ConvertBuf2, ConvertBuf, &ic);
		StrBufAppendBuf(Target, ConvertBuf2, 0);
#ifdef HAVE_ICONV
		iconv_close(ic);
	}
	else {
		StrBufAppendBufPlain(Target, HKEY("(unreadable)"), 0);
	}
#endif
}

/**
 *  Handle subjects with RFC2047 encoding such as: [deprecated old syntax!]
 * =?koi8-r?B?78bP0s3Mxc7JxSDXz9rE1dvO2c3JINvB0sHNySDP?=
 *  Target where to put the decoded string to 
 *  DecodeMe buffer with encoded string
 *  DefaultCharset if we don't find one, which should we use?
 *  FoundCharset overrides DefaultCharset if non-empty; If we find a charset inside of the string, 
 *        put it here for later use where no string might be known.
 */
void StrBuf_RFC822_to_Utf8(StrBuf *Target, const StrBuf *DecodeMe, const StrBuf* DefaultCharset, StrBuf *FoundCharset)
{
	StrBuf *ConvertBuf;
	StrBuf *ConvertBuf2;
	ConvertBuf = NewStrBufPlain(NULL, StrLength(DecodeMe));
	ConvertBuf2 = NewStrBufPlain(NULL, StrLength(DecodeMe));
	
	StrBuf_RFC822_2_Utf8(Target, 
			     DecodeMe, 
			     DefaultCharset, 
			     FoundCharset, 
			     ConvertBuf, 
			     ConvertBuf2);
	FreeStrBuf(&ConvertBuf);
	FreeStrBuf(&ConvertBuf2);
}

/**
 *  Handle subjects with RFC2047 encoding such as:
 * =?koi8-r?B?78bP0s3Mxc7JxSDXz9rE1dvO2c3JINvB0sHNySDP?=
 *  Target where to put the decoded string to 
 *  DecodeMe buffer with encoded string
 *  DefaultCharset if we don't find one, which should we use?
 *  FoundCharset overrides DefaultCharset if non-empty; If we find a charset inside of the string, 
 *        put it here for later use where no string might be known.
 *  ConvertBuf workbuffer. feed in, you shouldn't care about its content.
 *  ConvertBuf2 workbuffer. feed in, you shouldn't care about its content.
 */
void StrBuf_RFC822_2_Utf8(StrBuf *Target, 
			  const StrBuf *DecodeMe, 
			  const StrBuf* DefaultCharset, 
			  StrBuf *FoundCharset, 
			  StrBuf *ConvertBuf, 
			  StrBuf *ConvertBuf2)
{
	StrBuf *DecodedInvalidBuf = NULL;
	const StrBuf *DecodeMee = DecodeMe;
	const char *start, *end, *next, *nextend, *ptr = NULL;
#ifdef HAVE_ICONV
	iconv_t ic = (iconv_t)(-1) ;
#endif
	const char *eptr;
	int passes = 0;
	int i;
	int illegal_non_rfc2047_encoding = 0;


	if (DecodeMe == NULL)
		return;
	/* Sometimes, badly formed messages contain strings which were simply
	 *  written out directly in some foreign character set instead of
	 *  using RFC2047 encoding.  This is illegal but we will attempt to
	 *  handle it anyway by converting from a user-specified default
	 *  charset to UTF-8 if we see any nonprintable characters.
	 */
	
	for (i=0; i<DecodeMe->BufUsed; ++i) {
		if ((DecodeMe->buf[i] < 32) || (DecodeMe->buf[i] > 126)) {
			illegal_non_rfc2047_encoding = 1;
			break;
		}
	}

	if ((illegal_non_rfc2047_encoding) &&
	    (strcasecmp(ChrPtr(DefaultCharset), "UTF-8")) && 
	    (strcasecmp(ChrPtr(DefaultCharset), "us-ascii")) )
	{
#ifdef HAVE_ICONV
		ctdl_iconv_open("UTF-8", ChrPtr(DefaultCharset), &ic);
		if (ic != (iconv_t)(-1) ) {
			DecodedInvalidBuf = NewStrBufDup(DecodeMe);
			StrBufConvert(DecodedInvalidBuf, ConvertBuf, &ic);///TODO: don't void const?
			DecodeMee = DecodedInvalidBuf;
			iconv_close(ic);
		}
#endif
	}

	/* pre evaluate the first pair */
	end = NULL;
	start = strstr(DecodeMee->buf, "=?");
	eptr = DecodeMee->buf + DecodeMee->BufUsed;
	if (start != NULL) 
		end = FindNextEnd (DecodeMee, start + 2);
	else {
		StrBufAppendBuf(Target, DecodeMee, 0);
		FreeStrBuf(&DecodedInvalidBuf);
		return;
	}


	if (start != DecodeMee->buf) {
		long nFront;
		
		nFront = start - DecodeMee->buf;
		StrBufAppendBufPlain(Target, DecodeMee->buf, nFront, 0);
	}
	/*
	 * Since spammers will go to all sorts of absurd lengths to get their
	 * messages through, there are LOTS of corrupt headers out there.
	 * So, prevent a really badly formed RFC2047 header from throwing
	 * this function into an infinite loop.
	 */
	while ((start != NULL) && 
	       (end != NULL) && 
	       (start < eptr) && 
	       (end < eptr) && 
	       (passes < 20))
	{
		passes++;
		DecodeSegment(Target, 
			      DecodeMee, 
			      start, 
			      end, 
			      ConvertBuf,
			      ConvertBuf2,
			      FoundCharset);
		
		next = strstr(end, "=?");
		nextend = NULL;
		if ((next != NULL) && 
		    (next < eptr))
			nextend = FindNextEnd(DecodeMee, next);
		if (nextend == NULL)
			next = NULL;

		/* did we find two partitions */
		if ((next != NULL) && 
		    ((next - end) > 2))
		{
			ptr = end + 2;
			while ((ptr < next) && 
			       (isspace(*ptr) ||
				(*ptr == '\r') ||
				(*ptr == '\n') || 
				(*ptr == '\t')))
				ptr ++;
			/* 
			 * did we find a gab just filled with blanks?
			 * if not, copy its stuff over.
			 */
			if (ptr != next)
			{
				StrBufAppendBufPlain(Target, 
						     end + 2, 
						     next - end - 2,
						     0);
			}
		}
		/* our next-pair is our new first pair now. */
		ptr = end + 2;
		start = next;
		end = nextend;
	}
	end = ptr;
	nextend = DecodeMee->buf + DecodeMee->BufUsed;
	if ((end != NULL) && (end < nextend)) {
		ptr = end;
		while ( (ptr < nextend) &&
			(isspace(*ptr) ||
			 (*ptr == '\r') ||
			 (*ptr == '\n') || 
			 (*ptr == '\t')))
			ptr ++;
		if (ptr < nextend)
			StrBufAppendBufPlain(Target, end, nextend - end, 0);
	}
	FreeStrBuf(&DecodedInvalidBuf);
}

/*******************************************************************************
 *                   Manipulating UTF-8 Strings                                *
 *******************************************************************************/

/**
 *  evaluate the length of an utf8 special character sequence
 *  Char the character to examine
 * @returns width of utf8 chars in bytes; if the sequence is broken 0 is returned; 1 if its simply ASCII.
 */
static inline int Ctdl_GetUtf8SequenceLength(const char *CharS, const char *CharE)
{
	int n = 0;
        unsigned char test = (1<<7);

	if ((*CharS & 0xC0) != 0xC0) 
		return 1;

	while ((n < 8) && 
	       ((test & ((unsigned char)*CharS)) != 0)) 
	{
		test = test >> 1;
		n ++;
	}
	if ((n > 6) || ((CharE - CharS) < n))
		n = 0;
	return n;
}

/**
 *  detect whether this char starts an utf-8 encoded char
 *  Char character to inspect
 * @returns yes or no
 */
static inline int Ctdl_IsUtf8SequenceStart(const char Char)
{
/** 11??.???? indicates an UTF8 Sequence. */
	return ((Char & 0xC0) == 0xC0);
}

/**
 *  measure the number of glyphs in an UTF8 string...
 *  Buf string to measure
 * @returns the number of glyphs in Buf
 */
long StrBuf_Utf8StrLen(StrBuf *Buf)
{
	int n = 0;
	int m = 0;
	char *aptr, *eptr;

	if ((Buf == NULL) || (Buf->BufUsed == 0))
		return 0;
	aptr = Buf->buf;
	eptr = Buf->buf + Buf->BufUsed;
	while ((aptr < eptr) && (*aptr != '\0')) {
		if (Ctdl_IsUtf8SequenceStart(*aptr)){
			m = Ctdl_GetUtf8SequenceLength(aptr, eptr);
			while ((aptr < eptr) && (*aptr++ != '\0')&& (m-- > 0) );
			n ++;
		}
		else {
			n++;
			aptr++;
		}
	}
	return n;
}

/**
 *  cuts a string after maxlen glyphs
 *  Buf string to cut to maxlen glyphs
 *  maxlen how long may the string become?
 * @returns current length of the string
 */
long StrBuf_Utf8StrCut(StrBuf *Buf, int maxlen)
{
	char *aptr, *eptr;
	int n = 0, m = 0;

	aptr = Buf->buf;
	eptr = Buf->buf + Buf->BufUsed;
	while ((aptr < eptr) && (*aptr != '\0')) {
		if (Ctdl_IsUtf8SequenceStart(*aptr)){
			m = Ctdl_GetUtf8SequenceLength(aptr, eptr);
			while ((*aptr++ != '\0') && (m-- > 0));
			n ++;
		}
		else {
			n++;
			aptr++;
		}
		if (n >= maxlen) {
			*aptr = '\0';
			Buf->BufUsed = aptr - Buf->buf;
			return Buf->BufUsed;
		}
	}
	return Buf->BufUsed;

}





/*******************************************************************************
 *                               wrapping ZLib                                 *
 *******************************************************************************/

/**
 *  uses the same calling syntax as compress2(), but it
 *   creates a stream compatible with HTTP "Content-encoding: gzip"
 *  dest compressed buffer
 *  destLen length of the compresed data 
 *  source source to encode
 *  sourceLen length of source to encode 
 *  level compression level
 */
#ifdef HAVE_ZLIB
int ZEXPORT compress_gzip(Bytef * dest,
			  size_t * destLen,
			  const Bytef * source,
			  uLong sourceLen,     
			  int level)
{
	/* write gzip header */
	snprintf((char *) dest, *destLen, 
		 "%c%c%c%c%c%c%c%c%c%c",
		 gz_magic[0], gz_magic[1], Z_DEFLATED,
		 0 /*flags */ , 0, 0, 0, 0 /*time */ , 0 /* xflags */ ,
		 OS_CODE);

	/* normal deflate */
	z_stream stream;
	int err;
	stream.next_in = (Bytef *) source;
	stream.avail_in = (uInt) sourceLen;
	stream.next_out = dest + 10L;	// after header
	stream.avail_out = (uInt) * destLen;
	if ((uLong) stream.avail_out != *destLen)
		return Z_BUF_ERROR;

	stream.zalloc = (alloc_func) 0;
	stream.zfree = (free_func) 0;
	stream.opaque = (voidpf) 0;

	err = deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS,
			   DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		return err;

	err = deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out + 10L;

	/* write CRC and Length */
	uLong crc = crc32(0L, source, sourceLen);
	int n;
	for (n = 0; n < 4; ++n, ++*destLen) {
		dest[*destLen] = (int) (crc & 0xff);
		crc >>= 8;
	}
	uLong len = stream.total_in;
	for (n = 0; n < 4; ++n, ++*destLen) {
		dest[*destLen] = (int) (len & 0xff);
		len >>= 8;
	}
	err = deflateEnd(&stream);
	return err;
}
#endif


/**
 *  compress the buffer with gzip
 * Attention! If you feed this a Const String, you must maintain the uncompressed buffer yourself!
 *  Buf buffer whose content is to be gzipped
 */
int CompressBuffer(StrBuf *Buf)
{
#ifdef HAVE_ZLIB
	char *compressed_data = NULL;
	size_t compressed_len, bufsize;
	int i = 0;

	bufsize = compressed_len = Buf->BufUsed +  (Buf->BufUsed / 100) + 100;
	compressed_data = malloc(compressed_len);
	
	if (compressed_data == NULL)
		return -1;
	/* Flush some space after the used payload so valgrind shuts up... */
        while ((i < 10) && (Buf->BufUsed + i < Buf->BufSize))
		Buf->buf[Buf->BufUsed + i++] = '\0';
	if (compress_gzip((Bytef *) compressed_data,
			  &compressed_len,
			  (Bytef *) Buf->buf,
			  (uLongf) Buf->BufUsed, Z_BEST_SPEED) == Z_OK) {
		if (!Buf->ConstBuf)
			free(Buf->buf);
		Buf->buf = compressed_data;
		Buf->BufUsed = compressed_len;
		Buf->BufSize = bufsize;
		/* Flush some space after the used payload so valgrind shuts up... */
		i = 0;
		while ((i < 10) && (Buf->BufUsed + i < Buf->BufSize))
			Buf->buf[Buf->BufUsed + i++] = '\0';
		return 1;
	} else {
		free(compressed_data);
	}
#endif	/* HAVE_ZLIB */
	return 0;
}

/*******************************************************************************
 *           File I/O; Callbacks to libevent                                   *
 *******************************************************************************/

long StrBuf_read_one_chunk_callback (int fd, short event, IOBuffer *FB)
{
	long bufremain = 0;
	int n;
	
	if ((FB == NULL) || (FB->Buf == NULL))
		return -1;

	/*
	 * check whether the read pointer is somewhere in a range 
	 * where a cut left is inexpensive
	 */

	if (FB->ReadWritePointer != NULL)
	{
		long already_read;
		
		already_read = FB->ReadWritePointer - FB->Buf->buf;
		bufremain = FB->Buf->BufSize - FB->Buf->BufUsed - 1;

		if (already_read != 0) {
			long unread;
			
			unread = FB->Buf->BufUsed - already_read;

			/* else nothing to compact... */
			if (unread == 0) {
				FB->ReadWritePointer = FB->Buf->buf;
				bufremain = FB->Buf->BufSize;			
			}
			else if ((unread < 64) || 
				 (bufremain < already_read))
			{
				/* 
				 * if its just a tiny bit remaining, or we run out of space... 
				 * lets tidy up.
				 */
				FB->Buf->BufUsed = unread;
				if (unread < already_read)
					memcpy(FB->Buf->buf, FB->ReadWritePointer, unread);
				else
					memmove(FB->Buf->buf, FB->ReadWritePointer, unread);
				FB->ReadWritePointer = FB->Buf->buf;
				bufremain = FB->Buf->BufSize - unread - 1;
			}
			else if (bufremain < (FB->Buf->BufSize / 10))
			{
				/* get a bigger buffer */ 

				IncreaseBuf(FB->Buf, 0, FB->Buf->BufUsed + 1);

				FB->ReadWritePointer = FB->Buf->buf + unread;

				bufremain = FB->Buf->BufSize - unread - 1;
/*TODO: special increase function that won't copy the already read! */
			}
		}
		else if (bufremain < 10) {
			IncreaseBuf(FB->Buf, 1, FB->Buf->BufUsed + 10);
			
			FB->ReadWritePointer = FB->Buf->buf;
			
			bufremain = FB->Buf->BufSize - FB->Buf->BufUsed - 1;
		}
		
	}
	else {
		FB->ReadWritePointer = FB->Buf->buf;
		bufremain = FB->Buf->BufSize - 1;
	}

	n = read(fd, FB->Buf->buf + FB->Buf->BufUsed, bufremain);

	if (n > 0) {
		FB->Buf->BufUsed += n;
		FB->Buf->buf[FB->Buf->BufUsed] = '\0';
	}
	return n;
}

int StrBuf_write_one_chunk_callback(int fd, short event, IOBuffer *FB)
{
	long WriteRemain;
	int n;

	if ((FB == NULL) || (FB->Buf == NULL))
		return -1;

	if (FB->ReadWritePointer != NULL)
	{
		WriteRemain = FB->Buf->BufUsed - 
			(FB->ReadWritePointer - 
			 FB->Buf->buf);
	}
	else {
		FB->ReadWritePointer = FB->Buf->buf;
		WriteRemain = FB->Buf->BufUsed;
	}

	n = write(fd, FB->ReadWritePointer, WriteRemain);
	if (n > 0) {
		FB->ReadWritePointer += n;

		if (FB->ReadWritePointer == 
		    FB->Buf->buf + FB->Buf->BufUsed)
		{
			FlushStrBuf(FB->Buf);
			FB->ReadWritePointer = NULL;
			return 0;
		}
	// check whether we've got something to write
	// get the maximum chunk plus the pointer we can send
	// write whats there
	// if not all was sent, remember the send pointer for the next time
		return FB->ReadWritePointer - FB->Buf->buf + FB->Buf->BufUsed;
	}
	return n;
}

/**
 *  extract a "next line" from Buf; Ptr to persist across several iterations
 *  LineBuf your line will be copied here.
 *  FB BLOB with lines of text...
 *  Ptr moved arround to keep the next-line across several iterations
 *        has to be &NULL on start; will be &NotNULL on end of buffer
 * @returns size of copied buffer
 */
eReadState StrBufChunkSipLine(StrBuf *LineBuf, IOBuffer *FB)
{
	const char *aptr, *ptr, *eptr;
	char *optr, *xptr;

	if ((FB == NULL) || (LineBuf == NULL) || (LineBuf->buf == NULL))
		return eReadFail;
	

	if ((FB->Buf == NULL) || (FB->ReadWritePointer == StrBufNOTNULL)) {
		FB->ReadWritePointer = StrBufNOTNULL;
		return eReadFail;
	}

	FlushStrBuf(LineBuf);
	if (FB->ReadWritePointer == NULL)
		ptr = aptr = FB->Buf->buf;
	else
		ptr = aptr = FB->ReadWritePointer;

	optr = LineBuf->buf;
	eptr = FB->Buf->buf + FB->Buf->BufUsed;
	xptr = LineBuf->buf + LineBuf->BufSize - 1;

	while ((ptr <= eptr) && 
	       (*ptr != '\n') &&
	       (*ptr != '\r') )
	{
		*optr = *ptr;
		optr++; ptr++;
		if (optr == xptr) {
			LineBuf->BufUsed = optr - LineBuf->buf;
			IncreaseBuf(LineBuf,  1, LineBuf->BufUsed + 1);
			optr = LineBuf->buf + LineBuf->BufUsed;
			xptr = LineBuf->buf + LineBuf->BufSize - 1;
		}
	}

	if (ptr >= eptr) {
		if (optr > LineBuf->buf)
			optr --;
		if ((*(ptr - 1) != '\r') && (*(ptr - 1) != '\n')) {
			LineBuf->BufUsed = optr - LineBuf->buf;
			*optr = '\0';
			if ((FB->ReadWritePointer != NULL) && 
			    (FB->ReadWritePointer != FB->Buf->buf))
			{
				/* Ok, the client application read all the data 
				   it was interested in so far. Since there is more to read, 
				   we now shrink the buffer, and move the rest over.
				*/
				StrBufCutLeft(FB->Buf, 
					      FB->ReadWritePointer - FB->Buf->buf);
				FB->ReadWritePointer = FB->Buf->buf;
			}
			return eMustReadMore;
		}
	}
	LineBuf->BufUsed = optr - LineBuf->buf;
	*optr = '\0';       
	if ((ptr <= eptr) && (*ptr == '\r'))
		ptr ++;
	if ((ptr <= eptr) && (*ptr == '\n'))
		ptr ++;
	
	if (ptr < eptr) {
		FB->ReadWritePointer = ptr;
	}
	else {
		FlushStrBuf(FB->Buf);
		FB->ReadWritePointer = NULL;
	}

	return eReadSuccess;
}

/**
 *  check whether the chunk-buffer has more data waiting or not.
 *  FB Chunk-Buffer to inspect
 */
eReadState StrBufCheckBuffer(IOBuffer *FB)
{
	if (FB == NULL)
		return eReadFail;
	if (FB->Buf->BufUsed == 0)
		return eReadSuccess;
	if (FB->ReadWritePointer == NULL)
		return eBufferNotEmpty;
	if (FB->Buf->buf + FB->Buf->BufUsed > FB->ReadWritePointer)
		return eBufferNotEmpty;
	return eReadSuccess;
}

long IOBufferStrLength(IOBuffer *FB)
{
	if ((FB == NULL) || (FB->Buf == NULL))
		return 0;
	if (FB->ReadWritePointer == NULL)
		return StrLength(FB->Buf);
	
	return StrLength(FB->Buf) - (FB->ReadWritePointer - FB->Buf->buf);
}


/*******************************************************************************
 *           File I/O; Prefer buffered read since its faster!                  *
 *******************************************************************************/

/**
 *  Read a line from socket
 * flushes and closes the FD on error
 *  buf the buffer to get the input to
 *  fd pointer to the filedescriptor to read
 *  append Append to an existing string or replace?
 *  Error strerror() on error 
 * @returns numbers of chars read
 */
int StrBufTCP_read_line(StrBuf *buf, int *fd, int append, const char **Error)
{
	int len, rlen, slen;

	if ((buf == NULL) || (buf->buf == NULL)) {
		*Error = strerror(EINVAL);
		return -1;
	}

	if (!append)
		FlushStrBuf(buf);

	slen = len = buf->BufUsed;
	while (1) {
		rlen = read(*fd, &buf->buf[len], 1);
		if (rlen < 1) {
			*Error = strerror(errno);
			
			close(*fd);
			*fd = -1;
			
			return -1;
		}
		if (buf->buf[len] == '\n')
			break;
		if (buf->buf[len] != '\r')
			len ++;
		if (len + 2 >= buf->BufSize) {
			buf->BufUsed = len;
			buf->buf[len+1] = '\0';
			IncreaseBuf(buf, 1, -1);
		}
	}
	buf->BufUsed = len;
	buf->buf[len] = '\0';
	return len - slen;
}


/**
 *  Read a line from socket
 * flushes and closes the FD on error
 *  Line the line to read from the fd / I/O Buffer
 *  buf the buffer to get the input to
 *  fd pointer to the filedescriptor to read
 *  timeout number of successless selects until we bail out
 *  selectresolution how long to wait on each select
 *  Error strerror() on error 
 * @returns numbers of chars read
 */
int StrBufTCP_read_buffered_line(StrBuf *Line, 
				 StrBuf *buf, 
				 int *fd, 
				 int timeout, 
				 int selectresolution, 
				 const char **Error)
{
	int len, rlen;
	int nSuccessLess = 0;
	fd_set rfds;
	char *pch = NULL;
        int fdflags;
	int IsNonBlock;
	struct timeval tv;

	if (buf->BufUsed > 0) {
		pch = strchr(buf->buf, '\n');
		if (pch != NULL) {
			rlen = 0;
			len = pch - buf->buf;
			if (len > 0 && (*(pch - 1) == '\r') )
				rlen ++;
			StrBufSub(Line, buf, 0, len - rlen);
			StrBufCutLeft(buf, len + 1);
			return len - rlen;
		}
	}
	
	if (buf->BufSize - buf->BufUsed < 10)
		IncreaseBuf(buf, 1, -1);

	fdflags = fcntl(*fd, F_GETFL);
	IsNonBlock = (fdflags & O_NONBLOCK) == O_NONBLOCK;

	while ((nSuccessLess < timeout) && (pch == NULL)) {
		if (IsNonBlock){
			tv.tv_sec = selectresolution;
			tv.tv_usec = 0;
			
			FD_ZERO(&rfds);
			FD_SET(*fd, &rfds);
			if (select(*fd + 1, NULL, &rfds, NULL, &tv) == -1) {
				*Error = strerror(errno);
				close (*fd);
				*fd = -1;
				return -1;
			}
		}
		if (IsNonBlock && !  FD_ISSET(*fd, &rfds)) {
			nSuccessLess ++;
			continue;
		}
		rlen = read(*fd, 
			    &buf->buf[buf->BufUsed], 
			    buf->BufSize - buf->BufUsed - 1);
		if (rlen < 1) {
			*Error = strerror(errno);
			close(*fd);
			*fd = -1;
			return -1;
		}
		else if (rlen > 0) {
			nSuccessLess = 0;
			buf->BufUsed += rlen;
			buf->buf[buf->BufUsed] = '\0';
			pch = strchr(buf->buf, '\n');
			if ((pch == NULL) &&
			    (buf->BufUsed + 10 > buf->BufSize) &&
			    (IncreaseBuf(buf, 1, -1) == -1))
				return -1;
			continue;
		}
		
	}
	if (pch != NULL) {
		rlen = 0;
		len = pch - buf->buf;
		if (len > 0 && (*(pch - 1) == '\r') )
			rlen ++;
		StrBufSub(Line, buf, 0, len - rlen);
		StrBufCutLeft(buf, len + 1);
		return len - rlen;
	}
	return -1;

}

static const char *ErrRBLF_PreConditionFailed="StrBufTCP_read_buffered_line_fast: Wrong arguments or invalid Filedescriptor";
static const char *ErrRBLF_SelectFailed="StrBufTCP_read_buffered_line_fast: Select failed without reason";
static const char *ErrRBLF_NotEnoughSentFromServer="StrBufTCP_read_buffered_line_fast: No complete line was sent from peer";
/**
 *  Read a line from socket
 * flushes and closes the FD on error
 *  Line where to append our Line read from the fd / I/O Buffer; 
 *  IOBuf the buffer to get the input to; lifetime pair to FD
 *  Pos pointer to the current read position, should be NULL initialized on opening the FD it belongs to.!
 *  fd pointer to the filedescriptor to read
 *  timeout number of successless selects until we bail out
 *  selectresolution how long to wait on each select
 *  Error strerror() on error 
 * @returns numbers of chars read or -1 in case of error. "\n" will become 0
 */
int StrBufTCP_read_buffered_line_fast(StrBuf *Line, 
				      StrBuf *IOBuf, 
				      const char **Pos,
				      int *fd, 
				      int timeout, 
				      int selectresolution, 
				      const char **Error)
{
	const char *pche = NULL;
	const char *pos = NULL;
	const char *pLF;
	int len, rlen, retlen;
	int nSuccessLess = 0;
	fd_set rfds;
	const char *pch = NULL;
        int fdflags;
	int IsNonBlock;
	struct timeval tv;
	
	retlen = 0;
	if ((Line == NULL) ||
	    (Pos == NULL) ||
	    (IOBuf == NULL) ||
	    (*fd == -1))
	{
		if (Pos != NULL)
			*Pos = NULL;
		*Error = ErrRBLF_PreConditionFailed;
		return -1;
	}

	pos = *Pos;
	if ((IOBuf->BufUsed > 0) && 
	    (pos != NULL) && 
	    (pos < IOBuf->buf + IOBuf->BufUsed)) 
	{
		char *pcht;

		pche = IOBuf->buf + IOBuf->BufUsed;
		pch = pos;
		pcht = Line->buf;

		while ((pch < pche) && (*pch != '\n'))
		{
			if (Line->BufUsed + 10 > Line->BufSize)
			{
				long apos;
				apos = pcht - Line->buf;
				*pcht = '\0';
				IncreaseBuf(Line, 1, -1);
				pcht = Line->buf + apos;
			}
			*pcht++ = *pch++;
			Line->BufUsed++;
			retlen++;
		}

		len = pch - pos;
		if (len > 0 && (*(pch - 1) == '\r') )
		{
			retlen--;
			len --;
			pcht --;
			Line->BufUsed --;
		}
		*pcht = '\0';

		if ((pch >= pche) || (*pch == '\0'))
		{
			FlushStrBuf(IOBuf);
			*Pos = NULL;
			pch = NULL;
			pos = 0;
		}

		if ((pch != NULL) && 
		    (pch <= pche)) 
		{
			if (pch + 1 >= pche) {
				*Pos = NULL;
				FlushStrBuf(IOBuf);
			}
			else
				*Pos = pch + 1;
			
			return retlen;
		}
		else 
			FlushStrBuf(IOBuf);
	}

	/* If we come here, Pos is Unset since we read everything into Line, and now go for more. */
	
	if (IOBuf->BufSize - IOBuf->BufUsed < 10)
		IncreaseBuf(IOBuf, 1, -1);

	fdflags = fcntl(*fd, F_GETFL);
	IsNonBlock = (fdflags & O_NONBLOCK) == O_NONBLOCK;

	pLF = NULL;
	while ((nSuccessLess < timeout) && 
	       (pLF == NULL) &&
	       (*fd != -1)) {
		if (IsNonBlock)
		{
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		
			FD_ZERO(&rfds);
			FD_SET(*fd, &rfds);
			if (select((*fd) + 1, &rfds, NULL, NULL, &tv) == -1) {
				*Error = strerror(errno);
				close (*fd);
				*fd = -1;
				if (*Error == NULL)
					*Error = ErrRBLF_SelectFailed;
				return -1;
			}
			if (! FD_ISSET(*fd, &rfds) != 0) {
				nSuccessLess ++;
				continue;
			}
		}
		rlen = read(*fd, 
			    &IOBuf->buf[IOBuf->BufUsed], 
			    IOBuf->BufSize - IOBuf->BufUsed - 1);
		if (rlen < 1) {
			*Error = strerror(errno);
			close(*fd);
			*fd = -1;
			return -1;
		}
		else if (rlen > 0) {
			nSuccessLess = 0;
			pLF = IOBuf->buf + IOBuf->BufUsed;
			IOBuf->BufUsed += rlen;
			IOBuf->buf[IOBuf->BufUsed] = '\0';
			
			pche = IOBuf->buf + IOBuf->BufUsed;
			
			while ((pLF < pche) && (*pLF != '\n'))
				pLF ++;
			if ((pLF >= pche) || (*pLF == '\0'))
				pLF = NULL;

			if (IOBuf->BufUsed + 10 > IOBuf->BufSize)
			{
				long apos = 0;

				if (pLF != NULL) apos = pLF - IOBuf->buf;
				IncreaseBuf(IOBuf, 1, -1);	
				if (pLF != NULL) pLF = IOBuf->buf + apos;
			}

			continue;
		}
		else
		{
			nSuccessLess++;
		}
	}
	*Pos = NULL;
	if (pLF != NULL) {
		pos = IOBuf->buf;
		len = pLF - pos;
		if (len > 0 && (*(pLF - 1) == '\r') )
			len --;
		StrBufAppendBufPlain(Line, ChrPtr(IOBuf), len, 0);
		if (pLF + 1 >= IOBuf->buf + IOBuf->BufUsed)
		{
			FlushStrBuf(IOBuf);
		}
		else 
			*Pos = pLF + 1;
		return retlen + len;
	}
	*Error = ErrRBLF_NotEnoughSentFromServer;
	return -1;

}

static const char *ErrRBLF_BLOBPreConditionFailed="StrBufReadBLOB: Wrong arguments or invalid Filedescriptor";
/**
 *  Input binary data from socket
 * flushes and closes the FD on error
 *  Buf the buffer to get the input to
 *  fd pointer to the filedescriptor to read
 *  append Append to an existing string or replace?
 *  nBytes the maximal number of bytes to read
 *  Error strerror() on error 
 * @returns numbers of chars read
 */
int StrBufReadBLOB(StrBuf *Buf, int *fd, int append, long nBytes, const char **Error)
{
	int fdflags;
	int rlen;
	int nSuccessLess;
	int nRead = 0;
	char *ptr;
	int IsNonBlock;
	struct timeval tv;
	fd_set rfds;

	if ((Buf == NULL) || (Buf->buf == NULL) || (*fd == -1))
	{
		*Error = ErrRBLF_BLOBPreConditionFailed;
		return -1;
	}
	if (!append)
		FlushStrBuf(Buf);
	if (Buf->BufUsed + nBytes >= Buf->BufSize)
		IncreaseBuf(Buf, 1, Buf->BufUsed + nBytes);

	ptr = Buf->buf + Buf->BufUsed;

	fdflags = fcntl(*fd, F_GETFL);
	IsNonBlock = (fdflags & O_NONBLOCK) == O_NONBLOCK;
	nSuccessLess = 0;
	while ((nRead < nBytes) && 
	       (*fd != -1)) 
	{
		if (IsNonBlock)
		{
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		
			FD_ZERO(&rfds);
			FD_SET(*fd, &rfds);
			if (select(*fd + 1, &rfds, NULL, NULL, &tv) == -1) {
				*Error = strerror(errno);
				close (*fd);
				*fd = -1;
				if (*Error == NULL)
					*Error = ErrRBLF_SelectFailed;
				return -1;
			}
			if (! FD_ISSET(*fd, &rfds) != 0) {
				nSuccessLess ++;
				continue;
			}
		}

                if ((rlen = read(*fd, 
				 ptr,
				 nBytes - nRead)) == -1) {
			close(*fd);
			*fd = -1;
			*Error = strerror(errno);
                        return rlen;
                }
		nRead += rlen;
		ptr += rlen;
		Buf->BufUsed += rlen;
	}
	Buf->buf[Buf->BufUsed] = '\0';
	return nRead;
}

const char *ErrRBB_BLOBFPreConditionFailed = "StrBufReadBLOBBuffered: to many selects; aborting.";
const char *ErrRBB_too_many_selects        = "StrBufReadBLOBBuffered: to many selects; aborting.";
/**
 *  Input binary data from socket
 * flushes and closes the FD on error
 *  Blob put binary thing here
 *  IOBuf the buffer to get the input to
 *  Pos offset inside of IOBuf
 *  fd pointer to the filedescriptor to read
 *  append Append to an existing string or replace?
 *  nBytes the maximal number of bytes to read
 *  check whether we should search for '000\n' terminators in case of timeouts
 *  Error strerror() on error 
 * @returns numbers of chars read
 */
int StrBufReadBLOBBuffered(StrBuf *Blob, 
			   StrBuf *IOBuf, 
			   const char **Pos,
			   int *fd, 
			   int append, 
			   long nBytes, 
			   int check, 
			   const char **Error)
{
	const char *pos;
	int fdflags;
	int rlen = 0;
	int nRead = 0;
	int nAlreadyRead = 0;
	int IsNonBlock;
	char *ptr;
	fd_set rfds;
	struct timeval tv;
	int nSuccessLess = 0;
	int MaxTries;

	if ((Blob == NULL) || (*fd == -1) || (IOBuf == NULL) || (Pos == NULL)) {
		if (Pos != NULL)
			*Pos = NULL;
		*Error = ErrRBB_BLOBFPreConditionFailed;
		return -1;
	}

	if (!append)
		FlushStrBuf(Blob);
	if (Blob->BufUsed + nBytes >= Blob->BufSize) 
		IncreaseBuf(Blob, append, Blob->BufUsed + nBytes);
	
	pos = *Pos;

	if (pos != NULL) {
		rlen = pos - IOBuf->buf;
	}
	rlen = IOBuf->BufUsed - rlen;


	if ((IOBuf->BufUsed > 0) && (pos != NULL) && (pos < IOBuf->buf + IOBuf->BufUsed)) {
		if (rlen < nBytes) {
			memcpy(Blob->buf + Blob->BufUsed, pos, rlen);
			Blob->BufUsed += rlen;
			Blob->buf[Blob->BufUsed] = '\0';
			nAlreadyRead = nRead = rlen;
			*Pos = NULL; 
		}
		if (rlen >= nBytes) {
			memcpy(Blob->buf + Blob->BufUsed, pos, nBytes);
			Blob->BufUsed += nBytes;
			Blob->buf[Blob->BufUsed] = '\0';
			if (rlen == nBytes) {
				*Pos = NULL; 
				FlushStrBuf(IOBuf);
			}
			else 
				*Pos += nBytes;
			return nBytes;
		}
	}

	FlushStrBuf(IOBuf);
	*Pos = NULL;
	if (IOBuf->BufSize < nBytes - nRead) {
		IncreaseBuf(IOBuf, 0, nBytes - nRead);
	}
	ptr = IOBuf->buf;

	fdflags = fcntl(*fd, F_GETFL);
	IsNonBlock = (fdflags & O_NONBLOCK) == O_NONBLOCK;
	if (IsNonBlock)
		MaxTries =   1000;
	else
		MaxTries = 100000;

	nBytes -= nRead;
	nRead = 0;
	while ((nSuccessLess < MaxTries) && (nRead < nBytes) && (*fd != -1)) {
		if (IsNonBlock) {
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		
			FD_ZERO(&rfds);
			FD_SET(*fd, &rfds);
			if (select(*fd + 1, &rfds, NULL, NULL, &tv) == -1) {
				*Error = strerror(errno);
				close (*fd);
				*fd = -1;
				if (*Error == NULL) {
					*Error = ErrRBLF_SelectFailed;
				}
				return -1;
			}
			if (! FD_ISSET(*fd, &rfds) != 0) {
				nSuccessLess ++;
				continue;
			}
		}
		rlen = read(*fd, ptr, IOBuf->BufSize - (ptr - IOBuf->buf));
		if (rlen < 1) {			// We will always get at least 1 byte unless the connection is broken
			close(*fd);
			*fd = -1;
			*Error = strerror(errno);
			return rlen;
		}
		else if (rlen == 0){
			if ((check == NNN_TERM) && (nRead > 5) && (strncmp(IOBuf->buf + IOBuf->BufUsed - 5, "\n000\n", 5) == 0)) {
				StrBufPlain(Blob, HKEY("\n000\n"));
				StrBufCutRight(Blob, 5);
				return Blob->BufUsed;
			}
			else if (!IsNonBlock) 
				nSuccessLess ++;
			else if (nSuccessLess > MaxTries) {
				FlushStrBuf(IOBuf);
				*Error = ErrRBB_too_many_selects;
				return -1;
			}
		}
		else if (rlen > 0) {
			nSuccessLess = 0;
			nRead += rlen;
			ptr += rlen;
			IOBuf->BufUsed += rlen;
		}
	}
	if (nSuccessLess >= MaxTries) {
		FlushStrBuf(IOBuf);
		*Error = ErrRBB_too_many_selects;
		return -1;
	}

	if (nRead > nBytes) {
		*Pos = IOBuf->buf + nBytes;
	}
	Blob->buf[Blob->BufUsed] = '\0';
	StrBufAppendBufPlain(Blob, IOBuf->buf, nBytes, 0);
	if (*Pos == NULL) {
		FlushStrBuf(IOBuf);
	}
	return nRead + nAlreadyRead;
}


// extract a "next line" from Buf; Ptr to persist across several iterations
// LineBuf your line will be copied here.
// Buf BLOB with lines of text...
// Ptr moved arround to keep the next-line across several iterations
//     has to be &NULL on start; will be &NotNULL on end of buffer
// returns size of remaining buffer
int StrBufSipLine(StrBuf *LineBuf, const StrBuf *Buf, const char **Ptr) {
	const char *aptr, *ptr, *eptr;
	char *optr, *xptr;

	if ((Buf == NULL) || (*Ptr == StrBufNOTNULL) || (LineBuf == NULL)|| (LineBuf->buf == NULL)) {
		*Ptr = StrBufNOTNULL;
		return 0;
	}

	FlushStrBuf(LineBuf);
	if (*Ptr==NULL)
		ptr = aptr = Buf->buf;
	else
		ptr = aptr = *Ptr;

	optr = LineBuf->buf;
	eptr = Buf->buf + Buf->BufUsed;
	xptr = LineBuf->buf + LineBuf->BufSize - 1;

	while ((ptr <= eptr) && (*ptr != '\n') && (*ptr != '\r') ) {
		*optr = *ptr;
		optr++; ptr++;
		if (optr == xptr) {
			LineBuf->BufUsed = optr - LineBuf->buf;
			IncreaseBuf(LineBuf,  1, LineBuf->BufUsed + 1);
			optr = LineBuf->buf + LineBuf->BufUsed;
			xptr = LineBuf->buf + LineBuf->BufSize - 1;
		}
	}

	if ((ptr >= eptr) && (optr > LineBuf->buf))
		optr --;
	LineBuf->BufUsed = optr - LineBuf->buf;
	*optr = '\0';       
	if ((ptr <= eptr) && (*ptr == '\r'))
		ptr ++;
	if ((ptr <= eptr) && (*ptr == '\n'))
		ptr ++;
	
	if (ptr < eptr) {
		*Ptr = ptr;
	}
	else {
		*Ptr = StrBufNOTNULL;
	}

	return Buf->BufUsed - (ptr - Buf->buf);
}


// removes double slashes from pathnames
// Dir directory string to filter
// RemoveTrailingSlash allows / disallows trailing slashes
void StrBufStripSlashes(StrBuf *Dir, int RemoveTrailingSlash) {
	char *a, *b;

	a = b = Dir->buf;

	while (!IsEmptyStr(a)) {
		if (*a == '/') {
			while (*a == '/')
				a++;
			*b = '/';
			b++;
		}
		else {
			*b = *a;
			b++; a++;
		}
	}
	if ((RemoveTrailingSlash) &&
	    (b > Dir->buf) && 
	    (*(b - 1) == '/')){
		b--;
	}
	*b = '\0';
	Dir->BufUsed = b - Dir->buf;
}


// Decode a quoted-printable encoded StrBuf buffer "in place"
// This is possible because the decoded will always be shorter than the encoded
// so we don't have to worry about the buffer being to small.
void StrBufDecodeQP(StrBuf *Buf) {
	if (!Buf) {				// sanity check #1
		return;
	}

	int source_len = StrLength(Buf);
	if (source_len < 1) {			// sanity check #2
		return;
	}

	int spos = 0;				// source position
	int tpos = 0;				// target position

	while (spos < source_len) {
		if (!strncmp(&Buf->buf[spos], "=\r\n", 3)) {
			spos += 3;
		}
		else if (!strncmp(&Buf->buf[spos], "=\n", 2)) {
			spos += 2;
		}
		else if (Buf->buf[spos] == '=') {
			++spos;
			int ch;
			sscanf(&Buf->buf[spos], "%02x", &ch);
			Buf->buf[tpos++] = ch;
			spos +=2;
		}
		else {
			Buf->buf[tpos++] = Buf->buf[spos++];
		}
	}

	Buf->buf[tpos] = 0;
	Buf->BufUsed = tpos;
}
