#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

// Qt-independent ownership policy, shared by the production Qt adapter and
// standalone tests. A key-down and its key-up always use the same route.
class KeyboardRoutingPolicy {
public:
    enum Route { Ignore, Converter, Uhid };
    enum EventType { Press, Release, Other };
    struct Decision {
        Route route = Ignore;
        int logicalKey = 0;
    };
    Decision dispatch(std::uint64_t id, EventType type, bool repeat,
                      int logicalKey, Route preferred) {
        if (type == Other) { return {}; }
        auto it = m_held.find(id);
        if (repeat) {
            // Qt may synthesize press/release pairs during auto-repeat.
            // A cleared hold must never reappear through auto-repeat.
            return it == m_held.end() ? Decision{} : it->second;
        }
        if (type == Release) {
            if (it == m_held.end()) { return {}; }
            const Decision result = it->second;
            m_held.erase(it);
            return result;
        }
        if (it != m_held.end()) { return {}; }
        Decision result;
        result.route = preferred;
        result.logicalKey = logicalKey;
        m_held.emplace(id, result);
        return result;
    }
    void clear() { m_held.clear(); }
    void clearUhid() {
        for (auto it = m_held.begin(); it != m_held.end();) {
            if (it->second.route == Uhid) { it = m_held.erase(it); }
            else { ++it; }
        }
    }
    std::size_t heldCount() const { return m_held.size(); }
private:
    std::unordered_map<std::uint64_t, Decision> m_held;
};
