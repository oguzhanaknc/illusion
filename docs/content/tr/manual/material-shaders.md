# Material shader'ları

Material shader'ı, bir yüzeyin nasıl göründüğüne karar veren küçük bir Slang dosyasıdır: rengine, roughness'ına,
emission'ına, opacity'sine ve isterseniz vertex'lerinin nasıl hareket ettiğine. Motorun kendi PBR Material'ı da
aynı şekilde yazılmıştır (`builtin:/shader/standard`); bu yüzden özel bir shader, diğer her Material ile aynı
araçlara, Render Pass'lere ve aydınlatmaya sahiptir ([ADR 0063](../architecture/decisions.md)). Yüzeyi siz
yazarsınız; illusion onu depth prepass'te, shadow cascade'lerinde, aydınlatılmış pass'te (bound ve GPU-driven),
motion vector'lerde ve Entity id pass'inde, her alpha modunda çizer.

Shader bir metin dosyasıdır; bu yüzden bir ajan onu yazabilir, derleyebilir ve hataları okuyabilir:
[Editörden ve ajanlardan](#editorden-ve-ajanlardan) bölümüne bakın.

## İlk shader

`assets/shaders/pulse.shader` dosyasını oluşturun:

```slang
// A pulsing emissive surface: stripes of light scroll over a dark body and a hanging cloth sways in the wind.
[ShadingModel("Default")]
struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float3 glow;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(3)] int bands;
    [Default(1)] bool pulse;
    [Default2(0.5, 0.25)] float2 scroll;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float2 uv = input.uv + p.scroll * input.time;
    const float band = step(0.5, frac(uv.y * float(p.bands)));
    const float beat = p.pulse ? 0.5 + 0.5 * sin(input.time * 3.0) : 1.0;
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.08, 0.08, 0.1);
    output.emission = p.glow * p.intensity * band * beat * sampleTexture(input, p.mask, uv).r;
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    return windOffset(input, 0.05, 0.002);
}
```

Bir `.shader` dosyasının üç parçası vardır:

- `struct Parameters`: bir Material'ın ayarlayabileceği değerler ve varsayılanları. Zorunludur ve boş olabilir.
- `SurfaceOutput surface(SurfaceInput input)`: zorunludur. `parameters(input)`'u, UV'leri, dünya konumunu ve zamanı
  okuyun ve `defaultSurface()` ile başlayan bir `SurfaceOutput` doldurun.
- `float3 worldOffset(WorldOffsetInput input)`: isteğe bağlı. Vertex'i dünya uzayında kaydırır (rüzgar, dalga,
  nabız). Her pass'te, o karenin zamanıyla çalışır; böylece gölgeler ve motion vector'ler görüntüyle uyumlu olur.

Dosya hiçbir şey import etmez: derleyici onu motorun surface API'si (`illusion/Surface.slang`) ile pass'leri
arasına yerleştirir.

