/*
 * This module handles loading, saving, and parsing of room network configurations.
 *
 * Copyright (c) 2000-2018 by the citadel.org team
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "sysdep.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef HAVE_SYSCALL_H
# include <syscall.h>
#else 
# if HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
# endif
#endif
#include <dirent.h>
#include <assert.h>

#include <libcitadel.h>

#include "include/ctdl_module.h"
#include "serv_extensions.h"
#include "config.h"

void vFreeRoomNetworkStruct(void *vOneRoomNetCfg);
void FreeRoomNetworkStructContent(OneRoomNetCfg *OneRNCfg);

HashList *CfgTypeHash = NULL;

/*-----------------------------------------------------------------------------*
 *                       Per room network configs                              *
 *-----------------------------------------------------------------------------*/


void RegisterRoomCfgType(const char* Name, long len, RoomNetCfg eCfg, CfgLineParser p, int uniq,  int nSegments, CfgLineSerializer s, CfgLineDeAllocator d)
{
	CfgLineType *pCfg;

	pCfg = (CfgLineType*) malloc(sizeof(CfgLineType));
	pCfg->Parser = p;
	pCfg->Serializer = s;
	pCfg->DeAllocator = d;
	pCfg->C = eCfg;
	pCfg->Str.Key = Name;
	pCfg->Str.len = len;
	pCfg->IsSingleLine = uniq;
 	pCfg->nSegments = nSegments;
	if (CfgTypeHash == NULL) {
		CfgTypeHash = NewHash(1, NULL);
	}
	Put(CfgTypeHash, Name, len, pCfg, NULL);
}


const CfgLineType *GetCfgTypeByStr(const char *Key, long len)
{
	void *pv;
	
	if (GetHash(CfgTypeHash, Key, len, &pv) && (pv != NULL))
	{
		return (const CfgLineType *) pv;
	}
	else
	{
		return NULL;
	}
}


const CfgLineType *GetCfgTypeByEnum(RoomNetCfg eCfg, HashPos *It)
{
	const char *Key;
	long len;
	void *pv;
	CfgLineType *pCfg;

	RewindHashPos(CfgTypeHash, It, 1);
	while (GetNextHashPos(CfgTypeHash, It, &len, &Key, &pv) && (pv != NULL))
	{
		pCfg = (CfgLineType*) pv;
		if (pCfg->C == eCfg)
			return pCfg;
	}
	return NULL;
}


void ParseGeneric(const CfgLineType *ThisOne, StrBuf *Line, const char *LinePos, OneRoomNetCfg *OneRNCfg)
{
	RoomNetCfgLine *nptr;
	int i;

	nptr = (RoomNetCfgLine *)
		malloc(sizeof(RoomNetCfgLine));
	nptr->next = OneRNCfg->NetConfigs[ThisOne->C];
	nptr->Value = malloc(sizeof(StrBuf*) * ThisOne->nSegments);
	nptr->nValues = 0;
	memset(nptr->Value, 0, sizeof(StrBuf*) * ThisOne->nSegments);
	if (ThisOne->nSegments == 1)
	{
		nptr->Value[0] = NewStrBufPlain(LinePos, StrLength(Line) - ( LinePos - ChrPtr(Line)) );
		nptr->nValues = 1;
	}
	else for (i = 0; i < ThisOne->nSegments; i++)
	{
		nptr->nValues++;
		nptr->Value[i] = NewStrBufPlain(NULL, StrLength(Line) - ( LinePos - ChrPtr(Line)) );
		StrBufExtract_NextToken(nptr->Value[i], Line, &LinePos, '|');
	}

	OneRNCfg->NetConfigs[ThisOne->C] = nptr;
}


void SerializeGeneric(const CfgLineType *ThisOne, StrBuf *OutputBuffer, OneRoomNetCfg *OneRNCfg, RoomNetCfgLine *data)
{
	int i;

	StrBufAppendBufPlain(OutputBuffer, CKEY(ThisOne->Str), 0);
	StrBufAppendBufPlain(OutputBuffer, HKEY("|"), 0);
	for (i = 0; i < ThisOne->nSegments; i++)
	{
		StrBufAppendBuf(OutputBuffer, data->Value[i], 0);
		if (i + 1 < ThisOne->nSegments)
			StrBufAppendBufPlain(OutputBuffer, HKEY("|"), 0);
	}
	StrBufAppendBufPlain(OutputBuffer, HKEY("\n"), 0);
}


