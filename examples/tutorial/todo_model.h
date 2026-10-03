// todo_model.h - the app's data, with no UI in it (chapter 3 and 9).
//
// Plain structs and free functions: the UI reads and writes these directly, and
// the tests (todo_test.cpp) call them without opening a window.
#pragma once

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

struct todo_item
{
    unsigned id = 0; // stable identity: the UI keys widgets and animations by it
    std::string title;
    bool done = false;
};

enum todo_filter
{
    FILTER_ALL = 0,
    FILTER_ACTIVE = 1,
    FILTER_DONE = 2,
};

struct todo_list
{
    std::vector<todo_item> items;
    unsigned next_id = 1;
};

// Adds a task (surrounding spaces trimmed). Returns false for an empty title.
inline bool todo_add(todo_list &list, const std::string &title)
{
    const size_t a = title.find_first_not_of(" \t");
    if (a == std::string::npos) return false;
    const size_t b = title.find_last_not_of(" \t");
    list.items.push_back({list.next_id++, title.substr(a, b - a + 1), false});
    return true;
}

inline void todo_remove(todo_list &list, unsigned id)
{
    for (size_t i = 0; i < list.items.size(); ++i)
    {
        if (list.items[i].id == id)
        {
            list.items.erase(list.items.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

inline int todo_count_done(const todo_list &list)
{
    int n = 0;
    for (const todo_item &t : list.items) n += t.done ? 1 : 0;
    return n;
}

inline void todo_clear_done(todo_list &list)
{
    std::vector<todo_item> kept;
    for (const todo_item &t : list.items)
        if (!t.done) kept.push_back(t);
    list.items.swap(kept);
}

inline bool todo_visible(const todo_item &t, int filter)
{
    return filter == FILTER_ALL || (filter == FILTER_ACTIVE && !t.done) ||
           (filter == FILTER_DONE && t.done);
}

// One line per task: "0 Buy milk" / "1 Write the tutorial" (done flag, a space, title).
inline bool todo_save(const todo_list &list, const char *path)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const todo_item &t : list.items) out << (t.done ? '1' : '0') << ' ' << t.title << '\n';
    return static_cast<bool>(out);
}

inline void todo_load(todo_list &list, const char *path)
{
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.size() < 3 || (line[0] != '0' && line[0] != '1') || line[1] != ' ') continue;
        if (todo_add(list, line.substr(2))) list.items.back().done = (line[0] == '1');
    }
}
