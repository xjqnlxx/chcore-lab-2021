#include <common/util.h>
#include <common/macro.h>
#include <common/kprint.h>

#include "buddy.h"
// #include <stdio.h>

/*
 * The layout of a phys_mem_pool:
 * | page_metadata are (an array of struct page) | alignment pad | usable memory |
 *
 * The usable memory: [pool_start_addr, pool_start_addr + pool_mem_size).
 */
void init_buddy(struct phys_mem_pool *pool, struct page *start_page,
		vaddr_t start_addr, u64 page_num)
{
	// printf("init buddy start.\n");
	int order;
	int page_idx;
	struct page *page;

	/* Init the physical memory pool. */
	pool->pool_start_addr = start_addr;
	pool->page_metadata = start_page;
	pool->pool_mem_size = page_num * BUDDY_PAGE_SIZE;
	/* This field is for unit test only. */
	pool->pool_phys_page_num = page_num;

	/* Init the free lists */
	for (order = 0; order < BUDDY_MAX_ORDER; ++order) {
		pool->free_lists[order].nr_free = 0;
		init_list_head(&(pool->free_lists[order].free_list));
	}

	/* Clear the page_metadata area. */
	memset((char *)start_page, 0, page_num * sizeof(struct page));

	/* Init the page_metadata area. */
	for (page_idx = 0; page_idx < page_num; ++page_idx) {
		page = start_page + page_idx;
		page->allocated = 1;
		page->order = 0;
	}

	/* Put each physical memory page into the free lists. */
	for (page_idx = 0; page_idx < page_num; ++page_idx) {
		page = start_page + page_idx;
		buddy_free_pages(pool, page);
	}
	// printf("init buddy end.\n");
}

static struct page *get_buddy_chunk(struct phys_mem_pool *pool,
				    struct page *chunk)
{
	u64 chunk_addr;
	u64 buddy_chunk_addr;
	int order;

	/* Get the address of the chunk. */
	chunk_addr = (u64) page_to_virt(pool, chunk);
	order = chunk->order;
	/*
	 * Calculate the address of the buddy chunk according to the address
	 * relationship between buddies.
	 */
#define BUDDY_PAGE_SIZE_ORDER (12)
	buddy_chunk_addr = chunk_addr ^
	    (1UL << (order + BUDDY_PAGE_SIZE_ORDER));

	/* Check whether the buddy_chunk_addr belongs to pool. */
	if ((buddy_chunk_addr < pool->pool_start_addr) ||
	    (buddy_chunk_addr >= (pool->pool_start_addr +
				  pool->pool_mem_size))) {
		return NULL;
	}

	return virt_to_page(pool, (void *)buddy_chunk_addr);
}

/*
 * split_page: split the memory block into two smaller sub-block, whose order
 * is half of the origin page.
 * pool @ physical memory structure reserved in the kernel
 * order @ order for target page block
 * page @ splitted page
 * 
 * Hints: don't forget to substract the free page number for the corresponding free_list.
 * you can invoke split_page recursively until the given page can not be splitted into two
 * smaller sub-pages.
 */
static struct page *split_page(struct phys_mem_pool *pool, u64 order,
			       struct page *page)
{
	// <lab2>
	// printf("split page start.\n");
	// check page not null
	if(page == NULL){
		return NULL;
	}
	int to_splitted_order = page->order;

	// check order
	if(order > to_splitted_order){
		return NULL;
	}
	if(order == to_splitted_order){
		return page;
	}
	if(to_splitted_order < 1){
		return page;
	}

	// delete from the list
	list_del(&page->node);
	// reduce nr_free 
	pool->free_lists[to_splitted_order].nr_free--;

	// modify order
	for(int i=0; i<(1UL << to_splitted_order); i++){
		struct page *sep_page = page + i;
		sep_page->order = to_splitted_order - 1;
	}
	list_add(&page->node, &pool->free_lists[to_splitted_order - 1].free_list);
	list_add(&((page+(1UL << (to_splitted_order -1)))->node), &pool->free_lists[to_splitted_order - 1].free_list);
	// add nr_free
	pool->free_lists[to_splitted_order - 1].nr_free = pool->free_lists[to_splitted_order - 1].nr_free + 2;

	if(order == to_splitted_order-1){
		return page;
	}
	// printf("split page end.\n");
	return split_page(pool, order, page);
	// </lab2>
}

/*
 * buddy_get_pages: get free page from buddy system.
 * pool @ physical memory structure reserved in the kernel
 * order @ get the struct page of (1<<order) continous pages from the buddy system
 * 
 * Hints: Find the corresonding free_list which can allocate 1<<order
 * continuous pages and don't forget to split the list node after allocation   
 */
struct page *buddy_get_pages(struct phys_mem_pool *pool, u64 order)
{
	// <lab2>
	// printf("buddy get pages start.\n");
	struct page *page = NULL;
	u64 contain_order = order;
	// check order
	if(order < 0){
		return NULL;
	}
	if(order >= BUDDY_MAX_ORDER){
		return NULL;
	}
	// get min order (>= expected order) whose free list in not empty
	while(pool->free_lists[contain_order].nr_free == 0){
		contain_order++;
		if(contain_order == BUDDY_MAX_ORDER){
			return NULL;
		}
	}
	// find page by list node
	page = list_entry(pool->free_lists[contain_order].free_list.next, struct page, node);
	// split page
	if(contain_order != order){
		split_page(pool, order, page);
	}
	// del list node
	list_del(&page->node);
	// reduce nr_free
	pool->free_lists[page->order].nr_free --;
	// modify allocated
	for(int i=0; i<(1UL<<page->order); i++){
		struct page *to_modify = page + i;
		to_modify->allocated = 1;
	}
	// printf("buddy get pages end.\n");
	return page;
	// </lab2>
}

