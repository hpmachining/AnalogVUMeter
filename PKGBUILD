# Maintainer: hpmachining <aur at hpminc dot com>
pkgname=analogvumeter
pkgver=1.0.0
pkgrel=1
pkgdesc="A desktop application that visually replicates a classic analog stereo VU meter"
arch=('x86_64')
url="https://github.com/hpmachining/AnalogVUMeter"
license=('MIT')
depends=('qt6-base' 'libpulse' 'libzip')
makedepends=('cmake' 'pkgconf')
optdepends=(
    'pipewire-pulse: PulseAudio-compatible server for audio monitoring'
    'pulseaudio: PulseAudio server for audio monitoring'
)
source=("$pkgname-$pkgver.tar.gz::https://github.com/hpmachining/AnalogVUMeter/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('afa207934d97e5dd19c17b3a719cdb3163469266db44f7d6a5de987c8f9c1c07')

build() {
    cd "AnalogVUMeter-$pkgver"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

package() {
    cd "AnalogVUMeter-$pkgver"
    install -Dm755 "build/analog_vu_meter" "$pkgdir/usr/bin/analog_vu_meter"
    install -Dm644 "LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    install -Dm644 "README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
}

# vim:set ts=4 sw=4 et:
