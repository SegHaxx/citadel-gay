/*
 * Copyright (c) 1996-2020 by the citadel.org team
 *
 * This program is open source software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * 
 * Implementation note: this was kind of hacked up when we switched from Sieve to custom rules.
 * As a result there's probably some cruft in here...
 * ajc 2020jul12
 *
 */

#include "webcit.h"

CtxType CTX_SIEVELIST = CTX_NONE;
CtxType CTX_SIEVESCRIPT = CTX_NONE;

#define MAX_SCRIPTS	100
#define MAX_RULES	50
#define RULES_SCRIPT	"__WebCit_Generated_Script__"


/*
 * Translate the fields from the rule editor into something we can save...
 */
void parse_fields_from_rule_editor(void) {

	int active;
	char hfield[256];
	char compare[32];
	char htext[256];
	char sizecomp[32];
	int sizeval;
	char action[32];
	char fileinto[128];
	char redirect[256];
	char automsg[1024];
	char final[32];
	int i;
	char buf[256];
	char fname[256];
	char rule[2048];
	char encoded_rule[4096];
	char my_addresses[4096];
	
	/* Enumerate my email addresses in case they are needed for a vacation rule */
	my_addresses[0] = 0;
	serv_puts("GVEA");
	serv_getln(buf, sizeof buf);
	if (buf[0] == '1') while (serv_getln(buf, sizeof buf), strcmp(buf, "000")) {
		if (!IsEmptyStr(my_addresses)) {
			strcat(my_addresses, ",\n");
		}
		strcat(my_addresses, "\"");
		strcat(my_addresses, buf);
		strcat(my_addresses, "\"");
	}

	/* Now generate the script and write it to the Citadel server */
	serv_printf("PIBR");
	serv_getln(buf, sizeof buf);
	if (buf[0] != '4') {
		return;
	}

	for (i=0; i<MAX_RULES; ++i) {
		
		strcpy(rule, "");

		sprintf(fname, "active%d", i);
		active = !strcasecmp(BSTR(fname), "on") ;

		if (active) {

			sprintf(fname, "hfield%d", i);
			safestrncpy(hfield, BSTR(fname), sizeof hfield);
	
			sprintf(fname, "compare%d", i);
			safestrncpy(compare, BSTR(fname), sizeof compare);
	
			sprintf(fname, "htext%d", i);
			safestrncpy(htext, BSTR(fname), sizeof htext);
	
			sprintf(fname, "sizecomp%d", i);
			safestrncpy(sizecomp, BSTR(fname), sizeof sizecomp);
	
			sprintf(fname, "sizeval%d", i);
			sizeval = IBSTR(fname);
	
			sprintf(fname, "action%d", i);
			safestrncpy(action, BSTR(fname), sizeof action);
	
			sprintf(fname, "fileinto%d", i);
			safestrncpy(fileinto, BSTR(fname), sizeof fileinto);
	
			sprintf(fname, "redirect%d", i);
			safestrncpy(redirect, BSTR(fname), sizeof redirect);
	
			sprintf(fname, "automsg%d", i);
			safestrncpy(automsg, BSTR(fname), sizeof automsg);
	
			sprintf(fname, "final%d", i);
			safestrncpy(final, BSTR(fname), sizeof final);
	
			snprintf(rule, sizeof rule, "%d|%s|%s|%s|%s|%d|%s|%s|%s|%s|%s",
				active, hfield, compare, htext, sizecomp, sizeval, action, fileinto,
				redirect, automsg, final
			);
	
			size_t len = CtdlEncodeBase64(encoded_rule, rule, strlen(rule)+1, BASE64_NO_LINEBREAKS);
			if (encoded_rule[len - 1] == '\n') {
				encoded_rule[len - 1] = '\0';
			}
			serv_printf("rule|%d|%s|", i, encoded_rule);
			serv_puts("");
		}
	}

	serv_puts("000");
}


/*
 * save sieve config
 */
void save_sieve(void) {

	if (!havebstr("save_button")) {
		AppendImportantMessage(_("Cancelled.  Changes were not saved."), -1);
		display_main_menu();
		return;
	}

	parse_fields_from_rule_editor();

	AppendImportantMessage(_("Your changes have been saved."), -1);
	display_main_menu();
	return;
}


void display_sieve_add_or_delete(void) {
	output_headers(1, 1, 2, 0, 0, 0);
	do_template("sieve_add");
	wDumpContent(1);
}



/*
 * dummy panel indicating to the user that the server doesn't support Sieve
 */
void display_no_sieve(void) {

	output_headers(1, 1, 1, 0, 0, 0);
	do_template("sieve_none");
	wDumpContent(1);
}