/*
 * merge_page: merge the given page with the buddy page
 * pool @ physical memory structure reserved in the kernel
 * page @ merged page (attempted)
 * 
 * Hints: you can invoke the merge_page recursively until
 * there is not corresponding buddy page. get_buddy_chunk
 * is helpful in this function.
 */
static struct page *merge_page(struct phys_mem_pool *pool, struct page *page)
{
	// <lab2>
	// printf("merge page start.\n");
	if(page == NULL){
		return NULL;
	}
	struct page *m_page = NULL;
	//printf("merge page: page vaddress %p.\n", page_to_virt(pool, page));
	struct page *buddy_chunk = get_buddy_chunk(pool, page);
	//printf("merge page: buddy chunk vaddress %p.\n", page_to_virt(pool, buddy_chunk));
	int origin_order = page->order;
	int new_order = page->order + 1;
	// check if have buddy chunk
	if(buddy_chunk == NULL){
		// cannot merge
		//printf("merge page end: not have buddy chunk.\n");
		return page;
	}
	if(buddy_chunk->order != page->order){
		//printf("merge page end: order not equal.\n");
		return page;
	}
	if(page->order == BUDDY_MAX_ORDER - 1){
		//printf("merge page end: max order.\n");
		return page;
	}
	// check if two chunck are free
	if(page->allocated == 0 && buddy_chunk->allocated == 0){
		//printf("merge page: del list node\n");
		// del list node
		list_del(&page->node);
		list_del(&buddy_chunk->node);
		//printf("merge page: reduce nr_free\n");
		// reduce nr_free
		pool->free_lists[origin_order].nr_free = pool->free_lists[origin_order].nr_free - 2;
		//printf("merge page: modify order\n");
		// modify order
		for(int i=0; i<(1UL<<origin_order); i++){
			struct page *to_modify1 = page + i;
			struct page *to_modify2 = buddy_chunk + i;
			to_modify1->order = new_order;
			to_modify2->order = new_order;
		}
		m_page = page_to_virt(pool, page) > page_to_virt(pool, buddy_chunk)? buddy_chunk:page;
		//printf("merge page: add list node new order %d \n", new_order);
		// add list node
		list_add(&m_page->node, &pool->free_lists[new_order].free_list);
		//printf("merge page: add nr_free\n");
		// add nr_free
		pool->free_lists[new_order].nr_free++;
		// printf("merge page end.\n");
		return merge_page(pool, m_page);
	}
	else
	{
		// printf("merge page end: not two free chunk\n");
		return page;
	}
	// </lab2>
}

/*
 * buddy_free_pages: give back the pages to buddy system
 * pool @ physical memory structure reserved in the kernel
 * page @ free page structure
 * 
 * Hints: you can invoke merge_page.
 */
void buddy_free_pages(struct phys_mem_pool *pool, struct page *page)
{
	// <lab2>
	// printf("buddy free pages start.\n");
	if(page == NULL){
		return;
	}
	int order = page->order;
	
	if(order >= BUDDY_MAX_ORDER){
		return;
	}
	if(order < 0){
		return;
	}
	// modify allocated
	for(int i=0; i<(1UL << order); i++){
		struct page *to_modify = page + i;
		to_modify->allocated = 0;
	}
	// add list node
	list_add(&page->node, &pool->free_lists[order].free_list);
	// add nr_free
	pool->free_lists[order].nr_free++;
	// merge chunk
	merge_page(pool, page);
	// printf("buddy free pages end.\n");
	return;
	// </lab2>
}

void *page_to_virt(struct phys_mem_pool *pool, struct page *page)
{
	u64 addr;

	/* page_idx * BUDDY_PAGE_SIZE + start_addr */
	addr = (page - pool->page_metadata) * BUDDY_PAGE_SIZE +
	    pool->pool_start_addr;
	return (void *)addr;
}

struct page *virt_to_page(struct phys_mem_pool *pool, void *addr)
{
	struct page *page;

	page = pool->page_metadata +
	    (((u64) addr - pool->pool_start_addr) / BUDDY_PAGE_SIZE);
	return page;
}

u64 get_free_mem_size_from_buddy(struct phys_mem_pool * pool)
{
	int order;
	struct free_list *list;
	u64 current_order_size;
	u64 total_size = 0;

	for (order = 0; order < BUDDY_MAX_ORDER; order++) {
		/* 2^order * 4K */
		current_order_size = BUDDY_PAGE_SIZE * (1 << order);
		list = pool->free_lists + order;
		total_size += list->nr_free * current_order_size;

		/* debug : print info about current order */
		kdebug("buddy memory chunk order: %d, size: 0x%lx, num: %d\n",
		       order, current_order_size, list->nr_free);
	}
	return total_size;
}
