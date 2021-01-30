#ifndef __CITADEL_DIRS_H
#define __CITADEL_DIRS_H

#include <limits.h>

/* all our directories */
extern char *ctdl_home_directory;
extern char *ctdl_db_dir;
extern char *ctdl_file_dir;
extern char *ctdl_shared_dir;
extern char *ctdl_image_dir;
extern char *ctdl_info_dir;
extern char *ctdl_key_dir;
extern char *ctdl_message_dir;
extern char *ctdl_usrpic_dir;
extern char *ctdl_autoetc_dir;
extern char *ctdl_run_dir;
extern char *ctdl_netdigest_dir;
extern char *ctdl_netcfg_dir;
extern char *ctdl_bbsbase_dir;
extern char *ctdl_sbin_dir;
extern char *ctdl_bin_dir;
extern char *ctdl_utilbin_dir;

/* some of the frequently used files */
extern char *file_citadel_config;
extern char *file_lmtp_socket;
extern char *file_lmtp_unfiltered_socket;
extern char *file_arcq;
extern char *file_citadel_socket;
extern char *file_citadel_admin_socket;
extern char *file_pid_file;
extern char *file_pid_paniclog;
extern char *file_crpt_file_key;
extern char *file_crpt_file_csr;
extern char *file_crpt_file_cer;
extern char *file_chkpwd;
extern char *file_guesstimezone;

/* externs */
extern int create_run_directories(long UID, long GUID);
extern size_t assoc_file_name(char *buf, size_t n, struct ctdlroom *qrbuf, const char *prefix);
extern FILE *create_digest_file(struct ctdlroom *room, int forceCreate);
extern void remove_digest_file(struct ctdlroom *room);

#endif /* __CITADEL_DIRS_H */
