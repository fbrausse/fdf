
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdint.h>		/* for uint64_t */
#include <limits.h>		/* for ULLONG_MAX */
#include <signal.h>		/* for signal() */

#include "dl_list.h"

#define _DECL_OPTS
#define _DECL_INTERRUPTED
#include "fdf.h"

volatile bool interrupted = false;
struct option_s opts = { false, false, false, false, NORMAL, NULL };

extern int errno;

#define PRINT_BUF_SIZE 256
static char print_buf[PRINT_BUF_SIZE];

char *format_size(off_t s) {
	static const char unit[] = { 'b', 'k', 'm', 'g', 't', 'p', 'e', 'z', 'y' };
	unsigned int i;
	long double d = s;
	for (i = 0; i < sizeof(unit) / sizeof(unit[0]) && d >= 10240.0; i++)
		d /= 1024.0;
	if (i) {
		off_t t = (off_t)d;
		if ((long double)t == d) {
			snprintf(print_buf, PRINT_BUF_SIZE, "%ju%c", (intmax_t)t, unit[i]);
		} else {
			snprintf(print_buf, PRINT_BUF_SIZE, "%.1Lf%c", d, unit[i]);
		}
	} else {
		snprintf(print_buf, PRINT_BUF_SIZE, "%ju%c", (intmax_t)s, unit[i]);
	}
	return print_buf;
}

static char * path_concat(const char *a, const size_t a_len, const char *b, const size_t b_len) {
	const bool needs_slash = (a[a_len - 1] != '/');
	size_t off = 0;
	char *dest_path = malloc(a_len + needs_slash + b_len + 1);
	if (!dest_path) {
		perror(_("error allocating memory"));
		return NULL;
	}
	strncpy(dest_path, a, a_len);
	off += a_len;
	if (needs_slash)
		dest_path[off++] = '/';
	strncpy(dest_path + off, b, b_len);
	off += b_len;
	dest_path[off] = '\0';
	return dest_path;
}

// todo: transform to var-args method m(size_t sum_lenpp, ...), sentinel value NULL for last char * argument
// TODO: externalize cwd into parameter
char * resolve_path_cwd(char *dir, size_t dir_n, char *name, size_t name_n) {
	size_t len = dir_n + name_n + 2 + 1;		// 2: first '/' and middle '/', 1: final '\0'
	char *r = calloc(len, sizeof(char));
	char *nr = r;
	bool abs = false;
	char *token;
	char *cur = dir;
	
start:
	abs |= *cur == '/';
	for (token = strtok(cur, "/"); token != NULL; token = strtok(NULL, "/")) {
		if (strncmp(token, ".", sizeof(".")) == 0) {
			continue;
		} else if (strncmp(token, "..", sizeof("..")) == 0) {
			if (nr == r) {
				if (abs)
					continue;
				char *wd = get_current_dir_name();
				size_t wd_n = strlen(wd);
				r = realloc(r, wd_n + len);
				memcpy(r, wd, wd_n);
				free(wd);
				nr = r + wd_n;
				abs = true;
			}
			DBG("moved from '%s' ", nr);
			while (nr > r) {
				nr--;
				if (*nr == '/')
					break;
			}
			DBG("to '%s'\n", nr);
		} else {
			if (nr > r || abs)
				*nr++ = '/';
			size_t token_n = strlen(token);
			memcpy(nr, token, token_n);
			nr += token_n;
			DBG("after adding '%s': %s\n", token, r);
		}
	}
	if (cur == dir && name != NULL) {
		cur = name;
		goto start;
	}
	
	if (nr == r && abs)
		*nr++ = '/';
	*nr = '\0';
	
	return r;
}

static inline fentry_s * new_fentry_s(char *path, flags_t flags, range_t **const range, unsigned int input_nr) {
	fentry_s *fe = malloc(sizeof(fentry_s));
	fe->path = path;
	fe->flags = flags;
	fe->range = range;
	fe->input_nr = input_nr;
	return fe;
}

static inline fentry_s * new_fentry_t(char *path, const fentry_s *fe) {
	return new_fentry_s(path, fe->flags, fe->range, fe->input_nr);
}

