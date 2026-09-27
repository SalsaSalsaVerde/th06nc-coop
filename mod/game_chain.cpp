#include "game_chain.h"
#include "game.h"

#include <cstdio>
#include <cstring>

void Chain_InitNode(TaskNode* node, uint16_t priority, TaskNode::Callback tick, void* arg) {
    memset(node, 0, sizeof(*node));
    node->priority = priority;
    node->tick = tick;
    node->self = node;
    node->arg = arg;
}

void Chain_Insert(uintptr_t listHeadRva, TaskNode* node) {
    TaskNode* cur = Game::At<TaskNode>(listHeadRva);
    while (cur->next != nullptr) {
        if (node->priority < cur->priority) {
            break;
        }
        cur = cur->next;
    }
    if (cur->priority <= node->priority) {
        node->prev = cur;
        node->next = nullptr;
        cur->next = node;
        return;
    }
    node->next = cur;
    node->prev = cur->prev;
    if (cur->prev != nullptr) {
        cur->prev->next = node;
    }
    cur->prev = node;
}

namespace {

// Tasks that can come and go between a snapshot and a rollback without
// making the rollback unsafe: purely cosmetic, nothing the simulation reads
// (docs/14). The screen shake -- started by every bomb, several times over
// for some -- is the one that made rollbacks during bombs refuse.
bool IsCosmeticTask(const TaskNode* node) {
    return reinterpret_cast<uintptr_t>(node->tick) == Game::Base() + Game::kFnScreenShakeTick;
}

template <typename Visit>
void ForEachNode(Visit visit) {
    const uintptr_t heads[] = { Game::kUpdateListHead, Game::kDrawListHead };
    for (uintptr_t head : heads) {
        int guard = 0;
        for (TaskNode* node = Game::At<TaskNode>(head); node && guard < 4096; node = node->next, guard++) {
            if (!IsCosmeticTask(node)) visit(node);
        }
    }
}

} // namespace

uint64_t Chain_Signature() {
    uint64_t hash = 0xCBF29CE484222325ull;
    ForEachNode([&](const TaskNode* node) {
        uintptr_t values[] = { reinterpret_cast<uintptr_t>(node), reinterpret_cast<uintptr_t>(node->tick) };
        for (uintptr_t v : values) {
            hash = (hash ^ v) * 0x100000001B3ull;
        }
    });
    return hash;
}

void Chain_Capture(std::vector<ChainEntry>* out) {
    out->clear();
    ForEachNode([&](const TaskNode* node) {
        out->push_back(ChainEntry{ node, reinterpret_cast<uintptr_t>(node->tick) });
    });
}

void Chain_DescribeChange(const std::vector<ChainEntry>& before, char* out, size_t outSize) {
    std::vector<ChainEntry> after;
    Chain_Capture(&after);
    size_t len = 0;
    out[0] = '\0';
    auto add = [&](char sign, uintptr_t tick) {
        if (len + 24 >= outSize) return;
        uintptr_t base = Game::Base();
        uintptr_t rva = tick >= base && tick < base + 0x10000000 ? tick - base : tick;
        len += static_cast<size_t>(snprintf(out + len, outSize - len, " %c%llX", sign, static_cast<unsigned long long>(rva)));
    };
    auto contains = [](const std::vector<ChainEntry>& list, const ChainEntry& e) {
        for (const ChainEntry& x : list) {
            if (x.node == e.node && x.tick == e.tick) return true;
        }
        return false;
    };
    for (const ChainEntry& e : after) {
        if (!contains(before, e)) add('+', e.tick);
    }
    for (const ChainEntry& e : before) {
        if (!contains(after, e)) add('-', e.tick);
    }
}

void Chain_Cut(TaskNode* node) {
    using CutFn = void (*)(void* unused, TaskNode* node);
    Game::Fn<CutFn>(Game::kFnChainCut)(nullptr, node);
}