void DeleteGenericCfgLine(const CfgLineType *ThisOne, RoomNetCfgLine **data)
{
	int i;

	if (*data == NULL)
		return;

	for (i = 0; i < (*data)->nValues; i++)
	{
		FreeStrBuf(&(*data)->Value[i]);
	}
	free ((*data)->Value);
	free(*data);
	*data = NULL;
}


RoomNetCfgLine *DuplicateOneGenericCfgLine(const RoomNetCfgLine *data)
{
	int i;
	RoomNetCfgLine *NewData;

	NewData = (RoomNetCfgLine*)malloc(sizeof(RoomNetCfgLine));
	memset(NewData, 0, sizeof(RoomNetCfgLine));
	NewData->Value = (StrBuf **)malloc(sizeof(StrBuf*) * data->nValues);
	memset(NewData->Value, 0, sizeof(StrBuf*) * data->nValues);

	for (i = 0; i < data->nValues; i++)
	{
		NewData->Value[i] = NewStrBufDup(data->Value[i]);
	}
	NewData->nValues = data->nValues;
	return NewData;
}


/*
 * Create a config key for a room's netconfig entry
 */
void netcfg_keyname(char *keybuf, long roomnum)
{
	if (!keybuf) return;
	sprintf(keybuf, "c_netconfig_%010ld", roomnum);
}


/*
 * Given a room number and a textual netconfig, convert to base64 and write to the configdb
 */
void write_netconfig_to_configdb(long roomnum, const char *raw_netconfig)
{
	char keyname[25];
	char *enc;
	int enc_len;
	int len;

	len = strlen(raw_netconfig);
	netcfg_keyname(keyname, roomnum);
	enc = malloc(len * 2);

	if (enc) {
		enc_len = CtdlEncodeBase64(enc, raw_netconfig, len, 0);
		if ((enc_len > 1) && (enc[enc_len-2] == 13)) enc[enc_len-2] = 0;
		if ((enc_len > 0) && (enc[enc_len-1] == 10)) enc[enc_len-1] = 0;
		enc[enc_len] = 0;
		syslog(LOG_DEBUG, "netconfig: writing key '%s' (length=%d)", keyname, enc_len);
		CtdlSetConfigStr(keyname, enc);
		free(enc);
	}
}


/*
 * Given a room number, attempt to load the netconfig configdb entry for that room.
 * If it returns NULL, there is no netconfig.
 * Otherwise the caller owns the returned memory and is responsible for freeing it.
 */
char *LoadRoomNetConfigFile(long roomnum)
{
	char keyname[25];
	char *encoded_netconfig = NULL;
	char *decoded_netconfig = NULL;

	netcfg_keyname(keyname, roomnum);
	encoded_netconfig = CtdlGetConfigStr(keyname);
	if (!encoded_netconfig) return NULL;

	decoded_netconfig = malloc(strlen(encoded_netconfig));	// yeah, way bigger than it needs to be, but safe
	CtdlDecodeBase64(decoded_netconfig, encoded_netconfig, strlen(encoded_netconfig));
	return decoded_netconfig;
}


/*
 * Deserialize a netconfig , allocate and return structured data
 */
OneRoomNetCfg *ParseRoomNetConfigFile(char *serialized_data)
{
	const char *Pos = NULL;
	const CfgLineType *pCfg = NULL;
	StrBuf *Line = NULL;
	StrBuf *InStr = NULL;
	StrBuf *Cfg = NULL;
	OneRoomNetCfg *OneRNCfg = NULL;
	int num_lines = 0;
	int i = 0;

	OneRNCfg = malloc(sizeof(OneRoomNetCfg));
	memset(OneRNCfg, 0, sizeof(OneRoomNetCfg));

	Line = NewStrBuf();
	InStr = NewStrBuf();
        Cfg = NewStrBufPlain(serialized_data, -1);
	num_lines = num_tokens(ChrPtr(Cfg), '\n');

	for (i=0; i<num_lines; ++i) {
		StrBufExtract_token(Line, Cfg, i, '\n');
		if (StrLength(Line) > 0) {
			Pos = NULL;
			StrBufExtract_NextToken(InStr, Line, &Pos, '|');
	
			pCfg = GetCfgTypeByStr(SKEY(InStr));
			if (pCfg != NULL)
			{
				pCfg->Parser(pCfg, Line, Pos, OneRNCfg);
			}
			else
			{
				if (OneRNCfg->misc == NULL)
				{
					OneRNCfg->misc = NewStrBufDup(Line);
				}
				else
				{
					if (StrLength(OneRNCfg->misc) > 0) {
						StrBufAppendBufPlain(OneRNCfg->misc, HKEY("\n"), 0);
					}
					StrBufAppendBuf(OneRNCfg->misc, Line, 0);
				}
			}
		}
	}
	FreeStrBuf(&InStr);
	FreeStrBuf(&Line);
	FreeStrBuf(&Cfg);
	return OneRNCfg;
}


void SaveRoomNetConfigFile(OneRoomNetCfg *OneRNCfg, long roomnum)
{
	RoomNetCfg eCfg;
	StrBuf *Cfg = NULL;
	StrBuf *OutBuffer = NULL;
	HashPos *CfgIt;

	Cfg = NewStrBuf();
	OutBuffer = NewStrBuf();
	CfgIt = GetNewHashPos(CfgTypeHash, 1);
	for (eCfg = subpending; eCfg < maxRoomNetCfg; eCfg ++)
	{
		const CfgLineType *pCfg;
		pCfg = GetCfgTypeByEnum(eCfg, CfgIt);
		if (pCfg)
		{
			if (pCfg->IsSingleLine)
			{
				pCfg->Serializer(pCfg, OutBuffer, OneRNCfg, NULL);
			}
			else
			{
				RoomNetCfgLine *pName = OneRNCfg->NetConfigs[pCfg->C];
				while (pName != NULL)
				{
					pCfg->Serializer(pCfg, OutBuffer, OneRNCfg, pName);
					pName = pName->next;
				}
			}
		}

	}
	DeleteHashPos(&CfgIt);

	if (OneRNCfg->misc != NULL) {
		StrBufAppendBuf(OutBuffer, OneRNCfg->misc, 0);
	}

	write_netconfig_to_configdb(roomnum, ChrPtr(OutBuffer));

	FreeStrBuf(&OutBuffer);
	FreeStrBuf(&Cfg);
}


void AddRoomCfgLine(OneRoomNetCfg *OneRNCfg, struct ctdlroom *qrbuf, RoomNetCfg LineType, RoomNetCfgLine *Line)
{
	RoomNetCfgLine **pLine;

	if (OneRNCfg == NULL)
	{
		OneRNCfg = (OneRoomNetCfg*) malloc(sizeof(OneRoomNetCfg));
		memset(OneRNCfg, 0, sizeof(OneRoomNetCfg));
	}
	pLine = &OneRNCfg->NetConfigs[LineType];

	while(*pLine != NULL) pLine = &((*pLine)->next);
	*pLine = Line;
}


void FreeRoomNetworkStructContent(OneRoomNetCfg *OneRNCfg)
{
	RoomNetCfg eCfg;
	HashPos *CfgIt;

	CfgIt = GetNewHashPos(CfgTypeHash, 1);
	for (eCfg = subpending; eCfg < maxRoomNetCfg; eCfg ++)
	{
		const CfgLineType *pCfg;
		RoomNetCfgLine *pNext, *pName;
		
		pCfg = GetCfgTypeByEnum(eCfg, CfgIt);
		pName= OneRNCfg->NetConfigs[eCfg];
		while (pName != NULL)
		{
			pNext = pName->next;
			if (pCfg != NULL)
			{
				pCfg->DeAllocator(pCfg, &pName);
			}
			else
			{
				DeleteGenericCfgLine(NULL, &pName);
			}
			pName = pNext;
		}
	}
	DeleteHashPos(&CfgIt);

	FreeStrBuf(&OneRNCfg->Sender);
	FreeStrBuf(&OneRNCfg->RoomInfo);
	FreeStrBuf(&OneRNCfg->misc);
	memset(OneRNCfg, 0, sizeof(OneRoomNetCfg));
}


