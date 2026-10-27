# Vulkan compute: derin inceleme ve devam eden düzeltmeler

Bu belge 25 Ekim ara kaydıdır. Güncel birleşik renderer, son doğrulama ve
sınırlamalar [27 Ekim teslim raporundadır](perf_vulkan_integrated.md).

25 Ekim 2026. Başlangıç: `9f8679a`. Üretim kaynaklarının ikinci aşaması tam
Debug ve Release kabulünden geçti. Ayrı prototipler dağıtılacak değişiklik gibi
sunulmaz. Makaledeki genel painter ile mevcut suffix painter arasındaki mimari
fark hâlâ açıktır. Önceki raporların sonuçları aynı ölçüm serisine eklenmez.

## Güncel kabul: ikinci aşama

Altı dönüşümlü tekrar × beş mod × iki executable × 180 kare = **10.800 native
kare**. Normal Release executable; tanılama hook'u/readback yok. Her koşunun
başlangıcı, üç Add, maksimumu ve on tepe karesi korunur. Tablo koşu ortalaması
ve maksimumlarının medyanını gösterir. GPU sütunu 180 tamamlanmış submission
sample'ının toplamıdır; CPU süreleriyle toplanmaz. Gizli WSI/VSync kapalı protokolü
foreground sunum veya masaüstü GPU kullanım yüzdesi için kabul değildir.

| Mod | CPU ortalama, ms | Koşu maksimumu, ms | GPU toplam, ms |
| --- | ---: | ---: | ---: |
| Reference | 4,68491 → 4,74430 | 32,40370 → 32,86705 | 102,02148 → 102,41554 |
| SDF/Slug | 2,06210 → 2,03690 | 29,16140 → 28,92680 | 77,82506 → 77,14378 |
| Compute | 4,41902 → 2,25528 | 29,00915 → 29,06000 | 727,89544 → 294,92622 |
| Compute/cache | 4,90766 → 2,24887 | 29,68520 → 28,67265 | 808,74056 → 310,97552 |
| Adaptive | 2,14740 → 2,11508 | 28,71525 → 28,49695 | 77,17276 → 80,48782 |

Compute GPU toplamı %59,5, cache GPU toplamı %61,5 azaldı. CPU ortalamaları
sırasıyla %49,0 ve %54,2 azaldı. Compute GPU hâlâ SDF'nin yaklaşık 3,8 katıdır;
bu sonuç raster ile performans eşitliği veya genel painter tamamlanması değildir.
Reference CPU ortalamasındaki %1,3 ve adaptive GPU toplamındaki %4,3 artışlar da
kayıttadır; bütün yollar için hızlanma iddia edilmez.

