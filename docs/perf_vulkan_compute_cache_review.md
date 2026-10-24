**Vulkan compute/cache performans incelemesi — 24 Ekim 2026**

İncelenen kaynak: `29d7a0602b2f2a0a707a6a691a68904adbabf130`.
Karşılaştırma tabanı: `8c62fc4204e89ac93343593b4ff94907f6b2f18b`.
Bu çalışma bir incelemedir; üretim kodunda performans düzeltmesi uygulanmadı.
Mevcut test gövdeleri, shader'lar ve golden referansları değiştirilmedi.

Compute/cache'in maliyeti yalnız tek bir yavaş satırdan kaynaklanmıyor.
Çizim işi; kaynak oluşturma, kapasite büyütme, pipeline derleme ve geçmiş
saklama sorumluluklarını birlikte taşıyor. Kaynak hazırlığı çizimden önce
tamamlanmalı; çizim hazır alanları kullanıp komut kaydetmeli. Ayırmaları başka
bir fonksiyona taşımak tek başına yeterli değildir: ayırma sayısı, tekrar sıklığı
ve saklanan veri de azaltılmalıdır.

**Kanıt ve ölçüm sınırları**

- GPU: AMD Radeon RX 9070 XT; vendor 4098/device 30032/driver 8389024.
- Gerçek showcase yardımcıları, 1360×860, Spiral, minimum boyut 64, 5000 başlangıç
  öğesi, 180 kare, sabit 1/60 dt; 90/120/150'de gerçek Add etkinleştirmesi.
- Yeni incelemede 55.440 zamanlanmış kare tutuldu: 53.280 null kontrol karesi ve
  2.160 native tanılama karesi. Hiçbir ilk kare veya aykırı değer çıkarılmadı.
- Null serileri sırasıyla 24×4, 20×5, 20×5 executable koşusudur; sıra mevcut
  balanced Latin schedule ile döndürüldü. Zamanlama sırasında başka bir build,
  golden testi veya benchmark çalıştırılmadı.
- Native tanılama kopyasında ek CPU zamanlayıcıları vardır. Bunlar API maliyetini
  ayırır; dağıtılan sürümün yeni performans sertifikası değildir. CPU/GPU süreleri
  toplanmadı. GPU pass süreleri ayrı timestamp aralıklarıyla ölçülmedi.
- Tanılama başlatma evresi `frame=-1` olarak ayrıca tutuldu. İç içe API/job/history
  zamanlayıcıları birbirine eklenemez.
- Ham stdout/stderr, tanılama kaynakları, bütün özel derlemeler ve komutlar
  `build-vulkan-review-20261024/` altında korunuyor.
- [Birleşik JSON](perf/vulkan_compute_cache_review_20261024.json) kaynak commit'ini,
  binary/script hash'lerini, API aralıklarını ve her serinin tepe karesini içerir.
  [Bütün kareler](perf/vulkan_compute_cache_review_20261024_frames.csv) ayrı tutulur.

**Compute neden SDF raster'dan pahalı?**

Compute yolu mevcut SDF geometrisinin oluşturulmasını, yüklenmesini ve raster
draw kaydını sürdürüyor. Buna coarse/count/scan/emit, mask temizleme, compute
painter, cache açıksa compare/compact/restore/commit ve geçmiş kopyaları ekleniyor.
Raster fragment shader'ı ownership bitine göre pikseli atlıyor; bütün CPU/vertex
işi ortadan kalkmış olmuyor. Spiral'in hareketi, AA kenarları, glifleri ve diğer
uygun olmayan komutları faydalı yeniden kullanımı sınırlıyor.

