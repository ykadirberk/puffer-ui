# Vulkan default runtime: font identity and duplicate Slug work

Bu inceleme, Visual Studio Release/x64 üzerinden açılan varsayılan showcase'te
küçülen/farklılaşan yazıları ve compute varsayılanının GPU/FPS gerilemesini
birbirinden bağımsız iki sorun olarak doğrular. Baseline `79746c8`'in gerçek
compute-first sürümüdür. Yeni varsayılan da INTEGRATED, compute-first,
HarfBuzz/Slug, tile cache ve whole-program cache kullanır.

## Fontun çalışma dizinine bağlı değişmesi

Örnek executable'ın `PUFFERUI_ASSET_DIR` tanımı, önceden derlenmiş Vulkan
kütüphanesine aktarılmıyordu. `example_app::boot` bu kökü açıkça vermediği için
proje kökünde `assets/fonts/DejaVuSans.ttf`, Visual Studio'nun executable çalışma
dizininde ise Windows Segoe UI fallback'i seçiliyordu. HarfBuzz/Slug iki durumda
da etkin olmasına rağmen metrikler ve harf biçimleri farklıydı.

Aynı binary ve aynı 16 karelik, HUD kapalı Overview sahnesiyle önce:

- Proje kökü BMP SHA-256: `005789d507ac4d8cca552ce38629e8a432c53bb3693b936c49de948abe4592dc`.
- Release çalışma dizini BMP SHA-256: `791dd650730b011f2cbf22450c768c5ab24a2c728b4c475317395cfda93331c0`.
- Slug upload'ları sırasıyla 201152 ve 203160 byte idi; iki görüntü farklıydı.

`example_app` artık kendi asset kökünü `sdl3_app::asset_dir` üzerinden geçirir.
Düzeltmeden sonra iki çalışma dizini de ilk DejaVu BMP hash'ini ve 201152 byte
Slug upload'ını üretir. Yazı boyutu, gamma, outline algoritması veya tema ölçüsü
değiştirilmez. Açık runtime asset kökü hâlâ kullanılabilir.

Bundled font bulunamazsa Windows Segoe UI/Arial yolları korunur. Linux için
`/usr/share/fonts` ve `/usr/local/share/fonts` altındaki DejaVu/Liberation
konumları eklenmiştir. Arama yalnız başlangıçta yapılır. Bu rapor Windows
çalıştırmalarını doğrular; Linux runtime bu makinede çalıştırılmamıştır.

## GPU maliyetinin kaynağı ve değişiklik

Compute-first aynı değişen programın compute yorumlamasını ve compute'in
sahiplenemediği pikseller için native replay'i çalıştırır. Cache bir kareyi
yeniden kullanabiliyorsa kazanç vardır; sürekli animasyon/HUD değişimi bu
eşitliği sık sık bozar. Native RGBA8 ara yuvarlamasını koruyan function lookup
ve admission da ek maliyet getirir. Bu nedenle sürekli çizilen native SDF
kontrolünden otomatik olarak daha hızlı olması beklenemez.

Eski interpreter, cache dışındaki Slug glyph'lerini compute içinde tekrar
çözüyordu; native replay de kendi Slug coverage'ını hesaplıyordu. Bu ayrıca
eğri çözücüsünü bütün interpreter kernel'inin içine taşıyordu. Yeni interpreter
yalnız generation, tint ve koordinatları doğrulanmış source-alpha cache
texellerini kullanır. Cache miss, büyük glyph veya uyuşmayan bir kayıt compute
sahipliğini iptal eder; mevcut native Slug sonucu korunur. Daha sonraki tam
opak bir kaynak sahipliği yeniden kurabilir. R32_UINT cache'in float alpha
bitleri, native/producer Slug kodu, sıralama ve RGBA8 blend aritmetiği aynıdır.
Cache kapasitesi ve GPU allocation bütçesi büyütülmez; draw tahsisi eklenmez.

