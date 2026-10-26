#pragma once

#include "optimize/traversal/lookup.h"

namespace qvann {
// The same routing policy is used by the server and client replay.
template <class Codes, class Neighbors>
Trace route(const Lookup& codec, const float* q, int budget, Id first, Id maxlevel, Codes code,
            Neighbors adjacent) {
    codec.prepare(q);
    Trace t;
    std::map<Id, float> scores;
    auto discover = [&](Id id) {
        if (!scores.count(id)) {
            scores[id] = codec.score(q, code(id));
        }
    };
    auto open = [&](Id id, Id level) {
        Key key{id, level};
        if (t.opened.count(key)) {
            return std::vector<Id>{};
        }
        if (!t.expanded.count(id)) {
            if (int(t.expanded.size()) >= budget) {
                return std::vector<Id>{};
            }
            t.expanded.insert(id);
        }
        t.opened.insert(key);
        return adjacent(id, level);
    };
    Id entry = first;
    discover(entry);
    bool stopped = false;
    for (Id level = maxlevel; level > 0 && !stopped; --level) {
        while (true) {
            auto ns = open(entry, level);
            if (ns.empty()) {
                stopped = int(t.expanded.size()) >= budget;
                break;
            }
            for (Id id : ns) {
                discover(id);
            }
            ns.push_back(entry);
            Id next = *std::min_element(ns.begin(), ns.end(), [&](Id a, Id b) {
                return std::make_pair(scores.at(a), a) < std::make_pair(scores.at(b), b);
            });
            if (next == entry) {
                break;
            }
            entry = next;
        }
    }
    std::set<std::pair<float, Id>> frontier;
    for (auto [id, s] : scores) {
        frontier.insert({s, id});
    }
    while (int(t.expanded.size()) < budget && !frontier.empty()) {
        Id id = frontier.begin()->second;
        frontier.erase(frontier.begin());
        for (Id nb : open(id, 0)) {
            bool fresh = !scores.count(nb);
            discover(nb);
            if (fresh) {
                frontier.insert({scores.at(nb), nb});
            }
        }
    }
    for (auto [id, s] : scores) {
        t.disclosed.push_back(id);
    }
    return t;
}

} // namespace qvann
