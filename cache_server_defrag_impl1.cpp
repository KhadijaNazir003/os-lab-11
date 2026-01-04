#include "cache_server_defrag.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <iomanip>

const char* policyName(EvictionPolicy policy) {
    switch(policy) {
        case EvictionPolicy::LRU: return "LRU";
        case EvictionPolicy::FIFO: return "FIFO";
        case EvictionPolicy::SIEVE: return "SIEVE";
        case EvictionPolicy::CLOCK: return "CLOCK";
        default: return "UNKNOWN";
    }
}

CacheServerDefrag::CacheServerDefrag(EvictionPolicy eviction_policy) 
    : server_fd(-1), epoll_fd(-1), free_list_head(nullptr), 
      total_free_pages(TOTAL_PAGES), policy(eviction_policy), 
      fifo_counter(0), clock_hand(0) {
    cache.reserve(TOTAL_PAGES);
    sieve_hand = sieve_list.end();
}

CacheServerDefrag::~CacheServerDefrag() {
    stop();
    
    // Clean up free list
    FreeBlock* current = free_list_head;
    while (current) {
        FreeBlock* next = current->next;
        delete current;
        current = next;
    }
}

bool CacheServerDefrag::initializeCache() {
    try {
        std::cout << "Initializing cache with FREE LIST defragmentation..." << std::endl;
        std::cout << "  Policy: " << policyName(policy) << std::endl;
        std::cout << "  Pages: " << TOTAL_PAGES << " x " << PAGE_SIZE << " bytes" << std::endl;
        
        cache.resize(TOTAL_PAGES);
        
        // Initialize free list with entire cache as one block
        free_list_head = new FreeBlock(0, TOTAL_PAGES);
        total_free_pages = TOTAL_PAGES;
        
        std::cout << "  Total cache size: " << (CACHE_SIZE / (1024.0 * 1024)) << " MB" << std::endl;
        std::cout << "  Free list initialized: 1 block of " << TOTAL_PAGES << " pages" << std::endl;
        std::cout << "Cache initialized successfully!" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize cache: " << e.what() << std::endl;
        return false;
    }
}

// Free List Management Functions

FreeBlock* CacheServerDefrag::findBestFitBlock(size_t num_pages) {
    FreeBlock* best_fit = nullptr;
    size_t best_size = SIZE_MAX;
    
    FreeBlock* current = free_list_head;
    while (current) {
        if (current->num_pages >= num_pages && current->num_pages < best_size) {
            best_fit = current;
            best_size = current->num_pages;
            
            // Exact fit - can't do better
            if (current->num_pages == num_pages) {
                break;
            }
        }
        current = current->next;
    }
    
    return best_fit;
}

FreeBlock* CacheServerDefrag::findFirstFitBlock(size_t num_pages) {
    FreeBlock* current = free_list_head;
    while (current) {
        if (current->num_pages >= num_pages) {
            return current;
        }
        current = current->next;
    }
    return nullptr;
}

void CacheServerDefrag::splitBlock(FreeBlock* block, size_t num_pages) {
    if (block->num_pages == num_pages) {
        // Exact fit - remove from free list
        removeFromFreeList(block);
        delete block;
    } else {
        // Split block - update size and start
        block->start_page += num_pages;
        block->num_pages -= num_pages;
    }
}

void CacheServerDefrag::addToFreeList(size_t start_page, size_t num_pages) {
    // Create new free block
    FreeBlock* new_block = new FreeBlock(start_page, num_pages);
    
    // Insert in sorted order by start_page for easier coalescing
    if (!free_list_head || start_page < free_list_head->start_page) {
        // Insert at head
        new_block->next = free_list_head;
        if (free_list_head) {
            free_list_head->prev = new_block;
        }
        free_list_head = new_block;
    } else {
        // Find insertion point
        FreeBlock* current = free_list_head;
        while (current->next && current->next->start_page < start_page) {
            current = current->next;
        }
        
        // Insert after current
        new_block->next = current->next;
        new_block->prev = current;
        if (current->next) {
            current->next->prev = new_block;
        }
        current->next = new_block;
    }
    
    total_free_pages += num_pages;
    
    // Try to coalesce with adjacent blocks
    coalesceAdjacentBlocks(new_block);
}

