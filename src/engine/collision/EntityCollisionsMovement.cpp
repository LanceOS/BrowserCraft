#include "EntityCollisions.hpp"
#include "MovementSolver.hpp"

namespace terrain {

void EntityCollisions::resolveMovement(
    float dx, float dy, float dz,
    cmp::Transform& transform,
    cmp::RigidBody& body) const
{
  physics::MovementSolver solver{*this, m_world, m_config, body};
  solver.resolve(dx, dy, dz, transform);
}

} // namespace terrain
