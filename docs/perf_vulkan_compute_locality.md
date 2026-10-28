# Vulkan compute, tahsis ve veri yerleşimi incelemesi

Başlangıç: `58ed3ab074cc0d6fc9b5f3b9d0019862d9c9adbc`, 27 Ekim 2026.
Durum: inceleme, üç performans düzeltmesi ve doğrulama tamamlandı.
Makale ile mimari farklar ve sahneye bağlı ölçüm sonuçları aşağıda belirtilir.

## Kabul kapsamı

- [Blick makalesinin](https://blickeditor.com/blog/caching-an-immediate-mode-ui)
  komut dili, sıralı coarse/fine binning, GPU iş listesi, iki seviyeli cache,
  içerik bağımlılıkları, grup/alpha işlemleri ve örtülen işin elenmesi mevcut
  kaynak ve gerçek GPU sayaçlarıyla karşılaştırılacak.
- Üretim INTEGRATED ve zorlanmış tile yollarında constructor/ilk geçiş/sıcak
  frame tahsisleri ayrılacak. C++ ve module-C kapsamı driver/DLL heap'i veya
  canlı bellekle karıştırılmayacak.
- CPU/GPU veri akışı, ardışık sahiplik, arena kopyaları, command/index erişimi,
  descriptor ve image çalışma kümeleri incelenecek. İyileştirmeler yeni veri
  veya geçersiz bağımlılığı yanlışlıkla cache'lemeyecek.
- 5000 öğelik gerçek Spiral; moving/Add, idle, local-change ve direct/staged
  serilerinde önce/sonra bütün kareler, başlangıç ve aykırılar tutulacak.
  Alpha, glyph, image/producer, blur ve bütçe taşması ayrı regresyonlar olacak.
- Mevcut SDF/Slug görüntüsü ve bütün committed orakllar korunacak. Tam-kare
  native karşılaştırmalar, tepe komşuları ve mevcut testlerin değişmeyen
  assertion'ları son Release/Debug kapısına dahil olacak.

## Değişmez başlangıç

`build-vulkan-locality/baseline/receipt.json`, Git kaynak arşivi ve iki Release
benchmark executable'ının SHA-256 değerlerini tutar. Önceden var olan dokuz
ilgisiz rapor taslağı kapsam dışındadır.

## Başlangıçta kaynakta doğrulanan noktalar

- `vulkan_alpha.inl` her integrated surface için tam boy scratch hazırlar;
  alpha scope hedef değiştirip bu resme çizer. Pikselde yerel bir accumulator
  değildir. Kullanmayan surface'in çalışma kümesi de bundan etkilenir.
- `prepare_sdf_surface` slot başına 32 ayrı `sdf_gpu_job` nesnesi oluşturur ve
  sıcak/geçiş döngüleri pointer listeleri üzerinden gezer. Ön tahsis edilmiş
  GPU arena'ları CPU nesnelerinin ardışık olduğu anlamına gelmez.
- `integrated_paint.comp` tile programını arka plandan öne yürütür. Sonraki
  opak komutun sileceği alttaki iş, kaynak/function lookup dahil, yapılabilir.
  RGBA8'in her operasyondaki kuantizasyonu korunmadan yönü ters çevirmek doğru
  değildir; görünür suffix aynı sırada yürütülmelidir.
- Önceki heap profiler normal frame'leri izliyordu. Constructor ve ilk frame
  öncesini de izleyen ayrı mod eklendi; sonuçlar aşağıda ayrı gösterilir.

## Tamamlanma kaydı

Ölçülmüş bulgular, alınan/alınmayan kararlar, regresyonlar, ham frame/hash
kayıtları ve son test kapısı aşağıdadır.

## Mimari karşılaştırması

| Makaledeki fikir | Güncel PufferUI kaynağı ve sınır |
| --- | --- |
| Renderer komut dili | Semantik `render_painter` sınırı, 64 bayt GPU komutu ve packed native operandlar mevcut. Layout/etkileşim core'da; temsil ve yürütme renderer'da. |
| Coarse/fine aday listeleri | `bin_coarse/count/scan/emit` GPU geçişleri mevcut; fine liste 64 lane ile sıralı üretilir. Kapasite taşması tam native çizime döner. |
| Değişen tile'ları GPU'da seçme | GPU dirty listesi/indirect dispatch mevcut. Hash tek başına eşitlik kanıtı değildir; operand ve bağımlı içerik de kesin doğrulanır. |
| İki cache seviyesi | Whole-program kimliği + GPU LOAD sabit-nokta sertifikası, ardından gerekirse fine output reuse. Üretim INTEGRATED her kareyi zorunlu compute'a göndermez. |
| Şekil ve glif tekrar kullanımı | Shape union, HarfBuzz/Slug ve son float alpha bitlerini tutan glif cache'i mevcut. Producer revision/generation, geç güncelleme ve submit-token yaşamı ayrı izlenir. |
| Örtülen işte erken durma | Genel front-to-back accumulation kullanılmıyor: ara RGBA8 kuantizasyonu değişirdi. İleri sıralı interpreter ve native replay korunur. Yeni konservatif tile-prefix denemesi de aşağıdaki ölçüm nedeniyle alınmadı. |
| Opacity sınırı | Tek aktif scope var; fakat piksel içi accumulator yerine önceden hazırlanmış tam boy RGBA8 scratch kullanılıyor. Makalenin bu bölümü birebir uygulanmış değil. |

Üretim yolu native gözlem, kesin whole-image reuse ve gerektiğinde compute/fine
reuse'u birlikte seçer. Zorlanmış fine yolu; compute kernel'larını, native
blend-function kalibrasyonunu, SDF/Slug kaynaklarını ve tam raster destek
yürütmesini gerçekten çalıştırır. Bu ayrım aynı etiket altında saklanmadı:
ölçümün `production` ve `fine` kohortları ayrı tutulur.

## Kabul edilen düzeltmeler

1. Eski `primitive_recorder::reset`, henüz surface yokken INTEGRATED gözlemi için
   4 MiB command + 8 MiB payload CPU rezervasyonu yapıyordu.
   Artık gözlem boş kapasiteyle başlıyor. Gerçek fine
   isteği gelirse tape, mevcut tamamlanmış frame-boundary preparation içinde
   hazırlanıyor; GPU karşılaştırıcılarını ilk kullanım karesine ertelemedik.
2. Her slotun 32 `sdf_gpu_job` nesnesi tek owning dizide. İki slotta 64 ayrı
   tahsis yerine iki dizi tahsisi var. Pointer görünümü ve adres kararlılığı,
   bank/descriptor sahipliği ve tamamlanmış-slot imha sırası korunur. Nesnelerin
   toplam boyutu azaltılmış gibi sunulmaz; kazanç tahsis sayısı ve ardışık erişimdir.
3. `function_prepare.comp`, 32'den uzun request listelerinde her anahtarı
   64 lane'in tekrar okuması/hash'lemesi yerine bir lane'e yükletir. Shared
   key/mask blokları owner lane'lere sıralı indeksleri dağıtır. FIFO/stamp,
   duplicate/eviction ve emit sırası eski scalar algoritmayla aynıdır. En fazla
   32 request için barrier maliyeti eklemeyen eski kısa yol kullanılır. Optional
   subgroup özelliği veya yeni GPU kaynağı gerekmiyor.

Lane'ler arası SSBO iletişiminde `function_common.glsl` içindeki `coherent`
bildirimi ve `memoryBarrierBuffer()` sonrası scan'in workgroup barrier'ı korunur.
Shared key/mask blokları iki tarafta uniform barrier ile ayrılır; ikinci emit
geçişi önceki owner yazılarını bu sınırdan sonra okur. Bu sözleşme
[Khronos GLSL bellek kurallarıyla](https://docs.vulkan.org/glsl/latest/chapters/builtinfunctions.html)
karşılaştırıldı. Kabul edilen `function_prepare` SPIR-V'sinin manifest hash'i
ve `spirv-val --target-env vulkan1.3` denetimi de geçti.

Profiler'a constructor öncesi örnekleme aralığı da eklendi: interception
selftest'inden sonra, ilk benchmark statistics çağrısından önceki kayıtlar
`frame=-1`. Bu, process'in pre-main/DLL başlangıcının tamamı değildir. Relocated
benchmark'in diagnostic seçenek başlığını bulabilmesi için normal `tests/`
include yolu düzeltildi. Discard edilebilen whole-program kayıtları GPU query
modunda reddedilir; yalnız explicit forced-fine programlar bunu kullanabilir.

## Tahsis ve GPU tanısı

`accepted-profile-summary.json` ve orijinal CSV/stack kayıtları
`build-vulkan-locality/` altında saklanır. Aynı collector ile:

| Yakalanan module heap çağrısı | Önce | Sonra |
| --- | ---: | ---: |
| Production bütün izlenen aralık | 2417 | 2415 |
| Production frame -1 | 2096 | 2094 |
| Fine bütün izlenen aralık | 2405 | 2343 |
| Fine frame -1 | 2085 | 2023 |
| Fine `prepare_sdf_surface` doğrudan çağrıları | 68 | 6 |

Production'da kaldırılan iki rezervasyonun nominal kapasitesi toplam 12 MiB.
Heap isteği hizalama/allocator ek yükünü de içerir. Process içindeki toplam
requested-byte farkı buna ek olarak küçük miktarda farklı olabilir; SDL
environment string uzunluğu gibi akış gürültüsünü renderer kazancı saymıyoruz.
Dört after yapılandırmasında 8–179. kareler arasında, üç gerçek Add dahil,
yakalanan heap çağrısı sıfır. İlk sekiz kare ve frame -1 sayıları korunur.
Driver/DLL heap, aligned C malloc, frees/liveness ve gerçek residency bu
collector'ın kapsamında değildir. GPU draw memory/resource sayaçları ayrı ölçülür:
son 25.920 zamanlanmış karenin tamamında, önce ve sonra, draw memory allocation,
draw resource creation ve readback toplamı sıfırdır. Önceden var olan sıcak-yol
garantisi korunur; bu üç sıfır yeni kazanım diye sayılmaz.

920 `function_prepare` dispatch'inin toplam timestamp süresi tanı kaydında
184.136 → 36.550 ms oldu. Diğer aşamaların süreleri de clock/driver etkisiyle
değiştiğinden bunu kabul benchmark'i yerine kullanmıyoruz. Olağan executable
karşılaştırması aşağıda ayrı verilmiştir; bütün ham dispatch'ler tutulur.

## Alınmayan deneme ve kalan mimari maliyet

`stage3/rejected-pruning-source.zip`, denemenin kodunu ve bağımsız native RGBA
regresyonunu; `stage3/pruning-measure` 8640 zamanlanmış kareyi tutar. 64 lane,
tile'ın bütün piksel merkezlerini kesin örten son opak rect/rounded-gradient
komutunu arayıp önceki prefix'i atlıyordu. Suffix orijinal sırada kalıyordu.
24 tam native RGBA karelik yeni deneme testi direct/staged, Release/Debug'da
geçti. Ancak fine direct moving GPU 2.8129 → 2.8542 ms/kare; staged moving
2.8010 → 2.7554 ms/kare oldu. Küçük, tutarsız fark yeni interpreter/barrier
maliyetini haklı çıkarmadı. Üretim kodundan ve son kabul kaynağından çıkarıldı.
Önceki genel reverse-search denemesi de kazanç göstermemişti; sonuç gizlenmedi.

Alpha scratch, 1360×860 RGBA için en az 4,462 MiB texel alanı ve Vulkan'ın
allocation padding'ini kullanır. Kullanılmayan surface'te de hazırlanır.
Bu incelemede onu lazy first-draw tahsisine çevirmek kabul edilmedi. Scratch'i
LOAD snapshot ile alias etmek de probe'un alpha boyunca eski bytes'ı saklama
ömrünü çakıştırır; ilave spill kopyası/bağımlılık gerektirir. Ölçülmüş kazanç ve
bağımsız alias/lifetime kanıtı olmadan mevcut kaynak sahipliği değiştirilmedi.
Piksel içi alpha; image, blur prefix'i ve native ara-kuantizasyon için ayrı bir
sayısal tasarım gerektirir. Bu açık mimari fark, tamamlanmış gibi anlatılmaz.

GPU komutları/shape kayıtları zaten kompakt ve slot arenaları bounded. Yeni
admission ve job dizisi belirli tekrarları kaldırır; bütün cache locality'nin
matematiksel olarak maksimum olduğu veya bütün başlangıcın tahsissiz olduğu
iddia edilmez. SDL keymap, HarfBuzz ilk script-planı ve driver sahipliği bu
değişikliğin kapsamına alınmadı.

## Son olağan executable ölçümü

AMD Radeon RX 9070 XT, driver `8389024`; Release MSVC/LTCG; 1360×860,
başlangıç 5000 öğe/64 boyut Spiral; 180 kare, hareketlide 90/120/150'de gerçek
Add, sabit ve yerel-değişimde sayı 5000. Her kohortta altı dönüşümlü önce/sonra
çift. Zamanlamada readback, profiler, eşzamanlı build/test yok. Pencereler gizli
ve odaksızdır; kullanıcıdaki foreground FPS olayının çözüldüğü iddia edilmez.
Zamanlama executable'larında VSync kapalıdır. Ek 60 Hz kontrolü `--hz 60`
benchmark pacing'ini kullanır; FIFO/VSync ölçümü değildir.

`production` whole-program/native observation; `fine` aynı INTEGRATED'in
yalnız whole-program cache'i kapatılmış halidir. İki tarafta aynı bayraklar
kullanılır. 25.920 karenin hiçbiri, açılış veya aykırı olduğu için çıkarılmadı.
GPU sütunu, CSV aralığında tamamlanan submission süreleri toplamının kare
sayısına bölümüdür; aynı CPU karesine ait GPU süresi anlamına gelmez. Her fine
kohortunda 1080 GPU örneği, production'da 1080–1083 örnek tutuldu.

| Yol/upload/sahne | Kare ort. önce → sonra ms | GPU toplam/kare ms | p99 önce → sonra ms | Maks. önce → sonra ms |
| --- | ---: | ---: | ---: | ---: |
| production/direct/moving | 3.0343 → 3.2676 | 0.3655 → 0.3848 | 5.71 → 13.18 | 44.97 → 59.80 |
| production/direct/idle | 2.6194 → 2.6047 | 0.0222 → 0.0192 | 6.27 → 5.56 | 46.69 → 42.68 |
| production/direct/local | 2.6715 → 2.6221 | 0.0371 → 0.0342 | 8.23 → 5.79 | 48.71 → 46.49 |
| production/staged/moving | 3.2588 → 3.1757 | 0.4170 → 0.4468 | 9.00 → 8.52 | 44.93 → 48.62 |
| production/staged/idle | 2.5082 → 2.8011 | 0.0191 → 0.0190 | 6.35 → 7.27 | 45.97 → 50.30 |
| production/staged/local | 2.8145 → 2.7917 | 0.0432 → 0.0409 | 6.76 → 7.80 | 47.24 → 62.61 |
| fine/direct/moving | 4.9249 → 4.2633 | 3.4294 → 2.5164 | 10.90 → 9.44 | 42.32 → 40.85 |
| fine/direct/idle | 4.5550 → 4.0792 | 3.2922 → 2.6141 | 9.98 → 8.81 | 57.50 → 61.20 |
| fine/direct/local | 4.5298 → 4.0294 | 3.2497 → 2.5069 | 11.24 → 8.45 | 44.92 → 44.31 |
| fine/staged/moving | 5.2382 → 4.5505 | 3.5436 → 2.2145 | 19.78 → 14.43 | 70.01 → 52.83 |
| fine/staged/idle | 4.3164 → 4.1840 | 2.8037 → 2.0244 | 7.76 → 14.01 | 45.52 → 56.29 |
| fine/staged/local | 4.2447 → 3.9665 | 2.7441 → 2.0378 | 8.00 → 8.37 | 72.01 → 52.69 |

Fine ortalama kare süresi altı kohortta azalır; GPU toplam/kare düşüşü yaklaşık
%21–38 aralığındadır. Main-thread cycle altı fine kohortta %8,6–13,8 azalır.
Fine staged idle p99 ve bazı tekil en kötü kareler yükselir; tablo bunları korur.
Renderer'ın GPU image/metadata/upload tahsis sayaçları bu tur önce/sonra aynıdır:
production image `14.344.448`, metadata `4.678.528` bayt; fine image `58.908.928`,
metadata `35.307.648` bayt. Upload yapılandırmasına göre `25.589.760` veya
`49.051.648` bayttır. Bunlar driver residency ölçümü değildir. 12 MiB kazanım
CPU tape rezervasyonundadır; GPU belleği azalmış gibi sunulmaz.
Production direct moving ve staged idle ortalamaları yükselir. Aynı adayın
ilk ve son serideki `.text` SHA-256 değeri aynı olduğu halde zamanlar ayrışır:
`a003e467cf96ff0fbc3011bf3a32eac6049ab2d7131dc80299568c943ebf1453`.
Bu gözlem tek başına artışların nedenini kanıtlamaz.

Bu nedenle 8640 karelik iki ek kontrol tutuldu:

| Kontrol/kohort | Ortalama önce → sonra ms | p99 ms | Maks. ms | Main-thread cycle değişimi |
| --- | ---: | ---: | ---: | ---: |
| identical_binary/direct/moving | 2.9308 → 3.1177 | 9.05 → 9.83 | 53.25 → 50.91 | +5.04% |
| identical_binary/staged/idle | 2.4941 → 2.6468 | 9.31 → 8.56 | 41.93 → 40.52 | -0.43% |
| paced/direct/moving | 3.2247 → 3.0699 | 6.86 → 5.86 | 50.05 → 37.39 | -3.25% |
| paced/staged/idle | 3.8290 → 3.7590 | 12.46 → 17.00 | 50.48 → 47.84 | -2.27% |

`identical_binary` iki tarafta aynı executable/komut kullanır; yaklaşık %6
ortalama ayrışması bu ortamın gürültüsünü gösterir. `paced`, bu iki kohortta
gerçek önce/sonrayı 60 Hz'de karşılaştırır: ortalama ve main-thread cycle azalır,
staged idle p99 artar. Bunlardan bütün üretim sahnelerinde hızlanma veya bütün
spike'ların giderildiği sonucu çıkarılmaz. Diğer uygulamalara müdahale edilmedi.

## Görsel ve regresyon doğrulaması

Orijinal `58ed3ab` executable'ı altı direct/staged × moving/idle/local sahnesi
için 1080 değişmez tam RGBA kare üretti. Aynı eski kaynağın bağımsız native SDF
ve forced-fine yolları, aday production ve aday fine bu dosyalara karşı
4×1080 = **4320 tam-byte karşılaştırma** yaptı: fark sıfır. 1080 dosyanın
önce/sonra SHA-256 değerleri aynı. Zamanlamadaki her top-10 tepe ve ±1 komşusu
bu bütün-kare kapsamındadır; liste JSON'da ayrıca saklanır. Testler arasında
golden dosyası güncellenmedi. Pixel koşularının süreleri benchmark'e karıştırılmaz.

Yeni `pui_vulkan_locality_tests` Release/Debug direct/staged'da geçer:
constructor/native gözlemde boş tape, sonraki fine hazırlığı, job adres/ömür ve
rebuild kararlılığı, düşük bütçede tam çıktı/boş job havuzu. Gerçek GPU admission
testi 0/1/31/32/33/63/64/65/127/128/255/256/257 request, üç row kapasitesi ve
13 epoch için bütün row/request/list/indirect alanlarını eski scalar algoritmayla
karşılaştırır. Portable yeni dört test, yanlış iş yükü/readback/eksik frame'i
reddetmeyi, bütün aykırıları korumayı ve discard edilebilen programlarda güvenli
profiler sınırını doğrular. Eski test assertion'ları ve golden referansları sabit.

## Son test kapısı

Tam `python tools/run_tests.py --vulkan --format --release` ve ardından
`python tools/run_tests.py --vulkan --format` kapıları geçti. Her yapılandırmada
96 native kayıt vakası, core/pump/performance/memory/preparation/showcase,
eski SDL/performance/Spiral ve tepe golden'ları, 1150/5000 tüm-kare native
oraklları, iki 3600-kare/60-Add uzun koşu, tüm örnek selftest'leri, installed
core/SDL/Vulkan tüketicileri ve API/tutorial/clang-format denetimleri geçer.
334 kaynak dosyasının test öncesi/sonrası SHA-256 değerleri aynı.
Release kapısı 1806.7 saniye sürdü.
Debug kapısı 1243.6 saniye sürdü.

Makine kaydı ve bütün ham zamanlama/profile CSV/log arşivi:
[final JSON](perf/vulkan_compute_locality_final.json),
[ana kareler](perf/vulkan_compute_locality_frames.csv.gz),
[ek kontroller](perf/vulkan_compute_locality_controls.csv.gz),
[ham kayıtlar ve çalıştırma harness'leri](perf/vulkan_compute_locality_raw.zip).
Yerel immutable binary/source/RGBA arşivi `build-vulkan-locality/` altında.
Önceden var olan dokuz ilgisiz taslak korunur.