Önceki değişmez kabul serisinde 5000 öğe için Add dönemi SDF/Slug raster 2,154373 ms,
compute 3,980774 ms, compute/cache 4,766808 ms idi. Eski REFERENCE 5,416847 ms idi.
Compute/cache'in bütün kare ortalaması 6,747968 ms ile eski 4,727990 ms'den pahalıydı.
Dolayısıyla bu sahnede en iyi yol mevcut SDF/Slug raster. Bu değerler önceki
[kabul raporundandır](vulkan_gpu_painter_qualification.md); yeni tanılama
zamanlarıyla aynı seri gibi birleştirilmedi.

**Görüntüyü değiştirmeden ele alınabilecek beş bulgu**

1. **P1 — Pipeline sahipliği çizim işine bağlanmış; aynı programlar tekrar kuruluyor.**

   `vulkan_binning.inl:8–33,232` ve `vulkan_sdf.inl:49–110,226–240`:
   her yeni job dört binning ve altı painter/cache pipeline'ı oluşturuyor.
   İlk kare 10 job/100 pipeline, ikinci kare 14 yeni job/140 pipeline oluşturuyor.
   İki frame slot'u için 180 karede 28 job kuruluyor. Cache kapalıyken de
   compare/compact/restore/commit programları oluşturuluyor.

   Ayrıntılı tanılama koşusunda compute ilk karesi 219,1096 ms;
   hazırlık 186,3438 ms, pipeline aralıkları toplamı 145,1163 ms oldu.
   Bunun 43,4659 ms'si cache kapalıyken kullanılmayan dört cache pipeline'ında
   geçti. Bu koşuda başlangıç kalibrasyonu 65,6255 ms ile ayrıca frame dışında
   ölçüldü. Sonraki sıcak sürücü koşularında pipeline oluşturma çok daha ucuz;
   bu yüzden bütün başlangıç sıçramasını sabit bir shader derleme süresiyle
   açıklamak doğru değil.

   Değişmeyen descriptor/pipeline layout'ları ve compute pipeline'ları cihaz
   sahipliğinde paylaşılmalı. Yalnız seçilen painter'ın ihtiyaç duyduğu programlar
   hazırlanmalı. Job yalnız veri aralıklarını ve yürütme durumunu taşımalı.
   Derlemeyi frame0'dan device init'e taşımak toplam açılış maliyetini yok etmez;
   süreç başlangıcından ilk sunuma kadar süre ayrıca ölçülmeli.

2. **P1 — GPU metadata için çok sayıda ayrı ayırma ve tam ihtiyaç kadar büyütme var.**

   `vulkan_binning.inl:48–95,99–115,281–293` ve `vulkan_sdf.inl:49–64,120–132`:
   küçük her buffer ayrı `VkDeviceMemory` alıyor. Büyüme tam istenen boyuta
   yapılıyor; job, history ve geçici alan hazırlığı çizim çağrısından tetikleniyor.
   Native device mevcut `render_device::prepare_geometry` sözleşmesini de
   override etmiyor; showcase'in hazırlık çağrısı native kaynakları hazırlamıyor.

   Sıcak shader koşulundaki cache ilk karesi 103,9037 ms oldu. İlk karede 200
   buffer ayırması 48,8466 ms, ikinci karede 280 ayırma 72,0256 ms sürdü.
   Bütün 180 karede cache için 841 ayırma 212,8662 ms; cache kapalı compute için 526
   ayırma 128,7046 ms ölçüldü. Başlatma kalibrasyonundaki yedi ayırma bu sayılara
   dahil değildir. İlk karede mapping 0,5396 ms idi; baskın kalem `vkAllocateMemory`.

   Frame slot'u sahipliğinde büyük, yeniden kullanılan buffer arena'ları
   hazırlanmalı. Tape, liste, shape, route ve control verileri hizalı aralıklar
   almalı. Descriptor'lar buffer+offset/range üzerinden kurulmalı. Kapasite
   geometrik büyümeli ve gerçek Vulkan allocation requirements bütçeye sayılmalı.
   Büyüme ve emeklilik planı, timeline tamamlanmasını gözeterek çizim kaydından
   önce yürütülmeli. Hazır kapasite aşıldığında görüntüyü eksiltmeyen tam raster
   fallback korunmalı; ölçümden işi veya açılış maliyetini gizlemek kabul edilmez.

