#pragma once

#include <atomic>

#include <madrona/ecs.hpp>

namespace madrona {

class StateManager {
public:
    StateManager(uint32_t max_components);

    template <typename ComponentT>
    Entity registerComponent();

    template <typename ComponentT>
    Entity componentID();

    template <typename ArchetypeT>
    void registerArchetype();

private:
    uint32_t num_components_;

    std::atomic_uint32_t register_lock_;
};

}

#include "state.inl"