void CacheServerDefrag::removeFromFreeList(FreeBlock* block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_list_head = block->next;
    }
    
    if (block->next) {
        block->next->prev = block->prev;
    }
    
    total_free_pages -= block->num_pages;
}

void CacheServerDefrag::coalesceAdjacentBlocks(FreeBlock* block) {
    stats.coalesces++;
    
    // Try to merge with next block
    if (block->next && (block->start_page + block->num_pages == block->next->start_page)) {
        FreeBlock* next_block = block->next;
        block->num_pages += next_block->num_pages;
        block->next = next_block->next;
        if (next_block->next) {
            next_block->next->prev = block;
        }
        delete next_block;
    }
    
    // Try to merge with previous block
    if (block->prev && (block->prev->start_page + block->prev->num_pages == block->start_page)) {
        FreeBlock* prev_block = block->prev;
        prev_block->num_pages += block->num_pages;
        prev_block->next = block->next;
        if (block->next) {
            block->next->prev = prev_block;
        }
        delete block;
    }
}

// Defragmentation Functions

FragmentationStats CacheServerDefrag::getFragmentationStats() {
    FragmentationStats frag_stats;
    frag_stats.total_free_pages = total_free_pages;
    
    FreeBlock* current = free_list_head;
    while (current) {
        frag_stats.num_free_blocks++;
        if (current->num_pages > frag_stats.largest_free_block) {
            frag_stats.largest_free_block = current->num_pages;
        }
        current = current->next;
    }
    
    if (frag_stats.total_free_pages > 0) {
        frag_stats.fragmentation_ratio = 1.0 - 
            ((double)frag_stats.largest_free_block / frag_stats.total_free_pages);
    }
    
    return frag_stats;
}

bool CacheServerDefrag::defragment(size_t required_pages) {
    stats.defragmentations++;
    
    std::cout << "\n[DEFRAGMENTATION] Starting defragmentation..." << std::endl;
    std::cout << "  Required pages: " << required_pages << std::endl;
    std::cout << "  Free pages before: " << total_free_pages << std::endl;
    
    FragmentationStats before = getFragmentationStats();
    std::cout << "  Free blocks before: " << before.num_free_blocks << std::endl;
    std::cout << "  Largest block before: " << before.largest_free_block << std::endl;
    std::cout << "  Fragmentation ratio: " << std::fixed << std::setprecision(2) 
              << (before.fragmentation_ratio * 100) << "%" << std::endl;
    
    // Compact memory by moving allocations to the beginning
    compactMemory();
    
    FragmentationStats after = getFragmentationStats();
    std::cout << "  Free blocks after: " << after.num_free_blocks << std::endl;
    std::cout << "  Largest block after: " << after.largest_free_block << std::endl;
    std::cout << "  Fragmentation ratio: " << std::fixed << std::setprecision(2) 
              << (after.fragmentation_ratio * 100) << "%" << std::endl;
    std::cout << "[DEFRAGMENTATION] Complete!\n" << std::endl;
    
    return after.largest_free_block >= required_pages;
}

void CacheServerDefrag::compactMemory() {
    // Compact allocated blocks to the beginning of memory
    // This creates one large contiguous free block at the end
    
    std::vector<std::pair<std::string, CacheEntry>> entries_to_move;
    for (auto& pair : entries) {
        entries_to_move.push_back(pair);
    }
    
    // Sort by current start_page
    std::sort(entries_to_move.begin(), entries_to_move.end(),
        [](const auto& a, const auto& b) {
            return a.second.start_page < b.second.start_page;
        });
    
    // Rebuild free list
    FreeBlock* current = free_list_head;
    while (current) {
        FreeBlock* next = current->next;
        delete current;
        current = next;
    }
    free_list_head = nullptr;
    total_free_pages = 0;
    
    // Move entries to compact positions
    size_t next_free_page = 0;
    
    for (auto& pair : entries_to_move) {
        const std::string& key = pair.first;
        CacheEntry& entry = pair.second;
        size_t old_start = entry.start_page;
        size_t num_pages = entry.num_pages;
        
        if (old_start != next_free_page) {
            // Move data
            std::string data = readFromPages(old_start, entry.data_size);
            writeToPages(next_free_page, data);
            
            // Update entry
            entry.start_page = next_free_page;
            entries[key] = entry;
        }
        
        // Mark pages as used
        for (size_t i = next_free_page; i < next_free_page + num_pages; ++i) {
            cache[i].is_free = false;
            cache[i].block_start = next_free_page;
        }
        
        next_free_page += num_pages;
    }
    
    // Create one large free block at the end
    if (next_free_page < TOTAL_PAGES) {
        for (size_t i = next_free_page; i < TOTAL_PAGES; ++i) {
            cache[i].is_free = true;
        }
        
        free_list_head = new FreeBlock(next_free_page, TOTAL_PAGES - next_free_page);
        total_free_pages = TOTAL_PAGES - next_free_page;
    }
}

