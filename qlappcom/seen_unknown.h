#ifndef SEEN_UNKNOWN_H
#define SEEN_UNKNOWN_H

#include <string>
#include <cstddef>

// Forward declarations
namespace ap { class bloom_filter; }
struct bloom_filter;  // barrust

class SeenUnknown {
public:
    SeenUnknown();
    ~SeenUnknown();

    // Returns true if this is a NEW unknown line (not seen before)
    // Returns false if already seen (deduplicated)
    bool insert(const std::string& line);

    // Check without inserting
    bool contains(const std::string& line) const;

    // Reset both filters
    void clear();

    // Approximate count of inserted elements
    size_t approxCount() const;

private:
    ap::bloom_filter* m_primary;   // ArashPartow, 64KB, 1% FPR
    bloom_filter* m_secondary;      // barrust, 64KB, 1% FPR
};

#endif