void vFreeRoomNetworkStruct(void *vOneRoomNetCfg)
{
	OneRoomNetCfg *OneRNCfg;
	OneRNCfg = (OneRoomNetCfg*)vOneRoomNetCfg;
	FreeRoomNetworkStructContent(OneRNCfg);
	free(OneRNCfg);
}


void FreeRoomNetworkStruct(OneRoomNetCfg **pOneRNCfg)
{
	vFreeRoomNetworkStruct(*pOneRNCfg);
	*pOneRNCfg=NULL;
}


/*
 * Fetch the netconfig entry for a room, parse it, and return the data.
 * Caller owns the returned memory and MUST free it using FreeRoomNetworkStruct()
 */
OneRoomNetCfg *CtdlGetNetCfgForRoom(long roomnum)
{
	OneRoomNetCfg *OneRNCfg = NULL;
	char *serialized_config = NULL;

	serialized_config = LoadRoomNetConfigFile(roomnum);
	if (!serialized_config) return NULL;

	OneRNCfg = ParseRoomNetConfigFile(serialized_config);
	free(serialized_config);
	return OneRNCfg;
}


/*-----------------------------------------------------------------------------*
 *              Per room network configs : exchange with client                *
 *-----------------------------------------------------------------------------*/

void cmd_gnet(char *argbuf)
{
	if ( (CC->room.QRflags & QR_MAILBOX) && (CC->user.usernum == atol(CC->room.QRname)) ) {
		/* users can edit the netconfigs for their own mailbox rooms */
	}
	else if (CtdlAccessCheck(ac_room_aide)) return;
	
	cprintf("%d Network settings for room #%ld <%s>\n", LISTING_FOLLOWS, CC->room.QRnumber, CC->room.QRname);

	char *c = LoadRoomNetConfigFile(CC->room.QRnumber);
	if (c) {
		int len = strlen(c);
		client_write(c, len);			// Can't use cprintf() here, it has a limit of 1024 bytes
		if (c[len] != '\n') {
			client_write(HKEY("\n"));
		}
		free(c);
	}
	cprintf("000\n");
}


void cmd_snet(char *argbuf)
{
	struct CitContext *CCC = CC;
	StrBuf *Line = NULL;
	StrBuf *TheConfig = NULL;
	int rc;

	unbuffer_output();
	Line = NewStrBuf();
	TheConfig = NewStrBuf();
	cprintf("%d send new netconfig now\n", SEND_LISTING);

	while (rc = CtdlClientGetLine(Line), (rc >= 0))
	{
		if ((rc == 3) && (strcmp(ChrPtr(Line), "000") == 0))
			break;

		StrBufAppendBuf(TheConfig, Line, 0);
		StrBufAppendBufPlain(TheConfig, HKEY("\n"), 0);
	}
	FreeStrBuf(&Line);

	write_netconfig_to_configdb(CCC->room.QRnumber, ChrPtr(TheConfig));
	FreeStrBuf(&TheConfig);
}


/*-----------------------------------------------------------------------------*
 *                       Per node network configs                              *
 *-----------------------------------------------------------------------------*/
void DeleteCtdlNodeConf(void *vNode)
{
	CtdlNodeConf *Node = (CtdlNodeConf*) vNode;
	FreeStrBuf(&Node->NodeName);
	FreeStrBuf(&Node->Secret);
	FreeStrBuf(&Node->Host);
	FreeStrBuf(&Node->Port);
	free(Node);
}


CtdlNodeConf *NewNode(StrBuf *SerializedNode)
{
	const char *Pos = NULL;
	CtdlNodeConf *Node;

	/* we need at least 4 pipes and some other text so its invalid. */
	if (StrLength(SerializedNode) < 8)
		return NULL;
	Node = (CtdlNodeConf *) malloc(sizeof(CtdlNodeConf));

	Node->DeleteMe = 0;

	Node->NodeName=NewStrBuf();
	StrBufExtract_NextToken(Node->NodeName, SerializedNode, &Pos, '|');

	Node->Secret=NewStrBuf();
	StrBufExtract_NextToken(Node->Secret, SerializedNode, &Pos, '|');

	Node->Host=NewStrBuf();
	StrBufExtract_NextToken(Node->Host, SerializedNode, &Pos, '|');

	Node->Port=NewStrBuf();
	StrBufExtract_NextToken(Node->Port, SerializedNode, &Pos, '|');
	return Node;
}


