// Copyright (c) 1987-2022 by the citadel.org team
//
// Hashlist is a simple implementation of key value pairs. It doesn't implement collision handling.
// the hashing algorithm is pluggeable on creation. 
// items are added with a function pointer to a destructor; that way complex structures can be added.
// if no pointer is given, simply free is used. Use reference_free_handler if you don't want us to free your memory.
//
// This program is open source software.  Use, duplication, or disclosure
// is subject to the terms of the GNU General Public License, version 3.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include "libcitadel.h"
#include "lookup3.h"

typedef struct Payload Payload;


/*
 * Hash Payload storage Structure; filled in linear.
 */
struct Payload {
	void *Data; /**< the Data belonging to this storage */
	DeleteHashDataFunc Destructor; /**< if we want to destroy Data, do it with this function. */
};


/*
 * Hash key element; sorted by key
 */
struct HashKey {
	long Key;         /**< Numeric Hashkey comperator for hash sorting */
	long Position;    /**< Pointer to a Payload struct in the Payload Aray */
	char *HashKey;    /**< the Plaintext Hashkey */
	long HKLen;       /**< length of the Plaintext Hashkey */
	Payload *PL;      /**< pointer to our payload for sorting */
};

/*
 * Hash structure; holds arrays of Hashkey and Payload. 
 */
struct HashList {
	Payload **Members;     /**< Our Payload members. This fills up linear */
	HashKey **LookupTable; /**< Hash Lookup table. Elements point to members, and are sorted by their hashvalue */
	char **MyKeys;         /**< this keeps the members for a call of GetHashKeys */
	HashFunc Algorithm;    /**< should we use an alternating algorithm to calc the hash values? */
	long nMembersUsed;     /**< how many pointers inside of the array are used? */
	long nLookupTableItems; /**< how many items of the lookup table are used? */
	long MemberSize;       /**< how big is Members and LookupTable? */
	long tainted;          /**< if 0, we're hashed, else s.b. else sorted us in his own way. */
	long uniq;             /**< are the keys going to be uniq? */
};

/*
 * Anonymous Hash Iterator Object. used for traversing the whole array from outside 
 */
struct HashPos {
	long Position;        /**< Position inside of the hash */
	int StepWidth;        /**< small? big? forward? backward? */
};


/*
 * Iterate over the hash and call PrintEntry. 
 * Hash your Hashlist structure
 * Trans is called so you could for example print 'A:' if the next entries are like that.
 *        Must be aware to receive NULL in both pointers.
 * PrintEntry print entry one by one
 * returns the number of items printed
 */
int PrintHash(HashList *Hash, TransitionFunc Trans, PrintHashDataFunc PrintEntry) {
	int i;
	void *Previous;
	void *Next;
	const char* KeyStr;

	if (Hash == NULL)
		return 0;

	for (i=0; i < Hash->nLookupTableItems; i++) {
		if (i==0) {
			Previous = NULL;
		}
		else {
			if (Hash->LookupTable[i - 1] == NULL)
				Previous = NULL;
			else
				Previous = Hash->Members[Hash->LookupTable[i-1]->Position]->Data;
		}
		if (Hash->LookupTable[i] == NULL) {
			KeyStr = "";
			Next = NULL;
		}
		else {
			Next = Hash->Members[Hash->LookupTable[i]->Position]->Data;
			KeyStr = Hash->LookupTable[i]->HashKey;
		}

		Trans(Previous, Next, i % 2);
		PrintEntry(KeyStr, Next, i % 2);
	}
	return i;
}

const char *dbg_PrintStrBufPayload(const char *Key, void *Item, int Odd) {
	return ChrPtr((StrBuf*)Item);
}

/*
 * verify the contents of a hash list; here for debugging purposes.
 * Hash your Hashlist structure
 * First Functionpointer to allow you to print your payload
 * Second Functionpointer to allow you to print your payload
 * returns 0
 */
int dbg_PrintHash(HashList *Hash, PrintHashContent First, PrintHashContent Second) {
#ifdef DEBUG
	const char *foo;
	const char *bar;
	const char *bla = "";
	long key;
#endif
	long i;

	if (Hash == NULL)
		return 0;

	if (Hash->MyKeys != NULL)
		free (Hash->MyKeys);

	Hash->MyKeys = (char**) malloc(sizeof(char*) * Hash->nLookupTableItems);
#ifdef DEBUG
	printf("----------------------------------\n");
#endif
	for (i=0; i < Hash->nLookupTableItems; i++) {
		
		if (Hash->LookupTable[i] == NULL)
		{
#ifdef DEBUG
			foo = "";
			bar = "";
			key = 0;
#endif
		}
		else 
		{
#ifdef DEBUG
			key = Hash->LookupTable[i]->Key;
			foo = Hash->LookupTable[i]->HashKey;
#endif
			if (First != NULL)
#ifdef DEBUG
				bar =
#endif
					First(Hash->Members[Hash->LookupTable[i]->Position]->Data);
#ifdef DEBUG
			else 
				bar = "";
#endif

			if (Second != NULL)
#ifdef DEBUG
				bla = 
#endif 
					Second(Hash->Members[Hash->LookupTable[i]->Position]->Data);
#ifdef DEBUG

			else
				bla = "";
#endif

		}
#ifdef DEBUG
		if ((Hash->Algorithm == lFlathash) || (Hash->Algorithm == Flathash)) {
			printf (" ---- Hashkey[%ld][%ld]: %ld '%s' Value: '%s' ; %s\n", i, key, *(long*) foo, foo, bar, bla);
		}
		else {
			printf (" ---- Hashkey[%ld][%ld]: '%s' Value: '%s' ; %s\n", i, key, foo, bar, bla);
		}
#endif
	}
#ifdef DEBUG
	printf("----------------------------------\n");
#endif
	return 0;
}