typedef struct {
	dev_t dev;
	ino_t ino;
} file_id_t;

static void file_id_free(void *p) {
	free(p);
}

#define VOID(...)

static lst_entry_t sort_insert(dlst_t l, dev_t fd, ino_t fi) {
	lst_entry_t en;
	dev_t d;
	ino_t i;
	for (
		en = l->api->last((lst_t)l);
		en != NULL;
		en = l->api->prev((lst_t)l, en)) {
		file_id_t *f = l->api->data((lst_t)l, en);
		VOID("round %d ", j++);
		d = f->dev;
		i = f->ino;
		if (d > fd) { VOID("1: %llu > %llu\n", d, fd); continue; }
		if (d < fd) { VOID("2: %llu < %llu", d, fd); break; }
		if (i > fi) { VOID("3: %llu > %llu\n", i, fi); continue; }
		VOID("4: dev: %llu, ino: %llu, (%d, %d, %d)", d, i, d == fd, i == fi, en != NULL && d == fd && i == fi);
		break;
	}
	VOID("\n");
	if (en != NULL && d == fd && i == fi) {
		return NULL;
	} else {
		file_id_t *fid = malloc(sizeof(file_id_t));
		fid->dev = fd;
		fid->ino = fi;
		return l->api->insert((lst_t)l, en, fid, en != NULL);
	}
}

typedef lst_entry_t (*ack_file_f)(dlst_t, dev_t, ino_t);
static ack_file_f ack_file = sort_insert;

static bool check_dir(dlst_t is, dlst_t srcs, const fentry_s *fe) {
	struct dirent* de;
	
	DIR *dir = opendir(fe->path);
	if (dir == NULL) {
		ERROR(_("error opening directory '%s': %s\n"), fe->path, strerror(errno));
		return false;
	}
	
	const char *src = fe->path;
	const size_t src_len = strlen(src);
	
	errno = 0;
	while ((de = readdir(dir)) != NULL) {
		const char *d_name = de->d_name;
		const size_t len = strlen(d_name);
		switch (len) {
			case 1:
			case 2:	// break when dirs[n] is '.' or '..'
				if (!strncmp("..", d_name, len))
					break;
				// fall-through otherwise
			default: {
				DBG(_("in dir: d_name '%s', src '%s'\n"), d_name, fe->path);
			//	dlst_insert_after(srcs, e, fe);
				srcs->api->push((lst_t)srcs, new_fentry_t(path_concat(src, src_len, d_name, len), fe));
			} break;
		}
	}
	bool r = true;
	if (errno != 0) {
		ERROR(_("error reading directory '%s': %s\n"), fe->path, strerror(errno));
		r = false;
	}
	
	int ret = closedir(dir);
	if (ret == -1) {
		ERROR(_("error closing directory '%s': %s\n"), fe->path, strerror(errno));
		r = false;
	}
	return r;
}