/*
 * Load or refresh the Citadel network (IGnet) configuration for this node.
 */
HashList* CtdlLoadIgNetCfg(void)
{
	const char *LinePos;
	char       *Cfg;
	StrBuf     *Buf;
	StrBuf     *LineBuf;
	HashList   *Hash;
	CtdlNodeConf   *Node;

	Cfg =  CtdlGetSysConfig(IGNETCFG);
	if ((Cfg == NULL) || IsEmptyStr(Cfg)) {
		if (Cfg != NULL)
			free(Cfg);
		return NULL;
	}

	Hash = NewHash(1, NULL);
	Buf = NewStrBufPlain(Cfg, -1);
	free(Cfg);
	LineBuf = NewStrBufPlain(NULL, StrLength(Buf));
	LinePos = NULL;
	do
	{
		StrBufSipLine(LineBuf, Buf, &LinePos);
		if (StrLength(LineBuf) != 0) {
			Node = NewNode(LineBuf);
			if (Node != NULL) {
				Put(Hash, SKEY(Node->NodeName), Node, DeleteCtdlNodeConf);
			}
		}
	} while (LinePos != StrBufNOTNULL);
	FreeStrBuf(&Buf);
	FreeStrBuf(&LineBuf);
	return Hash;
}


int is_recipient(OneRoomNetCfg *RNCfg, const char *Name)
{
	const RoomNetCfg RecipientCfgs[] = {
		listrecp,
		digestrecp,
		participate,
		maxRoomNetCfg
	};
	int i;
	RoomNetCfgLine *nptr;
	size_t len;
	
	len = strlen(Name);
	i = 0;
	while (RecipientCfgs[i] != maxRoomNetCfg)
	{
		nptr = RNCfg->NetConfigs[RecipientCfgs[i]];
		
		while (nptr != NULL)
		{
			if ((StrLength(nptr->Value[0]) == len) && 
			    (!strcmp(Name, ChrPtr(nptr->Value[0]))))
			{
				return 1;
			}
			nptr = nptr->next;
		}
		i++;
	}
	return 0;
}


int CtdlNetconfigCheckRoomaccess(char *errmsgbuf, size_t n, const char* RemoteIdentifier)
{
	OneRoomNetCfg *RNCfg;
	int found;

	if (RemoteIdentifier == NULL)
	{
		snprintf(errmsgbuf, n, "Need sender to permit access.");
		return (ERROR + USERNAME_REQUIRED);
	}

	begin_critical_section(S_NETCONFIGS);
	RNCfg = CtdlGetNetCfgForRoom (CC->room.QRnumber);
	if (RNCfg == NULL)
	{
		end_critical_section(S_NETCONFIGS);
		snprintf(errmsgbuf, n,
			 "This mailing list only accepts posts from subscribers.");
		return (ERROR + NO_SUCH_USER);
	}
	found = is_recipient (RNCfg, RemoteIdentifier);
	FreeRoomNetworkStruct(&RNCfg);
	end_critical_section(S_NETCONFIGS);

	if (found) {
		return (0);
	}
	else {
		snprintf(errmsgbuf, n,
			 "This mailing list only accepts posts from subscribers.");
		return (ERROR + NO_SUCH_USER);
	}
}


/*-----------------------------------------------------------------------------*
 *                 Network maps: evaluate other nodes                          *
 *-----------------------------------------------------------------------------*/

void DeleteNetMap(void *vNetMap)
{
	CtdlNetMap *TheNetMap = (CtdlNetMap*) vNetMap;
	FreeStrBuf(&TheNetMap->NodeName);
	FreeStrBuf(&TheNetMap->NextHop);
	free(TheNetMap);
}


CtdlNetMap *NewNetMap(StrBuf *SerializedNetMap)
{
	const char *Pos = NULL;
	CtdlNetMap *NM;

	/* we need at least 3 pipes and some other text so its invalid. */
	if (StrLength(SerializedNetMap) < 6)
		return NULL;
	NM = (CtdlNetMap *) malloc(sizeof(CtdlNetMap));

	NM->NodeName=NewStrBuf();
	StrBufExtract_NextToken(NM->NodeName, SerializedNetMap, &Pos, '|');

	NM->lastcontact = StrBufExtractNext_long(SerializedNetMap, &Pos, '|');

	NM->NextHop=NewStrBuf();
	StrBufExtract_NextToken(NM->NextHop, SerializedNetMap, &Pos, '|');

	return NM;
}


