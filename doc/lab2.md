# lab2

## 518021910515 许嘉琦

## 问题1

`img_start`和`img_end`在`script/linker-aarch64.lds.in`中指定

boot page table的初始化在`boot/mmu.c`中完成

内核页表在初始化的时候只是将物理地址加上偏移量KBASE映射到虚拟地址

`mm.c`中定义了可以分配给程序使用的物理内存空间从`0x0000000001800000` 到`0x0000000020c00000`, 从`img_end`开始到`0x0000000001800000`之前存储page metadata

## 问题2

不同进程可共用独立的内核页表，不再需要修改每个进程页表的高地址区域来映射内核页，内核的设计和实现更加方便。

性能方面，内核和应用程序通过两个ttbr寄存器使用不同的页表，可以避免系统调用过程中切换页表，降低TLB刷新频率；安全方面，使用两个ttbr寄存器可以更好地隔离内核和应用程序。

## 问题3

1. 物理地址
2. 虚拟地址

## 问题4

1. $(2^{20}+2^{11}+2^{2}+1) \times 8$  byte；当任意一级页表中的某一个条目为空时，该条目对应的下一级页表不需要存在，依此类推，接下来的页表也不需要存在。因此，多级页表的设计极大减少了页表所占用的空间的大小。
2. 

## 问题5

需要；否则可能会造成崩溃以及安全隐患，如用户数据覆盖内核数据、恶意的用户态进程修改内核数据等

## 问题6

1. 因为内核通常需要给应用分配远大于4k的连续的内核空间，block的粒度更加合适；Boot阶段：KBASE~KABSE+256M；可延迟：KBASE+256M~KBASE+4G
2. 为了确保安全，防止用户数据覆盖内核数据、恶意的用户态进程修改内核数据等；具体机制是设置页表位的属性来隔离内核态和用户态的地址空间

## 挑战

`set_pte_flags`修改如下

```cpp
int set_pte_flags(pte_t * entry, vmr_prop_t flags, int kind)
{
	if (flags & VMR_WRITE)
		entry->l3_page.AP = AARCH64_PTE_AP_HIGH_RW_EL0_RW;
	else if(kind == KERNEL_PTE)
		entry->l3_page.AP = AARCH64_PTE_AP_HIGH_RW_EL0_NORW;
	else
		entry->l3_page.AP = AARCH64_PTE_AP_HIGH_RO_EL0_RO;

	if (flags & VMR_EXEC)
		entry->l3_page.UXN = AARCH64_PTE_UX;
	else if(kind == KERNEL_PTE)
		entry->l3_page.UXN = AARCH64_PTE_UXN;
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
```

`map_range_in_pgtbl`修改如下，新增`is_block`参数表示是否使用块粒度管理用户空间，默认是false，即使用页粒度

```cpp
int map_range_in_pgtbl(vaddr_t * pgtbl, vaddr_t va, paddr_t pa,
		       size_t len, vmr_prop_t flags, bool is_block = false)
{
    int size = 0;
    if(is_block){
	    u64 block_size = L2_PER_ENTRY_PAGES * PAGE_SIZE;
	    size_t a_len = ROUND_UP(len, block_size);
	    size_t block_num = a_len / block_size;

	    for(int i=0; i<block_num; i++){
		    u32 level = 0;
		    pte_t *entry;
		    ptp_t *cur_ptp = (ptp_t *)(pgtbl);
		    ptp_t * next_ptp; 
		    bool alloc = true;
		int ret;
		for(; level<2; level++){
			ret = get_next_ptp(cur_ptp, level, (va + i * block_size), &next_ptp, &entry, alloc);
			if(ret < 0){
				return ret;
			}
			if(ret == 1){
				return -1;
			}
			cur_ptp = next_ptp;
		}
		// level 2
		u32 index = 0;
		index = GET_L2_INDEX((va + i * block_size));
		entry = &(cur_ptp->ent[index]);

		entry->pte = 0;
		entry->l2_block.is_valid = 1;
		entry->l2_block.is_table = 0;
		entry->l2_block.pfn = (pa + i * PAGE_SIZE) >> (PAGE_SHIFT+9);
        set_pte_flags(entry, flags, flags & KERNEL_PT ? KERNEL_PTE : USER_PTE);
	}
    }
    else{
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
    }
	// change page table flush TLB
	flush_tlb();
	// </lab2>
	return 0;
}
```
