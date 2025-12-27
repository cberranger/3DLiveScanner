#ifndef UTILS_UNION_FIND_H
#define UTILS_UNION_FIND_H

/**
 * Union-Find (Disjoint Set Union) Data Structure
 * 
 * Provides near-constant time operations for:
 * - Finding which set an element belongs to
 * - Merging two sets
 * 
 * Uses path compression and union by rank for O(α(n)) amortized time,
 * where α is the inverse Ackermann function (effectively constant).
 * 
 * Used in TangoScan for efficient component merging.
 */

#include <vector>
#include <unordered_map>
#include <cstdint>

namespace oc {

/**
 * Basic Union-Find with integer indices
 */
class UnionFind {
public:
    /**
     * Create a Union-Find structure with n elements
     * Initially, each element is in its own set
     */
    explicit UnionFind(size_t n = 0) {
        resize(n);
    }
    
    /**
     * Resize to hold n elements
     */
    void resize(size_t n) {
        parent_.resize(n);
        rank_.resize(n, 0);
        for (size_t i = 0; i < n; ++i) {
            parent_[i] = static_cast<int>(i);
        }
    }
    
    /**
     * Add a new element and return its index
     */
    int addElement() {
        int idx = static_cast<int>(parent_.size());
        parent_.push_back(idx);
        rank_.push_back(0);
        return idx;
    }
    
    /**
     * Find the representative (root) of the set containing x
     * Uses path compression for efficiency
     */
    int find(int x) {
        if (parent_[x] != x) {
            parent_[x] = find(parent_[x]);  // Path compression
        }
        return parent_[x];
    }
    
    /**
     * Unite the sets containing x and y
     * Uses union by rank for efficiency
     * @return true if sets were different and merged, false if already same set
     */
    bool unite(int x, int y) {
        int rootX = find(x);
        int rootY = find(y);
        
        if (rootX == rootY) {
            return false;  // Already in same set
        }
        
        // Union by rank
        if (rank_[rootX] < rank_[rootY]) {
            parent_[rootX] = rootY;
        } else if (rank_[rootX] > rank_[rootY]) {
            parent_[rootY] = rootX;
        } else {
            parent_[rootY] = rootX;
            rank_[rootX]++;
        }
        
        return true;
    }
    
    /**
     * Check if x and y are in the same set
     */
    bool connected(int x, int y) {
        return find(x) == find(y);
    }
    
    /**
     * Get current number of elements
     */
    size_t size() const {
        return parent_.size();
    }
    
    /**
     * Clear all elements
     */
    void clear() {
        parent_.clear();
        rank_.clear();
    }

private:
    std::vector<int> parent_;
    std::vector<int> rank_;
};

/**
 * Union-Find with arbitrary key types using hash map
 * Useful when elements aren't contiguous integers
 */
template<typename Key, typename Hash = std::hash<Key>>
class UnionFindMap {
public:
    /**
     * Get or create the index for a key
     */
    int getIndex(const Key& key) {
        auto it = keyToIndex_.find(key);
        if (it != keyToIndex_.end()) {
            return it->second;
        }
        int idx = uf_.addElement();
        keyToIndex_[key] = idx;
        indexToKey_.push_back(key);
        return idx;
    }
    
    /**
     * Find the representative key for the set containing key
     */
    Key findKey(const Key& key) {
        int idx = getIndex(key);
        int root = uf_.find(idx);
        return indexToKey_[root];
    }
    
    /**
     * Find the representative index for the set containing key
     */
    int find(const Key& key) {
        int idx = getIndex(key);
        return uf_.find(idx);
    }
    
    /**
     * Unite the sets containing key1 and key2
     */
    bool unite(const Key& key1, const Key& key2) {
        int idx1 = getIndex(key1);
        int idx2 = getIndex(key2);
        return uf_.unite(idx1, idx2);
    }
    
    /**
     * Check if key1 and key2 are in the same set
     */
    bool connected(const Key& key1, const Key& key2) {
        auto it1 = keyToIndex_.find(key1);
        auto it2 = keyToIndex_.find(key2);
        if (it1 == keyToIndex_.end() || it2 == keyToIndex_.end()) {
            return false;
        }
        return uf_.connected(it1->second, it2->second);
    }
    
    /**
     * Check if a key exists
     */
    bool contains(const Key& key) const {
        return keyToIndex_.find(key) != keyToIndex_.end();
    }
    
    /**
     * Get number of unique keys
     */
    size_t size() const {
        return keyToIndex_.size();
    }
    
    /**
     * Clear all data
     */
    void clear() {
        uf_.clear();
        keyToIndex_.clear();
        indexToKey_.clear();
    }

private:
    UnionFind uf_;
    std::unordered_map<Key, int, Hash> keyToIndex_;
    std::vector<Key> indexToKey_;
};

/**
 * Fast integer-pair hasher for edge keys
 * Replaces slow string-based edge keys
 */
struct EdgeKey {
    int32_t v1;
    int32_t v2;
    
    EdgeKey() : v1(0), v2(0) {}
    EdgeKey(int32_t a, int32_t b) : v1(a), v2(b) {}
    
    bool operator==(const EdgeKey& other) const {
        return v1 == other.v1 && v2 == other.v2;
    }
    
    // Get the reverse edge
    EdgeKey reverse() const {
        return EdgeKey(v2, v1);
    }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        // Combine two 32-bit ints into one 64-bit hash
        return static_cast<size_t>(k.v1) ^ (static_cast<size_t>(k.v2) << 32);
    }
};

/**
 * Vertex key based on quantized position
 * Replaces slow string-based vertex keys
 */
struct VertexKey {
    int32_t x, y, z;
    
    VertexKey() : x(0), y(0), z(0) {}
    
    // Quantize float position to integer key (0.001 precision)
    VertexKey(float fx, float fy, float fz) {
        x = static_cast<int32_t>(fx * 1000.0f);
        y = static_cast<int32_t>(fy * 1000.0f);
        z = static_cast<int32_t>(fz * 1000.0f);
    }
    
    bool operator==(const VertexKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& k) const {
        // FNV-1a style hash
        size_t h = 14695981039346656037ULL;
        h ^= static_cast<size_t>(k.x);
        h *= 1099511628211ULL;
        h ^= static_cast<size_t>(k.y);
        h *= 1099511628211ULL;
        h ^= static_cast<size_t>(k.z);
        h *= 1099511628211ULL;
        return h;
    }
};

} // namespace oc

#endif // UTILS_UNION_FIND_H
