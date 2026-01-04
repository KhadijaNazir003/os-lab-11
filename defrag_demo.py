#!/usr/bin/env python3
"""
Defragmentation Demonstration
Shows how free list with coalescing resolves external fragmentation
"""

import random
from dataclasses import dataclass
from typing import Optional, List

@dataclass
class FreeBlock:
    """Represents a contiguous block of free pages"""
    start_page: int
    num_pages: int
    next: Optional['FreeBlock'] = None
    prev: Optional['FreeBlock'] = None

@dataclass
class Allocation:
    """Represents an allocated entry"""
    key: str
    start_page: int
    num_pages: int

class CacheWithDefrag:
    def __init__(self, total_pages=100):
        self.total_pages = total_pages
        self.free_list_head: Optional[FreeBlock] = FreeBlock(0, total_pages)
        self.allocations: dict[str, Allocation] = {}
        self.total_free = total_pages
        self.stats = {
            'allocations': 0,
            'deallocations': 0,
            'coalesces': 0,
            'defragmentations': 0
        }
    
    def find_best_fit(self, num_pages: int) -> Optional[FreeBlock]:
        """Find smallest block that fits (best-fit strategy)"""
        best_fit = None
        best_size = float('inf')
        
        current = self.free_list_head
        while current:
            if current.num_pages >= num_pages and current.num_pages < best_size:
                best_fit = current
                best_size = current.num_pages
                if current.num_pages == num_pages:
                    break  # Exact fit
            current = current.next
        
        return best_fit
    
    def split_block(self, block: FreeBlock, num_pages: int):
        """Split a free block or remove it if exact fit"""
        if block.num_pages == num_pages:
            # Exact fit - remove from list
            if block.prev:
                block.prev.next = block.next
            else:
                self.free_list_head = block.next
            
            if block.next:
                block.next.prev = block.prev
            
            self.total_free -= num_pages
        else:
            # Partial fit - update block
            block.start_page += num_pages
            block.num_pages -= num_pages
            self.total_free -= num_pages
    
    def add_to_free_list(self, start_page: int, num_pages: int):
        """Add freed pages to free list with automatic coalescing"""
        new_block = FreeBlock(start_page, num_pages)
        
        # Insert in sorted order by start_page
        if not self.free_list_head or start_page < self.free_list_head.start_page:
            new_block.next = self.free_list_head
            if self.free_list_head:
                self.free_list_head.prev = new_block
            self.free_list_head = new_block
        else:
            current = self.free_list_head
            while current.next and current.next.start_page < start_page:
                current = current.next
            
            new_block.next = current.next
            new_block.prev = current
            if current.next:
                current.next.prev = new_block
            current.next = new_block
        
        self.total_free += num_pages
        
        # Coalesce with adjacent blocks
        self.coalesce(new_block)
    
    def coalesce(self, block: FreeBlock):
        """Merge adjacent free blocks"""
        # Try to merge with next
        if block.next and (block.start_page + block.num_pages == block.next.start_page):
            self.stats['coalesces'] += 1
            next_block = block.next
            block.num_pages += next_block.num_pages
            block.next = next_block.next
            if next_block.next:
                next_block.next.prev = block
        
        # Try to merge with prev
        if block.prev and (block.prev.start_page + block.prev.num_pages == block.start_page):
            self.stats['coalesces'] += 1
            prev_block = block.prev
            prev_block.num_pages += block.num_pages
            prev_block.next = block.next
            if block.next:
                block.next.prev = prev_block
    
    def allocate(self, key: str, num_pages: int) -> bool:
        """Allocate pages for a key"""
        block = self.find_best_fit(num_pages)
        
        if not block:
            if self.total_free >= num_pages:
                print(f"  [FRAGMENTED] Have {self.total_free} free but can't fit {num_pages} pages")
                self.defragment()
                block = self.find_best_fit(num_pages)
            
            if not block:
                return False
        
        start_page = block.start_page
        self.split_block(block, num_pages)
        
        self.allocations[key] = Allocation(key, start_page, num_pages)
        self.stats['allocations'] += 1
        return True
    
    def deallocate(self, key: str) -> bool:
        """Free pages for a key"""
        if key not in self.allocations:
            return False
        
        alloc = self.allocations[key]
        self.add_to_free_list(alloc.start_page, alloc.num_pages)
        del self.allocations[key]
        self.stats['deallocations'] += 1
        return True
    
    def defragment(self):
        """Compact memory to eliminate fragmentation"""
        print(f"\n{'='*60}")
        print("DEFRAGMENTATION STARTED")
        print(f"{'='*60}")
        
        self.stats['defragmentations'] += 1
        
        # Sort allocations by current position
        sorted_allocs = sorted(self.allocations.values(), key=lambda a: a.start_page)
        
        # Rebuild free list
        self.free_list_head = None
        self.total_free = 0
        
        # Move allocations to compact positions
        next_free = 0
        new_allocs = {}
        
        for alloc in sorted_allocs:
            # Update allocation to new position
            new_alloc = Allocation(alloc.key, next_free, alloc.num_pages)
            new_allocs[alloc.key] = new_alloc
            next_free += alloc.num_pages
        
        self.allocations = new_allocs
        
        # Create one large free block
        if next_free < self.total_pages:
            self.free_list_head = FreeBlock(next_free, self.total_pages - next_free)
            self.total_free = self.total_pages - next_free
        
        print(f"Compacted {len(sorted_allocs)} allocations")
        print(f"Created 1 free block of {self.total_free} pages")
        print(f"{'='*60}\n")
    
    def get_fragmentation_stats(self):
        """Calculate fragmentation metrics"""
        largest_block = 0
        num_blocks = 0
        
        current = self.free_list_head
        while current:
            num_blocks += 1
            largest_block = max(largest_block, current.num_pages)
            current = current.next
        
        frag_ratio = 0.0
        if self.total_free > 0:
            frag_ratio = 1.0 - (largest_block / self.total_free)
        
        return {
            'total_free': self.total_free,
            'largest_block': largest_block,
            'num_blocks': num_blocks,
            'fragmentation': frag_ratio * 100
        }
    
    def print_state(self):
        """Print current cache state"""
        frag = self.get_fragmentation_stats()
        
        print(f"\nCache State:")
        print(f"  Total Pages: {self.total_pages}")
        print(f"  Allocated: {self.total_pages - self.total_free} pages ({len(self.allocations)} entries)")
        print(f"  Free: {frag['total_free']} pages in {frag['num_blocks']} block(s)")
        print(f"  Largest Free Block: {frag['largest_block']} pages")
        print(f"  Fragmentation: {frag['fragmentation']:.1f}%")
        
        # Show free blocks
        if frag['num_blocks'] <= 10:
            print(f"\n  Free Blocks:")
            current = self.free_list_head
            idx = 0
            while current:
                print(f"    [{idx}] Pages {current.start_page}-{current.start_page + current.num_pages - 1} ({current.num_pages} pages)")
                current = current.next
                idx += 1
    
    def visualize(self, width=80):
        """Visual representation of cache"""
        if self.total_pages > width:
            scale = self.total_pages / width
        else:
            scale = 1
            width = self.total_pages
        
        # Create visualization array
        vis = ['.' for _ in range(width)]
        
        # Mark allocated blocks
        for alloc in self.allocations.values():
            start_idx = int(alloc.start_page / scale)
            end_idx = int((alloc.start_page + alloc.num_pages) / scale)
            for i in range(start_idx, min(end_idx + 1, width)):
                vis[i] = '#'
        
        print(f"\n  Visualization (# = allocated, . = free):")
        print(f"  {''.join(vis)}")