[Native koşular ve executable hash'leri](perf/vulkan_deep_phase2_native.json),
[10.800 karenin tamamı](perf/vulkan_deep_phase2_native_frames.csv).

Sekiz dönüşümlü null/D3D11 kontrol tekrarı ayrıca **5760 kare** içerir. Buradaki
tablo bütün karelerin ortalamasını ve tekil en kötüsünü verir; native tablodaki
medyan-maksimum istatistiğiyle karıştırılmaz.

| Kontrol | Ortalama, ms | Bütün karelerin en kötüsü, ms |
| --- | ---: | ---: |
| Null | 2,852482 → 2,817835 | 13,8357 → 11,5892 |
| D3D11 | 13,629360 → 13,618077 | 210,3664 → 211,0845 |

D3D11 ortalaması yataydır ve tekil en kötü karesi yükselmiştir. Null sonucu
küçük bir ortalama kazancını destekler; spike'ların her ortamda azalacağı
iddiasını desteklemez. [Kontrol koşuları](perf/vulkan_deep_phase2_controls.json),
[5760 karenin tamamı](perf/vulkan_deep_phase2_controls_frames.csv).

Tam Release ve Debug `tools/run_tests.py --vulkan --format` koşuları
**all steps passed** ile bitti. Core/pump, 23 performans testi, yeni native
workgroup hedefindeki 10 test, 22 yazılım golden sahnesi, bütün native hedefler,
örnekler, kurulu core/SDL/Vulkan tüketicileri, API/tutorial ve format kapıları
geçti. Mevcut test gövdeleri ve golden dosyaları değiştirilmedi.

Son bağımsız görüntü matrisi Reference, SDF/Slug, forced compute, cache,
adaptive, staged/cache, staged/reference ve 1150/cache için **8 × 180 = 1440
kareyi** hem mevcut immutable fingerprint'ler hem önceden dondurulmuş R0/R1 RGBA
dosyalarıyla karşılaştırdı. Sıfır fark; ölçümdeki bütün tepe kareleri bu kapsamda.
[Golden matrisi ve executable hash'i](perf/vulkan_deep_phase2_goldens.json).
223 kaynak/test/araç dosyasının hash'i kapılar boyunca kontrol edildi; arşiv
`build-vulkan-deep/phase2-accepted-source.zip` içinde korunur.

Son profiler sıradan ve hizalı C++ new ile executable/statik SDL/HarfBuzz'ın
UCRT `malloc/calloc/realloc` import çağrılarını birlikte izler. Kayıt alanları
önceden ayrılmıştır; semboller karelerden sonra çözülür. C++ new'ın alttaki malloc'ı
iki kez sayılmaz. Beş allocation yolu selftest'te bağımsız doğrulanır.
DLL/sürücü heap'i, C aligned heap, free/liveness ve RSS kapsam dışıdır.

Aynı güncel showcase fixture'ı ve aynı profiler ile yeniden derlenen `9f8679a`
ile kabul adayı karşılaştırıldı. Compute/cache için **15.848 → 320 çağrı**:
C++ new **15.413 → 88**, calloc **174 → 105**, realloc **81 → 85**, malloc
**180 → 42**. İkinci kareden sonra C++ allocation yoktur. Kalan 18 C çağrısı
3. karede SDL cursor kurulumu ve 7. karede HarfBuzz'ın yeni script shape-plan
kurulumudur. **8–179. karelerde**, 90/120/150 Add'leri dahil, kapsanan C++ ve C
çağrılarının toplamı sıfırdır. Bütün süreç/sürücü için sıfır heap iddiası değildir.
İstenen byte toplamı 45.942.550 → 43.361.864'tür; retained/live/peak bellek
ölçüsü değildir. [Son heap özeti ve her karenin çağrıları](perf/vulkan_deep_heap_final.json).
Ham zincirler `profile-complete-before-v2/` ve `profile-final-after/` altında korunur.

## Derin incelemede kalan dört mimari bulgu

Bu bölüm üretim adayına eklenmemiş, ayrı executable'larda sınanmış yönleri
gösterir. [Deneyler, kaynak/executable hash'leri, bütün koşular ve görüntü
fingerprint'leri](perf/vulkan_deep_experiments.json) birlikte saklanır.
Prototiplerde tam Debug/Release/fault/lifetime kabulü çalıştırılmadı; aşağıdaki
sonuçlar onların üretime hazır olduğu anlamına gelmez.

### 1. P1 — Program sınırı texture/scissor batch sınırına bağlı

`vulkan_draw.inl:278` her native draw için `prepare_sdf_compute` çağırır.
5000 Spiral/180 karede **2366 program** oluşur. Texture/scissor değişikliği
tek başına önceki hedefi okuyan bir efekt değildir; buna rağmen binning,
dispatch ve compute↔graphics bağımlılığı yeniden kurulur. Üstelik raster
vertex/index üretimi, upload ve draw kaydı devam eder.

İzole prototip, aynı hedefe ait özgün native draw paketlerini önceden ayrılmış
alanda saklayıp yalnız gerçek hedef/okuma/efekt/submission bağımlılığında
kapatır. Komut bounds'larına özgün integer native scissor eklenir; shape
indeksleri program içinde yeniden adreslenir. Native vertex/index akışı ve
çizim sırası korunur. Texture örneklemesinin türevleri için ownership discard'ı
örneklemeden sonra yapılır; türev kullanmayan analitik kapsam erken elenebilir.
Bu, genel bir painter değil; mevcut suffix algoritmasının daha büyük programıdır.

Program sayısı **2366 → 929** oldu. Altı dönüşümlü normal Release tekrarında:

| Yol | GPU toplam, ms/180 | CPU ortalama, ms | Tam process medyanı, ms |
| --- | ---: | ---: | ---: |
| Compute | 294,82494 → 234,64374 | 2,26924 → 2,33562 | 738,93 → 752,38 |
| Cache | 311,86594 → 271,60938 | 2,28259 → 2,37078 | 750,45 → 765,58 |

GPU kazancı %20,4 / %12,9; CPU maliyeti %2,9 / %3,9 arttı. Ek command/shape
kopyaları ve geç kayıt bu basit prototipin maliyetidir. Bu nedenle doğrudan
üretime alınmadı. All-frame compute/cache/staged/1150 olmak üzere **900 kare**
eski immutable native fingerprint ve R1 RGBA ile eşleşti. İlk prototipin
135 ms maksimumu ve sonraki bütün aykırı kareler de ayrı ham seride durur;
son sürümün maksimumları önceki başarısız/ara sürümün yerine geçirilmez.

Uygulama yönü: kaynakları ilk kayıt anında doğru temsil ile yazan renderer-owned
program arena'sı; ikinci command/shape kopyası olmamalı. Queue slot'u, descriptor,
upload lease'i ve texture son-kullanım serial'ı bu programa ait olmalı.
Clear, copy/readback, blur, hedef değişimi, producer release, upload page reuse
ve submit programı tüketmeden belleği değiştirememeli. Init/kalibrasyon ve bütçe
taşması için tam eski raster yoluna güvenli dönüş korunmalı.

### 2. P1 — Değişmeyen bütün programı atlayan hızlı yol yok

Makalenin iki seviyeli cache'inin dış seviyesi mevcut SDF yolunda yoktur.
`vulkan_cache.inl:61` eski CPU retained cache'ini SDF için açıkça kapatır.
Sadece bu koşulu açmak güvenli değildir: eski key/event kaydı SDF/Slug
payload'larını ve ömürlerini içermez, her draw için vector allocation yapar.

Ek fixture, gerçek showcase'i 5000 öğe, reduced-motion açık, ambient kapalı,
`now=2`, `dt=0` ile kurar. Add yoktur. Başlangıcın dört geçiş karesi korunur;
**3–179 arası bütün RGBA görüntüleri birebir aynıdır**. Ayrı local-change
fixture'ında yalnız ilk öğenin hue değeri 90/120/150'de değişir. Görüntüler tam
olarak bu üç karede değişir; zamanlama ile sahte bir "idle" varsayılmadı.
İki fixture da SDF `9f8679a` ile güncel raster/compute/cache/prototip yollarında
180'er tam RGBA byte karşılaştırmasıyla doğrulandı. Referanslar üzerine yazılmadı.

Dört dönüşümlü tekrarın GPU medyan toplamları:

| 5000 öğe, 180 kare | Güncel SDF | Güncel compute | Güncel cache |
| --- | ---: | ---: | ---: |
| Sabit ekran | 72,7324 ms | 264,9079 ms | 272,3541 ms |
| Üç yerel renk değişimi | 73,5014 ms | 264,1627 ms | 271,1114 ms |

Görüntü sabitken de 13 program/kare çalışır. Normal completed telemetry'de
yaklaşık 263.981 yeni piksel ve 4829 cached piksel/kare görülür. Kalan maliyeti
yalnız animasyona bağlamak yanlıştır. [17.280 retained/prototype karelik iki
serinin ilki](perf/vulkan_deep_retained_frames.csv) ve
[program prototipiyle ikinci seri](perf/vulkan_deep_program_retained_frames.csv)
ayrıdır. Bütün başlangıçlar ve outlier'lar tutulur.

Uygulama yönü: clear dahil hedefin bütün programını kaydetmek; komutlar,
native clip, shape/Slug içeriği, DPI/gamma, hedef nesli ve texture **içerik**
revizyonlarını allocation'sız ve tam byte eşitliğiyle karşılaştırmak.
Sabit programda hedefin önceki bitmiş pikselini korumak, hiçbir binning/paint/
raster replay çalıştırmamak gerekir. Blur gibi iç üreticiler monoton GPU
revision yerine doğru producer-dependency kimliğiyle temsil edilmelidir;
external acquire/release/wait yan etkileri cache hit'te de korunmalıdır.
Readback ve prefix bağımlılığı, değişen hedef boyutu ve bütçe taşması için
açık flush/fallback gerekir. Hash eşitliği tek başına piksel eşitliği kanıtı
değildir; mevcut collision oracle'ı kaldırılmamalı.

### 3. P1 — Cache görüntüleri faydaya göre değil, ilk dört işe ayrılıyor

`vulkan_preparation.inl:149` ilk dört job/slot için tam boy görüntü ayırır.
Sabit ekranın pahalı Spiral işi bu dördün dışında kalır. Aynı görüntü sayısını,
tamamlanmış önceki slot'un qualified+cached pixel sayısına göre yeniden dağıtan
izole prototip, sabit karede **4829 → 268.810 cached piksel** sağladı. Yeniden
atamada history geçersizleştirilir; daha sonraki job'ların hash metadata'sı da
önceden hazırlanır. Draw içinde yeni görüntü oluşturulmaz.

Normal dört tekrarlı ölçümde sabit cache GPU **273,8048 → 252,8060 ms** (%7,7),
yerel değişimli cache **273,5220 → 253,7266 ms** (%7,2) oldu. Metadata peak'i
45.491.752 → 46.013.224 byte yükseldi; görüntü bütçesi/sayısı değişmedi.
Bu bellek azaltma değildir. Compute/cache/staged/1150 ve iki retained fixture
görüntü karşılaştırmaları geçti; tam fault/lifetime suite henüz bu prototipte yok.

Piksel hit oranındaki büyük artışın neden yalnız %7–8 kazanç verdiği de ölçüldü.
Bağımsız GPU stage tanılamasında paint **80,03 → 29,79 ms** düştü; fakat büyük
listelerin tam sıralı hash/eşitlik geçişi **15,65 → 40,05 ms** yükseldi ve native
graphics **85,80 → 86,65 ms** ile kaldı. Count/scan/emit de çalışmayı sürdürdü.
Ek timestamp içeren bu mutlak süreler kabul ölçümü değildir; geçişlerin nedenini
ayırır. Yalnız hit sayısını yükseltmek veya hash collision doğrulamasını kaldırmak
çözüm değildir. Önce program düzeyindeki reuse ve tekrar raster hazırlığı ele alınmalı.

### 4. P2 — GPU çalışma verisi mapped VRAM varken sistem heap'ine gidiyor

Gerçek buffer usage ve `memoryTypeBits=15` sorgusunda RX9070XT type1
HOST_VISIBLE|COHERENT sistem belleği, type2 ise DEVICE_LOCAL|HOST_VISIBLE|COHERENT
16GiB heap'tir. `gpu_bin_job::storage` (`vulkan_binning.inl:121`) yalnız coherent
tercih ettiği için type1'i önce seçer. Aynı seçim native blend LUT'u ve hazırlanan
çalışma bankaları için de kullanılır.

Tüm metadata'yı mapped VRAM'e alan prototip compute GPU toplamını
294,193 → 258,369 ms (%12,2) azalttı; fakat tam process medyanını
761,2 → 887,2 ms artırdı. Kalibrasyon yaklaşık 9 → 27 ms yükseldi. Bu genel
bayrak değişikliği reddedildi. CPU control ve LUT'u eski bellekte tutup yalnız
GPU bankalarını yerelleştiren hibrit adayda GPU kazancı yaklaşık %9 sürdü;
cache process medyanı 803,7 → 857,9 ms yükseldi.

Ayrı CPU timer tanılaması telemetry okumalarını eledi: cache için 362 stats
çağrısı her iki tarafta yaklaşık 0,62 ms. Cache storage allocate/bind/map toplamı
6,80 → 36,82 ms, free toplamı 5,54 → 32,54 ms oldu. Bu tanılama zamanları
kabul A/B'si değildir. Mapped VRAM'in başlangıç/kapanış maliyetini yalnız sıcak
180 kareye bakarak saklamak yanlış olur. Yaklaşık 45 MB sistem belleğinden
VRAM'e taşınır; toplam allocation byte'ı azalmaz.

Uygulama yönü: CPU'nun yazdığı command/shape staging alanı, GPU-only work/list/
route verisi ve CPU'nun okuduğu telemetry ayrı bellek rollerine sahip olmalı.
GPU-only alanın mapped olması gerekmemeli. Küçük BAR, coherent/noncoherent
fallback, queue completion ve budget/fault davranışları korunmalıdır. M3'ün
birleşik bellek modelindeki sonuçlar bu discrete GPU'ya doğrudan genellenemez.

## Üretime alınmayan diğer adaylar

Opak kaynağı ters arama adayı izolasyonda compute GPU'yu %3,4 yavaşlattığı için
geri alındı. Boş/clip dışı tile'larda workgroup-uniform erken dönüş adayı da
normal koşulda kazanç göstermedi. Boyut/overflow/çizim sırası açısından daha
basit görünmek tek başına performans kanıtı değildir. Kaynaklar, executable'lar,
bütün kareler ve kontroller deney receipt'inde hash'leriyle korunur.

## Tarihsel ara ölçümler

Aşağıdaki ilk/ikinci aşama çalışma notları tanılama sürecini korur. O tarihteki
"henüz" ve "sürüyor" durumları yukarıdaki son kabulün yerine geçmez.

## Güncel ölçüm ve neden

Gerçek showcase, 1360×860, Spiral size64, 5000 başlangıç öğesi, 180 kare ve
90/120/150'de üç Add. AMD Radeon RX 9070 XT, driver8389024. Native exe'ler ve
başlangıç kaynak arşivi değişiklikten önce `build-vulkan-deep/baseline/` altında
donduruldu. Eski test/golden dosyaları değiştirilmedi.

Yeni özel tanılama executable'ı her compute dispatch ve indexed draw çevresinde
GPU timestamp çifti kaydeder. Zaman aralıkları sadece GPU sorgularıdır; CPU
zamanıyla toplanmaz. Ek timestamp'ler yürütmeyi etkileyebilir: bu executable'ın
kare süreleri kabul ölçümü değildir. 180 karenin bütün geçiş kayıtları
`build-vulkan-deep/profile-{sdf,compute,cache}/gpu-stages.csv` altında korunur.

Başlangıç compute koşusunda toplam geçiş süreleri:

| Geçiş | Çağrı | GPU toplamı, ms |
| --- | ---: | ---: |
| Coarse bitset | 2366 | 0,966 |
| Fine count | 2366 | 47,928 |
| Prefix scan | 2366 | 302,937 |
| Ordered emit | 2366 | 72,131 |
| Compute paint | 2366 | 69,289 |
| Indirect args | 2366 | 2,336 |
| Graphics | 7280 | 62,535 |

Cache açık tanılama koşusunda seri compaction 720 çağrıda ayrıca 56,382 ms tuttu.
Shader'lar bunu açıklıyor: eski `bin_scan.comp` tek invocation ile bütün tile
sayılarını sırayla okuyup yazar; eski `sdf_compact.comp` aynı şekilde bütün dirty
flag'ları tarardı. Baskın sorun yalnız heap allocation veya paint shader'ı değil,
GPU üzerinde seri kurulan veri bağımlılığıydı.

## İlk uygulanan düzeltme

Prefix scan ve dirty compaction artık tek, taşınabilir 64-lane workgroup kullanır.
Her lane bitişik bir tile aralığını işler; ortak bellekteki deterministik prefix
scan sıralı çıktı offset'lerini üretir. Yeni buffer veya GPU allocation eklenmedi.
Toplamlar kapasitede doyurulur ve overflow ayrı taşınır; uint wraparound geçerli
liste diye yayımlanamaz. Liste ve primitive sırası korunur.

Sonraki tanılama koşusunda compute prefix scan 14,636 ms, cache koşusundaki scan
13,740 ms ve compaction 3,965 ms oldu. Diğer geçişlerin mutlak süreleri de değişti;
GPU saatleri ölçülmediğinden farklı koşuların bütün sürelerini tek tek toplayıp
sabit bir hızlanma garantisi çıkarılmaz. Aşağıdaki ayrı seri, dağıtılacak normal
benchmark executable'larını kullanır ve tanılama hook'larını içermez.

Dört dönüşümlü önce/sonra tekrar, beş mod, toplam **7200 kare**; hepsi tutuldu.
Tablo koşu ortalamalarının medyanını ve koşu maksimumlarının medyanını gösterir.
GPU sütunu 180 tamamlanmış sample'ın toplamıdır. WSI gizli, VSync kapalı, readback
yok; ölçüm sırasında build veya test çalışmadı.

| Mod | CPU ortalama, ms | Koşu maksimumu, ms | GPU toplam, ms | Tam process, ms |
| --- | ---: | ---: | ---: | ---: |
| Reference | 5,950 → 5,904 | 49,103 → 44,835 | 90,541 → 90,965 | 1523,3 → 1513,9 |
| SDF/Slug | 2,682 → 2,786 | 39,328 → 40,185 | 52,971 → 52,864 | 878,8 → 975,3 |
| Compute | 4,651 → 3,184 | 37,259 → 40,929 | 690,893 → 310,661 | 1258,3 → 979,7 |
| Compute/cache | 5,212 → 3,201 | 38,905 → 38,336 | 800,283 → 319,982 | 1360,4 → 988,6 |
| Adaptive | 2,781 → 2,811 | 40,858 → 35,851 | 53,853 → 52,901 | 906,8 → 909,7 |

Compute GPU toplamı %55,0; cache GPU toplamı %60,0 azaldı. CPU ortalamaları
sırasıyla %31,5 ve %38,6 azaldı. Buna rağmen compute GPU süresi SDF'den hâlâ
yaklaşık altı kat fazla. Compute tekil başlangıç tepesinin bu seride yükselmesi,
SDF CPU kontrolündeki artış ve bütün aykırı kareler kayıtta durur. Bunlar tam
hedefin bittiği veya her yolun hızlandığı diye sunulmaz.

[Koşular, hash'ler ve protokol](perf/vulkan_deep_phase1.json),
[7200 karenin tamamı](perf/vulkan_deep_phase1_frames.csv).

## Heap: gerçek çağrı zincirleri

Tanılama executable'ı sıradan C++ `operator new/new[]` çağrılarını sabit boyutlu
önceden ayrılmış kayda alır; semboller/PDB satırları koşu sonrasında çözülür.
Kayıp sample yoktur. Bu iz C `malloc`, hizalı new veya sürücünün kendi heap'ini
kapsamaz; dolayısıyla bütün süreç için sıfır allocation sertifikası değildir.
Ham zincirler `build-vulkan-deep/profile-*/heap.csv` dosyalarındadır.

Compute ve cache'in her biri 15415 çağrı yaptı: 15189 frontend, 215 renderer/font,
11 Vulkan. İlk iki kareden sonra sıradan new izinde Vulkan0, renderer39,
frontend16 kaldı. SDF15417 çağrı yaptı; compute özelinde binlerce ek C++ allocation
görülmedi. Paralel shader değişikliği bu sayıları değiştirmedi.

En büyük kaynaklar `ui::smooth`5035, `ui::animate`5034 ve
`ui::animate_rect`5014 adet unordered-map node allocation'ıdır. Bunlar ilk görüşte
ve yeni Add kimliklerinde oluşur; kalıcı UI durumunu saklar. Renderer tarafında
HarfBuzz outline map node'ları ve yeni glyph-run vector'leri ayrılıyor.
`harfbuzz_layout` her miss'te yeni bir `built.gs` oluşturup `reserve(count)` yapar;
mevcut cache, yeni bir run'ın depolamasını allocation olmadan sağlayamıyor.

[Frame/category/site özeti](perf/vulkan_deep_heap_baseline.json). CPU pool/arena
değişiklikleri henüz bu ilk aşamaya dahil değildir; allocation azaltma çalışması
hâlâ gereklidir. Hizalı/C allocation kapsamı da genişletilmelidir.

## Makaleyle mimari karşılaştırma

Yeni canlı bellek sorgusu ayrıca önemli bir yerleşim farkını gösterdi. RX9070XT
driver8389024 için storage/transfer/indirect buffer `memoryTypeBits=15` bildiriyor.
Type1, sistem heap'inde `HOST_VISIBLE|HOST_COHERENT` (flags6); type2, yaklaşık16GiB
device-local heap'te `DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT` (flags7).
`gpu_bin_job::storage` ve hazırlanmış metadata arena seçicileri yalnız
HOST_COHERENT tercih ettiği için önce type1'i seçiyor. Native blend LUT'u da
aynı storage yolunu kullanıyor. Bu, GPU read/write yoğun komut/list/route verisinin
device-local alternatif varken sistem belleğine konduğunu kanıtlıyor; yerleşimin
frametime etkisi henüz bu notta ölçülmüş değildir. Bellek tipi/heap boyutları ve
aynı buffer usage sorgusu `build-vulkan-deep/memory-probe.log` içinde saklandı.
Makalede kullanılan birleşik bellekli donanım varsayımları discrete Vulkan'a
otomatik taşınamaz. Device-local mapped tercihinin fallback/budget/fault/borrowed
device ve piksel kontratlarıyla birlikte ayrıca sınanması gerekir.

[Blick'in makalesi](https://blickeditor.com/blog/caching-an-immediate-mode-ui)
komut üretimi ile piksel üretimini ayırır; coarse/fine tile listeleri, sıralı
bağımlılık karşılaştırması, değişen tile'ların GPU listesi, öndeki opak içerikten
sonra işi durdurma ve değişmeyen programda painter'ı atlama prensiplerini anlatır.

Mevcut kaynakta saptanan durum:

- Renderer sınırı, coarse/fine listeler, tam sıralı karşılaştırma ve GPU indirect
  dispatch var. Hash eşitliği tek başına kabul edilmiyor; collision doğrulaması var.
- İki seri liste hazırlama aşaması bu ilk değişiklikte paralelleştirildi.
- İlk aşamada fine count/emit tile başına tek invocation ile candidate listesini
  tarıyordu; ikinci aşama bunu 64 lane'e taşıdı.
- Painter hâlâ bütün komutları eski→yeni sırada değerlendirir. Daha öndeki opak
  kaynak bulunduğunda daha eski değerlendirmelerin çoğu boşa gider. Blend sırası
  doğrudan ters çevrilemez: native RGBA8 yuvarlaması korunmalıdır.
- Boş tile'lar için de painter iş grupları kurulabilir. Aktif/dirty iş listesi
  yalnız gerçekten gerekli tile'ları içerecek şekilde incelenmelidir.
- Bu bir **destination-independent suffix cache**'idir. AA, Slug ve bilinmeyen
  geometri bölgelerinde tam raster replay sürer; bütün tile'ın bitmiş görüntüsünü
  saklayan genel painter değildir. Aynı programın tamamını atlayan hızlı yol yoktur.
- Texture/unknown komutlar cached ownership'i konservatif olarak geçersiz kılar.
  Daha genel piksel cache'i için içerik revizyonları ve blur prefix bağımlılıkları
  açıkça taşınmadan bu kısıt kaldırılamaz.
- Shape-group/alpha-barrier komutları mevcut frontend sözleşmesinde yoktur.
  Var olan per-primitive alfa davranışını grup opacity'sine çevirmek görüntüyü
  değiştirir; mevcut optimizasyon bunu yapmaz.

Bu nedenle mevcut uygulamanın makaledeki painter'ın tamamıyla eşdeğer olduğu
iddia edilemez. Hedefin kalan kısmı, bu farkların performans ve doğruluk etkisini
çözmek; yalnız adaptive raster bypass ile compute maliyetini gizlemek değildir.

## Şu ana kadarki doğrulama ve kalan kapılar

## İkinci aşama: sıralı paralel listeler ve sahipli CPU havuzları

Fine count ve emit artık tile başına 64 lane ile çalışıyor. Her lane farklı
coarse bitset sözcüklerini ele alır; emit 2048 komutluk parçaların exclusive
prefix'lerini kullanarak özgün primitive sırasını korur. Count için prefix
yerine daha ucuz workgroup reduction kullanılır. İki boyutlu dispatch desteği,
65 sütuna kısıtlanmış gerçek GPU listeleriyle ve 4099 komutla ayrıca sınandı.

Pikselin son geçerli opak kaynağını arkadan arayıp üstündeki saydam suffix'i
eski→yeni sırada blend eden aday ayrı sınandı. Diğer kaynakları tamamen aynı
iki normal executable'da yalnız paint SPIR-V değiştirildi. Dört dönüşümlü koşuda
compute GPU toplamı 294,765→304,671 ms/180-frame (%3,4 artış) oldu; dördünde de
arttı. Cache toplamı 314,255→319,648 ms arttı. SDF GPU kontrolü 78,019→77,520 ms
oldu. Bu aday reddedildi ve özgün forward painter geri kondu. Daha az gizli
kaynak değerlendirmek tek başına hızlanma kanıtı değildir; saydam katmanların
yeniden değerlendirilmesi maliyet yaratır. Kareler ve reddedilen kaynak ağaçları
`build-vulkan-deep/opaque-comparison/` ve `plain-variants-v2/` altında korunuyor.

Animasyon/rect hareket düğümleri, HarfBuzz outline düğümleri ve byte verisi ile
şekillendirilmiş vector-text run'ları sahiplerine ait size-class havuzlarından
gelir. Silinen bloklar context/font kapanışına kadar yeniden kullanım için
tutulur. Büyük veya aşırı hizalı istekler upstream'e gider. Bu, heap çağrısını
azaltırken CPU belleğini high-water seviyesinde elde tutar; GPU bütçelerini
artıran yeni kaynak eklemez. Cache hit/eviction/TTL, float hesabı ve ID'ler aynı.

MSVC'nin standart `unsynchronized_pool_resource` uygulaması tamamen boşalan bazı
chunk'ları upstream'e geri verdiği için erase→reinsert regresyonunda yeniden
allocation yaptı. Aday bu testte reddedildi; sahipli retained havuz aynı
depolamayı korur. Testler upstream'in hizalı çağrılarını da sayar, sadece normal
`operator new` hook'una dayanmaz.

İlk bounded sector kullanımından sonra olası küçük arc/topology scratch alanı
bir sonraki frame başında topluca hazırlanır. Daha önce clip dışında kalmış
kimliklerin görünür olmasına yetecek caption hit kapasitesi de önceki frame'in
gerçek `interact` çağrı sayısından hazırlanır. Clip dışı, blocked ve scope'suz
çağrılar da buna dahildir. Showcase ripple deposu başlangıçta hazırlanır.
Hiçbir primitive, hit rect, tween hesabı, alpha veya glyph kodlaması değişmez.

Ara tanılama, sıradan **ve hizalı** C++ allocation hook'larıyla eski implementasyon
arşivinden yeniden derlendi: compute/cache 15415→258 çağrı. Bu sonuç son outline
havuzu ve scratch/hit hazırlığından **önceki** adaydır; son sayım yerine
kullanılmamalıdır. İlk iki frame sonrasında renderer39→25, frontend16→7,
Vulkan0→0 kaldı. C malloc ve sürücü heap'i kapsam dışıdır. Sonuç sıfır heap
iddiası değildir. Ara GPU stage izi count50,395→4,799; scan316,739→10,418;
emit63,399→10,409 ms/180-frame gösterdi. Ek timestamp içeren bu executable,
normal önce/sonra frametime kanıtının yerini almaz.

Yeni native regresyonlar gerçek GPU sırasını, uint overflow'u, dispatch satırlarını,
cache dirty listesini ve AA/unknown/translucent suffix karşılaştırmasını kapsar.
5000 Spiral'in 180 karesi ikinci aşama adayında da eski immutable fingerprint ve
dondurulmuş R1 RGBA verisiyle eşleşti. Bu ara nottan sonra tamamlanan
Release/Debug kabulü, son normal zamanlama serisi ve kaynak hash'i doğrulaması
raporun başındaki güncel kabul bölümündedir.

- Yeni `pui_vulkan_workgroup_tests`: eski bağımsız CPU listesi oracle'ını da
  çalıştıran on test geçti. 63/64/65,127/129,257/1161/4099 tile sınırları,
  kapasite taşması, uint wraparound ve gerçek cache'te full/empty/partial dirty
  listelerinin sırası doğrulandı.
- 5000 öğelik compute/cache, 180 karenin hepsinde hem değişmez native fingerprint
  hem eski bağımsız SDF/Slug raw RGBA referansıyla eşleşti. Bütün ölçülen tepe
  kareleri bu kapsamda; golden güncellenmedi.
- GPU allocation bütçeleri ve ölçülen metadata/upload miktarları değişmedi.
- Altı yeni CPU regresyonu retained havuz tekrar kullanımını, hizalı upstream
  allocation'ları, state/TTL eşitliğini ve scratch hazırlığını sınar.
- Tam Debug/Release, 1150/staged/geniş etkileşim, bütün-kare goldens, son heap
  izi ve normal kabul ölçümleri tamamlandı. Ayrı idle/local-change ve mimari
  prototiplerin sonuçları güncel inceleme bölümünde üretimden ayrı tutulur.