typedef struct __SieveListing {
	int IsActive;
	int IsRulesScript;
	StrBuf *Name;
	StrBuf *Content;
} SieveListing;

int ConditionalSieveScriptIsActive(StrBuf *Target, WCTemplputParams *TP)
{
	SieveListing     *SieveList = (SieveListing *)CTX(CTX_SIEVELIST);
	return SieveList->IsActive;
}
int ConditionalSieveScriptIsRulesScript(StrBuf *Target, WCTemplputParams *TP)
{
	SieveListing     *SieveList = (SieveListing *)CTX(CTX_SIEVELIST);
	return SieveList->IsActive;
}
void tmplput_SieveScriptName(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveListing     *SieveList = (SieveListing *)CTX(CTX_SIEVELIST);
	StrBufAppendTemplate(Target, TP, SieveList->Name, 0);
}
void tmplput_SieveScriptContent(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveListing     *SieveList = (SieveListing *)CTX(CTX_SIEVELIST);
	StrBufAppendTemplate(Target, TP, SieveList->Content, 0);
}
void FreeSieveListing(void *vSieveListing)
{
	SieveListing *List = (SieveListing*) vSieveListing;

	FreeStrBuf(&List->Name);
	free(List);
}

HashList *GetSieveScriptListing(StrBuf *Target, WCTemplputParams *TP)
{
        wcsession *WCC = WC;
	StrBuf *Line;
	int num_scripts = 0;
	int rules_script_active = 0;
	int have_rules_script = 0;
	const char *pch;
	HashPos  *it;
	int Done = 0;
	SieveListing *Ruleset;

	if (WCC->KnownSieveScripts != NULL) {
		return WCC->KnownSieveScripts;
	}

	serv_puts("MSIV listscripts");
	Line = NewStrBuf();
	StrBuf_ServGetln(Line);
	if (GetServerStatus(Line, NULL) == 1) 
	{
		WCC->KnownSieveScripts = NewHash(1, Flathash);

		while(!Done && (StrBuf_ServGetln(Line) >= 0) )
			if ( (StrLength(Line)==3) && 
			     !strcmp(ChrPtr(Line), "000")) 
			{
				Done = 1;
			}
			else
			{
				pch = NULL;
				Ruleset = (SieveListing *) malloc(sizeof(SieveListing));
				Ruleset->Name = NewStrBufPlain(NULL, StrLength(Line));
				StrBufExtract_NextToken(Ruleset->Name, Line, &pch, '|');
				Ruleset->IsActive = StrBufExtractNext_int(Line, &pch, '|'); 
				Ruleset->Content = NULL;

				if (!strcasecmp(ChrPtr(Ruleset->Name), RULES_SCRIPT))
				{
					Ruleset->IsRulesScript = 1;
					have_rules_script = 1;
					if (Ruleset->IsActive)
					{
						rules_script_active = 1;
						PutBstr(HKEY("__SIEVE:RULESSCRIPT"), NewStrBufPlain(HKEY("1")));
					}
				}
				Put(WCC->KnownSieveScripts, IKEY(num_scripts), Ruleset, FreeSieveListing);

				++num_scripts;
			}
	}

	if ((num_scripts > 0) && (rules_script_active == 0)) {
		PutBstr(HKEY("__SIEVE:EXTERNAL_SCRIPT"), NewStrBufPlain(HKEY("1")));
	}

	if (num_scripts > have_rules_script)
	{
		long rc = 0;
		long len;
		const char *Key;
		void *vRuleset;

		/* 
		 * ok; we have custom scripts, expose that via bstr, and load the payload.
		 */
		PutBstr(HKEY("__SIEVE:HAVE_EXTERNAL_SCRIPT"), NewStrBufPlain(HKEY("1")));

		it = GetNewHashPos(WCC->KnownSieveScripts, 0);
		while (GetNextHashPos(WCC->KnownSieveScripts, it, &len, &Key, &vRuleset) && 
		       (vRuleset != NULL))
		{
			Ruleset = (SieveListing *) vRuleset;
			serv_printf("MSIV getscript|%s", ChrPtr(Ruleset->Name));
			StrBuf_ServGetln(Line);
			if (GetServerStatus(Line, NULL) == 1) 
			{
				Ruleset->Content = NewStrBuf();
				Done = 0;
				while(!Done && (rc = StrBuf_ServGetln(Line), rc >= 0) )
					if ( (StrLength(Line)==3) && 
					     !strcmp(ChrPtr(Line), "000")) 
					{
						Done = 1;
					}
					else
					{
						if (StrLength(Ruleset->Content)>0)
							StrBufAppendBufPlain(Ruleset->Content, HKEY("\n"), 0);
						StrBufAppendBuf(Ruleset->Content, Line, 0);
					}
				if (rc < 0) break;
			}
		}
	}
	FreeStrBuf(&Line);
	return WCC->KnownSieveScripts;
}