// Memory Allocation with Free List

bool CacheServerDefrag::allocatePages(const std::string& key, size_t data_size, const std::string& client_id) {
    size_t required_pages = calculateRequiredPages(data_size);
    
    // Try best-fit allocation
    FreeBlock* block = findBestFitBlock(required_pages);
    
    if (!block) {
        // Check if we have enough total free pages but fragmented
        if (total_free_pages >= required_pages) {
            std::cout << "[FRAGMENTATION DETECTED] Have " << total_free_pages 
                      << " free pages but largest block is too small" << std::endl;
            
            // Try defragmentation
            if (!defragment(required_pages)) {
                // Even after defrag, can't satisfy - try eviction
                if (!evict(required_pages)) {
                    return false;
                }
            }
            
            // Try allocation again after defragmentation/eviction
            block = findBestFitBlock(required_pages);
        } else {
            // Not enough total free pages - evict
            if (!evict(required_pages)) {
                return false;
            }
            block = findBestFitBlock(required_pages);
        }
    }
    
    if (!block) {
        return false;
    }
    
    // Allocate from the block
    size_t start_page = block->start_page;
    splitBlock(block, required_pages);
    
    // Mark pages as used
    for (size_t i = start_page; i < start_page + required_pages; ++i) {
        cache[i].is_free = false;
        cache[i].block_start = start_page;
    }
    
    // Create entry
    CacheEntry entry;
    entry.key = key;
    entry.client_id = client_id;
    entry.start_page = start_page;
    entry.num_pages = required_pages;
    entry.data_size = data_size;
    entry.insertion_order = fifo_counter++;
    entry.visited = false;
    entry.reference_bit = false;
    
    entries[key] = entry;
    
    return true;
}

void CacheServerDefrag::freePages(const std::string& key) {
    auto it = entries.find(key);
    if (it == entries.end()) return;
    
    size_t start_page = it->second.start_page;
    size_t num_pages = it->second.num_pages;
    
    // Mark pages as free
    for (size_t i = start_page; i < start_page + num_pages; ++i) {
        cache[i].is_free = true;
    }
    
    // Add to free list (with automatic coalescing)
    addToFreeList(start_page, num_pages);
}

size_t CacheServerDefrag::calculateRequiredPages(size_t data_size) {
    return (data_size + PAGE_SIZE - 1) / PAGE_SIZE;
}

void CacheServerDefrag::printFreeList() {
    std::cout << "\n[FREE LIST]" << std::endl;
    std::cout << "Total free pages: " << total_free_pages << std::endl;
    
    int count = 0;
    FreeBlock* current = free_list_head;
    while (current) {
        std::cout << "  Block " << count++ << ": "
                  << "start=" << current->start_page << ", "
                  << "pages=" << current->num_pages << std::endl;
        current = current->next;
    }
    std::cout << "Total blocks: " << count << "\n" << std::endl;
}

void CacheServerDefrag::printFragmentationStats() {
    FragmentationStats stats = getFragmentationStats();
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "FRAGMENTATION STATISTICS" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Free Pages:     " << stats.total_free_pages << " / " << TOTAL_PAGES 
              << " (" << std::fixed << std::setprecision(1) 
              << (100.0 * stats.total_free_pages / TOTAL_PAGES) << "%)" << std::endl;
    std::cout << "Largest Free Block:   " << stats.largest_free_block << " pages" << std::endl;
    std::cout << "Number of Free Blocks: " << stats.num_free_blocks << std::endl;
    std::cout << "Fragmentation Ratio:  " << std::fixed << std::setprecision(2) 
              << (stats.fragmentation_ratio * 100) << "%" << std::endl;
    std::cout << "  (0% = no fragmentation, 100% = completely fragmented)" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;
}

// Rest of the implementation continues in part 2...
