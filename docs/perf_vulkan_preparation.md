# Vulkan hazırlığı, GPU yükü ve bellek kullanımı

24 Ekim 2026. Başlangıç kaynağı: `29d7a0602b2f2a0a707a6a691a68904adbabf130`.
Önceki [incelemenin](perf_vulkan_compute_cache_review.md) beş bulgusu bu değişiklikte
ele alındı. Eski kaynak arşivi ve executable'lar değişiklikten önce
`build-vulkan-resources/baseline/` altında SHA256 kayıtlarıyla donduruldu.

## Değişiklikler

1. Bin/painter/cache pipeline'ları ve layout'ları cihazda paylaşılır. Normal
   compute cihazı yedi compute pipeline oluşturur; cache gereken cihazda toplam
   on bir olur. Her epoch için yeniden derleme yapılmaz. Kullanılmayan cache
   kernel'ları oluşturulmaz; çalışma sırasında cache açılması da desteklenir.
2. Yüzey hazırlığı, frame slot başına upload alanını, 64 grafik descriptor setini,
   32 compute işinin descriptor'larını ve hizalı metadata arenalarını hazırlar.
   Çizim hazır aralıkları kullanır. VSync/present-mode yenilemesi aynı boyuttaki
   kaynakları yeniden oluşturmaz. Metadata veya job kapasitesi yetmezse eksiksiz
   raster programı kullanılır; descriptor sınırında tamamlanmış setler beklenerek
   tekrar kullanılır. Çok küçük upload bütçelerinin eski streaming yolu korunur.
3. Cache görüntüsü için bütçe reddi hazırlıkta bir kez değerlendirilir. Reddedilen
   epoch, her çizimde allocation denemez ve kullanılamayacak history bankaları
   ayırmaz. Tam boyutlu görüntü çalışma kümesi slot başına dört epoch ile sınırlıdır.
4. Önceki komut/list/shape/ownership verileri diğer arena bankasında sabit kalır;
   beş ayrı history buffer kopyası kaldırıldı. Painter yalnız sahip olduğu
   pikselleri doğrudan cache görüntüsüne de yazar. Her epoch'taki tam ekran
   `vkCmdCopyImage` snapshot'ları kaldırıldı. Tile hash/commit eşlemesi korunur.
5. Piksel telemetry'si her pikselde tek global sayaç üzerinde atomic yapmak yerine
   workgroup içinde toplanır; global sayaç grup başına bir kez güncellenir.
   Ownership maskesinin ve piksel programının davranışı değişmez.

Ek GPU/bellek çalışması:

- Outline kodlama ve HarfBuzz shaping cache'leri zaten vardı. Biriken Slug verisinin
  her batch'te tekrar aktarılması kaldırıldı. Coherent host staging üzerinden
  device-local, append-only buffer'a yalnız yeni glifler eklenir; taşmada ve
  noncoherent host bellekte aynı çıktıyı veren eski upload yolu kullanılır. GPU
  ve staging buffer'ları ayrı ayrı en fazla 512 KiB ve upload bütçesinin sekizde
  biridir. Font/DPI/gamma/coverage değiştirilmedi.
- Adaptive sahne, gerçekten uygun bir compute programı isteyene kadar history
  görüntülerini, metadata arenalarını ve cache kernel'larını ayırmaz. İlk uygun
  program aynı raster çıktısını kullanır; kaynakları sonraki karenin hazırlığı
  oluşturur. Kaynak isteği ve runtime cache geçişleri ayrı regresyonlarla sınanır.
