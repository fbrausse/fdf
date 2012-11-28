
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "dl_list.h"
#include "fdf.h"

extern int errno;

typedef struct {
	dlst_t scan;		// list of centry_s, scanned up till pos and equal to one another
	lst_entry_t scan_from;
	off_t pos;			// position in files up to which scanning is completed up to now (valid only when scan is NULL), page_size-aligned
	unsigned char val;	// value at the above position
	bool eof : 1;		// determines, whether this branch is finished; this value is reset when new centry_s are added
} celink_t;

static void celink_free(void *c) {
	celink_t *cel = c;
	const lst_t l = (lst_t)cel->scan;
	if (l->api->size(l) == 0) {
		ERROR("freeing celink with zero sized scan-list o_O\n");
	} else {
		fentry_s *fe = l->api->data(l, l->api->first(l));
		DBG("freeing celink with first '%s' @ 0x%08x...\n", fe->path, (unsigned int)&(cel->scan));
	}
	cel->scan->api->free((lst_t *)&(cel->scan));
	free(cel);
}

static int memcmp_at(void *m1, void *m2, size_t len, ssize_t *at) {
	DBG("memcmp_at(0x%08x, 0x%08x, %u, 0x%08x)\n", (unsigned int)m1, (unsigned int)m2, len, (unsigned int)at);
	int ret = memcmp(m1, m2, len);
	if (ret == 0)
		return 0;
	
	size_t o_len = len;
	char *mc1 = m1, *mc2 = m2;
	while (len != 0) {
//		DBG("memcmp_at: testing idx %d\n", o_len - len);
		if (*mc1 != *mc2) {
			if (at)
				*at = o_len - len;
			return (int)*mc1 - (int)*mc2;
		}
		mc1++;
		mc2++;
		len--;
	}
	
	ERROR("memcmp failed to recognize the diff!\n");
	return 0;
}

static lst_entry_t insert(dlst_t l, lst_entry_t en, fentry_s *fe, off_t off, unsigned char val, bool lt) {
	lst_entry_t (*func)(lst_t, lst_entry_t) = (lt) ? dlst.prev : dlst.next;
	lst_entry_t cen;
	celink_t *cel;
	
	while ((en = func((lst_t)l, en)) != NULL) {
		cel = l->api->data((lst_t)l, en);
		
		if (cel->pos == off) {
			if (cel->val == val) {
				goto ins_old;
			} else if ((cel->val < val) ^ !lt) {
				break;
			}
		} else if ((cel->pos < off) ^ !lt) {
			break;
		}
	}
	
	cel = calloc(1, sizeof(celink_t));
	cel->pos = off;
	cel->val = val;
	DBG("init new list for cel @ 0x%08x\n", (unsigned int)&(cel->scan));
	dlst.init((lst_t *)&(cel->scan));
	
	en = l->api->insert((lst_t)l, en, cel, (en == NULL) ^ lt);
	
ins_old:
	cen = cel->scan->api->push((lst_t)cel->scan, fe);
	if (!cel->scan_from)
		cel->scan_from = cen;
	return en;
}

static void * feopen(dlst_t l, lst_entry_t *en, int *fd, off_t off, size_t size) {
	lst_entry_t e;
	for (; *en != NULL; e = l->api->next((lst_t)l, *en), l->api->remove((lst_t)l, *en), *en = e) {
		errno = 0;
		fentry_s *fe = l->api->data((lst_t)l, *en);
		DBG("got fe: %s\n", fe->path);
		if (S_ISLNK(fe->mode)) {
			if (off != 0) {
				ERROR("off != 0 (%llu) @ '%s'\n", off, fe->path);
				continue;
			}
			void *buf = malloc(size);
			DBG(_("reading symlink '%s'...\n"), fe->path);
			ssize_t ret = readlink(fe->path, buf, size);
			if (ret == -1) {
				ERROR(_("error reading symlink '%s': %s\n"), fe->path, strerror(errno));
				free(buf);
				continue;
			}
			*fd = 0;
			return buf;
		} else {
			DBG(_("opening file '%s'...\n"), fe->path);
			int _fd = open(fe->path, O_RDONLY);
			if (_fd == -1) {
				ERROR(_("error opening '%s': %s\n"), fe->path, strerror(errno));
				continue;
			}
			
			DBG(_("mmapping '%s' from off %llu, size: %u...\n"), fe->path, off, size);
			void *mem = mmap(NULL, size, PROT_READ, MAP_PRIVATE, _fd, off);
			if (mem == (void *)-1) {
				ERROR(_("error mmapping '%s': %s\n"), fe->path, strerror(errno));
				close(_fd);
				continue;
			}
			*fd = _fd;
			DBG("mmap returned: 0x%08x\n", (unsigned int)mem);
			return mem;
		}
	}
	return NULL;
}

