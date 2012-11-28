
#ifndef _H_DL_LIST
#define _H_DL_LIST

/*
#ifdef name
#undef name
#endif
#define name(n) dlst ## n
*/

#include "sequence.h"

struct i_dlst_s {
	size_t		(*size)(const lst_t);
	void		(*init)(lst_t *);
	void		(*free)(lst_t *);
	void		(*free_data)(lst_t *, void (*)(void *));
	lst_entry_t	(*push)(lst_t, void *);
	void *		(*pop)(lst_t);
	bool		(*empty)(const lst_t);
	void		(*clear)(lst_t);
	lst_entry_t (*first)(const lst_t);
	lst_entry_t (*next)(const lst_t, const lst_entry_t);
	void *		(*data)(const lst_t, const lst_entry_t);
	void *		(*remove_next)(lst_t, lst_entry_t);
	lst_entry_t	(*insert_after)(lst_t, lst_entry_t, void *);
	
	void *		(*remove)(lst_t, lst_entry_t);
	lst_entry_t (*insert)(lst_t, lst_entry_t, void *, bool);
	lst_entry_t (*prev)(const lst_t, const lst_entry_t);
	lst_entry_t (*last)(const lst_t);
	lst_entry_t (*prepend)(lst_t, void *);
};

typedef struct {
	struct i_dlst_s *api;
} * dlst_t;

#ifndef NEXTERN
extern const struct i_dlst_s dlst;
#endif

// #define dlst_forall(a, l) for (a = dlst_first(l); a != NULL; a = dlst_next(l, a))

#endif