- Vulkan'a özel, isteğe bağlı dual-filter Kawase blur eklendi. `LEGACY` varsayılanı
  korunur. Yeni görünüm ayrı golden alanında doğrulanır; eski golden'lar onunla
  değiştirilmez. [Sözleşme ve algoritma kaynağı](vulkan.md#kawase-blur),
  [örnek](../examples/atomic/vulkan_blur.cpp) ve
  [incelenmiş örnek görüntü](img/readme/vulkan-blur.png) mevcuttur.

## Ölçüm protokolü ve sınırlar

AMD Radeon RX 9070 XT, vendor 4098/device 30032, driver 8389024. Gerçek showcase
yardımcıları; 1360×860, Spiral, minimum tile boyutu 64, başlangıçta 5000 öğe,
180 kare, sabit 1/60 dt ve 90/120/150'de gerçek Add etkinleştirmesi kullanılır.
Ölçüm hidden WSI ile, VSync kapalı ve readback olmadan yapılır. Bu protokol
masaüstü foreground pacing veya Görev Yöneticisi GPU yüzdesi sertifikası değildir.
İnteraktif showcase'in başlangıç VSync davranışı değiştirilmedi.

`tools/compare_vulkan_preparation.py` executable'ları çıktı dizinine kopyalayıp
hash'ler; önce/sonra sırasını değiştirir ve bütün 180 kareyi tutar. Eksik kare,
readback, adapter/driver değişikliği veya geometri değişikliği koşuyu reddeder.
Başlangıç sıçraması, ortalama, tekil maksimum ve frame index'i, üç Add karesi,
GPU sample sayısı, aktarım ve metadata sayaçları saklanır. Tam process süresi de
ayrı tutulur; hazırlığı ilk karenin dışına taşımak ücretsiz kazanç sayılmaz.
Global GPU süresi CPU süresine eklenmez. Timestamp'ler tamamlanmış submission'ları
ölçer. Ayrı Python regresyonları, 274 ms ilk kare ve 127. karedeki 500 ms sıçramanın
protokolden düşürülemediğini doğrular.

Eski 274 ms değer tanılama/önceki cold koşudan geliyordu. Driver pipeline cache'i
silinmedi; yeni seri o tarihsel tek kareyle doğrudan aynı koşu diye sunulmaz.
Güncel dondurulmuş baseline ile eşleşen sonuçlar kullanılmalıdır.

Kaynak sayaçları uygulamanın `VkDeviceMemory` ayırmalarını gösterir. Host-visible
upload/metadata, image allocation ve driver'ın kendi belleği farklı şeylerdir.
Üç kategorinin toplamı Görev Yöneticisi'nin dedicated VRAM değeri değildir;
driver/pipeline iç belleği ve işletim sistemi residency'si bu toplamda yoktur.
Memory probe aynı workload'u, aynı sayaç noktalarında çalıştırır; onun zamanları
performans kabul serisine katılmaz.

## Regresyon ve görüntü doğrulaması

`pui_vulkan_preparation_tests` bağımsız on iki regresyon içerir: paylaşılan/lazy
kernel'lar, readback altında history ömrü, gerçek image-copy spy'ı, bütçe reddinin
tekrarlanmaması, runtime cache geçişleri, job/descriptor taşması, gerçek 5000 Spiral
direct/staged/adaptive akışında çizim sırasında sıfır GPU allocation, adaptive
hazırlığının sonraki kareye geçmesi, resident/fallback glif piksel eşitliği ve
Kawase'nin tek sayılı ROI sınırları. Telemetry sonucu ayrıca ownership bitlerinin
popcount'u ile bağımsız olarak karşılaştırılır.

Mevcut test gövdeleri ve committed golden referansları değiştirilmez. Eski
REFERENCE ve bağımsız SDF/Slug ham 180-kare referansları ile immutable native
fingerprint'ler kullanılır. Bütün kare doğrulaması, ölçümdeki tüm tepe karelerini
ve komşularını da kapsar. Yeni Kawase tablosu ayrı bir dosyadır; SDF raster çıktısı
incelenip dondurulduktan sonra compute/cache, direct/staged ve etkileşim karşılığı
doğrulanır. `--update` kullanılmaz.

Kawase fixture'ları gerçek 180-kare text/caret/scroll/popup/DPI/gamma/target-recreation
akışını da yeniden kullanır. Varsayılan core/SDL blur çıktısı değişmez. Yeni blur'un
görüntüsü özellikle farklıdır; performans veya kalite üstünlüğü ölçümden bağımsız
varsayılmaz.

## Kabul sonuçları

Son device-local uygulama için Debug ve Release tam suite'leri geçti. Aşağıdaki
değerler altı eşleşmiş önce/sonra koşunun medyanlarıdır; her koşunun 180 karesi,
ilk kareleri ve uç değerleri ham kayıtta korunur. `GPU` sütunu 180 tamamlanmış
submission sample'ının toplamıdır; CPU kare süresiyle toplanmaz.

| Yol | CPU ortalama, ms | Koşu maksimumu, ms | GPU toplam, ms | Tam process, ms |
| --- | ---: | ---: | ---: | ---: |
| Reference | 5,005 → 4,942 | 35,708 → 35,557 | 107,359 → 109,923 | 1273,3 → 1278,8 |
| SDF/Slug | 2,345 → 2,164 | 31,887 → 31,276 | 84,735 → 83,664 | 761,7 → 750,0 |
| Compute | 5,703 → 4,962 | 72,947 → 30,525 | 781,395 → 759,715 | 1458,7 → 1239,0 |
| Compute/cache | 7,259 → 5,451 | 107,139 → 30,222 | 970,305 → 878,375 | 1767,6 → 1337,7 |
| Adaptive | 2,499 → 2,237 | 33,972 → 30,623 | 86,556 → 85,338 | 841,5 → 753,5 |

Compute/cache CPU ortalaması %24,9, medyan koşu maksimumu %71,8, GPU toplamı
%9,5 azaldı. Buna rağmen bu sahnede compute/cache hâlâ SDF raster yolundan
yavaştır. Varsayılan yol ve görüntü seçimi değiştirilmedi. Reference kontrolündeki
GPU toplamı %2,4 arttı; genel bir GPU hızlanması iddiası yoktur.

SDF ailesinde aktarım 747.943.242 → 402.388.850 byte oldu (%46,2 azalma).
Yeni glif verisi yalnız bir kez device-local belleğe aktarılır. Reference'ın
3.473.560.484 byte aktarımı değişmedi.

Glif cache kararı için ara sonuçlar da tutuldu. İlk coherent host cache serisinde
24 çiftlik ayrı SDF doğrulaması GPU toplamını 70,930 → 75,434 ms gösterdi (%6,35
artış). Daha sonra 18 tekrarlı, altı dengeli sıraya sahip üçlü deneyde baseline,
host ve device-local sonuçları sırasıyla 87,953 / 83,881 / 84,730 ms oldu.
Önceki artış bu deneyde tekrarlanmadı; host erişiminin kanıtlanmış neden olduğu
veya device-local seçeneğin host seçeneğinden kesin hızlı olduğu söylenemez.
Device-local seçenek toplam cache ayırmasını 2 MiB'dan 1 MiB'a indirir, bounded
residency ve artımlı aktarım sağlar. Farklı serilerin mutlak süreleri doğrudan
birbirleriyle karşılaştırılmaz; GPU saat/residency nedeni ölçülmedi.

Uygulama allocation sayaçları (aynı workload; zamanları kabul ölçümüne dahil değil):

| Yol | Upload tepe, MiB | Metadata tepe, MiB | Image, MiB | Toplam, MiB |
| --- | ---: | ---: | ---: | ---: |
| Reference | 254,40 → 254,40 | 0 → 0 | 10,84 → 10,84 | 265,23 → 265,23 |
| SDF/Slug | 60,79 → 24,38 | 0 → 0 | 6,84 → 6,84 | 67,62 → 31,21 |
| Compute | 60,79 → 24,38 | 26,13 → 29,81 | 6,84 → 6,84 | 93,75 → 61,02 |
| Compute/cache | 60,79 → 24,38 | 41,14 → 43,38 | 69,40 → 45,34 | 171,33 → 113,10 |
| Adaptive | 60,79 → 24,38 | 16,31 → 16,31 | 6,84 → 6,84 | 83,94 → 47,52 |

Toplam, kategori tepelerinin muhafazakâr toplamıdır; eşzamanlı gerçek residency
tepesi değildir. Device-local glif buffer'ının 512 KiB'ı upload toplamına zaten
dahildir; ayrıca eklenmez. Cache image sayacı image kategorisinin alt kümesidir.
Compute/cache toplamı %34 azaldı. Önceden ayırma descriptor sayısını artırdı
(cache 121 → 349); descriptor belleği driver'a aittir ve bu tabloda ölçülmez.
Varsayılan Reference yolunun yüksek allocation'ı bu değişiklikte azaltılmadı.

Null/D3D11 kontrolleri ayrı 16 tekrarda, toplam 11.520 kareyle ölçüldü. Null
ortalaması 3,087 → 3,170 ms (%2,7 artış), medyanı 2,496 → 2,519 ms; tekil maksimumu
22,853 → 21,562 ms oldu. D3D11 ortalaması 13,673 → 13,592 ms, tekil maksimumu
215,037 → 235,708 ms oldu. Dolayısıyla eski %1,3–1,4 null gerilemesinin tümüyle
ortadan kalktığı veya her yolun her tepe karesinin iyileştiği söylenmez. CPU kontrol
varyasyonu korunur; bu Vulkan kaynak değişiklikleri null çizimini hızlandırmaz.

Yeni Kawase ile legacy blur, aynı SDF/Slug sahnesinde altı çiftte karşılaştırıldı:
GPU toplamı 84,860 → 75,445 ms (%11,1 azalma); CPU ortalaması 2,144 → 2,194 ms
(%2,3 artış), medyan koşu maksimumu 30,279 → 28,195 ms, tam process 712,4 → 719,8 ms.
GPU tasarrufu vardır; toplam CPU kare süresini iyileştirdiği iddia edilmez.
Kawase isteğe bağlı kalır. Önceki host-cache serisinin farklı GPU sonuçları da
saklanmıştır; yalnız son aday serisi bu tablolara dahildir.

Kanıtlar:

- [Son native seri ve protokol](perf/vulkan_preparation_native.json),
  [10.800 karenin tamamı](perf/vulkan_preparation_native_frames.csv).
- [Null/D3D11 kontrol özeti](perf/vulkan_preparation_controls.json),
  [11.520 karenin tamamı](perf/vulkan_preparation_controls_frames.csv).
- [Kawase/legacy karşılaştırması](perf/vulkan_preparation_blur.json),
  [2.160 karenin tamamı](perf/vulkan_preparation_blur_frames.csv),
  [allocation sayaçları](perf/vulkan_preparation_resources.json).
- Ara [host-cache seri](perf/vulkan_preparation_host_intermediate.json) ve
  [tüm kareleri](perf/vulkan_preparation_host_intermediate_frames.csv);
  [24 çift doğrulama](perf/vulkan_preparation_host_confirmation.json) ve
  [tüm kareleri](perf/vulkan_preparation_host_confirmation_frames.csv);
  [üçlü glif deneyi](perf/vulkan_preparation_glyph_comparison.json) ve
  [9.720 karenin tamamı](perf/vulkan_preparation_glyph_comparison_frames.csv).
- [Görüntü matrisi, tepe kare birleşimi ve hash'ler](perf/vulkan_preparation_validation.json),
  [Debug suite](perf/vulkan_preparation_debug.log),
  [Release suite](perf/vulkan_preparation_release.log),
  [son glif artımlı aktarım regresyonu](perf/vulkan_preparation_regressions.log).

Frozen Reference/SDF/Slug alanlarında sekiz ayrı 180-kare doğrulaması ve yeni
Kawase direct/staged görüntü testleri kullanılır. Bütün tepe kareleri ve ±2
komşuları bu tam kare karşılaştırmalarına dahildir. Eski test gövdeleri ve golden
referansları değiştirilmedi; `--update` çalıştırılmadı.