Tanısal profiler, actual 5000-Spiral/180 karede şu GPU aşamalarını ölçtü.
Ek timestamp/heap takibi zamanlamayı etkilediği için bu ayrı bir neden-atıf
kontrolüdür; aşağıdaki normal benchmark'ın yerine geçmez:

| Aşama, ms/kare | Önce | Sonra |
| --- | ---: | ---: |
| Interpreter paint | 1.925221 | 1.358754 |
| Function prepare | 0.194955 | 0.203719 |
| Native graphics | 0.087978 | 0.101725 |

Paint maliyeti yaklaşık %29,4 azalır; bütün aşamalar iyileşmiş gibi sunulmaz.
RGBA8 native function lookup/admission ve genel replay maliyeti devam eder.

## Önce/sonra normal ölçüm

Aynı MSVC Release/LTCG seçenekleri ve AMD Radeon RX 9070 XT, driver 8389024.
Binary'ler ölçümden önce dondurulmuştur. Her workload altı önce/sonra çiftidir;
sıra dönüşümlüdür. Derleme, test veya başka benchmark aynı anda çalıştırılmadı.
5000-Spiral 180 fixed-dt kare ve 90/120/150'de üç gerçek keyboard Add içerir.
Moving, idle ve local-change; production/fine; direct/staged ayrı tutulur:
**25920 zamanlanmış kare**. Başlangıç ve bütün aykırı kareler dahildir.

Ek Overview/HUD kontrolü altı çift × 120 kare ve ayrı native SDF/Slug kontrolü
ile **2880 kare** tutar. GPU süresi tamamlanmış, gecikmeli submission
timestamp toplamının kare sayısına bölümüdür; CPU wall time ile toplanmaz.
Pencereler gizli ve FPS sınırsızdır; bu ölçüm foreground FPS sertifikası değildir.

| Sahne/yol | Wall ortalama önce → sonra, ms | GPU önce → sonra, ms/kare | p99 önce → sonra, ms | Maks. önce → sonra, ms |
| --- | ---: | ---: | ---: | ---: |
| Overview/HUD default | 3.1973 → 2.3745 | 2.3455 → 1.5824 | 6.5484 → 5.1917 | 32.4714 → 35.0008 |
| Overview/HUD native SDF kontrolü | 1.5933 → 1.5804 | 0.8507 → 0.8464 | 3.8586 → 3.9743 | 31.9447 → 31.9806 |
| Spiral production/direct/moving | 4.2688 → 4.1646 | 3.0480 → 2.5287 | 8.1913 → 9.9886 | 39.2841 → 40.8285 |
| Spiral production/direct/idle | 3.4935 → 3.4986 | 0.1174 → 0.0773 | 6.5354 → 5.2569 | 44.6962 → 41.5930 |
| Spiral production/direct/local | 3.6455 → 3.6934 | 0.3845 → 0.2331 | 6.8516 → 7.5328 | 41.7044 → 49.4796 |
| Spiral production/staged/moving | 4.2607 → 4.0579 | 3.0136 → 2.5554 | 7.5817 → 7.9181 | 44.2843 → 38.6851 |
| Spiral production/staged/idle | 3.8050 → 3.6498 | 0.1229 → 0.0780 | 9.6057 → 7.6764 | 43.7386 → 77.1946 |
| Spiral production/staged/local | 3.6330 → 3.5559 | 0.3806 → 0.2322 | 6.9174 → 6.3855 | 51.1514 → 50.9324 |

Fine kontrolünün bütün kohortları ve ham kareleri makine kaydına dahildir.
Overview default GPU maliyeti %32,5 ve wall ortalaması %25,7 azalır.
Moving Spiral GPU maliyeti direct %17,0, staged %15,2 azalır. Frametime p99 ve
maksimumlar her workload'da iyileşmez. Özellikle staged/idle/after tekrar 5,
kare 0'daki **77.1946 ms** korunmuştur: begin 55.5886, build 12.8397, end
8.7663 ms. Bu örneğin 0.05252 ms completed-GPU değeri önceki tamamlanmış
submission'a aittir; onu bu karenin GPU süresi diye yorumlamak doğru olmaz.
Bu kayıt tek başına begin aşamasındaki artışın alt nedenini belirlemez.

