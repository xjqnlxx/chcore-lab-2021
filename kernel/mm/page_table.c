/*
 * Copyright (c) 2020 Institute of Parallel And Distributed Systems (IPADS), Shanghai Jiao Tong University (SJTU)
 * OS-Lab-2020 (i.e., ChCore) is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *   http://license.coscl.org.cn/MulanPSL
 *   THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 *   PURPOSE.
 *   See the Mulan PSL v1 for more details.
 */

#ifdef CHCORE
#include <common/util.h>
#include <common/kmalloc.h>
#endif
#include <common/vars.h>
#include <common/macro.h>
#include <common/types.h>
#include <common/printk.h>
#include <common/mm.h>
#include <common/mmu.h>

#include <common/errno.h>

#include "page_table.h"

// #include <stdio.h>

/* Page_table.c: Use simple impl for debugging now. */

extern void set_ttbr0_el1(paddr_t);
extern void flush_tlb(void);

void set_page_table(paddr_t pgtbl)
{
	set_ttbr0_el1(pgtbl);
}

#define USER_PTE 0
#define KERNEL_PTE 1
/*
 * the 3rd arg means the kind of PTE.
 */
static int set_pte_flags(pte_t * entry, vmr_prop_t flags, int kind)
{
	if (flags & VMR_WRITE)
		entry->l3_page.AP = AARCH64_PTE_AP_HIGH_RW_EL0_RW;
	else
		entry->l3_page.AP = AARCH64_PTE_AP_HIGH_RO_EL0_RO;

	if (flags & VMR_EXEC)
		entry->l3_page.UXN = AARCH64_PTE_UX;
	else
		entry->l3_page.UXN = AARCH64_PTE_UXN;

	// EL1 cannot directly execute EL0 accessiable region.
	if (kind == USER_PTE)
		entry->l3_page.PXN = AARCH64_PTE_PXN;
	entry->l3_page.AF = AARCH64_PTE_AF_ACCESSED;

	// inner sharable
	entry->l3_page.SH = INNER_SHAREABLE;
	// memory type
	entry->l3_page.attr_index = NORMAL_MEMORY;
	
	return 0;
}

#define GET_PADDR_IN_PTE(entry) \
	(((u64)entry->table.next_table_addr) << PAGE_SHIFT)
#define GET_NEXT_PTP(entry) phys_to_virt(GET_PADDR_IN_PTE(entry))

#define NORMAL_PTP (0)
#define BLOCK_PTP  (1)

/*
 * Find next page table page for the "va".
 *
 * cur_ptp: current page table page
 * level:   current ptp level
 *
 * next_ptp: returns "next_ptp"
 * pte     : returns "pte" (points to next_ptp) in "cur_ptp"
 *
 * alloc: if true, allocate a ptp when missing
 *
 */
int get_next_ptp(ptp_t * cur_ptp, u32 level, vaddr_t va,
			ptp_t ** next_ptp, pte_t ** pte, bool alloc)
{
	u32 index = 0;
	pte_t *entry;

	if (cur_ptp == NULL)
		return -ENOMAPPING;

	switch (level) {
	case 0:
		index = GET_L0_INDEX(va);
		break;
	case 1:
		index = GET_L1_INDEX(va);
		break;
	case 2:
		index = GET_L2_INDEX(va);
		break;
	case 3:
		index = GET_L3_INDEX(va);
		break;
	default:
		BUG_ON(1);
	}

	entry = &(cur_ptp->ent[index]);
	if (IS_PTE_INVALID(entry->pte)) {
		if (alloc == false) {
			return -ENOMAPPING;
		} else {
			/* alloc a new page table page */
			ptp_t *new_ptp;
			paddr_t new_ptp_paddr;
			pte_t new_pte_val;

			/* alloc a single physical page as a new page table page */
			new_ptp = get_pages(0);
			BUG_ON(new_ptp == NULL);
			memset((void *)new_ptp, 0, PAGE_SIZE);
			new_ptp_paddr = virt_to_phys((vaddr_t) new_ptp);

			new_pte_val.pte = 0;
			new_pte_val.table.is_valid = 1;
			new_pte_val.table.is_table = 1;
			new_pte_val.table.next_table_addr
			    = new_ptp_paddr >> PAGE_SHIFT;

			/* same effect as: cur_ptp->ent[index] = new_pte_val; */
			entry->pte = new_pte_val.pte;
		}
	}
	*next_ptp = (ptp_t *) GET_NEXT_PTP(entry);
	*pte = entry;
	if (IS_PTE_TABLE(entry->pte))
		return NORMAL_PTP;
	else
		return BLOCK_PTP;
}

/*
 * Translate a va to pa, and get its pte for the flags
 */
/*
 * query_in_pgtbl: translate virtual address to physical 
 * address and return the corresponding page table entry
 * 
 * pgtbl @ ptr for the first level page table(pgd) virtual address
 * va @ query virtual address
 * pa @ return physical address
 * entry @ return page table entry
 * 
 * Hint: check the return value of get_next_ptp, if ret == BLOCK_PTP
 * return the pa and block entry immediately
 */
