# External Fragmentation Resolution Using Linked List Free Blocks

## Problem: External Fragmentation

### What is External Fragmentation?

**External fragmentation** occurs when free memory exists but is scattered in small, non-contiguous blocks that cannot satisfy allocation requests.

**Example:**
```
Cache: [Used][Free(10)][Used][Free(5)][Used][Free(8)][Used]
Request: Allocate 20 pages (contiguous)
Result: FAIL - Have 23 free pages total, but largest block is only 10 pages
```

### Why It's a Problem

In the original implementation:
- Pages must be **contiguous** for each key
- Simple boolean array marks pages as free/used
- No tracking of free block sizes
- Linear scan O(n) to find contiguous space
- **Fragmentation makes allocations fail** even when enough total space exists

## Solution: Free List with Coalescing

### Data Structure: Doubly-Linked List of Free Blocks

```cpp
struct FreeBlock {
    size_t start_page;    // First page of this free block
    size_t num_pages;     // Number of contiguous free pages
    FreeBlock* next;      // Next free block
    FreeBlock* prev;      // Previous free block (for easy removal)
};
```

**Visual Representation:**
```
Free List:
HEAD -> [Block: start=50, pages=20] <-> [Block: start=120, pages=15] <-> NULL

Cache Memory:
[0-49: Used][50-69: FREE][70-119: Used][120-134: FREE][135-2559: Used]
```

### Key Operations

#### 1. Allocation (Best-Fit Strategy)

**Algorithm:**
```cpp
FreeBlock* findBestFitBlock(size_t num_pages) {
    FreeBlock* best_fit = nullptr;
    size_t best_size = SIZE_MAX;
    
    for (FreeBlock* current = free_list_head; current; current = current->next) {
        if (current->num_pages >= num_pages && current->num_pages < best_size) {
            best_fit = current;
            best_size = current->num_pages;
            
            if (current->num_pages == num_pages) {
                break; // Exact fit - can't do better
            }
        }
    }
    
    return best_fit;
}
```

**Benefits:**
- Finds smallest block that fits
- Minimizes wasted space
- O(k) where k = number of free blocks
- Much better than O(n) scan of all pages

**Example:**
```
Free List: [Block: 50 pages] -> [Block: 20 pages] -> [Block: 100 pages]
Request: 25 pages
Best Fit: 50-page block (smallest that fits)
```

#### 2. Block Splitting

**Algorithm:**
```cpp
void splitBlock(FreeBlock* block, size_t num_pages) {
    if (block->num_pages == num_pages) {
        // Exact fit - remove from free list
        removeFromFreeList(block);
        delete block;
    } else {
        // Partial fit - update block to represent remaining free space
        block->start_page += num_pages;
        block->num_pages -= num_pages;
    }
}
```

**Visual Example:**
```
Before: [Block: start=50, pages=20]
Allocate 8 pages

After:  [Block: start=58, pages=12]
        Pages 50-57 now allocated
```

#### 3. Freeing Memory (with Coalescing)

**Algorithm:**
```cpp
void addToFreeList(size_t start_page, size_t num_pages) {
    // Create new free block
    FreeBlock* new_block = new FreeBlock(start_page, num_pages);
    
    // Insert in sorted order by start_page
    insertInSortedOrder(new_block);
    
    // Coalesce with adjacent blocks
    coalesceAdjacentBlocks(new_block);
}

void coalesceAdjacentBlocks(FreeBlock* block) {
    // Merge with next block if adjacent
    if (block->next && 
        block->start_page + block->num_pages == block->next->start_page) {
        block->num_pages += block->next->num_pages;
        removeFromFreeList(block->next);
    }
    
    // Merge with previous block if adjacent
    if (block->prev && 
        block->prev->start_page + block->prev->num_pages == block->start_page) {
        block->prev->num_pages += block->num_pages;
        removeFromFreeList(block);
    }
}
```

**Visual Example:**
```
Before Delete:
Cache: [Used][Free: 10][Used][Free: 5][Used]
List:  [Block: 50,10] -> [Block: 65,5]

Delete entry at pages 60-64 (5 pages):
New block: [60, 5]

After Insert & Coalesce:
Cache: [Used][Free: 20][Used]
List:  [Block: 50,20]   // Merged 3 blocks into 1!
```

