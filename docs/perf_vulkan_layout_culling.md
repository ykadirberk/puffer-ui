# Showcase görünmeyen iş maliyeti ve gerçek Masonry kaydı

Bu belge 26 Ekim ölçüm kaydıdır. Son birleşik renderer teslimi ve güncel kabul
kanıtı için [27 Ekim raporuna](perf_vulkan_integrated.md) bakın.

26 Ekim 2026. Bu çalışma mevcut SDF/Slug görüntüsünü korur. Genel Blick painter'ın
tamamlandığını veya CPU kabul kapısının kapandığını ifade etmez. Daha önceki
[program reuse raporu](perf_vulkan_program_reuse.md) ve
[açık bütünleştirme işleri](vulkan_integrated_painter.md) ayrı tutulur.

## Gerçek kullanıcı kaydı

Kullanıcı, Masonry/64/500 ön ayarıyla tek showcase açarak kontrolleri kullandı,
düşüşü gördü ve normal kapattı. Kayıt sırasında derleme, test veya benchmark
çalıştırılmadı. Kullanılan executable SHA256:
`4c7478d3c930cb1e74768fdb1dbacda9564352348e738671f19bf2178c5a5f71`.
Kayıt `build-vulkan-integrated/masonry-500/live-user-20261026-01.csv` altında,
SHA256 `e42fd817ae9f5bb05f0b96b4c0722b41c7052f31e5a1071ae13d9a50ba333647`.
[Eksiksiz kullanıcı CSV'si](perf/vulkan_live_masonry_20261026_frames.csv) ve
[segment/250 ms pencere türetmeleri](perf/vulkan_live_masonry_20261026.json)
ayrıca korunur.

24.825 satırın frame ID'leri 0–24.824 arasında kesintisiz. Gözlenen süre yaklaşık
42,76 saniye. Boyut 1360×860, render scale 1 ve SDF painter sabit. Kullanıcı
aralığı 12'den 0'a indirdi, farklı kareleri seçti, 12 Add ile 500'den 512 öğeye
çıktı, beş sayfayı gezdi ve Overview'e döndü. Son seçili öğe 34. Compact HUD ve
VSync de değiştirildi. Önceki otomatik Masonry/64/500 senaryosu bu ayarların
tamamını içermiyordu; onun sonucu gerçek kontrol sırasının kanıtı sayılmaz.

VSync/native mode geçişleri uyumlu: FIFO/2 açık, IMMEDIATE/0 kapalı. Kapalı
aralıklarda 250 ms'lik pencereler yaklaşık 487–600 FPS'ye iniyor. Bu pencereler
CSV'deki loop sürelerinden türetilmiştir; uygulamanın HUD güncelleme sınırlarıyla
birebir hizalı olduğu iddia edilmez. İlk 1200 FPS Overview bu kayıtta yok.

Örneğin 10.061–10.182 karelerinde 512 öğelik Layout yaklaşık 486,6 FPS:
ortalama UI build 0,829 ms, Present 0,486 ms, acquire 0,108 ms, slot wait 0,030 ms,
submit 0,122 ms; tamamlanan GPU örneği ortalaması 0,598 ms. Overview'e döndükten
sonra 12.642–12.775 karelerinde yaklaşık 533,6 FPS: build 0,492 ms ve Present
0,709 ms; GPU örneği 0,654 ms. GPU tamamlanma örnekleri gecikmelidir, tek CPU
karesiyle aynı işi gösterdiği varsayılmaz.

İzlenen kaynaklar kayıt boyunca sabit: 6 texture, 7.168.256 image byte,
460 descriptor set, 25.559.040 upload byte ve 4.678.528 metadata byte. Native draw
allocation ve resource creation farkları sıfır. Bu, izlenen renderer kaynaklarında
büyüme olmadığını gösterir; sürücünün bütün heap/VRAM maliyeti için kanıt değildir.

Bazı uzun karelerde ölçülen native aşamalar süreyi açıklamıyor. Örneğin 11.095.
kare loop 13,042 ms iken build 0,348 ms, Present 0,362 ms, acquire 0,064 ms,
slot wait 0,013 ms ve submit 0,024 ms. Geri kalan süre bu recorder'da ayrılmamış.
Olay pompası, foreground/visibility, thread CPU ve validation konfigürasyonu
kaydedilmedi. Bu nedenle her sıçrama Present'e, CPU çizimine veya GPU işine
atanamaz; kayıt foreground performans kabulü olarak sunulmaz.

## İkinci gerçek kayıt: kalıcı düşüş oluşmadı

Kullanıcı ikinci denemede düşüş oluşmadığını bildirdi ve pencereyi normal kapattı.
Bu executable yeni schema 2 recorder/bootstrap object'ini **ilk kayıtta kullanılan
orijinal renderer kütüphanesine** bağlar. Swapchain BLIT veya sonradan reddedilen
geç GPU hazırlığı adayını içermez. Bu nedenle hızlı ikinci koşu bir renderer
düzeltmesinin başarı kanıtı değildir. Executable SHA256
`1ee12dafc0be6135a0b006b5a6ddccc58c3d289ced3ec1965f0d95000a3f9bcd`,
renderer library SHA256
`f0d501bae93e4ff66df2eaf3ff5942f9c7d2d760eced7d1667e0434f501f575c`.
İki manuel kayıt sırasında da derleme veya benchmark çalıştırılmadı.

[İkinci kaydın değiştirilmemiş CSV'si, gzip](perf/vulkan_live_masonry_stages_20261026_frames.csv.gz)
ve [kapsam, segmentler, kaynak hash'leri ve bütün büyük sıçramalar](perf/vulkan_live_masonry_stages_20261026.json)
korunur. Ham CSV SHA256
`ce2f71c08a808b857a2f878f7662a0f3fd4cc0ee64f5267a3e2308355227dab3`.
Recorder 82.254 aralık gözlemiş; bounded ilk/son arena 65.536 satırı tutmuş.
**2048–18765 arasındaki 16.718 satır yok.** Tutulan aralıkların toplamı 62,83 sn;
bu bütün oturum süresi değildir. Son çizim karesi de sonraki build sınırı olmadığı
için gözlenmemiş. Eksik aralık için sıçrama yokluğu veya all-frame kabulü verilmez.

| Tutulan Overview aralığı | Satır | CSV loop FPS | Build medyanı |
| --- | ---: | ---: | ---: |
| İlk uncapped bölüm, 293–2047 | 1755 | 1017,5 | 0,260 ms |
| Ara dönüş, 49538–54626 | 5089 | 1081,9 | 0,297 ms |
| Son dönüş, 75883–82253 | 6371 | 1142,2 | 0,258 ms |

İlk bölümdeki VSync geçişinin 23 ms'lik karesi korunur. İkinci kayıt 523 öğeye
çıkmış; Masonry/64/gap0 tutulan uncapped aralıkların toplamı 889,6 FPS, build
medyanı 0,457 ms. İlk kayıttaki 512 öğelik aynı düzenin toplamı 626,2 FPS,
build medyanı 0,654 ms. Bunlar farklı kontrol sıralarıdır; eşlenmiş önce/sonra
performans kabulü olarak yorumlanmaz.

Tutulan ikinci kayıt satırlarının tamamında SDL cached focus/visibility geçerli
ve açık, `validation_requested=0`. Bu son alan **uygulamanın validation
istemediğini** gösterir; loader/SDK tarafından zorlanan layer'ları dışlamaz.
İlk kayıt bu alanları içermiyordu. Adaptör kimliği, GPU saatleri ve etkin loader
layer listesi iki kayıtta da yok; koşulların tamamının aynı olduğu söylenemez.
Native kaynak gauge'ları ilk kayıttaki değerlerle aynı ve sabit; draw allocation
ve resource creation farkları ikinci kayıtta da sıfır.

Yeni aşama sayaçları tekil sıçramaları gerçekten ayırıyor:

- 71215: loop **584,930 ms**, pump **583,633 ms**, build 0,426 ms,
  end 0,712 ms. Hangi OS olayı olduğu kaydedilmedi; pencere sürüklemesi diye
  kesinleştirilmez.
- 63404: loop **31,437 ms**, end **30,793 ms**, bunun içindeki native Present
  **30,767 ms**. Ana thread CPU tick 31,25 ms ve cycle 143.485.360; bu salt
  GPU'da bekleme kanıtı değildir. Özel sürücü fonksiyonu belirlenmiş değildir.
- 293: VSync kapatma sınırı loop **23,043 ms**, begin **21,216 ms**.
  Bu maliyet kalıcı uncapped sahne maliyetiyle birleştirilmez veya silinmez.

Loop eksi build/pump/begin/end toplamının medyanı yaklaşık 0,00030 ms; aşamalar
bu kaydın döngü süresini büyük ölçüde açıklıyor. Native timer'lar bu aşamaların
içindedir ve tekrar eklenmez. Windows CPU tick çözünürlüğü kaba olduğundan tek
karede sıfır CPU tick, o karede CPU işi yapılmadığını göstermez.

### Ayrı konfigürasyon hatası: validation değişkeninin değeri okunmuyordu

Eski bootstrap yalnız `getenv(...) != nullptr` kullanıyordu;
`PUFFERUI_VULKAN_VALIDATION=0` bile synchronization validation'ı açıyordu.
Aynı dondurulmuş schema 2 executable'da yalnız child-process değişkenini yok/`0`
değiştirerek dört dönüşümlü tekrar ve iki sayfa ölçüldü. 28.800 çizim karesinin
28.784 build-sınır aralığı ve her process'in başlangıç/teardown dahil CPU'su
korunur. Son kare/koşu sonraki sınır olmadığı için gözlenmemiştir. Sabit 500
Masonry/64 veya Overview, HUD/script/readback kapalı; hidden native WSI olduğu
için bu bir foreground kabulü değildir.

| Sayfa | Değişken yok: build / loop | Değişken `0`: build / loop |
| --- | ---: | ---: |
| Overview | 0,245 / 0,939 ms | 2,145 / 3,079 ms |
| Layout | 0,384 / 0,931 ms | 1,054 / 1,750 ms |

Bunlar bütün tutulan aralık ortalamalarının dört tekrar medyanıdır; ilk 60
kareyi dışlayan uygulama `--perf` özeti bunların yerine kullanılmadı. Geometry
sayaçları bütün eşlenmiş karelerde aynı. Bu kontrol hatayı ve maliyetini gösterir;
**ilk manuel kayıtta validation açık olduğunu kanıtlamaz**. Kullanıcı daha sonra
ilk, yavaş kaydın PowerShell oturumunda hem `PUFFERUI_VULKAN_VALIDATION` hem de
`VK_INSTANCE_LAYERS` değişkenlerinin tanımlı olmadığını bildirdi. Bu bilgi doğrudan
kullanıcı beyanıdır; eski CSV'de ölçülmüş bir alana dönüştürülmez. Bu iki değişken
ilk kaydın yavaşlığına açıklama olarak kullanılmıyor; false-value parsing hatası
bağımsız bir düzeltmedir. İlk kaydın süre
oranları da bu kontroldeki oranlarla birebir örtüşmüyor.
[Bütün koşular ve çevre konfigürasyonu](perf/vulkan_validation_env_control.json),
[28.784 ham aralık, gzip](perf/vulkan_validation_env_control_frames.csv.gz).

Bootstrap şimdi açık false değerlerini okuyor; `1` ile validation açılması
korunuyor. Yeni native regresyon gerçek example bootstrap'ta eski kodla
`value=0 requested=1 expected=0` ile başarısız oldu. Düzeltmeden sonra unset,
`0`, `false`, `OFF` ve `1` durumları ve bunların RGBA eşitliği geçti.
Ek headless test boş/mixed-case değerleri kapsar. Showcase suite 22 ve mevcut
native VSync testi değişmeden yeşil. Bu değişiklik mevcut SDF/Slug görüntüsünü
ve VSync seçimini değiştirmez.

## Görünmeyen hazırlığın azaltılması

- Dört gerçek vertex'ten hesaplanan AABB tamamen paint alanı dışındaysa rect,
  semantik kayda ve geometri hazırlığına girmiyor. Görünür rect ikinci kez aynı
  clip hesabından geçmiyor. Zero-area, fractional, transform ve DPI oraklları var.
- Showcase zemininde görünmeyen satırlar çizilmiyor. Koordinatlar eski tekrarlı
  float toplamasıyla ilerliyor; çarpımla satır atlamak pikseli değiştirebildiğinden
  kullanılmıyor. Orijinal vertex/index akışıyla karşılaştırma eklendi.
- Exact-size native vector text'te, eski font sınırlarıyla tamamen dikey olarak
  dışarıda kalan satır HarfBuzz/layout/outline hazırlığından önce reddediliyor.
  Legacy quantized-size ve custom shaper cache davranışı korunuyor. Çakışan
  görünmez label'ın görünür layout'u cache'ten atması için red/green regresyon var.

[34 koşunun 94.272 karesi](perf/vulkan_layout_culling_frames.csv),
[komutlar, hash'ler ve ölçümler](perf/vulkan_layout_culling.json),
[yeni bağımsız Grid/Masonry golden kimlikleri](perf/vulkan_layout_culling_golden_manifest.json).
Mevcut golden dosyaları ve test gövdeleri değiştirilmedi. Ayrı 3600 tam RGBA kare
karşılaştırması Grid/Masonry/Spiral, 1150/5000, native/offscreen, SDF/compute/cache,
direct/staged/adaptive ve sabit/yerel değişim yollarında geçti. Bütün karelerin
karşılaştırılması o serilerin tepe karelerini de kapsıyor. Zamanlama koşuları
readback yapmıyor; golden koşuları performans ölçümü sayılmıyor.

Ölçümler olumlu ve olumsuz sonuçları birlikte korur: Grid5000 ve Masonry5000 ana
thread cycle maliyetleri yaklaşık %63,48 ve %49,53 azaldı. Default180 Spiral ana
thread cycle %2,79 ve toplam kare penceresi cycle %3,53 arttı; process CPU %4,67
azaldı. 500 öğelik ayrı turda yerleşmiş Layout build %28,29 azalırken geri dönülen
Overview build %14,72 arttı. Bu kohortta process CPU da daha yüksek. Bunlar farklı
kohortlardır ve birbirinin yerine kabul kanıtı olarak kullanılmaz.

## Swapchain bağımlılığı

Kaynak incelemesinde acquired-image semaphore beklemesinin `ALL_COMMANDS`
kullandığı bulundu. Swapchain yalnız transfer destination; ilk erişim sahnenin
son blit işlemi. Bekleme ve ilk swapchain layout barrier'ının source/destination
stage'i ortak `BLIT` aşamasına daraltıldı. Üretici image beklemeleri, completion
sinyalleri, VSync politikası ve çizim/blend işlemleri değişmedi.

Yeni bağımsız native regresyon, doğrudan/staged ve VSync kapalı/açık 16 sunumda
gerçek swapchain handle'larını izler. Önce 16 geniş bekleme/barrier ile başarısız
oldu; düzeltmeden sonra geçti. Aynı test her kareyi bağımsız offscreen çıktı ile
byte byte karşılaştırır ve synchronization validation hata sayısını doğrular.
WSI, showcase VSync ve external-image testleri de geçti. Ayrı beş native SDF,
compute, cache, staged ve adaptive koşusunun 900 karesi eski 5000 Spiral RGBA
referansı ile eşit. Bu daraltmanın gerçek FPS düşüşünü tek başına giderdiği henüz
kanıtlanmış değildir; eşlenmiş readback'siz önce/sonra ölçümü ayrı tutulur.

Dört dönüşümlü tekrarda toplam **196.320 kare** ölçüldü. İki binary aynı benchmark
object dosyasını ve aynı SDL/HarfBuzz kütüphanelerini kullanır; renderer farkı
yalnız bu bekleme/barrier aşamalarıdır. Uzun seriler gerçek duvar saatiyle
animasyon, 512 Masonry/64/gap0/seçim34/compact HUD ve ayrı sidebar/kaydırma turunu
kullanır. Bunlar kullanıcının girdilerinin tam zamanlı replay'i değildir.

| Senaryo | Ortalama kare farkı | Ana thread cycle farkı | Process CPU farkı |
| --- | ---: | ---: | ---: |
| Masonry, 12.000 kare | +%1,09 | +%0,93 | +%0,94 |
| Sayfa turu, 12.000 kare | −%1,26 | −%1,24 | −%1,24 |
| 5000 Spiral SDF, 180 kare | −%0,12 | −%0,35 | −%0,94 |
| 5000 Spiral compute, 180 kare | −%0,26 | −%0,14 | −%1,11 |
| 5000 Spiral cache, 180 kare | +%0,65 | +%0,92 | +%3,30 |

Değerler her koşunun bütün kare ortalamasının veya CPU/cycle toplamının dört
tekrar medyanından hesaplandı. Tepe kareleri ve bütün başlangıç maliyetleri
korundu; sıcak bir alt pencereyle değiştirilmedi. SDL focus sayısı bütün
koşularda sıfır: **bu seri foreground kabulü değildir**. Sonuç kalıcı, büyük bir
kazancı veya 1200 → 500 FPS sorununun giderildiğini göstermiyor. GPU timestamp
başlangıcı artık acquire beklemesinden önce çalışabildiği için GPU aralıklarını
yalnız etkin GPU çalışma süresi gibi yorumlamak da doğru olmaz.
[Koşular ve hash'ler](perf/vulkan_acquire_stage.json),
[196.320 kronolojik ham kare, gzip](perf/vulkan_acquire_stage_frames.csv.gz).

Vulkan sunum çağrısı host üzerinde bekleyebilir; IMMEDIATE seçimi bu çağrının
her zaman beklemesiz olmasını garanti etmez.
[Khronos: vkQueuePresentKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).
Semaphore beklemesi ile sonraki layout transition'ın aynı dependency chain'de
olması gerekir; bu yüzden yalnız wait mask'i değiştirilmedi.
[Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

## Reddedilen aday: GPU karşılaştırmasını ilk ihtiyaca ertelemek

Bir aday CPU canonical arena'larını başlangıçta hazırlayıp GPU comparator
pipeline/snapshot/descriptor'larını tekrarlanan LOAD bağımlılığından sonraki
surface hazırlığına erteliyordu. Değişen sahnelerde 4.678.528 byte, yaklaşık
4,46 MiB izlenen GPU metadata tasarrufu ve byte eşit çıktılar sağladı. Normal
10.800 karede SDF kare ortalaması %0,34 azalsa da ana thread cycle %0,33,
process CPU %3,09 arttı. Cache process cycle %1,47 arttı. Bu sonuçlar korunur:
[ölçümler ve ret kararı](perf/vulkan_lazy_program_probe.json),
[10.800 ham kare](perf/vulkan_lazy_program_probe_frames.csv).

Gerçek sabit/yerel değişim kohortu daha önemli bir sorunu yakaladı: beşinci
çizilen karede (`frame=4`) geç hazırlık begin süresini **0,106 → 15,783 ms**,
toplam kareyi **1,877 → 18,974 ms** yaptı. Pipeline/allocation maliyeti ilk
kullanıma taşınmıştı. İlk kare daha pahalı olduğu için yalnız koşunun tek en kötü
karesine bakmak bu yeni sıçramayı gizleyebilirdi. 11.520 karenin tamamı tutulur;
idle/cache ana thread cycle %7,45, local/cache %4,24 arttı.
[Doğru retained protokolü](perf/vulkan_lazy_program_probe_retained.json),
[11.520 ham kare](perf/vulkan_lazy_program_probe_retained_frames.csv).

**Aday reddedildi ve kaynak koddan geri alındı.** Güncel davranış comparator
kernel/snapshot/descriptor'larını ilk çizim karesinden önce hazırlar. Bu nedenle
4,46 MiB tasarruf teslim edilmiş bir iyileştirme olarak sayılmaz. Yeni üç
regresyon, ilk frame öncesi hazır kaynakları, direct/staged LOAD birikiminin
bağımsız referansla byte eşitliğini ve gerçek allocation hizalamasıyla kısmi bütçe
reddinin kaynak sızdırmadan tam çizime dönmesini doğrular. Geç hazırlık adayında
başarısız olan testler geri alma sonrasında geçti; program suite 21 ve preparation
suite yeşil. Eski test gövdeleri ve golden'lar değiştirilmedi.

İlk statik ölçüm komutunda `--retained` unutulmuştu. O **7200 karelik ek normal
kohort** da atılmadı; dosya klasör adı statik olduğu anlamına gelmez. Açık protokol
düzeltmesiyle [ayrı JSON](perf/vulkan_lazy_program_probe_extra_normal.json) ve
[ham kareleri](perf/vulkan_lazy_program_probe_extra_normal_frames.csv) korunur.
Bu seride control/global drift de var. Hiçbir kohort foreground kabulü değildir.

10.800 karelik normal kohortta native draw allocation farkı optimize modlarda
sıfır; Reference kontrolde her iki tarafta koşu başına 20, toplam 240 upload
allocation kaydı var. Hiçbir modda draw resource creation artmadı. Native
sayaçlar C++/driver heap'lerinin tamamını kapsamaz; bütün renderer yollarında
koşulsuz allocation yokluğu iddia edilmez.

## Native draw hazırlığı ve eşlenmiş eski renderer kontrolü

Native grafik binding state'i command-buffer slot'unda tutuluyor. Aynı pipeline,
viewport, scissor, projection ve sampler set'i yeniden gönderilmiyor; her başarılı
`vkBeginCommandBuffer` state'i sıfırlıyor. SDF'nin byte renk kanalları 1 KiB'lik
constexpr tabloyla çevriliyor; 256 kanalın tamamı eski division işlemiyle bit
eşit. Reference, kendi division yolunu koruyor. Tabloyu Reference'a da uygulayan
aday onun ana thread cycle sayısını %2,76 artırdığı için reddedildi.

Yeni bağımsız dispatch regresyonu eski kaynakta dört kare × altı mod boyunca
52'şer tekrar pipeline/viewport/scissor çağrısını yakaladı. Güncel kaynakta
Reference/SDF/compute, direct/staged, target boyut değişimi, clear/copy ve slot
sıfırlamalarını geçiyor. Ayrı upload oraklı 6144 gerçek GPU vertex'inde xyuv,
index ve bütün renk kanallarının float bitlerini doğruluyor. Program suite 23
regresyon. Grafik state'ini struct'ın başında tutan, yalnız sona taşıyan ve
inline/noinline helper adayları ayrı ayrı ölçüldü; başarısız denemeler atılmadı.

Yalnız binding/renk farkının sekiz dönüşümlü tekrarı, 14.400 hareketli Spiral
karesinde SDF kare ortalamasını %1,77, ana thread cycle'ı %1,41 azalttı.
Reference ortalaması +%0,05, ana thread cycle'ı −%0,36. Compute ortalaması
+%0,53 ve ana thread cycle'ı +%0,07; compute maksimumu +%4,11, cache maksimumu
+%2,06. Bu küçük farklar genel CPU kabulü veya bütün spike'ların giderilmesi
olarak sunulmuyor.

Daha geniş kontrol için orijinal ikinci aşama implementation/shader kaynakları,
bugünkü CMake Release bayraklarıyla yeniden derlendi. Yalnız public declaration
ve `vulkan_stats` dönüş ABI'sini içeren native declaration dosyası uyarlandı;
eski algoritmalar korunuyor ve yeni cache sayaçları sıfır. Bu, eski executable'ın
byte eşit kopyası değildir. İki taraf aynı benchmark object'ini, SDL/HarfBuzz
kütüphanelerini ve linker seçeneklerini kullanıyor. Eski tarafın 2340 karesi
immutable normal/sabit/yerel-değişim RGBA referanslarıyla birebir geçti; güncel
adayın aynı 2340 karesi de geçti. Tüm 180 kare karşılaştırıldığı için ilk kare,
Add kareleri ve iki tarafın tepe kareleri de kapsamda.

| İkinci aşama → güncel aday | Kare ortalaması | Ana thread cycle | Frame-window process cycle | GPU toplam aralığı |
| --- | ---: | ---: | ---: | ---: |
| Hareketli SDF, 8 tekrar | +%0,17 | +%0,71 | +%0,16 | −%0,81 |
| Hareketli compute, 8 tekrar | −%1,47 | −%0,36 | +%10,49 | −%17,62 |
| Hareketli cache, 8 tekrar | −%3,61 | −%2,12 | +%6,87 | −%16,19 |
| Sabit cache, 4 tekrar | −%18,38 | −%22,12 | +%24,76 | −%96,92 |
| Yerel değişim cache, 4 tekrar | −%17,11 | −%20,92 | +%23,99 | −%92,74 |
| Sabit cache, aynı 60 Hz, 4 tekrar | −%10,43 | −%10,93 | +%8,32 | −%97,52 |
| Yerel değişim cache, aynı 60 Hz, 4 tekrar | −%11,03 | −%11,63 | +%7,45 | −%94,04 |

Her değer koşu istatistiklerinin tekrar medyanlarından türetilmiştir. 60 Hz,
VSync kapalıyken host pacing'idir; `total_ms` çizim ölçümüdür, uyku dahil duvar
saati FPS'si değildir. CPU zamanları/cycle, frame-window process ve ana thread
olarak ayrı tutulur; process'in başlangıç/kapanışı içeren toplamları da JSON'da
durur. GPU değerleri delayed submission timestamp aralıklarıdır, Task Manager
3D yüzdesi değildir. CPU artışı eşit sunum hızında da kaldığı için yalnız daha
yüksek FPS ile açıklanamaz. Tabloda yer almayan modlar ve bütün maksimumlar da
korunur; örneğin sabit compute maksimumu +%3,67, sabit cache maksimumu +%2,22.

Bir ek çağrıda `--retained` unutulduğu için klasör adına rağmen 7200 normal
hareketli kare daha ölçüldü. Bunlar `phase2-extra-normal` etiketi ve açık komut
satırlarıyla tutulur. Doğru sabit/yerel değişim serileri ayrıca yürütüldü.
Altı binding adayı ve beş geniş kontrol kohortunun toplam **124.560 karesi**,
başlangıç/tepe değerleri, allocation/resource sayaçları ve binary/run hash'leri
[karar/ölçüm JSON'u](perf/vulkan_native_draw_controls.json) ile
[kronolojik ham karelerde](perf/vulkan_native_draw_controls_frames.csv.gz).
Bütün bu otomatik WSI koşularında focus=0; foreground kabulü değildir.

## Mesh culling için ayrı null ve core kontrolü

43 header'lık iki snapshot yalnız mesh state ve primitive culling dosyalarında
farklı. Aynı uygulama object'i, SDL ve Release bayraklarıyla gerçek 5000 öğelik
null Spiral'in sekiz dönüşümlü tekrarı, 2880 kare ve üç gerçek klavye Add işlemini
kapsıyor. Kare ortalaması **2,896695 → 2,890113 ms (−%0,23)**, process cycle
**2.528.414.115 → 2.516.876.085 (−%0,46)**; process CPU medyanı her iki tarafta
539,0625 ms. Geometri/count akışları eşit; run maksimumu medyanı 10,210 →
10,036 ms. Önceden mevcut 5000 Spiral ve tepe-frame pixel oraklları iki tarafta
da geçti. Bu bileşen kontrolü tarihsel %1,3–1,4 farkının tamamını açıklamaz.

Sekiz core iş yükündeki 128 ölçüm de tutulur. `layout_37000` her iki tarafta
27 allocation içerir; koşulsuz allocation yokluğu denmez. `empty_ids` sonucu
0,4 → 0,5 ns, yani ham +%25; `text_area_gutter` +%0,52. Saat tabanına yakın
sonuçlar atılmaz ve bu veriyle neden atanmaz.
[Protokol/hash/özet](perf/vulkan_mesh_culling_control.json),
[core örnekleri](perf/vulkan_mesh_culling_core_samples.csv.gz),
[bütün null kareleri](perf/vulkan_mesh_culling_null_frames.csv.gz).

## Başarısız açılışın yanlış başarı bildirmesi

Gerçek `example_run` yolu, SDL init başarısız olduğunda cleanup sonucunu dönüyor,
selftest için `ok` yazıp exit 0 verebiliyordu. Yeni child-process regresyonu,
geçersiz video driver'ı ile bunu kırmızı olarak doğruladı. Cleanup sonrası
başarısız init artık exit 1; `--help` exit 0 kalıyor ve iki durumda da sahte `ok`
yazılmıyor. Aynı testin beş gerçek native validation konfigürasyonu RGBA eşitliği
kontrolü de yeşil. Önceden alınan stdout/stderr'siz eksik bir process kaydının
bu hatadan kaynaklandığı kanıtlanmadı; o kayıt hâlâ kabul dışında tutuluyor.

## Process CPU farkının thread bazlı tanısı

İki küçük, eşlenmiş sabit/cache/60 Hz tanılama koşusunda ana thread yaşam süresi
CPU'su 1015,625 → 984,375 ms, AMD driver giriş adresli worker CPU'su 2640,625 →
2890,625 ms. Bunlar kabul benchmark'ı değil; 50 ms aralıklarla yalnız başlatılan
child process'in thread sayaçları okunuyor. Sayılar process yaşam süresidir ve
frame-window sayaçlarıyla karıştırılmaz. Aynı worker'ın ek stack kontrollerindeki
26 örneği `NtWaitForSingleObject` → `WaitForSingleObjectEx` →
`amdvlk64.dll+0x385165` → `+0x24da25` zincirini içeriyor. Yerel disassembly'de
wait timeout'u sıfır; caller retry dalında `Sleep(0)` var. Bu, örneklenen worker'da
polling gösterir; hangi Vulkan API çağrısının veya sürücü içi işin nedenini tek
başına kanıtlamaz. Driver private sembolleri yok; DbgHelp'in büyük offset'li en
yakın export adları fonksiyon adı gibi yorumlanmaz.

UI, pencere ve shader içermeyen ham Vulkan kontrolünün beş varyantı; device,
timeline semaphore, fence/timeline signal ve wait işlemlerini ayrı yürütüyor.
Üç saniyelik sessiz bölümde bu worker'ın yüksek CPU'su görülmedi. Gerçek backend
factory'siyle yapılan on kontrol de device/SDF/compute hazırlığı, WSI surface
oluşturma, offscreen çizim ve ilk sunumu ayırdı. WSI'de dört sunumdan sonra,
font/glyph/blur/cache olmadan, worker yaklaşık 3 saniye CPU kullandı. Reference,
SDF, compute ve FIFO varyantlarında var; surface oluşturup sunmayan veya yalnız
offscreen çizen kontrolde yok. İki görünür fakat **focus=0** kontrolünde de var.
Bu nedenle davranış yalnız gizli pencereye özgü denmiyor.

İlk sessiz kontroller `Sleep(3000)` sırasında SDL event pump çalıştırmıyor.
Ek kontrollerde event pump yaklaşık 290 kez çalıştırıldı; worker'ın yaklaşık
3 saniyelik CPU'su sürdü. Main surface'in kaldırılması da bunu durdurmadı.
Üç ek overlay surface'i açılıp kapatıldığında birden fazla yüksek CPU worker
birikmedi; bu kontrol overlay-thread sızıntısı göstermiyor. Yalnız child process
environment'ında bütün implicit layer'ları veya sadece OBS layer'ını kapatmak
polling'i değiştirmedi; loader log'u aynı AMD adaptörünü doğruluyor. Bu, gerçek
uygulamada aktif capture maliyetini ölçmez. Ana uygulamalar ve sistem ayarları
değişmedi. Child-only layer ayırma mekanizması
[Khronos Loader debugging](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderDebugging.md)
belgesindedir.

Ayrı bir aday, üç saniye host tarafından bekletilen timeline wait ile queue'yu
boşta tuttu. Submit beklemeden döndü ama polling CPU'su azalmadı; aday üretime
alınmadı. Gerçek foreground kontrolü, bilgisayar aracında
`Computer Use app approval timed out` ile tamamlanamadı. Yenilenen pencere
listesi boştu. Uygulama focus/start gate koşulları sağlanmadığından hiçbir kare
sunmadan exit 1 verdi; koşu `INCOMPLETE_REJECTED_NO_DRAWING_FRAMES` tutulur.

Bu kontroller gerçek kullanıcı döngüsünü veya foreground performansını temsil
etmez; odak kontrolü hâlâ gerekir. Ek stack kontrolleri yalnız kendi child
worker'ını kısa süre durdurur ve bu yüzden kabul zamanlaması olarak kullanılmaz.
Hiçbir harici process, driver dosyası, registry, uygulama ayarı veya thread
priority'si değiştirilmedi. Sürücü polling'inin kullanıcının ilk gerçek kaydındaki
658 FPS sonucunun nedeni olduğu henüz kanıtlanmış değildir.
[29 tamamlanan kontrol ve reddedilen odak denemesi](perf/vulkan_native_thread_controls.json),
[ham sayaçlar, stack'ler ve tanılama kaynakları](perf/vulkan_native_thread_controls.json.gz).

## Doğrulama sınırı

Recorder'ın yeni schema 2 sürümü olay pompası, begin/end süreleri, Windows ana
thread CPU/cycle farkları, SDL'nin cached focus/visibility bayrakları ve istenen
validation konfigürasyonunu ekliyor. Geçerli olmayan sayaçlar ayrıca işaretli;
eski kayıtta bulunmayan alanlar geriye dönük sıfır sayılmıyor. Döngü aralığı önceki
build başlangıcından güncel build başlangıcına gider: önceki build/end ile güncel
pump/begin bu aralığın parçalarıdır. Native submit/Present sayaçları bu aşamaların
içinde olabilir; build ile tekrar toplanıp süre iki kere sayılmaz. Thread cycle
sayacı etkin GPU çalışma veya bütün process CPU sayacı değildir.

Schema 2 kayıt açık/kapalı, validation kapalı/açık üç native selftest'in aynı BMP
byte'larını verdi. Yeni iki recorder/helper regresyonuyla showcase suite 21 test;
10000 gözlem boyunca storage büyümesi veya C++ allocation olmaması mevcut testi
değiştirilmeden doğrulandı. GPU kontrolleri de native draw allocation/resource
farklarının sıfır kaldığını gösterdi. Bu 12-frame selftest'ler görünür performans
karşılaştırması değildir. Başka bir görünür benchmark'ı öne alma denemesi bilgisayar
erişimi onayında zaman aşımına uğradı; onun 60.000 karesi de focus=0 ve tanılama
olarak saklandı. Hiçbiri foreground kabulü diye sunulmuyor.

`build-vulkan-integrated/current-native-checkpoint/` içindeki önceki kaynakla
tam Release ve Debug doğrulaması bütün adımları geçti; o snapshot reddedilen
geç hazırlık adayını içerir. Geri alma ve yeni upfront-preparation regresyonları
sonrası kaynak `build-vulkan-integrated/post-probe-rollback/` altında tekrar
zip/hash ile donduruldu. Bu kaynağın tam Release ve Debug doğrulaması bütün
adımları geçti ve kaynak hash'leri iki suite boyunca değişmedi. Ardından validation
konfigürasyonu, binding/renk hazırlığı ve başarısız-bootstrap düzeltmeleri de
`build-vulkan-integrated/post-draw-binding/` altında yeni zip/hash ile donduruldu.
**Bu güncel kaynağın tam Release ve Debug koşularında da bütün adımlar geçti.**
279 compiler/test input'unun hash'i iki suite boyunca değişmedi. Yeni eklenenler
dahil 194 C++/header/inl dosyası ayrıca clang-format 22.1.3 dry-run kontrolünü geçti.
Mevcut test gövdeleri ve committed golden dosyaları değiştirilmedi.
[Kaynak/hash/doğrulama kaydı](perf/vulkan_native_binding_validation.json),
[tam Release log'u](perf/vulkan_native_binding_release.log),
[tam Debug log'u](perf/vulkan_native_binding_debug.log).

Bu doğrulanmış bir ara kayıttır. Genel tile cache, AA/texture/glyph compute,
group/alpha barrier ve tek üretim yolu tamamlanmış değildir. CPU gerilememesi,
bütün paket ve commit kabul kapıları açıktır.

Bu snapshot'tan sonra eklenen `pui_vulkan_source_tests`, ayrı native F32/compute
kaynak-renk oraklıdır. Direct/staged ve deliberate alpha-bit negative kontrolü,
HarfBuzz açık/kapalı varyantlarıyla ayrıca yürütülür. Tam binding snapshot'ı
doğrulaması ile bu yeni hedefin focused doğrulaması ayrı kayıtlardır;
[kaynak-float kanıtı](perf/vulkan_source_float_oracle.json) genel RGBA8 compute
painter'ın tamamlandığını bildirmez.