int TestValidateHash(HashList *TestHash) {
	long i;

	if (TestHash->nMembersUsed != TestHash->nLookupTableItems)
		return 1;

	if (TestHash->nMembersUsed > TestHash->MemberSize)
		return 2;

	for (i=0; i < TestHash->nMembersUsed; i++) {

		if (TestHash->LookupTable[i]->Position > TestHash->nMembersUsed)
			return 3;
		
		if (TestHash->Members[TestHash->LookupTable[i]->Position] == NULL)
			return 4;
		if (TestHash->Members[TestHash->LookupTable[i]->Position]->Data == NULL)
			return 5;
	}
	return 0;
}

/*
 * instanciate a new hashlist
 * returns the newly allocated list. 
 */
HashList *NewHash(int Uniq, HashFunc F) {
	HashList *NewList;
	NewList = malloc (sizeof(HashList));
	if (NewList == NULL)
		return NULL;
	memset(NewList, 0, sizeof(HashList));

	NewList->Members = malloc(sizeof(Payload*) * 100);
	if (NewList->Members == NULL) {
		free(NewList);
		return NULL;
	}
	memset(NewList->Members, 0, sizeof(Payload*) * 100);

	NewList->LookupTable = malloc(sizeof(HashKey*) * 100);
	if (NewList->LookupTable == NULL) {
		free(NewList->Members);
		free(NewList);
		return NULL;
	}
	memset(NewList->LookupTable, 0, sizeof(HashKey*) * 100);

	NewList->MemberSize = 100;
	NewList->tainted = 0;
        NewList->uniq = Uniq;
	NewList->Algorithm = F;

	return NewList;
}

int GetCount(HashList *Hash) {
	if(Hash==NULL) return 0;
	return Hash->nLookupTableItems;
}


/*
 * private destructor for one hash element.
 * Crashing? go one frame up and do 'print *FreeMe->LookupTable[i]'
 * Data an element to free using the user provided destructor, or just plain free
 */
static void DeleteHashPayload (Payload *Data) {
	/** do we have a destructor for our payload? */
	if (Data->Destructor)
		Data->Destructor(Data->Data);
	else
		free(Data->Data);
}

/*
 * Destructor for nested hashes
 */
void HDeleteHash(void *vHash) {
	HashList *FreeMe = (HashList*)vHash;
	DeleteHash(&FreeMe);
}

/*
 * flush the members of a hashlist 
 * Crashing? do 'print *FreeMe->LookupTable[i]'
 * Hash Hash to destroy. Is NULL'ed so you are shure its done.
 */
void DeleteHashContent(HashList **Hash) {
	int i;
	HashList *FreeMe;

	FreeMe = *Hash;
	if (FreeMe == NULL)
		return;
	/* even if there are sparse members already deleted... */
	for (i=0; i < FreeMe->nMembersUsed; i++) {
		/** get rid of our payload */
		if (FreeMe->Members[i] != NULL) {
			DeleteHashPayload(FreeMe->Members[i]);
			free(FreeMe->Members[i]);
		}
		/** delete our hashing data */
		if (FreeMe->LookupTable[i] != NULL) {
			free(FreeMe->LookupTable[i]->HashKey);
			free(FreeMe->LookupTable[i]);
		}
	}
	FreeMe->nMembersUsed = 0;
	FreeMe->tainted = 0;
	FreeMe->nLookupTableItems = 0;
	memset(FreeMe->Members, 0, sizeof(Payload*) * FreeMe->MemberSize);
	memset(FreeMe->LookupTable, 0, sizeof(HashKey*) * FreeMe->MemberSize);

	// free the array of our keys
	if (FreeMe->MyKeys != NULL)
		free(FreeMe->MyKeys);
}


/*
 * destroy a hashlist and all of its members
 * Crashing? do 'print *FreeMe->LookupTable[i]'
 * Hash Hash to destroy. Is NULL'ed so you are shure its done.
 */
void DeleteHash(HashList **Hash) {
	HashList *FreeMe;

	FreeMe = *Hash;
	if (FreeMe == NULL)
		return;
	DeleteHashContent(Hash);
	/** now, free our arrays... */
	free(FreeMe->LookupTable);
	free(FreeMe->Members);

	/** buye bye cruel world. */	
	free (FreeMe);
	*Hash = NULL;
}


