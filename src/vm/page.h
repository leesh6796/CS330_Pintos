#ifndef PAGE_H
#define PAGE_H

#include <stdio.h>
#include <stdlib.h>
#include <hash.h>
#include "threads/thread.h"

static const int PAGE = 1;
static const int SWAP = 2;
static const int MM_FILE = 3;
static const int LAZY = 4;
static const int SWAP_MM = 5;

struct spage {
	struct hash_elem elem; // hash element

	int status;
	void *vaddr;
	bool valid; // false면 swap out된 상태.
	size_t index; // SWAP Index, swap된 상태일 때만 사용
	bool writable;
	int fd;
	struct file *file;

	int offset; // lazy loading 할 때 file offset
	bool is_zero; // lazy loading 할 때 zero fill 여부
	bool is_over;
	int length_over; // 튀어나온 길
};

struct mmap {
	struct list_elem elem;

	int mapid;
	struct file *file;
	void *addr;
	uint32_t size;
	struct thread *owner;
};

void spage_load (struct spage *spe);
struct spage* spage_create(void *addr, int status, bool writable);
int spage_free(struct spage* target);
struct spage* find_spage(void *vaddr);
void stack_growth (void *addr);

//void spage_load (struct spage *spe);

/*struct spage* spage_get_entry_from_thread (void *vaddr, struct thread *t);

struct spage_entry *
spage_get_entry (void *vaddr);

struct hash_elem *
spage_insert_entry (struct spage_entry *spe);

struct spage_entry *
spage_insert_upage (uint8_t *upage, bool writable);*/

bool hash_comp_func (const struct hash_elem *x, const struct hash_elem *y, void *aux UNUSED);
unsigned hash_calc_func (const struct hash_elem *e, void *aux UNUSED);
void hash_free_func (const struct hash_elem *e, void *aux UNUSED);

#endif