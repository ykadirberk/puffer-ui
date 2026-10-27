# Birleşik Vulkan painter: uygulama ve doğrulama

27 Ekim 2026.

Standart `pui_showcase_vulkan.exe`, tek `INTEGRATED` yapılandırmasıyla açılır. Python gerekmez. HarfBuzz açık derlemede shaping/Slug, native operandlar, exact cache ve yürütme seçimi birlikte kurulur. Library default'u ve açık tanılama seçenekleri bağımsız kalır.

## Değişiklik ve seçim

- Native vertex 48 yerine 24 byte taşır; RGBA8 kanalları GPU'da önceki CPU byte/255 bitlerine açılır. Topoloji/sıra korunur. Renderer-owned mesh ve native destek yolu hâlâ vardır.
- CPU program kimliği ve GPU fixed-point sertifikası bütün programın tekrar kullanılmasını sağlar. Programı yalnız gözlemlemek için ikinci GPU tape/binning/compute işi yapılmaz.
- Aynı komutlar değişen LOAD çıktısı üretiyorsa fine reuse otomatik seçilir. Kernel, blend atlası, bounded arena ve history, tamamlanmış frame sınırında hazırlanır. Desteklenmeyen pikselde tam native yürütme korunur.
- Son glif alpha değeri R32_UINT içinde exact float bitleriyle tutulur. Submitted recording token, tam fiziksel kaynak anahtarı ve generation kontrolü vardır. Hücre etiketleri görüntü kimliğinden ayrıdır.
- Native çıktı cache'i AA, yazı, image, mesh ve LOAD sonucunu kapsar. Üretici dirty/overflow kanıtı sonraki geçişe GPU'da taşınır; kanıt yoksa literal revision korunur. Kawase için kanıtsız normalizasyon yoktur.
- Rounded shape union için ortak fill/outline/glow ve bir aktif common-opacity scope eklendi. Alpha, çocukların sıralı RGBA8 kuantizasyonunu korur. Mevcut primitive alpha davranışı değişmez.
- Cold upload/ilk baseline sürekli değişim sayılmaz; native karede encoder stack, glyph admission ve gereksiz compute hazırlığı atlanır. Core retained pools/cooperative binning/erken conservative culling değişiklikleri de teslim kapsamındadır.

Mimari ve API: [integrated painter](vulkan_integrated_painter.md), [Vulkan kontratı](vulkan.md), [HarfBuzz/Slug](harfbuzz.md).

## Protokol

Frozen phase2 implementation/shaders; only public declarations/counter return ABI adapted. Not original HEAD executable bytes. Null control is frozen 9f source, independently linked.

5000 actual Spiral; hareketli üç Add (90/120/150), sabit ve 90/120/150'de yerel değişim. Direct/staged ve eski native/güncel native/güncel integrated, altı üçlü sırayla dönüşümlüdür. Son kohort 19.440 kare; her açılış ve aykırı kare dahildir. Timed koşular readback yapmaz. Golden ve profiler ayrı yürütülür. Derleme/test/ölçüm çakıştırılmadı.

Dört ara adayın 77.760 karesi de silinmedi; son seriyle toplam 97.200 native timed kare, 5.760 null kare ve ayrı 8.640 paced GPU-counter karesi tutuluyor. Ara adaylar final başarı kanıtı diye sunulmaz.

## Saf native ile son karşılaştırma

Süreler ms; p99 ve maximum bütün 1080 kareden, açılış dahil hesaplanır. Ana thread cycle değişimi karşılaştırmalı 180-frame pencerelerinden gelir.

| Senaryo | Upload | Ortalama önce → sonra | Ana thread cycle | p99 önce → sonra | Maximum önce → sonra |
|---|---|---:|---:|---:|---:|
| Hareketli/Add | direct | 2.2341 → 2.2210 | -0.46% | 4.2664 → 3.9433 | 38.7760 → 35.2239 |
| Hareketli/Add | staged | 2.2429 → 2.1978 | -1.74% | 4.5330 → 4.0949 | 36.5376 → 34.9789 |
| Sabit | direct | 2.0532 → 1.8605 | -13.09% | 3.2595 → 3.9624 | 36.3350 → 36.2764 |
| Sabit | staged | 2.0514 → 1.8852 | -12.98% | 3.0940 → 4.8748 | 39.2127 → 35.7001 |
| Yerel değişim | direct | 2.0617 → 1.8985 | -11.32% | 3.3969 → 4.0072 | 35.8473 → 37.1273 |
| Yerel değişim | staged | 2.0681 → 1.8827 | -12.65% | 2.9225 → 3.9340 | 38.3120 → 35.5506 |

Ana thread cycle altı kohortta da azalır; küçük farklar tek başına evrensel hızlanma kanıtı değildir. Process CPU/cycle değerleri ayrı tutulur ve her kohortta düşmez. Sabit/yerel değişim p99'u ve bazı tekil maximum'lar yükselir; bunlar tablodan çıkarılmadı.

## 60 Hz GPU ve bellek

