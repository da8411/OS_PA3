/**********************************************************************
 * Copyright (c) 2020-2022
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "types.h"
#include "list_head.h"
#include "vm.h"

/**
 * Ready queue of the system
 */
extern struct list_head processes;

/**
 * Currently running process
 */
extern struct process *current;

/**
 * Page Table Base Register that MMU will walk through for address translation
 */
extern struct pagetable *ptbr;

/**
 * TLB of the system.
 */
//256개?
extern struct tlb_entry tlb[1UL << (PTES_PER_PAGE_SHIFT * 2)];
int tlbSize = 0;

/**
 * The number of mappings for each page frame. Can be used to determine how
 * many processes are using the page frames.
 */
extern unsigned int mapcounts[];


/**
 * lookup_tlb(@vpn, @pfn)
 *
 * DESCRIPTION
 *   Translate @vpn of the current process through TLB. DO NOT make your own
 *   data structure for TLB, but use the defined @tlb data structure
 *   to translate. If the requested VPN exists in the TLB, return true
 *   with @pfn is set to its PFN. Otherwise, return false.
 *   The framework calls this function when needed, so do not call
 *   this function manually.
 *
 * RETURN
 *   Return true if the translation is cached in the TLB.
 *   Return false otherwise
 */
bool lookup_tlb(unsigned int vpn, unsigned int *pfn)
{
	for(int i = 0; i < tlbSize; i++) {
        if(tlb[i].vpn == vpn) {
            *pfn = tlb[i].pfn;
            return true;
        }
    }
	return false;
}

int find_smallest_pfn(unsigned int* mapcounts)
{
    bool found = false;
    for(int i = 0; i < NR_PAGEFRAMES; i++) {
        if(*(mapcounts++) == 0) {
            found = true;
            return i;
        }
    }
    
    if (found == false) {
        fprintf(stderr, "[ERROR] MAX find_smallest_pfn");
    }
    return NR_PAGEFRAMES;
}

/**
 * insert_tlb(@vpn, @pfn)
 *
 * DESCRIPTION
 *   Insert the mapping from @vpn to @pfn into the TLB. The framework will call
 *   this function when required, so no need to call this function manually.
 *
 */
void insert_tlb(unsigned int vpn, unsigned int pfn)
{
	struct tlb_entry new_entry;
    new_entry.valid = true;
    new_entry.vpn = vpn;
    new_entry.pfn = pfn;
    tlb[tlbSize++] = new_entry;
}


/**
 * alloc_page(@vpn, @rw)
 *
 * DESCRIPTION
 *   Allocate a page frame that is not allocated to any process, and map it
 *   to @vpn. When the system has multiple free pages, this function should
 *   allocate the page frame with the **smallest pfn**.
 *   You may construct the page table of the @current process. When the page
 *   is allocated with RW_WRITE flag, the page may be later accessed for writes.
 *   However, the pages populated with RW_READ only should not be accessed with
 *   RW_WRITE accesses.
 *
 * RETURN
 *   Return allocated page frame number.
 *   Return -1 if all page frames are allocated.
 */
unsigned int alloc_page(unsigned int vpn, unsigned int rw)
{
    unsigned int outer;
    unsigned int inner;
    outer = vpn / NR_PTES_PER_PAGE;
    inner = vpn % NR_PTES_PER_PAGE;

    // if memory is not allocated, allocate memory
    if(!ptbr->outer_ptes[outer]) {
    ptbr->outer_ptes[outer] = malloc(sizeof(struct pte_directory)*NR_PTES_PER_PAGE);
    }
    struct pte* pte = &(ptbr->outer_ptes[outer]->ptes[inner]);
    // set valid true
    pte->valid = true;
    if(rw == RW_READ) {
        pte->writable = false;
        pte->private = RW_READ;
    } else if(rw == (RW_WRITE | RW_READ)) {
        pte->writable = true;
    }
    //set pfn to possible smallest pfn
    int smallest_pfn = find_smallest_pfn(mapcounts);

    // return -1 if all page frames are allocated
    if(smallest_pfn == NR_PAGEFRAMES) {
        return -1;
    }

    pte->pfn = smallest_pfn;
    mapcounts[smallest_pfn]++;

    //inc s_smallest_pfn by 1
    return smallest_pfn;
}


/**
 * free_page(@vpn)
 *
 * DESCRIPTION
 *   Deallocate the page from the current processor. Make sure that the fields
 *   for the corresponding PTE (valid, writable, pfn) is set @false or 0.
 *   Also, consider carefully for the case when a page is shared by two processes,
 *   and one process is to free the page.
 */
void free_page(unsigned int vpn)
{
    unsigned int outer;
    unsigned int inner;
    outer = vpn / NR_PTES_PER_PAGE;
    inner = vpn % NR_PTES_PER_PAGE;

    struct pte* pte = &(ptbr->outer_ptes[outer]->ptes[inner]);

    pte->valid = false;
    pte->writable = false;
    mapcounts[pte->pfn]--;
    pte->pfn = 0;

    //struct tlb_entry default_tlb;
    for(int i = 0; i < tlbSize; i++) {
        if(tlb[i].vpn == vpn) {
            tlb[i].valid = false;
            tlbSize--;
            return;
        }
    }
}