static void feclose(fentry_s *fe, void *mem, int fd, size_t len) {
	DBG("closing '%s'...\n", fe->path);
	if (S_ISLNK(fe->mode)) {
		free(mem);
	} else {
		munmap(mem, len);
		close(fd);
	}
}

static bool scan_celink(dlst_t rlist, dlst_t tlist, lst_entry_t rlentry, size_t chunk_len, off_t size) {
	
	const size_t page_size = sysconf(_SC_PAGE_SIZE);
	celink_t *cel = rlist->api->data((lst_t)rlist, rlentry);
	
	while (cel->pos < size && !interrupted) {
		off_t pa_off = cel->pos & ~(page_size - 1);
		size_t len = min(size - pa_off, chunk_len);
		
		
		lst_entry_t enroot = cel->scan->api->first((lst_t)cel->scan);
		int fdroot;
		fentry_s *feroot = cel->scan->api->data((lst_t)cel->scan, enroot);
		
		DBG("scanning chunk (size: %llu bytes, pos: %llu, chunk_len: %u, scan @ 0x%08x) with pa_off: %llu, len: %u\n",
				feroot->size, cel->pos, chunk_len, (unsigned int)&(cel->scan), pa_off, len);
		// return true;
		
		unsigned char *rmem = feopen(cel->scan, &enroot, &fdroot, pa_off, len);
		if (!rmem) {
			ERROR(_("error opening any file of this chunk: %s\n"), strerror(errno));
			return false;
		}
		DBG("successfully opened '%s'\n", feroot->path);
		lst_entry_t en = enroot;
		while ((en = cel->scan->api->next((lst_t)cel->scan, en)) != NULL) {
			MSG("---------------------------------------\n");
			fentry_s *fe = cel->scan->api->data((lst_t)cel->scan, en);
			int fd;
			unsigned char *mem = feopen(cel->scan, &en, &fd, pa_off, len);
			if (!mem) {
				ERROR(_("error opening any file for comparison with root in this chunk: %s\n"), strerror(errno));
				break;
			}
			
			ssize_t pos = -1;
			long int delta = memcmp_at(mem, rmem, len, &pos);
			DBG("memcmp_at for '%s' returned delta %ld, pos: %d", fe->path, delta, pos);
			if (pos > -1) DBG(", mem: %d, rmem: %d", mem[pos], rmem[pos]);
			DBG("\n");
			if (delta == 0) {
				// nothing to do, advance to next ce
			} else {
				MSG(_("%s and %s differ at offset 0x%x\n"), fe->path, feroot->path, pos);
				lst_entry_t ins_en = insert(rlist, rlentry, fe, pa_off + pos, mem[pos], delta < 0);
				tlist->api->push((lst_t)tlist, ins_en);
				// prev always exists due to enroot
				lst_entry_t ep = cel->scan->api->prev((lst_t)cel->scan, en);
				cel->scan->api->remove((lst_t)cel->scan, en);
				en = ep;
			}
			
			feclose(fe, mem, fd, len);
		}
		cel->pos += len;
		feclose(feroot, rmem, fdroot, len);
	}
	
	return true;
}

static void scan_stuff(dlst_t rlist, dlst_t tlist, off_t size) {
	lst_entry_t en;
	celink_t *cel;
	
	if (size == 0) {
		tlist->api->clear((lst_t)tlist);
	} else {
		size_t chunk_len = 128  * sysconf(_SC_PAGE_SIZE);		// 512 KB
		
		while (tlist->api->size((lst_t)tlist) != 0) {
			en = tlist->api->pop((lst_t)tlist);
			cel = rlist->api->data((lst_t)rlist, en);
			bool ret = scan_celink(rlist, tlist, en, chunk_len, size);
			if (interrupted)
				return;
			if (!ret) {
				celink_free(rlist->api->remove((lst_t)rlist, en));
			}
		}
	}
	
	for (en = rlist->api->first((lst_t)rlist); en != NULL; en = rlist->api->next((lst_t)rlist, en)) {
		cel = rlist->api->data((lst_t)rlist, en);
		output_fes(cel->scan);
	}
}

bool check_list(dlst_t l, off_t size) {
	celink_t *cel = calloc(1, sizeof(celink_t));
	dlst_t nl;
	lst_entry_t en;
	dlst.init((lst_t *)&nl);
	for (en = l->api->first((lst_t)l); en != NULL; en = l->api->next((lst_t)l, en))
		nl->api->push((lst_t)nl, l->api->data((lst_t)l, en));
	cel->scan = nl;
	cel->scan_from = nl->api->first((lst_t)nl);
	
	dlst_t rlist, tlist;
	dlst.init((lst_t *)&rlist);
	dlst.init((lst_t *)&tlist);
	en = rlist->api->push((lst_t)rlist, cel);
	tlist->api->push((lst_t)tlist, en);
	scan_stuff(rlist, tlist, size);
	
	tlist->api->free((lst_t *)&tlist);
	rlist->api->free_data((lst_t *)&rlist, celink_free);
	
	return true;
}
