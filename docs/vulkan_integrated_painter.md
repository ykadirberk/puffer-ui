# Tek Vulkan painter: mimari ve doğrulama

27 Ekim uygulaması: Vulkan cihazlarının ve standart `pui_showcase_vulkan.exe`'nin
varsayılanı `INTEGRATED`;
tile/whole-program reuse ve derleme açıksa HarfBuzz/Slug aynı yapılandırmadadır.
Python launcher isteğe bağlıdır. HarfBuzz kapalı derlemede bitmap yazılar kullanılır. Açıkça
verilen `--vulkan-painter` seçenekleri kendi bağımsız ayarlarıyla başlar.
Son ölçümler, ham kareler, test kayıtları ve sınırlamalar
[teslim raporunda](perf_vulkan_integrated.md) tutulur. Example shorthand olmadan
kurulum için [çalışan tutorial](tutorial/10-vulkan-without-shorthand.md) bulunur.

Normal yolun seçim sırası şöyledir:

1. Renderer semantik çağrıları kendi prepared native girdilerine dönüştürür.
   Değişen programı gözlemlemek için ikinci bir GPU tape çalıştırmaz.
2. Kesin CPU program kimliği eşleşince GPU, LOAD bağımlı bütün çıktıların sabit
   noktasını doğrular. Sertifika gelene kadar native yürütme devam eder. Başarılı
   sertifikada bütün program atlanır; yalnız sunum kaydı kalır.
3. Aynı komutlar değişen LOAD içeriği üretiyorsa GPU sertifikası bunu bildirir.
   Sonraki tamamlanmış frame sınırında fine-cache kernel'ları, blend atlası,
   bounded metadata ve tile history hazırlanır. Sıralı binning/compute/native
   replay aynı renderer içinde çalışır. Eksik kaynak veya kapsamda tam native
   sonuç korunur. Kullanıcının ayrı özellik anahtarları birleştirmesi gerekmez.

Glif hücre nesilleri görüntü kimliği değildir. Bounds, outline, scale, gamma ve
vertex tint kesin karşılaştırılır; değişen/eskimiş hücre etiketi aynı Slug
hesabına döner. CPU tape ve GPU tile karşılaştırmaları aynı ayrımı uygular.
Soğuk upload ve ilk geçerli program, sürekli animasyon gibi sayılıp 120 kare
cache'i kapatmaz. Gözlemde kullanılmayan encoder stack, glyph admission ve
compute hazırlığı native karede atlanır.

Native operand 24 bayttır; CPU RGBA8 kanallarını dört float'a genişletmez. Shader
önceki CPU byte/255 bitlerini aynı tabloyla açar. Topoloji ve primitive sırası
korunur. Renderer-owned CPU mesh ve eksiksiz native destek yolu hâlâ vardır.

`vulkan_shape_group` 1–256 rounded-rect çocuğunun birleşik signed distance'ına
tek fill/outline/glow uygular. Geçersiz/sığmayan giriş atomik olarak reddedilir.
`vulkan_alpha` ortak opacity için tek prepared RGBA8 scratch kullanır; çocukların
sıralı kuantizasyonu korunur, kapanışta unassociation ve ortak opacity uygulanır.
Tek aktif scope sınırı ve frame dışına taşma guard'ı vardır. Bunlar core/SDL
primitive alpha davranışını değiştirmez. Örnekler: `pui_ex_vulkan_groups` ve
`pui_ex_vulkan_alpha`.

Genel çıktı cache'i bilinmeyen native mesh, AA, bitmap/Slug, image ve gerçek
LOAD sonucunu saklar. Üretici rolü/nesli ve GPU dirty/overflow kanıtı uygun olduğunda
sonraki geçiş sayısal revision yerine içerik eşitliğini kullanır. External producer
ve Kawase için kanıtsız normalizasyon yapılmaz. Son glif alpha cache'i R32_UINT
içinde float bitlerini saklar; bounded key ve yalnız başarılı submit token'ı
yayınlama koşulu korunur. Fine path'in RGBA8 blend fonksiyonu, gerçek native
blending ile GPU'da kalibre edilir; alpha 8 bite yaklaştırılmaz.

