# Çözünen bir yüzey

Bu eğitim çözünen bir material shader'ı yazar: bir sayı büyüdükçe gürültü, bir sandığın nerede kesileceğine karar
verir ve kesiğin kenarı parlar. Shader'ı yazar, onu kullanan bir Material yapar, Scene view'da görür, editör
çalışırken değiştirir ve bir ajanın denetlemesine izin verirsiniz. İçinde C++ yoktur. Burada kullanılan her
parçayı [Material shader'ları kılavuzu](../manual/material-shaders.md) açıklar.

Editöre ve bir projeye ihtiyacınız var (**3D Starter** şablonu yeter).

## 1. Ne yapacaksınız

- 0 (bütün) ile 1 (yok) arasında bir `progress` parametresi, parlayan bir kenar rengi ve bir gürültü ölçeği olan
  `assets/shaders/dissolve.shader` shader'ı.
- Onu kullanan, `alphaMode` değeri `Mask` olan bir `assets/materials/dissolve.material` Material'ı; çünkü shader
  piksel keser.
- Material'ı giyen bir küp.

## 2. Shader'ı yazın

`assets/shaders/dissolve.shader` dosyasını aşağıdaki metinle oluşturun. Asset Browser onu bir **Shader** olarak
listeler ve editör dosya kaydedilir kaydedilmez onu derler.

```slang
// A surface that dissolves: noise decides where the material is cut away as progress grows, and the cut
// edge glows. Use it with a material whose alphaMode is "Mask".
struct Parameters
{
    [Default(0.4)] [Range(0, 1)] float progress;
    [Color(1, 0.5, 0.1, 1)] float3 edgeColor;
    [Default(0.06)] float edgeWidth;
    [Default(6)] float scale;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float n = fractalNoise(input.worldPosition * p.scale, 3);
    const float edge = 1.0 - saturate((n - p.progress) / max(p.edgeWidth, 0.001));
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.6, 0.55, 0.5);
    output.roughness = 0.8;
    output.emission = p.edgeColor * edge * 4.0;
    output.opacity = n;
    output.opacityMask = p.progress;
    return output;
}
```

Nasıl çalışır:

- `fractalNoise()` dünyanın her noktası için [0, 1] aralığında bir değer verir; bu yüzden kesik UV'lere bağlı
  değildir.
- `opacity`'nin `opacityMask`'in altında olduğu yerde piksel kesilir: shader `opacityMask`'i `progress` yapar;
  `progress` büyüdükçe gürültünün daha fazlası onun altına düşer.
- `edge` kesiğin hemen üstünde 1'dir ve `edgeWidth` boyunca 0'a söner; emission'ı o sürer, bu yüzden başka hiçbir şey
  farklı aydınlatılmadığı halde kenar parlar.

## 3. Material'ı yapın

Material'ı komutla, palette'ten ya da bir ajanla oluşturun:

```json
{"call": "material.create", "params": {"path": "assets:/materials/dissolve.material", "value": {"shader": "assets:/shaders/dissolve.shader", "material": {"name": "Dissolve", "alphaMode": "Mask"}, "parameters": {"progress": 0.45}}}}
```

Material'ı Asset Browser'da seçin. Sağ panel shader'ı ve her parametre için bir widget gösterir: `progress` için bir
kaydırıcı (shader'ın `[Range(0, 1)]`'i), `edgeColor` için bir renk seçici ve `edgeWidth` ile `scale` için sürükleme
alanları. Kaydırıcıyı sürükleyin: her adım bir `material.set`'tir; bu yüzden Undo onu geri alır.

## 4. Bir küpe koyun

Mesh'i `builtin:/mesh/cube`, Material'ı `assets:/materials/dissolve.material` olan bir `MeshRenderer`'lı bir Entity
oluşturun. Küp turuncu bir kenarlı deliklerle dolu görünür. `progress`'i 0'a getirin, yine bütündür; 1'e getirin,
yok olur.

## 5. Editör çalışırken shader'ı değiştirin

Dosyada `output.baseColor`'ın rengini değiştirin ve kaydedin. Editör onu bir arka plan thread'inde derler ve hazır
olunca Scene view'da yerine koyar; hiçbir kare derleyiciyi beklemez ve Material `progress`'ini korur. Şimdi onu
bozun; örneğin `output.opacity`'de `n` yerine `nothing` yazın ve yine kaydedin: Console dosya, satır ve sütunla
bir hata gösterir, küp son çalışan görünümünü korur ve Material'ın paneli hatayı gösterir. Dosyayı düzeltin; bir
sonraki derleme onun yerini alır.

## 6. Dışarıdan denetleyin

Bir ajan, ya da `illusion_ctl` ile siz, editöre her şeyin uyup uymadığını sorabilir:

```json
{"call": "material.compile", "params": {"path": "assets:/shaders/dissolve.shader"}}
{"call": "material.validate", "params": {"path": "assets:/materials/dissolve.material"}}
```

`material.compile` arka plan derlemesi bitince `status`, diagnostics ve shader'ın bildirdiği parametrelerle
yanıtlar. `material.validate` Material'ın `parameters`'ının bunlara uyup uymadığını söyler: `progres` gibi yanlış
yazılmış bir ad, `/parameters/progres` konumunda, shader'ın bildirdiği adları sayan bir hatadır.

## 7. Canlandırın

Shader karenin saniyesi olan `input.time`'ı okur; bu yüzden hiçbir oyun kodu olmadan hareket edebilir.
Kullanıldığı iki yerde `p.progress`'i `p.progress + 0.2 * sin(input.time)` ile değiştirin; yüzey kendi kendine
nefes alır, hem editörde hem de export edilmiş oyunda.

## Sırada ne var

- [Material shader'ları](../manual/material-shaders.md): surface API'sinin her alanı, shading modelleri ve export.
- Showcase projesinin `assets/shaders/pulse.shader` dosyası her türden bir parametre bildirir.
