#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The game's scheduler task node (overlay/08; field meanings confirmed against
// FUN_1400694d0's registration and FUN_140012e10's cut).
struct TaskNode {
    using Callback = uint64_t (*)(void* arg);

    uint16_t priority;  // +0x00, list sorted ascending
    uint8_t flags;      // +0x02, bit 0 = heap-owned: cut frees it with the game's delete
    uint8_t pad[5];
    Callback tick;      // +0x08
    Callback added;     // +0x10, called once on insert
    Callback deleted;   // +0x18, called once on cut
    TaskNode* prev;     // +0x20
    TaskNode* next;     // +0x28
    TaskNode* self;     // +0x30
    void* arg;          // +0x38, passed to every callback
};
static_assert(sizeof(TaskNode) == 0x40, "TaskNode layout");

// Prepares a mod-owned node (not heap-owned, no added/deleted callbacks).
void Chain_InitNode(TaskNode* node, uint16_t priority, TaskNode::Callback tick, void* arg);

// Inserts after every existing node of equal priority, exactly like the
// game's own inline insertion code.
void Chain_Insert(uintptr_t listHeadRva, TaskNode* node);

// Unlinks via the game's own cut routine. Safe on a node that isn't linked.
void Chain_Cut(TaskNode* node);

// Hash of both lists' node identities (process-local: contains pointers).
// Snapshots don't cover heap-allocated task nodes, so a rollback across a
// frame where this changed can't be done safely (docs/02). Cosmetic tasks
// (the screen shake) are left out: they may come and go (docs/14).
uint64_t Chain_Signature();

// The lists' non-cosmetic nodes, and a "+tick -tick" description (tick
// function RVAs) of what changed since a capture -- for the log.
struct ChainEntry {
    const void* node;
    uintptr_t tick;
};
void Chain_Capture(std::vector<ChainEntry>* out);
void Chain_DescribeChange(const std::vector<ChainEntry>& before, char* out, size_t outSize);