3. **P2 — Cache bütçesine sığmayan kaynaklar tekrar tekrar hazırlanıyor.**

   `vulkan_sdf.inl:112–148`:
   altı history buffer'ı, snapshot görüntüsünün bütçeye sığıp sığmadığı
   belirlenmeden büyütülüyor. Kalan bütçe bir tam görüntüye yetmiyorsa
   `saved==nullptr` kalıyor; sonraki draw yeniden `make_texture` çağırıyor.
   Kısmi boş bütçe nedeniyle `used>=budget` kontrolü bunu durdurmuyor.

   Cache tanılama serisinde 1209 snapshot oluşturma denemesi ve toplam 8,3195 ms
   bu aralıkta kaydedildi. İlk 28 job'un hepsi cache görüntüsü edinemiyor;
   başaramayanlar sonraki karelerde denemeyi sürdürüyor. Bu aralık API çağrısının
   tamamıdır; hepsinin başarılı GPU allocation olduğu iddia edilmez.

   Hedef/boyut/format ve gerçek allocation requirements üzerinden cache bütçesi
   önce ayrılmalı. Kaynak edinme sonucu hedef veya bütçe sürümü değişene kadar
   tutulmalı. Cache edinilemeyen job için büyük history alanları hazırlanmamalı.
   Gerçek device/OOM hataları olağan hata sözleşmesinde kalmalı; uygunluk
   yetersizliği bunlarla karıştırılmamalı.

4. **P1 — Küçük miktarda yeniden kullanılan piksel için tam hedefler kopyalanıyor.**

   `vulkan_sdf.inl:367–407`, `sdf_compare.comp:48–55` ve `sdf_restore.comp:22–24`:
   cache etkin her job, bütün hedefi snapshot'a kopyalıyor; owned piksel sayısı
   veya gerçekten faydalı reuse miktarı bu kopyayı sınırlamıyor.

   180 karede 1170 tam 1360×860 RGBA8 kopya, yani 5.473.728.000 byte kaydedildi.
   Bağımsız golden koşusunda 898.104 tile hit varken gerçekten restore edilen
   piksel 388.220 idi: bir tile hit başına ortalama 0,432 piksel.
   Boş veya owned pikseli olmayan temiz tile da hit sayılabildiği için hit oranı
   tek başına cache faydasını temsil etmiyor. Kopya sayacı kaydedilen transfer
   byte'ıdır; bunun tek başına kaç ms GPU süresi tükettiği ölçülmedi.

   Snapshot'a yalnız cache'in sahip olduğu pikseller yazılmalı. Bir seçenek,
   painter'ın dirty tile için ürettiği kesin RGBA8 sonucunu aynı aşamada saved
   görüntüsüne de yazması; temiz pikseller önceki saved verisini korur.
   Restore/painter/history bağımlılıkları ve mask geçerliliği aynen korunmalı.
   Yeni bir kırpma, öğe atlama veya kalite indirimi gerekmiyor. Fayda seçimi
   boş tile hit'ine değil gerçek saved/restored piksel ve maliyete dayanmalı.

5. **P2 — İstatistik sayacı her pikselde aynı adrese atomik yazıyor.**

   `sdf_paint.comp:48–49`, `sdf_restore.comp:22–23`:
   her qualified/restore piksel, ortak `control.words[6]` veya `[8]` sayacına
   atomik ekleme yapıyor. Cache kapalı golden koşusunda 46.467.594 compute
   pikseli, dolayısıyla aynı sayıda paint sayacı atomik eklemesi doğrulandı.
   Ownership maskesinin atomikleri de ayrıca var. Bunlar açılış ayırmalarının
   nedeni değil; devam eden GPU işinin bir parçası.

   Telemetri sayısı invocation veya workgroup içinde toplanıp global sayaca
   toplu eklenebilir. Maske optimizasyonu ayrıca ele alınmalı; hizasız hedef
   genişliği ve kısmi tile'larda komşu bitler korunmalı. Bu önerinin ms kazanımı
   henüz izole edilmedi; sayım/overflow ve golden eşitliğiyle ölçülmelidir.

**274,1678ms sıçraması hakkında kesin söylenebilen**

Önceki kabul serisinin run0/frame0 karesi 274,1678 ms idi:
begin 15,5298/build 240,8441/end 17,7939 ms. `sdf_prepare_ms=240,3677`,
10 yeni job, slot bekleme 0,1327 ms idi. O koşunun per-API kaydı olmadığı için
274 ms'nin kaç ms'sinin shader, kaç ms'sinin bellek ayırması olduğunu geriye
dönük kesin bölmek mümkün değil. Yeni tanılama, aynı hazırlık yolunda hem
pipeline derlemeyi hem çok sayıda ayrı allocation'ı doğrudan ölçüyor.
Yalnız GPU beklemesi veya yalnız shader derlemesi açıklaması yetersiz.

**Null kontroldeki küçük gerileme: doğrulandı, tekil kaynak halen açık**

| Seri | Eski Add ort.ms | Güncel Add ort.ms | Fark |
| --- | ---: | ---: | ---: |
| Önceki kabul, 8 tekrar |3,158526|3,198910|+%1,28|
| Yeni A/A kontrollü, 24 tekrar |3,297841|3,368357|+%2,14|
| Yeni aynı ayar/manifest kontrolü, 20 tekrar |3,494550|3,532674|+%1,09|

Ortalama farkın üç seride aynı yönde olması nedeniyle onu ölçüm gürültüsü
diye kapatmıyorum. Ancak şu nedensel kontroller bir tekil kök neden vermedi:

- SDL implementation ön işlemci gövdeleri karşılaştırıldı. Compute/shape sink
  ve HarfBuzz yürütümü derleme dışında. Kalan implementation farkı font_data'nın
  açık copy/move bildirimi ve kullanılmayan Vulkan painter fallback stub'ı.
- Gradient yardımcısı ve kapasite hesabı ayrı ayrı, sonra birlikte eskiye
  döndürülen özel derlemeler iyileşmeyi geri getirmedi. Bu ilk yardımcı izolasyonu
  özel link ayarları kullandığından resmi seriyle mutlak süre karşılaştırması
  yapılmadı; sonuç tek başına kesin bir compiler nedenselliği sayılmaz.
- Daha sonra resmi CMake Release seçenekleri ve SegmentHeap manifestiyle
  current/old-layout özel derlemeleri hazırlandı. Yeni Vulkan option alanlarının
  kaldırılıp eski struct yerleşiminin kullanılması Add ortalamasını 3,536194→
  3,526299 ms değiştirdi (-%0,28); farkın tamamını açıklamadı.
- Aynı resmi object/library dosyalarının yeniden linklenmesi 3,529664 ms ile
  resmi 3,532674 ms'ye yakındı. İlk farklı ayarlı özel build'in hızını bir düzeltme
  gibi sunmuyorum.
- Bütün null karşılaştırmalarında 180 karenin öğe/draw/vertex/index sayıları aynı.

Önceki tekil maksimum +%1,40 artışı ise güvenilir bir sabit oran olarak yeniden
görülmedi. 24 tekrarlı seride eski 17,0690 ms, güncel 15,4527 ms; aynı eski executable'ın
A/A etiketinde 11,6337 ms maksimum var. Eski 17,0690 ms frame127 build 17,0577 ms;
güncel 15,4527 ms frame128 build 15,4371 ms. Native compute kurulumuyla aynı olay
değiller: yoğun geometri dönemindeki CPU build süresi baskın. Hepsi CSV'de durur.
İkili/compiler yerleşimi olasılığı açık; bunu kanıtlanmış kök neden diye yazmıyorum.
Bu beş native bulgunun düzeltmesinin null farkını gidereceği de iddia edilmez.

