# Vulkan varsayılanı: compute SDF ve Slug

28 Ekim 2026. Başlangıç commit'i `1b0c6f25a6e8d51733db16177ecc37798de3dacb`.
Durum: tam Release/Debug kabul kapıları ve görüntü/ölçüm doğrulaması tamam.

## İstenen seçim ve düzeltilen hata

Önceki `INTEGRATED` varsayılanı native-first observation kullanıyordu. Slug
açıktı, fakat 5000-Spiral gibi değişen programlarda GPU tile painter genellikle
çalışmıyordu. Bu politika, kullanıcının istediği compute tabanlı varsayılanı
karşılamıyordu. Slug'ın kapalı olduğu varsayımıyla bir görüntü değişikliği
yapılmadı; sorun varsayılan yürütme seçimindeydi.

Yeni `vulkan_device_opts{}`:

- `painter=INTEGRATED`, `native_observation=false`, `adaptive_compute=false`.
- GPU komutları, coarse/fine binning, dirty tile seçimi ve compute SDF/Slug
  ilk değişen kareden çalışır; başarısız LOAD sertifikasını beklemez.
- `tile_cache=true` ve `whole_program_cache=true` birlikte korunur. Değişmeyen,
  sertifikalı program painter'ı tamamen atlayabilir; değişiklik tekrar compute
  üretir. Native-first equal-prefix replay normal compute politikasına girmez.
- Kerneller, native blend-function atlası, job/descriptor/arena/history kaynakları
  UI çiziminden önce hazırlanır. Native observation'ın kullanılmayan 12 MiB tape
  rezervasyonunu kaldıran önceki düzeltme, açıkça seçilen o politikada geçerlidir.
- `native_observation=true` eski native-first politikayı açıkça seçer.
- Yeni Vulkan CMake yapılandırması HarfBuzz/Slug'ı açar. Core/SDL-only varsayılanı
  bağımsız kalır; açıkça verilen `PUFFERUI_WITH_HARFBUZZ=OFF` korunur. Önceden
  OFF olan bir CMake cache'ini açmak için seçenek açıkça ON yapılmalıdır.

## Görsel sözleşme ve makaleyle sınır