def demo_scenario_1():
    """Demonstrate coalescing"""
    print("\n" + "="*60)
    print("SCENARIO 1: Coalescing Adjacent Free Blocks")
    print("="*60)
    
    cache = CacheWithDefrag(100)
    
    print("\n1. Allocate 5 entries of 10 pages each")
    for i in range(5):
        cache.allocate(f"entry{i}", 10)
    
    cache.print_state()
    cache.visualize()
    
    print("\n2. Delete every other entry (create gaps)")
    cache.deallocate("entry1")
    cache.deallocate("entry3")
    
    cache.print_state()
    cache.visualize()
    print(f"  Note: {cache.stats['coalesces']} coalesces so far")
    
    print("\n3. Delete remaining entries")
    cache.deallocate("entry0")
    cache.deallocate("entry2")
    cache.deallocate("entry4")
    
    cache.print_state()
    cache.visualize()
    print(f"  Total coalesces: {cache.stats['coalesces']}")
    print("  Result: All blocks merged into ONE!")

def demo_scenario_2():
    """Demonstrate defragmentation"""
    print("\n" + "="*60)
    print("SCENARIO 2: Defragmentation When Fragmented")
    print("="*60)
    
    cache = CacheWithDefrag(100)
    
    print("\n1. Create fragmented state")
    # Allocate many small entries
    for i in range(10):
        cache.allocate(f"small{i}", 5)
    
    # Delete some randomly
    to_delete = ['small1', 'small3', 'small5', 'small7', 'small9']
    for key in to_delete:
        cache.deallocate(key)
    
    cache.print_state()
    cache.visualize()
    
    frag_before = cache.get_fragmentation_stats()
    print(f"\n  Fragmentation: {frag_before['fragmentation']:.1f}%")
    print(f"  Have {frag_before['total_free']} free pages but in {frag_before['num_blocks']} blocks")
    
    print("\n2. Try to allocate 30 pages (requires contiguous space)")
    success = cache.allocate("large", 30)
    
    if success:
        print("  SUCCESS after defragmentation!")
    else:
        print("  FAILED")
    
    cache.print_state()
    cache.visualize()
    
    frag_after = cache.get_fragmentation_stats()
    print(f"\n  Fragmentation after: {frag_after['fragmentation']:.1f}%")
    print(f"  Defragmentations: {cache.stats['defragmentations']}")

