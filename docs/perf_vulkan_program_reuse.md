# Vulkan program reuse: ara ölçüm ve açık kabul kapıları

Bu belge 25 Ekim ara kaydıdır; aşağıdaki açık kapılar o aşamanın durumudur.
Güncel uygulama ve sonuçlar [27 Ekim teslim raporundadır](perf_vulkan_integrated.md).

25 Ekim 2026. Bu rapor, ikinci aşama kaynakları ile sonraki program/cache
düzeltmelerini ayırır. Kullanıcının istediği genel Blick painter, bütün
konfigürasyonlarda CPU gerilememesi ve teslim commit'i henüz tamamlanmış değildir.
Devam eden kapsam [bütünleştirme kaydındadır](vulkan_integrated_painter.md).
Önceki ikinci aşamanın bağımsız sonuçları
[kendi raporunda](perf_vulkan_compute_deep_analysis.md) korunur.

## Uygulanan işler

Native texture/scissor batch'leri, kaynak bağımlılığı izin verdiği sürece tek
sıralı GPU programında tutuluyor. Binning/compute hazırlığı her küçük native
batch için tekrarlanmıyor. Mutation, target, copy, effect, external release ve
submission sınırları gerçek sırayı koruyor; farklı örnekleme durumları aynı
primitive gibi ele alınmıyor.

Whole-program reuse, hazırlanmış iki CPU arena ve kesin payload karşılaştırması
kullanıyor. Eşit vertex/index/shape/span verisi yeniden kopyalanmıyor. Geometri
eşitliği yeterli sayılmıyor: LOAD veya blur scratch durumunun sabit nokta olması,
GPU'da bütün RGBA8 byte'larının önce/sonra eşitliğiyle ayrıca doğrulanıyor.
Sertifika yalnız tamamlanan submission'dan sonra okunuyor. Kısmi/saydam çizimler
kendiliğinden dondurulmuyor. SDF raster bu yolu compute suffix painter'ını veya
blend tablosunu hazırlamadan kullanabiliyor.

Sertifikalı clear/raster programı, eşit işlemleri upload/conversion/descriptor ve
native command recording'den önce erteleyebiliyor. İlk değişiklikte veya kısalan
kayıtta, ertelenen prefix bütünüyle ve özgün sırada yürütülüyor. Diğer efekt
çekirdekleri muhafazakâr record/discard yolunu kullanıyor. Bütçe sınırı veya
desteklenmeyen side effect tam çizime dönüyor; eksik primitive bırakmıyor.

GPU metadata/blend verisi yerel GPU belleğinde; CPU'nun yazdığı staging ve küçük
okunabilir kontrol alanı ayrı. Slot başına iki cache image lease'i, tamamlanan
qualified pixel kapsamıyla seçiliyor. Mevcut image taşınıyor; admission veya draw
sırasında yeni image oluşturulmuyor. Eşit kapsamda mevcut sahibi korunuyor.

Sonraki iki düzeltme ayrıca eklendi:

- Sürekli değişen programda sekiz ardışık başarısız adaydan sonra canonical kayıt
  120 kare duruyor. Bütün kareler tam çiziliyor. Ardından ardışık kareler yeniden
  gözleniyor ve yerleşen sayfa reuse'a dönebiliyor. HUD ve animasyon için her
  kare ikinci CPU tape'i kopyalamanın maliyeti sınırlandırılıyor.
- Slug resident buffer'ı dolunca eski glyph prefix'i korunuyor. Karar, global
  outline koleksiyonunun bütünü yerine gerçek batch glyph offset'lerinden
  veriliyor. Eski sayfaya dönüş ve yalnız şekil içeren batch'ler, başka sayfanın
  unresident outline verisini yeniden yüklemiyor. Yeni glyph gerçekten residency
  dışında ise eksiksiz upload yolu kullanılıyor. Yeni GPU allocation eklenmedi.

## Ölçüm pencereleri ve kalan CPU artışı

Altı dönüşümlü tekrar, beş mod, iki executable ve 180 kare toplam **10.800 kare**
içeriyor. Bütün başlangıçlar, Add kareleri, maksimumlar ve uç değerler korunuyor.
Normal Release binary, gizli native WSI ve VSync kapalı; readback yok. Bu protokol
foreground FPS veya masaüstü GPU kullanım yüzdesi için kabul değildir.

Tablo koşu ortalamalarının medyanını gösterir. Frame sütunu begin/build/end
duvar süresidir. CPU cycle ayrı process frame-window sayacıdır; bu iki ölçüm
birbirinin yerine kullanılamaz. GPU sütunu kayıt penceresinde tamamlanan timestamp
örneklerinin toplamından türetilmiştir.

| Mod | Frame, ms önce → sonra | Process CPU cycle farkı | GPU süre farkı |
| --- | ---: | ---: | ---: |
| Reference | 4,93361 → 4,97684 | +%1,48 | +%3,72 |
| SDF/Slug | 2,21390 → 2,27222 | +%3,15 | +%4,43 |
| Compute | 2,67677 → 2,61918 | +%4,95 | −%8,90 |
| Compute/cache | 2,69489 → 2,60991 | +%5,43 | −%16,63 |
| Adaptive | 2,32715 → 2,34882 | +%1,54 | +%8,58 |

