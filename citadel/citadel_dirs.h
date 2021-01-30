#ifndef __CITADEL_DIRS_H
#define __CITADEL_DIRS_H

#include <limits.h>

/* Fixed directory names (some of these are obsolete and used only for migration) */
#define ctdl_home_directory	"."
#define ctdl_db_dir		"data"
#define ctdl_file_dir		"files"
#define ctdl_shared_dir		"."
#define ctdl_image_dir		"images"
#define ctdl_info_dir		"info"
#define ctdl_key_dir		"keys"
#define ctdl_message_dir	"messages"
#define ctdl_usrpic_dir		"userpics"
#define ctdl_autoetc_dir	"."
#define ctdl_run_dir		"."
#define ctdl_netcfg_dir		"netconfigs"
#define ctdl_bbsbase_dir	"."
#define ctdl_sbin_dir		"."
#define ctdl_bin_dir		"."
#define ctdl_utilbin_dir	"."

/* Fixed file names (some of these are obsolete and used only for migration) */
#define file_citadel_config		"citadel.config"
#define file_lmtp_socket		"lmtp.socket"
#define file_lmtp_unfiltered_socket	"lmtp-unfiltered.socket"
#define file_arcq			"refcount_adjustments.dat"
#define file_citadel_socket		"citadel.socket"
#define file_citadel_admin_socket	"citadel-admin.socket"
#define file_pid_file			"/var/run/citserver.pid"
#define file_pid_paniclog		"panic.log"
#define file_crpt_file_key		"keys/citadel.key"
#define file_crpt_file_csr		"keys/citadel.csr"
#define file_crpt_file_cer		"keys/citadel.cer"
#define file_chkpwd			"chkpwd"
#define file_guesstimezone		"guesstimezone.sh"


/* externs */
extern int create_run_directories(long UID, long GUID);
extern size_t assoc_file_name(char *buf, size_t n, struct ctdlroom *qrbuf, const char *prefix);
extern FILE *create_digest_file(struct ctdlroom *room, int forceCreate);
extern void remove_digest_file(struct ctdlroom *room);

#endif /* __CITADEL_DIRS_H */