typedef enum __eSieveHfield 
{
	from,		
	tocc,		
	subject,	
	replyto,	
	sender,	
	resentfrom,	
	resentto,	
	envfrom,	
	envto,	
	xmailer,	
	xspamflag,	
	xspamstatus,	
	listid,	
	size,		
	all
} eSieveHfield;

typedef enum __eSieveCompare {
	contains,
	notcontains,
	is,
	isnot,
	matches,
	notmatches
} eSieveCompare;

typedef enum __eSieveAction {
	keep,
	discard,
	reject,
	fileinto,
	redirect,
	vacation
} eSieveAction;


typedef enum __eSieveSizeComp {
	larger,
	smaller
} eSieveSizeComp;

typedef enum __eSieveFinal {
	econtinue,
	estop
} eSieveFinal;


typedef struct __SieveRule {
	int active;
	int sizeval;
	eSieveHfield hfield;
	eSieveCompare compare;
	StrBuf *htext;
	eSieveSizeComp sizecomp;
	eSieveAction Action;
	StrBuf *fileinto;
	StrBuf *redirect;
	StrBuf *automsg;
	eSieveFinal final;
}SieveRule;



int ConditionalSieveRule_hfield(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
	
        return GetTemplateTokenNumber(Target, 
                                      TP, 
                                      3, 
                                      from)
                ==
                Rule->hfield;
}
int ConditionalSieveRule_compare(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        return GetTemplateTokenNumber(Target, 
                                      TP, 
                                      3, 
                                      contains)
                ==
		Rule->compare;
}
int ConditionalSieveRule_action(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        return GetTemplateTokenNumber(Target, 
                                      TP, 
                                      3, 
                                      keep)
                ==
		Rule->Action; 
}
int ConditionalSieveRule_sizecomp(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        return GetTemplateTokenNumber(Target, 
                                      TP, 
                                      3, 
                                      larger)
                ==
		Rule->sizecomp;
}
int ConditionalSieveRule_final(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        return GetTemplateTokenNumber(Target, 
                                      TP, 
                                      3, 
                                      econtinue)
                ==
		Rule->final;
}
int ConditionalSieveRule_ThisRoom(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        return GetTemplateTokenNumber(Target, 
                                      TP, 
                                      3, 
                                      econtinue)
                ==
		Rule->final;
}
int ConditionalSieveRule_Active(StrBuf *Target, WCTemplputParams *TP)
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        return Rule->active;
}
void tmplput_SieveRule_htext(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
	StrBufAppendTemplate(Target, TP, Rule->htext, 0);
}
void tmplput_SieveRule_fileinto(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
	StrBufAppendTemplate(Target, TP, Rule->fileinto, 0);
}
void tmplput_SieveRule_redirect(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
	StrBufAppendTemplate(Target, TP, Rule->redirect, 0);
}
void tmplput_SieveRule_automsg(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
	StrBufAppendTemplate(Target, TP, Rule->automsg, 0);
}
void tmplput_SieveRule_sizeval(StrBuf *Target, WCTemplputParams *TP) 
{
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
	StrBufAppendPrintf(Target, "%d", Rule->sizeval);
}

void tmplput_SieveRule_lookup_FileIntoRoom(StrBuf *Target, WCTemplputParams *TP) 
{
	void *vRoom;
	SieveRule     *Rule = (SieveRule *)CTX(CTX_SIEVESCRIPT);
        wcsession *WCC = WC;
	HashList *Rooms = GetRoomListHashLKRA(Target, TP);

	GetHash(Rooms, SKEY(Rule->fileinto), &vRoom);
	WCC->ThisRoom = (folder*) vRoom;
}

void FreeSieveRule(void *vRule)
{
	SieveRule *Rule = (SieveRule*) vRule;

	FreeStrBuf(&Rule->htext);
	FreeStrBuf(&Rule->fileinto);
	FreeStrBuf(&Rule->redirect);
	FreeStrBuf(&Rule->automsg);
	
	free(Rule);
}

#define WC_RULE_HEADER "rule|"
HashList *GetSieveRules(StrBuf *Target, WCTemplputParams *TP)
{
	StrBuf *Line = NULL;
	StrBuf *EncodedRule = NULL;
	int n = 0;
	const char *pch = NULL;
	HashList *SieveRules = NULL;
	int Done = 0;
	SieveRule *Rule = NULL;

	SieveRules = NewHash(1, Flathash);
	serv_printf("GIBR");
	Line = NewStrBuf();
	EncodedRule = NewStrBuf();
	StrBuf_ServGetln(Line);
	if (GetServerStatus(Line, NULL) == 1) 
	{
		while(!Done && (StrBuf_ServGetln(Line) >= 0) )
			if ( (StrLength(Line)==3) && 
			     !strcmp(ChrPtr(Line), "000")) 
			{
				Done = 1;
			}
			else
			{
				pch = NULL;
				/* We just care for our encoded header and skip everything else */
				if ((StrLength(Line) > sizeof(WC_RULE_HEADER) - 1) && (!strncasecmp(ChrPtr(Line), HKEY(WC_RULE_HEADER))))
				{
					StrBufSkip_NTokenS(Line, &pch, '|', 1);
					n = StrBufExtractNext_int(Line, &pch, '|'); 
					StrBufExtract_NextToken(EncodedRule, Line, &pch, '|');
					StrBufDecodeBase64(EncodedRule);

					Rule = (SieveRule*) malloc(sizeof(SieveRule));

					Rule->htext = NewStrBufPlain (NULL, StrLength(EncodedRule));

					Rule->fileinto = NewStrBufPlain (NULL, StrLength(EncodedRule));
					Rule->redirect = NewStrBufPlain (NULL, StrLength(EncodedRule));
					Rule->automsg = NewStrBufPlain (NULL, StrLength(EncodedRule));

					/* Grab our existing values to populate */
					pch = NULL;
					Rule->active = StrBufExtractNext_int(EncodedRule, &pch, '|');
					StrBufExtract_NextToken(Line, EncodedRule, &pch, '|');
					
					Rule->hfield = (eSieveHfield) GetTokenDefine(SKEY(Line), tocc);
					StrBufExtract_NextToken(Line, EncodedRule, &pch, '|');
					Rule->compare = (eSieveCompare) GetTokenDefine(SKEY(Line), contains);
					StrBufExtract_NextToken(Rule->htext, EncodedRule, &pch, '|');
					StrBufExtract_NextToken(Line, EncodedRule, &pch, '|');
					Rule->sizecomp = (eSieveSizeComp) GetTokenDefine(SKEY(Line), larger);
					Rule->sizeval = StrBufExtractNext_int(EncodedRule, &pch, '|');
					StrBufExtract_NextToken(Line, EncodedRule, &pch, '|');
					Rule->Action = (eSieveAction) GetTokenDefine(SKEY(Line), keep);
					StrBufExtract_NextToken(Rule->fileinto, EncodedRule, &pch, '|');
					StrBufExtract_NextToken(Rule->redirect, EncodedRule, &pch, '|');
					StrBufExtract_NextToken(Rule->automsg, EncodedRule, &pch, '|');
					StrBufExtract_NextToken(Line, EncodedRule, &pch, '|');
					Rule->final = (eSieveFinal) GetTokenDefine(SKEY(Line), econtinue);
					Put(SieveRules, IKEY(n), Rule, FreeSieveRule);
					n++;
				}
			}
	}

	while (n < MAX_RULES) {
		Rule = (SieveRule*) malloc(sizeof(SieveRule));
		memset(Rule, 0, sizeof(SieveRule));
		Put(SieveRules, IKEY(n), Rule, FreeSieveRule);
	    
		n++;
	}


	FreeStrBuf(&EncodedRule);
	FreeStrBuf(&Line);
	return SieveRules;
}

void
SessionDetachModule_SIEVE
(wcsession *sess)
{
	DeleteHash(&sess->KnownSieveScripts);
}