Native SDF kontrolü hâlâ default'tan daha hızlıdır. Bu değişiklik GPU tekrar
işini azaltır; önceki native FPS düzeyinin tamamen geri kazanıldığı veya tüm
frametime sıçramalarının çözüldüğü iddia edilmez. Default native observation'a
geri çevrilmemiştir.

Aynı SDF/Slug görüntüsünde yeni default, doğrudan native kontrolünden hâlâ
ortalama wall süresinde yaklaşık **%50,2**, GPU süresinde **%87,0** pahalıdır.
Dolayısıyla bu düzeltme yeterli bir performans sonucu olarak kabul edilmez.
Bu tablodaki kontrol `SDF_RASTER + vector_text` yoludur. Eski REFERENCE
triangle/bitmap yolunun bu kohortta ayrı ölçümü yoktur; iki yolun font/coverage
farkları göz ardı edilerek aynı görüntüymüş gibi bir hız oranı üretilmez.

Kalan maliyetin büyük bölümü yorumlayıcıdadır: tanısal GPU kaydında izlenen
draw/dispatch aşamalarının süre toplamında paint yaklaşık %72 pay alır.
Daha büyük kazanç için piksel başına
yorumlama/lookup işini ve bu işin gerçekten ortadan kaldırdığı native çizim
miktarını birlikte ölçmek gerekir. Native şekil ve glyph shader'larının
uzmanlaştırılması da SDF ile eski mesh yolu arasındaki fark için ayrı bir
ölçüm adayıdır. Bunlar bu commit'te uygulanmış veya hız kazancı kanıtlanmış
değişiklikler olarak sunulmaz.

## Windows 3D ve bellek sayaçları

İki binary de aynı INTEGRATED flags ile 180 kare, native VSync + aynı 60 Hz
harness limiti altında çalıştırılır. Moving ve idle için üç dönüşümlü çift,
toplam **2160 ayrı paced kare**. PDH yalnız kendi PID'sinin verilerini saklar;
adaptör LUID'leri/fiziksel engine'ler birleştirilmez. Sandbox içinde PDH sayaç
açılışı `c0000bc8` döndüğü için yalnız okuma yapan probe yükseltilmiş izinle
tekrarlandı; sistem veya driver ayarı değiştirilmedi.