static bool check_sources(dlst_t is, dlst_t srcs, lst_entry_t e) {
	struct stat s;
	bool device_set = false;
	dev_t device = 0;
	lst_entry_t en;
	
	while (!interrupted && e && !srcs->api->empty((lst_t)srcs)) {
		fentry_s *fe = srcs->api->data((lst_t)srcs, e);
		if (lstat(fe->path, &s)) {
			ERROR(_("error accessing file '%s': %s\n"), fe->path, strerror(errno));
			goto err;
		}
		
		const char *type = NULL;
		const char *expl = NULL;
		
		if (!device_set) {
			device = s.st_dev;
			device_set = true;
		} else if (fe->flags.stay_on_fs && device != s.st_dev) {
			type = "";
			expl = _("not on same device as initial file");
			goto err_info;
		}
		
		fe->blksize = s.st_blksize;
		fe->size = s.st_size;
		fe->mode = s.st_mode;
		fe->gid = s.st_gid;
		fe->uid = s.st_uid;
		
		switch (s.st_mode & S_IFMT) {
			case S_IFSOCK:
				type = _("socket ");
				goto err_info;
			case S_IFLNK:
				// TODO: 1. und 2. if vertauschen, weil unabhängig, da "error reading symlink" für 'fdf -s ..'
				// en = NULL;
				if (fe->flags.follow_symlinks) {/*
					char *ln_path = malloc(fe->size + 1);
					ssize_t ret = readlink(fe->path, ln_path, fe->size);
					if (ret == -1) {
						ERROR(_("error reading symbolic link '%s': %s\n"), fe->path, strerror(errno));
						goto err;
					}
					ln_path[ret] = '\0';*/
					char *ln_path = realpath(fe->path, NULL);
					if (!ln_path) {
						ERROR(_("error resolving symbolic link '%s': %s\n"), fe->path, strerror(errno));
					} else {
						srcs->api->push((lst_t)srcs, new_fentry_t(ln_path, fe));
					}
				}
				if (!fe->flags.scan_symlinks) {
					type = _("symbolic link ");
					goto err_info;
				}
				break;
			case S_IFREG: // regular file
				break;
			case S_IFBLK:
				type = _("block device ");
				goto err_info;
			case S_IFDIR:
				if (fe->flags.single) {
					type = _("directory ");
					goto err_info;
				}
				if (is && ack_file != NULL && ack_file(is, s.st_dev, s.st_ino) == NULL) {
					INFO(_("won't scan double directory '%s'\n"), fe->path);
					goto err;
				}
				check_dir(is, srcs, fe);
				goto err;
			case S_IFCHR:
				type = _("character device ");
				goto err_info;
			case S_IFIFO:
				type = _("named pipe ");
				goto err_info;
			default:
				type = _("unknown file-type ");
				snprintf(print_buf, PRINT_BUF_SIZE, _("mode: %o"), s.st_mode);
				expl = print_buf;
				goto err_info;
		}
		
		if (fe->range) {
			range_t *r = *fe->range;
			if ((uint64_t)s.st_size < (uint64_t)r->lower || (uint64_t)s.st_size > (uint64_t)r->upper) {
				char *fsize = strdup(format_size(s.st_size));
				snprintf(print_buf, PRINT_BUF_SIZE, _("file-size %s is not in range %s"), fsize, r->spec);
				free(fsize);
				MSG(_("won't scan '%s': %s\n"), fe->path, print_buf);
				goto err;
			}
		}
		
		if (is && ack_file != NULL && ack_file(is, s.st_dev, s.st_ino) == NULL) {
			type = "double file ";
			goto err_info;
		}
		
		e = srcs->api->next((lst_t)srcs, e);
		continue;
		
err_info:
		if (expl) {
			INFO(_("won't scan %s'%s': %s\n"), type, fe->path, expl);
		} else {
			INFO(_("won't scan %s'%s'\n"), type, fe->path);
		}
err:
		en = srcs->api->next((lst_t)srcs, e);
		free(fe);
		srcs->api->remove((lst_t)srcs, e);
		e = en;
	}
	return true;
}

static bool read_stdin(dlst_t is, dlst_t srcs, flags_t flags, range_t **range, unsigned int input_nr, const bool zeroterm) {
	ssize_t read;
	char *line = NULL;
	size_t len = 0;
	char delim = (zeroterm) ? '\0' : '\n';
	while ((read = getdelim(&line, &len, delim, stdin)) != -1) {
		size_t tlen = read;
		if (!zeroterm && line[tlen - 1] == '\n')
			line[--tlen] = '\0';
		lst_entry_t e = srcs->api->push((lst_t)srcs, new_fentry_s(strndup(line, tlen), flags, range, input_nr));
		check_sources(is, srcs, e);
	}
	if (line)
		free(line);
	if (!feof(stdin)) {
		perror("getline()");
		return false;
	}
	return true;
}

static void fentry_free(void *v) {
	fentry_s *fe = v;
	free(fe->path);
	if (fe->range && *fe->range) {
		free((*fe->range)->spec);
		free(*fe->range);
		*fe->range = NULL;
	}
	free(fe);
}

static int comp_fentry(const void *v, const void *w) {
	off_t ov = (*((fentry_s **)v))->size;
	off_t ow = (*((fentry_s **)w))->size;
	return (ov < ow) ? -1 : (ov > ow) ? 1 : 0;
}