/*
 * Private function to increase the hash size.
 * Hash the Hasharray to increase
 */
static int IncreaseHashSize(HashList *Hash) {
	/* Ok, Our space is used up. Double the available space. */
	Payload **NewPayloadArea;
	HashKey **NewTable;
	
	if (Hash == NULL)
		return 0;

	/** If we grew to much, this might be the place to rehash and shrink again.
	if ((Hash->NMembersUsed > Hash->nLookupTableItems) && 
	    ((Hash->NMembersUsed - Hash->nLookupTableItems) > 
	     (Hash->nLookupTableItems / 10)))
	{


	}
	*/

	NewPayloadArea = (Payload**) malloc(sizeof(Payload*) * Hash->MemberSize * 2);
	if (NewPayloadArea == NULL)
		return 0;
	NewTable = malloc(sizeof(HashKey*) * Hash->MemberSize * 2);
	if (NewTable == NULL) {
		free(NewPayloadArea);
		return 0;
	}

	/** double our payload area */
	memset(&NewPayloadArea[Hash->MemberSize], 0, sizeof(Payload*) * Hash->MemberSize);
	memcpy(NewPayloadArea, Hash->Members, sizeof(Payload*) * Hash->MemberSize);
	free(Hash->Members);
	Hash->Members = NewPayloadArea;
	
	/** double our hashtable area */
	memset(&NewTable[Hash->MemberSize], 0, sizeof(HashKey*) * Hash->MemberSize);
	memcpy(NewTable, Hash->LookupTable, sizeof(HashKey*) * Hash->MemberSize);
	free(Hash->LookupTable);
	Hash->LookupTable = NewTable;
	
	Hash->MemberSize *= 2;
	return 1;
}


/*
 * private function to add a new item to / replace an existing in -  the hashlist
 * if the hash list is full, its re-alloced with double size.
 * Hash our hashlist to manipulate
 * HashPos where should we insert / replace?
 * HashKeyStr the Hash-String
 * HKLen length of HashKeyStr
 * Data your Payload to add
 * Destructor Functionpointer to free Data. if NULL, default free() is used.
 */
static int InsertHashItem(HashList *Hash, 
			  long HashPos, 
			  long HashBinKey, 
			  const char *HashKeyStr, 
			  long HKLen, 
			  void *Data,
			  DeleteHashDataFunc Destructor)
{
	Payload *NewPayloadItem;
	HashKey *NewHashKey;
	char *HashKeyOrgVal;

	if (Hash == NULL)
		return 0;

	if ((Hash->nMembersUsed >= Hash->MemberSize) &&
	    (!IncreaseHashSize (Hash)))
	    return 0;

	NewPayloadItem = (Payload*) malloc (sizeof(Payload));
	if (NewPayloadItem == NULL)
		return 0;
	NewHashKey = (HashKey*) malloc (sizeof(HashKey));
	if (NewHashKey == NULL) {
		free(NewPayloadItem);
		return 0;
	}
	HashKeyOrgVal = (char *) malloc (HKLen + 1);
	if (HashKeyOrgVal == NULL) {
		free(NewHashKey);
		free(NewPayloadItem);
		return 0;
	}


	/** Arrange the payload */
	NewPayloadItem->Data = Data;
	NewPayloadItem->Destructor = Destructor;
	/** Arrange the hashkey */
	NewHashKey->HKLen = HKLen;
	NewHashKey->HashKey = HashKeyOrgVal;
	memcpy (NewHashKey->HashKey, HashKeyStr, HKLen + 1);
	NewHashKey->Key = HashBinKey;
	NewHashKey->PL = NewPayloadItem;
	/** our payload is queued at the end... */
	NewHashKey->Position = Hash->nMembersUsed;
	/** but if we should be sorted into a specific place... */
	if ((Hash->nLookupTableItems != 0) && 
	    (HashPos != Hash->nLookupTableItems) ) {
		long ItemsAfter;

		ItemsAfter = Hash->nLookupTableItems - HashPos;
		/** make space were we can fill us in */
		if (ItemsAfter > 0) {
			memmove(&Hash->LookupTable[HashPos + 1], &Hash->LookupTable[HashPos], ItemsAfter * sizeof(HashKey*));
		} 
	}
	
	Hash->Members[Hash->nMembersUsed] = NewPayloadItem;
	Hash->LookupTable[HashPos] = NewHashKey;
	Hash->nMembersUsed++;
	Hash->nLookupTableItems++;
	return 1;
}

/*
 * if the user has tainted the hash, but wants to insert / search items by their key
 *  we need to search linear through the array. You have been warned that this will take more time!
 * Hash Our Hash to manipulate
 * HashBinKey the Hash-Number to lookup. 
 * returns the position (most closely) matching HashBinKey (-> Caller needs to compare! )
 */