Ana adaptör `luid_0x00000000_0x000159B8_phys_0`, 3D engine 0 için moving
ortalaması **%18.9598 → %13.6509**, maksimum **%24.2524 → %18.0812**.
Her tarafta 19 geçerli engine örneği vardır. Idle ortalama **%0.6130 → %0.5323**
(18'er örnek). İkinci adaptörün 3D kullanımı sıfırdır ve ayrı saklanmıştır.
Moving tepe dedicated memory iki tarafta **244887552 byte / 233.54 MiB**;
idle tepe **255905792 byte / 244.05 MiB**. VRAM düşüşü iddia edilmez.
Ortalama residency değişken başlangıç örneklerini de içerir; ham örnekler,
geçersiz status'lar ve bütün frame visibility/focus bayrakları saklanır.
Pencereler visible olsa da focused kare sayısı sıfırdır; bunlar foreground
pacing/FPS kanıtı değildir.

## Görüntü ve regresyonlar

Önceden dondurulmuş 1080 native RGBA kare, önceki committed hash haritasına
karşı hem önce hem sonra kontrol edildi; hiçbir dosyanın üzerine yazılmadı.
Baseline, candidate production ve candidate fine kontrolü; direct/staged ×
moving/idle/local için bu aynı referanslara karşı **3240 tam-byte karşılaştırma**
yaptı ve hepsi eşit. Her 180 kare karşılaştırıldığı için ölçülen bütün top-10
tepe ve komşuları kapsanır. Tepe indeksleri ayrıca kayıtta tutulur.

Yeni `pui_vulkan_example_font_tests`, gerçek örnek bootstrap'ını proje kökü,
boş çalışma dizini ve açık asset köküyle çalıştırır: 12 tam RGBA kare, ayrı
bundled-font ölçü oraklı, bold/alpha ve Slug upload kontrolü.
Yeni `pui_vulkan_painter_work_tests`, 31/33/65/257 piksel genişlikli hedeflerde
192 karede native SDF/Slug oraklına eşitliği kontrol eder: tekrarlanan cache
kayıtları, fractional position churn, cache'e sığmayan 112px glyph'ler,
gamma/disabled alpha, bilinmeyen üçgenler ve kenarlar. Direct ve staged vakaları
vardır. Yeni private kernel regresyonu derlenmiş SPIR-V'de interpreter'ın glyph
alpha binding'ini içerdiğini, outline binding'ini içermediğini, native/producer
Slug binding'lerinin ise kaldığını doğrular. Böylece Slug'ı tamamen kapatmak
pozitif kontrolü geçiremez. Eski test gövdeleri ve golden beklentileri aynıdır.

## Tam kabul ve teslim

Tam Release ve Debug `--vulkan --format` geçişleri başarılıdır: her
yapılandırmada **103 native Vulkan vakası**, mevcut core/pump/performans
regresyonları, 22 SDL golden sahnesi, 1150/5000 Spiral ve tepe kareleri, uzun
3600-kare devamları, bütün örnekler, installed core/SDL/Vulkan tüketicileri ve
format/API/tutorial kontrolleri geçmiştir. Atlanan adım yoktur. Release
1495,47 s, Debug 1134,24 s sürmüştür. Her iki geçiş boyunca **339 kaynak
dosyasının hash'i** aynı kalmıştır; son paketlemede tekrar denetlenmiştir.

Standart Release executable'ı Visual Studio x64 Release hedefinden Ctrl+F5 ile
açılmış, Overview, `vulkan showcase` etiketi ve fontlar canlı olarak görülmüştür.
Kalan `pui_demo` canlı smoke kontrolünde iki pencere de ayrıca açılmış ve
gözle denetlenmiştir. Son pencere envanterinde bu test pencereleri kalmamıştır.
Bu gözlem bir foreground FPS ölçümü değildir.

Son standart executable proje kökü ve Release çalışma dizininden yine aynı
DejaVu BMP hash'ini üretir; her iki kayıtta 201152 byte Slug upload'ı vardır.
`default_execution=compute`, `default_vector_text=1` ve gerçek pozitif GPU SDF/
glyph sayaçları doğrulanır. Ölçüm binary'siyle son derlenen benchmark'ın CPU
`.text` byte'ları ve ilgili dört GPU shader modülü birebirdir; debug/build
kimlikleri farklı olduğu için bütün executable SHA'ları ayrı saklanır.

[Makine kaydı](perf/vulkan_default_runtime_final.json),
[28800 zamanlanmış karenin tamamı](perf/vulkan_default_runtime_frames.csv.gz) ve
[ham CSV/log, test kayıtları ve harness arşivi](perf/vulkan_default_runtime_raw.zip)
önceki referansları değiştirmeden saklanır. Ayrı 2160 paced karenin ham Windows
sayaçları arşivdedir. Dokuz ilgisiz rapor taslağının hash'leri önceki kayıtla
aynıdır; bu taslaklar commit'e dahil edilmez.

Ham veriler yerel `build-vulkan-default-repro/` altında dondurulmuştur.
Bu çalışma dokuz ilgisiz rapor taslağını değiştirmez. Arithmetic certification,
opak prefix ön taraması, lokal request filtresi, farklı workgroup/route
denemelerinin kazançları yeterli görülmedi ve production değişikliğine alınmadı;
deneme kayıtları korunmuştur.