static int comp_fname(const void *v, const void *w) {
	fentry_s *fev = *(fentry_s **)v;
	fentry_s *few = *(fentry_s **)w;
	int r = strcmp(basename(fev->path), basename(few->path));
	return signum(r);
}

static void for_unique_range(
		fentry_s **entries, unsigned int from, size_t to, int (*comp)(const void *, const void *),
		void (*exec)(fentry_s **, unsigned int, unsigned int)) {
	unsigned int i = from;
	while (i < to && !interrupted) {
		unsigned int ss = i;
		int r = 0;
		while (r == 0 && ++i < to)
			r = comp(&(entries[ss]), &(entries[i]));
		
		if (i - ss > 1)
			exec(entries, ss, i);
	}
}

static void check_range(fentry_s **entries, unsigned int from, unsigned int to) {
	MSG(_("scan(entries, %d, %d)\n"), from, to);
	
	unsigned int i;
	dlst_t l;
	dlst.init((lst_t *)&l);
	fentry_s *fe = NULL;
	for (i=from; i<to; i++) {
		fe = entries[i];
		l->api->push((lst_t)l, fe);
		MSG(_("\t%s (%zu bytes, idx: %u)\n"), fe->path, (size_t)fe->size, i);
	}
	check_list(l, fe->size);
	
	l->api->free((lst_t *)&l);
}

static void check_range_fnames(fentry_s **entries, unsigned int from, unsigned int to) {
	DBG("qsorting .. from: %d, to: %d\n", from, to);
	qsort(&(entries[from]), to - from, sizeof(fentry_s *), comp_fname);
	for_unique_range(entries, from, to, comp_fname, check_range);
}

static bool check_files(fentry_s **entries, size_t len) {
	INFO(_("found %zu files...\n"), len);
	qsort(entries, len, sizeof(fentry_s *), comp_fentry);
	
	if (opts.msglevel == DEBUG) {
		unsigned int i;
		for (i=0; i<len; i++) {
			fentry_s *fe = entries[i];
			DBG("%zu\t%s\n", (size_t)fe->size, fe->path);
		}
	}
	
	for_unique_range(entries, 0, len, comp_fentry, (opts.fname_match) ? check_range_fnames : check_range);
	bool r = true;
	return r;
}

static FILE *out_file;
static bool output_started = false;
static unsigned int output_nr;

static void start_output() {
	if (!output_started) {
		output_started = true;
		INFO("Input#\tPref\tChunk#\tSize\tPath\n");
	}
	output_nr = 0;
}

static void output_fe(const fentry_s *fe, bool pref) {
	fprintf(out_file, "%d\t%c\t%d\t%s\t%s%c",
			fe->input_nr,
			(pref) ? 'P' : (fe->flags.preferred) ? 'p' : '.',
			output_nr++,
			format_size(fe->size),
			fe->path,
			(opts.zeroterm) ? '\0' : '\n');
}

static void stop_output() {
	fprintf(out_file, "%c", (opts.zeroterm) ? '\0' : '\n');
}

void output_fes(const dlst_t l) {
	size_t size = l->api->size((lst_t)l);
	if (size == 0)
		return;
	if ((l->api->size((lst_t)l) > 1) ^ opts.single_out) {
		lst_entry_t en;
		bool pfound[2] = { false, false };
		fentry_s *fe;
		
		start_output();
		
		for (en = l->api->first((lst_t)l); en != NULL; en = l->api->next((lst_t)l, en)) {
			fe = l->api->data((lst_t)l, en);
			pfound[fe->flags.preferred] = true;
		}
		
		if (pfound[false] && pfound[true]) {
			for (en = l->api->first((lst_t)l); en != NULL; en = l->api->next((lst_t)l, en)) {
				fe = l->api->data((lst_t)l, en);
				if (fe->flags.preferred)
					output_fe(fe, true);
			}
		} else {
			for (en = l->api->first((lst_t)l); en != NULL; en = l->api->next((lst_t)l, en)) {
				fe = l->api->data((lst_t)l, en);
				output_fe(fe, false);
			}
		}
		
		stop_output();
	}
}