static long FindInTaintedHash(HashList *Hash, long HashBinKey) {
	long SearchPos;

	if (Hash == NULL)
		return 0;

	for (SearchPos = 0; SearchPos < Hash->nLookupTableItems; SearchPos ++) {
		if (Hash->LookupTable[SearchPos]->Key == HashBinKey){
			return SearchPos;
		}
	}
	return SearchPos;
}

/*
 * Private function to lookup the Item / the closest position to put it in
 * Hash Our Hash to manipulate
 * HashBinKey the Hash-Number to lookup. 
 * returns the position (most closely) matching HashBinKey (-> Caller needs to compare! )
 */
static long FindInHash(HashList *Hash, long HashBinKey) {
	long SearchPos;
	long StepWidth;

	if (Hash == NULL)
		return 0;

	if (Hash->tainted)
		return FindInTaintedHash(Hash, HashBinKey);

	SearchPos = Hash->nLookupTableItems / 2;
	StepWidth = SearchPos / 2;
	while ((SearchPos > 0) && (SearchPos < Hash->nLookupTableItems)) {
		/** Did we find it? */
		if (Hash->LookupTable[SearchPos]->Key == HashBinKey){
			return SearchPos;
		}
		/** are we Aproximating in big steps? */
		if (StepWidth > 1){
			if (Hash->LookupTable[SearchPos]->Key > HashBinKey)
				SearchPos -= StepWidth;
			else
				SearchPos += StepWidth;
			StepWidth /= 2;			
		}
		else { /** We are right next to our target, within 4 positions */
			if (Hash->LookupTable[SearchPos]->Key > HashBinKey) {
				if ((SearchPos > 0) && (Hash->LookupTable[SearchPos - 1]->Key < HashBinKey))
					return SearchPos;
				SearchPos --;
			}
			else {
				if ((SearchPos + 1 < Hash->nLookupTableItems) && (Hash->LookupTable[SearchPos + 1]->Key > HashBinKey)) {
					return SearchPos;
				}
				SearchPos ++;
			}
			StepWidth--;
		}
	}
	return SearchPos;
}


/*
 * another hashing algorithm; treat it as just a pointer to int.
 * str Our pointer to the int value
 * len the length of the data pointed to; needs to be sizeof int, else we won't use it!
 * returns the calculated hash value
 */
long Flathash(const char *str, long len) {
	if (len != sizeof (int)) {
#ifdef DEBUG
		int *crash = NULL;
		*crash = 1;
#endif
		return 0;
	}
	else return *(int*)str;
}

/*
 * another hashing algorithm; treat it as just a pointer to long.
 * str Our pointer to the long value
 * len the length of the data pointed to; needs to be sizeof long, else we won't use it!
 * returns the calculated hash value
 */
long lFlathash(const char *str, long len) {
	if (len != sizeof (long)) {
#ifdef DEBUG
		int *crash = NULL;
		*crash = 1;
#endif
		return 0;
	}
	else return *(long*)str;
}

/*
 * another hashing algorithm; accepts exactly 4 characters, convert it to a hash key.
 * str Our pointer to the long value
 * len the length of the data pointed to; needs to be sizeof long, else we won't use it!
 * returns the calculated hash value
 */
long FourHash(const char *key, long length) {
	int i;
	int ret = 0;
	const unsigned char *ptr = (const unsigned char*)key;

	for (i = 0; i < 4; i++, ptr ++) 
		ret = (ret << 8) | 
			( ((*ptr >= 'a') &&
			   (*ptr <= 'z'))? 
			  *ptr - 'a' + 'A': 
			  *ptr);

	return ret;
}

/*
 * private abstract wrapper around the hashing algorithm
 * HKey the hash string
 * HKLen length of HKey
 * returns the calculated hash value
 */
inline static long CalcHashKey (HashList *Hash, const char *HKey, long HKLen) {
	if (Hash == NULL)
		return 0;

	if (Hash->Algorithm == NULL)
		return hashlittle(HKey, HKLen, 9283457);
	else
		return Hash->Algorithm(HKey, HKLen);
}


/*
 * Add a new / Replace an existing item in the Hash
 * Hash the list to manipulate
 * HKey the hash-string to store Data under
 * HKLen Length of HKey
 * Data the payload you want to associate with HKey
 * DeleteIt if not free() should be used to delete Data set to NULL, else DeleteIt is used.
 */
void Put(HashList *Hash, const char *HKey, long HKLen, void *Data, DeleteHashDataFunc DeleteIt) {
	long HashBinKey;
	long HashAt;

	if (Hash == NULL)
		return;

	/** first, find out were we could fit in... */
	HashBinKey = CalcHashKey(Hash, HKey, HKLen);
	HashAt = FindInHash(Hash, HashBinKey);

	if ((HashAt >= Hash->MemberSize) &&
	    (!IncreaseHashSize (Hash)))
		return;

	/** oh, we're brand new... */
	if (Hash->LookupTable[HashAt] == NULL) {
		InsertHashItem(Hash, HashAt, HashBinKey, HKey, HKLen, Data, DeleteIt);
	}/** Insert Before? */
	else if (Hash->LookupTable[HashAt]->Key > HashBinKey) {
		InsertHashItem(Hash, HashAt, HashBinKey, HKey, HKLen, Data, DeleteIt);
	}/** Insert After? */
	else if (Hash->LookupTable[HashAt]->Key < HashBinKey) {
		InsertHashItem(Hash, HashAt + 1, HashBinKey, HKey, HKLen, Data, DeleteIt);
	}
	else { /** Ok, we have a colision. replace it. */
		if (Hash->uniq) {
			long PayloadPos;
			
			PayloadPos = Hash->LookupTable[HashAt]->Position;
			DeleteHashPayload(Hash->Members[PayloadPos]);
			Hash->Members[PayloadPos]->Data = Data;
			Hash->Members[PayloadPos]->Destructor = DeleteIt;
		}
		else {
			InsertHashItem(Hash, HashAt + 1, HashBinKey, HKey, HKLen, Data, DeleteIt);
		}
	}
}