/**
 * handle_page_fault()
 *
 * DESCRIPTION
 *   Handle the page fault for accessing @vpn for @rw. This function is called
 *   by the framework when the __translate() for @vpn fails. This implies;
 *   0. page directory is invalid
 *   1. pte is invalid
 *   2. pte is not writable but @rw is for write
 *   This function should identify the situation, and do the copy-on-write if
 *   necessary.
 *
 * RETURN
 *   @true on successful fault handling
 *   @false otherwise
 */
bool handle_page_fault(unsigned int vpn, unsigned int rw)
{
    unsigned int outer;
    unsigned int inner;
    outer = vpn / NR_PTES_PER_PAGE;
    inner = vpn % NR_PTES_PER_PAGE;

    struct pte *pte;

    // 안들어오는데?
    // pte is invalid
    if(ptbr->outer_ptes[outer] != NULL ) {
        pte = &(ptbr->outer_ptes[outer]->ptes[inner]);
        if (pte->valid == false) {
            return false;
        }
    }
    
    // 안들어오는데?
    // page directory is invalid
    if(ptbr->outer_ptes[outer] == NULL) {
        fprintf(stderr,"pte invalid2");
        return false;
    }
    //pte is not writable but @rw is for write
    pte = &(ptbr->outer_ptes[outer]->ptes[inner]);
    if(pte->writable == false && (rw & RW_WRITE) == RW_WRITE) {
        //원래 read만 가능
        if(pte->private == RW_READ) {
            return false;
        }
        //원래 write 가능이지만 fork하느라 바뀜
        if(pte->private != RW_READ) {
            // 나만 참조하고있음
            if(mapcounts[pte->pfn] == 1) {
                pte->writable = true;
                return true;
            }
            // 다른애도 참조하고 있음
            if(mapcounts[pte->pfn] >= 2) {
                mapcounts[pte->pfn]--;
                pte->pfn = find_smallest_pfn(mapcounts);
                pte->writable = true;
                mapcounts[pte->pfn]++;
                return true;
            }
        }
    }

    //이거는 에러임
    fprintf(stderr, "error\n");
    return false;
}


/**
 * switch_process()
 *
 * DESCRIPTION
 *   If there is a process with @pid in @processes, switch to the process.
 *   The @current process at the moment should be put into the @processes
 *   list, and @current should be replaced to the requested process.
 *   Make sure that the next process is unlinked from the @processes, and
 *   @ptbr is set properly.
 *
 *   If there is no process with @pid in the @processes list, fork a process
 *   from the @current. This implies the forked child process should have
 *   the identical page table entry 'values' to its parent's (i.e., @current)
 *   page table. 
 *   To implement the copy-on-write feature, you should manipulate the writable
 *   bit in PTE and mapcounts for shared pages. You may use pte->private for 
 *   storing some useful information :-)
 */

// 원래 read였던것은 fork후에도 read만해야함
// 원래 write였던것은 fork후에도 write가능
// private에 write가능하다고 표시

//On a context switch, TLB should be flushed
//– Each process has its own VA to PA mapping
//– Thus, cannot share TLB entries between processes
void switch_process(unsigned int pid)
{
    memset(tlb, 0, sizeof(tlb));
    // 프로세스 리스트 찾아서 있으면 변경
    struct process *p, *cursor = NULL;

    list_for_each_entry_safe(p, cursor,&processes,list) {
        if(p->pid == pid) {
            list_add_tail(&current->list,&processes);
            current = p;
            ptbr = &(p->pagetable);
            list_del_init(&p->list);
            goto exit;
        }
    }

    // 프로세스 리스트에 없으면 새로 생성
    struct process *next =(struct process*)malloc(sizeof(struct process));
    struct pagetable* nextPtbr = &(next->pagetable);
    for(int i = 0 ; i < NR_PTES_PER_PAGE ; i++) {
        if(!ptbr->outer_ptes[i]) {
            continue;
        } else {
            next->pagetable.outer_ptes[i] = malloc(sizeof(struct pte_directory)*NR_PTES_PER_PAGE);
        }

        for (int j = 0 ; j < NR_PTES_PER_PAGE ;j++) {
            if (ptbr->outer_ptes[i]->ptes[j].valid) {
                struct pte forkedPte = ptbr->outer_ptes[i]->ptes[j];
                mapcounts[ptbr->outer_ptes[i]->ptes[j].pfn]++;
                //원래 read만 가능한것이었으면 read만 가능했다고 표시
                if (ptbr->outer_ptes[i]->ptes[j].writable == 0) {
                    forkedPte.private = ptbr->outer_ptes[i]->ptes[j].private;
                }
                // copy on write
                ptbr->outer_ptes[i]->ptes[j].writable = false;
                forkedPte.valid = true;
                forkedPte.writable = false;
                forkedPte.pfn = ptbr->outer_ptes[i]->ptes[j].pfn;
                nextPtbr->outer_ptes[i]->ptes[j] = forkedPte;
            }       
        }
    }
    next->pid = pid;
    next->list = current->list;
    list_add_tail(&current->list,&processes);
    current = next;
    ptbr = nextPtbr;

    exit:
    return;
}


