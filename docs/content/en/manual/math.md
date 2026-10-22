# Math

illusion's math types are based on [glm](https://github.com/g-truc/glm). Include
`<illusion/core/Math.hpp>` instead of glm directly, so that every file uses the same configuration.

Header: `#include <illusion/core/Math.hpp>`

## Conventions

- Right-handed world coordinates with +y up; cameras look down -z.
- Angles are in radians.
- Clip space has y pointing up and depth in [0, 1]. The renderer uses **reversed-Z**: depth is 1 at the
  near plane and 0 at the far plane (or at infinity), which gives far better depth precision.
- Matrices are column-major (`m[column][row]`), as in glm.

| Type | Meaning |
| --- | --- |
| `Vec2`, `Vec3`, `Vec4`, `IVec2`, `UVec2` | Vectors |
| `Mat3`, `Mat4` | Matrices |
| `Quat` | Rotation quaternion |
| `Aabb`, `Sphere`, `Obb` | Axis-aligned box, sphere, oriented box |
| `Plane`, `Frustum` | Plane, six planes of a view volume |
| `Ray` | Half-line for picking |

## Projections

```cpp
const illusion::Mat4 perspective = illusion::perspectiveReversedZ(fovY, aspect, 0.1f); // infinite far plane
const illusion::Mat4 orthographic =
    illusion::orthographicReversedZ(-width / 2, width / 2, -height / 2, height / 2, 0.1f, 500.0f);
```

`perspectiveReversedZ()` has an infinite far plane. `orthographicReversedZ()` maps the near plane to depth
1 and the far plane to 0.

## Rays and intersections

`Ray{origin, direction}` is a half-line. The intersection functions accept any non-zero direction and
return the ray parameter `t` of the hit (the point is `ray.at(t)`); with a unit direction `t` is the
distance. A miss, or a hit behind the origin, returns `std::nullopt`.

| Function | Result |
| --- | --- |
| `intersect(ray, aabb)`, `intersect(ray, obb)` | Entry parameter; 0 if the origin is inside |
| `intersect(ray, sphere)` | Entry parameter; 0 if the origin is inside |
| `intersect(ray, plane)` | Crossing from either side; misses parallel rays |
| `intersect(ray, a, b, c)` | `RayTriangleHit{t, u, v}` with barycentric weights; both sides, edges included |
| `closestPoints(ray, start, end)` | Closest points between the ray and a segment (`rayT`, `segmentT`, `distance`) |

Edge cases are defined: points on a box's surface count as inside, a ray parallel to a box face hits only
if its origin lies between the face's planes, a ray in a triangle's plane misses it, and for a ray parallel
to a segment `closestPoints()` picks the point nearest to the segment's start.

## Picking from the screen

`screenToRay()` turns a pixel into a world-space ray for a camera with a reversed-Z projection, perspective
or orthographic. Pixels have their origin at the top left with y pointing down:

```cpp
const illusion::Mat4 viewProjection = view.projection * view.view;
const illusion::Ray ray = illusion::screenToRay(mousePixel, viewportSize, viewProjection);
if (const std::optional<illusion::f32> t = illusion::intersect(ray, bounds))
{
    select(entity, ray.at(*t));
}
```

The ray starts on the near plane and has a unit direction.

## Euler angles

`quatFromEuler(Vec3{pitch, yaw, roll})` builds `yaw * pitch * roll`: roll about z first, then pitch about
x, then yaw about the world's up axis. `eulerFromQuat(rotation, hint)` converts back. A rotation has many
Euler forms; the result is the one closest to `hint`, with each angle shifted by whole turns, so an
inspector that converts every frame never jumps:

```cpp
m_angles = illusion::eulerFromQuat(transform.rotation, m_angles); // continuous while dragging
```

At pitch ±90° (gimbal lock) the roll keeps the hinted value and the yaw takes the rest of the rotation.

## Transforms

`Transform` is the placement of an Entity in world space (see [ECS](ecs.md#transform-hierarchy)).
`combine(parent, local)` places a local transform in its parent's space, and `relativeTo(world, parent)`
inverts it. Positions are exact; because a `Transform` cannot hold shear, a rotated child of a
non-uniformly scaled parent only approximates the product of the matrices.