void handler(int sig) {
	switch (sig) {
		case SIGINT:
		case SIGTERM:
			interrupted = true;
	}
	signal(sig, handler);
}

static void usage(char *);
static range_t *parse_range(char *);

extern char *optarg;
extern int optind, opterr, optopt;

int main(int argc, char **argv) {
	char *program_name = *argv;
	
	int r;
	int opt;
	fentry_s **entries = NULL;
	
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	
	dlst_t srcs, is = NULL;
	dlst.init((lst_t *)&srcs);
	
	out_file = stdout;
	
	bool inzeroterm = false;
	union {
		unsigned char val;
		flags_t flags;
	} f = { 0 };
	range_t *range = NULL;
	
	unsigned int input_nr = 1;
	
	bool ret;
	while ((opt = getopt(argc, argv, "-rsxl:cdpo:0zfSkvDqh")) != -1) {
		switch (opt) {
			case 1:
				if (f.flags.follow_symlinks)
					dlst.init((lst_t *)&is);
				if (!memcmp("-", optarg, sizeof("-"))) {
					ret = read_stdin(is, srcs, f.flags, (range) ? &range->p : NULL, input_nr, inzeroterm);
				} else {
					fentry_s *fe = new_fentry_s(strdup(optarg), f.flags, (range) ? &range->p : NULL, input_nr);
					lst_entry_t e = srcs->api->push((lst_t)srcs, fe);
					ret = check_sources(is, srcs, e);
				}
				inzeroterm = false;
				f.val = 0;
				range = NULL;
				if (is) {
					is->api->free_data((lst_t *)&is, file_id_free);
					is = NULL;
				}
				if (!ret && opts.failfast)
					goto error;
				input_nr++;
				break;
			case 'c': f.flags.scan_symlinks = true; break;
			case 'r': f.flags.single = true; break;
			case 's': f.flags.follow_symlinks = true; break;
			case 'x': f.flags.stay_on_fs = true; break;
			case 'l':
				errno = 0;
				range = parse_range(optarg);
				if (errno) {
					ERROR(_("invalid range '%s'\n"), optarg);
					if (range != NULL)
						free(range);
					goto error;
				} else if (range != NULL && (uint64_t)range->upper < (uint64_t)range->lower) {
					ERROR(_("upper < lower for range '%s'\n"), optarg);
					free(range);
					goto error;
				}
				range->p = range;
				range->spec = strdup(optarg);
				break;
			case 'd': f.flags.dirout = true; break;
			case 'p': f.flags.preferred = true; break;
			case 'o': 
				out_file = fopen(optarg, "w+");
				if (!out_file) {
					perror(_("error opening output-file"));
					goto error;
				}
				break;
			case '0': inzeroterm = true; break;
			case 'z': opts.zeroterm = true; break;
			case 'f': opts.fname_match = true; break;
			case 'S': opts.single_out = true; break;
			case 'k': opts.failfast = true; break;
			case 'v': opts.msglevel = VERBOSE; break;
			case 'D': opts.msglevel = DEBUG; break;
			case 'q': opts.msglevel = QUIET; break;
			case 'h': usage(program_name); goto end;
			default:
				fprintf(stderr, _("Use '-h' to view a list of all supported options\n"));
				goto error;
		}
	}
	
	if (dlst.empty((lst_t)srcs)) {
		fprintf(stderr, _("no files to check\n"));
		goto error;
	}
	
	size_t len = dlst.size((lst_t)srcs);
	entries = malloc(len * sizeof(fentry_s *));
	if (entries == NULL) {
		perror(_("memory allocation"));
		goto error;
	}
	lst_entry_t e;
	unsigned int i = 0;
	for (e = srcs->api->first((lst_t)srcs); e != NULL; e = srcs->api->next((lst_t)srcs, e))
		entries[i++] = srcs->api->data((lst_t)srcs, e);
	
	// todo: free entries
	
	if (!check_files(entries, len))
		goto error;
	
end:
	r = EXIT_SUCCESS;
	if (false)
error:	r = EXIT_FAILURE;
	
	if (out_file != stdout && fclose(out_file)) {
		perror(_("error closing output file"));
		r = EXIT_FAILURE;
	}
	free(entries);
	srcs->api->free_data((lst_t *)&srcs, fentry_free);
	return r;
}