/*
 * Lookup the Data associated with HKey
 * Hash the Hashlist to search in
 * HKey the hashkey to look up
 * HKLen length of HKey
 * Data returns the Data associated with HKey
 * returns 0 if not found, 1 if.
 */
int GetHash(HashList *Hash, const char *HKey, long HKLen, void **Data) {
	long HashBinKey;
	long HashAt;

	if (Hash == NULL)
		return 0;

	if (HKLen <= 0) {
		*Data = NULL;
		return  0;
	}
	/** first, find out were we could be... */
	HashBinKey = CalcHashKey(Hash, HKey, HKLen);
	HashAt = FindInHash(Hash, HashBinKey);
	if ((HashAt < 0) || /**< Not found at the lower edge? */
	    (HashAt >= Hash->nLookupTableItems) || /**< Not found at the upper edge? */
	    (Hash->LookupTable[HashAt]->Key != HashBinKey)) { /**< somewhere inbetween but no match? */
		*Data = NULL;
		return 0;
	}
	else { /** GOTCHA! */
		long MemberPosition;

		MemberPosition = Hash->LookupTable[HashAt]->Position;
		*Data = Hash->Members[MemberPosition]->Data;
		return 1;
	}
}

/* TODO? */
int GetKey(HashList *Hash, char *HKey, long HKLen, void **Payload) {
	return 0;
}

/*
 * get the Keys present in this hash, similar to array_keys() in PHP
 *  Attention: List remains to Hash! don't modify or free it!
 * Hash Your Hashlist to extract the keys from
 * List returns the list of hashkeys stored in Hash
 */
int GetHashKeys(HashList *Hash, char ***List) {
	long i;

	*List = NULL;
	if (Hash == NULL)
		return 0;
	if (Hash->MyKeys != NULL)
		free (Hash->MyKeys);

	Hash->MyKeys = (char**) malloc(sizeof(char*) * Hash->nLookupTableItems);
	if (Hash->MyKeys == NULL)
		return 0;

	for (i=0; i < Hash->nLookupTableItems; i++)
	{
		Hash->MyKeys[i] = Hash->LookupTable[i]->HashKey;
	}
	*List = (char**)Hash->MyKeys;
	return Hash->nLookupTableItems;
}

/*
 * creates a hash-linear iterator object
 * Hash the list we reference
 * StepWidth in which step width should we iterate?
 *  If negative, the last position matching the 
 *  step-raster is provided.
 * returns the hash iterator
 */
HashPos *GetNewHashPos(const HashList *Hash, int StepWidth) {
	HashPos *Ret;
	
	Ret = (HashPos*)malloc(sizeof(HashPos));
	if (Ret == NULL)
		return NULL;

	if (StepWidth != 0)
		Ret->StepWidth = StepWidth;
	else
		Ret->StepWidth = 1;
	if (Ret->StepWidth <  0) {
		Ret->Position = Hash->nLookupTableItems - 1;
	}
	else {
		Ret->Position = 0;
	}
	return Ret;
}

/*
 * resets a hash-linear iterator object
 * Hash the list we reference
 * StepWidth in which step width should we iterate?
 * it the iterator object to manipulate
 *  If negative, the last position matching the 
 *  step-raster is provided.
 * returns the hash iterator
 */
void RewindHashPos(const HashList *Hash, HashPos *it, int StepWidth) {
	if (StepWidth != 0)
		it->StepWidth = StepWidth;
	else
		it->StepWidth = 1;
	if (it->StepWidth <  0) {
		it->Position = Hash->nLookupTableItems - 1;
	}
	else {
		it->Position = 0;
	}
}

/*
 * Set iterator object to point to key. If not found, don't change iterator
 * Hash the list we reference
 * HKey key to search for
 * HKLen length of key
 * At HashPos to update
 * returns 0 if not found
 */
