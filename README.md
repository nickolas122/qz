<img src="icons/qz-lite/qz-lite-512.png" alt="" width="104" align="right">

# QZ-lite

*A build of QZ, narrowed to one rider, one trainer and one PC. The name and the mark are [documented here](docs/fork/IDENTITY.md); the project they belong to is below.*

> ### This is a fork. The real project is [cagnulein/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift).
>
> QZ is written and maintained by **[Roberto Viola (cagnulein)](https://github.com/cagnulein)**
> and its contributors. Every feature this repository is useful for is his work; what is
> here is a handful of changes on top, made for one rider with one trainer.
>
> This build is **narrowed on purpose**: 18 of upstream's 132 device drivers remain, and
> only Windows and Android are built. If your machine is not one of the few still
> supported — or you just want QZ — **[go to upstream](https://github.com/cagnulein/qdomyos-zwift)**
> or [qzfitness.com](https://www.qzfitness.com/).
>
> **[What this fork changes, and why →](FORK.md)** · [Releases](../../releases)
>
> #### Support the author, not this fork
>
> This fork takes no money and asks for none. If QZ is worth something to you, it is his
> work to support:
>
> - **[Patreon](https://www.patreon.com/cagnulein)**
> - **[Buy Me a Coffee](https://www.buymeacoffee.com/cagnulein)**
> - **[IssueHunt](https://issuehunt.io/r/cagnulein)**
> - Buy the app: **[Google Play](https://play.google.com/store/apps/details?id=org.cagnulen.qdomyoszwift)** · **[App Store](https://apps.apple.com/app/id1543684531)**

QZ is a bridge between an exercise machine and the training app you ride with. It is not
affiliated with or endorsed by any subscription service or maker of exercise equipment.

### Features

# UI Features

|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|Tiles Customization|X|X|X|X|Order and visibility of each tile|
|Profiles|X|X|X|X|Different user or different fitness device profiles|
|UI Zoom Customization|X|X|X|X||

# Peloton Features

|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|Bike metrics on the peloton app|X||X|||
|Power zone with auto resistance|X|||||
|Peloton real-time resistance conversion|X||X||with the possibility to customize it|
|Peloton real-time auto-resistance|X||X||with the possibility to customize it|
|Peloton auto speed and auto inclination||X|X||with the possibility to customize it|

# Heart Rate Features

|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|Heart Rate support|X|X|X|X|Apple Watch, ANT+ devices and Bluetooth devices|
|Heart Rate Zones Customizations|X|X|X|X||
|Ability to calculate Wattage from HR and Cadence|X||||for the bikes that doesn't have a power sensor|

# 3rd Apps Compatibility

|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|Zwift Compatibility|X|X|X|X||
|Zwift Auto resistance|X||X|||
|Zwift Auto inclination and speed||X|X||https://www.youtube.com/watch?v=KTQ2n7yeDbo|
|Wahoo RGT Compatibility|X|X|X|X||
|VzFit Compatibility|X|X|X|X||
|Rouvy Compatibility|X|X|X|X||
|IFIT app Compatibility|X|||||
|Echelon app Compatibility|X|||||
|Wahoo Dircon Compatibility|X|X|X|X|in order to send data to Zwift or RGT with Wifi only!|
|One device only support for Zwift and Wahoo RGT|X|X|X|X|using Wahoo Dircon https://www.youtube.com/watch?v=gYYUXNWFAok|
|BitGym Compatibility|X|X|X|X||

# Training Program
|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|Builtin video support (Kinomap like)|X|X|X|X|Files could be local or on the cloud!|
|GPX auto following|X|X|X|X||
|2D/3D maps for GPX|X|X|X|X||
|ZWO (Zwift workout file) compatibility|X|X|X|X||
|XML Workout file compatibility|X|X|X|X||
|Auto follow workout based on your heart rate|X|X|X|X||
|Random workout|X|X|X|X||


# Statistics 

|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|E-Mail report|X|X|X|X|at the end of the workout|
|Strava integration|X|X|X|X|press stop at the end of the workout to auto upload it|

# Misc

|Feature|Bike|Treadmill|Elliptical|Rower|Notes|
|:---|:---:|:---:|:---:|:---:|---:|
|Resistance shifting with bluetooth remote|X||X|||
|TTS support|X|X|X|X||
|Zwift Play & Click support|X|||||
|MQTT integration|X|X|X|X||
|OpenSoundControl integration|X|X|X|X||


### Installation 

  You can install it on multiple platforms.
  Read the [installation procedure](docs/10_Installation.md)


### Tested on

  The QDomyos-Zwift application can run on [Macintosh or Linux devices](docs/10_Installation.md) iOS, and Android. 
  It supports any [FTMS-compatible application](docs/20_supported_devices_and_applications.md) software and most [bluetooth enabled device](docs/20_supported_devices_and_applications.md).

### No GUI version

run as

  $ sudo ./qdomyos-zwift -no-gui

### Reference

  => GitHub Repository: [QDomyos-Zwift on GitHub](https://github.com/ProH4Ck/treadmill-bridge)

  => Treadmill Incline Reference: [What Is 10 Degrees in Incline on a Treadmill?](https://www.livestrong.com/article/422012-what-is-10-degrees-in-incline-on-a-treadmill/)

  => Icon Attribution: Icons used in this documentation are from [Flaticon.com](https://www.flaticon.com)

### Translations

  This fork ships two languages: English, which is the source language, and Brazilian
  Portuguese. The picker is in Settings, under Display. See
  [src/translations/README.md](src/translations/README.md).

  Upstream's thirty languages are managed with [Weblate](https://hosted.weblate.org/engage/qdomyos-zwift/)
  and translate `homeform`, which this fork deleted — so they were dropped here rather than
  shipped as catalogues of strings nothing displays. If you want to help translate QZ,
  [do it upstream](https://hosted.weblate.org/engage/qdomyos-zwift/); it reaches the people
  who use it.

### Blog

  => Related Blog: [Roberto Viola's Blog](https://robertoviola.cloud)
