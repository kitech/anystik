#include "seen_unknown.h"
#include "bloom_filter.hpp"  // ArashPartow (namespace ap)
#include "bloom.h"           // barrust
#include <cstring>

// Memory budget per filter: 64KB = 65536 bytes
// ArashPartow: bit_table_ uses table_size/8 bytes, salt_ uses k*4 bytes
// barrust: bloom uses ceil(number_bits/8) bytes
// At 1% FPR with ~54600 elements, optimal k≈7, m≈523776 bits = 65472 bytes

static const size_t kBloomMemoryBytes = 64 * 1024;  // 64KB per filter
static const double kFalsePositiveRate = 0.01;      // 1%
static const size_t kProjectedElements = 54600;     // fits in 64KB at 1% FPR

SeenUnknown::SeenUnknown() {
    // ── ArashPartow bloom (C++ header-only) ──
    ap::bloom_parameters params;
    params.projected_element_count = kProjectedElements;
    params.false_positive_probability = kFalsePositiveRate;
    params.random_seed = 0xA5A5A5A55A5A5A5AULL;
    if (params.compute_optimal_parameters()) {
        m_primary = new ap::bloom_filter(params);
    } else {
        m_primary = nullptr;
    }

    // ── barrust bloom (C library) ──
    m_secondary = new bloom_filter;
    bloom_filter_init(m_secondary, kProjectedElements, kFalsePositiveRate);
}

SeenUnknown::~SeenUnknown() {
    delete m_primary;
    bloom_filter_destroy(m_secondary);
    delete m_secondary;
}

bool SeenUnknown::insert(const std::string& line) {
    bool primary_has = m_primary ? m_primary->contains(line) : false;
    bool secondary_has = bloom_filter_check_string(m_secondary, line.c_str()) == BLOOM_SUCCESS;

    if (m_primary) {
        m_primary->insert(line);
    }
    bloom_filter_add_string(m_secondary, line.c_str());

    // OR logic: if at least one filter didn't have it, it's new
    return !primary_has || !secondary_has;
}

bool SeenUnknown::contains(const std::string& line) const {
    bool primary_has = m_primary ? m_primary->contains(line) : false;
    bool secondary_has = bloom_filter_check_string(m_secondary, line.c_str()) == BLOOM_SUCCESS;

    // AND logic: both must agree it's present
    return primary_has && secondary_has;
}

void SeenUnknown::clear() {
    if (m_primary) {
        m_primary->clear();
    }
    bloom_filter_clear(m_secondary);
}

size_t SeenUnknown::approxCount() const {
    if (m_primary) {
        return static_cast<size_t>(m_primary->element_count());
    }
    return 0;
}