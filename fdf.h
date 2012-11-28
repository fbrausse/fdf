
#ifndef _H_FDF
#define _H_FDF

#include <stdio.h>

#ifdef _HAVE_GETTEXT
#define DOMAIN "fdf"
#include <libintl.h>
#define _(s) dgettext(DOMAIN, (s))
#else
#define _(s) (s)
#endif

typedef struct {
	bool single : 1;
	bool stay_on_fs : 1;
	bool follow_symlinks : 1;
	bool dirout : 1;
	bool preferred : 1;
	bool scan_symlinks : 1;
} flags_t;

typedef struct range_s {
	off_t lower;
	off_t upper;
	struct range_s *p;
	char *spec;
} range_t;

typedef struct {
	char *path;
	range_t **range;
	flags_t flags;
	unsigned int input_nr;
	
	off_t size;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	blksize_t blksize;
} fentry_s;

struct option_s {
	bool failfast : 1;
	bool zeroterm : 1;
	bool fname_match : 1;
	bool single_out : 1;
	enum { QUIET, NORMAL, VERBOSE, DEBUG } msglevel;
	FILE *output;
};

#ifndef _DECL_OPTS
extern struct option_s opts;
#endif

#ifndef _DECL_INTERRUPTED
extern volatile bool interrupted;
#endif

#define ERROR(...)	({ fprintf(stderr, __VA_ARGS__); })
#define INFO(...)	({ if (opts.msglevel >= NORMAL) fprintf(stderr, __VA_ARGS__); })
#define MSG(...)	({ if (opts.msglevel >= VERBOSE) fprintf(stderr, __VA_ARGS__); })
#define DBG(...)	({ if (opts.msglevel >= DEBUG) fprintf(stderr, __VA_ARGS__); })

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define signum(a) (((a) < 0) ? -1 : ((a) > 0) ? 1 : 0)

bool check_list(dlst_t l, off_t size);
/*
void start_output();
void output_fe(const fentry_s *, bool pref);
void stop_output();
*/
void output_fes(const dlst_t l);
char * readlink_malloc(char *, ssize_t *);

#endif	/* ifndef _H_FDF */