static range_t *parse_range(char *s) {
	if (*s == '\0')
		return NULL;
	
	off_t lower = 0;
	off_t upper = ~(off_t)0;
	char *endptr;
	unsigned long long int val;
	if (*s != '-') {
		val = strtoull(s, &endptr, 0);
		unsigned int m;
		switch (*endptr) {
			case '-': m = 0; break;
			case 'b': case 'B': m =  0; endptr++; break;
			case 'k': case 'K': m = 10; endptr++; break;
			case 'm': case 'M': m = 20; endptr++; break;
			case 'g': case 'G': m = 30; endptr++; break;
			case 't': case 'T': m = 40; endptr++; break;
			default:
				errno = EINVAL;
				return NULL;
		}
		lower = min(ULLONG_MAX >> m, val) << m;
		s = endptr;
	}
	
	if (*s == '-') do {
		s++;
		if (*s == '\0') break;
		if (*s == '-') {
			errno = EINVAL;
			return NULL;
		}
		val = strtoull(s, &endptr, 0);
		unsigned int m;
		switch (*endptr) {
			case '\0': m = 0; break;
			case 'b': case 'B': m =  0; endptr++; break;
			case 'k': case 'K': m = 10; endptr++; break;
			case 'm': case 'M': m = 20; endptr++; break;
			case 'g': case 'G': m = 30; endptr++; break;
			case 't': case 'T': m = 40; endptr++; break;
			default:
				errno = EINVAL;
				return NULL;
		}
		if (*endptr != '\0') {
			errno = EINVAL;
			return NULL;
		}
		upper = min(ULLONG_MAX >> m, val) << m;
	} while (false);
	
	if (lower == (off_t)0 && upper == ~(off_t)0)
		return NULL;
	range_t *r = malloc(sizeof(range_t));
	r->lower = lower;
	r->upper = upper;
	return r;
}

static void usage(char *program_name) {
	puts(_("Test for double files"));
	printf(_("Usage: %s [OPTS] { - | FILE } [ [OPTS] FILE ... ]\n"), program_name);
	puts("");
	puts(_("Options affecting only the input specified subsequently:"));
	puts(_("  -r        don't recurse into directories"));
	puts(_("  -s        follow symbolic links (can't handle recusive links yet)"));
	puts(_("  -x        stay on the filesystem the input resides on (recursive mode)"));
	puts(_("  -l RANGE  limit files to scan to those whose size is in RANGE"));
	puts(_("\
  -c        scan content of symbolic links and identify doubles (if used with\n\
            the switch -s, only the contents of the first symlink encountered\n\
            will be scanned)"));/*
	puts(_("\
  -d       if tested directories contain >= 1 file and this file is tested\n\
           positively, output directory instead of all the contained file(s)"));*/
	puts(_("\
  -p        mark input as preferred, output only doubles from this input when\n\
            matching files from other inputs are found"));
	puts("");
	puts(_("\
RANGE is specified by [SIZE]-[SIZE], whereas SIZE is a positive integer which\n\
may be post-fixed by 'b', 'k', 'm', 'g', 't', for bytes, kilo-, mega-, giga-\n\
bytes, etc. The lower or upper limit can be omitted."));
	puts("");
	puts(_("Other options:"));
	puts(_("  -o FILE   redirect output to FILE"));
	puts(_("  -0        separate file names from stdin by 0x00-bytes instead of newline"));
	puts(_("  -z        when writing file names, separate by 0x00-bytes instead of newline"));
	puts(_("  -f        file names of doubles must match as well"));
	puts(_("  -S        reverse mode, output single files, which have no doubles"));
	puts(_("  -k        fail fast"));
	puts(_("  -v        verbose output"));
	puts(_("  -D        debug output (generally not wanted)"));
	puts(_("  -q        suppress any output (except for -o option)"));
	puts(_("  -h        shows this help message"));
	puts("");
	puts(_("\
This program is Free Software and released under the terms of the General Public\n\
License v2 by Franz Brausse (<dev@karlchenofhell.org>) in the hope to be useful."));
}
