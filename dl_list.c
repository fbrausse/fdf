
#include <stdlib.h>
#include <stdbool.h>

#define NEXTERN
#include "dl_list.h"

#define SELF(l) ((struct dlst_list_s *)l)
#define ESELF(e) ((struct dlst_entry_s *)e)

struct dlst_entry_s {
	struct dlst_entry_s *next, *prev;
	void *data;
};

struct dlst_list_s {
	const struct i_dlst_s *api;
	struct dlst_entry_s *head, *tail;
	size_t size;
};

void dlst_clear(lst_t l) {
	struct dlst_entry_s *e = SELF(l)->head;
	SELF(l)->head = SELF(l)->tail = NULL;
	SELF(l)->size = 0;
	while (e) {
		struct dlst_entry_s *en = e->next;
		free(e);
		e = en;
	}
}

void dlst_free(lst_t *l) {
	dlst_clear(*l);
	free(*l);
}

void dlst_clear_data(lst_t l, void (*f)(void *)) {
	struct dlst_entry_s *e = SELF(l)->head;
	SELF(l)->head = NULL;
	SELF(l)->tail = NULL;
	SELF(l)->size = 0;
	if (f) {
		while (e) {
			struct dlst_entry_s *en = e->next;
			f(e->data);
			free(e);
			e = en;
		}
	} else {
		while (e) {
			struct dlst_entry_s *en = e->next;
			free(en);
			e = en;
		}
	}
}

void dlst_free_data(lst_t *l, void (*f)(void *)) {
	dlst_clear_data(*l, f);
	free(*l);
}

lst_entry_t dlst_push(lst_t l, void *d) {
	struct dlst_entry_s *e = malloc(sizeof(struct dlst_entry_s));
	e->next = NULL;
	e->prev = SELF(l)->tail;
	e->data = d;
	if (SELF(l)->size++ > 0) {
		SELF(l)->tail->next = e;
		SELF(l)->tail = e;
	} else {
		SELF(l)->head = SELF(l)->tail = e;
	}
	return e;
}

lst_entry_t dlst_prepend(lst_t l, void *d) {
	struct dlst_entry_s *e = malloc(sizeof(struct dlst_entry_s));
	e->next = SELF(l)->head;
	e->prev = NULL;
	e->data = d;
	if (SELF(l)->size++ > 0) {
		SELF(l)->head->prev = e;
		SELF(l)->head = e;
	} else {
		SELF(l)->head = SELF(l)->tail = e;
	}
	return e;
}

void * dlst_pop(lst_t l) {
	struct dlst_entry_s *e = SELF(l)->head;
	void *r = e->data;
	if (--SELF(l)->size) {
		SELF(l)->head = e->next;
		SELF(l)->head->prev = NULL;
	} else {
		SELF(l)->head = SELF(l)->tail = NULL;
	}
	free(e);
	return r;
}

size_t dlst_size(const lst_t l) {
	return SELF(l)->size;
}

lst_entry_t dlst_first(const lst_t l) {
	return SELF(l)->head;
}

lst_entry_t dlst_last(const lst_t l) {
	return SELF(l)->tail;
}

lst_entry_t dlst_next(const lst_t l, const lst_entry_t e) {
	return ESELF(e)->next;
}

lst_entry_t dlst_prev(const lst_t l, const lst_entry_t e) {
	return ESELF(e)->prev;
}

bool dlst_empty(lst_t l) {
	return !(SELF(l)->size);
}

void * dlst_data(const lst_t l, const lst_entry_t e) {
	return ESELF(e)->data;
}

void * dlst_remove(lst_t l, lst_entry_t e) {
	if (ESELF(e)->prev == NULL) {
		SELF(l)->head = ESELF(e)->next;
	} else {
		ESELF(e)->prev->next = ESELF(e)->next;
	}
	
	if (ESELF(e)->next == NULL) {
		SELF(l)->tail = ESELF(e)->prev;
	} else {
		ESELF(e)->next->prev = ESELF(e)->prev;
	}
	
	SELF(l)->size--;
	void *r = ESELF(e)->data;
	free(e);
	return r;
}

lst_entry_t dlst_insert(lst_t l, lst_entry_t o, void * d, const bool after) {
	struct dlst_entry_s *e = malloc(sizeof(struct dlst_entry_s));
	e->data = d;
	
	if (!SELF(l)->size++) {
		e->prev = e->next = NULL;
		return SELF(l)->head = SELF(l)->tail = e;
	}
	
	if (after) {
		if (!o) o = SELF(l)->tail;
		e->prev = o;
		e->next = ESELF(o)->next;
		if (o == SELF(l)->tail) {
			SELF(l)->tail = e;
		} else {
			ESELF(o)->next->prev = e;
		}
		ESELF(o)->next = e;
	} else {
		if (!o) o = SELF(l)->head;
		e->next = o;
		e->prev = ESELF(o)->prev;
		if (o == SELF(l)->head) {
			SELF(l)->head = e;
		} else {
			ESELF(o)->prev->next = e;
		}
		ESELF(o)->prev = e;
	}
	return e;
}

void dlst_init(lst_t *l);

const struct i_dlst_s dlst = {
	.size = dlst_size,
	.init = dlst_init,
	.free = dlst_free,
	.free_data = dlst_free_data,
	.push = dlst_push,
	.pop = dlst_pop,
	.empty = dlst_empty,
	.clear = dlst_clear,
	.first = dlst_first,
	.next = dlst_next,
	.data = dlst_data,
	.remove_next = NULL,
	.insert_after = NULL,
	
	.remove = dlst_remove,
	.insert = dlst_insert,
	.prev = dlst_prev,
	.last = dlst_last,
	.prepend = dlst_prepend
};

void dlst_init(lst_t *l) {
	*l = malloc(sizeof(struct dlst_list_s));
	SELF(*l)->api = &dlst;
	SELF(*l)->size = 0;
	SELF(*l)->head = NULL;
	SELF(*l)->tail = NULL;
}

/*
int main() {
	list l;
	lst_init(&l);
	
	
	printf("insert: %08x\n", lst_push(l, (void *)6));
	printf("insert: %08x\n", lst_push(l, (void *)7));
 	printf("%d\n", lst_pop(l));
	printf("insert: %08x\n", lst_push(l, (void *)8));
	
	printf("head %08x, tail %08x\n", l->head, l->tail);
	entry e;
	lst_forall(e, l)
 		printf("%08x, data: %d\n", e, lst_remove(l, e));
 	
 	while (lst_size(l))
 		printf("%d\n", lst_pop(l));
 	
	lst_free(&l);
	return 0;
}*/