int GetHashPosFromKey(HashList *Hash, const char *HKey, long HKLen, HashPos *At) {
	long HashBinKey;
	long HashAt;

	if (Hash == NULL)
		return 0;

	if (HKLen <= 0) {
		return  0;
	}
	/** first, find out were we could be... */
	HashBinKey = CalcHashKey(Hash, HKey, HKLen);
	HashAt = FindInHash(Hash, HashBinKey);
	if ((HashAt < 0) || /**< Not found at the lower edge? */
	    (HashAt >= Hash->nLookupTableItems) || /**< Not found at the upper edge? */
	    (Hash->LookupTable[HashAt]->Key != HashBinKey)) { /**< somewhere inbetween but no match? */
		return 0;
	}
	/** GOTCHA! */
	At->Position = HashAt;
	return 1;
}

/*
 * Delete from the Hash the entry at Position
 * Hash the list we reference
 * At the position within the Hash
 * returns 0 if not found
 */
int DeleteEntryFromHash(HashList *Hash, HashPos *At) {
	Payload *FreeMe;
	if (Hash == NULL)
		return 0;

	/* if lockable, lock here */
	if ((Hash == NULL) || 
	    (At->Position >= Hash->nLookupTableItems) || 
	    (At->Position < 0) ||
	    (At->Position > Hash->nLookupTableItems))
	{
		/* unlock... */
		return 0;
	}

	FreeMe = Hash->Members[Hash->LookupTable[At->Position]->Position];
	Hash->Members[Hash->LookupTable[At->Position]->Position] = NULL;


	/** delete our hashing data */
	if (Hash->LookupTable[At->Position] != NULL) {
		free(Hash->LookupTable[At->Position]->HashKey);
		free(Hash->LookupTable[At->Position]);
		if (At->Position < Hash->nLookupTableItems) {
			memmove(&Hash->LookupTable[At->Position],
				&Hash->LookupTable[At->Position + 1],
				(Hash->nLookupTableItems - At->Position - 1) * 
				sizeof(HashKey*));

			Hash->LookupTable[Hash->nLookupTableItems - 1] = NULL;
		}
		else 
			Hash->LookupTable[At->Position] = NULL;
		Hash->nLookupTableItems--;
	}
	/* unlock... */


	/** get rid of our payload */
	if (FreeMe != NULL) {
		DeleteHashPayload(FreeMe);
		free(FreeMe);
	}
	return 1;
}

/*
 * retrieve the counter from the itteratoor
 * Hash which 
 * At the Iterator to analyze
 * returns the n'th hashposition we point at
 */
int GetHashPosCounter(HashList *Hash, HashPos *At) {
	if ((Hash == NULL) || 
	    (At->Position >= Hash->nLookupTableItems) || 
	    (At->Position < 0) ||
	    (At->Position > Hash->nLookupTableItems))
		return 0;
	return At->Position;
}

/*
 * frees a linear hash iterator
 */
void DeleteHashPos(HashPos **DelMe) {
	if (*DelMe != NULL) {
		free(*DelMe);
		*DelMe = NULL;
	}
}


/*
 * Get the data located where HashPos Iterator points at, and Move HashPos one forward
 * Hash your Hashlist to follow
 * At the position to retrieve the Item from and move forward afterwards
 * HKLen returns Length of Hashkey Returned
 * HashKey returns the Hashkey corrosponding to HashPos
 * Data returns the Data found at HashPos
 * returns whether the item was found or not.
 */
int GetNextHashPos(const HashList *Hash, HashPos *At, long *HKLen, const char **HashKey, void **Data) {
	long PayloadPos;

	if ((Hash == NULL) || 
	    (At->Position >= Hash->nLookupTableItems) || 
	    (At->Position < 0) ||
	    (At->Position > Hash->nLookupTableItems))
		return 0;
	*HKLen = Hash->LookupTable[At->Position]->HKLen;
	*HashKey = Hash->LookupTable[At->Position]->HashKey;
	PayloadPos = Hash->LookupTable[At->Position]->Position;
	*Data = Hash->Members[PayloadPos]->Data;

	/* Position is NULL-Based, while Stepwidth is not... */
	if ((At->Position % abs(At->StepWidth)) == 0)
		At->Position += At->StepWidth;
	else 
		At->Position += ((At->Position) % abs(At->StepWidth)) * 
			(At->StepWidth / abs(At->StepWidth));
	return 1;
}

/*
 * Get the data located where HashPos Iterator points at
 * Hash your Hashlist to follow
 * At the position retrieve the data from
 * HKLen returns Length of Hashkey Returned
 * HashKey returns the Hashkey corrosponding to HashPos
 * Data returns the Data found at HashPos
 * returns whether the item was found or not.
 */
int GetHashPos(HashList *Hash, HashPos *At, long *HKLen, const char **HashKey, void **Data) {
	long PayloadPos;

	if ((Hash == NULL) || 
	    (At->Position >= Hash->nLookupTableItems) || 
	    (At->Position < 0) ||
	    (At->Position > Hash->nLookupTableItems))
		return 0;
	*HKLen = Hash->LookupTable[At->Position]->HKLen;
	*HashKey = Hash->LookupTable[At->Position]->HashKey;
	PayloadPos = Hash->LookupTable[At->Position]->Position;
	*Data = Hash->Members[PayloadPos]->Data;

	return 1;
}