[Koşular, komutlar ve binary hash'leri](perf/vulkan_program_admission_normal.json),
[bütün kareler](perf/vulkan_program_admission_normal_frames.csv).
Bu seri CPU gerilemedi kabulünü sağlamıyor. GPU veya frame kazancı bu açığı
ortadan kaldırmıyor. CPU tick sayacının kısa koşulardaki çözünürlüğüne ek olarak
cycle sayacı da tutuluyor; uygun olmayan sonuçlar ölçümden çıkarılmıyor.

Canonical admission değişikliğini tek başına ayıran başka altı dönüşümlü seride
frame-window CPU cycle SDF'de %3,47, compute'te %1,63 ve cache'te %2,74 azaldı.
Bu yalnız o değişikliğin etkisidir; yukarıdaki toplam başlangıç farkının tamamen
giderildiği anlamına gelmez.
[Ayrı nedensel karşılaştırma](perf/vulkan_program_admission_causal.json),
[10.800 ham kare](perf/vulkan_program_admission_causal_frames.csv).

Sabit ve yerel değişim workload'ları ayrıca **17.280 kare** içeriyor. Compute/cache
GPU toplamı sabit sahnede yaklaşık %97, yerel değişimde yaklaşık %92 azalırken,
kısa process CPU cycle penceresi yaklaşık %20–25 artabiliyor. SDF sabit sahne
cycle farkı −%2,19, yerel değişim +%1,44. Bu sonuçlar da korunuyor:
[koşular](perf/vulkan_program_admission_retained.json),
[bütün kareler](perf/vulkan_program_admission_retained_frames.csv).

## CPU thread ayrımı

İki dönüşümlü tanılama tekrarı, ana thread ve process CPU/cycle sayaçlarını ayrı
okuyor. Sabit compute/cache'te ana çizim thread'inin cycle maliyeti yaklaşık %25
azalırken, ek cycle başka thread'lerde görülüyor. Bu iki tekrarlı bir neden
araştırmasıdır; altı tekrarlı kabul serisinin yerine geçmez.
[Sayaçlar](perf/vulkan_program_main_thread.json),
[5760 kare](perf/vulkan_program_main_thread_frames.csv).

Ayrı, 3600 karelik örneklemeli process tanılamasında, ek maliyetin büyük kısmı
başlangıç adresi `amdvlk64.dll` aralığında olan bir thread'de görüldü. Yaklaşık
328 ms CPU'nun çoğu ilk bir saniyede oluştu ve ardından büyük ölçüde sabit kaldı.
Baseline compute tarafında aynı tür thread yaklaşık 78 ms CPU kullandı. Giriş
adresi, Toolhelp module aralıkları ve dinamik olarak çözülen
[NtQueryInformationThread](https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationthread)
ile okundu; thread durdurulmadı veya sürece kod enjekte edilmedi. Thread içindeki
hangi sürücü fonksiyonunun çalıştığı belirlenmiş değildir.

Bu uzun, örneklemeli compute koşusunda process CPU 7968,75 → 6625 ms, ana thread
7890,625 → 6296,875 ms oldu. Bunlar foreground kabul ölçümü değildir. Uzun koşudaki
kazanç, ilk 180 karedeki gerilemeyi silmek için kullanılmıyor. Ham thread/time/module
verisi `build-vulkan-integrated/worker-cpu/` altında korunuyor.

## Ana ekrana dönüşte 500 FPS bildirimi

Kullanıcı tam komutu doğruladı:

```powershell
& "D:\myimgui\out\build\x64-release\Release\pui_showcase_vulkan.exe" --vulkan-painter sdf --vector-text
```

Bu dosyanın güncellemeden önceki kopyası korundu: SHA256
`44c1ec559acb04c143ec50cd2f6eed8f74b0c774aa902929cbf807648356e0a3`,
dosya zamanı 18:49:51. Kaynak/kopya hash'i ve değişmeyen boyut/zaman kontrol edildi.

Önceki gerçek UI turu farklı `stable-sdf` binary'sindeydi; o sonuç bu standart
dosyanın doğrulaması olarak sunulmuyor. Eski standart dosyanın selftest'inde ve
güncel dosyanın selftest'inde ana ekran yaklaşık 0,9 ms kaldı. Bunlar kendi
selftest saatini kullanan özet tanılamalardır; gerçek kullanıcı etkileşimi veya
bütün karelerin performans kabulü değildir. Eski standart binary'nin gerçek UI
turunda bilgisayar erişim onayı tamamlanamadı.

Güncel standart uygulamada noktasal HUD okumaları 1469 FPS Layout ve 1283 FPS
Overview gösterdi. Bir bölümünde iki ayrı showcase açıktı ve kullanıcı girdisi
vardı; bu okumalar kontrollü karşılaştırma değildir. Tanılama için açılan ek
pencereler kapandı. Kullanıcı, kaydırma ve başka kontrolleri kullanıp geri dönmeyi;
ayrıca Layout lab'da ekleme sırasında ani FPS yarılanmasını bildirdi. Gerçek
sidebar girdisi, beş sayfada kaydırma ve native popup aç/kapat tanılamaları kalıcı
yarılanmayı üretmedi. Bunlar önceki otomatik denemelerdir. **26 Ekim'de kullanıcı
aynı düşüşü yeni kayıt destekli sürümle üretti ve pencereyi normal kapattı.**
24.825 kesintisiz karelik gerçek kayıt, VSync kapalıyken Layout ve geri dönülen
Overview'de yaklaşık 487–600 FPS aralıklarını gösteriyor. Başlangıçtaki 1200 FPS
Overview bu kayıtta yok; bu nedenle kayıt tek başına aynı sahnede 2,4 kat farkın
öncesi/sonrası kabulü değildir. Sorun giderilmiş sayılmıyor. Gerçek kayıt ve
sonraki kaynak ayrımı [ayrı raporda](perf_vulkan_layout_culling.md) tutulur.
Başlangıçtaki AMD thread maliyeti bunun kanıtlanmış açıklaması değildir.

İkinci manuel denemede kullanıcı düşüş olmadığını bildirdi. Yeni aşama
recorder'ı ilk kaydın renderer kütüphanesine bağlıdır; deneysel renderer
düzeltmeleri bu executable'da yok. Tutulan ilk uncapped Overview 1017,5 FPS,
son Overview 1142,2 FPS. Bounded kayıtta 16.718 ara satır bulunmadığı için bütün
oturumun sıçramasız olduğu doğrulanamaz. Tutulan bütün satırlarda SDL focus ve
visibility açık; uygulama validation istememiş. Loader tarafından zorlanan
layer'lar kaydedilmedi ve ilk kayıtta bu alanların hiçbiri yoktu. Dolayısıyla
ilk yavaş kaydın kök nedeni veya sorunun giderildiği kesinleştirilmiş değildir.
[İkinci kayıt ve aşama ayrımı](perf_vulkan_layout_culling.md).

## Görüntü ve regresyon kanıtı

Yeni admission ve resident-prefix regresyonları önce başarısız oldu, düzeltmelerle
geçti. Eski test gövdeleri ve golden referansları değiştirilmedi. Program suite'i
18 test, preparation suite'i yeni residency testiyle birlikte geçti. Yeni testler
direct/staged/compute, küçük residency bütçesi, gerçek unresident glyph, eski
sayfaya dönüş, yalnız şekil batch'i, değişen program, yerleşme ve yeniden değişimi
kapsıyor; native draw allocation/resource sayaçları artmıyor.

**3240 değişmez RGBA kare karşılaştırması** geçti: native/offscreen SDF, compute,
cache, staged ve adaptive; 1150/5000 öğe; sabit/yerel değişim. Her 180 karenin tamamı
okunup karşılaştırıldı, dolayısıyla bu serilerdeki bütün tepe kareleri de kapsamda.
Yeni golden üretilmedi veya mevcut dosyanın üstüne yazılmadı.
[Komutlar ve binary hash'leri](perf/vulkan_program_admission_goldens.json).

İki ara zamanlama serisi, exit 0 olmasına rağmen eksik CSV ile bitti ve reddedildi;
verileri silinmedi. Benchmark şimdi stdout'u teardown'dan önce flush ediyor.
Yeni portable test, 180 satır içinde eksik/tekrarlı/sırası bozuk frame ID'lerini
reddediyor. Eksik ana-thread sayaçları sıfır sayılmıyor. Bu eklerle portable Vulkan
tool testleri 22/22 geçti.

Güncel Release koşusunda bütün işlevsel/native/golden/örnek/tüketici/API/tutorial
adımları geçti. Son format adımı generated shader dizilerindeki virgül aralığı
nedeniyle başarısız oldu; çıktı ve generator düzeltildi, aynı tracked-file format
kontrolü ayrıca exit 0 ile geçti. Manifest ile karşılaştırma 20 SPIR-V dizisinin
byte'larının değişmediğini doğruladı. Debug tam doğrulaması da bütün adımlarıyla
geçti (`updated-debug-suite.log`). Sonradan eklenen isteğe bağlı showcase kare
kaydı ayrıca doğrulandı: üç yeni regresyon, native showcase testi ve kayıt açık/
kapalı SDL ile Vulkan ekran görüntülerinin byte eşitliği geçti. Kayıt arena'sı
başlangıçta ayrılıyor; 10000 gözlem boyunca büyüme/alokasyon veya dosya yazımı yok.
Bu tanılama, bütün renderer paketinin performans kabulü olarak kullanılmıyor.

Yeni kaynak hash'leri [ayrı kayıttadır](perf/vulkan_program_admission_sources.json).
Genel tile cache, AA/texture/glyph compute kapsamı, shape groups, alpha barrier,
efekt bağımlılıkları ve tek üretim girişinin açık işleri
[bütünleştirme planında](vulkan_integrated_painter.md) korunuyor. Bu ara rapor,
CPU kabulü veya bütün renderer paketinin tamamlandığı iddiası değildir.
