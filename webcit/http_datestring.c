#include "webcit.h"

#ifdef __FreeBSD__
/** I like to believe there is a better way to do this. */
#define HAVE_STRUCT_TM_TM_GMTOFF
#endif
/** HTTP Months - do not translate - these are not for human consumption */
static char *httpdate_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/** HTTP Weekdays - do not translate - these are not for human consumption */
static char *httpdate_weekdays[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};


/**
 * \brief Supplied with a unix timestamp, generate a textual time/date stamp
 * \param buf the return buffer
 * \param n the size of the buffer
 * \param xtime the time to format as string
 */
void http_datestring(char *buf, size_t n, time_t xtime) {
	struct tm t;

	long offset;
	char offsign;

	localtime_r(&xtime, &t);

	/** Convert "seconds west of GMT" to "hours/minutes offset" */
#ifdef HAVE_STRUCT_TM_TM_GMTOFF
	offset = t.tm_gmtoff;
#else
	offset = timezone;
#endif
	if (offset > 0) {
		offsign = '+';
	}
	else {
		offset = 0L - offset;
		offsign = '-';
	}
	offset = ( (offset / 3600) * 100 ) + ( offset % 60 );

	snprintf(buf, n, "%s, %02d %s %04d %02d:%02d:%02d %c%04ld",
		httpdate_weekdays[t.tm_wday],
		t.tm_mday,
		httpdate_months[t.tm_mon],
		t.tm_year + 1900,
		t.tm_hour,
		t.tm_min,
		t.tm_sec,
		offsign, offset
	);
}


void tmplput_nowstr(StrBuf *Target, WCTemplputParams *TP)
{
	char buf[64];
	long bufused;
	time_t now;
	
	now = time(NULL);
#ifdef HAVE_SOLARIS_LOCALTIME_R
	asctime_r(localtime(&now), buf, sizeof(buf));
#else
	asctime_r(localtime(&now), buf);
#endif
	bufused = strlen(buf);
	if ((bufused > 0) && (buf[bufused - 1] == '\n')) {
		buf[bufused - 1] = '\0';
		bufused --;
	}
	StrEscAppend(Target, NULL, buf, 0, 0);
}
void tmplput_nowno(StrBuf *Target, WCTemplputParams *TP)
{
	time_t now;
	now = time(NULL);
	StrBufAppendPrintf(Target, "%ld", now);
}

void 
InitModule_DATE
(void)
{
	RegisterNamespace("DATE:NOW:STR", 0, 0, tmplput_nowstr, NULL, CTX_NONE);
	RegisterNamespace("DATE:NOW:NO", 0, 0, tmplput_nowno, NULL, CTX_NONE);
}