void 
InitModule_SIEVE
(void)
{
	RegisterCTX(CTX_SIEVELIST);
	RegisterCTX(CTX_SIEVESCRIPT);
	REGISTERTokenParamDefine(from);		
	REGISTERTokenParamDefine(tocc);		
	REGISTERTokenParamDefine(subject);	
	REGISTERTokenParamDefine(replyto);	
	REGISTERTokenParamDefine(sender);	
	REGISTERTokenParamDefine(resentfrom);	
	REGISTERTokenParamDefine(resentto);	
	REGISTERTokenParamDefine(envfrom);	
	REGISTERTokenParamDefine(envto);	
	REGISTERTokenParamDefine(xmailer);	
	REGISTERTokenParamDefine(xspamflag);	
	REGISTERTokenParamDefine(xspamstatus);	
	REGISTERTokenParamDefine(listid);	
	REGISTERTokenParamDefine(size);		
	REGISTERTokenParamDefine(all);

	REGISTERTokenParamDefine(contains);
	REGISTERTokenParamDefine(notcontains);
	REGISTERTokenParamDefine(is);
	REGISTERTokenParamDefine(isnot);
	REGISTERTokenParamDefine(matches);
	REGISTERTokenParamDefine(notmatches);

	REGISTERTokenParamDefine(keep);
	REGISTERTokenParamDefine(discard);
	REGISTERTokenParamDefine(reject);
	REGISTERTokenParamDefine(fileinto);
	REGISTERTokenParamDefine(redirect);
	REGISTERTokenParamDefine(vacation);

	REGISTERTokenParamDefine(larger);
	REGISTERTokenParamDefine(smaller);

	/* these are c-keyworads, so do it by hand. */
	RegisterTokenParamDefine(HKEY("continue"), econtinue);
	RegisterTokenParamDefine(HKEY("stop"), estop);

	RegisterIterator("SIEVE:SCRIPTS", 0, NULL, GetSieveScriptListing, NULL, NULL, CTX_SIEVELIST, CTX_NONE, IT_NOFLAG);

	RegisterConditional("COND:SIEVE:SCRIPT:ACTIVE", 0, ConditionalSieveScriptIsActive, CTX_SIEVELIST);
	RegisterConditional("COND:SIEVE:SCRIPT:ISRULES", 0, ConditionalSieveScriptIsRulesScript, CTX_SIEVELIST);
	RegisterNamespace("SIEVE:SCRIPT:NAME", 0, 1, tmplput_SieveScriptName, NULL, CTX_SIEVELIST);
	RegisterNamespace("SIEVE:SCRIPT:CONTENT", 0, 1, tmplput_SieveScriptContent, NULL, CTX_SIEVELIST);

 
	RegisterIterator("SIEVE:RULES", 0, NULL, GetSieveRules, NULL, DeleteHash, CTX_SIEVESCRIPT, CTX_NONE, IT_NOFLAG);

	RegisterConditional("COND:SIEVE:ACTIVE", 1, ConditionalSieveRule_Active, CTX_SIEVESCRIPT);
	RegisterConditional("COND:SIEVE:HFIELD", 1, ConditionalSieveRule_hfield, CTX_SIEVESCRIPT);
	RegisterConditional("COND:SIEVE:COMPARE", 1, ConditionalSieveRule_compare, CTX_SIEVESCRIPT);
	RegisterConditional("COND:SIEVE:ACTION", 1, ConditionalSieveRule_action, CTX_SIEVESCRIPT);
	RegisterConditional("COND:SIEVE:SIZECOMP", 1, ConditionalSieveRule_sizecomp, CTX_SIEVESCRIPT);
	RegisterConditional("COND:SIEVE:FINAL", 1, ConditionalSieveRule_final, CTX_SIEVESCRIPT);
	RegisterConditional("COND:SIEVE:THISROOM", 1, ConditionalSieveRule_ThisRoom, CTX_SIEVESCRIPT);

	RegisterNamespace("SIEVE:SCRIPT:HTEXT", 0, 1, tmplput_SieveRule_htext, NULL, CTX_SIEVESCRIPT);
	RegisterNamespace("SIEVE:SCRIPT:SIZE", 0, 1, tmplput_SieveRule_sizeval, NULL, CTX_SIEVESCRIPT);
	RegisterNamespace("SIEVE:SCRIPT:FILEINTO", 0, 1, tmplput_SieveRule_fileinto, NULL, CTX_SIEVESCRIPT);
	RegisterNamespace("SIEVE:SCRIPT:REDIRECT", 0, 1, tmplput_SieveRule_redirect, NULL, CTX_SIEVESCRIPT);
	RegisterNamespace("SIEVE:SCRIPT:AUTOMSG", 0, 1, tmplput_SieveRule_automsg, NULL, CTX_SIEVESCRIPT);

	/* fetch our room into WCC->ThisRoom, to evaluate while iterating over rooms with COND:THIS:THAT:ROOM */
	RegisterNamespace("SIEVE:SCRIPT:LOOKUP_FILEINTO", 0, 1, tmplput_SieveRule_lookup_FileIntoRoom, NULL, CTX_SIEVESCRIPT);
	WebcitAddUrlHandler(HKEY("save_sieve"), "", 0, save_sieve, 0);
	WebcitAddUrlHandler(HKEY("display_sieve_add_or_delete"), "", 0, display_sieve_add_or_delete, 0);
}
