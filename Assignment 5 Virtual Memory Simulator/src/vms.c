#include "vms.h"

#include "mmu.h"
#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A debugging helper that will print information about the pointed to PTE
   entry. */
static void print_pte_entry(uint64_t* entry); // print off the ppn and all the bits

// array to keep track of how many references there are to a page
static int page_references[MAX_PAGES];

void page_fault_handler(void* virtual_address, int level, void* page_table) {
    // Check why the fault happend

    // Find faulting pte
    uint64_t* page_fault_entry = vms_page_table_pte_entry(page_table, virtual_address, level);
    uint64_t fault_ppn = vms_pte_get_ppn(page_fault_entry);
    void* fault_page = vms_ppn_to_page(fault_ppn); // get page from ppn

    if (vms_pte_custom(page_fault_entry)) { // Shared page
        if (page_references[vms_get_page_index(fault_page)] > 0) { // make a copy
            // make a new page
            void* p0_child = vms_new_page();

            // Copy
            memcpy(p0_child, fault_page, PAGE_SIZE);

            // Link Page
            vms_pte_set_ppn(page_fault_entry, vms_page_to_ppn(p0_child));

            // Set Write and Custom
            vms_pte_custom_clear(page_fault_entry);
            vms_pte_write_set(page_fault_entry);

            // Decrement page reference
            --page_references[vms_get_page_index(fault_page)];

        } else {
            // Turn on Write and turn off custom
            vms_pte_write_set(page_fault_entry);
            vms_pte_custom_clear(page_fault_entry);
        }
    }
}

void* vms_fork_copy(void) {
    void* l2_parent = vms_get_root_page_table();
    void* l2_child = vms_new_page(); // get new l2 page

    // Go through Parent and copy
    for (int i = 0; i < NUM_PTE_ENTRIES; ++i) { // check over the page
        uint64_t* l2_parent_entry = vms_page_table_pte_entry_from_index(l2_parent,i);
        //printf("L2 Parent:\n");
        //print_pte_entry(l2_parent_entry); // print pte
        if (!vms_pte_valid(l2_parent_entry)) { // not a valid pte
            continue; // do nothing
        } else { // valid pte
            uint64_t* l2_child_entry = vms_page_table_pte_entry_from_index(l2_child, i); // get child entry based on index
            vms_pte_valid_set(l2_child_entry); // set child entry to be valid (no ppn put in yet)
            uint64_t l1_parent_ppn = vms_pte_get_ppn(l2_parent_entry); // get ppn from parent just to store
            
            // Create an L1 Page table
            void* l1_parent = vms_ppn_to_page(l1_parent_ppn); // get page from ppn
            void* l1_child = vms_new_page();

            // Link l2 child entry to l1 child
            vms_pte_set_ppn(l2_child_entry, vms_page_to_ppn(l1_child));

            // Go through Parent and Copy into l1
            for (int j = 0; j < NUM_PTE_ENTRIES; ++j) {
                uint64_t* l1_parent_entry = vms_page_table_pte_entry_from_index(l1_parent,j);
                if (!vms_pte_valid(l1_parent_entry)) { // not a valid pte
                    //printf("%d\n", j);
                    continue; // do nothing
                } else { // valid pte
                    uint64_t* l1_child_entry = vms_page_table_pte_entry_from_index(l1_child, j); // get child entry based on index
                    vms_pte_valid_set(l1_child_entry); // set child entry to be valid (no ppn put in yet)
                    uint64_t l0_parent_ppn = vms_pte_get_ppn(l1_parent_entry); // get ppn from parent just to store
                
                    // Create an L0 Page table
                    void* l0_parent = vms_ppn_to_page(l0_parent_ppn); // get page from ppn
                    void* l0_child = vms_new_page();

                    // Link l1 child entry to l0 child
                    vms_pte_set_ppn(l1_child_entry, vms_page_to_ppn(l0_child));

                    // Go through Parent and Copy into l1
                    for (int k = 0; k < NUM_PTE_ENTRIES; ++k) {
                        uint64_t* l0_parent_entry = vms_page_table_pte_entry_from_index(l0_parent,k);
                        if (!vms_pte_valid(l0_parent_entry)) { // not a valid pte
                            continue; // do nothing
                        } else { // valid pte
                            uint64_t* l0_child_entry = vms_page_table_pte_entry_from_index(l0_child, k); // get child entry based on index
                            vms_pte_valid_set(l0_child_entry); // set child entry to be valid (no ppn put in yet)
                            
                            // Check read and write
                            if (vms_pte_read(l0_parent_entry)) {
                                vms_pte_read_set(l0_child_entry);
                            }
                            if (vms_pte_write(l0_parent_entry)) {
                                vms_pte_write_set(l0_child_entry);
                            }
                            uint64_t p0_parent_ppn = vms_pte_get_ppn(l0_parent_entry); // get ppn from parent just to store
                        
                            // Create an p0 Page table
                            void* p0_parent = vms_ppn_to_page(p0_parent_ppn); // get page from ppn
                            void* p0_child = vms_new_page();

                            // Link l0 child entry to p0 child
                            vms_pte_set_ppn(l0_child_entry, vms_page_to_ppn(p0_child));

                            // Memcopy p0_parent to p0_child
                            memcpy(p0_child, p0_parent, PAGE_SIZE);

                        }
                    }
                }
            }
        }
        
    }
    //printf("Finished Copy!\n");
    return l2_child;
}

