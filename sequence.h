
#ifndef _H_SEQUENCE

#include <stdbool.h>

typedef struct base_lst_s * lst_t;
typedef void * lst_entry_t;

struct i_lst_s {
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
};

struct base_lst_s {
	struct i_lst_s *api;
};

#define lst_forall(en, l) \
		for (en = l->api->first(l); en != NULL; en = l->api->next(l, en))


/*
typedef struct name(_list_s) * name(_t);
typedef struct name(_entry_s) * name(_entry_t);

size_t			name(_size)(const name(_t));
void			name(_init)(name(_t) *);
void			name(_free)(name(_t) *);
void			name(_free_data)(name(_t) *, void (*)(void *));
name(_entry_t)	name(_push)(name(_t), void *);
void *			name(_pop)(name(_t));
bool			name(_empty)(name(_t));
void			name(_clear)(name(_t));
void			name(_clear_data)(name(_t), void (*)(void *));
name(_entry_t)	name(_first)(const name(_t));
name(_entry_t)	name(_next)(const name(_t), const name(_entry_t));
void *			name(_data)(const name(_t), const name(_entry_t));
void *			name(_remove_next)(name(_t) l, name(_entry_t) e);
name(_entry_t)	name(_insert_after)(name(_t) l, name(_entry_t) e, void *d);
*/
// #define lst_forall(a,l) for (a = name(_first)(l); a != NULL; a = name(_next)(l, a))

#endif
