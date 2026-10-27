# Varsayılan integrated Vulkan ve açık kurulum tutorial'ı

27 Ekim 2026. Önceki teslim: `a189807352acfee6e6617bca543d3cff52600bac`.

## Davranış

`vulkan_device_opts{}` artık `INTEGRATED`, `tile_cache=true` ve
`whole_program_cache=true` seçer. HarfBuzz açık Vulkan derlemesinde `vector_text`
da açıktır; kapalı derlemede integrated şekiller bitmap yazıyla çalışır.
`create_vulkan_device()`, `create_sdl3_vulkan_device()` ve Vulkan bootstrap'ları
bu normal seçenekleri kullanır. Showcase'e özel varsayılan makrosu kaldırıldı.
Core/SDL backend seçimi, legacy blur ve uygulamanın VSync politikası korunur.

Açık showcase tanı seçenekleri feature bayraklarını kendi bağımsız başlangıcına
getirir; bütçe, upload, blur ve validation seçeneklerini korur. Düşük seviyeli
API, çağıranın alanlarını aynen kullanır. REFERENCE isteyen bir HarfBuzz tüketicisi
`vector_text=false` ve bağımsız cache seçeneklerini de açıkça belirtir.

Bu değişiklik önceki teslimde doğrulanan painter'ın seçim politikasını taşır.
Performans ölçümleri ve kapsam sınırları [önceki raporda](perf_vulkan_integrated.md)
aynı ölçülmüş seçeneklerle kayıtlıdır; shader ve painter yürütme algoritmaları
bu geçişin parçası değildir.

## Çalışan tutorial

[Bölüm 10](tutorial/10-vulkan-without-shorthand.md) ve
[`vulkan_manual.cpp`](../examples/tutorial/vulkan_manual.cpp), SDL penceresini,
`vulkan_host` adaptörünü, cihazı, yüzeyi, UI context'ini, fontu, DPI/input
ölçeklerini, olay yönlendirmesini, idle beklemesini, VSync'i ve kapanış sırasını
açıkça kurar. Example veya application bootstrap kullanmaz.

`pui_tut_vulkan_manual --selftest --frames 8 --resize 760x520`, gerçek SDL
press/release ile sayacın bir arttığını, resize sonrasındaki client ölçülerini,
integrated seçimini ve sıfır UI/validation hatasını doğrular. Normal uygulama
VSync açık başlar; hidden-window selftest sınırsız çalışır. Bu test gerçek native
Vulkan sunumunu gerektirir, bir foreground pacing ölçümü değildir.

## Regresyon ve görüntü doğrulaması

- `pui_vulkan_default_tests`: public no-argument/aggregate/SDL factory seçimleri,
  açık diagnostic override, cache hit ve HarfBuzz residency kontrolü. Direct ve
  staged koşullarının her birinde 24 karenin tamamı uncached native SDF referansıyla
  byte düzeyinde eşittir. Sabit kareler ve 12. karede yerel değişim dahildir.
- Aynı test ve tutorial, HarfBuzz kapalı ayrı bir tüketici projesinde de geçti.
  CMake dosyası tutorial'daki normal `add_subdirectory`/`target_link_libraries`
  örneğinden üretildi; test yalnız repo/asset yollarını ve HarfBuzz seçimini uyarladı.
  Bu makinenin MSVC PDB sürüm uyuşmazlığı için configure sırasında Embedded
  debug-info seçildi; tutorial C++ kaynağı ana projeyle aynı SHA-256 değerindedir.
- Eski diagnostic fixture'ları `tests/vulkan_test_options.h` üzerinden önceki
  bağımsız başlangıç seçeneklerini açıkça seçer. Mevcut assertion'lar, karşılaştırma
  toleransları ve committed golden dosyaları değiştirilmedi. REFERENCE/cache,
  SDF/compute ve yeni varsayılan ayrı yollarda sınanır.
- Tutorial ve değişen native örneklerin README görselleri gerçek Release
  executable'larından yeniden alındı ve gözle incelendi. Mevcut golden oracle'ları
  için `--update` kullanılmadı. Tutorial kontrolündeki toplam 62 kaynak alıntısı geçti.

## Toplu kapı

Uygulama aşamaları Release ve Debug derlendi. Son kaynaklar sabitlenerek şu iki
komut tam native vaka listesi, mevcut core/pump/performance/showcase/golden
testleri, örnekler, tutorial drift, kurulu paket tüketicileri ve format kontrolüyle
başarıyla çalıştırıldı:

```powershell
python tools/run_tests.py --vulkan --format --release
python tools/run_tests.py --vulkan --format
```

| Yapılandırma | Sonuç | Süre | Native vaka |
|---|---|---|---|
| Release | bütün adımlar geçti, exit 0 | 1415,4 saniye | 94 |
| Debug | bütün adımlar geçti, exit 0 | 1304,3 saniye | 94 |

Kayıt kimliği: `full-20261027-081856-16472`. Her iki geçiş boyunca 330
kaynak/test/araç dosyasının SHA-256 değeri sabit kaldı. Kaynak manifestinin SHA-256
değeri `8dcd94513e635b2aec304acfb408b1acabe92219835284eacff6fa9489e60d69`.
[Makinece okunabilir kayıt](vulkan_default_validation.json), tam kayıt/log ve
executable hash'lerini, komutları ve ek tüketici kontrollerini içerir.

Her iki yapılandırmada 1150/5000 öğelik 180 karelik native karşılaştırmalar,
5000 öğelik iki 3600 kare/60 Add uzun koşusu, mevcut Spiral golden ve on tepe
golden karesi geçti. Core, pump, performance, preparation, mevcut software
golden'lar, bütün örnek selftest'leri, external/installed Vulkan/core/SDL
tüketicileri, API/tutorial drift ve clang-format 22.1.3 kontrolleri geçti.
Yeni tutorial ve iki varsayılan vaka aynı native kayıt üzerinden runner'a dahildir.

Yerel ham kanıtlar `build-vulkan-integrated/final-gate/full-20261027-081856-16472/`
altındaki `release.log`, `debug.log` ve `validation.json` dosyalarında korunur.
Önceden var olan dokuz ilgisiz rapor taslağının hash'leri değişmedi ve commit
kapsamına alınmadılar. Bu doğrulama Windows native/hidden/offscreen çalıştırmalarıdır;
foreground performans veya Linux/macOS doğrulaması olarak sunulmaz.