Shader'ı bir Material dosyasından kullanın ([Shader belirten Material'lar](#shader-belirten-materiallar)) ve
Material'ı bir `MeshRenderer`'a verin.

## Surface API'si

### SurfaceInput

| Alan | Anlamı |
| --- | --- |
| `worldPosition` | Piksel'in dünya uzayındaki konumu. |
| `normal`, `tangent`, `bitangent` | Normalize edilmiş dünya uzayı çerçevesi; `normal` bakana döner. |
| `uv` | Mesh'in texture koordinatları. |
| `color` | Vertex rengi (renksiz Mesh'ler için beyaz). |
| `viewDirection` | Yüzeyden kameraya, normalize. |
| `time` | Saniye; bir karenin her pass'inde aynıdır. |
| `customData` | Entity'nin özel verisi (`MeshRenderer` taşıyana kadar sıfır). |
| `frontFace` | Üçgenin ön yüzü kameraya bakıyorsa true. |

### SurfaceOutput

`defaultSurface()` ile başlayın (beyaz, dielektrik, roughness 0,5, opak) ve ihtiyacınız olanı değiştirin.

| Alan | Anlamı |
| --- | --- |
| `baseColor` | Lineer albedo (metaller için metal rengi). |
| `normal` | Tangent uzayında normal; geometrik olan için `(0, 0, 1)`. |
| `metallic`, `roughness`, `occlusion` | glTF'deki gibi. |
| `emission` | Aydınlatılmış renge eklenen lineer radiance. |
| `opacity`, `opacityMask` | Blended Material'lar: kapsama. Masked Material'lar: `opacity`'nin `opacityMask`'in altında olduğu yerde piksel kesilir. |
| `subsurfaceColor`, `thickness` | Subsurface ve TwoSidedFoliage modelleri. |
| `clearCoat`, `clearCoatRoughness` | ClearCoat modeli. |

### WorldOffsetInput

`worldPosition`, `objectOrigin` (nesnenin başlangıç noktasının dünya konumu), `normal`, `uv`, `time`,
`customData`. `worldOffset()` zaman dışında zamanla değişen hiçbir şeye bağlı olmamalıdır; böylece önceki karenin
offset'i (motion vector'ler için) önceki karenin zamanıyla aynı fonksiyondur.

### Yardımcılar

| Fonksiyon | Ne yapar |
| --- | --- |
| `parameters(input)` | Çizilen Material'ın `Parameters`'ı. |
| `sampleTexture(input, slot, uv)` | Bir texture parametresini örnekler (yalnız `surface()` içinde). |
| `sampleTextureLevel(material, slot, uv, level)` | Verilen mip seviyesini örnekler (`worldOffset()` içinde de). |
| `unpackNormal(texel, scale)` | Bir normal map texel'inden tangent uzayı normali. |
| `triplanar(input, slot, scale, sharpness)` | Üç dünya ekseni boyunca yansıtılmış bir texture. |
| `valueNoise(p)`, `fractalNoise(p, octaves)` | `float2` veya `float3` noktalar için [0, 1] aralığında yumuşak gürültü. |
| `hash12(p)`, `hash13(p)` | Bir hücrenin [0, 1) aralığına hash'i. |
| `windOffset(input, sway, flutter)` | Sahnenin Wind'i vertex'te: yüksekliğe göre eğilme ve çırpınma, m/s başına metre. |

## Parametreler

`Parameters`'ın her alanı Material'ın bir parametresidir. Attribute'lar editörün ve dosyaların onu nasıl
bildiğini belirler:

| Bildirim | `.material` dosyasındaki tür |
| --- | --- |
| `[Default(2)]` ve `[Range(0, 10)]` ile `float x;` | bir sayı; Inspector bir kaydırıcı gösterir |
| `[Default2(..)]`, `[Default3(..)]`, `[Default4(..)]` ile `float2`, `float3`, `float4` | o kadar sayıdan oluşan bir dizi |
| `[Color(r, g, b, a)]` ile `float3` veya `float4` | bir renk: bir dizi, ya da `"#rrggbb"` / `"#rrggbbaa"` (sRGB) |
| `[Default(3)]` ile `int` | bir tam sayı |
| `[Default(1)]` ile `bool` | `true` veya `false` |
| `[Texture("white", "mask")]` ile `Texture2DSlot` | bir asset yolu (`""`: görüntü yok) |

Attribute'suz bir alanın varsayılanı sıfırdır. Bir texture'ın ilk argümanı varsayılanıdır: `"white"`, `"black"`,
`"normal"` (düz bir normal map) ya da bir görüntünün yolu; ikincisi kullanımıdır ve görüntünün nasıl import
edileceğini belirler: `"color"` (sRGB, BC7), `"linear"` (BC7), `"normal"` (BC5) veya `"mask"` (tek kanal, BC4).
`Parameters` en çok 256 bayttır ve bir shader'ın en çok sekiz texture'ı olur.

## Shading modelleri

`Parameters` üzerindeki `[ShadingModel("...")]`, aydınlatma kodunun çıktıyı nasıl kullanacağını seçer. Modeller
pass'lerde değil aydınlatmanın yaptığında ayrışır; bu yüzden her model her alpha moduyla çalışır.

| Model | Aydınlatma |
| --- | --- |
| `Default` | Metallic-roughness PBR. |
| `Unlit` | Base color artı emission, aydınlatma yok. |
| `Subsurface` | Diffuse ışık terminatörün ötesine `1 - thickness` kadar sarar, `subsurfaceColor` ile boyanır (deri, mum). |
| `TwoSidedFoliage` | Yaprak yarı saydamlığı: ışık ince yüzeylerden geçer, `subsurfaceColor` ile boyanır. |
| `ClearCoat` | `clearCoat` ve `clearCoatRoughness` ile geometrik normal boyunca ikinci bir specular lob (araba boyası). |

## Shader belirten Material'lar

2. sürüm bir `.material` dosyasında `shader` (bir asset yolu) ve `parameters` (değerler, ada göre) bulunur:

```json
{
  "format": "illusion.material",
  "version": 2,
  "shader": "assets:/shaders/pulse.shader",
  "material": {"name": "Pulse", "alphaMode": "Opaque"},
  "parameters": {
    "glow": [0.2, 0.8, 1],
    "intensity": 4,
    "bands": 5,
    "pulse": false,
    "scroll": [0.1, 0.4],
    "mask": "assets:/textures/smoke.png"
  }
}
```

- Dosyanın bırakmadığı parametreler shader'ın varsayılanlarını alır; bir Material değişiklikleri kadar kısadır.
- Shader'ın bildirmediği bir ad ya da yanlış türde veya boyutta bir değer, değerin JSON Pointer'ıyla bir hatadır;
  örneğin `/parameters/glow: the shader 'assets:/shaders/pulse.shader' declares no parameter 'glow' (it
  declares: ...)`. Yüklenemeyen bir Material varsayılan Material ile çizilir.
- `material` her shader için alpha modunu, çift yüzlülüğü ve wrap modlarını yine ayarlar.
- İkisi de olmayan 1. sürüm dosyalar olduğu gibi okunur: standart shader'ın Material'ları. Dosya yazmak her
  zaman 2. sürümü yazar.
- `builtin:/shader/standard` ve `builtin:/shader/foliage` motorun kendi shader'larını adlandırır. Shader'sız ve
  `diffuseTransmission` değeri sıfırın üstünde olan bir Material yine foliage shader'ıyla çizilir.

## Editörde

- **Asset Browser.** `.shader` dosyaları **Shader** asset türüdür. Bir `.material` dosyası seçin: sağ panel
  shader'ını (üzerine bir `.shader` dosyası bırakın ya da Clear'a basın) ve bildirilen her parametre için türüne
  uygun bir widget gösterir: float'lar için kaydırıcı ya da sürükleme alanı, renkler için renk seçici, vektörler
  için sürükleme alanları, bool'lar için onay kutusu ve texture'lar için bırakma hedefi. Her değişiklik bir
  `material.set`'tir; bu yüzden geri alınabilir ve Scene view onu hemen gösterir.
