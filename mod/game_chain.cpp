#include "game_chain.h"
#include "game.h"

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

uint64_t Chain_Signature() {
    uint64_t hash = 0xCBF29CE484222325ull;
    const uintptr_t heads[] = { Game::kUpdateListHead, Game::kDrawListHead };
    for (uintptr_t head : heads) {
        int guard = 0;
        for (TaskNode* node = Game::At<TaskNode>(head); node && guard < 4096; node = node->next, guard++) {
            uintptr_t values[] = { reinterpret_cast<uintptr_t>(node), reinterpret_cast<uintptr_t>(node->tick) };
            for (uintptr_t v : values) {
                hash = (hash ^ v) * 0x100000001B3ull;
            }
        }
    }
    return hash;
}

void Chain_Cut(TaskNode* node) {
    using CutFn = void (*)(void* unused, TaskNode* node);
    Game::Fn<CutFn>(Game::kFnChainCut)(nullptr, node);
}