def demo_scenario_3():
    """Demonstrate realistic workload"""
    print("\n" + "="*60)
    print("SCENARIO 3: Realistic Workload Simulation")
    print("="*60)
    
    cache = CacheWithDefrag(200)
    random.seed(42)
    
    print("\n1. Simulate 50 allocate/deallocate operations")
    
    operations = []
    active_keys = []
    
    for i in range(50):
        if len(active_keys) < 20 and (random.random() < 0.6 or len(active_keys) == 0):
            # Allocate
            key = f"key{i}"
            pages = random.randint(3, 15)
            if cache.allocate(key, pages):
                active_keys.append(key)
                operations.append(f"  [{i}] ALLOC {key} ({pages} pages)")
            else:
                operations.append(f"  [{i}] ALLOC {key} ({pages} pages) - FAILED")
        elif active_keys:
            # Deallocate
            key = random.choice(active_keys)
            cache.deallocate(key)
            active_keys.remove(key)
            operations.append(f"  [{i}] FREE  {key}")
        
        if i in [15, 30, 45]:
            print(f"\nAfter {i+1} operations:")
            cache.print_state()
    
    print(f"\nFinal Statistics:")
    cache.print_state()
    cache.visualize()
    
    print(f"\nOperation Counts:")
    print(f"  Allocations: {cache.stats['allocations']}")
    print(f"  Deallocations: {cache.stats['deallocations']}")
    print(f"  Coalesces: {cache.stats['coalesces']}")
    print(f"  Defragmentations: {cache.stats['defragmentations']}")

def main():
    print("\n" + "#"*60)
    print("# CACHE DEFRAGMENTATION DEMONSTRATION")
    print("# Using Linked List Free Blocks with Coalescing")
    print("#"*60)
    
    demo_scenario_1()
    input("\n\nPress Enter to continue to Scenario 2...")
    
    demo_scenario_2()
    input("\n\nPress Enter to continue to Scenario 3...")
    
    demo_scenario_3()
    
    print("\n" + "="*60)
    print("DEMONSTRATION COMPLETE")
    print("="*60)
    print("\nKey Takeaways:")
    print("  ✓ Coalescing automatically merges adjacent free blocks")
    print("  ✓ Defragmentation compacts memory when fragmented")
    print("  ✓ Best-fit allocation minimizes waste")
    print("  ✓ Enables successful allocation even with fragmentation")
    print()

if __name__ == "__main__":
    main()