/*
 * Move HashPos one forward
 * Hash your Hashlist to follow
 * At the position to move forward
 * returns whether there is a next item or not.
 */
int NextHashPos(HashList *Hash, HashPos *At) {
	if ((Hash == NULL) || 
	    (At->Position >= Hash->nLookupTableItems) || 
	    (At->Position < 0) ||
	    (At->Position > Hash->nLookupTableItems))
		return 0;

	/* Position is NULL-Based, while Stepwidth is not... */
	if ((At->Position % abs(At->StepWidth)) == 0)
		At->Position += At->StepWidth;
	else 
		At->Position += ((At->Position) % abs(At->StepWidth)) * 
			(At->StepWidth / abs(At->StepWidth));
	return !((At->Position >= Hash->nLookupTableItems) || 
		 (At->Position < 0) ||
		 (At->Position > Hash->nLookupTableItems));
}

/*
 * Get the data located where At points to
 * note: you should prefer iterator operations instead of using me.
 * Hash your Hashlist peek from
 * At get the item in the position At.
 * HKLen returns Length of Hashkey Returned
 * HashKey returns the Hashkey corrosponding to HashPos
 * Data returns the Data found at HashPos
 * returns whether the item was found or not.
 */
int GetHashAt(HashList *Hash,long At, long *HKLen, const char **HashKey, void **Data) {
	long PayloadPos;

	if ((Hash == NULL) || 
	    (At < 0) || 
	    (At >= Hash->nLookupTableItems))
		return 0;
	*HKLen = Hash->LookupTable[At]->HKLen;
	*HashKey = Hash->LookupTable[At]->HashKey;
	PayloadPos = Hash->LookupTable[At]->Position;
	*Data = Hash->Members[PayloadPos]->Data;
	return 1;
}

/*
 * Get the data located where At points to
 * note: you should prefer iterator operations instead of using me.
 * Hash your Hashlist peek from
 * HKLen returns Length of Hashkey Returned
 * HashKey returns the Hashkey corrosponding to HashPos
 * Data returns the Data found at HashPos
 * returns whether the item was found or not.
 */
/*
long GetHashIDAt(HashList *Hash,long At)
{
	if ((Hash == NULL) || 
	    (At < 0) || 
	    (At > Hash->nLookupTableItems))
		return 0;

	return Hash->LookupTable[At]->Key;
}
*/


/*
 * sorting function for sorting the Hash alphabeticaly by their strings
 * Key1 first item
 * Key2 second item
 */
static int SortByKeys(const void *Key1, const void* Key2) {
	HashKey *HKey1, *HKey2;
	HKey1 = *(HashKey**) Key1;
	HKey2 = *(HashKey**) Key2;

	return strcasecmp(HKey1->HashKey, HKey2->HashKey);
}

/*
 * sorting function for sorting the Hash alphabeticaly reverse by their strings
 * Key1 first item
 * Key2 second item
 */
static int SortByKeysRev(const void *Key1, const void* Key2) {
	HashKey *HKey1, *HKey2;
	HKey1 = *(HashKey**) Key1;
	HKey2 = *(HashKey**) Key2;

	return strcasecmp(HKey2->HashKey, HKey1->HashKey);
}

/*
 * sorting function to regain hash-sequence and revert tainted status
 * Key1 first item
 * Key2 second item
 */
static int SortByHashKeys(const void *Key1, const void* Key2) {
	HashKey *HKey1, *HKey2;
	HKey1 = *(HashKey**) Key1;
	HKey2 = *(HashKey**) Key2;

	return HKey1->Key > HKey2->Key;
}


/*
 * sort the hash alphabeticaly by their keys.
 * Caution: This taints the hashlist, so accessing it later 
 * will be significantly slower! You can un-taint it by SortByHashKeyStr
 * Hash the list to sort
 * Order 0/1 Forward/Backward
 */
void SortByHashKey(HashList *Hash, int Order) {
	if (Hash->nLookupTableItems < 2)
		return;
	qsort(Hash->LookupTable, Hash->nLookupTableItems, sizeof(HashKey*), 
	      (Order)?SortByKeys:SortByKeysRev);
	Hash->tainted = 1;
}

/*
 * sort the hash by their keys (so it regains untainted state).
 * this will result in the sequence the hashing allgorithm produces it by default.
 * Hash the list to sort
 */
void SortByHashKeyStr(HashList *Hash) {
	Hash->tainted = 0;
	if (Hash->nLookupTableItems < 2)
		return;
	qsort(Hash->LookupTable, Hash->nLookupTableItems, sizeof(HashKey*), SortByHashKeys);
}


/*
 * gives user sort routines access to the hash payload
 * HashVoid to retrieve Data to
 * returns Data belonging to HashVoid
 */
const void *GetSearchPayload(const void *HashVoid) {
	return (*(HashKey**)HashVoid)->PL->Data;
}

/*
 * sort the hash by your sort function. see the following sample.
 * this will result in the sequence the hashing allgorithm produces it by default.
 * Hash the list to sort
 * SortBy Sortfunction; see below how to implement this
 */