#### 4. Defragmentation via Memory Compaction

**Algorithm:**
```cpp
void compactMemory() {
    // 1. Collect all allocated entries
    std::vector<Entry> entries_sorted = getAllEntries();
    
    // 2. Sort by current position
    sort(entries_sorted.begin(), entries_sorted.end(), 
         [](auto& a, auto& b) { return a.start_page < b.start_page; });
    
    // 3. Move all allocations to beginning
    size_t next_free = 0;
    for (Entry& entry : entries_sorted) {
        if (entry.start_page != next_free) {
            // Move data from old position to new position
            moveData(entry.start_page, next_free, entry.num_pages);
            entry.start_page = next_free;
        }
        next_free += entry.num_pages;
    }
    
    // 4. Create single large free block at end
    free_list_head = new FreeBlock(next_free, TOTAL_PAGES - next_free);
}
```

**Visual Example:**
```
Before Compaction (Fragmented):
[E1: 10][Free: 5][E2: 8][Free: 3][E3: 12][Free: 20]
Free List: [5] -> [3] -> [20]   (Total: 28 pages, largest: 20)

After Compaction:
[E1: 10][E2: 8][E3: 12][Free: 28]
Free List: [28]   (One contiguous block!)
```

## Implementation Highlights

### Allocation Decision Tree

```
allocatePages(required_pages):
    block = findBestFitBlock(required_pages)
    
    if block found:
        splitBlock(block, required_pages)
        return SUCCESS
    
    if total_free_pages >= required_pages:
        // Have enough space but fragmented
        defragment()
        retry allocation
    else:
        // Not enough total space
        evict(required_pages)
        retry allocation
    
    return FAIL if still can't allocate
```

### Fragmentation Metrics

```cpp
struct FragmentationStats {
    size_t total_free_pages;        // Total free space
    size_t largest_free_block;      // Biggest contiguous block
    size_t num_free_blocks;         // Number of fragments
    double fragmentation_ratio;     // 1.0 - (largest/total)
};

// 0% = no fragmentation (one big block)
// 100% = completely fragmented (all 1-page blocks)
```

**Example Calculation:**
```
Total Free: 100 pages
Largest Block: 80 pages
Fragmentation: 1.0 - (80/100) = 0.20 = 20%

Total Free: 100 pages
Largest Block: 10 pages  
Fragmentation: 1.0 - (10/100) = 0.90 = 90%  (Very fragmented!)
```

### Statistics Tracking

```cpp
struct CacheStats {
    atomic<uint64_t> defragmentations;  // Times compaction ran
    atomic<uint64_t> coalesces;         // Times adjacent blocks merged
    // ... other stats
};
```

## Performance Analysis

### Time Complexity

| Operation | Old (Boolean Array) | New (Free List) |
|-----------|---------------------|-----------------|
| Allocate | O(n) linear scan | O(k) free blocks |
| Free | O(1) mark as free | O(k) coalesce |
| Defragment | N/A (not supported) | O(m log m) compact |

Where:
- n = total pages (2,560)
- k = number of free blocks (typically << n)
- m = number of allocations

**Typical case:** k is small (5-50 blocks), so O(k) << O(n)

### Space Complexity

**Old:** O(1) - just boolean array

**New:** O(k) - k FreeBlock objects

**Trade-off:** Small extra memory for better allocation performance and defragmentation capability

### Fragmentation Prevention

**Coalescing** is key:
```
Without Coalescing:
Delete 5 entries -> 5 separate free blocks
Delete 5 more -> 10 separate free blocks
Eventually: hundreds of tiny fragments

With Coalescing:
Delete adjacent entries -> Automatically merge
Result: Fewer, larger free blocks
```

## Comparison: Before and After

### Scenario: Allocate-Delete Cycle

**Original Implementation:**
```
1. Allocate A (pages 0-10)
2. Allocate B (pages 11-20)
3. Allocate C (pages 21-30)
4. Delete B
5. Allocate D (15 pages) -> FAIL!
   Have 10 free pages but not contiguous
```

