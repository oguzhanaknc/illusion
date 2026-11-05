# Prefab'lar

Prefab, `.prefab` Asset'i olarak kaydedilen ve sahnelere *instance* olarak yerleştirdiğiniz bir Entity
grubudur. Bir instance prefab'ına bağlı kalır: prefab'ı değiştirin, her instance onu izler; yalnızca sahnenin
bilerek değiştirdiği değerler, yani *override*'ları hariç. Dosya biçimi, iç içe yerleştirme ve eksik
prefab'ların nasıl tutulduğu [Sahneler](../scenes.md#prefab-instancelar) sayfasında anlatılır (ADR 0046).

Buradaki her şey bir [Editör komutu](../../reference/editor-commands.md)'dur; böylece insanlar ve yapay zekâ
ajanları prefab'larla aynı şekilde çalışır. Asset Browser prefab'ları `Prefab` türüyle listeler.

## Prefab oluşturma

`prefab.create` bir Entity'yi ve alt Entity'lerini bir prefab dosyası olarak kaydeder ve onları aynı yerde bir
instance ile değiştirir. Başka Entity'lerin değiştirilenlere başvuruları instance'ın Entity'lerine geçer;
böylece sahnede hiçbir şey bozulmaz. Yalnızca dosyayı yazmak için `"replace": false`, var olan bir dosyanın
yerine yazmak için `"overwrite": true` verin.

```json
{"entity": "/Crate", "path": "assets:/props/crate.prefab"}
```

Kök, dünyadaki yerini prefab'daki kendi yerleşimi olarak korur. Kaydedilen Entity'lerin içindeki instance'lar
instance olarak kalır: yeni prefab onları iç içe barındırır.

## Instance yerleştirme

`prefab.instantiate` bir prefab dosyasının instance'ını kök olarak ya da `parent` altında `index`
konumunda ekler. Her instance, Entity'lerinin id'lerinin türetildiği yeni bir id alır.

```json
{"path": "assets:/props/crate.prefab", "parent": "/Level", "index": 0}
```

## Override'lar

Bir instance'ı herhangi bir Entity gibi düzenleyin: yeniden adlandırın, Component değerlerini değiştirin,
Component ekleyin ya da kaldırın, Entity silin ya da çocuk ekleyin. Sahneyi kaydetmek yalnızca prefab'dan
farklı olanı kaydeder.

- `prefab.overrides` bunları bir instance'ın herhangi bir Entity'si için, sahnenin kaydettiği biçimde
  listeler: Entity'nin prefab dosyasındaki id'sine göre Component patch'leri (kaldırılmış bir Component için
  `null`) ya da kaldırılmış bir Entity için `null`.
- `prefab.revert` bir instance Entity'sine prefab'ının değerlerini geri verir: tek bir `component` ya da
  `illusion.Hierarchy` dışındaki her Component (prefab'da olmayan Component'ler kaldırılır, olanlar geri
  eklenir).

```json
{"entity": "/Level/Crate/Lid", "component": "illusion.Name"}
```

## Override'ları prefab'a uygulama

`prefab.apply` bir instance'ın değiştirdiklerini prefab dosyasına yazar: bir Entity'nin tek bir
`component`'ini, Entity'nin her Component'ini ya da `"all": true` ile instance'tan çıkarılan Entity'ler dahil
bütün instance'ı. Kökün yerleşimi ve hiyerarşi sahnede kalır. Açık sahnedeki diğer her instance ardından yeni
prefab'ı izler ve kendi override'larını korur; dosya yazımı ve bu düzenlemeler geri alınabilir tek bir
adımdır.

```json
{"entity": "/Level/Crate", "all": true}
```

## Kopyalama ve çoğaltma

Bütün bir instance'ı (kökünü) panoya kopyalayıp yapıştırmak, aynı override'larla yeni, bağlı bir instance
verir. Kökü olmadan kopyalanan instance Entity'leri ve `entity.duplicate` ile yapılan Entity'ler sıradan
kopyalardır.

## Paketi açma

`prefab.unpack` bir instance'ı, prefab'a artık bağlı olmayan, aynı id ve değerlere sahip sıradan Entity'lere
dönüştürür; içine yerleştirilmiş instance'ların da paketi açılır. Her düzenleme gibi oluşturma, yerleştirme,
geri alma (revert) ve paket açma da geri alınabilir (undo).

## Başvurular ve taşımalar

Bir sahne, her instance'ının prefab'ına ve onun üzerinden prefab'ın başvurduklarına başvurur:
`asset.references` ve `asset.move` ikisini de görür. Bir Asset'i taşımak, prefab dosyalarını ve onların
instance'larını içeren sahneleri tek adımda, taşımayı override'lara dönüştürmeden yeniden yazar.

## Şimdilik sınırlar

- Bir sahne ve instance'ları açıkken bir prefab dosyası diskte (`prefab.apply` dışında) değişirse, açık
  instance'lar sahne yeniden açıldığında güncellenir. Prefab'ın artık sahip olmadığı Entity'ler o zaman sıradan
  Entity'ler olarak kaydedilir.
