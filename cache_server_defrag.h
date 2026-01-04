#ifndef CACHE_SERVER_DEFRAG_H
#define CACHE_SERVER_DEFRAG_H

#include <string>
#include <unordered_map>
#include <vector>
#include <list>
#include <queue>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <sys/epoll.h>

// Constants
constexpr size_t CACHE_SIZE = 100ULL * 1024 * 1024; // 100 MB
constexpr size_t PAGE_SIZE = 40 * 1024; // 40 KB
constexpr size_t TOTAL_PAGES = CACHE_SIZE / PAGE_SIZE;
constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;
constexpr int NUM_WORKER_THREADS = 4;

// Eviction policies
enum class EvictionPolicy {
    LRU,
    FIFO,
    SIEVE,
    CLOCK
};

// Free block in the linked list
struct FreeBlock {
    size_t start_page;
    size_t num_pages;
    FreeBlock* next;
    FreeBlock* prev;
    
    FreeBlock(size_t start, size_t count) 
        : start_page(start), num_pages(count), next(nullptr), prev(nullptr) {}
};

// Page structure
struct Page {
    uint8_t data[PAGE_SIZE];
    bool is_free;
    size_t block_start;  // Start of the contiguous block this page belongs to
    
    Page() : is_free(true), block_start(0) {}
};

// Cache entry metadata
struct CacheEntry {
    std::string key;
    std::string client_id;
    size_t start_page;
    size_t num_pages;
    size_t data_size;
    
    // Policy-specific data
    std::list<std::string>::iterator lru_iter;
    size_t insertion_order;
    bool visited;
    bool reference_bit;
    size_t clock_position;
    
    CacheEntry() : start_page(0), num_pages(0), data_size(0), 
                   insertion_order(0), visited(false), 
                   reference_bit(false), clock_position(0) {}
};

// Client connection state
struct ClientConnection {
    int fd;
    std::string client_id;
    std::string buffer;
    bool authenticated;
    
    ClientConnection() : fd(-1), authenticated(false) {}
    ClientConnection(int socket_fd) 
        : fd(socket_fd), authenticated(false) {}
};

// Protocol command
struct Command {
    std::string method;
    std::string key;
    std::string value;
    bool valid;
    
    Command() : valid(false) {}
};

// Work item for thread pool
struct WorkItem {
    int client_fd;
    std::string data;
};

// Cache statistics
struct CacheStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> adds{0};
    std::atomic<uint64_t> updates{0};
    std::atomic<uint64_t> deletes{0};
    std::atomic<uint64_t> defragmentations{0};
    std::atomic<uint64_t> coalesces{0};
    
    double getHitRatio() const {
        uint64_t total = total_requests.load();
        return total > 0 ? (double)hits.load() / total : 0.0;
    }
    
    void reset() {
        total_requests = 0;
        hits = 0;
        misses = 0;
        evictions = 0;
        adds = 0;
        updates = 0;
        deletes = 0;
        defragmentations = 0;
        coalesces = 0;
    }
};

// Fragmentation statistics
struct FragmentationStats {
    size_t total_free_pages;
    size_t largest_free_block;
    size_t num_free_blocks;
    double fragmentation_ratio;  // 1.0 - (largest_block / total_free)
    
    FragmentationStats() : total_free_pages(0), largest_free_block(0), 
                          num_free_blocks(0), fragmentation_ratio(0.0) {}
};

// Enhanced Cache Server with Defragmentation
class CacheServerDefrag {
private:
    int server_fd;
    int epoll_fd;
    std::vector<Page> cache;
    std::unordered_map<std::string, CacheEntry> entries;
    std::unordered_map<int, ClientConnection> clients;
    
    // Free list management (doubly-linked list of free blocks)
    FreeBlock* free_list_head;
    size_t total_free_pages;
    
    // Eviction policy
    EvictionPolicy policy;
    
    // LRU data structures
    std::list<std::string> lru_list;
    
    // FIFO data structures
    std::queue<std::string> fifo_queue;
    size_t fifo_counter;
    
    // SIEVE data structures
    std::list<std::string> sieve_list;
    std::list<std::string>::iterator sieve_hand;
    
    // Clock data structures
    std::vector<std::string> clock_list;
    size_t clock_hand;
    
    // Thread pool
    std::vector<std::thread> worker_threads;
    std::queue<WorkItem> work_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> should_stop{false};
    
    // Cache mutex for thread safety
    std::mutex cache_mutex;
    
    // Statistics
    CacheStats stats;
    
    // Private methods
    bool initializeCache();
    bool setupServer(int port);
    bool setupEpoll();
    void startWorkerThreads();
    void stopWorkerThreads();
    void workerThreadFunction();
    
    void handleNewConnection();
    void handleClientData(int client_fd);
    void handleClientDisconnect(int client_fd);
    
    Command parseCommand(const std::string& message);
    std::string processCommand(const Command& cmd, const std::string& client_id);
    
    // Cache operations
    std::string addKey(const std::string& key, const std::string& value, const std::string& client_id);
    std::string updateKey(const std::string& key, const std::string& value, const std::string& client_id);
    std::string getKey(const std::string& key, const std::string& client_id);
    std::string deleteKey(const std::string& key, const std::string& client_id);
    
    // Memory management with free list
    bool allocatePages(const std::string& key, size_t data_size, const std::string& client_id);
    void freePages(const std::string& key);
    FreeBlock* findBestFitBlock(size_t num_pages);
    FreeBlock* findFirstFitBlock(size_t num_pages);
    void splitBlock(FreeBlock* block, size_t num_pages);
    void addToFreeList(size_t start_page, size_t num_pages);
    void removeFromFreeList(FreeBlock* block);
    void coalesceAdjacentBlocks(FreeBlock* block);
    size_t calculateRequiredPages(size_t data_size);
    
    // Defragmentation
    bool defragment(size_t required_pages);
    void compactMemory();
    FragmentationStats getFragmentationStats();
    
    // Eviction policies
    bool evict(size_t required_pages);
    bool evictLRU(size_t required_pages);
    bool evictFIFO(size_t required_pages);
    bool evictSIEVE(size_t required_pages);
    bool evictClock(size_t required_pages);
    
    // Policy-specific updates
    void updatePolicy(const std::string& key);
    void updateLRU(const std::string& key);
    void updateFIFO(const std::string& key);
    void updateSIEVE(const std::string& key);
    void updateClock(const std::string& key);
    
    // Data operations
    bool writeToPages(size_t start_page, const std::string& data);
    std::string readFromPages(size_t start_page, size_t data_size);
    
    // Utility
    void sendResponse(int client_fd, const std::string& response);
    std::string getClientId(int client_fd);
    void printFreeList();

public:
    CacheServerDefrag(EvictionPolicy eviction_policy = EvictionPolicy::LRU);
    ~CacheServerDefrag();
    
    bool start(int port);
    void run();
    void stop();
    
    // Statistics
    const CacheStats& getStats() const { return stats; }
    void resetStats() { stats.reset(); }
    void printStats() const;
    void printFragmentationStats();
};

#endif // CACHE_SERVER_DEFRAG_H
