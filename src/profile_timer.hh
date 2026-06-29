#pragma once

#if defined(FLUID_PROFILE) && FLUID_PROFILE

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Fluid_Profile
{
struct Entry
{
    std::string name;
    std::uint64_t calls = 0;
    double inclusive_ms = 0.0;
    double exclusive_ms = 0.0;
};

struct Stack_Entry
{
    int id = 0;
    std::chrono::steady_clock::time_point start;
    double child_ms = 0.0;
};

inline std::vector<Entry>& entries()
{
    static std::vector<Entry> data;
    return data;
}

inline std::unordered_map<std::string, int>& ids()
{
    static std::unordered_map<std::string, int> data;
    return data;
}

inline std::vector<Stack_Entry>& stack()
{
    static std::vector<Stack_Entry> data;
    return data;
}

inline int id_for(std::string_view name)
{
    auto key = std::string(name);
    auto& known_ids = ids();
    auto it = known_ids.find(key);
    if (it != known_ids.end())
        return it->second;

    int id = static_cast<int>(entries().size());
    known_ids.emplace(key, id);
    entries().push_back(Entry{.name = std::move(key)});
    return id;
}

class Scope
{
public:
    explicit Scope(std::string_view name)
    {
        stack().push_back(Stack_Entry{.id = id_for(name), .start = std::chrono::steady_clock::now()});
    }

    ~Scope()
    {
        auto end = std::chrono::steady_clock::now();
        Stack_Entry active = stack().back();
        stack().pop_back();

        double inclusive_ms = std::chrono::duration<double, std::milli>(end - active.start).count();
        double exclusive_ms = inclusive_ms - active.child_ms;

        Entry& entry = entries()[active.id];
        ++entry.calls;
        entry.inclusive_ms += inclusive_ms;
        entry.exclusive_ms += exclusive_ms;

        if (!stack().empty())
            stack().back().child_ms += inclusive_ms;
    }
};

inline void reset()
{
    entries().clear();
    ids().clear();
    stack().clear();
}

inline void print_report(std::ostream& out, std::size_t limit = 40)
{
    std::vector<Entry> sorted = entries();
    std::sort(sorted.begin(), sorted.end(), [](Entry const& lhs, Entry const& rhs)
    {
        return lhs.exclusive_ms > rhs.exclusive_ms;
    });

    out << "rank,function,calls,exclusive_ms,inclusive_ms,exclusive_percent,avg_exclusive_ms\n";
    double total_exclusive = 0.0;
    for (Entry const& entry : sorted)
        total_exclusive += entry.exclusive_ms;

    std::size_t count = std::min(limit, sorted.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        Entry const& entry = sorted[i];
        double exclusive_percent = total_exclusive > 0.0 ? (entry.exclusive_ms / total_exclusive) * 100.0 : 0.0;
        double avg_exclusive = entry.calls > 0 ? entry.exclusive_ms / static_cast<double>(entry.calls) : 0.0;
        out << (i + 1) << ','
            << '"' << entry.name << '"' << ','
            << entry.calls << ','
            << std::fixed << std::setprecision(3) << entry.exclusive_ms << ','
            << std::fixed << std::setprecision(3) << entry.inclusive_ms << ','
            << std::fixed << std::setprecision(2) << exclusive_percent << ','
            << std::fixed << std::setprecision(6) << avg_exclusive << '\n';
    }
}
} // namespace Fluid_Profile

#define FLUID_PROFILE_CONCAT_IMPL(a, b) a##b
#define FLUID_PROFILE_CONCAT(a, b) FLUID_PROFILE_CONCAT_IMPL(a, b)
#define FLUID_PROFILE_SCOPE(name) Fluid_Profile::Scope FLUID_PROFILE_CONCAT(profile_scope_, __LINE__)(name)

#else

#define FLUID_PROFILE_SCOPE(name) ((void)0)

#endif