void SortByPayload(HashList *Hash, CompareFunc SortBy) {
	if (Hash->nLookupTableItems < 2) {
		return;
	}
	qsort(Hash->LookupTable, Hash->nLookupTableItems, sizeof(HashKey*), SortBy);
	Hash->tainted = 1;
}




/*
 * given you've put char * into your hash as a payload, a sort function might
 * look like this:
 * int SortByChar(const void* First, const void* Second)
 * {
 *      char *a, *b;
 *      a = (char*) GetSearchPayload(First);
 *      b = (char*) GetSearchPayload(Second);
 *      return strcmp (a, b);
 * }
 */


/*
 * Generic function to free a reference.  
 * since a reference actualy isn't needed to be freed, do nothing.
 */
void reference_free_handler(void *ptr) {
	return;
}


/*
 * This exposes the hashlittle() function to consumers.
 */
int HashLittle(const void *key, size_t length) {
	return (int)hashlittle(key, length, 1);
}


/*
 * parses an MSet string into a list for later use
 * MSetList List to be read from MSetStr
 * MSetStr String containing the list
 */
int ParseMSet(MSet **MSetList, StrBuf *MSetStr) {
	const char *POS = NULL, *SetPOS = NULL;
	StrBuf *OneSet;
	HashList *ThisMSet;
	long StartSet, EndSet;
	long *pEndSet;
	
	*MSetList = NULL;
	if ((MSetStr == NULL) || (StrLength(MSetStr) == 0))
	    return 0;
	    
	OneSet = NewStrBufPlain(NULL, StrLength(MSetStr));
	if (OneSet == NULL)
		return 0;

	ThisMSet = NewHash(0, lFlathash);
	if (ThisMSet == NULL) {
		FreeStrBuf(&OneSet);
		return 0;
	}

	*MSetList = (MSet*) ThisMSet;

	/* an MSet is a coma separated value list. */
	StrBufExtract_NextToken(OneSet, MSetStr, &POS, ',');
	do {
		SetPOS = NULL;

		/* One set may consist of two Numbers: Start + optional End */
		StartSet = StrBufExtractNext_long(OneSet, &SetPOS, ':');
		EndSet = 0; /* no range is our default. */
		/* do we have an end (aka range?) */
		if ((SetPOS != NULL) && (SetPOS != StrBufNOTNULL)) {
			if (*(SetPOS) == '*')
				EndSet = LONG_MAX; /* ranges with '*' go until infinity */
			else 
				/* in other cases, get the EndPoint */
				EndSet = StrBufExtractNext_long(OneSet, &SetPOS, ':');
		}

		pEndSet = (long*) malloc (sizeof(long));
		if (pEndSet == NULL) {
			FreeStrBuf(&OneSet);
			DeleteHash(&ThisMSet);
			return 0;
		}
		*pEndSet = EndSet;

		Put(ThisMSet, LKEY(StartSet), pEndSet, NULL);
		/* if we don't have another, we're done. */
		if (POS == StrBufNOTNULL)
			break;
		StrBufExtract_NextToken(OneSet, MSetStr, &POS, ',');
	} while (1);
	FreeStrBuf(&OneSet);

	return 1;
}

/*
 * checks whether a message is inside a mset
 * MSetList List to search for MsgNo
 * MsgNo number to search in mset
 */
int IsInMSetList(MSet *MSetList, long MsgNo) {
	/* basicaly we are a ... */
	long MemberPosition;
	HashList *Hash = (HashList*) MSetList;
	long HashAt;
	long EndAt;
	long StartAt;

	if (Hash == NULL)
		return 0;
	if (Hash->MemberSize == 0)
		return 0;
	/** first, find out were we could fit in... */
	HashAt = FindInHash(Hash, MsgNo);
	
	/* we're below the first entry, so not found. */
	if (HashAt < 0)
		return 0;
	/* upper edge? move to last item */
	if (HashAt >= Hash->nMembersUsed)
		HashAt = Hash->nMembersUsed - 1;
	/* Match? then we got it. */
	else if (Hash->LookupTable[HashAt]->Key == MsgNo)
		return 1;
	/* One above possible range start? we need to move to the lower one. */ 
	else if ((HashAt > 0) && 
		 (Hash->LookupTable[HashAt]->Key > MsgNo))
		HashAt -=1;

	/* Fetch the actual data */
	StartAt = Hash->LookupTable[HashAt]->Key;
	MemberPosition = Hash->LookupTable[HashAt]->Position;
	EndAt = *(long*) Hash->Members[MemberPosition]->Data;
	if ((MsgNo >= StartAt) && (EndAt == LONG_MAX))
		return 1;
	/* no range? */
	if (EndAt == 0)
		return 0;
	/* inside of range? */
	if ((StartAt <= MsgNo) && (EndAt >= MsgNo))
		return 1;
	return 0;
}


/*
 * frees a mset [redirects to @ref DeleteHash
 * FreeMe to be free'd
 */
void DeleteMSet(MSet **FreeMe) {
	DeleteHash((HashList**) FreeMe);
}