int query_in_pgtbl(vaddr_t * pgtbl, vaddr_t va, paddr_t * pa, pte_t ** entry)
{
	// <lab2>
	// check NULL
	if(pgtbl == NULL) return -1;

	// kinfo("query in pgtbl start.\n");

	ptp_t *cur_ptp = (ptp_t *)(pgtbl);
	vaddr_t arg_va = va;
	ptp_t * next_ptp; 
	bool alloc = false;
	int ret;
	
	for(u32 level=0; level<4; level++){
		ret = get_next_ptp(cur_ptp, level, arg_va, &next_ptp, entry, alloc);
		if(ret < 0){
			return ret;
		}
		// level 0 don't have block pte
		if(level == 0 && ret == BLOCK_PTP){
			return -1;
		}
		if(ret == BLOCK_PTP){
			u64 offset;
			u64 pfn;
			switch(level){
				case 1:
					offset = GET_VA_OFFSET_L1(va);
					pfn = (u64)((*entry)->l1_block.pfn) << L1_INDEX_SHIFT;
					break;
				case 2:
					offset = GET_VA_OFFSET_L2(va);
					pfn = (u64)((*entry)->l2_block.pfn) << L2_INDEX_SHIFT;
					break;
				case 3:
					offset = GET_VA_OFFSET_L3(va);
					pfn = (u64)((*entry)->l3_page.pfn) << L3_INDEX_SHIFT;
					break;
				default:
					return -1;
			}
			*pa = (paddr_t)(pfn + offset);
			return 0;
		}
		if(ret == NORMAL_PTP){
			if(level == 3){
				return -1;
			}
			cur_ptp = next_ptp;
		}
	}
	// </lab2>
	return 0;
}

/*
 * map_range_in_pgtbl: map the virtual address [va:va+size] to 
 * physical address[pa:pa+size] in given pgtbl
 *
 * pgtbl @ ptr for the first level page table(pgd) virtual address
 * va @ start virtual address
 * pa @ start physical address
 * len @ mapping size
 * flags @ corresponding attribution bit
 *
 * Hint: In this function you should first invoke the get_next_ptp()
 * to get the each level page table entries. Read type pte_t carefully
 * and it is convenient for you to call set_pte_flags to set the page
 * permission bit. Don't forget to call flush_tlb at the end of this function 
 */
int map_range_in_pgtbl(vaddr_t * pgtbl, vaddr_t va, paddr_t pa,
		       size_t len, vmr_prop_t flags)
{
	// <lab2>
	size_t actual_len = ROUND_UP(len, PAGE_SIZE);
	int n_page_entry = actual_len/PAGE_SIZE;

	// check NULL
	if(pgtbl == NULL) return -1;

	// start map
	for(int i=0; i<n_page_entry; i++){
		u32 level = 0;
		pte_t *entry;
		ptp_t *cur_ptp = (ptp_t *)(pgtbl);
		ptp_t * next_ptp; 
		bool alloc = true;
		int ret;
		for(; level<3; level++){
			ret = get_next_ptp(cur_ptp, level, (va + i * PAGE_SIZE), &next_ptp, &entry, alloc);
			if(ret < 0){
				return ret;
			}
			if(ret == BLOCK_PTP){
				return -1;
			}
			cur_ptp = next_ptp;
		}
		// level 3
		u32 index = 0;
		index = GET_L3_INDEX((va + i * PAGE_SIZE));
		entry = &(cur_ptp->ent[index]);

		entry->pte = 0;
		entry->l3_page.is_valid = 1;
		entry->l3_page.is_page = 0;
		entry->l3_page.pfn = (pa + i * PAGE_SIZE) >> PAGE_SHIFT;

		set_pte_flags(entry, flags, flags & KERNEL_PT ? KERNEL_PTE : USER_PTE);
	}
	// change page table flush TLB
	flush_tlb();
	// </lab2>
	return 0;
}


/*
 * unmap_range_in_pgtble: unmap the virtual address [va:va+len]
 * 
 * pgtbl @ ptr for the first level page table(pgd) virtual address
 * va @ start virtual address
 * len @ unmapping size
 * 
 * Hint: invoke get_next_ptp to get each level page table, don't 
 * forget the corner case that the virtual address is not mapped.
 * call flush_tlb() at the end of function
 * 
 */
int unmap_range_in_pgtbl(vaddr_t * pgtbl, vaddr_t va, size_t len)
{
	// <lab2>
	int n_page_entry = len/PAGE_SIZE;

	// check NULL
	if(pgtbl == NULL) return -1;

	// start unmap
	for(int i=0; i<n_page_entry; i++){
		u32 level = 0;
		pte_t *entry;
		ptp_t *cur_ptp = (ptp_t *)(pgtbl);
		ptp_t * next_ptp; 
		bool alloc = false;
		int ret;
		for(; level<4; level++){
			ret = get_next_ptp(cur_ptp, level, (va + i * PAGE_SIZE), &next_ptp, &entry, alloc);
			if(ret < 0){
				return ret;
			}
			if(ret == BLOCK_PTP){
				// wrong
				if(level == 0){
					return -1;
				}
				switch (level){
					case 1:
						i = i + L1_PER_ENTRY_PAGES - 1;
						entry->l1_block.is_valid = 0;
						break;
					case 2:
						i = i + L2_PER_ENTRY_PAGES - 1;
						entry->l2_block.is_valid = 0;
						break;
					case 3:
						i = i + L3_PER_ENTRY_PAGES - 1;
						entry->l3_page.is_valid = 0;
						break;
					default:
						return -1;
				}
			}
			cur_ptp = next_ptp;
		}
	}
	// change page table flush TLB
	flush_tlb();
	// </lab2>
	return 0;
}

// TODO: add hugepage support for user space.