[Blick makalesindeki](https://blickeditor.com/blog/caching-an-immediate-mode-ui)
GPU komut/binning/dirty/painter/cache akışı artık normal yürütmedir.
Mevcut sayısal sözleşme de korunur: her işlemin native RGBA8 kuantizasyonu,
glyph/source generation'ları, producer sırası ve LOAD bağımlılıkları kesin
doğrulanır. Kapsam dışındaki pikselde native replay kullanılır. Bu destek yolu,
değişen bütün sahneyi önce native gözlemle yürütmekten ayrıdır.

Ortak opacity hâlâ prepared RGBA8 scratch kullanır; article-style piksel içi alpha
accumulator bu değişiklikte eklenmedi. Genel front-to-back float accumulation da
ara kuantizasyonu değiştireceğinden kullanılmaz. Dolayısıyla bu teslim makaledeki
bütün ayrıntıların birebir uygulandığı iddiası taşımaz; compute tabanlı varsayılan
seçimini düzeltir. Blur modu ve mevcut SDF/Slug görüntü alanı korunur.

## Doğrudan standart executable kanıtı

Python veya painter/vector seçeneği olmadan normal executable kullanılabilir:

```powershell
& "D:\myimgui\out\build\x64-release\Release\pui_showcase_vulkan.exe"
```

`--vulkan-device-info` çıktısı `default_execution=compute` ve
`default_vector_text=1` olarak doğrulandı. 16 karelik bounded selftest'te,
ek painter/vector bayrağı verilmeden standart executable'dan alınan 15
tamamlanmış trace satırı: `glyph_upload_bytes=272008`,
`glyph_resident_batches=472`, `gpu_sdf_batches=90`, `gpu_sdf_pixels=68241232`.
Trace son kareyi bir sonraki frame başında gözlediği için son kareyi eksik
diye işaretler; eksik satır sıfır işlem gibi sunulmaz. Bu kayıt zamanlama
benchmark'i değildir. Yerel kanıt: `build-vulkan-compute-default/runtime-proof/`.

## Regresyonlar

`pui_vulkan_compute_default_tests`, public varsayılanla gerçek showcase'in bütün
180 karesini bağımsız SDF raster'a karşı karşılaştırır. Moving direct/staged'da
180/180 kare compute üretir. Sabit/yerel-değişim direct/staged'da 14 aktif kare,
166 whole-program hit vardır; 90/120/150'deki gerçek değişiklikler aynı karede
compute'a döner. Slug upload/resident ve GPU paint sayaçları pozitiftir. Draw
tahsis/kaynak oluşturma ve validation hata sayaçları sıfırdır. Release ve Debug
odaklı kapılar geçti.

Önceki observation tanılama fixture'ları politikalarını test helper'ında açıkça
sabitler. Eski assertion ve golden byte'ları değiştirilmez. Yeni default test
bağımsızdır; native-first başarısını compute varsayılanının kanıtı diye saymaz.
`test_vulkan_default_tools.py` dört gerçek CMake option/FetchContent yolu için
fresh Vulkan ON, core OFF ve iki explicit opt-in/out davranışını kontrol eder;
yerel dependency stub'ı nedeniyle test ağdan indirme yapmaz.

## Önce/sonra ölçümü

AMD Radeon RX 9070 XT / driver 8389024; Release MSVC/LTCG, 1360×860,
5000 başlangıç öğesi/64 boyut Spiral; HUD kapalıdır. Her kohortta dört dönüşümlü çift ve
180 kare; moving'de 90/120/150'de gerçek Add, idle/local'de sayı 5000.
Toplam 17.280 zamanlanmış kare saklandı. Readback, profiler ve eşzamanlı
build/test yok; VSync kapalı ve WSI pencereleri gizlidir. GPU sütunu, aralıkta
tamamlanan submission sürelerinin kare sayısına bölümüdür.
Cihaz/surface oluşturma, kernel hazırlama ve başlangıç font yüklemeleri bu
kare zamanlarının dışındadır; tablo soğuk executable açılış süresi değildir.
Normal showcase'in değişen FPS HUD'ı whole-program eşleşmesini etkileyebilir;
bu sabit saat/HUD'sız kohort, normal ön plan FPS sertifikası olarak sunulmaz.

`production` iki revision'ın gerçek farklı varsayılanlarını karşılaştırır:
önce native-first, sonra compute-first. CLI bayrakları aynıdır; politika farkı
bilinçlidir. `fine` kontrolünde her iki tarafta da whole-program cache kapalıdır.
Bu tablo hızlanma iddiası değildir; kullanıcı tarafından istenen seçimin maliyeti
de korunur. Süreler ms; bütün başlangıç ve aykırı kareler hesaplara dahildir.

| Yol/upload/sahne | Ortalama önce → sonra | GPU toplam/kare | p99 önce → sonra | Maks. önce → sonra |
| --- | ---: | ---: | ---: | ---: |
| production/direct/moving | 2.0921 → 3.7311 | 0.4516 → 2.8441 | 3.65 → 6.86 | 32.20 → 32.95 |
| production/direct/idle | 1.7608 → 2.8104 | 0.0212 → 0.1270 | 2.81 → 5.50 | 33.04 → 32.95 |
| production/direct/local | 1.7452 → 2.9987 | 0.0433 → 0.3848 | 2.95 → 6.52 | 32.65 → 34.25 |
| production/staged/moving | 2.0935 → 3.6694 | 0.5330 → 2.7846 | 3.61 → 6.65 | 30.27 → 31.14 |
| production/staged/idle | 1.7712 → 2.8109 | 0.0229 → 0.1230 | 3.05 → 4.13 | 30.91 → 33.27 |
| production/staged/local | 1.7738 → 2.9637 | 0.0519 → 0.3816 | 2.96 → 6.25 | 31.06 → 35.26 |
| fine/direct/moving | 3.7303 → 3.7210 | 2.8348 → 2.8120 | 6.64 → 6.62 | 33.02 → 32.92 |
| fine/direct/idle | 3.5210 → 3.5001 | 2.7877 → 2.7591 | 6.63 → 6.57 | 33.35 → 35.10 |
| fine/direct/local | 3.5212 → 3.5268 | 2.7714 → 2.7804 | 6.65 → 6.66 | 33.15 → 34.40 |
| fine/staged/moving | 3.6624 → 3.6730 | 2.7607 → 2.7620 | 6.58 → 6.65 | 30.88 → 32.45 |
| fine/staged/idle | 3.3673 → 3.3557 | 2.5968 → 2.6044 | 6.36 → 6.34 | 33.86 → 34.41 |
| fine/staged/local | 3.3595 → 3.3589 | 2.6045 → 2.5998 | 6.21 → 6.22 | 32.80 → 34.51 |

Native-first bu sahnede daha hızlıdır. Compute-first'in maliyetini azaltmak için
varsayılan sessizce native'e geri çevrilmedi. Whole-program cache açık yeni
varsayılanın idle GPU işi, sürekli fine boyamadan belirgin düşüktür.
Source/binary kimlikleri, process/thread CPU/cycle ve bütün ham kareler JSON'dadır.

## Frozen görüntüler ve tepe kareler

Önceki doğrulanmış 1080 frozen RGBA dosyası tekrar kullanılmadan önce ve sonra
SHA-256 ile denetlendi; üzerlerine yazılmadı. Baseline native-first, yeni default
ve yeni fine kontrolü direct/staged × moving/idle/local için bu dosyalara karşı
3×1080 = **3240 tam-byte karşılaştırma** yaptı: bütün kareler eşit. Zamanlama
serilerindeki bütün top-10 tepe ve ±1 komşusu bu kapsamda; indeksleri JSON'da
saklıdır. Native SDF bağımsız oraklı ayrıca yeni 180-kare default testinde vardır.
Ortak opacity/Slug/blur ve opacity değişiklikleri de cache açık/kapalı 32 tam
RGBA kareyle karşılaştırılır; draw tahsis ve validation sayaçları sıfırdır.

## Son kabul

Tam Release ve Debug `--vulkan --format` kapıları geçti: her yapılandırmada
100 native Vulkan vakası, dört yeni compute-default vakası, mevcut core/pump/
performans testleri, 22 SDL golden sahnesi, 1150/5000 Spiral ve tepe kareleri,
iki 3600-kare/60-Add çifti, bütün örnekler, installed core/SDL/Vulkan tüketicileri,
format/API/tutorial kontrolleri. Atlanan adım yoktur. Yeni varsayılanın test
çıktıları moving direct/staged için active=180, idle için active=14 ve
cache_hits=166 değerlerini tekrar doğrular. Her iki kapı boyunca 336 kaynak
dosyasının hash'leri, ölçümdeki kaynaklarla aynı ve değişmez kaldı.
Dokuz ilgisiz taslağın hash'leri de başlangıçla aynıdır; SDL submodule temizdir.
Kapı süreleri: Release 1451.6 s, Debug 1386.7 s.
[Makine kaydı](perf/vulkan_compute_default_final.json),
[bütün zamanlanmış kareler](perf/vulkan_compute_default_frames.csv.gz),
[ham CSV/log ve harness'ler](perf/vulkan_compute_default_raw.zip).
Yerel immutable kaynak/binary arşivi `build-vulkan-compute-default/` altındadır.
İlgisiz dokuz rapor taslağı bu değişikliğe dahil değildir.