- **Hot reload.** Bir `.shader` dosyası değiştiğinde editör onu bir arka plan thread'inde, hiçbir karenin içinde
  değil, derler ve bitince `RenderResources::updateShader()` ile yerine koyar. Material'lar değerlerini ada göre
  korur. Derlenmeyen bir shader son çalışan sürümüyle çizmeye devam eder ve hatalar Console'a
  `file:line:column: error: message [code]` biçiminde gider. Shader'ı hiç derlenmemiş bir Material macenta bir
  dama deseniyle, hata shader'ıyla çizilir.
- **Derleme hataları.** Hatalar `.shader` dosyasını satır ve sütunla adlandırır. Shader'ınızın dahil ettiği motorun
  kendi shader kütüphanesindeki hatalar o dosyayı adlandırır; genellikle shader'da `Parameters` ya da `surface()`
  eksik olduğu ya da derleyicinin yerleştiremediği bir alan bildirdiği anlamına gelir.

## Editörden ve ajanlardan

| Komut | Ne yapar |
| --- | --- |
| `material.compile` | Bir `.shader` dosyasını arka plan thread'inde derler ve bitince `status` (`ok` ya da `failed`), diagnostics (dosya, satır, sütun, önem, kod, mesaj) ve bildirilen parametrelerle yanıtlar. |
| `material.validate` | Bir `.material` ya da `.shader` dosyasını denetler: bir shader'ın güncel kaynağı derlendiyse, bir Material'ın `parameters`'ı adını verdiği shader'a uyuyorsa geçerlidir. Hataların `errorPath`'inde bir JSON Pointer bulunur. |
| `material.get` | Bir Material'ı ve özel shader için durumunu ve bildirilen parametrelerini (ad, tür, varsayılan, aralık, kullanım) döndürür. |
| `material.set` | Bir Material'a alanları birleştirir; `parameters` ada göre birleşir ve `null` birini kaldırır. |

Bir ajan shader'ı ve Material'ı yazar, `material.compile` çağırır, `status` `ok` olana kadar diagnostics'i okur,
sonra Material üzerinde `material.validate` çağırır:

```json
{"call": "material.compile", "params": {"path": "assets:/shaders/pulse.shader"}}
{"call": "material.validate", "params": {"path": "assets:/materials/pulse.material"}}
```

## Export ve player

Export, her `.shader` dosyasını kurulu motorun `slangc`'siyle her pass ve alpha modu için SPIR-V'ye cook eder;
cook edilmiş shader'lar oyunun verisindedir. Player'ın derleyicisi yoktur. Derlenmeyen bir shader export'u
derleyicinin mesajı ve dosya, satır, sütun bilgisiyle başarısız kılar.

## C++ tarafından

`ShaderCompiler` (`illusion/assets/ShaderCompiler.hpp`) bir `.shader` kaynağını `ShaderAsset`'e ya da
diagnostics'e derler. `ShaderLoader` (`illusion/assets/ShaderLoader.hpp`) shader'ları kendi thread'inde derler ve
`RenderAssetCacheDesc::shaders` içinde birini alan bir `RenderAssetCache`, Material dosyalarının adını verdiği
shader'ları yükler:

```cpp
illusion::ShaderLoader shaders(files, illusion::ShaderLoaderDesc{.cacheMount = "cache", .compiler = {},
                                                                 .verifySources = false});
illusion::RenderAssetCacheDesc desc;
desc.shaders = &shaders;
illusion::RenderAssetCache cache(resources, streamer, desc);
```

Derleyicisiz loader, player'ın yaptığı gibi import cache'indeki cook edilmiş shader'ları okur. Editörün loader'ı
module SDK'sının derleyicisine sahiptir.