`pui_vulkan_identity_tests`, native gözlem ve otomatik LOAD/fine geçişini direct
ve staged olarak zorunlu tutar. Forced tile, source/function, output, producer,
group, alpha, LOAD ve glyph-source testleri algoritmaların gerçek GPU kapsamını
ayrı sayaçlar ve bağımsız native orakllarla doğrular. Mevcut test gövdeleri ve
golden'lar değiştirilmez. Aşağıdaki bölümler önceki tasarım ve aşama kayıtlarıdır.

25 Ekim 2026. Kullanıcı mevcut showcase'i
`python tools/run_vulkan_showcase.py -- --vulkan-painter sdf --vector-text`
ile açıyor. Bu, bugün SDF raster ve Slug seçer; GPU tile cache veya compute
painter seçmez. Mevcut `adaptive`, opaque ve desteklenen büyük batch'leri
değerlendiren sınırlı bir seçicidir. Genel cached painter'ın tamamlanması değildir.

Kullanıcının güncel isteği, [Blick yaklaşımını](https://blickeditor.com/blog/caching-an-immediate-mode-ui)
tek renderer paketi olarak tamamlamaktır. Bu belge açık işlerin yürütme kaydıdır;
aşağıdaki boş kapılar geçmeden üretim tamamlandı veya CPU gerilemedi denmez.
Mevcut zorunlu renderer-owned painting sözleşmesi korunur. UI; layout, input ve
modeli yürütür; temsil, kaynak hazırlama, cache ve yürütme renderer'a aittir.

## Teslim sözleşmesi

- Kullanıcı komut akışı, binning, compute, cache ve yazı için ayrı seçenekler
  birleştirmeyecek. Standart Vulkan showcase hedefi tek üretim yolunu açacak.
  Eski reference/SDF/forced-compute seçenekleri bağımsız doğrulama için kalacak.
- HarfBuzz şekillendirme, Slug outline sahipliği ve glyph coverage tekrar kullanımı
  aynı yürütmenin parçaları olacak. Core/SDL/null varyantları bağımsız kalacak.
- Mevcut SDF/Slug görüntüsü korunacak. Eski testler ve immutable golden'lar
  değişmeyecek. Yeni grup özellikleri mevcut primitive alpha davranışını değiştirmeyecek.
- Hazırlama ve kapasite büyümesi çizim çağrısının dışında olacak. Sınır aşımı
  eksiksiz ve sıralı yürütmeye dönecek; komut veya görünür öğe düşürülmeyecek.
- Gerçek 5000 Spiral'in hareketli, sabit ve yerel değişim serileri; açılış,
  bütün spike'lar, null kontrolü, CPU zamanı/cycle, GPU timestamp, 3D/compute,
  dedicated/shared ve odak bilgileri birlikte tutulacak.

## 25 Ekim başlangıcında kaynakla fark

| Parça | Bugünkü durum | Kapanacak iş |
| --- | --- | --- |
| Renderer-owned semantik çağrılar | Uygulandı; her backend bütün işlemleri sağlıyor | Yeni normal painter aynı sınırı kullanacak |
| Kompakt komutlar ve GPU coarse/fine listeler | Uygulandı | Texture, glyph ve bütün operation bağımlılıkları aynı programda |
| Tile cache | Qualified opaque suffix pikselleri | AA, yazı, texture ve pass bağımlılıklarını kapsayan genel çıktı cache'i |
| Whole-program skip | CPU canonical identity + GPU fixed-point certificate | Normal üretim yolunun parçası; SDF destek yolu da dahil |
| Compute boyama | Sonlu RGBA8 suffix; kalan pikselde tam raster replay | Genel komut yorumlayıcısı, açık kapsam ve sayısal uyumluluk kapıları |
| Şekil grupları | Opcode alanında gelecek iş olarak bırakılmış | Birleşik mesafeye ortak fill/outline/glow |
| Ortak opacity | Yerel alpha barrier uygulanmamış | Piksel içi ayrı accumulation, sonda ortak opacity; nesting guard |
| Özel efektler | Blur/copy ayrı native olaylar | İçerik revizyonlu üretici geçişleri ve doğru prefix bağımlılıkları |
| Vektör yazı | Shape ve outline cache var; Slug coverage tekrar hesaplanıyor | Kesin anahtarlı, bütçeli GPU coverage cache |
| Üretim seçimi | Kullanıcı ayrı modları seçiyor | Tek paket; iç maliyet seçimi ve bağımsız tanılama yolları |

## Yürütme sırası

### I0: Değişmez ara kayıt

Şu anki dört mimari düzeltme ve önceki ikinci aşama değişiklikleri birlikte
korunuyor. `build-vulkan-program/deferred-prefix/` kaynak/executable hash'leri,
altı dönüşümlü 180-frame serileri ve 6480 değişmez RGBA karşılaştırması içeriyor.
15 program regresyonu ve tam Debug/Release suite'leri bu kaynakta geçti;
`build-vulkan-program/phase3-validated-source.zip` sürümü korur. Sonraki SDF
reuse bütünleştirmesi 16 program regresyonuyla ayrıca doğrulanmaya başladı.
Bu ara kayıt genel painter'ın tamamlandığı anlamına gelmez.

Sınırsız FPS serisinde ana kare süresi azalmasına rağmen process CPU/cycle
artışları var; bunlar kabul dışına çıkarılmaz. Normal üretim ve eşit sunum hızı
karşılaştırması bu farkı da açıklamalı veya gidermelidir. İki adaptörün PDH
sayaçları tek ortalamaya karıştırılmaz. Görünürlük tek başına foreground kanıtı değildir.

### I1: Tek üretim konfigürasyonu

Normal painter; shaping, outline/coverage residency, semantik komutlar, GPU
listeleri, genel cache ve yürütme politikasını birlikte kurar. Constructor ve
frame-boundary preparation kapasiteleri hazırlar; draw çağrısı storage büyütmez.
Diagnostic raster ve force-compute aynı input programını bağımsız yürütebilir.
İlk teslimde library default'u korunmuştu; takip eden varsayılan geçişi,
doğrulanan INTEGRATED yapılandırmasını bütün Vulkan cihaz fabrikalarına taşır.

### I2: Renderer programı ve genel tile cache

Clear/draw/copy/effect işlemleri seal edilmiş programlara dönüştürülür. Target,
sample-before-write, update, external release, readback ve present sınırları
korunur. Her üretici erişim version'ı taşır; atlas hücresinin varlığı içeriğinin
güncel olduğunu göstermez. GPU listeleri sıralı kalır; hash yalnız hızlı filtredir.

Temiz tile'ı her karede clear etmek yasaktır. Clear ve copy de GPU'nun dirty
kararını tüketir. Bir önceki çıktının LOAD bağımlılığı, opaque tam overwrite ve
gerçek GPU fixed-point durumu ayrı izlenir. Blur, final cached frame'den değil,
orijinal sıradaki prefix'ten beslenir. Aynı pikselde compute ve raster iki kez
blend yapamaz. Genel cache, destek yolunda kalan AA/yazı piksellerini de saklar.

### I3: Sayısal uyumluluk ve compute kapsamı

Mevcut RGBA8 attachment her fragment'te kuantize olur. Bütün katmanları FP32'de
front-to-back biriktirmek aynı resmi garanti etmez. Örtülme araması ile renk
çözümleme ayrılır; mevcut sıralı kuantizasyon korunur. Sonlu blend tablosu yalnız
kanıtlanmış girdilerde kullanılabilir. Fractional coverage'ı 8 bite yuvarlamak,
gölgeleri veya glyph coverage'ı değiştirmek kabul edilmez.

Yeni compute kapsamı; analitik şekiller, gerçek Slug coverage, resimler ve
üretici sonuçlarını aynı programda değerlendirir. Native shader ile coverage ve
blend byte oraklları ayrı sınanır. Destek yolu gerektiğinde sırayı ve pikseli
korur; kapsam sayaçları bir raster cache'i genel compute painter diye gizleyemez.
GPU allocation, descriptor ve feature gereksinimleri borrowed-device sözleşmesine
açıkça eklenir; sessizce etkinleşmemiş feature kullanılmaz.

İlk sayısal kapı bağımsız GPU oraklıyla doğrulandı: `pui_vulkan_source_tests`,
değişmemiş native SDF fragment modülünü blend kapalı F32 attachment'a yürütüyor;
ayrı compute pipeline aynı shape/outline verisinden kaynak rengi üretiyor.
HarfBuzz açıkken 562 fixture'daki 9.207.808 float bileşeni direct ve staged
yollarda bit eşit. Renk kanallarının 256 değeri, gradient, beş gerçek glyph,
üç ölçek/gamma ve altı örnekleme fazı var. Negatif kontrol her alpha bileşeninin
bir bitini değiştiriyor; oraklın 2.301.952 değişimi yakalaması zorunlu.
HarfBuzz kapalı shape/gradient varyantı ayrıca doğrulanıyor. Önceden bulunan
20 shader'ın SPIR-V/hash'leri değişmedi. F32 target'ı desteklemeyen adaptörde
test sıfır karşılaştırmayla açıkça unavailable bildirir.

Bu kaynak-float eşitliği, mevcut compute painter'ın glyph/AA desteklediği veya
native RGBA8 blending'in çözüldüğü anlamına gelmez. Sonuçların native blending
fonksiyon atlası ve gerçek RGBA8/all-frame golden ile doğrulanması hâlâ gerekli.
[Sayısal kayıt](perf/vulkan_source_float_oracle.json).

26 Ekim ara kaydı: `INTEGRATED` artık ayrı native float-fonksiyon cache'i ve
analitik AA/Slug yorumlayıcısıyla çalışıyor; standart showcase seçimi henüz bu
moda alınmadı. Cache'i GPU request listesi, bounded set admission, indirect reset
dispatch ve indirect native calibration draw yönetiyor. CPU resident anahtarları
okumuyor. Direct/staged 262.144-byte native blend oraklı exact; kasıtlı yanlış
blend state'i 161.718 farkla reddediliyor. İstek/capacity sınırları, epoch wrap,
küçük bütçe, native binding dönüşü ve unsubmitted teardown ayrıca sınanıyor.
Borrowed device için vertex-store feature istemeyen readonly bildirim kullanılıyor.

Gerçek Spiral'in altı 180-frame Release vakası geçti: 5000 direct/cache/staged,
1150 staged+cache, bitmap text ve bütçeden dolayı tam native destek yolu. Vector
text açık beş vakanın bütün 900 karesi mevcut immutable golden'larla da eşit.
GPU sayaçları gerçek AA/glyph/float boyamayı gösteriyor; draw sırasında native
allocation/resource sayaçları sıfır. Ek byte-tint vakaları CPU byte/255 dönüşümünü
shader'da bit eşit doğruluyor. Önceki 20 production shader'ın SPIR-V'leri aynı
kaldı. Bu focused Release ara kaydı tam suite/CPU kabulünün yerine geçmez.
Blend function miss sayısı yüksek; genel native çıktı cache'i ve kaynak residency
işi sürüyor, hızlanma iddiası yok.
[Kaynak, tam sonuç ve sınırlar](perf/vulkan_integrated_float_stage.json).

Genel float kaynak için araştırılacak kesin destek, GPU'da native blend fonksiyon
cache'idir: aynı float RGBA kaynak ve blend state için 256 olası destination
kanalının native sonucu GPU atlasında tutulur. Compute her kanalın gerçek byte
değerini bu fonksiyondan çözer; kaynak alpha'yı 8 bite yaklaştırmaz. Fonksiyonlar
GPU request listesi ve indirect calibration draw ile hazırlanır. Önerilen ilk
4096 satırın RGBA8 ham atlas maliyeti 4 MiB'dir; gerçek allocation/bütçe ayrıca
ölçülür. Slot/row generation, producer barrier ve tüm kanal kombinasyonları
doğrulanmadan bu yöntem etkinleştirilmez. Mevcut 8bit blend tablosu daha geniş
float alanının kanıtı değildir. Bir cache miss'te bu kare eksiksiz native sonuç
alır; hazır fonksiyon sonraki işte kullanılabilir. Anahtar/kapasite yetersizliği
hiçbir öğenin veya rengin düşürülmesine izin vermez.

### I4: Şekil grupları ve alpha barrier

Grup başlangıç/bitişi etkilenen bütün tile listelerinde dengeli bulunur. Shape
grubunda geometri mesafeleri birleştirilir; fill ve outline sonda uygulanır.
Glow/outline kapsamı binning bounds'ına eklenir. Yeni özellik eski per-primitive
alpha veya disabled-scope semantiğini grup opacity'si olarak yeniden yorumlamaz.

Alpha barrier ayrı piksel sonucunu toplar ve ortak opacity'yi grubun sonunda
uygular. Tek aktif barrier sınırı açık guard ve regresyonla korunur. Normal GPU
yolunda bunun için bir popup başına offscreen image ayrılmaz. Bağımsız raster
oracle gerektiğinde önceden hazırlanmış scratch kullanabilir.

### I5: Yazı ve efekt residency

Glyph coverage anahtarı font/outline generation, scale, gamma ve kesin örnekleme
fazını içerir. Integer translation eşitliği varsayılmaz; GPU corpus ile kanıtlanır.
Coverage float değerini düşürmek veya tekrar filtrelemek yasaktır. Bounded cache
hücresi in-flight kullanıcısı bitmeden değiştirilemez. Yeni glyph işi seal edilmiş
programdan önce bitirilir; glyph/atlas revision ilgili tile anahtarına girer.

Blur ve özel efekt üreticileri input bölgesi, sampling footprint ve parametre
bağımlılıklarıyla güncellik bildirir. Aynı allocation'ın yeniden kullanılması
content identity sayılmaz. Legacy blur değişmez; Kawase ayrı opt-in alanıdır.

### I6: Tek teslim ve kabul

**Birleştirme aşaması, 26 Ekim:** Aşağıdaki ekler kaynakta bulunuyor; bu
aşamanın derlemesi ve son toplu doğrulaması tamamlanmadan doğrulanmış özellik
veya performans kazancı olarak kabul edilmeyecek:

- INTEGRATED native yolu, 48 baytlık genişletilmiş vertex yerine 24 baytlık
  konum/UV/RGBA8/shape girdisini GPU'da açıyor. Orijinal indeksler, topoloji ve
  projeksiyon aritmetiği korunuyor. Core tarafındaki renderer-owned mesh hazırlığı
  bütünüyle kaldırılmış sayılmaz.
- Son glif alpha değeri R32_UINT hücrelerde gerçek float bitleriyle saklanıyor.
  Anahtar kesin fiziksel konum, outline adresi, scale, gamma ve etkili alpha'yı
  içeriyor. Sabit 128 girdiye kadar tablo ve slot başına hazırlanan request page
  kullanılıyor. Generation uyuşmazlığı orijinal Slug hesabına dönüyor. Bir girdinin
  hazır durumu, sadece onu dolduran recording token başarıyla gönderilince yayınlanıyor.
- Native çıktı üreticisinin güncel GPU dirty/overflow kontrolü sonraki geçişe
  GPU kopyasıyla taşınıyor. Üretici rolü ve kaynak nesli eşleşip dirty/overflow
  sıfır olduğunda tüketici, değişen sayısal revision yerine bu içerik kanıtını
  kullanabiliyor. Güncelleme ve sertifikasız üretici literal revision kullanıyor.
  Kawase geçişleri henüz bu içerik sertifikasını üretmiyor; konservatif kalıyor.
- Mesafe grubu, bitişik çocuk shape kayıtlarını izleyen tek bir düğüm olarak
  saklanıyor. Fill/outline/glow birleşik mesafeye bir kez uygulanıyor. Tile anahtarı
  yalnız dış quad'ı değil çocukların kesin kayıtlarını da karşılaştırıyor.
- Alpha barrier'ın bağımsız native destek yolu, yüzey hazırlanırken ayrılan tek
  scratch üzerinde çocukların gerçek RGBA8 sırasını koruyor; kapanışta ortak
  opacity uygulanıyor. Nesting ve frame dışına taşan scope guard'ları var.
  Compute yorumlayıcısı da gerçek RGBA8 LOAD'dan başlayıp piksel içinde her
  katmandan sonra native blend fonksiyonunun byte sonucunu kullanıyor. Son
  opacity geçişi, native shader ile aynı texelFetch ve unassociation aritmetiğini
  kullanıyor. Scratch bir kez yüzey hazırlığında ayrılıyor; popup başına kaynak
  yaratılmıyor. Blur çocukları doğru yerel prefix'i kullanıyor. Bu aşamanın GPU
  eşitlik testleri ve gerçek compute kapsam sayaçları son kapıda doğrulandı.
- Sürekli değişen programlarda ikinci GPU tape'in hazırlanması atlanıyor;
  sertifikalanmış sabit programlar CPU karşılaştırmasını koruyor. Bu politika,
  `--vulkan-painter integrated` altında bütün parçaları bir araya getiriyor.
  Son doğrulamayla standart showcase varsayılanı bu yapılandırmaya alındı.

`pui_vulkan_glyph_source_tests`, `pui_vulkan_producer_tests`,
`pui_vulkan_group_tests`, `pui_vulkan_alpha_tests` ve `pui_vulkan_load_tests`
bu eklerin regresyonlarıdır. LOAD regresyonunda her karede değişen destination
üzerinde yalnız yarı saydam çizimler var; gerçek float blend işi ve native
vertex çağrılarının GPU'da atlanması ayrı sayaçlarla zorunlu tutuluyor. Glyph
regresyonu CPU anahtar isabetinin yanında gerçek GPU kaynak isabetini de istiyor.
Testler son kaynakla toplu Release/Debug doğrulamasında geçti.
Her uygulama aşaması derlenebilir bırakılır; tam suite ve benchmark her adımda
tekrarlanmaz. Önceki aşamanın yeşil sonucu yeni kaynak için kanıt sayılmaz.

- [x] Tek normal showcase yolu; Python launcher zorunluluğu yok.
- [x] Genel çıktı cache'i; AA, bitmap/Slug text, image ve effect revision kapsamı.
- [x] Compute kapsamı gerçek sayaçlarla doğrulanmış; tam native destek yolu korunmuş.
- [x] Şekil grupları ve alpha barrier; order/bounds/guard regresyonları.
- [x] 5000 Spiral önce/sonra; bütün 180 kare, tepe birliği ve komşuları exact.
- [x] Sabit/yerel değişim, 1150, staged/direct, resize/DPI/gamma/overlay,
  external image, OOM/kapasite/wrap, 3600-frame devam testleri.
- [x] Mevcut Debug/Release suite'leri ve golden'lar değiştirilmeden yeşil.
- [x] Ölçülen draw allocation/resource sayaçları sıfır; altı kohortta ana thread
  cycle azaldı. Cold heap, process CPU, p99/maximum ve ek bellek maliyetleri
  [raporda](perf_vulkan_integrated.md) ayrıca korundu; evrensel hızlanma iddiası yok.
- [x] Dokümantasyon ve eksiksiz ham ölçümler teslim commit'inde; roadmap revizyon
  kaydı commit kimliğiyle güncellenir.

Tanılama seçenekleri bağımsız referanslardır. Standart showcase, bu parçaları
tek INTEGRATED yapılandırmasıyla açar ve yürütme yolunu renderer seçer.
