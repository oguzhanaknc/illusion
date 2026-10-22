# Matematik

illusion'ın matematik türleri [glm](https://github.com/g-truc/glm) tabanlıdır. Her dosyanın aynı
yapılandırmayı kullanması için glm yerine `<illusion/core/Math.hpp>`'yi dahil edin.

Header: `#include <illusion/core/Math.hpp>`

## Kurallar

- Sağ elli dünya koordinatları, +y yukarı; kameralar -z yönüne bakar.
- Açılar radyandır.
- Clip uzayında y yukarı bakar ve derinlik [0, 1] aralığındadır. Renderer **reversed-Z** kullanır: derinlik
  yakın düzlemde 1, uzak düzlemde (veya sonsuzda) 0'dır; bu çok daha iyi derinlik hassasiyeti verir.
- Matrisler glm'deki gibi sütun önceliklidir (`m[column][row]`).

| Tür | Anlamı |
| --- | --- |
| `Vec2`, `Vec3`, `Vec4`, `IVec2`, `UVec2` | Vektörler |
| `Mat3`, `Mat4` | Matrisler |
| `Quat` | Dönüş quaternion'u |
| `Aabb`, `Sphere`, `Obb` | Eksen hizalı kutu, küre, yönlendirilmiş kutu |
| `Plane`, `Frustum` | Düzlem, bir görüş hacminin altı düzlemi |
| `Ray` | Seçim (picking) için yarı doğru |

## Projeksiyonlar

```cpp
const illusion::Mat4 perspective = illusion::perspectiveReversedZ(fovY, aspect, 0.1f); // infinite far plane
const illusion::Mat4 orthographic =
    illusion::orthographicReversedZ(-width / 2, width / 2, -height / 2, height / 2, 0.1f, 500.0f);
```

`perspectiveReversedZ()`'nin uzak düzlemi sonsuzdadır. `orthographicReversedZ()` yakın düzlemi 1, uzak
düzlemi 0 derinliğine eşler.

## Işınlar ve kesişimler

`Ray{origin, direction}` bir yarı doğrudur. Kesişim fonksiyonları sıfır olmayan her yönü kabul eder ve
isabetin ışın parametresi `t`'yi döndürür (nokta `ray.at(t)`'dir); birim yönle `t` mesafedir. Iskalama veya
başlangıcın arkasındaki bir isabet `std::nullopt` döndürür.

| Fonksiyon | Sonuç |
| --- | --- |
| `intersect(ray, aabb)`, `intersect(ray, obb)` | Giriş parametresi; başlangıç içerideyse 0 |
| `intersect(ray, sphere)` | Giriş parametresi; başlangıç içerideyse 0 |
| `intersect(ray, plane)` | İki yönden geçiş; paralel ışınlar ıskalar |
| `intersect(ray, a, b, c)` | Barisentrik ağırlıklarla `RayTriangleHit{t, u, v}`; iki yüz, kenarlar dahil |
| `closestPoints(ray, start, end)` | Işın ile bir doğru parçası arasındaki en yakın noktalar (`rayT`, `segmentT`, `distance`) |

Uç durumlar tanımlıdır: bir kutunun yüzeyindeki noktalar içeride sayılır, bir kutu yüzüne paralel bir ışın
yalnızca başlangıcı o yüzün düzlemleri arasındaysa isabet eder, bir üçgenin düzlemindeki ışın onu ıskalar ve
bir doğru parçasına paralel bir ışın için `closestPoints()` parçanın başlangıcına en yakın noktayı seçer.

## Ekrandan seçim

`screenToRay()`, reversed-Z projeksiyonlu (perspektif veya ortografik) bir kamera için bir pikseli dünya
uzayında bir ışına çevirir. Piksellerin başlangıcı sol üsttedir ve y aşağı bakar:

```cpp
const illusion::Mat4 viewProjection = view.projection * view.view;
const illusion::Ray ray = illusion::screenToRay(mousePixel, viewportSize, viewProjection);
if (const std::optional<illusion::f32> t = illusion::intersect(ray, bounds))
{
    select(entity, ray.at(*t));
}
```

Işın yakın düzlemde başlar ve yönü birim uzunluktadır.

## Euler açıları

`quatFromEuler(Vec3{pitch, yaw, roll})`, `yaw * pitch * roll` oluşturur: önce z etrafında roll, sonra x
etrafında pitch, sonra dünyanın yukarı ekseni etrafında yaw. `eulerFromQuat(rotation, hint)` geri çevirir.
Bir dönüşün birçok Euler biçimi vardır; sonuç, her açı tam turlarla kaydırılarak `hint`'e en yakın olandır.
Böylece her karede dönüştürme yapan bir inspector hiç sıçramaz:

```cpp
m_angles = illusion::eulerFromQuat(transform.rotation, m_angles); // continuous while dragging
```

Pitch ±90°'de (gimbal lock) roll ipucundaki değeri korur ve yaw dönüşün geri kalanını alır.

## Transform'lar

`Transform`, bir Entity'nin dünya uzayındaki yerleşimidir (bkz. [ECS](ecs.md#transform-hiyerarsisi)).
`combine(parent, local)` yerel bir dönüşümü ebeveyninin uzayına yerleştirir ve `relativeTo(world, parent)`
bunu tersine çevirir. Konumlar tamdır; bir `Transform` kayma (shear) tutamadığı için düzgün olmayan ölçekli
bir ebeveynin döndürülmüş çocuğu matrislerin çarpımına yalnızca yaklaşır.