void* vms_fork_copy_on_write(void) {
    void* l2_parent = vms_get_root_page_table();
    void* l2_child = vms_new_page(); // get new l2 page

    // Go through Parent and copy
    for (int i = 0; i < NUM_PTE_ENTRIES; ++i) { // check over the page
        uint64_t* l2_parent_entry = vms_page_table_pte_entry_from_index(l2_parent,i);
        //printf("L2 Parent:\n");
        //print_pte_entry(l2_parent_entry); // print pte
        if (!vms_pte_valid(l2_parent_entry)) { // not a valid pte
            continue; // do nothing
        } else { // valid pte
            uint64_t* l2_child_entry = vms_page_table_pte_entry_from_index(l2_child, i); // get child entry based on index
            vms_pte_valid_set(l2_child_entry); // set child entry to be valid (no ppn put in yet)
            uint64_t l1_parent_ppn = vms_pte_get_ppn(l2_parent_entry); // get ppn from parent just to store
            
            // Create an L1 Page table
            void* l1_parent = vms_ppn_to_page(l1_parent_ppn); // get page from ppn
            void* l1_child = vms_new_page();

            // Link l2 child entry to l1 child
            vms_pte_set_ppn(l2_child_entry, vms_page_to_ppn(l1_child));

            // Go through Parent and Copy into l1
            for (int j = 0; j < NUM_PTE_ENTRIES; ++j) {
                uint64_t* l1_parent_entry = vms_page_table_pte_entry_from_index(l1_parent,j);
                if (!vms_pte_valid(l1_parent_entry)) { // not a valid pte
                    //printf("%d\n", j);
                    continue; // do nothing
                } else { // valid pte
                    uint64_t* l1_child_entry = vms_page_table_pte_entry_from_index(l1_child, j); // get child entry based on index
                    vms_pte_valid_set(l1_child_entry); // set child entry to be valid (no ppn put in yet)
                    uint64_t l0_parent_ppn = vms_pte_get_ppn(l1_parent_entry); // get ppn from parent just to store
                
                    // Create an L0 Page table
                    void* l0_parent = vms_ppn_to_page(l0_parent_ppn); // get page from ppn
                    void* l0_child = vms_new_page();

                    // Link l1 child entry to l0 child
                    vms_pte_set_ppn(l1_child_entry, vms_page_to_ppn(l0_child));

                    // Go through Parent and Copy into l1
                    for (int k = 0; k < NUM_PTE_ENTRIES; ++k) {
                        uint64_t* l0_parent_entry = vms_page_table_pte_entry_from_index(l0_parent,k);
                        if (!vms_pte_valid(l0_parent_entry)) { // not a valid pte
                            continue; // do nothing
                        } else { // valid pte
                            uint64_t* l0_child_entry = vms_page_table_pte_entry_from_index(l0_child, k); // get child entry based on index
                            vms_pte_valid_set(l0_child_entry); // set child entry to be valid (no ppn put in yet)
                            
                            // Check read and write
                            if (vms_pte_read(l0_parent_entry)) {
                                vms_pte_read_set(l0_child_entry);
                            }
                            if (vms_pte_write(l0_parent_entry)) {
                                vms_pte_write_set(l0_child_entry);
                            }
                            uint64_t p0_parent_ppn = vms_pte_get_ppn(l0_parent_entry); // get ppn from parent just to store
                        
                            // Create an p0 Page table
                            void* p0_parent = vms_ppn_to_page(p0_parent_ppn); // get page from ppn

                            // Link l0 child entry to p0 child
                            vms_pte_set_ppn(l0_child_entry, vms_page_to_ppn(p0_parent));

                            if (vms_pte_write(l0_parent_entry) || vms_pte_custom(l0_parent_entry)) { // only if not already shared
                                // Increment number of pages referenced
                                ++page_references[vms_get_page_index(p0_parent)];
                                //printf("Page Referenced: %d\n", page_references[vms_get_page_index(p0_parent)]);

                                // Set to unwriteable on both child and parent
                                vms_pte_write_clear(l0_parent_entry);
                                vms_pte_write_clear(l0_child_entry);

                                // Set custom bit to indicate sharing
                                vms_pte_custom_set(l0_parent_entry);
                                vms_pte_custom_set(l0_child_entry);

                            } else {
                                continue; // its just read only page, nothing else has to be done
                            }
                        }
                    }
                }
            }
        }
        
    }
    //printf("Finished Copy!\n");
    return l2_child;
}

static void print_pte_entry(uint64_t* entry) {
    const char* dash = "-";
    const char* custom = dash;
    const char* write = dash;
    const char* read = dash;
    const char* valid = dash;
    if (vms_pte_custom(entry)) {
        custom = "C";
    }
    if (vms_pte_write(entry)) {
        write = "W";
    }
    if (vms_pte_read(entry)) {
        read = "R";
    }
    if (vms_pte_valid(entry)) {
        valid = "V";
    }

    printf("PPN: 0x%lX Flags: %s%s%s%s\n",
           vms_pte_get_ppn(entry),
           custom, write, read, valid);
}
