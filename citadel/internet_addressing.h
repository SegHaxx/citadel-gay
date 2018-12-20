
#include "server.h"
#include "ctdl_module.h"

recptypes *validate_recipients(const char *recipients, const char *RemoteIdentifier, int Flags);
void free_recipients(recptypes *);
void process_rfc822_addr(const char *rfc822, char *user, char *node, char *name);
char *rfc822_fetch_field(const char *rfc822, const char *fieldname);
void sanitize_truncated_recipient(char *str);
char *qp_encode_email_addrs(char *source);
int alias (char *name);
int IsDirectory(char *addr, int allow_masq_domains);
void CtdlRebuildDirectoryIndex(void);
int CtdlDirectoryAddUser(char *internet_addr, char *citadel_addr);
int CtdlDirectoryDelUser(char *internet_addr, char *citadel_addr);
int CtdlDirectoryLookup(char *target, char *internet_addr, size_t targbuflen);
void CtdlSetEmailAddressesForUser(char *requested_user, char *new_emailaddrs);
void AutoGenerateEmailAddressForUser(struct ctdluser *user);
struct CtdlMessage *convert_internet_message(char *rfc822);
struct CtdlMessage *convert_internet_message_buf(StrBuf **rfc822);
int CtdlIsMe(char *addr, int addr_buf_len);
int CtdlHostAlias(char *fqdn);
char *harvest_collected_addresses(struct CtdlMessage *msg);

/* 
 * Values that can be returned by CtdlHostAlias()
 */
enum {
	hostalias_nomatch,
	hostalias_localhost,
	hostalias_masq
};

extern char *inetcfg;
