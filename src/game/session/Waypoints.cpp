#include "game/session/Waypoints.h"

#include "game/session/Gate.h"

#include <algorithm>

namespace mm2::game::session {

void WaypointTracker::reset(std::span<const Checkpoint> checkpoints, WaypointRule rule, int laps,
                            bool single) {
    // mmWaypoints::Reset.
    m_rule = rule;
    m_laps = laps;
    singleVisible = single;
    const int n = static_cast<int>(checkpoints.size());
    cleared.assign(checkpoints.size(), 0);
    visible.assign(checkpoints.size(), singleVisible ? 0 : 1);
    current = std::min(1, std::max(0, n - 1));
    count = 1;
    lastCleared = 0;
    lap = 0;
    finished = false;
    stopped = false;
    if (n == 0 || rule == WaypointRule::None)
        return;
    if (rule != WaypointRule::Circuit) {
        visible[0] = 0;
        if (rule == WaypointRule::CheckpointRace && n >= 3)
            visible[static_cast<std::size_t>(n - 1)] = 0; // the finish opens later
        cleared[0] = 1;
    }
    if (singleVisible) {
        std::fill(visible.begin(), visible.end(), 0);
        visible[static_cast<std::size_t>(current)] = 1;
    }
}

void WaypointTracker::setTarget(int index) {
    // mmWaypoints::SetCurrentGoals.
    const int n = static_cast<int>(cleared.size());
    current = index < 0 ? 0 : std::min(index, n - 1);
}

void WaypointTracker::closestTarget(std::span<const Checkpoint> checkpoints, const Vec3& pos) {
    // mmWaypoints::GetClosestWaypoint: the nearest shown, uncleared one.
    const int n = static_cast<int>(std::min(checkpoints.size(), cleared.size()));
    int best = current, found = 0;
    float bestD = 1e9f;
    for (int i = 1; i < n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (cleared[k] || !visible[k])
            continue;
        ++found;
        const float d = checkpoints[k].position.dist2(pos);
        if (d < bestD && best != 0) {
            bestD = d;
            best = i;
        }
    }
    setTarget(found ? best : current);
}

void WaypointTracker::cycleCurrent(bool forward) {
    // mmWaypoints::CycleCurrentWaypoint (GetNextWaypoint / GetLastWaypoint).
    // OpenMM2 also stops for fewer than three waypoints, where the original
    // could index past the list.
    const int n = static_cast<int>(cleared.size());
    if (n < 3 || finished)
        return;
    if (count == n - 1) {
        setTarget(n - 1);
        return;
    }
    const int step = forward ? 1 : -1;
    int i = current;
    for (int guard = 0;; ++guard) {
        i = (i + step) % n;
        if (i == 0 || i == n - 1)
            i = forward ? 1 : n - 2;
        if (!cleared[static_cast<std::size_t>(i)])
            break;
        if (i == current || guard > n)
            return;
    }
    setTarget(i);
}

void WaypointTracker::markCleared(int index, std::vector<WaypointStep>& steps) {
    // mmWaypoints::DisplayHUDMessage's bookkeeping (the HUD is the caller's).
    const auto k = static_cast<std::size_t>(index);
    lastCleared = index;
    cleared[k] = 1;
    visible[k] = 0;
    ++count;
    steps.push_back({WaypointStep::Kind::Cleared, index, count});
}

int WaypointTracker::detect(std::span<const Checkpoint> checkpoints, const Mat34& car,
                            const Vec3& inertiaBox) const {
    const int n = static_cast<int>(std::min(checkpoints.size(), cleared.size()));
    if (n < 2 || m_rule == WaypointRule::None || stopped || finished)
        return -1;
    auto hit = [&](int i) {
        const Checkpoint& cp = checkpoints[static_cast<std::size_t>(i)];
        if (m_rule == WaypointRule::AnyOrderEnd && cp.hitByRadius)
            return radiusHit(cp, car.m3);
        return playerGateHit(cp, car, inertiaBox);
    };
    switch (m_rule) {
    case WaypointRule::Blitz:
    case WaypointRule::CheckpointRace:
    case WaypointRule::AnyOrderEnd:
        // mmWaypoints::ClearWaypoint: the first uncleared waypoint hit.
        for (int i = 0; i < n; ++i)
            if (!cleared[static_cast<std::size_t>(i)] && hit(i))
                return i;
        return -1;
    case WaypointRule::InOrder:
    case WaypointRule::Circuit: return current >= 0 && current < n && hit(current) ? current : -1;
    case WaypointRule::None: break;
    }
    return -1;
}

bool WaypointTracker::apply(std::span<const Checkpoint> checkpoints, int index, const Vec3& position,
                            std::vector<WaypointStep>& steps) {
    const int n = static_cast<int>(std::min(checkpoints.size(), cleared.size()));
    if (n < 2 || m_rule == WaypointRule::None || stopped || finished || index < 0 || index >= n)
        return false;
    const auto k = static_cast<std::size_t>(index);
    switch (m_rule) {
    case WaypointRule::Blitz:
        if (cleared[k])
            return false;
        markCleared(index, steps);
        if (std::any_of(cleared.begin(), cleared.end(), [](char c) { return c == 0; })) {
            closestTarget(checkpoints, position);
            if (singleVisible)
                visible[static_cast<std::size_t>(current)] = 1;
        } else {
            finished = true; // the last checkpoint ends a Blitz
            steps.push_back({WaypointStep::Kind::Finished});
        }
        return true;
    case WaypointRule::CheckpointRace:
    case WaypointRule::AnyOrderEnd:
        if (cleared[k])
            return false;
        if (index == n - 1 && count == n - 1) {
            finished = true;
            steps.push_back({WaypointStep::Kind::Finished});
            return true;
        }
        if (index <= 0 || index >= n - 1)
            return false;
        markCleared(index, steps);
        if (m_rule == WaypointRule::CheckpointRace) {
            if (count == n - 1) {
                visible[static_cast<std::size_t>(n - 1)] = 1;
                steps.push_back({WaypointStep::Kind::FinishActivated});
            }
            closestTarget(checkpoints, position);
        } else if (index == current) {
            cycleCurrent(true);
        }
        if (singleVisible)
            visible[static_cast<std::size_t>(current)] = 1;
        if (count == n - 1) {
            visible[static_cast<std::size_t>(current)] = 1;
            steps.push_back({WaypointStep::Kind::FinalCheckpoint});
        }
        return true;
    case WaypointRule::InOrder: {
        if (index != current)
            return false;
        const int passed = current;
        visible[static_cast<std::size_t>(passed)] = 0;
        ++current;
        markCleared(passed, steps);
        if (count == n) {
            finished = true;
            steps.push_back({WaypointStep::Kind::Finished});
        } else if (singleVisible) {
            visible[static_cast<std::size_t>(current)] = 1;
        }
        if (current >= n)
            current = n - 1;
        return true;
    }
    case WaypointRule::Circuit: {
        if (index != current)
            return false;
        const int passed = current;
        visible[static_cast<std::size_t>(passed)] = 0;
        markCleared(passed, steps);
        if (passed == 0) {
            ++lap;
            steps.push_back({WaypointStep::Kind::LapCompleted, lap});
            if (lap == m_laps) {
                finished = true;
                steps.push_back({WaypointStep::Kind::Finished});
            } else {
                // mmWaypoints::ResetAllTags: every gate shows again.
                std::fill(cleared.begin(), cleared.end(), 0);
                std::fill(visible.begin(), visible.end(), singleVisible ? 0 : 1);
            }
        } else if (passed == n - 1 && lap == m_laps - 1) {
            steps.push_back({WaypointStep::Kind::FinalCheckpoint});
        }
        current = passed + 1 == n ? 0 : passed + 1;
        if (singleVisible && !finished)
            visible[static_cast<std::size_t>(current)] = 1;
        return true;
    }
    case WaypointRule::None: break;
    }
    return false;
}

void WaypointTracker::update(std::span<const Checkpoint> checkpoints, const Mat34& car, const Vec3& inertiaBox,
                             std::vector<WaypointStep>& steps) {
    const int index = detect(checkpoints, car, inertiaBox);
    if (index >= 0)
        apply(checkpoints, index, car.m3, steps);
}

} // namespace mm2::game::session