Aynı 360-frame native WSI fixture'i iki dönüşümlü tekrarda, VSync istenerek ve 60 Hz harness sınırıyla ölçüldü. Tüm pencereler visible, focus sayıları sıfırdır; foreground FPS sertifikası değildir. Sayaçlar aynı PID/LUID/physical engine için tutulur; iki adaptör ortalanmaz. Aşağıdaki tablo RX 9070 XT'nin 3D engine'ini gösterir. Bu adaptörde compute dispatch'i de bu queue/engine üzerinde görünür; gerçek compute kapsamını native test sayaçları ayrıca zorunlu tutar.

| Kontrol | Senaryo | 3D % önce → sonra | Dedicated MiB önce → sonra | Shared MiB önce → sonra |
|---|---|---:|---:|---:|
| Saf native | Hareketli/Add | 2.954 → 2.953 | 149.83 → 160.98 | 56.08 → 56.12 |
| Saf native | Sabit | 2.735 → 0.306 | 149.83 → 174.24 | 56.08 → 56.60 |
| Saf native | Yerel değişim | 2.748 → 0.348 | 150.06 → 171.49 | 56.12 → 57.32 |
| Eski compute/cache | Hareketli/Add | 11.007 → 2.934 | 184.82 → 160.29 | 95.18 → 56.28 |
| Eski compute/cache | Sabit | 9.270 → 0.308 | 187.47 → 174.90 | 98.89 → 57.46 |
| Eski compute/cache | Yerel değişim | 9.640 → 0.378 | 192.21 → 170.86 | 100.82 → 56.44 |

Bellek saf native'den yüksektir; hazır alpha/glyph kaynakları ve cache/certificate maliyeti vardır. Eski compute/cache'ten düşüktür. PDH 0,5 saniyelik örneklerdir; örnekleme öncesi anlık tepeyi ölçtüğü iddia edilmez. Renderer allocation toplamları driver residency veya bütün process heap'i değildir.

## Alokasyon ve null kontrolü

Yeni painter'ın 180-frame örneklerinde draw memory allocation/resource creation sayaçları sıfırdır. Release C++/module-C collector'da eski native 322; integrated moving/staged 321, idle/local 329 cold çağrı kaydetti. Bütün serilerde 8–179. karelerde, üç Add dahil, yakalanan heap çağrısı sıfır. İlk sekiz kare korunur; HarfBuzz/UI başlangıcı sıfır alokasyon diye sunulmaz. Driver/DLL heap, aligned C malloc ve frees collector kapsamı dışındadır.

Null (16 dönüşümlü çift): ortalama 2.887065 → 2.870583 ms; process CPU 533.2031 → 530.2734 ms; maximum 11.9837 → 14.3987 ms. Tekil kötü kare artışı korunuyor; bu kaydın wall-time dışındaki nedenini kanıtlamadan algoritmaya veya işletim sistemine bağlamıyoruz.

## Test ve görüntü kanıtı

- Tam Release ve Debug `python tools/run_tests.py --vulkan --format [--release]` yeşil; kaynak hash'leri tur boyunca değişmedi. Örnekler, installed core/SDL/Vulkan consumers, API/tutorial/format kontrolleri dahildir.
- Mevcut test gövdeleri ve committed BMP/RGBA oraklları değiştirilmedi. Yeni testler ayrı hedeflerdir.
- Eski frozen RGBA'larla 18×180 = 3240 tam-byte karşılaştırma: moving/idle/local, direct/staged, üç tarafın tamamı. 540 frozen dosyanın önce/sonra SHA256'ları aynı. Her timed peak/komşusu bu bütün-kare kapsamına dahildir.
- Her build'de gerçek 5000/1150 Spiral direct/staged/cache/bitmap/budget vakaları ve iki 3600-frame/60-Add native eşitlik koşusu var. Mevcut 180-frame immutable domain ve tüm uzun koşu kareleri ayrı doğrulanır.
- Source float ve native blend oraklları, hash collision, producer/late update, glyph submit-token/generation, LOAD, alpha nesting/lifetime, group child identity ve otomatik native/fine geçişi ayrı regresyonlardır.
- Varsayılan showcase açılışı, altı override ve sekiz native-reference kare yeni bootstrap testinde doğrulanır. VSync/HUD geçişlerinin mevcut testleri korunur. Her uygulama aşaması Release/Debug derlenebilir bırakıldı.

Tüm receipt, kaynak/binary hash'leri, ham CSV arşivleri, suite log'ları ve sınırlamalar: [machine-readable final evidence](perf/vulkan_integrated_final.json). Kullanıcının tarihsel 1200→500 FPS geçişi için nedensel bir tekrar üretilemedi; bu olayın tamamen giderildiği iddia edilmiyor.

## Açma ve yeniden ölçme

```powershell
& "D:\myimgui\out\build\x64-release\Release\pui_showcase_vulkan.exe"
# Independent raster control:
& "D:\myimgui\out\build\x64-release\Release\pui_showcase_vulkan.exe" --vulkan-painter sdf --vector-text
python tools/compare_vulkan_integrated.py BEFORE.exe AFTER.exe --output NEW_DIR --runs 6 --staged
python tools/measure_vulkan_gpu.py BEFORE.exe AFTER.exe --integrated --before-cache --output NEW_GPU_DIR --frames 360 --runs 2
python tools/profile_vulkan_painter.py --program --heap-only --integrated --output NEW_HEAP_DIR
```

Python yalnız ölçüm/driver tanılama araçları içindir; uygulamayı açma şartı değildir.