**With Free List:**
```
1. Allocate A (pages 0-10)
   Free List: [11, 2549]
   
2. Allocate B (pages 11-20)
   Free List: [21, 2539]
   
3. Allocate C (pages 21-30)
   Free List: [31, 2529]
   
4. Delete B
   Free List: [11, 10] -> [31, 2529]  (Inserted in sorted order)
   
5. Allocate D (15 pages)
   Best Fit: [31, 2529] block
   Result: SUCCESS! Pages 31-45 allocated
   Free List: [11, 10] -> [46, 2514]
```

**With Defragmentation:**
```
If above still failed (hypothetically):
1. compactMemory() moves A and C to start
2. Creates one large free block at end
3. Retry allocation -> SUCCESS
```

## Example Output

### Before Defragmentation:
```
[FRAGMENTATION STATISTICS]
Total Free Pages:     500 / 2560 (19.5%)
Largest Free Block:   45 pages
Number of Free Blocks: 8
Fragmentation Ratio:  91.00%
  (Very fragmented!)

Free List:
  Block 0: start=50, pages=45
  Block 1: start=120, pages=12
  Block 2: start=200, pages=8
  ...
```

### After Defragmentation:
```
[FRAGMENTATION STATISTICS]
Total Free Pages:     500 / 2560 (19.5%)
Largest Free Block:   500 pages
Number of Free Blocks: 1
Fragmentation Ratio:  0.00%
  (No fragmentation!)

Free List:
  Block 0: start=2060, pages=500
```

## Code Integration

### Key Changes to Original

1. **Replace boolean array scanning:**
```cpp
// Old
bool findContiguousFreePages(size_t num_pages, size_t& start_page) {
    for (size_t i = 0; i < TOTAL_PAGES; ++i) {
        // O(n) scan...
    }
}

// New
FreeBlock* block = findBestFitBlock(num_pages);
```

2. **Add coalescing on free:**
```cpp
// Old
void freePages(key) {
    for (size_t i = start; i < start + num; ++i) {
        cache[i].is_free = true;
    }
}

// New
void freePages(key) {
    addToFreeList(start_page, num_pages);  // Automatic coalescing!
}
```

3. **Enable defragmentation:**
```cpp
if (!findBestFitBlock(required) && total_free >= required) {
    defragment();  // Compact memory
    retry();
}
```

## Benefits Summary

### ✅ Resolves External Fragmentation
- Coalescing merges adjacent free blocks
- Compaction creates large contiguous space
- Allocation success rate dramatically improved

### ✅ Better Performance
- O(k) allocation vs O(n) scan
- k << n in practice
- Faster for typical workloads

### ✅ Observable Metrics
- Track fragmentation ratio
- Monitor defragmentation events
- See coalescing in action

### ✅ Adaptive Strategy
- Try best-fit first
- Defragment when needed
- Evict as last resort

## Limitations and Trade-offs

### Memory Overhead
- O(k) for free list
- Typically k < 100, so ~8KB overhead
- Negligible compared to 100MB cache

### Defragmentation Cost
- Moving data is expensive
- O(m) entries to move
- Only done when necessary

### Best-Fit vs First-Fit
- Best-fit minimizes waste
- First-fit is faster
- Configurable strategy

## Testing Scenarios

### Test 1: Coalescing
```
1. Allocate 10 entries of 10 pages each
2. Delete entries 2, 4, 6, 8 (every other)
3. Verify: Free list has 4 separate blocks
4. Delete entries 1, 3, 5, 7, 9
5. Verify: Free list has 1 large block (coalesced!)
```

### Test 2: Defragmentation
```
1. Fill cache with small entries
2. Delete random entries (create fragments)
3. Try to allocate large entry -> triggers defragmentation
4. Verify: Allocation succeeds
5. Check stats: defragmentation count increased
```

### Test 3: Fragmentation Metrics
```
1. Start with empty cache (0% fragmentation)
2. Allocate and delete in pattern
3. Monitor fragmentation ratio over time
4. Verify it decreases after defragmentation
```

## Conclusion

The free list approach with coalescing and compaction:
- **Solves external fragmentation** through intelligent memory management
- **Improves performance** with better allocation algorithms
- **Maintains simplicity** with clear data structures
- **Provides visibility** through fragmentation metrics

This is a production-ready solution used in real memory allocators, databases, and cache systems!