HashList* CtdlReadNetworkMap(void)
{
	const char *LinePos;
	char       *Cfg;
	StrBuf     *Buf;
	StrBuf     *LineBuf;
	HashList   *Hash;
	CtdlNetMap     *TheNetMap;

	Hash = NewHash(1, NULL);
	Cfg =  CtdlGetSysConfig(IGNETMAP);
	if ((Cfg == NULL) || IsEmptyStr(Cfg)) {
		if (Cfg != NULL)
			free(Cfg);
		return Hash;
	}

	Buf = NewStrBufPlain(Cfg, -1);
	free(Cfg);
	LineBuf = NewStrBufPlain(NULL, StrLength(Buf));
	LinePos = NULL;
	while (StrBufSipLine(Buf, LineBuf, &LinePos))
	{
		TheNetMap = NewNetMap(LineBuf);
		if (TheNetMap != NULL) { /* TODO: is the NodeName Uniq? */
			Put(Hash, SKEY(TheNetMap->NodeName), TheNetMap, DeleteNetMap);
		}
	}
	FreeStrBuf(&Buf);
	FreeStrBuf(&LineBuf);
	return Hash;
}


StrBuf *CtdlSerializeNetworkMap(HashList *Map)
{
	void *vMap;
	const char *key;
	long len;
	StrBuf *Ret = NewStrBuf();
	HashPos *Pos = GetNewHashPos(Map, 0);

	while (GetNextHashPos(Map, Pos, &len, &key, &vMap))
	{
		CtdlNetMap *pMap = (CtdlNetMap*) vMap;
		StrBufAppendBuf(Ret, pMap->NodeName, 0);
		StrBufAppendBufPlain(Ret, HKEY("|"), 0);

		StrBufAppendPrintf(Ret, "%ld", pMap->lastcontact, 0);
		StrBufAppendBufPlain(Ret, HKEY("|"), 0);

		StrBufAppendBuf(Ret, pMap->NextHop, 0);
		StrBufAppendBufPlain(Ret, HKEY("\n"), 0);
	}
	DeleteHashPos(&Pos);
	return Ret;
}


/*
 * Convert any legacy configuration files in the "netconfigs" directory
 */
void convert_legacy_netcfg_files(void)
{
	DIR *dh = NULL;
	struct dirent *dit = NULL;
	char filename[PATH_MAX];
	long roomnum;
	FILE *fp;
	long len;
	char *v;

	dh = opendir(ctdl_netcfg_dir);
	if (!dh) return;

	syslog(LOG_INFO, "netconfig: legacy netconfig files exist - converting them!");

	while (dit = readdir(dh), dit != NULL)			// yes, we use the non-reentrant version; we're not in threaded mode yet
	{
		roomnum = atol(dit->d_name);
		if (roomnum > 0) {
			snprintf(filename, sizeof filename, "%s/%ld", ctdl_netcfg_dir, roomnum);
			fp = fopen(filename, "r");
			if (fp)
			{
				fseek(fp, 0L, SEEK_END);
				len = ftell(fp);
				if (len > 0)
				{
					v = malloc(len);
					if (v)
					{
						rewind(fp);
						if (fread(v, len, 1, fp))
						{
							write_netconfig_to_configdb(roomnum, v);
							unlink(filename);
						}
						free(v);
					}
				}
				else
				{
					unlink(filename);	// zero length netconfig, just delete it
				}
				fclose(fp);
			}
		}
	}

	closedir(dh);
	rmdir(ctdl_netcfg_dir);
}


/*
 * Module entry point
 */
CTDL_MODULE_INIT(netconfig)
{
	if (!threading)
	{
		convert_legacy_netcfg_files();
		CtdlRegisterProtoHook(cmd_gnet, "GNET", "Get network config");
		CtdlRegisterProtoHook(cmd_snet, "SNET", "Set network config");
	}
	return "netconfig";
}