**Önerilen sahiplik ve hazırlık düzeni**

| Sahip | Ömür | Hazırlık | Çizim sırasında |
| --- | --- | --- | --- |
| Device kernels | Cihaz | Seçili modun paylaşılan layout/pipeline'ları | Hazır handle'ı kullan |
| Frame slot arenası | Slot ve timeline | Bütçeli bloklar, geometrik kapasite, descriptor alanları | Hizalı offset al, veriyi yaz |
| Target cache | Hedef/bütçe sürümü | Kaynak uygunluğu ve snapshot/metadata rezervasyonu | Dirty/clean programını yürüt |
| Draw job | Tek epoch | Hazır slot aralıklarına referans | Komut kaydet; pipeline/bellek kurma |

İlk teslim hedefi, hazırlanmış 5000 öğelik workload'da draw kaydı sırasında
`vkAllocateMemory`, pipeline oluşturma ve descriptor pool oluşturma çağrılarının
olmaması; kapasite büyümesinin ayrıca ölçülmesi olmalı. Başlangıç, ilk sunum,
iki slot'un ilk kullanımı, Add tepe kareleri ve toplam süreç süresi birlikte
raporlanmalı. Bütçe yetersizliğinde bütün görüntü doğru kalmalı.

**Doğrudan showcase açılması**

Eski executable `build-vulkan-compute/r0/bin/pui_showcase_vulkan.exe`;
güncel executable `out/build/x64-release/Release/pui_showcase_vulkan.exe`.
İkisi de `--vulkan-device-info` ile, iki driver değişkeni boşken gerçek RX9070XT'yi
doğrudan buldu. Process/User/Machine kapsamlarında `VK_DRIVER_FILES` ve
`VK_ICD_FILENAMES` boştu. Güncel `.exe`, kendi Release klasöründen
`--selftest --frames 12 --no-script` ile Python olmadan geçti (`ok showcase`).

Python başlatıcısı, önceki bozuk sürücü kaydı için çocuk süreç ortamında mevcut
bir manifest seçebilen yardımcıdır; native executable'ın bağımlılığı değildir.
Bu oturumda eski sürücü bulma hatası yeniden üretilemedi; Windows registry veya
sürücü kurulumu değiştirilmedi. IDE'de `pui_showcase_vulkan` hedefi doğrudan
başlatılabilir. Parametresiz güncel showcase REFERENCE kullanır. Aynı SDF/Slug
görünümü için `--vulkan-painter sdf --vector-text` doğrudan executable'a verilir.

**Doğrulama**

İlk ve ayrıntılandırılmış tanılama implementation'ı bağımsız mevcut oracle'a
linklendi. SDF/compute/cache'in her biri 5000 öğelik 180 karenin tamamında hem
değişmez native golden hem bağımsız raw RGBA referansıyla eşleşti: toplam 1080
doğrulanmış kare; bütün ölçülen tepe indeksleri ve komşuları bunun içinde.
Referanslar yeniden üretilmedi.

Dört yardımcı izolasyonu ve iki layout izolasyonunun her birinde mevcut 11
showcase regresyonu, 4 büyük Spiral golden'ı ve 10 tepe golden'ı geçti. Son olarak
dağıtılan Release executable'larıyla core, Vulkan command, binning ve SDF cache
testleri yeniden çalıştırıldı; hepsi exit 0 verdi. Doğrudan showcase başlatma
kontrolü de tekrar geçti. Komutlar ve çıktılar JSON'daki `final_checks` içindedir.
Bu çalışma
üretim kodunu değiştirmediği için tam Debug/Release kabul matrisinin yeniden
geçtiği iddia edilmez. `git diff HEAD -- include src examples tests tools
CMakeLists.txt` boş; yeni dosyalar yalnız inceleme raporu/JSON/CSV'dir.
